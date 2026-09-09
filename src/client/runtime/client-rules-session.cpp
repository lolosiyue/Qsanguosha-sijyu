#include "client-rules-session.h"
#include "rules-bundle-exporter.h"
#include "runtime-paths.h"
#include "protocol/rules-bundle-identity.h"

#include "client-room-context.h"
#include "client-selection-runtime.h"
#include "game-rng.h"
#include "interaction-command-registry.h"
#include "interaction-reply-encoder.h"
#include "protocol/skill-instance-message.h"
#include "server-info.h"
#include "skill-dialog-info.h"
#include "skill-instance-utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
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

// An absent list is an empty selection, not a malformed query: the browser
// only sends the dimensions the current prompt actually has.
QList<int> optionalCardIds(const QJsonValue &value, int count)
{
    if (value.isUndefined() || value.isNull())
        return {};
    return cardIds(value, count);
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
        const QJsonObject instances = person.value(QStringLiteral("skill_instances")).toObject();
        for (auto instance = instances.constBegin(); instance != instances.constEnd(); ++instance) {
            SkillInstanceEntryMessage instanceEntry;
            require(instanceEntry.tryParse(instance.value().toVariant()) && instanceEntry.ownerName == name,
                    QStringLiteral("invalid_skill_instance"));
            require(Sanguosha->getSkill(instanceEntry.instance.skillName) != nullptr,
                    QStringLiteral("unsupported_skill:") + instanceEntry.instance.skillName);
        }
        const QJsonObject distances = person.value(QStringLiteral("fixed_distances")).toObject();
        for (auto distance = distances.constBegin(); distance != distances.constEnd(); ++distance)
            require(names.contains(distance.key()), QStringLiteral("unknown_distance_player"));
        if (person.contains(QStringLiteral("attack_range_pairs"))) {
            for (const QString &other : strings(person.value(QStringLiteral("attack_range_pairs"))))
                require(names.contains(other), QStringLiteral("unknown_range_player"));
        }
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
    require(scene.room.projectStateCards(), QStringLiteral("card_update_failed"));
    scene.players.sync();
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
    // Enumerated prompts answer out of a set the Room already sent. They never
    // build a ViewAs card and never re-implement the skill's effect here; the
    // shared ClientCore payload owns the set, the counts and the reply shape.
    bool enumerated = false;
};

QList<int> discardSelectableCards(const Scene &scene, const QString &pattern,
                                  bool includeEquip, bool applyDiscardLimit)
{
    QList<int> result = scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceHand);
    if (includeEquip)
        result.append(scene.state.cardsForPlayer(scene.state.selfName(), Player::PlaceEquip));
    const Player *self = scene.players.self();
    for (auto it = result.begin(); it != result.end();) {
        const Card *card = Sanguosha->getCard(*it, false);
        if (card == nullptr || (applyDiscardLimit
                && self->isCardLimited(card, Card::MethodDiscard))
            || !Sanguosha->matchPattern(pattern, self, card))
            it = result.erase(it);
        else
            ++it;
    }
    return result;
}

// The structured request built by ProtocolInteractionRequestBuilder, forwarded
// by ClientRulesIngress. Enumerated prompts read their contract from here so
// the set/count semantics have exactly one implementation.
Prompt makePrompt(const QJsonObject &input, const QJsonObject &interaction,
                  const Scene &scene, quint64 requestId)
{
    using namespace QSanProtocol;
    Prompt prompt;
    prompt.request.requestId = requestId;
    prompt.request.command = integer(input.value(QStringLiteral("command")), 0,
                                    std::numeric_limits<int>::max());
    const QJsonObject payload = object(input.value(QStringLiteral("payload")), QStringLiteral("payload"));
    const QJsonObject typed = interaction.value(QStringLiteral("payload")).isObject()
        ? interaction.value(QStringLiteral("payload")).toObject() : QJsonObject();
    require(!interaction.contains(QStringLiteral("command"))
            || integer(interaction.value(QStringLiteral("command")), 0,
                       std::numeric_limits<int>::max()) == prompt.request.command,
            QStringLiteral("interaction_command_mismatch"));
    prompt.request.cancelable = interaction.value(QStringLiteral("cancelable")).toBool();
    prompt.request.prompt = interaction.value(QStringLiteral("prompt")).toString();
    prompt.request.skillName = interaction.value(QStringLiteral("skill")).toString();
    const int cardCount = Sanguosha->getCardCount();
    switch (prompt.request.command) {
    case S_COMMAND_EXCHANGE_CARD:
    case S_COMMAND_DISCARD_CARD: {
        prompt.request.type = prompt.request.command == S_COMMAND_EXCHANGE_CARD
            ? InteractionType::ExchangeCard : InteractionType::DiscardCard;
        prompt.enumerated = true;
        prompt.request.cancelable = interaction.value(QStringLiteral("cancelable")).toBool()
            || payload.value(QStringLiteral("optional")).toBool();
        prompt.cards.selection.pattern = payload.value(QStringLiteral("pattern")).toString();
        if (prompt.cards.selection.pattern.isEmpty())
            prompt.cards.selection.pattern = QStringLiteral(".");
        prompt.cards.selection.handlingMethod = Card::MethodDiscard;
        prompt.cards.selection.minSelection = integer(payload.value(QStringLiteral("min_cards")), 0,
                                                       std::numeric_limits<int>::max());
        prompt.cards.selection.maxSelection = integer(payload.value(QStringLiteral("max_cards")),
                                                       prompt.cards.selection.minSelection,
                                                       std::numeric_limits<int>::max());
        prompt.cards.includeEquip = payload.value(QStringLiteral("include_equip")).toBool();
        prompt.cards.selection.selectableCards = discardSelectableCards(
            scene, prompt.cards.selection.pattern, prompt.cards.includeEquip,
            prompt.request.type == InteractionType::DiscardCard);
        prompt.cards.selection.enumerated = true;
        prompt.request.payload = prompt.cards;
        break;
    }
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
    case S_COMMAND_SKILL_GUANXING: {
        prompt.request.type = InteractionType::SkillGuanxing;
        prompt.enumerated = true;
        RearrangeCardsInteractionPayload value;
        value.cardIds = cardIds(typed.value(QStringLiteral("cards")), cardCount);
        const QString mode = typed.value(QStringLiteral("mode")).toString();
        value.mode = mode == QLatin1String("up_only") ? RearrangementMode::UpOnly
            : mode == QLatin1String("down_only") ? RearrangementMode::DownOnly
            : RearrangementMode::BothSides;
        const int total = value.cardIds.size();
        value.minTop = integer(typed.value(QStringLiteral("min_top")), 0, total);
        value.maxTop = integer(typed.value(QStringLiteral("max_top")), 0, total);
        value.minBottom = integer(typed.value(QStringLiteral("min_bottom")), 0, total);
        value.maxBottom = integer(typed.value(QStringLiteral("max_bottom")), 0, total);
        value.mirrored = typed.value(QStringLiteral("mirrored")).toBool();
        require(value.minTop <= value.maxTop && value.minBottom <= value.maxBottom
                && value.minTop + value.minBottom <= total
                && value.maxTop + value.maxBottom >= total,
                QStringLiteral("invalid_rearrangement_bounds"));
        prompt.request.payload = value;
        break;
    }
    case S_COMMAND_SKILL_GONGXIN: {
        prompt.request.type = InteractionType::SkillGongxin;
        prompt.enumerated = true;
        GongxinInteractionPayload value;
        value.targetPlayer = typed.value(QStringLiteral("target_player")).toString();
        require(scene.players.player(value.targetPlayer) != nullptr,
                QStringLiteral("unknown_gongxin_target"));
        value.visibleCards = cardIds(typed.value(QStringLiteral("visible_cards")), cardCount);
        value.selectableCards = cardIds(typed.value(QStringLiteral("selectable_cards")), cardCount);
        value.allowHeartOperation = typed.value(QStringLiteral("allow_heart_operation")).toBool();
        prompt.request.payload = value;
        break;
    }
    case S_COMMAND_SKILL_YIJI: {
        prompt.request.type = InteractionType::SkillYiji;
        prompt.enumerated = true;
        YijiInteractionPayload value;
        value.cardIds = cardIds(typed.value(QStringLiteral("cards")), cardCount);
        value.targetPlayers = strings(typed.value(QStringLiteral("target_players")));
        for (const QString &name : value.targetPlayers) {
            require(scene.state.playerNames().contains(name), QStringLiteral("unknown_yiji_target"));
        }
        value.minCards = integer(typed.value(QStringLiteral("min_cards")), 0, value.cardIds.size());
        value.maxCards = integer(typed.value(QStringLiteral("max_cards")), 0, value.cardIds.size());
        value.remainingCount = integer(typed.value(QStringLiteral("remaining_count")), 0,
                                       value.cardIds.size());
        require(value.minCards <= value.maxCards, QStringLiteral("invalid_yiji_bounds"));
        prompt.request.payload = value;
        break;
    }
    default:
        throw std::runtime_error("unsupported_command");
    }
    if (prompt.enumerated) {
        // The shared registry, not this file, decides the reply shape/encoder.
        const auto *descriptor = InteractionCommandRegistry::find(
            static_cast<CommandType>(prompt.request.command));
        require(descriptor != nullptr && descriptor->type == prompt.request.type,
                QStringLiteral("unsupported_command"));
        prompt.request.responseSchema = descriptor->responseShape;
        return prompt;
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

// Where a selectable card physically sits, so a shell can group hand, equip,
// hand pile and expand pile without guessing from ownership.
QString cardZone(int id, const Scene &scene)
{
    const QString self = scene.state.selfName();
    if (scene.state.cardsForPlayer(self, Player::PlaceHand).contains(id))
        return QStringLiteral("hand");
    if (scene.state.cardsForPlayer(self, Player::PlaceEquip).contains(id))
        return QStringLiteral("equip");
    if (scene.players.self()->getHandPile().contains(id))
        return QStringLiteral("hand_pile");
    const Player *owner = scene.players.cardOwner(id);
    if (owner != nullptr && owner->objectName() != self)
        return QStringLiteral("sibling_pile");
    return QStringLiteral("expand_pile");
}

QJsonObject zoneMap(const QList<int> &ids, const Scene &scene)
{
    QJsonObject result;
    for (int id : ids) {
        const QString key = QString::number(id);
        if (!result.contains(key))
            result.insert(key, cardZone(id, scene));
    }
    return result;
}

QString limitScopeName(Skill::LimitScope scope)
{
    switch (scope) {
    case Skill::Limit_Round: return QStringLiteral("round");
    case Skill::Limit_Turn: return QStringLiteral("turn");
    case Skill::Limit_Phase: return QStringLiteral("phase");
    case Skill::Limit_Game: return QStringLiteral("game");
    case Skill::Limit_Custom: return QStringLiteral("custom");
    case Skill::Limit_None: break;
    }
    return QStringLiteral("none");
}

// Committed usage only. This reads the projected mark the Room already sent;
// a preview never adds one, so opening a skill cannot spend a use.
int committedUsage(const Skill *skill, const QString &name, int instanceId, const Scene &scene)
{
    QString suffix;
    switch (skill->getLimitScope()) {
    case Skill::Limit_Turn: suffix = QStringLiteral("-Clear"); break;
    case Skill::Limit_Round: suffix = QStringLiteral("_lun"); break;
    case Skill::Limit_Phase:
        suffix = skill->getPhaseName().isEmpty()
            ? QStringLiteral("-PhaseClear")
            : QStringLiteral("-") + skill->getPhaseName() + QStringLiteral("Clear");
        break;
    case Skill::Limit_Game: suffix = QStringLiteral("_game"); break;
    default: return -1;
    }
    const QString key = SkillInstanceUtils::formatUsageMarkKey(name, instanceId, suffix);
    if (key.isEmpty())
        return -1;
    return scene.players.self()->getMark(key);
}

QString activationStatusName(ClientRules::SkillActivationStatus status)
{
    switch (status) {
    case ClientRules::SkillActivationStatus::Available: return QStringLiteral("available");
    case ClientRules::SkillActivationStatus::MissingSkill: return QStringLiteral("missing_skill");
    case ClientRules::SkillActivationStatus::InvalidInstance: return QStringLiteral("invalid_instance");
    case ClientRules::SkillActivationStatus::Unavailable: return QStringLiteral("unavailable");
    case ClientRules::SkillActivationStatus::Unknown: break;
    }
    return QStringLiteral("unknown");
}

// Presentation detail for one ViewAs candidate: the declared subcard amount,
// the committed usage and whether the instance is invalidated. None of it is a
// rule decision; canActivate/cardSelectionFeasible remain authoritative.
QJsonObject skillDetail(const SkillActivationCandidate &candidate,
                        const ClientRules::SkillActivationResult &activation,
                        const Scene &scene)
{
    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(candidate.skillName);
    const Skill *skill = Sanguosha->getSkill(candidate.skillName);
    int minimum = -1;
    int maximum = -1;
    bool v2 = false;
    if (const auto *active = dynamic_cast<const ViewAsSkillV2 *>(viewAs)) {
        v2 = true;
        // n == 0 means the skill decides through cardSelectionFeasible, so the
        // amount stays unconstrained rather than being reported as "zero card".
        if (active->getN() > 0) {
            minimum = active->getN();
            maximum = minimum;
        }
    } else if (dynamic_cast<const ZeroCardViewAsSkill *>(viewAs) != nullptr) {
        minimum = 0;
        maximum = 0;
    } else if (dynamic_cast<const OneCardViewAsSkill *>(viewAs) != nullptr) {
        minimum = 1;
        maximum = 1;
    }
    QJsonObject entry{{QStringLiteral("name"), candidate.skillName},
        {QStringLiteral("instance_id"), candidate.instanceId},
        {QStringLiteral("available"), activation.known && activation.available},
        {QStringLiteral("status"), activationStatusName(activation.status)},
        {QStringLiteral("v2"), v2},
        {QStringLiteral("subcard_min"), minimum},
        {QStringLiteral("subcard_max"), maximum},
        {QStringLiteral("usage_scope"), QStringLiteral("none")},
        {QStringLiteral("usage_used"), -1},
        {QStringLiteral("invalid"), false},
        {QStringLiteral("response_or_use"), viewAs != nullptr && viewAs->isResponseOrUse()},
        {QStringLiteral("expand_pile"), viewAs != nullptr ? viewAs->getExpandPile() : QString()}};
    if (skill != nullptr) {
        entry.insert(QStringLiteral("usage_scope"), limitScopeName(skill->getLimitScope()));
        entry.insert(QStringLiteral("usage_used"),
                     committedUsage(skill, candidate.skillName, candidate.instanceId, scene));
        entry.insert(QStringLiteral("invalid"),
                     scene.players.self()->isSkillInvalid(candidate.skillName, candidate.instanceId));
    }
    return entry;
}

bool sameCardSet(QList<int> left, QList<int> right)
{
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}

// Enumerated prompts: validate the draft against the shared ClientCore payload
// and hand the canonical reply back through the registry's own encoder.
void evaluateEnumerated(const Prompt &prompt, const QJsonObject &selection,
                        QJsonObject *output)
{
    const int count = Sanguosha->getCardCount();
    const QList<int> chosen = optionalCardIds(selection.value(QStringLiteral("card_ids")), count);
    const QStringList targets = strings(selection.value(QStringLiteral("targets")));
    QList<int> selectable;
    QStringList candidates;
    QString reason;
    InteractionResponse response;
    int minimum = prompt.request.minSelection();
    int maximum = prompt.request.maxSelection();

    if (const auto *value = prompt.request.payloadAs<RearrangeCardsInteractionPayload>()) {
        selectable = value->cardIds;
        const QList<int> top = optionalCardIds(selection.value(QStringLiteral("top")), count);
        const QList<int> bottom = optionalCardIds(selection.value(QStringLiteral("bottom")), count);
        QList<int> merged = top;
        merged.append(bottom);
        QSet<int> unique(merged.constBegin(), merged.constEnd());
        if (unique.size() != merged.size() || !sameCardSet(merged, value->cardIds))
            reason = QStringLiteral("rearrangement_incomplete");
        else if (top.size() < value->minTop || top.size() > value->maxTop
                 || bottom.size() < value->minBottom || bottom.size() > value->maxBottom)
            reason = QStringLiteral("rearrangement_out_of_range");
        else
            response = InteractionResponse::makeRearrangement(prompt.request.requestId, top, bottom);
    } else if (const auto *value = prompt.request.payloadAs<GongxinInteractionPayload>()) {
        selectable = value->selectableCards;
        // Gongxin answers with exactly one card id; the shared payload carries
        // the visible/selectable split rather than a count.
        minimum = 1;
        maximum = 1;
        if (chosen.size() != 1)
            reason = QStringLiteral("select_one_card");
        else if (!selectable.contains(chosen.first()))
            reason = QStringLiteral("card_unavailable");
        else
            response = InteractionResponse::makeCards(prompt.request.requestId, chosen);
    } else if (const auto *value = prompt.request.payloadAs<YijiInteractionPayload>()) {
        selectable = value->cardIds;
        candidates = value->targetPlayers;
        const bool known = std::all_of(chosen.constBegin(), chosen.constEnd(),
            [&selectable](int id) { return selectable.contains(id); });
        if (!known)
            reason = QStringLiteral("card_unavailable");
        else if (chosen.size() < value->minCards || chosen.size() > value->maxCards)
            reason = QStringLiteral("selection_count_out_of_range");
        else if (targets.size() != 1 || !candidates.contains(targets.first()))
            reason = QStringLiteral("incomplete_targets");
        else
            response = InteractionResponse::makeDistribution(prompt.request.requestId,
                                                             chosen, targets.first());
    } else if (const auto *value = prompt.request.payloadAs<CardInteractionPayload>()) {
        selectable = value->selection.selectableCards;
        minimum = value->selection.minSelection;
        maximum = value->selection.maxSelection;
        if (chosen.size() < minimum || chosen.size() > maximum)
            reason = QStringLiteral("selection_count_out_of_range");
        else if (!std::all_of(chosen.constBegin(), chosen.constEnd(),
                              [&selectable](int id) { return selectable.contains(id); }))
            reason = QStringLiteral("card_unavailable");
        else
            response = InteractionResponse::makeCards(prompt.request.requestId, chosen);
    } else {
        require(false, QStringLiteral("unsupported_command"));
    }

    output->insert(QStringLiteral("selectable_cards"), jsonIds(selectable));
    output->insert(QStringLiteral("selection_min"), minimum);
    output->insert(QStringLiteral("selection_max"), maximum);
    output->insert(QStringLiteral("next_targets"), QJsonObject{
        {QStringLiteral("candidates"), QJsonArray::fromStringList(candidates)},
        {QStringLiteral("max_votes"), QJsonObject()}});
    if (!reason.isEmpty()) {
        output->insert(QStringLiteral("reason"), reason);
        return;
    }
    response.command = prompt.request.command;
    const auto *descriptor = InteractionCommandRegistry::find(
        static_cast<QSanProtocol::CommandType>(prompt.request.command));
    require(descriptor != nullptr && descriptor->replyEncoder != nullptr,
            QStringLiteral("unsupported_command"));
    const auto wire = descriptor->replyEncoder(prompt.request, response);
    require(wire.command != QSanProtocol::S_COMMAND_UNKNOWN
            && wire.replyTo == prompt.request.requestId,
            QStringLiteral("reply_encoding_failed"));
    output->insert(QStringLiteral("can_confirm"), true);
    output->insert(QStringLiteral("wire"), QJsonObject{
        {QStringLiteral("command"), static_cast<int>(wire.command)},
        {QStringLiteral("reply_to"), QString::number(wire.replyTo)},
        {QStringLiteral("payload"), QJsonValue::fromVariant(wire.payload)}});
}

// The dialog shape a skill declares, so a shell implements guhuo / juguan /
// tiansuan once instead of one branch per general.
QJsonObject declarationDialog(const QString &skillName)
{
    const Skill *skill = Sanguosha->getSkill(skillName);
    if (skill == nullptr)
        return {};
    SkillDialogInfo info = skill->getDialogInfo();
    if (!info.isValid()) {
        const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName);
        if (viewAs != nullptr)
            info = viewAs->getDialogInfo();
    }
    if (!info.isValid())
        return {};
    return {{QStringLiteral("type"), info.type},
        {QStringLiteral("object_name"), info.objectName},
        {QStringLiteral("parameters"), QJsonObject::fromVariantMap(info.parameters)}};
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
    QJsonObject result = QSanRules::exportRegistry(*Sanguosha);
    const auto identity = Sanguosha->rulesBundleIdentity();
    require(QSanRules::validate(identity), QStringLiteral("rules_content_unsupported"));
    // New initialization contract makes old hosts fail before issuing queries.
    result.insert(QStringLiteral("schema_version"), QSanRules::BridgeSchema);
    result.insert(QStringLiteral("rules_bundle"), identity);
    result.insert(QStringLiteral("translations"), QJsonObject::fromVariantMap(Sanguosha->translationTable()));
    result.insert(QStringLiteral("extension_files"), QJsonArray::fromStringList(
        QDir(QSanRuntimePaths::assetPath(QStringLiteral("extensions"))).entryList(
            QStringList{QStringLiteral("*.lua")}, QDir::Files, QDir::Name)));
    return result;
}

QJsonObject ClientRulesSession::evaluate(const QJsonObject &input) const
{
    // A preview must not draw from any shared stream. Binding a throwaway
    // generator keeps an accidental random call inside this query instead of
    // advancing the process-wide fallback the next query would observe.
    GameRng previewRng;
    GameRng::Binding previewRngBinding(previewRng);
    QJsonObject output{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("generation"), input.value(QStringLiteral("generation"))},
        {QStringLiteral("revision"), input.value(QStringLiteral("revision"))},
        {QStringLiteral("request_id"), input.value(QStringLiteral("request_id"))},
        {QStringLiteral("known"), false}, {QStringLiteral("reason"), QString()},
        {QStringLiteral("can_confirm"), false}, {QStringLiteral("card_text"), QString()},
        {QStringLiteral("selectable_cards"), QJsonArray()}, {QStringLiteral("skills"), QJsonArray()},
        {QStringLiteral("declarations"), QJsonArray()},
        {QStringLiteral("declaration_dialog"), QJsonObject()},
        {QStringLiteral("card_zones"), QJsonObject()},
        {QStringLiteral("selection_min"), 0}, {QStringLiteral("selection_max"), 0},
        {QStringLiteral("interaction"), QJsonObject()},
        {QStringLiteral("next_targets"), QJsonObject{
            {QStringLiteral("candidates"), QJsonArray()}, {QStringLiteral("max_votes"), QJsonObject()}}},
        {QStringLiteral("player_metrics"), QJsonObject()},
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
        output.insert(QStringLiteral("player_metrics"), scene.players.metrics());
        const QJsonObject interaction = input.value(QStringLiteral("interaction")).isObject()
            ? input.value(QStringLiteral("interaction")).toObject() : QJsonObject();
        // The shell renders sets and counts from the shared ClientCore request,
        // not from a second reading of the wire payload.
        output.insert(QStringLiteral("interaction"), interaction);
        const Prompt prompt = makePrompt(input, interaction, scene, id);
        const QJsonObject selectionDraft = object(input.value(QStringLiteral("selection")),
                                                  QStringLiteral("selection"));
        if (prompt.enumerated) {
            output.insert(QStringLiteral("known"), true);
            evaluateEnumerated(prompt, selectionDraft, &output);
            return output;
        }
        // UNKNOWN is the native context for neutral/discard physical responses.
        scene.room.setCardUseContext(prompt.reason, prompt.cards.selection.pattern);
        const QJsonObject &selection = selectionDraft;
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
            skills.append(skillDetail(candidate, activation, scene));
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
        // One physical card, or the declared subcard amount of the chosen skill.
        // -1 means the skill answers through cardSelectionFeasible instead.
        output.insert(QStringLiteral("selection_min"), 1);
        output.insert(QStringLiteral("selection_max"), 1);
        for (const QJsonValue &entry : skills) {
            const QJsonObject detail = entry.toObject();
            if (!usingSkill || detail.value(QStringLiteral("name")).toString() != draft.skill.skillName
                || detail.value(QStringLiteral("instance_id")).toInt() != draft.skill.instanceId)
                continue;
            output.insert(QStringLiteral("selection_min"), detail.value(QStringLiteral("subcard_min")));
            output.insert(QStringLiteral("selection_max"), detail.value(QStringLiteral("subcard_max")));
        }
        if (usingSkill) {
            require(Sanguosha->getViewAsSkill(draft.skill.skillName) != nullptr,
                    QStringLiteral("unsupported_skill:") + draft.skill.skillName);
            if (!selectedSkillKnown || !selectedSkillAvailable) {
                output.insert(QStringLiteral("reason"), QStringLiteral("skill_unavailable"));
                return output;
            }
            const QStringList options = declarations(draft.skill, prompt, scene);
            output.insert(QStringLiteral("declarations"), QJsonArray::fromStringList(options));
            output.insert(QStringLiteral("declaration_dialog"),
                          declarationDialog(draft.skill.skillName));
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
            output.insert(QStringLiteral("card_zones"), zoneMap(next + selectedCards, scene));
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
            output.insert(QStringLiteral("card_zones"), zoneMap(selectable + selectedCards, scene));
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
