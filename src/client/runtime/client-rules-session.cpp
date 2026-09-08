#include "client-rules-session.h"

#include "client-room-context.h"
#include "client-selection-runtime.h"
#include "interaction-reply-encoder.h"
#include "protocol/skill-instance-message.h"
#include "server-info.h"
#include "skill-dialog-info.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QSet>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const QString &reason)
{
    if (!condition)
        throw std::runtime_error(reason.toStdString());
}

QJsonObject object(const QJsonValue &value, const QString &name)
{
    require(value.isObject(), QStringLiteral("invalid_") + name);
    return value.toObject();
}

int integer(const QJsonValue &value, int minimum, int maximum)
{
    const double number = value.toDouble();
    require(value.isDouble() && std::isfinite(number) && number == std::floor(number)
        && number >= minimum && number <= maximum, QStringLiteral("invalid_integer"));
    return static_cast<int>(number);
}

QStringList strings(const QJsonValue &value)
{
    if (value.isUndefined())
        return {};
    require(value.isArray(), QStringLiteral("invalid_string_list"));
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        require(entry.isString() && !entry.toString().isEmpty(), QStringLiteral("invalid_string_list"));
        result.append(entry.toString());
    }
    return result;
}

QList<int> cardIds(const QJsonValue &value, int count)
{
    require(value.isArray(), QStringLiteral("invalid_card_ids"));
    QList<int> result;
    for (const QJsonValue &entry : value.toArray()) {
        const int id = integer(entry, 0, count - 1);
        require(!result.contains(id), QStringLiteral("duplicate_card_ids"));
        result.append(id);
    }
    return result;
}

QJsonArray jsonIds(const QList<int> &ids)
{
    QJsonArray result;
    for (int id : ids)
        result.append(id);
    return result;
}

struct Scene
{
    ClientGameState state;
    ClientRoomContext room{&state};
    ClientPlayerModel players{&state};

    ~Scene()
    {
        // This Worker runs one synchronous query at a time. Dispose temporary
        // ViewAs/declaration cards while their projected players still exist.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

void loadScene(const QJsonObject &data, Scene &scene)
{
    const int count = Sanguosha->getCardCount();
    require(integer(data.value(QStringLiteral("card_id_space")), 1,
                    std::numeric_limits<int>::max()) == count,
            QStringLiteral("card_registry_mismatch"));
    scene.state.setCardIdSpace(count);
    const QJsonObject connection = object(data.value(QStringLiteral("connection")), QStringLiteral("connection"));
    for (auto it = connection.constBegin(); it != connection.constEnd(); ++it)
        scene.state.setConnectionValue(it.key(), it.value().toVariant());
    QJsonObject setup = object(data.value(QStringLiteral("setup")), QStringLiteral("setup"));
    // The shared Player model uses mode; SETUP names that value game_mode.
    if (setup.contains(QStringLiteral("game_mode")))
        setup.insert(QStringLiteral("mode"), setup.value(QStringLiteral("game_mode")));
    scene.state.setSetup(setup.toVariantMap());
    // Native skills also consult the same setup singleton as the desktop.
    // Assign all gameplay fields per query so a previous scene cannot leak.
    ServerInfo.GameMode = setup.value(QStringLiteral("mode")).toString();
    ServerInfo.GameRuleMode = setup.value(QStringLiteral("game_rule_mode")).toString();
    ServerInfo.BanPackages = strings(setup.value(QStringLiteral("ban_packages")));
    ServerInfo.Enable2ndGeneral = setup.value(QStringLiteral("enable_second_general")).toBool();
    ServerInfo.EnableSame = setup.value(QStringLiteral("enable_same")).toBool();
    ServerInfo.EnableBasara = setup.value(QStringLiteral("enable_basara")).toBool();
    ServerInfo.EnableHegemony = setup.value(QStringLiteral("enable_hegemony")).toBool();
    ServerInfo.EnableMeleeMode = setup.value(QStringLiteral("enable_melee_mode")).toBool();
    ServerInfo.MaxHpScheme = setup.value(QStringLiteral("max_hp_scheme")).toInt();
    ServerInfo.Scheme0Subtraction = setup.value(QStringLiteral("scheme0_subtraction")).toInt();
    ServerInfo.DuringGame = true;
    const QJsonObject game = object(data.value(QStringLiteral("game")), QStringLiteral("game"));
    for (auto it = game.constBegin(); it != game.constEnd(); ++it)
        scene.state.setGameValue(it.key(), it.value().toVariant());
    const QString self = data.value(QStringLiteral("self_name")).toString();
    const QStringList names = strings(data.value(QStringLiteral("player_names")));
    require(!self.isEmpty() && names.contains(self) && names.size() <= 64,
            QStringLiteral("incomplete_player_state"));
    QSet<QString> uniqueNames;
    for (const QString &name : names) {
        require(!uniqueNames.contains(name), QStringLiteral("duplicate_player"));
        uniqueNames.insert(name);
    }
    require(data.value(QStringLiteral("players")).isArray(), QStringLiteral("invalid_players"));
    QSet<QString> suppliedNames;
    for (const QJsonValue &entry : data.value(QStringLiteral("players")).toArray()) {
        const QJsonObject person = object(entry, QStringLiteral("player"));
        const QString name = person.value(QStringLiteral("object_name")).toString();
        require(names.contains(name) && !suppliedNames.contains(name)
                && !person.contains(QStringLiteral("objectName")),
                QStringLiteral("invalid_player_identity"));
        suppliedNames.insert(name);
        for (const QString &field : {QStringLiteral("general"), QStringLiteral("deputy_general")}) {
            const QString general = person.value(field).toString();
            require(general.isEmpty() || Sanguosha->getGeneral(general) != nullptr,
                    QStringLiteral("unsupported_general:") + general);
        }
        for (const QString &skill : strings(person.value(QStringLiteral("skills"))))
            require(Sanguosha->getSkill(skill) != nullptr, QStringLiteral("unsupported_skill:") + skill);
        const QJsonObject generalPiles = person.value(QStringLiteral("general_piles")).toObject();
        for (auto pile = generalPiles.constBegin(); pile != generalPiles.constEnd(); ++pile) {
            for (const QString &general : strings(pile.value()))
                require(Sanguosha->getGeneral(general) != nullptr,
                        QStringLiteral("unsupported_general:") + general);
        }
        for (auto it = person.constBegin(); it != person.constEnd(); ++it) {
            const QVariant value = it.key() == QLatin1String("skills") || it.key() == QLatin1String("flags")
                ? QVariant(strings(it.value())) : it.value().toVariant();
            scene.state.setPlayerValue(name, it.key(), value);
        }
    }
    require(suppliedNames == uniqueNames, QStringLiteral("incomplete_player_state"));
    scene.state.setPlayerNames(names);
    scene.state.setSelfName(self);
    require(data.value(QStringLiteral("cards")).isArray(), QStringLiteral("invalid_cards"));
    QSet<int> suppliedIds;
    for (const QJsonValue &entry : data.value(QStringLiteral("cards")).toArray()) {
        const QJsonObject card = object(entry, QStringLiteral("card"));
        const int id = integer(card.value(QStringLiteral("id")), 0, count - 1);
        require(!suppliedIds.contains(id), QStringLiteral("duplicate_card"));
        suppliedIds.insert(id);
        if (card.contains(QStringLiteral("place")))
            integer(card.value(QStringLiteral("place")), Player::PlaceHand, Player::PlaceWuGu);
        const QString owner = card.value(QStringLiteral("owner")).toString();
        require(owner.isEmpty() || names.contains(owner), QStringLiteral("unknown_card_owner"));
        for (auto it = card.constBegin(); it != card.constEnd(); ++it) {
            const QVariant value = it.key() == QLatin1String("flags")
                ? QVariant(strings(it.value())) : it.value().toVariant();
            scene.state.setCardValue(id, it.key(), value);
        }
    }
    for (const QString &name : names) {
        const QVariantMap piles = scene.state.playerValue(name, QStringLiteral("piles")).toMap();
        for (auto pile = piles.constBegin(); pile != piles.constEnd(); ++pile) {
            for (const QVariant &value : pile.value().toList()) {
                const int id = value.toInt();
                if (id == Card::S_UNKNOWN_CARD_ID)
                    continue;
                require(id >= 0 && id < count, QStringLiteral("unsupported_pile_card"));
                // SYNC_PILE can provide an identity without a MOVE event. Its
                // named owner is enough to project that visible special zone.
                if (!scene.state.card(id).contains(QStringLiteral("place"))) {
                    scene.state.setCardValue(id, QStringLiteral("owner"), name);
                    scene.state.setCardValue(id, QStringLiteral("place"), Player::PlaceSpecial);
                }
            }
        }
    }
    scene.room.setOwnerResolver([&scene](int id) { return scene.players.cardOwner(id); });
    scene.room.enterGame();
    // Fresh RoomState means modified=false always restores the printed card,
    // even when the JS reducer retains old UPDATE_CARD fields after a reset.
    for (int id : suppliedIds) {
        const QVariantMap data = scene.state.card(id);
        if (data.value(QStringLiteral("modified")).toBool()) {
            const QString name = data.value(QStringLiteral("card_name")).toString();
            WrappedCard *wrapped = qobject_cast<WrappedCard *>(scene.room.card(id));
            require(wrapped != nullptr, QStringLiteral("incomplete_card_state"));
            // UPDATE_CARD carries the class name (e.g. Slash), whereas hasCard
            // searches deck object names. Use the shared clone/adoption path
            // and reject a failed clone instead of retaining the printed card.
            Card *updated = Sanguosha->cloneCard(name,
                static_cast<Card::Suit>(data.value(QStringLiteral("suit")).toInt()),
                data.value(QStringLiteral("number")).toInt(),
                data.value(QStringLiteral("flags")).toStringList());
            require(updated != nullptr, QStringLiteral("unsupported_card:") + name);
            updated->setId(id);
            updated->setSkillName(data.value(QStringLiteral("skill_name")).toString());
            const QString objectName = data.value(QStringLiteral("object_name")).toString();
            if (!objectName.isEmpty())
                updated->setObjectName(objectName);
            wrapped->copyEverythingFrom(updated);
            require(wrapped->getRealCard() == updated, QStringLiteral("card_update_failed"));
        }
        Card *card = scene.room.card(id);
        require(card != nullptr, QStringLiteral("incomplete_card_state"));
        if (data.contains(QStringLiteral("flags"))) {
            card->setFlags(QStringLiteral("."));
            for (const QString &flag : data.value(QStringLiteral("flags")).toStringList())
                card->setFlags(flag);
        }
        const QVariantMap marks = data.value(QStringLiteral("marks")).toMap();
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
            card->setMark(it.key(), it.value().toInt());
    }
    scene.players.sync();
    for (const QString &name : names) {
        ClientPlayer *player = scene.players.player(name);
        const QVariantMap data = scene.state.player(name);
        player->applyVisibleZones(data);
        const QVariantMap instances = data.value(QStringLiteral("skill_instances")).toMap();
        if (data.contains(QStringLiteral("skill_instances")))
            player->clearSkillInstances();
        for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
            SkillInstanceEntryMessage instance;
            require(instance.tryParse(it.value()) && instance.ownerName == name,
                    QStringLiteral("invalid_skill_instance"));
            require(Sanguosha->getSkill(instance.instance.skillName) != nullptr,
                    QStringLiteral("unsupported_skill:") + instance.instance.skillName);
            player->upsertSkillInstance(instance.instance);
            player->setSkillInstanceState(instance.instance.skillName,
                instance.instance.instanceID, instance.privateState);
        }
        // ATTACH_SKILL and UI-derived visible skills can exist alongside an
        // instance snapshot. Do not lose them when replacing its instances.
        for (const QString &skill : data.value(QStringLiteral("skills")).toStringList()) {
            if (player->getSkillInstanceIds(skill).isEmpty())
                player->addSkill(skill);
        }
        const QVariantMap distances = data.value(QStringLiteral("fixed_distances")).toMap();
        for (auto distance = distances.constBegin(); distance != distances.constEnd(); ++distance) {
            const Player *other = scene.players.player(distance.key());
            require(other != nullptr, QStringLiteral("unknown_distance_player"));
            player->setFixedDistance(other, distance.value().toInt());
        }
        for (const QVariant &otherName : data.value(QStringLiteral("attack_range_pairs")).toList()) {
            const Player *other = scene.players.player(otherName.toString());
            require(other != nullptr, QStringLiteral("unknown_range_player"));
            player->insertAttackRangePair(other);
        }
    }
    const QVariant handCount = scene.state.playerValue(self, QStringLiteral("hand_count"));
    require(!handCount.isValid()
            || handCount.toInt() == scene.state.cardsForPlayer(self, Player::PlaceHand).size(),
            QStringLiteral("incomplete_self_hand"));
}

struct Prompt
{
    InteractionRequest request;
    CardInteractionPayload cards;
    CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    const Player *fixedTarget = nullptr;
    bool targetsOwnedByPrompt = false;
    bool nonUseResponse = false;
};

Prompt makePrompt(const QJsonObject &input, const Scene &scene, quint64 requestId)
{
    using namespace QSanProtocol;
    Prompt prompt;
    prompt.request.requestId = requestId;
    prompt.request.command = integer(input.value(QStringLiteral("command")), 0,
                                    std::numeric_limits<int>::max());
    const QJsonObject payload = object(input.value(QStringLiteral("payload")), QStringLiteral("payload"));
    switch (prompt.request.command) {
    case S_COMMAND_PLAY_CARD:
        prompt.request.type = InteractionType::PlayCard;
        prompt.cards.selection.handlingMethod = Card::MethodUse;
        break;
    case S_COMMAND_RESPONSE_CARD:
        prompt.request.type = InteractionType::ResponseCard;
        require(payload.value(QStringLiteral("pattern")).isString(), QStringLiteral("invalid_pattern"));
        prompt.cards.selection.pattern = payload.value(QStringLiteral("pattern")).toString();
        prompt.cards.selection.handlingMethod = payload.contains(QStringLiteral("handling_method"))
            ? integer(payload.value(QStringLiteral("handling_method")), Card::MethodNone, Card::MethodGet)
            : Card::MethodResponse;
        prompt.nonUseResponse = prompt.cards.selection.handlingMethod != Card::MethodUse
            && prompt.cards.selection.handlingMethod != Card::MethodResponse
            && prompt.cards.selection.handlingMethod != Card::MethodPlay;
        break;
    case S_COMMAND_ASK_PEACH: {
        prompt.request.type = InteractionType::AskPeach;
        const QString dying = payload.value(QStringLiteral("dying_player")).toString();
        prompt.fixedTarget = scene.players.player(dying);
        require(prompt.fixedTarget != nullptr, QStringLiteral("missing_dying_player"));
        prompt.cards.selection.pattern = dying == scene.state.selfName()
            ? QStringLiteral("peach+analeptic") : QStringLiteral("peach");
        prompt.cards.selection.handlingMethod = Card::MethodUse;
        prompt.targetsOwnedByPrompt = true;
        break;
    }
    case S_COMMAND_NULLIFICATION:
        prompt.request.type = InteractionType::Nullification;
        prompt.cards.selection.pattern = QStringLiteral("nullification");
        prompt.cards.selection.handlingMethod = Card::MethodUse;
        prompt.targetsOwnedByPrompt = true;
        break;
    default:
        throw std::runtime_error("unsupported_command");
    }
    prompt.reason = ClientRules::skillPromptReason(prompt.request.type,
        prompt.cards.selection.handlingMethod, prompt.cards.selection.pattern);
    // Ordinary responses (e.g. Slash for Duel) answer without target picking.
    if (prompt.request.type == InteractionType::ResponseCard
        && prompt.cards.selection.handlingMethod == Card::MethodResponse)
        prompt.targetsOwnedByPrompt = true;
    prompt.cards.selection.minSelection = 1;
    prompt.cards.selection.maxSelection = 1;
    prompt.cards.cardTextAllowed = true;
    prompt.cards.virtualCardAllowed = true;
    prompt.cards.optionalTargets = scene.state.playerNames();
    prompt.request.payload = prompt.cards;
    return prompt;
}

QList<int> physicalCardPool(const Scene &scene, const Prompt &prompt)
{
    QList<int> result = scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceHand);
    // Dashboard exposes equips to ResponseSkill; only use/response/play expand
    // hand piles. Neutral and discard requests must not consume those piles.
    if (prompt.reason != CardUseStruct::CARD_USE_REASON_PLAY)
        result.append(scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceEquip));
    if (!prompt.nonUseResponse)
        result.append(scene.players.self()->getHandPile());
    return result;
}

QList<int> skillCardPool(const ViewAsSkill *skill, const Scene &scene)
{
    QList<int> result = scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceHand);
    result.append(scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceEquip));
    if (skill->isResponseOrUse())
        result.append(scene.players.self()->getHandPile());
    // This existing native API handles named, % sibling and / equip piles.
    // A permissive viewFilter alone cannot make an unrelated pile selectable.
    result.append(skill->getExpandPileCardIds(scene.players.self()));
    QList<int> unique;
    for (int id : result) {
        if (id >= 0 && id < Sanguosha->getCardCount() && !unique.contains(id))
            unique.append(id);
    }
    return unique;
}

QString cardRestriction(const Card *card, const Prompt &prompt, const Scene &scene)
{
    const Player *self = scene.players.self();
    Card::HandlingMethod method = card->getHandlingMethod();
    if (prompt.nonUseResponse)
        method = prompt.cards.selection.handlingMethod == Card::MethodDiscard
            ? Card::MethodDiscard : Card::MethodNone;
    else if (prompt.reason == CardUseStruct::CARD_USE_REASON_PLAY)
        method = Card::MethodUse;
    else if (prompt.reason == CardUseStruct::CARD_USE_REASON_RESPONSE && method == Card::MethodUse)
        method = Card::MethodResponse;
    if (self->isCardLimited(card, method))
        return QStringLiteral("card_limited");
    if (prompt.reason == CardUseStruct::CARD_USE_REASON_PLAY)
        return card->isAvailable(self) ? QString() : QStringLiteral("card_unavailable");
    QString pattern = prompt.cards.selection.pattern;
    if (pattern.endsWith(QLatin1Char('!')))
        pattern.chop(1);
    // Named ViewAs prompts are matched by activation identity, not ExpPattern.
    if (!ClientRules::patternSkillName(pattern).isEmpty()) {
        if (card->getActivationSkillName() != ClientRules::patternSkillName(pattern))
            return QStringLiteral("pattern_mismatch");
    } else if (!pattern.isEmpty() && pattern != QLatin1String(".")
               && !Sanguosha->matchPattern(pattern, self, card)) {
        return QStringLiteral("pattern_mismatch");
    }
    if (prompt.fixedTarget != nullptr && Sanguosha->isProhibited(self, prompt.fixedTarget, card))
        return QStringLiteral("target_prohibited");
    return {};
}

bool nextSubcard(const ClientRules::SkillCardBuildRequest &draft,
                const QList<int> &selected, int id, const Scene &scene)
{
    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(draft.skillName);
    if (selected.contains(id) || skill == nullptr || !skillCardPool(skill, scene).contains(id))
        return false;
    const Card *card = Sanguosha->getCard(id, false);
    if (skill == nullptr || card == nullptr)
        return false;
    if (const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill)) {
        ActiveSkillRequest request;
        request.reason = Sanguosha->getCurrentCardUseReason();
        request.pattern = Sanguosha->getCurrentCardUsePattern();
        request.initiator = scene.players.self();
        request.activationRef = SkillInstanceRef(scene.state.selfName(),
            SkillInstanceKey(draft.skillName, draft.instanceId));
        request.selectedCardIds = selected;
        request.selectedTargetNames = draft.selectedTargets;
        request.userString = draft.userString;
        return v2->canSelectCard(request, card);
    }
    QList<const Card *> cards;
    for (int selectedId : selected)
        cards.append(Sanguosha->getCard(selectedId, false));
    return skill->viewFilter(cards, card);
}

QString applyDeclaration(const ClientRules::SkillCardBuildRequest &draft,
                         const Prompt &prompt, Scene &scene)
{
    const Skill *skill = Sanguosha->getSkill(draft.skillName);
    SkillDialogInfo info = skill->getDialogInfo();
    if (!info.isValid())
        info = Sanguosha->getViewAsSkill(draft.skillName)->getDialogInfo();
    if (!info.isValid())
        return {};
    const QVariantMap params = info.parameters;
    const bool play = prompt.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    const QString names = params.value(QStringLiteral("cardNames")).toString();
    const bool active = info.type == QLatin1String("guhuo")
        ? !params.value(QStringLiteral("playOnly"), true).toBool() || play
        : info.type == QLatin1String("juguan")
            ? !names.isEmpty() && (names.endsWith(QLatin1Char('!')) || play)
            : true;
    if (!active)
        return {};
    if (draft.userString.isEmpty())
        return QStringLiteral("declaration_required");
    const QString key = info.objectName.isEmpty() ? draft.skillName : info.objectName;
    if (info.type == QLatin1String("tiansuan")) {
        if (!params.value(QStringLiteral("choices")).toString().split(QLatin1Char(','))
                .contains(draft.userString))
            return QStringLiteral("invalid_declaration");
        const QString prefix = key + QStringLiteral("_tiansuan_remove_") + draft.userString;
        for (const QString &mark : scene.players.self()->getMarkNames()) {
            if (mark.startsWith(prefix) && scene.players.self()->getMark(mark) > 0)
                return QStringLiteral("invalid_declaration");
        }
        scene.players.self()->setTag(key, draft.userString);
        return {};
    }
    require(info.type == QLatin1String("guhuo") || info.type == QLatin1String("juguan"),
            QStringLiteral("unsupported_skill_dialog:") + info.type);
    Card *card = Sanguosha->cloneCard(draft.userString);
    if (card == nullptr)
        return QStringLiteral("invalid_declaration");
    card->QObject::deleteLater();
    card->setSkillName(key);
    card->setCanRecast(false);
    bool allowed = !scene.players.self()->isLocked(card);
    if (info.type == QLatin1String("juguan")) {
        QString choices = names;
        choices.remove(QLatin1Char('!'));
        choices.remove(QLatin1Char('$'));
        allowed = allowed && choices.split(QLatin1Char(',')).contains(draft.userString)
            && (names.startsWith(QLatin1Char('$')) || !play || card->isAvailable(scene.players.self()));
    } else {
        bool registered = false;
        const QStringList banned = scene.state.setup().value(QStringLiteral("ban_packages")).toStringList();
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *printed = Sanguosha->getEngineCard(id);
            if (printed->objectName() == card->objectName()
                && !banned.contains(printed->getPackage()))
                registered = true;
        }
        allowed = allowed && registered && !card->objectName().startsWith(QLatin1Char('_'))
            && ((card->getTypeId() == Card::TypeBasic && params.value(QStringLiteral("left"), true).toBool())
                || (card->getTypeId() == Card::TypeTrick && params.value(QStringLiteral("right"), true).toBool()
                    && (card->isNDTrick() || params.value(QStringLiteral("delayedTricks"), false).toBool())))
            && (!params.value(QStringLiteral("slashCombined"), false).toBool()
                || !card->isKindOf("Slash") || card->objectName() == QLatin1String("slash"))
            && (!(params.value(QStringLiteral("playOnly"), true).toBool() || play)
                || card->isAvailable(scene.players.self()));
    }
    if (!allowed)
        return QStringLiteral("invalid_declaration");
    scene.players.self()->setTag(key, QVariant::fromValue(static_cast<const Card *>(card)));
    return {};
}

QStringList declarations(const ClientRules::SkillCardBuildRequest &draft,
                         const Prompt &prompt, Scene &scene)
{
    const Skill *skill = Sanguosha->getSkill(draft.skillName);
    SkillDialogInfo info = skill->getDialogInfo();
    if (!info.isValid())
        info = Sanguosha->getViewAsSkill(draft.skillName)->getDialogInfo();
    if (!info.isValid())
        return {};
    const bool play = prompt.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    const QString names = info.parameters.value(QStringLiteral("cardNames")).toString();
    if ((info.type == QLatin1String("guhuo")
            && info.parameters.value(QStringLiteral("playOnly"), true).toBool() && !play)
        || (info.type == QLatin1String("juguan")
            && (names.isEmpty() || (!names.endsWith(QLatin1Char('!')) && !play))))
        return {};
    QStringList candidates;
    if (info.type == QLatin1String("tiansuan")) {
        candidates = info.parameters.value(QStringLiteral("choices")).toString()
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    } else if (info.type == QLatin1String("juguan")) {
        QString choices = names;
        choices.remove(QLatin1Char('!'));
        choices.remove(QLatin1Char('$'));
        candidates = choices.split(QLatin1Char(','), Qt::SkipEmptyParts);
    } else {
        require(info.type == QLatin1String("guhuo"),
                QStringLiteral("unsupported_skill_dialog:") + info.type);
        for (int id = 0; id < Sanguosha->getCardCount(); ++id)
            candidates.append(Sanguosha->getEngineCard(id)->objectName());
    }
    candidates.removeDuplicates();
    QStringList result;
    for (const QString &candidate : candidates) {
        ClientRules::SkillCardBuildRequest option = draft;
        option.userString = candidate.trimmed();
        if (applyDeclaration(option, prompt, scene).isEmpty())
            result.append(option.userString);
    }
    // Probes above install native declaration tags. Clear that preview before
    // applying the actual user choice, including the unselected state.
    scene.players.self()->removeTag(info.objectName.isEmpty() ? draft.skillName : info.objectName);
    return result;
}
} // namespace

QJsonObject ClientRulesSession::registry() const
{
    require(Sanguosha != nullptr, QStringLiteral("engine_unavailable"));
    QJsonArray cards;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        require(card != nullptr, QStringLiteral("incomplete_card_registry"));
        cards.append(QJsonObject{{QStringLiteral("id"), id},
            {QStringLiteral("object_name"), card->objectName()},
            {QStringLiteral("suit"), static_cast<int>(card->getSuit())},
            {QStringLiteral("number"), card->getNumber()},
            {QStringLiteral("class_name"), card->getClassName()},
            {QStringLiteral("package"), card->getPackage()}});
    }
    return {{QStringLiteral("schema_version"), 1},
            {QStringLiteral("card_count"), Sanguosha->getCardCount()},
            {QStringLiteral("registry"), cards}};
}

QJsonObject ClientRulesSession::evaluate(const QJsonObject &input) const
{
    QJsonObject output{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("generation"), input.value(QStringLiteral("generation"))},
        {QStringLiteral("revision"), input.value(QStringLiteral("revision"))},
        {QStringLiteral("request_id"), input.value(QStringLiteral("request_id"))},
        {QStringLiteral("known"), false}, {QStringLiteral("reason"), QString()},
        {QStringLiteral("can_confirm"), false}, {QStringLiteral("card_text"), QString()},
        {QStringLiteral("selectable_cards"), QJsonArray()}, {QStringLiteral("skills"), QJsonArray()},
        {QStringLiteral("declarations"), QJsonArray()},
        {QStringLiteral("next_targets"), QJsonObject{
            {QStringLiteral("candidates"), QJsonArray()}, {QStringLiteral("max_votes"), QJsonObject()}}},
        {QStringLiteral("wire"), QJsonValue(QJsonValue::Null)}};
    try {
        require(Sanguosha != nullptr && Sanguosha->currentRoomContext() == nullptr,
                QStringLiteral("engine_unavailable"));
        integer(input.value(QStringLiteral("schema_version")), 1, 1);
        integer(input.value(QStringLiteral("generation")), 0, std::numeric_limits<int>::max());
        integer(input.value(QStringLiteral("revision")), 0, std::numeric_limits<int>::max());
        const QString idText = input.value(QStringLiteral("request_id")).toString();
        static const QRegularExpression idPattern(QStringLiteral("^[1-9][0-9]*$"));
        bool validId = false;
        const quint64 id = idText.toULongLong(&validId);
        require(validId && idPattern.match(idText).hasMatch(), QStringLiteral("invalid_request_id"));
        Scene scene;
        loadScene(object(input.value(QStringLiteral("state")), QStringLiteral("state")), scene);
        const Prompt prompt = makePrompt(input, scene, id);
        // UNKNOWN is the native context for neutral/discard physical responses.
        scene.room.setCardUseContext(prompt.reason, prompt.cards.selection.pattern);
        const QJsonObject selection = object(input.value(QStringLiteral("selection")), QStringLiteral("selection"));
        const QList<int> selectedCards = cardIds(selection.value(QStringLiteral("card_ids")),
                                                 Sanguosha->getCardCount());
        ClientRules::CardSelectionDraft draft;
        draft.selectedTargets = strings(selection.value(QStringLiteral("targets")));
        draft.targetPool = scene.state.playerNames();
        draft.skill.selfName = scene.state.selfName();
        draft.skill.skillName = selection.value(QStringLiteral("skill_name")).toString();
        draft.skill.instanceId = selection.contains(QStringLiteral("skill_instance_id"))
            ? integer(selection.value(QStringLiteral("skill_instance_id")), 0, std::numeric_limits<int>::max()) : 0;
        draft.skill.userString = selection.value(QStringLiteral("user_string")).toString();
        draft.skill.selectedTargets = draft.selectedTargets;
        draft.skill.subcardIds = selectedCards;
        const bool usingSkill = !draft.skill.skillName.isEmpty();
        CardInteractionPayload candidates;
        ClientRules::fillSkillCandidates(scene.state, prompt.cards.selection.pattern, &candidates);
        QJsonArray skills;
        bool selectedSkillAvailable = false;
        bool selectedSkillKnown = false;
        for (const SkillActivationCandidate &candidate : candidates.skillCandidates) {
            const auto activation = ClientRules::evaluateSkillActivation(candidate.skillName,
                candidate.instanceId, prompt.reason, prompt.cards.selection.pattern);
            skills.append(QJsonObject{{QStringLiteral("name"), candidate.skillName},
                {QStringLiteral("instance_id"), candidate.instanceId},
                {QStringLiteral("available"), activation.known && activation.available}});
            if (candidate.skillName == draft.skill.skillName
                && candidate.instanceId == draft.skill.instanceId) {
                selectedSkillKnown = true;
                selectedSkillAvailable = activation.known && activation.available;
            }
        }
        const QString namedSkill = ClientRules::patternSkillName(prompt.cards.selection.pattern);
        require(!prompt.cards.selection.pattern.startsWith(QLatin1Char('@')) || !namedSkill.isEmpty(),
                QStringLiteral("unsupported_skill_pattern"));
        require(namedSkill.isEmpty() || Sanguosha->getViewAsSkill(namedSkill) != nullptr,
                QStringLiteral("unsupported_skill:") + namedSkill);
        output.insert(QStringLiteral("skills"), skills);
        output.insert(QStringLiteral("known"), true);
        if (usingSkill) {
            require(Sanguosha->getViewAsSkill(draft.skill.skillName) != nullptr,
                    QStringLiteral("unsupported_skill:") + draft.skill.skillName);
            if (!selectedSkillKnown || !selectedSkillAvailable) {
                output.insert(QStringLiteral("reason"), QStringLiteral("skill_unavailable"));
                return output;
            }
            const QStringList options = declarations(draft.skill, prompt, scene);
            output.insert(QStringLiteral("declarations"), QJsonArray::fromStringList(options));
            const QString declaration = options.isEmpty() && draft.skill.userString.isEmpty()
                ? QString() : applyDeclaration(draft.skill, prompt, scene);
            if (!declaration.isEmpty()) {
                output.insert(QStringLiteral("reason"), declaration);
                return output;
            }
            QList<int> prefix;
            for (int selected : selectedCards) {
                if (!nextSubcard(draft.skill, prefix, selected, scene)) {
                    output.insert(QStringLiteral("reason"), QStringLiteral("subcard_rejected"));
                    return output;
                }
                prefix.append(selected);
            }
            QList<int> next;
            for (int candidate : skillCardPool(Sanguosha->getViewAsSkill(draft.skill.skillName), scene)) {
                if (nextSubcard(draft.skill, selectedCards, candidate, scene))
                    next.append(candidate);
            }
            output.insert(QStringLiteral("selectable_cards"), jsonIds(next));
        } else {
            QList<int> selectable;
            for (int candidate : physicalCardPool(scene, prompt)) {
                if (candidate < 0 || candidate >= Sanguosha->getCardCount() || selectable.contains(candidate))
                    continue;
                const Card *card = Sanguosha->getCard(candidate, false);
                // ResponseSkill checks the request method before RoomScene
                // checks the resulting card's own handling method.
                const Card::HandlingMethod responseMethod = prompt.nonUseResponse
                    ? (prompt.cards.selection.handlingMethod == Card::MethodDiscard
                        ? Card::MethodDiscard : Card::MethodNone)
                    : static_cast<Card::HandlingMethod>(prompt.cards.selection.handlingMethod);
                if (card != nullptr && cardRestriction(card, prompt, scene).isEmpty()
                    && (prompt.reason == CardUseStruct::CARD_USE_REASON_PLAY
                        || !scene.players.self()->isCardLimited(card, responseMethod)))
                    selectable.append(candidate);
            }
            output.insert(QStringLiteral("selectable_cards"), jsonIds(selectable));
            if (selectedCards.size() != 1) {
                output.insert(QStringLiteral("reason"), QStringLiteral("select_one_card"));
                return output;
            }
            draft.cardId = selectedCards.first();
            draft.skill.subcardIds.clear();
            if (!selectable.contains(draft.cardId)) {
                output.insert(QStringLiteral("reason"), QStringLiteral("card_unavailable"));
                return output;
            }
        }
        const ClientRules::PlayerLookup lookup = [&scene](const QString &name) {
            return scene.players.player(name);
        };
        for (const QString &target : draft.selectedTargets) {
            require(draft.targetPool.contains(target), QStringLiteral("unknown_target"));
        }
        auto evaluated = ClientRules::evaluateCardSelection(draft, lookup, scene.players.self());
        if (!evaluated.known) {
            output.insert(QStringLiteral("known"), false);
            output.insert(QStringLiteral("reason"), QStringLiteral("incomplete_selection_state"));
            return output;
        }
        if (!evaluated.cardReady || evaluated.nativeCard == nullptr) {
            output.insert(QStringLiteral("reason"), QStringLiteral("incomplete_card_selection"));
            return output;
        }
        output.insert(QStringLiteral("card_text"), evaluated.cardText);
        if (evaluated.nativeCard->getTypeId() != Card::TypeSkill) {
            QStringList aliveTargets;
            for (const QString &name : draft.targetPool) {
                const Player *player = lookup(name);
                if (player->isAlive() && !player->isRemoved())
                    aliveTargets.append(name);
            }
            for (const QString &name : draft.selectedTargets) {
                if (!aliveTargets.contains(name)) {
                    output.insert(QStringLiteral("reason"), QStringLiteral("target_unavailable"));
                    return output;
                }
            }
            evaluated.nextTargets = ClientRules::targetStep(evaluated.nativeCard,
                draft.selectedTargets, aliveTargets, lookup, scene.players.self());
        }
        const QString restriction = cardRestriction(evaluated.nativeCard, prompt, scene);
        if (!restriction.isEmpty()) {
            output.insert(QStringLiteral("reason"), restriction);
            return output;
        }
        if (prompt.targetsOwnedByPrompt || evaluated.nextTargets.fixed
            || (prompt.nonUseResponse && evaluated.nativeCard->getTypeId() != Card::TypeSkill)) {
            evaluated.canConfirm = draft.selectedTargets.isEmpty();
            evaluated.nextTargets.candidates.clear();
            evaluated.nextTargets.maxVotes.clear();
        }
        QJsonObject votes;
        for (auto it = evaluated.nextTargets.maxVotes.constBegin();
             it != evaluated.nextTargets.maxVotes.constEnd(); ++it)
            votes.insert(it.key(), it.value());
        output.insert(QStringLiteral("next_targets"), QJsonObject{
            {QStringLiteral("candidates"), QJsonArray::fromStringList(evaluated.nextTargets.candidates)},
            {QStringLiteral("max_votes"), votes}});
        if (!evaluated.canConfirm) {
            output.insert(QStringLiteral("reason"), QStringLiteral("incomplete_targets"));
            return output;
        }
        const auto response = ClientRules::makeCardSelectionResponse(prompt.request, draft, evaluated);
        const auto wire = InteractionReplyEncoder::cardResponse(prompt.request, response);
        require(wire.command != QSanProtocol::S_COMMAND_UNKNOWN && wire.replyTo == id,
                QStringLiteral("reply_encoding_failed"));
        output.insert(QStringLiteral("can_confirm"), true);
        output.insert(QStringLiteral("wire"), QJsonObject{
            {QStringLiteral("command"), static_cast<int>(wire.command)},
            {QStringLiteral("reply_to"), QString::number(wire.replyTo)},
            {QStringLiteral("payload"), QJsonValue::fromVariant(wire.payload)}});
    } catch (const std::exception &error) {
        output.insert(QStringLiteral("known"), false);
        output.insert(QStringLiteral("reason"), QString::fromUtf8(error.what()));
        output.insert(QStringLiteral("can_confirm"), false);
        output.insert(QStringLiteral("wire"), QJsonValue(QJsonValue::Null));
    } catch (...) {
        output.insert(QStringLiteral("known"), false);
        output.insert(QStringLiteral("reason"), QStringLiteral("native_rules_failed"));
        output.insert(QStringLiteral("can_confirm"), false);
        output.insert(QStringLiteral("wire"), QJsonValue(QJsonValue::Null));
    }
    return output;
}
