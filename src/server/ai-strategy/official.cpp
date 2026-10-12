#include "ai-strategy/official.h"

// ---- yczh2016-ai ----
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "skill.h"
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QVariantMap>

namespace {
const QString LedgerName = QStringLiteral("_IsolatedAI.YCZH2016.Hints");
class HintLedger final : public QObject {
public:
    explicit HintLedger(Room &room) : QObject(&room) { setObjectName(LedgerName); }
    QVariantMap pending;
    QVariantMap visible;
    QHash<QString, QPointer<const Card>> acceptedCards;
};
HintLedger *ledger(Room &room, bool create)
{
    // A direct child belongs to this exact Room lifetime. No global Room* cache,
    // gameplay tags, scenario callbacks, or state-revision writes are involved.
    QObject *child = room.findChild<QObject *>(LedgerName, Qt::FindDirectChildrenOnly);
    if (child) return dynamic_cast<HintLedger *>(child);
    return create ? new HintLedger(room) : nullptr;
}
QString skillForHint(const QString &name)
{
    if (name == "jiaozhaoAI") return "jiaozhao";
    if (name == "jiyuAI") return "jiyu";
    if (name == "kuangbiAI") return "kuangbi";
    return {};
}
QString entryKey(const QString &name, const QString &player)
{
    // Roster names need not exclude punctuation; length-prefix avoids collisions.
    return QString::number(name.size()) + ':' + name + player;
}
QVariantMap cardHintIdentity(const Card *card, const SkillInstanceRef &activation,
                             const SkillInstanceRef &root)
{
    if (!card || !activation.isValid() || !root.isValid()
        || card->getActivationSkillName() != activation.key.skillName
        || card->getActivationSkillInstanceId() != activation.key.instanceID
        || card->getSourceSkillName() != root.key.skillName
        || card->getSourceSkillInstanceId() != root.key.instanceID) return {};
    QVariantList costs; for (int id : card->getSubcards()) costs.append(id);
    const SkillCard *skillCard = qobject_cast<const SkillCard *>(card->getRealCard());
    return {{"activation_owner", activation.ownerObjectName},
        {"activation_skill", activation.key.skillName}, {"activation_id", activation.key.instanceID},
        {"root_owner", root.ownerObjectName}, {"root_skill", root.key.skillName}, {"root_id", root.key.instanceID},
        {"name", card->objectName()}, {"class", card->getClassName()},
        {"skill", card->getSkillName()}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
        {"costs", costs}, {"user_string", skillCard ? skillCard->getUserString() : QString()}};
}
QJsonObject failure(const QString &message) { return {{"error", message}}; }
}

bool stageAIStrategyPlayerHints(Room &room, const AIRequest &request,
                                const QJsonArray &effects, QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    ServerPlayer *source = room.findPlayerByObjectName(request.viewerObjectName, true);
    if (!source || request.decisionId == 0) return reject("invalid accepted hint source");
    QVariantMap additions;
    for (const QJsonValue &value : effects) {
        if (!value.isObject()) return reject("hint is not an object");
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        const QString name = effect.value("name").toString();
        const QString skill = skillForHint(name);
        const QString target = effect.value("player").toString();
        if (skill.isEmpty() || effect.value("source").toString() != source->objectName()
            || !room.findPlayerByObjectName(target, true)) return reject("hint source, name, or target is not authorized");
        if (skill == "jiaozhao") {
            if (request.kind != AIRequest::PlayerChosen || request.choiceOptions.reason != skill)
                return reject("Jiaozhao hint needs its accepted player choice");
        } else if (request.kind != AIRequest::Activate && request.kind != AIRequest::UseCard) {
            return reject("skill-use hint needs an accepted card action");
        }
        const QString key = entryKey(name, target);
        additions.insert(key, QVariantMap{{"name", name}, {"source", source->objectName()},
            {"target", target}, {"skill", skill}, {"decision", QString::number(request.decisionId)}});
    }
    if (additions.isEmpty()) return true;
    HintLedger *state = ledger(room, false);
    if (!state && room.findChild<QObject *>(LedgerName, Qt::FindDirectChildrenOnly))
        return reject("hint ledger name collision");
    QVariantMap pending = state ? state->pending : QVariantMap();
    // A newer accepted decision replaces all older pending guesses by this
    // source for that skill, even when its selected target has changed.
    for (const QVariant &value : additions) {
        const QVariantMap row = value.toMap();
        for (auto it = pending.begin(); it != pending.end();) {
            const QVariantMap old = it.value().toMap();
            if (old.value("source") == row.value("source") && old.value("skill") == row.value("skill")) it = pending.erase(it);
            else ++it;
        }
    }
    for (auto it = additions.cbegin(); it != additions.cend(); ++it) pending.insert(it.key(), it.value());
    const int bound = 3 * room.getAllPlayers(true).size();
    if (pending.size() > bound) return reject("hint pending roster bound exceeded");
    if (!commit) return true;
    if (!state) state = ledger(room, true);
    for (auto it = state->acceptedCards.begin(); it != state->acceptedCards.end();) {
        if (!pending.contains(it.key()) || additions.contains(it.key())) it = state->acceptedCards.erase(it);
        else ++it;
    }
    state->pending = pending;
    return true;
}

void witnessAIStrategyPlayerHints(Room &room, quint64 decisionId, const ServerPlayer *source,
                                  const QString &skill, const QStringList &targets)
{
    HintLedger *state = ledger(room, false);
    if (!state || !source || decisionId == 0) return;
    for (auto it = state->pending.begin(); it != state->pending.end();) {
        const QVariantMap row = it.value().toMap();
        if (row.value("source").toString() != source->objectName()
            || row.value("skill").toString() != skill
            || row.value("decision").toString() != QString::number(decisionId)) { ++it; continue; }
        const QString target = row.value("target").toString();
        if (targets.contains(target) && room.findPlayerByObjectName(target, true)) {
            state->visible.insert(it.key(), row.value("source"));
        }
        // Same accepted witness finishes this publication attempt; an absent
        // target cannot be revived by an unrelated later use of the same skill.
        state->acceptedCards.remove(it.key());
        it = state->pending.erase(it);
    }
}

bool stageAcceptedAIStrategyCardHints(Room &room, const AIRequest &request,
                                     const QJsonArray &effects, const Card *acceptedCard, QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    QJsonArray hints;
    for (const QJsonValue &value : effects) {
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        const QString skill = skillForHint(effect.value("name").toString());
        if ((skill != "jiyu" && skill != "kuangbi") || !acceptedCard
            || acceptedCard->getActivationSkillName() != skill)
            return reject("hint does not belong to the accepted card product");
        hints.append(effect);
    }
    if (hints.isEmpty()) return true;
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return reject("accepted card hint revision is stale");
    const SkillInstanceRef activation(request.viewerObjectName,
        SkillInstanceKey(acceptedCard->getActivationSkillName(), acceptedCard->getActivationSkillInstanceId()));
    const SkillInstanceRef root = room.resolveSkillInstanceRootRef(activation);
    const QVariantMap identity = cardHintIdentity(acceptedCard, activation, root);
    if (identity.isEmpty()) return reject("accepted card hint provenance is not a live complete source");
    if (!stageAIStrategyPlayerHints(room, request, hints, error, commit)) return false;
    if (!commit) return true;
    HintLedger *state = ledger(room, false);
    for (const QJsonValue &value : hints) {
        const QJsonObject effect = value.toObject();
        const QString key = entryKey(effect.value("name").toString(),effect.value("player").toString());
        state->acceptedCards.insert(key, acceptedCard);
        QVariantMap row = state->pending.value(key).toMap();
        row.insert("accepted_identity", identity); state->pending.insert(key, row);
    }
    return true;
}

void transferAcceptedAIStrategyCardHints(Room &room, const Card *acceptedCard,
    const Card *rebuiltCard, const ServerPlayer *source,
    const SkillInstanceRef &activationRef, const SkillInstanceRef &sourceRef)
{
    HintLedger *state = ledger(room, false);
    if (!state || !acceptedCard || !rebuiltCard || !source || source->getRoom() != &room
        || activationRef.ownerObjectName != source->objectName()) return;
    const QString skill = activationRef.key.skillName;
    if (skill != "jiyu" && skill != "kuangbi") return;
    const QVariantMap identity = cardHintIdentity(acceptedCard, activationRef, sourceRef);
    if (identity.isEmpty() || identity != cardHintIdentity(rebuiltCard, activationRef, sourceRef)) return;
    // This is a native-only successful-rebuild handoff, never a lookup by skill,
    // latest request, cost ids or caller-controlled Lua descriptor.
    for (auto it = state->pending.cbegin(); it != state->pending.cend(); ++it) {
        const QVariantMap row = it.value().toMap();
        if (state->acceptedCards.value(it.key()).data() == acceptedCard
            && row.value("source").toString() == source->objectName()
            && row.value("skill").toString() == skill
            && row.value("accepted_identity").toMap() == identity)
            state->acceptedCards[it.key()] = rebuiltCard;
    }
}

void witnessActualAIStrategyCardHints(Room &room, const Card *actualCard,
                                      const ServerPlayer *source, const QStringList &targets)
{
    HintLedger *state = ledger(room, false);
    if (!state || !actualCard || !source) return;
    const QString skill = actualCard->getActivationSkillName();
    if (skill != "jiyu" && skill != "kuangbi") return;
    QSet<quint64> decisions;
    for (auto it = state->pending.cbegin(); it != state->pending.cend(); ++it) {
        const QVariantMap row = it.value().toMap();
        if (state->acceptedCards.value(it.key()).data() == actualCard
            && row.value("source").toString() == source->objectName()
            && row.value("skill").toString() == skill)
            decisions.insert(row.value("decision").toString().toULongLong());
    }
    for (quint64 decision : decisions)
        witnessAIStrategyPlayerHints(room, decision, source, skill, targets);
}

bool stageAcceptedAIStrategyChoiceHints(Room &room, const AIRequest &request,
                                       const QJsonArray &effects, const ServerPlayer *choice,
                                       QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    QJsonArray hints;
    for (const QJsonValue &value : effects) {
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        if (effect.value("name").toString() != "jiaozhaoAI" || !choice
            || effect.value("player").toString() != choice->objectName())
            return reject("hint does not belong to the accepted player choice");
        hints.append(effect);
    }
    if (hints.isEmpty()) return true;
    if (hints.size() != 1 || request.kind != AIRequest::PlayerChosen || request.choiceOptions.reason != "jiaozhao"
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("Jiaozhao accepted choice is outside its current question");
    const QVariantMap history = room.historyParent(room.currentHistoryEventId(), "skill", true).value("data").toMap();
    const qint64 execution = history.value("execution_id").toLongLong();
    const SkillExecutionRegistry::Entry *entry = room.findSkillExecution(execution);
    if (execution <= 0 || !entry || entry->finished) return reject("Jiaozhao live execution is missing");
    const SkillContext context = room.getSkillExecutionContext(execution);
    if (context.activationRef.key.skillName != "jiaozhao" || !context.initiator
        || context.initiator->objectName() != request.viewerObjectName || !context.use_card)
        return reject("Jiaozhao accepted choice belongs to another execution");
    if (!stageAIStrategyPlayerHints(room, request, hints, error, commit)) return false;
    if (!commit) return true;
    HintLedger *state = ledger(room, false);
    const QString key = entryKey("jiaozhaoAI", choice->objectName());
    QVariantMap pending = state->pending.value(key).toMap();
    pending.insert("execution", QString::number(execution));
    state->pending.insert(key,pending);
    state->acceptedCards.insert(key,context.use_card);
    return true;
}

void witnessAIStrategyJiaozhaoDeclaration(Room &room, const SkillContext &context,
                                         const ServerPlayer *target)
{
    HintLedger *state = ledger(room, false);
    if (!state || !target || !target->isAlive() || context.is_canceled
        || context.choice != "declare" || context.activationRef.key.skillName != "jiaozhao"
        || !context.initiator || !context.use_card || context.executionID <= 0
        || !context.targets.contains(const_cast<ServerPlayer *>(target))) return;
    const SkillExecutionRegistry::Entry *entry = room.findSkillExecution(context.executionID);
    if (!entry || entry->finished) return;
    const QString key = entryKey("jiaozhaoAI",target->objectName());
    const QVariantMap row = state->pending.value(key).toMap();
    if (row.value("execution").toString() != QString::number(context.executionID)
        || row.value("source").toString() != context.initiator->objectName()
        || state->acceptedCards.value(key).data() != context.use_card) return;
    witnessAIStrategyPlayerHints(room,row.value("decision").toString().toULongLong(),
        context.initiator,"jiaozhao",{target->objectName()});
}

QJsonObject consumeAIStrategyPlayerHint(Room &room, const AIRequest &request,
                                      ServerPlayer *viewer, const QString &name)
{
    const QString skill = skillForHint(name);
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision() || skill.isEmpty())
        return failure("hint reader is outside its active viewer/revision");
    const AIRequest::DecisionKind expected = skill == "jiaozhao" ? AIRequest::AmazingGrace : AIRequest::Discard;
    if (request.kind != expected || request.choiceOptions.reason != skill)
        return failure("hint reader is outside its original question");
    HintLedger *state = ledger(room, false);
    const QString key = entryKey(name, viewer->objectName());
    const QString source = state ? state->visible.take(key).toString() : QString();
    if (source.isEmpty() || !room.findPlayerByObjectName(source, true))
        return {{"known", true}, {"source", false}};
    return {{"known", true}, {"source", source}};
}

namespace {
QJsonObject disclosedCard(const Card *card)
{
    if (!card) return {};
    QJsonArray kinds;
    for (const QString &kind : {QString("Card"), QString("BasicCard"), QString("TrickCard"),
        QString("DelayedTrick"), QString("EquipCard"), QString("SkillCard"), QString("Slash"),
        QString("Jink"), QString("Peach"), QString("Analeptic"), QString("Weapon"), QString("Armor"),
        QString("DefensiveHorse"), QString("OffensiveHorse"), QString("Treasure"), QString("AOE")})
        if (card->isKindOf(kind.toLatin1().constData())) kinds.append(kind);
    if (!kinds.contains(card->getClassName())) kinds.append(card->getClassName());
    QJsonArray costs; for (int id : card->getSubcards()) costs.append(id);
    return {{"id", card->getId()}, {"effective_id", card->getEffectiveId()},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"kind_of", kinds},
        {"legacy_string_key", card->toString()}, {"legacy_sort_key", card->toString()},
        {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
        {"red", card->isRed()}, {"black", card->isBlack()}, {"virtual_card", card->isVirtualCard()},
        {"subcards", costs}, {"can_recast", card->canRecast()}, {"target_fixed", card->targetFixed()},
        {"damage_card", card->isDamageCard()}, {"handling_method", int(card->getHandlingMethod())},
        {"skill_name", card->getActivationSkillName()}, {"skill_names_complete", false}};
}
}

void projectAIStrategyYCZH2016(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (!viewer || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject facts, levels;
    // jiaozhaoSetLevel uses Room::setPlayerProperty plus a public mark. These
    // presentation levels, including the original invalid-property toInt()==0,
    // are public; selected-instance gameplay state is deliberately not exposed.
    for (ServerPlayer *player : room.getAllPlayers(true))
        levels.insert(player->objectName(), player->property("jiaozhao_level").toInt());
    facts.insert("jiaozhao_levels", levels);
    const int declared = viewer->getMark("jiaozhao_id-Clear") - 1;
    if (declared >= 0 && declared < Sanguosha->getCardCount())
        facts.insert("jiaozhao_declared_card", disclosedCard(Sanguosha->getEngineCard(declared)));
    else facts.insert("jiaozhao_declared_card", false);
    request.worldView.strategyFacts.insert("yczh2016", facts);

    if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "jiaozhao") {
        QJsonObject printed;
        for (int id : request.choiceOptions.cardIds)
            if (id >= 0 && id < Sanguosha->getCardCount())
                printed.insert(QString::number(id), disclosedCard(Sanguosha->getEngineCard(id)));
        request.choiceOptions.context.insert("yczh2016_printed_cards", printed);
    }
    const bool offer = request.kind == AIRequest::UseCard && request.pattern == "@@huisheng";
    const bool discard = request.kind == AIRequest::Discard && request.choiceOptions.reason == "huisheng";
    if (offer || discard) {
        const QVariant value = room.getTag("HuishengDamage");
        if (value.canConvert<DamageStruct>()) {
            const DamageStruct damage = value.value<DamageStruct>();
            if ((offer && damage.to == viewer) || (discard && damage.from == viewer)) {
                QJsonObject row{{"from", damage.from ? damage.from->objectName() : QString()},
                    {"to", damage.to ? damage.to->objectName() : QString()},
                    {"amount", damage.damage}, {"nature", int(damage.nature)},
                    {"chain", damage.chain}, {"transfer", damage.transfer}, {"reason", damage.reason}};
                if (damage.card) row.insert("card", disclosedCard(damage.card));
                request.choiceOptions.context.insert("damage", row);
            }
        }
        if (discard) {
            // Audited writer YCZH2016::Huisheng::effectTarget sets this viewer's
            // own tag immediately after fillAG(ids, viewer), and restores it on
            // return. These faces were explicitly shown for this very question.
            QJsonArray ids; QJsonObject cards;
            const QVariantList offered = viewer->getTag("huisheng_ag_ids").toList();
            for (const QVariant &item : offered) {
                bool ok = false; const int id = item.toInt(&ok);
                if (!ok || id < 0 || id >= Sanguosha->getCardCount()) continue;
                ids.append(id); cards.insert(QString::number(id), disclosedCard(Sanguosha->getCard(id)));
            }
            request.choiceOptions.context.insert("huisheng_ag_ids", ids);
            request.choiceOptions.context.insert("huisheng_cards", cards);
        }
    }
}

// ---- yczh2017-ai ----
#include "ai.h"

void projectAIStrategyYCZH2017(AIRequest &request, Room &room, ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &publicDecisionCard)
{
    if (!viewer || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject facts{{"funan_level_up", viewer->property("funan_level_up").toBool()},
                      {"funan_card", false}};
    // Funan temporarily records the publicly declared response while it asks
    // its owner. Do not read this tag for another observer or another question.
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "funan") {
        const Card *card = viewer->getTag("FunanCard").value<const Card *>();
        if (card) facts.insert("funan_card", publicDecisionCard(card));
    }
    request.worldView.strategyFacts.insert("yczh2017", facts);
    // These recipients were captured before Huimin drew cards. Recomputing
    // them from present hand sizes changes the native selection domain.
    if (request.kind == AIRequest::UseCard && request.pattern == "@@huimin!") {
        QJsonObject value; bool found = false;
        for (const auto &scope : request.skillActions) {
            if (!scope.isValid() || scope.getActivationOwner() != viewer->objectName()
                || scope.getActivationSkillName() != "huimin"
                || !room.isAcceptedViewAsEffect(scope.activationRef)) continue;
            const int id = scope.getActivationInstanceId();
            const QVariant count = viewer->getSkillInstanceStateValue("huimin", id, "count");
            const QVariant recipients = viewer->getSkillInstanceStateValue("huimin", id, "recipients");
            if (!count.isValid() || !recipients.isValid()) continue;
            const QJsonObject candidate{{"count", count.toInt()},
                {"recipients", QJsonArray::fromStringList(recipients.toStringList())}};
            // Two differing active scopes are ambiguous; no arbitrary winner.
            if (found && value != candidate) { value = {}; break; }
            value = candidate; found = true;
        }
        if (!value.isEmpty()) {
            request.choiceOptions.context.insert("huimin", value);
            // UseCard does not serialize AIChoiceOptions. The shared facade
            // merges this world context for card and choice inquiries alike.
            request.worldView.decisionContext.insert("huimin", value);
        }
    }
    // Current Fumian uses a split player inquiry. Its public declared use is
    // read only from the live enclosing Fumian execution, never a stale tag.
    if ((request.kind == AIRequest::PlayersChosen || request.kind == AIRequest::PlayerChosen)
        && request.choiceOptions.reason == "fumian") {
        const QVariantMap history = room.historyParent(room.currentHistoryEventId(), "skill", true).value("data").toMap();
        const qint64 execution = history.value("execution_id").toLongLong();
        const auto *entry = room.findSkillExecution(execution);
        if (execution > 0 && entry && !entry->finished) {
            const SkillContext ctx = room.getSkillExecutionContext(execution);
            if ((ctx.skill_name == "fumian" || ctx.activationRef.key.skillName == "fumian")
                && ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                // The ongoing use belongs to this viewer, even when Fumian
                // originated from another public receipt owner.
                if (use.from == viewer && use.card) {
                    QJsonArray targets;
                    for (const ServerPlayer *p : use.to) if (p) targets.append(p->objectName());
                    request.choiceOptions.context.insert("fumian_use", QJsonObject{
                        {"from", viewer->objectName()}, {"to", targets}, {"card", publicDecisionCard(use.card)}});
                }
            }
        }
    }
    // Shouxi offers printed engine definitions in a private-to-viewer AG.
    // Serialize only those already offered IDs, never an arbitrary ID oracle.
    if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "shouxi") {
        QJsonObject printed;
        for (int id : request.choiceOptions.cardIds) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) continue;
            QJsonObject row = publicDecisionCard(card);
            row.insert("legacy_string_key", card->toString());
            row.insert("legacy_sort_key", card->toString());
            row.insert("virtual_card", false);
            row.insert("subcards", QJsonArray());
            printed.insert(QString::number(id), row);
        }
        request.choiceOptions.context.insert("shouxi_printed_cards", printed);
    }
}

// ---- yjcm2012-ai ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/native-registry.h"
#include "standard.h"
#include "lua.hpp"
#include <QScopeGuard>
#include <memory>
#include <cmath>
#include <limits>

namespace {
// The source uses sgs.weapon_range[class] or 1, not Weapon::getRange().
// A table metatable could synthesize a value: never run that callback here.
bool sourceWeaponRange(Room &room, const Card *weapon, double &range)
{
    lua_State *state = room.roomRuntime()->lua().state();
    if (!state || !weapon) return false;
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    lua_pushglobaltable(state); lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
    if (!lua_istable(state, -1)) return false;
    lua_pushliteral(state, "weapon_range"); lua_rawget(state, -2);
    if (!lua_istable(state, -1)) return false;
    const bool meta = lua_getmetatable(state, -1) != 0;
    if (meta) return false;
    const QByteArray key = weapon->getClassName().toUtf8();
    lua_pushlstring(state, key.constData(), size_t(key.size())); lua_rawget(state, -2);
    if (lua_isnil(state, -1) || (lua_isboolean(state, -1) && !lua_toboolean(state, -1))) {
        range = 1;
        return true;
    }
    if (!lua_isnumber(state, -1)) return false;
    range = lua_tonumber(state, -1);
    return std::isfinite(range);
}
}

void projectAIStrategyYjcm2012(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    const QVariant color = viewer->getTag("qianxi");
    const QVariant optional = viewer->getTag("SecondMijiOptional");
    QJsonObject facts{
        {"qianxi_color_present", color.isValid()}, {"qianxi_color", color.toString()},
        {"second_miji_optional_present", optional.isValid()}, {"second_miji_optional", optional.toBool()}};
    ServerPlayer *dying = room.getCurrentDyingPlayer();
    facts.insert("current_dying_present", dying != nullptr);
    facts.insert("current_dying", dying ? dying->objectName() : QString());
    if (dying) {
        // No held card is inspected: this is the exact fresh dummy used by
        // Chunlao's source, and only the public rule answer is disclosed.
        const std::unique_ptr<Card> analeptic(Sanguosha->cloneCard("analeptic", Card::NoSuit, 0));
        if (analeptic) facts.insert("dying_analeptic_locked", dying->isLocked(analeptic.get()));
    }
    if (([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("qice"); }()) && world.strategyFacts.value("pattern_names").isArray()) {
        QJsonObject patterns;
        for (const QJsonValue &value : world.strategyFacts.value("pattern_names").toArray()) {
            const QString name = value.toString();
            const std::unique_ptr<Card> clone(Sanguosha->cloneCard(name, Card::NoSuit, 0));
            patterns.insert(name, QJsonObject{{"exists", bool(clone)}, {"nd_trick", clone && clone->isNDTrick()}});
        }
        facts.insert("qice_patterns", patterns);
    }
    if (const Card *weapon = viewer->getWeapon()) {
        double sourceRange = 0;
        if (sourceWeaponRange(room, weapon, sourceRange)) {
            const double fix = sourceRange - viewer->getAttackRange(false);
            // The fixed SWIG Player::distanceTo wrapper casts lua_tonumber to
            // int (truncation toward zero), including fractional metadata.
            // Values outside C++ int would be undefined in the donor binding.
            if (std::isfinite(fix) && fix >= std::numeric_limits<int>::min()
                && fix <= std::numeric_limits<int>::max()) {
                QJsonObject distances;
                for (ServerPlayer *target : room.getAllPlayers())
                    distances.insert(target->objectName(), viewer->distanceTo(target, int(fix)));
                facts.insert("jiefan_weapon_range", sourceRange);
                facts.insert("jiefan_range_fix", fix);
                facts.insert("jiefan_distances", distances);
            }
        }
    }
    world.strategyFacts.insert("yjcm2012", facts);
}

// ---- yjcm2014-ai ----
#include "card.h"
#include "structs.h"

#include <QSet>

void projectAIYjcm2014(AIWorldView &world, Room &room, const ServerPlayer *viewer,
                      const std::function<QJsonObject(const Card *)> &describeCard)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    QJsonObject standard = world.strategyFacts.value("standard").toObject();
    // Fixed donor yjcm2014-ai.lua reads this exact owner property. The current
    // gameplay uses #shenduan instead and has no writer for shenduanForAI;
    // an empty property must remain empty, not silently become the new pile.
    standard.insert("shenduan_for_ai", viewer->property("shenduanForAI").toString());

    QSet<QString> roster;
    QJsonObject hasAI;
    const QJsonObject availability = world.strategyFacts.value("ai_control_availability").toObject();
    for (const ServerPlayer *player : room.getAllPlayers(true)) {
        roster.insert(player->objectName());
        // Reuse the viewer-authorized, read-only controller observation. A
        // foreign non-online actor's private onsole relation is not public,
        // and getAI() may repair malformed controller links while observing.
        const QJsonObject row = availability.value(player->objectName()).toObject();
        if (row.value("known").isBool() && row.value("known").toBool()
            && row.value("has_ai").isBool())
            hasAI.insert(player->objectName(), row.value("has_ai"));
        else
            hasAI.insert(player->objectName(), QJsonValue::Null);
    }
    standard.insert("player_has_ai", hasAI);

    // yjcm2014.cpp Zenhui sets this tag immediately before its owner inquiry
    // and removes it immediately afterwards. The used card is already public;
    // never enumerate arbitrary foreign player tags or expose their hand IDs.
    const QVariant tag = viewer->getTag("zenhui");
    standard.insert("zenhui_use", QJsonValue::Null);
    if (tag.canConvert<CardUseStruct>()) {
        const CardUseStruct use = tag.value<CardUseStruct>();
        if (use.card && use.from == viewer && describeCard) {
            QJsonArray targets;
            bool valid = true;
            for (const ServerPlayer *target : use.to) {
                if (!target || !roster.contains(target->objectName())) { valid = false; break; }
                targets.append(target->objectName());
            }
            const QJsonObject card = describeCard(use.card);
            if (valid && !card.isEmpty()) {
                standard.insert("zenhui_use", QJsonObject{
                    {"card", card}, {"from", viewer->objectName()}, {"to", targets}
                });
            }
        }
    }
    world.strategyFacts.insert("standard", standard);
}

// ---- yjcm2015-ai ----
namespace {
QJsonObject yjcm2015AiDisclosedCard(const AICardView &c)
{
    // Reuse only the identity the world producer already disclosed. In
    // particular, do not reread a foreign live wrapper's flags or root chain.
    QJsonObject row{{"id",c.cardId},{"effective_id",c.effectiveId},
        {"name",c.objectName},{"class_name",c.className},
        {"name_length",c.nameLength},{"legacy_string_key",c.legacyStringKey},{"legacy_sort_key",c.legacySortKey},
        {"kind_of",QJsonArray::fromStringList(c.kindOfNames)},
        {"type_id",c.typeId},{"suit",c.suit},{"number",c.number},
        {"red",c.red},{"black",c.black},{"skill_name",c.skillName},
        {"virtual_card",false},{"subcards",QJsonArray()},
        {"damage_card",c.damageCard},{"target_fixed",c.targetFixed},
        {"will_throw",c.willThrow},{"can_recast",c.canRecast},
        {"handling_method",c.handlingMethod},{"known_flags",c.knownFlags},
        {"flags_complete",c.flagsComplete}};
    row.insert("skill_names_complete",c.skillNamesComplete);
    if(c.skillNamesComplete)row.insert("skill_names",QJsonArray::fromStringList(c.skillNames));
    if(c.flagsComplete)row.insert("flags_complete_scope","owner_non_visibility");
    if(c.weaponRange>=0)row.insert("weapon_range",c.weaponRange);
    return row;
}
}
void projectAIYjcm2015(AIWorldView &world, Room &room, const ServerPlayer *viewer,
                      const std::function<QJsonObject(const Card *)> &publicDecisionCard)
{
    if(!viewer || world.self.objectName!=viewer->objectName())return;
    const QVariant idTag=viewer->getTag("TaoxiId");
    bool idValid=false;const int id=idTag.toInt(&idValid);
    QJsonObject facts{{"taoxi_id",id},{"taoxi_id_present",idTag.isValid()},
        {"taoxi_here",viewer->getTag("TaoxiHere").toBool()},
        {"taoxi_use",false},{"taoxi_borrowed",false}};
    const QVariant useTag=viewer->getTag("taoxi_carduse");
    if(useTag.canConvert<CardUseStruct>()){
        const CardUseStruct use=useTag.value<CardUseStruct>();
        if(use.card){
            QJsonArray targets;for(const auto *p:use.to)if(p)targets.append(p->objectName());
            facts.insert("taoxi_use",QJsonObject{{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(false)},
                {"to",targets},{"card",publicDecisionCard(use.card)}});
        }
    }
    // Source TaoxiMove does not transfer the underlying card. showCard occurs
    // first, so lawful world disclosure + these live tags are the witness.
    const ServerPlayer *owner=idValid&&id>=0?room.getCardOwner(id):nullptr;
    if(viewer->hasFlag("TaoxiRecord")&&facts.value("taoxi_here").toBool()
        &&owner&&owner!=viewer&&room.getCardPlace(id)==Player::PlaceHand
        &&owner->handCards().contains(id)){
        for(const auto &player:world.players){
            if(player.objectName!=owner->objectName())continue;
            for(const auto &card:player.knownCards){
                if(card.effectiveId!=id||card.virtualCard)continue;
                facts.insert("taoxi_borrowed",QJsonObject{{"card_id",id},{"owner",owner->objectName()},
                    {"place","hand"},{"card",yjcm2015AiDisclosedCard(card)}});
                break;
            }
            break;
        }
    }
    // Range rules can involve third-party lords (zhaofu/jinchengwu). Refuse
    // this exact matrix unless the whole participating public skill roster is
    // complete, rather than querying a hidden general's contribution.
    QList<AIPlayerView> rangePlayers=world.players;rangePlayers.prepend(world.self);
    bool rangeKnown=true;for(const auto &p:rangePlayers)if(!p.skillsComplete)rangeKnown=false;
    QJsonObject ranges;
    {
        std::unique_ptr<AIPublicAttackRangeMatrixScope> rangeSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            rangeSource.reset(new AIPublicAttackRangeMatrixScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        if(rangeKnown){
            for(const auto &fromView:rangePlayers){
                if(!fromView.alive)continue;
                const auto *from=room.findPlayerByObjectName(fromView.objectName,true);
                if(!from){rangeKnown=false;break;}
                QSet<int> offsets{0,1};
                for(const auto &equip:fromView.equips)if(equip.weaponRange>=1)offsets.insert(equip.weaponRange-1);
                QJsonObject targets;
                for(const auto &toView:rangePlayers){
                    if(!toView.alive)continue;
                    const auto *to=room.findPlayerByObjectName(toView.objectName,true);
                    if(!to){rangeKnown=false;break;}
                    QJsonObject values;
                    for(int offset:offsets)values.insert(QString::number(offset),from->inMyAttackRange(to,offset));
                    targets.insert(toView.objectName,values);
                }
                if(!rangeKnown)break;
                ranges.insert(fromView.objectName,targets);
            }
        }
    }
    facts.insert("attack_range_known",rangeKnown);
    facts.insert("in_attack_range",rangeKnown?ranges:QJsonObject());
    world.strategyFacts.insert("yjcm2015",facts);
}

// ---- newstar ----
namespace {
QJsonValue participant(Room &room, const ServerPlayer *player)
{
    if (!player) return false;
    for (const ServerPlayer *known : room.getAllPlayers(true))
        if (known == player) return player->objectName();
    return false;
}
QString disclosedName(const AIRequest &request, int id)
{
    const auto find = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards)
            if (!card.virtualCard && (card.cardId == id || card.effectiveId == id)) return card.objectName;
        return QString();
    };
    QString name = find(request.worldView.handCards);
    if (name.isEmpty()) name = find(request.worldView.discardPile);
    const auto playerName = [&](const AIPlayerView &player) {
        QString result = find(player.equips);
        if (result.isEmpty()) result = find(player.judgingArea);
        if (result.isEmpty()) result = find(player.knownCards);
        for (const auto &pile : player.piles)
            if (result.isEmpty() && (pile.open || player.objectName == request.viewerObjectName)) result = find(pile.cards);
        return result;
    };
    if (name.isEmpty()) name = playerName(request.worldView.self);
    for (const auto &player : request.worldView.players)
        if (name.isEmpty()) name = playerName(player);
    return name;
}
QJsonObject newstarFailure(const char *reason) { return {{"error", QString::fromLatin1(reason)}}; }
}
void projectAINewstarContext(AIRequest &request, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject context = request.choiceOptions.context;
    QJsonObject values = context.value("newstar").toObject();
    // extensions/newstar.lua349 sets this exact owner tag around @@LuaJuece,
    // then removes it. The AI reads only from/to; no hidden card is described.
    if (request.kind == AIRequest::UseCard && request.pattern == "@@LuaJuece") {
        const auto damage = viewer->getTag("LuaJueceDamage").value<DamageStruct>();
        values.insert("juece_damage", QJsonObject{{"from", participant(room, damage.from)}, {"to", participant(room, damage.to)}});
    }
    // Room::damage maintains CurrentDamageStruct while asking LuaQuanbian.
    // Its callback only needs the damaged participant, not the card or tags.
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "LuaQuanbian") {
        const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
        values.insert("current_damage", QJsonObject{{"to", participant(room, damage.to)}});
    }
    if (!values.isEmpty()) {
        context.insert("newstar", values);
        request.choiceOptions.context = context;
    }
}
QJsonObject evaluateAINengchenPreview(Room &room, ServerPlayer *viewer, const AIRequest &request, int materialId)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || request.stateRevision != room.roomRuntime()->stateRevision()) return newstarFailure("Nengchen preview request mismatch");
    if (!viewer->hasSkill("LuaNengchen") || !viewer->hasFlag("jx")) return newstarFailure("Nengchen preview is not enabled");
    const bool hand = viewer->handCards().contains(materialId);
    const bool pile = viewer->getPile("wooden_ox").contains(materialId);
    if (materialId < 0 || (!hand && !pile) || room.getCardOwner(materialId) != viewer)
        return newstarFailure("Nengchen preview material is not disclosed owner material");
    const Card *material = Sanguosha->getCard(materialId);
    if (!material) return newstarFailure("Nengchen preview material is missing");
    const int rememberedId = viewer->getMark("luanengchenskill");
    QString name = disclosedName(request, rememberedId);
    const bool estimated = name.isEmpty();
    if (estimated) {
        // Writer newstar.lua73 clears its previous public mark, then publishes
        // the last physically used NDTrick name. Never inspect the remembered
        // card's current hidden wrapper, even to compare it to this old name.
        const QString prefix = "&LuaNengchen+";
        for (const QString &mark : viewer->getMarkNames()) {
            if (!mark.startsWith(prefix) || viewer->getMark(mark) <= 0) continue;
            const int suffix = mark.lastIndexOf('-');
            if (suffix <= prefix.size() || !mark.endsWith("Clear")) continue;
            const QString observed = mark.mid(prefix.size(), suffix - prefix.size());
            if (!name.isEmpty() && name != observed) return newstarFailure("Nengchen last public declaration is ambiguous");
            name = observed;
        }
        if (name.isEmpty()) return newstarFailure("Nengchen remembered face has no lawful observation");
    }
    std::unique_ptr<Card> preview(Sanguosha->cloneCard(name, material->getSuit(), material->getNumber()));
    if (!preview) return newstarFailure("Nengchen declared card cannot be cloned");
    // Fixed donor preview is deliberately NOT the paid view-as card: it has
    // no subcards and no LuaNengchen skill name. Preserve the true third arg.
    const bool limited = viewer->isCardLimited(preview.get(), Card::MethodUse, true);
    QJsonObject result{{"name", preview->objectName()}, {"class_name", preview->getClassName()},
        {"is_iron_chain", preview->isKindOf("IronChain")}, {"limited_use_hand", limited},
        {"available_checked", !limited}, {"estimated", estimated}};
    // Source short-circuits here; do not run availability hooks on a limited
    // preview or describe an unqueried value as known false.
    if (!limited) result.insert("available", preview->isAvailable(viewer));
    return result;
}

// ---- collateral-2 ----
#include "ai-strategy/source-slots.h"
#include "ai-strategy/owner.h"
#include "ai-runtime.h"
#include <atomic>
namespace {
thread_local QHash<Room*,QList<AICollateralEffect*>> effects;
thread_local QHash<Room*,QList<AICollateralSlashQuestion*>> questions;
std::atomic<quint64> serial{0};
QJsonObject collateral2Fail(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&room,const AIRequest&r,ServerPlayer*p){return p&&p->getRoom()==&room&&r.decisionId&&r.conversionLedger&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&aiRangeRequestCurrent(room,r,p);}
const Card*sourceCard(Room&room,const AIRequest&r,ServerPlayer*p,const QJsonObject&s){
 if(s.size()==1&&s.value("readonly_recipe_key").isString())return room.roomRuntime()->ai().readonlyCard(r,p,s.value("readonly_recipe_key").toString());
 if(s.size()!=2||!s.value("physical_id").isDouble()||!s.value("legacy_identity_token").isString())return nullptr;
 const int id=s.value("physical_id").toInt(-1);if(id<0||s.value("physical_id").toDouble()!=id||!p->handCards().contains(id))return nullptr;
 const Card*c=room.getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.legacyIdentityToken==s.value("legacy_identity_token").toString()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,p,c)&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber())return c;
 return nullptr;
}
}
struct AICollateralEffect::Data {Room*room;QPointer<ServerPlayer>killer,victim,user;const Card*card=nullptr;QString prompt;bool valid=false,bound=false;};
struct AICollateralSlashQuestion::Data {Room*room;AICollateralEffect*effect=nullptr;quint64 serial=0;};
struct AICollateralSlashAccess {
 static AICollateralSlashQuestion::Data*question(Room&room){auto it=questions.constFind(&room);return it==questions.cend()||it->isEmpty()?nullptr:it->last()->d.get();}
 static ServerPlayer*user(Room&room,const AIRequest&r,ServerPlayer*p){auto*q=question(room);if(!q||!q->effect||!q->serial||q->serial!=r.collateralSlashQuestionSerial)return nullptr;
  auto it=effects.constFind(&room);if(it==effects.cend()||it->isEmpty()||it->last()!=q->effect)return nullptr;const auto*d=q->effect->d.get();
  return d->valid&&d->killer==p&&d->victim&&d->user&&r.kind==AIRequest::UseCard&&r.pattern=="slash"&&r.prompt==d->prompt&&r.handlingMethod==Card::MethodUse&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&current(room,r,p)?d->user.data():nullptr;
 }
};
AICollateralEffect::AICollateralEffect(Room&room,ServerPlayer*k,ServerPlayer*v,ServerPlayer*u,const Card*c,const QString&prompt):d(new Data){
 d->room=&room;d->killer=k;d->victim=v;d->user=u;d->card=c;d->prompt=prompt;
 d->valid=k&&v&&u&&k->getRoom()==&room&&v->getRoom()==&room&&u->getRoom()==&room&&c&&c->isKindOf("Collateral")&&prompt=="collateral-slash:"+v->objectName()+":"+u->objectName();effects[&room].append(this);
}
AICollateralEffect::~AICollateralEffect(){auto&v=effects[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())effects.remove(d->room);}
AICollateralSlashQuestion::AICollateralSlashQuestion(Room&room,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&room;auto it=effects.find(&room);if(it!=effects.end()&&!it->isEmpty()){
  auto*e=it->last();auto*ed=e->d.get();if(ed->valid&&!ed->bound&&ed->killer==p&&pattern=="slash"&&prompt==ed->prompt&&method==int(Card::MethodUse)){ed->bound=true;d->effect=e;d->serial=++serial;}
 }questions[&room].append(this);
}
AICollateralSlashQuestion::~AICollateralSlashQuestion(){auto&v=questions[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())questions.remove(d->room);}
void projectAICollateralSlash(AIRequest&r,Room&room,ServerPlayer*p){
 r.collateralSlashQuestionSerial=0;
 // This key is projected only for the genuine compiled Collateral inquiry.
 // Clear only this finite field; retain unrelated mode-policy observations.
 auto rows=r.worldView.modePolicy.value("strategy_flags").toObject();
 for(auto it=rows.begin();it!=rows.end();++it){auto row=it.value().toObject();row.remove("SlashAssignee");it.value()=row;}
 r.worldView.modePolicy.insert("strategy_flags",rows);
 auto*q=AICollateralSlashAccess::question(room);if(!q)return;
 r.collateralSlashQuestionSerial=q->serial;
 if(!AICollateralSlashAccess::user(room,r,p)){r.collateralSlashQuestionSerial=0;return;}
 // Fixed native/extension writes of this literal all use setPlayerFlag, which
 // broadcasts the value. Preserve all current flags, including a preexisting
 // second assignee; the donor still selects in getOtherPlayers order.
 for(auto*target:room.getAllPlayers(true)){
  auto row=rows.value(target->objectName()).toObject();
  row.insert("SlashAssignee",target->getFlagList().contains("SlashAssignee"));
  rows.insert(target->objectName(),row);
 }
 r.worldView.modePolicy.insert("strategy_flags",rows);
}
QJsonObject queryAICollateralFlag(Room&room,const AIRequest&r,ServerPlayer*p,const QJsonObject&s){
 if(!current(room,r,p))return collateral2Fail("Collateral flag query requires exact current owner request");
 if(s.size()!=4||s.value("source")!="ordinary_collateral"||!s.value("op").isString()||!s.value("target").isString()||(!s.value("card").isObject()&&s.value("card")!=QJsonValue(false)))return collateral2Fail("Collateral flag schema differs");
 const auto op=s.value("op").toString();ServerPlayer*target=nullptr;bool value=false;
 if(op=="set"){
  if(s.value("target")!=p->objectName())return collateral2Fail("Collateral planning writer is owner-only");
  const Card*c=sourceCard(room,r,p,s.value("card").toObject());if(!c||!c->isKindOf("Collateral"))return collateral2Fail("Collateral planning writer lacks exact current source Card");
  target=p;value=true; // Donor sets even in dummy planning; repeated true is a native no-op.
 }else if(op=="read"||op=="clear"){
  target=AICollateralSlashAccess::user(room,r,p);
  if(!target||target->objectName()!=s.value("target").toString()||s.value("card")!=QJsonValue(false))return collateral2Fail("Collateral foreign flag lacks actual compiled effect/question witness");
  if(op=="read"){const bool value=target->getFlagList().contains("needCrossbow");if(!current(room,r,p))return collateral2Fail("Collateral flag read changed current");return{{"known",true},{"value",value}};}
 }else return collateral2Fail("Collateral flag operation outside finite source catalog");
 if(!current(room,r,p))return collateral2Fail("Collateral flag changed before source setter");
 AISourceEffectPermit permit(room,r,p,target,QStringLiteral("needCrossbow"),false,value);QString error;
 if(!writeAISourceEffect(room,r,p,target,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}

// ---- collateral ----
namespace {
thread_local QHash<Room*,QList<AIBenxiCollateralSource*>> sources;
thread_local QHash<Room*,QList<AIBenxiCollateralQuestion*>> collateralQuestions;
std::atomic<quint64> nextSerial{0};
QString face(const Card*c){return c->objectName()+"|"+c->getClassName()+"|"+QString::number(int(c->getSuit()))+"|"+QString::number(c->getNumber())+"|"+c->getSkillName(false)+"|"+c->toString();}
QString expectedRaw(const CardUseStruct&u){QStringList parts;parts<<u.card->toString();for(auto*t:u.to)parts<<t->objectName();parts<<"tenyearbenxi";return parts.join('+');}
}
struct AIBenxiCollateralSource::Data {Room*room;QPointer<ServerPlayer>owner;CardUseStruct use;QString raw,face;bool valid=false,bound=false;};
struct AIBenxiCollateralQuestion::Data {Room*room;QPointer<ServerPlayer>owner;AIBenxiCollateralSource*source=nullptr;quint64 serial=0;QString prompt;};
struct AIBenxiCollateralAccess {
 static AIBenxiCollateralQuestion::Data*question(Room&room){auto it=collateralQuestions.constFind(&room);return it==collateralQuestions.cend()||it->isEmpty()?nullptr:it->last()->d.get();}
 static bool current(const AIRequest&r,Room&room,ServerPlayer*p){
  auto*q=question(room);if(!q||!q->source||!q->serial||q->serial!=r.benxiCollateralQuestionSerial||q->owner!=p||!p||p->getRoom()!=&room)return false;
  const auto it=sources.constFind(&room);if(it==sources.cend()||it->isEmpty()||it->last()!=q->source)return false;
  const auto*s=q->source->d.get();return s->valid&&s->owner==p&&p->property("extra_collateral").toString()==s->raw
   &&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
   &&r.kind==AIRequest::UseCard&&r.pattern=="@@tenyearbenxi!"&&r.prompt==q->prompt&&r.handlingMethod==Card::MethodUse
   &&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&aiRangeRequestCurrent(room,r,p);
 }
 static QString raw(Room&room){return question(room)->source->d->raw;}
 static bool physical(Room&room,const Card*c){const auto*s=question(room)->source->d.get();
  // Numeric Card_Parse uses the current WrappedCard, not getRealCard. The
  // current face must still be the exact face revealed by this source use.
  return c&&c->getEffectiveId()==s->use.card->getEffectiveId()&&face(c)==s->face;
 }
};
AIBenxiCollateralSource::AIBenxiCollateralSource(Room&room,ServerPlayer*p,const CardUseStruct&u):d(new Data){
 d->room=&room;d->owner=p;d->use=u;
 if(p&&p->getRoom()==&room&&u.from==p&&u.card&&u.card->isKindOf("Collateral")&&u.to.size()==1){
  bool roster=true;for(auto*t:u.to)if(!t||room.findPlayerByObjectName(t->objectName(),true)!=t)roster=false;
  if(roster){d->face=face(u.card);d->raw=expectedRaw(u);d->valid=p->property("extra_collateral").toString()==d->raw;}
 }sources[&room].append(this);
}
AIBenxiCollateralSource::~AIBenxiCollateralSource(){auto&v=sources[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())sources.remove(d->room);}
AIBenxiCollateralQuestion::AIBenxiCollateralQuestion(Room&room,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&room;d->owner=p;d->prompt=prompt;auto it=sources.find(&room);
 if(it!=sources.end()&&!it->isEmpty()){
  auto*s=it->last();auto*sd=s->d.get();
  if(sd->valid&&!sd->bound&&sd->owner==p&&pattern=="@@tenyearbenxi!"&&prompt=="@tenyearbenxi-extra:"+sd->use.card->objectName()&&method==int(Card::MethodUse)){
   sd->bound=true;d->source=s;d->serial=++nextSerial;
  }
 }collateralQuestions[&room].append(this);
}
AIBenxiCollateralQuestion::~AIBenxiCollateralQuestion(){auto&v=collateralQuestions[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())collateralQuestions.remove(d->room);}
void projectAIBenxiCollateral(AIRequest&r,Room&room,ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("tenyear_st_standard").toObject();facts.remove("extra_collateral_current");facts.remove("extra_collateral_card");r.worldView.strategyFacts.insert("tenyear_st_standard",facts);r.benxiCollateralQuestionSerial=0;
 auto*q=AIBenxiCollateralAccess::question(room);if(!q)return;r.benxiCollateralQuestionSerial=q->serial;
 if(!AIBenxiCollateralAccess::current(r,room,p)){r.benxiCollateralQuestionSerial=0;return;}
 facts.insert("extra_collateral_current",AIBenxiCollateralAccess::raw(room));r.worldView.strategyFacts.insert("tenyear_st_standard",facts);
}
bool aiBenxiCollateralCurrent(const AIRequest&r,Room&room,ServerPlayer*p,const QString&wire){
 return AIBenxiCollateralAccess::current(r,room,p)&&wire==AIBenxiCollateralAccess::raw(room).split('+',Qt::KeepEmptyParts).first();
}
const Card*parseAIBenxiCollateral(const AIRequest&r,Room&room,ServerPlayer*p,const QString&wire,bool*authorized){
 *authorized=aiBenxiCollateralCurrent(r,room,p,wire);if(!*authorized)return nullptr;
 // This is the exact native parser and first literal source token. In
 // particular, never strip a trailing skill or repair truncated + materials.
 const Card*c=Card::Parse(wire);
 if(c&&!c->isVirtualCard()&&!AIBenxiCollateralAccess::physical(room,c)){*authorized=false;return nullptr;}
 return c;
}

// ---- rangyi ----
#include "ai-strategy/lua-seal.h"
#include "card-lifetime-manager.h"
#include "lua-wrapper.h"
#include "room-state.h"
#include "ai-strategy/lua-raw-inspect.h"
// Exact LuaSkillCard userdata decoder lives next to the generated SWIG types.
extern bool matchAIRangyiTemplate(lua_State*,int,const Card*);
namespace {
thread_local QHash<Room*,QList<AIRangyiUse*>> uses;
thread_local QHash<Room*,QList<AIRangyiCallback*>> callbacks;
thread_local QHash<Room*,QList<AIRangyiQuestion*>> rangyiQuestions;
std::atomic<quint64> rangyiNextSerial{0};
const QString path=QStringLiteral("extensions/extra.lua");
bool sourceFunction(Room&r,lua_State*L,int index,int first,int last){
 if(!L||!lua_checkstack(L,32)||!matchAIPinnedFunction(r,L,index,path,first,last))return false;
 index=lua_absindex(L,index);const int top=lua_gettop(L);bool env=false,ok=true;
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  if(QString::fromLatin1(n)!="_ENV"){ok=false;lua_pop(L,1);break;}
  AILuaInspect::globals(L);env=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!env){ok=false;break;}
 }lua_settop(L,top);return ok&&env;
}
bool sourceFilter(Room&r,lua_State*L,int index){
 if(!L||!lua_checkstack(L,64)||!matchAIPinnedFunction(r,L,index,"lua/sgs_ex.lua",640,649))return false;
 index=lua_absindex(L,index);const int top=lua_gettop(L);bool env=false,spec=false,ok=true;
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  const QByteArray name(n);
  if(name=="_ENV"){AILuaInspect::globals(L);env=lua_rawequal(L,-1,-2);lua_pop(L,2);ok=ok&&env;}
  else if(name=="spec"){
   if(!lua_istable(L,-1)){ok=false;lua_pop(L,1);break;}
   // CreateSkillCard's authentic wrapper dispatches through this exact raw
   // spec.filter; a changed filter or inherited/metatable fallback cannot pass.
   if(lua_getmetatable(L,-1)){lua_pop(L,2);ok=false;break;}
   AILuaInspect::field(L,-1,"filter");spec=sourceFunction(r,L,-1,2593,2600);lua_pop(L,2);ok=ok&&spec;
  }else{lua_pop(L,1);ok=false;break;}
 }lua_settop(L,top);return ok&&env&&spec;
}
bool productTemplate(Room&r,lua_State*L,const LuaSkillCard*c){
 if(!c||!L||!lua_checkstack(L,64)||c->objectName()!="twyj_rangyiUse"||c->getSkillName(false)!="twyj_rangyiuse")return false;
 const int top=lua_gettop(L);AILuaInspect::globals(L);AILuaInspect::field(L,-1,"twyj_rangyiUseCard");
 const bool same=matchAIRangyiTemplate(L,-1,c);lua_settop(L,top);if(!same)return false;
 c->about_to_use.push(L);const bool a=sourceFunction(r,L,-1,2609,2630);lua_settop(L,top);
 c->feasible.push(L);const bool b=sourceFunction(r,L,-1,2601,2608);lua_settop(L,top);
 c->filter.push(L);const bool f=sourceFilter(r,L,-1);lua_settop(L,top);return a&&b&&f;
}
template<class T>void pop(QHash<Room*,QList<T*>>&map,Room*r,T*p){auto i=map.find(r);if(i==map.end())return;if(!i->isEmpty()&&i->last()==p)i->removeLast();else i->clear();if(i->isEmpty())map.erase(i);}
}
struct AIRangyiUse::Data{
 Room*room;QPointer<ServerPlayer>from,to;QPointer<Card>card;
 std::shared_ptr<const CardLifetimeToken>token;CardLifetimeLease lease;bool valid=false;
 const Card*liveCard()const{
  // This is identity custody for the already-accepted native outer use, not
  // an owner readonly-physical observation or permission to inspect its tags.
  if(card.isNull()||!token||!token->live||!lease.isValid())return nullptr;
  return globalCardLifetimeManager().liveToken(card.data())==token?card.data():nullptr;
 }
};
struct AIRangyiCallback::Data{Room*room;AIRangyiUse*use=nullptr;QPointer<ServerPlayer>from,to;lua_State*game=nullptr;const void*function=nullptr;quint64 generation=0;bool valid=false,bound=false;};
struct AIRangyiQuestion::Data{Room*room;AIRangyiCallback*callback=nullptr;QPointer<ServerPlayer>viewer;quint64 serial=0;bool revoked=false;};
struct AIRangyiAccess{
 static AIRangyiQuestion::Data*question(Room&r){auto i=rangyiQuestions.find(&r);return i==rangyiQuestions.end()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool current(const AIRequest&q,Room&r,ServerPlayer*p){
  auto*d=question(r);if(!d||d->revoked||!d->callback||!d->serial||q.rangyiWitnessSerial!=d->serial||d->viewer!=p||!p)return false;
  auto c=callbacks.constFind(&r);auto u=uses.constFind(&r);if(c==callbacks.cend()||c->isEmpty()||c->last()!=d->callback||u==uses.cend()||u->isEmpty())return false;
  auto*b=d->callback->d.get();if(!b->valid||u->last()!=b->use||!b->use->d->valid||b->to!=p||!b->from||!b->use->d->liveCard())return false;
  auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||rt->lua().rawState()!=b->game||rt->lua().generation()!=b->generation)return false;
  return p->getRoom()==&r&&r.findPlayerByObjectName(p->objectName(),true)==p&&p->isAlive()
   &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.decisionId
   &&q.kind==AIRequest::UseCard&&q.pattern=="@@twyj_rangyi"&&q.prompt=="@twyj_rangyi"&&q.handlingMethod==Card::MethodUse
   &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.roomState()->getCurrentCardUsePattern()==q.pattern
   &&r.roomState()->getCurrentCardUseReason()==q.reason&&aiRangeRequestCurrent(r,q,p);
 }
};
void registerAIRangyiSource(Room&r,lua_State*L){
 registerAIPinnedSource(r,L,"lua/sgs_ex.lua",QByteArrayLiteral("26445717639068578986fefa44c396f40426b155410b47c3538e450794e4aece"));
 registerAIPinnedSource(r,L,path,QByteArrayLiteral("2cc0e3e6386fdbbbd1d4ee4c4cbc9ae3e1479ca99050182edf1e40d14b779c3a"));
}
AIRangyiUse::AIRangyiUse(Room&r,const CardUseStruct&u):d(new Data){
 d->room=&r;uses[&r].append(this);
 if(!u.card||!u.from||u.from->getRoom()!=&r||u.to.size()!=1||!u.to.first()||u.to.first()==u.from||u.to.first()->getRoom()!=&r)return;
 if(u.card->objectName()!="twyj_rangyi"||u.card->getTypeId()!=Card::TypeSkill||u.activationRef.ownerObjectName!=u.from->objectName()
  ||u.activationRef.key.skillName!="twyj_rangyi"||!u.from->hasSkillInstance("twyj_rangyi",u.activationRef.key.instanceID))return;
 d->from=u.from;d->to=u.to.first();d->card=const_cast<Card*>(u.card);
 auto&manager=globalCardLifetimeManager();d->token=manager.liveToken(u.card);
 d->lease=CardLifetimeLease(manager,d->token);d->valid=d->liveCard()!=nullptr;
}
AIRangyiUse::~AIRangyiUse(){pop(uses,d->room,this);}
AIRangyiCallback::AIRangyiCallback(Room&r,lua_State*L,int fn,const Card*card,ServerPlayer*from,const QList<ServerPlayer*>&to):d(new Data){
 d->room=&r;callbacks[&r].append(this);auto i=uses.constFind(&r);
 if(i==uses.cend()||i->isEmpty()||!r.roomRuntime()||r.roomRuntime()->isClosing()||L!=r.roomRuntime()->lua().rawState())return;
 auto*u=i->last();if(!u->d->valid||u->d->from!=from||to.size()!=1||u->d->to!=to.first()||u->d->liveCard()!=card
  ||!sourceFunction(r,L,fn,2639,2656))return;
 d->use=u;d->from=from;d->to=to.first();d->game=L;d->function=lua_topointer(L,fn);d->generation=r.roomRuntime()->lua().generation();d->valid=true;
}
AIRangyiCallback::~AIRangyiCallback(){pop(callbacks,d->room,this);}
AIRangyiQuestion::AIRangyiQuestion(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&r;d->viewer=p;
 if(auto*parent=AIRangyiAccess::question(r))parent->revoked=true;
 auto i=callbacks.find(&r);if(i!=callbacks.end()&&!i->isEmpty()){
  auto*c=i->last();auto*b=c->d.get();bool reached=false;
  if(b->valid&&b->game&&lua_checkstack(b->game,8)){
   // The first Lua caller must be the real captured on_use, at its sole
   // source askForUseCard call. Earlier obtain/mark sibling callbacks cannot
   // mint a recipient question merely by reusing the same prompt.
   for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(b->game,level,&ar)||!lua_getinfo(b->game,"Slf",&ar))break;
    const void*fn=lua_topointer(b->game,-1);lua_pop(b->game,1);
    if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;
    reached=fn==b->function&&ar.currentline==2649;break;
   }
  }
  if(reached&&b->valid&&!b->bound&&b->to==p&&pattern=="@@twyj_rangyi"&&prompt=="@twyj_rangyi"&&method==int(Card::MethodUse)){
   b->bound=true;d->callback=c;d->serial=++rangyiNextSerial;
  }
 }rangyiQuestions[&r].append(this);
}
AIRangyiQuestion::~AIRangyiQuestion(){pop(rangyiQuestions,d->room,this);}
void projectAIRangyi(AIRequest&q,Room&r,ServerPlayer*p){
 q.rangyiWitnessSerial=0;auto*d=AIRangyiAccess::question(r);if(!d)return;q.rangyiWitnessSerial=d->serial;
 if(!AIRangyiAccess::current(q,r,p)){q.rangyiWitnessSerial=0;return;}
 q.worldView.decisionContext.insert("round52_rangyi",QJsonObject{{"known",true}});
}
bool aiRangyiCurrent(const AIRequest&q,Room&r,ServerPlayer*p,int id){
 if(!AIRangyiAccess::current(q,r,p)||id<0||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand||!p->handCards().contains(id))return false;
 const Card*c=r.getCard(id);if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||!c->hasFlag("twyj_rangyi"))return false;
 for(const auto&v:q.worldView.handCards)if(v.effectiveId==id)return !v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(r,p,c)
  &&v.nativeIdentity==globalCardLifetimeManager().liveToken(c)
  &&v.cardId==c->getId()&&!v.virtualCard&&v.subcardIds==c->getSubcards()
  &&v.skillName==c->getSkillName(false)&&v.sourceSkillName==c->getSourceSkillName()
  &&v.typeId==int(c->getTypeId())&&v.handlingMethod==int(c->getHandlingMethod())
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber();
 return false;
}
Card*createAIRangyiProduct(const AIRequest&q,Room&r,ServerPlayer*p,int id,bool selected,QString*error){
 auto fail=[&](const char*s)->Card*{if(error)*error=QString::fromLatin1(s);return nullptr;};
 if(!aiRangyiCurrent(q,r,p,id))return fail("Rangyi exact recipient question/material changed");
 auto*rt=r.roomRuntime();lua_State*L=rt->lua().state();const auto*t=rt->luaSkillCard("twyj_rangyiUse");
 if(!productTemplate(r,L,t))return fail("Rangyi raw source template changed");
 // Source clones this exact registered object, with no V2 attribution.
 std::unique_ptr<LuaSkillCard> c(t->clone());if(!c)return fail("Rangyi native clone failed");c->addSubcard(id);
 if(c->getSkillName(false)!="twyj_rangyiuse"||c->objectName()!="twyj_rangyiUse"||c->getSubcards()!=QList<int>{id})return fail("Rangyi source product differs");
 if(selected&&!r.getCard(id)->isAvailable(p))return fail("Rangyi selected physical card is not available");
 if(!aiRangyiCurrent(q,r,p,id)||rt->luaSkillCard("twyj_rangyiUse")!=t||!productTemplate(r,L,t))return fail("Rangyi source changed during construction");
 return c.release();
}
Card*rebuildAIRangyiProduct(const AIRequest&q,Room&r,ServerPlayer*p,const AICardConversionView&t){
 if(!t.rangyiQuestion||t.rangyiQuestionSerial!=q.rangyiWitnessSerial||t.producerVersion!=0||t.policyClass!="@rangyi_question"
  ||t.producerReason!=q.reason||t.producerPattern!=q.pattern||t.subcardIds.size()!=1||t.costCount!=1||t.activationRef.isValid()||t.sourceRef.isValid())return nullptr;
 std::unique_ptr<Card>c(createAIRangyiProduct(q,r,p,t.subcardIds.first(),true));
 if(!c||c->objectName()!=t.name||c->getClassName()!=t.className||int(c->getSuit())!=t.suit||c->getNumber()!=t.number||c->getSkillName(false)!=t.skillName||c->toString()!=t.legacyStringKey)return nullptr;
 return c.release();
}

// ---- faqi-response ----
#include <QUuid>
#include <QWeakPointer>
namespace {
struct Saved {
 quint64 epoch=0; bool synchronized=false;
 QPointer<ServerPlayer> viewer;
 QString token;
 AIReadonlyCardLease lease;
 QStringList targets;
 QList<int> materials;
 int physical=-1,offered=-1;
};
struct Receipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class Ledger final:public QObject {
 public:explicit Ledger(Room &room):QObject(&room){} quint64 epoch=1;bool exhausted=false;QHash<QString,Saved> plans;QList<Receipt> receipts;
};
Ledger *faqiResponseLedger(Room *room,bool create=false){
 if(!room)return nullptr;for(QObject *child:room->children())if(auto *found=dynamic_cast<Ledger*>(child))return found;
 return create?new Ledger(*room):nullptr;
}
bool sameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec specOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject jsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool faqiResponseCurrent(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool shown(const AIRequest&r,int id){
 for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
 for(const auto &c:r.worldView.discardPile)if(c.effectiveId==id)return true;
 for(const auto &c:r.choiceOptions.cards)if(c.effectiveId==id)return true;
 QList<AIPlayerView> roster=r.worldView.players;roster.prepend(r.worldView.self);
 for(const auto &p:roster){
  for(const auto &c:p.equips)if(c.effectiveId==id)return true;
  for(const auto &c:p.judgingArea)if(c.effectiveId==id)return true;
  for(const auto &c:p.knownCards)if(c.effectiveId==id)return true;
  for(const auto &pile:p.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 }
 return false;
}
bool recipient(const AIRequest&r,const ServerPlayer*p,int chosen){
 if(r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
   ||r.pattern!="@@faqi"||r.handlingMethod!=Card::MethodUse)return false;
 // Only a genuinely issued current Faqi action establishes its native pending
 // choice. Lua's remembered scalar/mark cannot confer this question permission.
 QList<AiSkillActionContext> actions=r.skillActions;
 if(r.hasSkillActionContext)actions.prepend(r.skillActionContext);
 for(const auto &a:actions)if(a.activationRef.ownerObjectName==p->objectName()
   &&a.activationRef.key.skillName=="faqi"&&a.activationRef.key.instanceID>0
   &&p->hasSkillInstance("faqi",a.activationRef.key.instanceID)
   &&!p->isSkillInvalid("faqi",a.activationRef.key.instanceID)
   &&p->getSkillInstanceStateValue("faqi",a.activationRef.key.instanceID,"pendingCardId",-1).toInt()==chosen)
   return true;
 return false;
}
}
QString captureAIFaqiPlan(const AIRequest&r,ServerPlayer*p,int offered,
 AIReadonlyCardLease lease,const QStringList &targets,QString *error,const QJsonObject &sourceFields){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return QString();};
 if(!faqiResponseCurrent(r,p)||r.kind!=AIRequest::AmazingGrace||r.choiceOptions.reason!="faqi"
   ||!r.choiceOptions.cardIds.contains(offered))return fail("Faqi capture requires actual offered AG choice");
 bool offeredFace=false;for(const auto &c:r.choiceOptions.cards)if(c.effectiveId==offered)offeredFace=true;
 if(!offeredFace||!lease)return fail("Faqi capture lacks disclosed offer or native policy custody");
 const Card *card=lease();if(!card)return fail("Faqi selected policy graph expired");
 // Do not read material-derived suit/number/name/string before disclosure.
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();
 if(physical>=0&&!shown(r,physical))return fail("Faqi selected physical object is not disclosed");
 const auto materials=card->getSubcards();
 for(int id:materials)if(!shown(r,id))return fail("Faqi selected material is not disclosed");
 QSet<QString> roster{p->objectName()};for(const auto &other:r.worldView.players)roster.insert(other.objectName);
 for(const auto &target:targets)if(!roster.contains(target))return fail("Faqi selected target is outside public roster");
 Room *room=p->getRoom();
 auto *ownerLedger=faqiResponseLedger(room,true);if(ownerLedger->exhausted)return fail("Faqi continuity epoch exhausted");
 Saved s;s.epoch=ownerLedger->epoch;s.viewer=p;s.token="faqi-observation:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 s.lease=std::move(lease);s.targets=targets;s.materials=materials;s.physical=physical;s.offered=offered;
 if(!sourceFields.isEmpty()){
  const QString actual=writeAIFaqiSlot(r,p,offered,s.lease,targets,sourceFields,error);
  if(actual.isEmpty())return {};s.synchronized=true;s.token=actual;
 }
 ownerLedger->plans[p->objectName()]=s;if(error)error->clear();return s.token;
}
const Card *restoreAIFaqiPlan(const AIRequest&r,ServerPlayer*p,const QString&token,
 QStringList *targets,QString *error,AIFaqiOutcome*sourceStatus){
 auto fail=[&](const char*m)->const Card*{if(error)*error=QString::fromLatin1(m);return nullptr;};
 if(!faqiResponseCurrent(r,p))return fail("Faqi saved observation requires current owner request");
 auto *room=faqiResponseLedger(p->getRoom());if(!room)return fail("Faqi saved observation absent");
 auto saved=room->plans.constFind(p->objectName());
 if(room->exhausted||saved==room->plans.cend()||(!saved->synchronized&&saved->epoch!=room->epoch)||saved->viewer!=p||token.isEmpty()||saved->token!=token)
  return fail("Faqi observation token does not match current viewer slot");
 if(!recipient(r,p,saved->offered))return fail("Faqi observation does not match issued continuation and pending choice");
 if(saved->synchronized){
  int offered=-1;AIFaqiSlotStatus status=AIFaqiSlotStatus::Unknown;
  const Card*actual=readAIFaqiSlot(r,p,token,targets,&offered,error,&status);
  if(sourceStatus){if(status==AIFaqiSlotStatus::SourcePass)*sourceStatus=AIFaqiOutcome::SourcePass;
   else if(status==AIFaqiSlotStatus::SourceError)*sourceStatus=AIFaqiOutcome::SourceError;}
  if(!actual||offered!=saved->offered)return nullptr;
  return actual;
 }
 if(saved->physical>=0&&!shown(r,saved->physical))return fail("Faqi physical observation is no longer disclosed");
 for(int id:saved->materials)if(!shown(r,id))return fail("Faqi material observation is no longer disclosed");
 const Card *card=saved->lease();if(!card)return fail("Faqi saved native graph expired");
 if(card->getSubcards()!=saved->materials)return fail("Faqi saved material sequence changed outside observation");
 if(targets)*targets=saved->targets;if(error)error->clear();return card;
}
void interruptAIFaqiPlansAsync(Room&r)noexcept{
 // Retain custody until normal cleanup; destroying a lease during a Lua GC or
 // debug-hook notification could dispatch native deletion side effects.
 if(auto*l=faqiResponseLedger(&r)){
  if(l->epoch==std::numeric_limits<quint64>::max())l->exhausted=true;
  else ++l->epoch;
 }
}
void clearAIFaqiPlans(Room&r){if(auto*l=faqiResponseLedger(&r)){
 // Old overlays still invalidate unconditionally. Actual synchronized source
 // slots are independently re-read on EVERY reconstruction and final apply.
 for(auto i=l->plans.begin();i!=l->plans.end();)if(!i->synchronized||!hasAIFaqiSlot(r,i->viewer,i->token))i=l->plans.erase(i);else ++i;
 for(auto i=l->receipts.begin();i!=l->receipts.end();){auto*p=r.findPlayerByObjectName(i->viewer,true);if(!p||!hasAIFaqiSlot(r,p,i->token))i=l->receipts.erase(i);else ++i;}
}}
AIFaqiSelection prepareAIFaqiSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const std::function<bool(CardUseStruct&)> &admit){
 AIFaqiSelection out;
 const Card *saved=restoreAIFaqiPlan(r,p,token,&out.targets,&out.error,&out.outcome);
 if(!saved||!admit)return out;
 auto *observations=faqiResponseLedger(p->getRoom());const quint64 observedEpoch=observations->epoch;
 const bool synchronized=hasAIFaqiSlot(*p->getRoom(),p,token);
 // Exact fixed source @@faqi serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Faqi source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 if(!physicalWire)out.use.setOwnedCard(const_cast<Card*>(parsed));else out.use.card=parsed;
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Faqi target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (!v2 || (skill!="faqi" && skill!="limu"))){
  out.error="Faqi original provider is outside audited inquiry-free reconstruction";return out;
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(!v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Faqi non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Faqi current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Faqi non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Faqi current original native provider rejected saved source";return out;}
 if(!synchronized&&(observations->exhausted||observations->epoch!=observedEpoch)){
  out.error="Faqi reconstruction crossed an uncertified gameplay entry";return out;
 }
 if(synchronized){
  QStringList actualTargets;int offered=-1;QString why;
  const Card*actual=readAIFaqiSlot(r,p,token,&actualTargets,&offered,&why);
  if(!actual||actual!=saved||actualTargets!=out.targets||actual->toString()!=out.sourceWire){
   out.error=why.isEmpty()?"Faqi actual source changed during current native admission":why;return out;
  }
 }
 if(!faqiResponseCurrent(r,p)){out.error="Faqi reconstruction changed current request state";return out;}
 out.outcome=AIFaqiOutcome::Ready;out.error.clear();return out;
}

std::unique_ptr<Card> constructAIFaqiRevise(const AIRequest&r,ServerPlayer*p,
 const QString&kind,const QList<int>&costs,QString *error){
 auto fail=[&](const char*m)->std::unique_ptr<Card>{if(error)*error=m;return {};};
 if(!faqiResponseCurrent(r,p)||r.kind!=AIRequest::AmazingGrace||r.choiceOptions.reason!="faqi"||!p->hasSkill("limu"))
  return fail("Limu readonly revise constructor requires current Faqi AG and actual owner skill");
 if((kind=="limu"&&costs.size()!=1)||(kind=="spear"&&costs.size()!=2)||(kind!="limu"&&kind!="spear"))
  return fail("Limu source constructor cost shape changed");
 for(int id:costs)if(!shown(r,id)||(!p->handCards().contains(id)&&!p->getEquipsId().contains(id)))
  return fail("Limu source constructor material is not disclosed own HE");
 std::unique_ptr<Card> card;
 if(kind=="limu")card.reset(const_cast<Card*>(Card::Parse("@LimuCard="+QString::number(costs.first()))));
 else{card.reset(Sanguosha->cloneCard("slash",Card::NoSuit,0));if(card){card->setSkillName("spear");card->addSubcards(costs);}}
 if(error)error->clear();return card;
}
QJsonObject issueAIFaqiReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIFaqiSelection(r,p,token,admit);
 if(selected.outcome==AIFaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIFaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIFaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIFaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Faqi request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=specOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@faqi_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=faqiResponseLedger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
bool hasAIFaqiReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@faqi_saved_reconstruction")return true;
 return false;
}
bool applyAIFaqiReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!faqiResponseCurrent(r,p)||!hasAIFaqiReceipt(r,spec.conversionId))return false;
 auto *l=faqiResponseLedger(p->getRoom());if(!l)return false;
 const Receipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const Receipt receipt=*found;
 if(!sameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 auto selected=prepareAIFaqiSelection(r,p,receipt.token,admit);
 if(selected.outcome!=AIFaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!sameSpec(specOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}

// ---- faqi-slot ----
#include "ai-strategy/inovation-ai.h"
#include "lua-runtime.h"
#include <cstring>
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct FaqiSlotRow{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int tableRef=LUA_NOREF,offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class FaqiSlotLedger final:public QObject{
public:explicit FaqiSlotLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,FaqiSlotRow>rows;
};
FaqiSlotLedger*faqiSlotLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<FaqiSlotLedger*>(c))return l;return create?new FaqiSlotLedger(r):nullptr;}
bool faqiSlotCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision()&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool faqiSlotShown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact)exact=Sanguosha->getCard(id);if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void field(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){Stack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&env(L,f);}
bool sourceCallback(Room&r,lua_State*L){
 Stack stack(L);AILuaInspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 field(L,-1,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@faqi");
 return code(r,L,-1,"lua/ai/doudizhu-ai.lua",158,167);
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L){
 Stack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 field(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@faqi");
 if(!code(r,L,-1,"lua/ai/doudizhu-ai.lua",158,167))return false;
 field(L,sgs,"list");if(!code(r,L,-1,"lua/utilities.lua",38,41))return false;
 field(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=code(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=code(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){field(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 field(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){field(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct Write {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Faqi assignment inspection stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"Faqi native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Faqi native target list unavailable");lua_setfield(L,d,"to");
 if(!faqiSlotCurrent(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Faqi actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 field(L,1,"faqiUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Faqi source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"faqiUse");
 lua_pushvalue(L,d);return 1;
}
}
void clearAIFaqiSlots(Room&r){auto*l=faqiSlotLedger(r);if(!l)return;
 auto*rt=r.roomRuntime();if(rt&&rt->lua().rawState()&&rt->lua().generation()==l->generation){
  LuaRuntime::Binding b(rt->lua(),false);auto*L=rt->lua().state();if(L==l->L)for(const auto&row:l->rows)if(row.tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,row.tableRef);
 }delete l;
}
bool hasAIFaqiSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=faqiSlotLedger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAIFaqiSlot(const AIRequest&q,ServerPlayer*p,int offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!faqiSlotCurrent(q,p)||q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="faqi"||!q.choiceOptions.cardIds.contains(offered)||!lease)return fail("Faqi source assignment requires actual current offered AG");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Faqi full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Faqi exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!faqiSlotShown(q,physical,card))return fail("Faqi source physical card undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!faqiSlotShown(q,id))return fail("Faqi source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Faqi game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("Faqi actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 field(L,receiver,"faqiUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Faqi source __newindex is not the direct assignment domain");
 Write w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Faqi source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Faqi native source assignment failed before completion");
 if(!faqiSlotCurrent(q,p)||!lease())return fail("Faqi source assignment changed current request or custody");
 field(L,receiver,"faqiUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Faqi assignment no longer names its actual source table");
 auto*l=faqiSlotLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Faqi synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 FaqiSlotRow row;row.owner=p;row.lease=std::move(lease);row.token="faqi-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 lua_pushvalue(L,-1);row.tableRef=luaL_ref(L,LUA_REGISTRYINDEX);
 auto old=l->rows.find(p);if(old!=l->rows.end()&&old->tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,old->tableRef);
 l->rows[p]=row;
 if(!faqiSlotCurrent(q,p)||!row.lease())return fail("Faqi source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAIFaqiSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,int*offered,QString*error,AIFaqiSlotStatus*status){
 if(status)*status=AIFaqiSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!faqiSlotCurrent(q,p))return fail("Faqi actual source read requires current owner request");Room&r=*p->getRoom();auto*l=faqiSlotLedger(r);auto*rt=r.roomRuntime();
 if(!l||l->generation!=rt->lua().generation()||!hasAIFaqiSlot(r,p,token))return fail("Faqi actual slot provenance unavailable");
 const FaqiSlotRow row=l->rows.value(p);
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||L!=l->L||!lua_checkstack(L,4096))return fail("Faqi actual source runtime unavailable");Stack stack(L);
 if(!sourceCallback(r,L))return fail("Faqi actual source callback changed");
 if(!self(L,r,p))return fail("Faqi actual LuaAI self changed");if(!lookup(L,-1,"faqiUse"))return fail("Faqi own slot lookup requires unproved dynamic getter");
 // The source indexes dummy.card before any truth test. Nil/boolean dummy
 // therefore is an ordinary source error, never a successful decline.
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  const int type=lua_type(L,-1);if(AILuaInspect::primitiveMetatable(L,type))return fail("Faqi dummy primitive has an unproved index metamethod");
  if(status)*status=AIFaqiSlotStatus::SourceError;
  return fail(type==LUA_TNIL?"attempt to index a nil value (local 'dummy')":"attempt to index a boolean value (local 'dummy')");
 }
 if(!lua_istable(L,-1))return fail("Faqi source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!lookup(L,d,"card"))return fail("Faqi card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!faqiSlotCurrent(q,p))return fail("Faqi source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AIFaqiSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!lookup(L,d,"to"))return fail("Faqi target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 if(!lua_toboolean(L,list))return pass(); // do not inspect truthy card userdata yet
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. The old tableRef is NOT authority.
 if(!plain(L,d))return fail("Faqi truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Faqi actual source Card custody expired");
 if(!matchAISlotCard(L,value,card)||!matchAINativeObjectMethods(r,L,value,"Card",{"toString"}))return fail("Faqi actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAISlotTargets(L,list,actual))return fail("Faqi actual source target list custody changed");
 if(!sourceReadBindings(r,L))return fail("Faqi actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!faqiSlotShown(q,row.physical,card))return fail("Faqi actual source physical observation expired");
 for(int id:row.materials)if(!faqiSlotShown(q,id))return fail("Faqi actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Faqi actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Faqi actual source target is not a live roster identity");names<<t->objectName();}
 if(!faqiSlotCurrent(q,p)||row.lease()!=card)return fail("Faqi actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AIFaqiSlotStatus::Card;return card;
}

// ---- poxi-effects ----
#include <typeinfo>
namespace {
struct PoxiEffectsStack{lua_State*L;int n;~PoxiEffectsStack(){lua_settop(L,n);}};
bool no(QString*r,const QString&s){if(r)*r=s;return false;}
bool poxiEffectsPlain(lua_State*L,int i){auto*t=AILuaInspect::table(L,i);return t&&!t->metatable;}
bool require(const AIPoxiEffectProof&p,const QString&key,QString*r){return p.dependency?p.dependency(key,r):no(r,"Poxi unproved dependency: "+key);}
bool callback(lua_State*L,const LuaFunction&ref,const QString&where,const AIPoxiEffectProof&p,QString*r){
 // Fixed generated wrappers use zero for an absent function. Do not treat an
 // invalid nonzero reference as a native default or silently skip it.
 if(ref==0)return true;
 PoxiEffectsStack s{L,lua_gettop(L)};
 if(ref.state()!=L||ref.generation()!=LuaRuntime::fromState(L)->generation())return no(r,where+": callback generation/state mismatch");
 AILuaInspect::rawGeti(L,LUA_REGISTRYINDEX,ref.registryRef());
 if(!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return no(r,where+": missing/non-Lua correction callback");
 return p.luaFunction?p.luaFunction(L,lua_absindex(L,-1),where,r):no(r,where+": complete Lua effect proof unavailable");
}
bool poxiEffectsEnv(lua_State*L,int index){
 index=lua_absindex(L,index);PoxiEffectsStack s{L,lua_gettop(L)};
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  if(std::strcmp(n,"_ENV"))return false;
  AILuaInspect::globals(L);bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
 }return true;
}
}
bool proveAIPoxiCorrectionUnion(Room&r,lua_State*L,const AIPoxiEffectProof&p,QString*why){
 if(!L||r.getLuaState()!=L||!AILuaInspect::capacity(L,64))return no(why,"Poxi correction union outside native gameplay state");
 EngineRuntimeContextScope scope(*Sanguosha,&r);
 // Prove all invalidity definitions BEFORE any caller can branch on hasSkill.
 // Include even definitions whose current native mayReject prefilter is false.
 for(const InvaliditySkill*skill:Sanguosha->getInvaliditySkills()){
  if(!skill)return no(why,"null invalidity definition");
  if(const auto*q=dynamic_cast<const LuaInvaliditySkill*>(skill)){
   if(!callback(L,q->skill_valid,"invalidity:"+skill->objectName()+".skill_valid",p,why))return false;
  }else if(typeid(*skill)!=typeid(InvaliditySkill)){
   if(!p.nativeSkill||!p.nativeSkill(skill,"InvaliditySkill.isSkillValid",why))return no(why,"unproved native invalidity provider: "+skill->objectName());
  }
 }
 // Both getFixed and getCorrect are in the source branch union. Enumerate all
 // V2 definitions even when no current hidden holder/instance would select it.
 for(const DistanceSkill*skill:Sanguosha->getDistanceSkills()){
  if(!skill)return no(why,"null distance definition");
  if(const auto*q=dynamic_cast<const LuaDistanceSkillV2*>(skill)){
   if(!callback(L,q->correct_func,"distance-v2:"+skill->objectName()+".correct_func",p,why)
      ||!callback(L,q->fixed_func,"distance-v2:"+skill->objectName()+".fixed_func",p,why))return false;
  }else if(const auto*q=dynamic_cast<const LuaDistanceSkill*>(skill)){
   if(!callback(L,q->correct_func,"distance:"+skill->objectName()+".correct_func",p,why)
      ||!callback(L,q->fixed_func,"distance:"+skill->objectName()+".fixed_func",p,why))return false;
  }else if(typeid(*skill)!=typeid(DistanceSkill)&&typeid(*skill)!=typeid(DistanceSkillV2)){
   if(!p.nativeSkill||!p.nativeSkill(skill,"DistanceSkill.fixed+correct",why))return no(why,"unproved native distance provider: "+skill->objectName());
  }
 }
 // V2 holder expansion itself calls instance availability/invalidity. Its
 // fixed native body must be checked as well, not merely the final Lua refs.
 return require(p,"Engine.evaluateCorrectSkill+correctSkillHolders+Player.isSkillInstanceEffectAvailable.native_union",why);
}
bool proveAIPoxiSuppressionBindings(Room&r,lua_State*L,int method,int ns,const AIPoxiEffectProof&p,QString*why){
 if(!L||!AILuaInspect::capacity(L,128))return no(why,"Poxi suppression inspection unavailable");
 PoxiEffectsStack stack{L,lua_gettop(L)};method=lua_absindex(L,method);ns=lua_absindex(L,ns);
 if(!matchAIPinnedFunction(r,L,method,"lua/ai/smart-ai.lua",2366,2431,why)||!poxiEffectsEnv(L,method))return no(why,"Poxi shouldSuppressIntention exact provider/environment changed");
 if(!lua_istable(L,ns))return no(why,"Poxi sgs namespace absent");
 // Native SWIG sgs carries its captured __index/__newindex table. Authenticate
 // exact read/write keys; do not falsely require the whole namespace be plain.
 for(const auto&name:QStringList{"ai_liuli_effect","ai_lijian_effect"})
  if(!matchAINativeNamespaceWrite(r,L,"sgs",name))return no(why,"Poxi namespace write route changed: "+name);
 // Shared service proves this is the original namespace with unmodified
 // binding routes; merely being a plain attacker-supplied table is insufficient.
 if(!require(p,"captured_sgs_namespace_identity+raw_required_keys_or_exact_native_getters",why)
    ||!require(p,"sgs.QList2Table+sgs.list.full_pinned_union",why))return false;
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"ipairs");
 if(!matchAINativeBinding(r,L,-1,"ipairs"))return no(why,"Poxi ipairs native binding changed");lua_pop(L,2);
 AILuaInspect::field(L,ns,"ai_suppress_intention");
 if(!poxiEffectsPlain(L,-1))return no(why,"Poxi intention lookup has unproved __index");lua_pop(L,1);
 // Truthiness of suppression-table values and the two global booleans is never
 // used here. All private outcomes have the same proof result.
 for(const auto&name:QStringList{"hasFlag","isKindOf","toString","getSkillNames"})
  if(!matchAINativeDerivedMethods(r,L,"Card",{name}))return no(why,"Poxi card native binding changed: "+name);
 for(const auto&name:QStringList{"hasSkill","distanceTo","hasFlag","getMark","inMyAttackRange"})
  if(!matchAINativeDerivedMethods(r,L,"Player",{name}))return no(why,"Poxi player native binding changed: "+name);
 if(!require(p,"Card.flags+kind+serialization+change_cards.native_lifetime_union",why))return false;
 // Keep even the Slash/lolita alternative until an independently proved
 // immutable construction/effect graph allows its exclusion. Mutable current
 // isDamageCard/isKindOf observations cannot establish continuity.
 if(!require(p,"Player.inMyAttackRange.full_native_targetmod_attackrange_union",why))return false;
 return proveAIPoxiCorrectionUnion(r,L,p,why);
}
bool certifyAIPoxiFilterBody(Room&r,lua_State*L,int self,const AINaturalFilterProof&proof,const AIPoxiEffectProof&p,QString*why){
 if(!L||!AILuaInspect::capacity(L,128))return no(why,"Poxi body metadata stack unavailable");
 PoxiEffectsStack stack{L,lua_gettop(L)};self=lua_absindex(L,self);
 bool actual=inspectAINaturalFilterEntry(r,L,4,[&](int event,ServerPlayer*,const QVariant&){return event==proof.event;});
 if(!actual)return no(why,"Poxi body has no authenticated actual filterEvent tuple");
 if(!require(p,"actual_self_is_captured_native_AI_instance",why))return false;
 if(!require(p,"Poxi.native_factory_to_current_event_frame.complete_effect_union",why))return false;
 // These exact events have no event-specific body in fixed SmartAI:filterEvent;
 // the common prelude, complete event callbacks, and trailing common effects
 // are still obligations of the central verifier. No claim about private data.
 switch(proof.event){
 case TargetSpecifying:case TargetConfirming:case TargetConfirmed:
 case EventSkillWillInvoke:case EventSkillPay:case EventSkillTargetConfirming:
 case EventSkillInvoking:case EventSkillEffect:case EventSkillEffectTarget:
 case CardAsked:case MarkChanged:case EventAcquireSkill:return true;
 case PreCardUsed:return require(p,"Poxi.PreCardUsed.full_getLord+conversion+whocard+choicemade_union",why);
 case TargetSpecified:return require(p,"Poxi.TargetSpecified.actual_suppress_binding+complete_intention+damage_union",why);
 case CardEffect:return require(p,"Poxi.CardEffect.clone+prior_cardEffect_deletion+native_lifetime_union",why);
 default:return no(why,"Poxi event outside pre-consumer finite body union");
 }
}
bool certifyAIPoxiFilterBody(Room&r,lua_State*L,int self,const AINaturalFilterProof&p,QString*why){
 return certifyAIPoxiFilterBody(r,L,self,p,AIPoxiEffectProof{},why);
}

// ---- poxi-response ----
namespace {
struct Observation {QPointer<ServerPlayer>target;QList<int>ids;QJsonObject printed;SkillInstanceRef activation;quint64 revision=0;};
class PoxiResponseLedger final:public QObject {public:explicit PoxiResponseLedger(Room&r):QObject(&r){}QHash<ServerPlayer*,Observation>observations;};
PoxiResponseLedger*poxiResponseLedger(Room&r,bool create){for(auto*c:r.children())if(auto*l=dynamic_cast<PoxiResponseLedger*>(c))return l;return create?new PoxiResponseLedger(r):nullptr;}
SkillInstanceRef poxiResponseCurrent(Room&r,ServerPlayer*p,const QString&skill){
 if(!p)return {};const int id=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
 const SkillInstanceRef ref(p->objectName(),SkillInstanceKey(skill,id));return r.isAcceptedViewAsEffect(ref)?ref:SkillInstanceRef();
}
bool scope(const AIRequest&r,Room&room,ServerPlayer*p,const QString&skill){
 return p&&p->getRoom()==&room&&r.decisionId&&r.viewerObjectName==p->objectName()
  &&r.worldView.self.objectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision()
  &&r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&r.pattern=="@@"+skill&&r.handlingMethod==Card::MethodUse
  &&r.skillActionContext.activationRef==poxiResponseCurrent(room,p,skill)&&r.skillActionContext.activationRef.isValid();
}
const Observation*observed(const AIRequest&r,Room&room,ServerPlayer*p){
 auto*l=poxiResponseLedger(room,false);if(!l||!scope(r,room,p,"poxi"))return nullptr;
 const auto it=l->observations.constFind(p);if(it==l->observations.cend())return nullptr;
 const auto&o=it.value();if(!o.target||o.activation!=r.skillActionContext.activationRef
  ||o.revision!=r.stateRevision||r.prompt!="@poxi:"+o.target->objectName())return nullptr;
 return &o;
}
}
void recordAIPoxiNotification(Room&room,ServerPlayer*viewer,const QList<int>&ids,const QString&reason,bool active){
 if(reason!="poxi"||!viewer)return;auto*l=poxiResponseLedger(room,true);l->observations.remove(viewer);
 if(!active||ids.isEmpty())return;const auto ref=poxiResponseCurrent(room,viewer,"poxi");if(!ref.isValid())return;
 const auto notification=readAIStrategyNotifiedPile(room,viewer,"#poxi");
 const auto locations=notification.value("locations").toObject(),printed=notification.value("cards").toObject();
 Observation o;o.activation=ref;o.revision=room.roomRuntime()->stateRevision();QSet<int>unique;
 for(int id:ids){const auto location=locations.value(QString::number(id)).toObject();
  if(id<0||unique.contains(id)||!printed.value(QString::number(id)).isObject()
   ||location.value("place").toInt(-1)!=int(Player::PlaceHand))return;
  auto*target=room.findPlayerByObjectName(location.value("owner").toString(),true);
  if(!target||target==viewer||(o.target&&o.target!=target))return;
  o.target=target;unique.insert(id);o.ids<<id;
 }
 o.printed=printed;l->observations.insert(viewer,o);
}
void projectAILeiExchange(AIRequest&r,Room&room,ServerPlayer*p){
 const auto*o=observed(r,room,p);if(!o)return;
 QJsonArray ids,faces;bool currentComplete=true;
 for(int id:o->ids){ids.append(id);
  // A fake-pile notification authenticates order and printed identity only.
  // Never consult a foreign live wrapper to decide whether it is unchanged.
  // Existing request knownCards is built only after viewer visibility proves
  // the live wrapped face. Fake-pile printed cards are not added to that list.
  const AICardView*known=nullptr;
  for(const auto&player:r.worldView.players)if(player.objectName==o->target->objectName())
   for(const auto&card:player.knownCards)if(card.effectiveId==id){known=&card;break;}
  if(!known){currentComplete=false;continue;}
  faces.append(QJsonObject{{"id",id},{"effective_id",id},{"name",known->objectName},
   {"class_name",known->className},{"suit",known->suit},{"number",known->number},
   {"virtual_card",false},{"subcards",QJsonArray()}});
 }
 QJsonObject row{{"known",true},{"target",o->target->objectName()},{"card_ids",ids},
  {"current_faces_complete",currentComplete},{"cards",faces},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()}};
 auto facts=r.worldView.strategyFacts.value("lei").toObject();facts.insert("poxi_disclosure",row);r.worldView.strategyFacts.insert("lei",facts);
}
int aiLeiPrototype(const AIRequest&r,const ServerPlayer*viewer,const QString&skill,const QString&prototype){
 static const QHash<QString,QString>prototypes{{"poxi","PoxiDisCard"},{"hongju","HongjuCard"},{"olhongju","OLHongjuCard"},{"mobilehongju","MobileHongjuCard"}};
 if(!viewer||!viewer->getRoom()||!prototypes.contains(skill)||prototypes.value(skill)!=prototype)return -1;
 auto*p=const_cast<ServerPlayer*>(viewer);return scope(r,*p->getRoom(),p,skill)?1:-1;
}
bool authorizeAIPoxiEvaluation(const AIRequest&r,int id,QJsonObject*face){
 if(face)*face={};if(r.kind!=AIRequest::UseCard||r.pattern!="@@poxi"||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)return false;
 const auto row=r.worldView.strategyFacts.value("lei").toObject().value("poxi_disclosure").toObject();
 if(row.value("known")!=QJsonValue(true)||row.value("current_faces_complete")!=QJsonValue(true)
  ||row.value("decision_id")!=QJsonValue(r.getDecisionId())||row.value("state_revision")!=QJsonValue(r.getStateRevision()))return false;
 const auto ids=row.value("card_ids").toArray(),cards=row.value("cards").toArray();if(ids.size()!=cards.size())return false;
 for(int n=0;n<ids.size();++n)if(ids.at(n)==QJsonValue(id)){
  const auto c=cards.at(n).toObject();if(c.value("effective_id")!=QJsonValue(id))return false;if(face)*face=c;return true;
 }
 return false;
}
bool authorizeAILeiPayment(const AIRequest&r,Room&room,ServerPlayer*p,const SkillInstanceRef&activation,
 const SkillInstanceRef&source,const QList<int>&costs,QList<int>*foreign){
 if(foreign)foreign->clear();const QString skill=activation.key.skillName;
 if(!scope(r,room,p,skill)||activation!=r.skillActionContext.activationRef||source!=r.skillActionContext.sourceRef)return false;
 QSet<int>seen;for(int id:costs){if(id<0||seen.contains(id))return false;seen.insert(id);}
 if(skill=="poxi"){
  const auto*o=observed(r,room,p);if(!o||costs.size()!=4)return false;QSet<Card::Suit>suits;
  for(int id:costs){const auto*c=Sanguosha->getCard(id);if(!c||suits.contains(c->getSuit())||room.getCardPlace(id)!=Player::PlaceHand)return false;suits.insert(c->getSuit());
   if(p->handCards().contains(id)){if(p->isJilei(c))return false;}
   else if(o->ids.contains(id)&&o->target->handCards().contains(id)&&p->canDiscard(o->target,id)){if(foreign)*foreign<<id;}
   else return false;
  }return suits.size()==4;
 }
 if(skill!="hongju"&&skill!="olhongju"&&skill!="mobilehongju")return false;
 int hand=0,pile=0;const auto rong=p->getPile("rong");for(int id:costs){if(p->handCards().contains(id))++hand;else if(rong.contains(id))++pile;else return false;}
 return hand>0&&hand==pile&&costs.size()<=2*rong.size();
}

// ---- poxi-slot ----
namespace {
constexpr const char *slot = "poxi_target";
struct PoxiSlotStack {lua_State *L;int top;~PoxiSlotStack(){lua_settop(L,top);}};
QJsonObject unknown(const char *reason){return{{"known",false},{"reason",QString::fromLatin1(reason)}};}
bool poxiSlotCurrent(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&aiRangeRequestCurrent(r,q,p);
}
bool sourceScope(const AIRequest&q,const ServerPlayer*p){
 const auto exact=[&](const AiSkillActionContext&a){return a.isValid()
  &&a.activationRef==a.sourceRef&&a.getActivationOwner()==p->objectName()
  &&a.getActivationSkillName()=="poxi";};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;
 return false;
}
bool publicPlayer(const AIRequest&q,const ServerPlayer*p){
 if(!p)return false;
 if(q.worldView.self.objectName==p->objectName())return true;
 for(const auto&v:q.worldView.players)if(v.objectName==p->objectName())return true;
 return false;
}
// Implement the exact table-valued __index chain, never call authored getters.
// A missing raw slot is NOT automatically nil: inherited fields are observed.
bool pushField(lua_State*L,int self){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;
  seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);
  AILuaInspect::field(L,row,slot);
  if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);
  if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}
  AILuaInspect::field(L,-1,"__index");
  if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false;
 }
 return false;
}
bool ordinaryAssignment(lua_State*L,int self){
 AILuaInspect::field(L,self,slot);const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(present)return true; // Lua does not consult __newindex for a present raw key.
 if(!lua_getmetatable(L,self))return true;
 AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);
 return absent; // Do not bypass a function OR table-valued assignment handler.
}
struct PoxiSlotWrite {Room*r;const AIRequest*q;ServerPlayer*owner;ServerPlayer*target;const char*failure=nullptr;};
int assignmentError(lua_State*L,PoxiSlotWrite*w,const char*reason){
 // Only our fixed native failure codes cross back to isolated Lua. Never copy
 // arbitrary authored Lua/GC error strings, userdata or private payloads.
 w->failure=reason;return luaL_error(L,"%s",reason);
}
// Only this finite native thunk runs in the game VM; never a legacy strategy.
int poxiSlotAssign(lua_State*L){
 auto*w=static_cast<PoxiSlotWrite*>(lua_touserdata(L,1));
 // luaD_precall creates a fresh C CallInfo with LUA_MINSTACK. The caller's
 // reserve is not this frame's inspect capacity; reserve inside the thunk.
 if(!lua_checkstack(L,2048))return assignmentError(L,w,"Poxi assignment frame capacity unavailable");
 lua_pushstring(L,slot);const int key=lua_absindex(L,-1);
 if(w->target){if(!pushAISlotPlayer(L,w->target))return assignmentError(L,w,"Poxi Player binding unavailable");}
 else lua_pushnil(L);
 const int value=lua_absindex(L,-1);
 // Any allocation/finalizer or protected-entry observer ran BEFORE this exact
 // self/request reauthentication. The setter itself cannot call __newindex.
 if(!poxiSlotCurrent(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi current request changed before assignment");
 if(!pushAIActualAISelf(*w->r,w->owner,L))
  return assignmentError(L,w,"Poxi actual LuaAI receiver authentication failed before assignment");
 const int self=lua_absindex(L,-1);
 if(!ordinaryAssignment(L,self)||!poxiSlotCurrent(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi source assignment metamethod is not certified");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 // Preserve any reached assignment on later error, like the original body.
 if(!poxiSlotCurrent(*w->r,*w->q,w->owner))
  return assignmentError(L,w,"Poxi current request changed after assignment");
 if(!pushAIActualAISelf(*w->r,w->owner,L)||!lua_rawequal(L,self,-1))
  return assignmentError(L,w,"Poxi actual LuaAI receiver authentication failed after assignment");
 AILuaInspect::field(L,self,slot);
 if(w->target?!matchAISlotPlayer(L,-1,w->target):!lua_isnil(L,-1))
  return assignmentError(L,w,"Poxi actual assigned slot changed");
 return 0;
}
}
QJsonObject queryAIPoxiSlot(Room&r,const AIRequest&q,ServerPlayer*p,
 const QString&op,const QString&name){
 if(!poxiSlotCurrent(r,q,p))return unknown("Poxi slot requires current native owner request");
 const bool write=op=="write";
 if(write){
  if(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty()
   ||p->getPhase()!=Player::Play||!sourceScope(q,p)||!poxiSlotCurrent(r,q,p))
   return unknown("Poxi writer requires current own Play source");
 }else if(op!="read"||!name.isEmpty()||q.kind!=AIRequest::UseCard||q.pattern!="@@poxi")
  return unknown("Poxi reader requires exact current response question");
 ServerPlayer*target=nullptr;
 if(write&&!name.isEmpty()){
  target=r.findPlayerByObjectName(name,true);
  if(!target||!publicPlayer(q,target))return unknown("Poxi assigned Player is not current public roster");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);lua_State*L=vm.state();
 if(!L||(!Sanguosha||Sanguosha->currentRoom()!=&r)||!lua_checkstack(L,2048))return unknown("Poxi game VM binding unavailable");
 PoxiSlotStack restore{L,lua_gettop(L)};
 if(write){
  PoxiSlotWrite w{&r,&q,p,target};lua_pushcfunction(L,poxiSlotAssign);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.failure?w.failure:"Poxi protected source assignment failed outside fixed native guards");
  return poxiSlotCurrent(r,q,p)?QJsonObject{{"known",true}}:unknown("Poxi source assignment changed current request");
 }
 if(!poxiSlotCurrent(r,q,p)||!pushAIActualAISelf(r,p,L))return unknown("Poxi actual own LuaAI self unavailable");
 const int self=lua_absindex(L,-1);
 if(!pushField(L,self))return unknown("Poxi source slot requires an unproved dynamic lookup");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)))
  return{{"known",true},{"value",false}};
 // Only exact live roster identities; never derive this value from current
 // prompt target, poxiForAI, notified hand, or saved JSON/name memory.
 for(auto*actual:r.getAllPlayers(true))if(matchAISlotPlayer(L,-1,actual)){
  if(!publicPlayer(q,actual)||!poxiSlotCurrent(r,q,p))return unknown("Poxi actual slot Player is not current public roster");
  // Source next invokes this actual saved userdata's getCards("h"). Matching
  // only its pointer/type cannot justify substituting a native projection if
  // authored Lua replaced the SWIG metatable/class method dispatch.
  if(!matchAINativeObjectMethods(r,L,-1,"ServerPlayer",{"getCards","objectName"}))
   return unknown("Poxi actual Player source method binding changed");
  return{{"known",true},{"value",actual->objectName()}};
 }
 return unknown("Poxi source slot is not nil or an authenticated roster Player");
}

// ---- limu ----
namespace {
bool limuCurrent(const AIRequest&r,const ServerPlayer*p){
 return p&&p->getRoom()&&r.kind==AIRequest::Activate
  &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty()
  &&r.viewerObjectName==p->objectName()&&p->getPhase()==Player::Play
  &&p->getRoom()->getCurrent()==p&&p->hasSkill("limu")
  &&p->getRoom()->roomRuntime()->stateRevision()==r.stateRevision;
}
bool disclosed(const AIRequest&r,int id){
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id)return true;
 for(const auto&v:r.worldView.self.equips)if(v.effectiveId==id)return true;
 for(const auto&pile:r.worldView.self.piles)for(const auto&v:pile.cards)if(v.effectiveId==id)return true;
 return false;
}
}
int aiLimuPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&name){
 return limuCurrent(r,p)&&skill=="limu"&&name=="LimuCard"?1:-1;
}
std::unique_ptr<Card> aiLimuEvaluation(const AIRequest&r,Room&room,ServerPlayer*p,
 const QString&kind,const QList<int>&costs,QString*error){
 auto fail=[&](const char*m)->std::unique_ptr<Card>{if(error)*error=m;return {};};
 if(!limuCurrent(r,p)||p->getRoom()!=&room)return fail("Limu source hypothesis requires actual current owner Play");
 if((kind=="indulgence"&&costs.size()!=1)||(kind=="spear"&&costs.size()!=2)
   ||(kind!="indulgence"&&kind!="spear"))return fail("Limu source hypothesis shape changed");
 QList<int> seen;
 for(int id:costs){
  if(id<0||seen.contains(id)||!disclosed(r,id)||room.getCardOwner(id)!=p)return fail("Limu source material is not distinct disclosed owner material");
  if(!p->handCards().contains(id)&&(kind!="indulgence"||!p->getHandPile().contains(id)))return fail("Limu source material is outside exact source zone");
  seen<<id;
 }
 if(kind=="indulgence"&&Sanguosha->getCard(costs.first())->getSuit()!=Card::Diamond)return fail("Limu lock hypothesis requires source Diamond material");
 if(kind=="spear"&&!p->hasWeapon("spear"))return fail("Limu local Spear hypothesis requires equipped Spear");
 std::unique_ptr<Card> card(Sanguosha->cloneCard(kind=="indulgence"?"indulgence":"slash",Card::NoSuit,0));
 if(!card)return fail("fixed Limu source printed prototype is unavailable");
 if(kind=="indulgence"){card->addSubcards(costs);card->setSkillName("limu");}
 else{card->setSkillName("spear");card->addSubcards(costs);}
 if(error)error->clear();return card;
}

// ---- chuli ----
namespace {
constexpr const char*key="chuli_id_choice";
QJsonObject chuliNo(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool chuliCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool play(const AIRequest&q){
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty())return true;
 const auto c=q.worldView.strategyFacts.value("readonly_play_context").toObject();const auto s=c.value("source").toString();
 if(c.value("known")!=true||c.value("readonly")!=true||c.value("viewer")!=q.viewerObjectName||c.value("decision_id")!=q.getDecisionId()||c.value("state_revision")!=q.getStateRevision()||c.value("kind")!="activate"||c.value("reason")!=int(CardUseStruct::CARD_USE_REASON_PLAY)||c.value("pattern")!=""||c.value("handling_method")!=int(Card::MethodUse))return false;
 if(q.kind==AIRequest::SkillInvoke)return(s=="olClan.zuheta"&&(q.choiceOptions.reason=="zuheta"||q.choiceOptions.reason=="zuheta$-1"))||(s=="OverseasVersion.ov_xiafeng"&&q.choiceOptions.reason=="ov_xiafeng");
 if(q.kind==AIRequest::Choice)return s=="OverseasVersion.ov_shelie"&&q.choiceOptions.reason=="ov_shelie"&&q.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
 return s=="OverseasVersion.ov_niwo"&&q.kind==AIRequest::UseCard&&q.pattern=="@@ov_niwo"&&q.prompt=="ov_niwo0"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool writer(const AIRequest&q){return q.decisionId&&!q.choiceOptions.context.contains("r53_chuli_event")&&(play(q)||(q.kind==AIRequest::UseCard&&q.pattern=="@@heg_yanxi"));}
bool integer(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
bool chuliDisclosed(Room&r,const AIRequest&q,ServerPlayer*t,int id){
 if(r.getCardOwner(id)!=t)return false;const auto place=r.getCardPlace(id);if(place!=Player::PlaceHand&&place!=Player::PlaceEquip)return false;
 const AIPlayerView*v=nullptr;if(q.worldView.self.objectName==t->objectName())v=&q.worldView.self;else for(const auto&x:q.worldView.players)if(x.objectName==t->objectName()){v=&x;break;}if(!v)return false;
 auto has=[&](const QList<AICardView>&cards){for(const auto&c:cards)if(c.effectiveId==id)return true;return false;};
 if(place==Player::PlaceEquip)return has(v->equips);
 return t->objectName()==q.viewerObjectName?has(q.worldView.handCards):has(v->knownCards);
}
bool chuliField(lua_State*L,int self){QSet<const void*>seen;lua_pushvalue(L,self);for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AILuaInspect::field(L,self,key);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct Op{Room*r;const AIRequest*q;ServerPlayer*p;QString op,name;int id=-1;QJsonObject result;const char*error=nullptr;};
int fail(lua_State*L,Op*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int execute(lua_State*L){
 auto*w=static_cast<Op*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"Chuli map frame capacity unavailable");
 int fresh=0;if(w->op=="reset"){lua_newtable(L);fresh=lua_absindex(L,-1);}
 const auto name=w->name.toUtf8();lua_pushlstring(L,name.constData(),size_t(name.size()));int nameKey=lua_absindex(L,-1);
 if(!chuliCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return fail(L,w,"Chuli actual current Self unavailable");int self=lua_absindex(L,-1);
 if(w->op=="reset"){
  if(!assignment(L,self)||!chuliCurrent(*w->r,*w->q,w->p))return fail(L,w,"Chuli map source assignment handler changed");
  lua_pushstring(L,key);lua_pushvalue(L,fresh);lua_settable(L,self);
 }else{
  if(!chuliField(L,self))return fail(L,w,"Chuli actual field lookup changed");int map=lua_absindex(L,-1);
  if(w->op=="state"){
   if(lua_isnil(L,map)||lua_type(L,map)==LUA_TBOOLEAN){w->result={{"known",true},{"type",lua_isnil(L,map)?"nil":"boolean"},{"value",bool(lua_toboolean(L,map))}};}
   else if(lua_istable(L,map)&&!lua_getmetatable(L,map))w->result={{"known",true},{"type","map"}};
   else return fail(L,w,"Chuli current slot is outside its fixed scalar-map domain");
  }else{
   if(lua_isnil(L,map)){w->result={{"known",true},{"type","nil_map"}};}
   else if(!lua_istable(L,map)||lua_getmetatable(L,map))return fail(L,w,"Chuli source map changed type or handlers");
   else if(w->op=="set"){
    if(!chuliCurrent(*w->r,*w->q,w->p))return fail(L,w,"Chuli request changed before reached entry write");
    lua_pushvalue(L,nameKey);lua_pushinteger(L,w->id);lua_settable(L,map);
   }else{
    lua_pushvalue(L,nameKey);lua_rawget(L,map);
    if(lua_isnil(L,-1))w->result={{"known",true},{"type","nil"}};
    else if(lua_type(L,-1)==LUA_TNUMBER){double n=lua_tonumber(L,-1);if(!std::isfinite(n)||n<0||n>2147483647.0||n!=std::floor(n))return fail(L,w,"Chuli saved source entry is not an ID");// An actual own slot is not disclosure authority for a legacy-picked
     // opponent hand ID. TargetSpecified precedes the native discard/reveal.
     // Admit only a same-snapshot disclosed, still-current target HE identity.
     auto*target=w->r->findPlayerByObjectName(w->name,true);
     if(!target||!chuliDisclosed(*w->r,*w->q,target,int(n)))return fail(L,w,"Chuli saved source ID is not currently disclosed target HE");
     w->result={{"known",true},{"type","number"},{"value",n}};}
    else return fail(L,w,"Chuli actual source entry is outside fixed integer shape");
   }
  }
 }
 if(!chuliCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"Chuli current Self changed after source operation");
 if(w->op=="reset"){AILuaInspect::field(L,self,key);if(!lua_rawequal(L,fresh,-1))return fail(L,w,"Chuli reset table replaced during assignment");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject aiChuliEvent(const AIEventView&e){
 if(e.triggerEvent!=int(TargetSpecified)||(e.cardClass!="ChuliCard"&&e.cardClass!="heg_yanxi"&&e.cardName!="heg_yanxi"))return{};
 return{{"sequence",QString::number(e.sequence)},{"targets",QJsonArray::fromStringList(e.targets)}};
}
QJsonObject queryAIChuliMap(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&v){
 if(!chuliCurrent(r,q,p))return chuliNo("Chuli actual source map requires current owner world");
 const bool write=op=="reset"||op=="set";const auto event=q.choiceOptions.context.value("r53_chuli_event").toObject();
 if(write?!writer(q):(event.isEmpty()||(op!="state"&&op!="entry")))return chuliNo("Chuli map operation outside exact source writer/reader");
 QString name;int id=-1;
 if(op=="reset"||op=="state"){if(!v.isEmpty())return chuliNo("Chuli map state operation takes no arguments");}
 else{
  if(v.size()!=(op=="set"?2:1)||!v.value("player").isString())return chuliNo("Chuli map entry shape changed");name=v.value("player").toString();auto*t=r.findPlayerByObjectName(name,true);if(!t)return chuliNo("Chuli map key is outside actual roster");
  if(write){if(!integer(v.value("id"),id)||!chuliDisclosed(r,q,t,id)||(q.pattern=="@@heg_yanxi"&&r.getCardPlace(id)!=Player::PlaceHand))return chuliNo("Chuli selected source ID lacks current disclosed target HE/hand custody");}
  else if(!event.value("targets").toArray().contains(name))return chuliNo("Chuli reader key outside actual public event targets");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return chuliNo("Chuli gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 Op w{&r,&q,p,op,name,id};lua_pushcfunction(L,execute);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return chuliNo(w.error?w.error:"Chuli protected source map operation failed");return w.result;
}

// ---- daoji ----
#include "wrapped-card.h"
// Must follow Qt headers: the private Lua implementation defines cast macros.
namespace {
QJsonObject daojiUnknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool daojiCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool daojiScope(const AIRequest&q,ServerPlayer*p,const QString&op){
 if(op=="write")return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill("daoji");
 return(op=="truth"||op=="contains"||op=="read")&&q.kind==AIRequest::CardChosen&&q.choiceOptions.reason=="daoji"&&q.choiceOptions.question=="askForCardChosen";
}
bool daojiField(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool daojiAssignment(lua_State*L,int self,const char*key){AILuaInspect::field(L,self,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
ServerPlayer*roster(Room&r,const AIRequest&q,const QString&name){
 bool projected=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)projected=projected||v.objectName==name;
 if(!projected)return nullptr;for(auto*p:r.getAllPlayers(true))if(p->objectName()==name)return p;return nullptr;
}
ServerPlayer*actualPlayer(Room&r,lua_State*L,int at){for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,at,p))return p;return nullptr;}

bool publicEquip(const AIRequest&q,ServerPlayer*owner,const Card*c){
 if(!owner||!c)return false;auto views=q.worldView.players;views.prepend(q.worldView.self);
 for(const auto&v:views)if(v.objectName==owner->objectName())for(const auto&e:v.equips)
  if(e.effectiveId==c->getEffectiveId()&&e.nativeIdentity&&globalCardLifetimeManager().isLive(e.nativeIdentity)&&e.nativeIdentity->address==c)return true;
 return false;
}
const Card*sourceWrapped(Room&r,const AIRequest&q,int id){
 auto*holder=r.getCardOwner(id);if(!holder||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 // Only bind the source Sanguosha:getCard identity. No wrapped face/change-chain
 // is read or exported from this observation-only slot operation.
 for(const auto*c:holder->getEquips())if(c->getEffectiveId()==id&&publicEquip(q,holder,c)){
  const auto*w=Sanguosha->getCard(id);return w&&w->getRealCard()==c?w:nullptr;
 }return nullptr;
}
struct Call{Room*r;const AIRequest*q;ServerPlayer*p;QString op;const Card*card=nullptr;ServerPlayer*who=nullptr;QList<const Card*>equips;QJsonObject result;const char*error=nullptr;};
int daojiFail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return daojiFail(L,w,"Daoji source stack unavailable");
 int fresh=0;if(w->op=="write"){if(w->card){if(!pushAISlotCard(L,w->card))return daojiFail(L,w,"Daoji exact Card wrapper unavailable");}else lua_pushnil(L);fresh=lua_absindex(L,-1);}
 if(!daojiCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return daojiFail(L,w,"Daoji actual Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="write"){
  if(!daojiAssignment(L,self,"daoji_throwcard")||!daojiCurrent(*w->r,*w->q,w->p))return daojiFail(L,w,"Daoji source assignment changed");
  if(w->card&&sourceWrapped(*w->r,*w->q,w->card->getEffectiveId())!=w->card)return daojiFail(L,w,"Daoji public source object changed before assignment");
  lua_pushvalue(L,fresh);lua_setfield(L,self,"daoji_throwcard");
 }else{
  if(!daojiField(L,self,"daoji_throwcard"))return daojiFail(L,w,"Daoji actual getter changed");const int value=lua_absindex(L,-1);
  if(w->op=="truth")w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
  else{
   if(w->who->getCards("e")!=w->equips)return daojiFail(L,w,"Daoji actual public equipment list changed");
   for(auto*c:w->equips)if(!publicEquip(*w->q,w->who,c))return daojiFail(L,w,"Daoji current list not disclosed");
   int found=-1;for(int i=0;i<w->equips.size();++i)if(matchAISlotCard(L,value,w->equips.at(i))){found=i;break;}
   if(found<0){
    // Authenticate fixed source-produced wrapped Cards as well. A WrappedCard
    // and its real EquipCard have different pointer identities, even at one ID.
    bool fixed=lua_isnil(L,value);
    for(int id:Sanguosha->getDeckCardIds(false)){
     const auto*outer=Sanguosha->getCard(id);if(outer&&matchAISlotCard(L,value,outer))fixed=true;
    }
    if(!fixed)return daojiFail(L,w,"Daoji retained Card outside current physical equipment domain");
   }
   if(w->op=="contains")w->result={{"known",true},{"type","boolean"},{"value",found>=0}};
   else{if(found<0)return daojiFail(L,w,"Daoji source read no longer equals current equipment");w->result={{"known",true},{"index",found+1}};}
  }
 }
 if(!daojiCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return daojiFail(L,w,"Daoji receiver changed after operation");
 if(w->op=="write"){AILuaInspect::field(L,self,"daoji_throwcard");if(!lua_rawequal(L,fresh,-1))return daojiFail(L,w,"Daoji source assignment did not persist");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject queryAIDaojiCard(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&input){
 if(!daojiCurrent(r,q,p)||!daojiScope(q,p,op))return daojiUnknown("Daoji card slot requires exact current own source inquiry");Call w{&r,&q,p,op};
 if(op=="write"){
  if(input.size()!=1)return daojiUnknown("Daoji write shape invalid");const auto id=input.value("id");
  if(input.value("clear")!=QJsonValue(true)){
   if(!input.contains("id"))return daojiUnknown("Daoji write key invalid");
   const double d=id.toDouble(-1);if(!id.isDouble()||d<0||d>2147483647||d!=std::floor(d))return daojiUnknown("Daoji source ID malformed");w.card=sourceWrapped(r,q,int(d));if(!w.card)return daojiUnknown("Daoji source ID is not current publicly projected equipment");
  }
 }else if(op=="truth"){if(!input.isEmpty())return daojiUnknown("Daoji truth has extra arguments");}
 else{
  if(input.size()!=2||!input.value("who").isString()||!input.value("ids").isArray())return daojiUnknown("Daoji equipment observation shape invalid");w.who=roster(r,q,input.value("who").toString());if(!w.who||q.choiceOptions.playerNames!=QStringList{w.who->objectName()})return daojiUnknown("Daoji list holder differs from actual question");
  w.equips=w.who->getCards("e");const auto ids=input.value("ids").toArray();if(ids.size()!=w.equips.size())return daojiUnknown("Daoji observed equipment order/count changed");
  for(int i=0;i<ids.size();++i)if(!ids.at(i).isDouble()||ids.at(i).toDouble()!=w.equips.at(i)->getEffectiveId()||!publicEquip(q,w.who,w.equips.at(i)))return daojiUnknown("Daoji observed equipment object/order changed");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return daojiUnknown("Daoji game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return daojiUnknown(w.error?w.error:"protected Daoji source operation failed");return w.result;
}

// ---- manjuan-2 ----
namespace {
QJsonObject manjuan2No(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool manjuan2Current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&aiRangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool index(const QJsonValue&v,int&n){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;n=int(d);return true;}
bool publicId(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 if(r.getCardPlace(id)!=Player::DiscardPile)return false;
 const Card*live=Sanguosha->getCard(id);if(!live)return false;
 const QString token=aiLegacyCardIdentity(r,p,live);
 for(const auto&v:q.worldView.discardPile)if(v.effectiveId==id&&!v.virtualCard&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==token)return true;
 return false;
}
}
QJsonObject queryAIManjuanPrinted(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&in){
 if(!manjuan2Current(r,q,p)||!p->hasSkill("kezhuanmanjuan",true)||!manjuan2Current(r,q,p))return manjuan2No("Manjuan printed prefix needs actual current owner Play source");
 if(op=="printed_count"){if(!in.isEmpty())return manjuan2No("Manjuan printed count takes no arguments");return{{"known",true},{"value",Sanguosha->getCardCount()}};}
 int id;if(!index(in.value("id"),id)||id>=Sanguosha->getCardCount())return manjuan2No("Manjuan printed source index outside actual catalogue");
 // getEngineCard itself is reference acquisition. Do not read or project a face
 // before the source own-mark branch, and do not replace it by the live wrapper.
 const Card*c=Sanguosha->getEngineCard(id);
 if(op=="printed_step"){
  if(in.size()!=1)return manjuan2No("Manjuan printed reference shape changed");
  return{{"known",true},{"exists",c!=nullptr},{"identity",c?aiLegacyCardIdentity(r,p,c):QString()}};
 }
 const bool available=op=="printed_available";
 if((op!="printed_number"&&!available)||in.size()!=(available?3:2)||!in.value("identity").isString())return manjuan2No("Manjuan printed getter is outside finite source");
 if(available&&in.value("player")!=p->objectName())return manjuan2No("Manjuan printed availability cannot query foreign private rules");
 if(!publicId(r,q,p,id)||!c||in.value("identity")!=aiLegacyCardIdentity(r,p,c))return manjuan2No("Manjuan marked printed input lacks current public discard identity");
 QJsonValue v;
 if(available){auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);if(!vm.state())return manjuan2No("Manjuan printed availability binding unavailable");v=c->isAvailable(p);}
 else v=c->getNumber();
 if(!manjuan2Current(r,q,p)||!publicId(r,q,p,id)||Sanguosha->getEngineCard(id)!=c)return manjuan2No("Manjuan printed source changed during query");
 return{{"known",true},{"value",v}};
}

// R53: the fixed source returns a raw LuaSkillCard wire, whose inferred skill
// is kezhuanmanjuanvs. It is not the GUI ViewAsSkillV2 kezhuanmanjuan product.
namespace {
bool manjuanResponseCurrent(const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()&&p->getRoom()->getAllPlayers(true).contains(p)
  &&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()
  &&q.decisionId&&q.kind==AIRequest::RespondCard
  &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE&&q.handlingMethod==Card::MethodResponse
  &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom()
  &&aiRangeRequestCurrent(*p->getRoom(),q,p);
}
bool manjuanActualIndex(const AIRequest&q,ServerPlayer*p,int&id){
 auto&r=*p->getRoom();auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,4096))return false;int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(!pushAIActualAISelf(r,p,L))return false;
 // Fixed writer always creates this own raw field. No metatable evaluation or
 // inherited/global guess grants a selection; missing is a refusal.
 AILuaInspect::field(L,-1,"kezhuanmanjuan_id");if(lua_type(L,-1)!=LUA_TNUMBER)return false;
 double n=lua_tonumber(L,-1);if(!std::isfinite(n)||n<0||n>2147483647.0||n!=std::floor(n))return false;
 id=int(n);return manjuanResponseCurrent(q,p);
}
bool manjuanSelection(const AIRequest&q,ServerPlayer*p,const QString&declaration,int&id){
 if(!manjuanResponseCurrent(q,p)||!p->isKongcheng()||!p->hasSkill("kezhuanmanjuan",true)
  ||!manjuanResponseCurrent(q,p)||!manjuanActualIndex(q,p,id))return false;
 auto&r=*p->getRoom();if(r.getCardPlace(id)!=Player::DiscardPile)return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;bool seen=false;
 for(const auto&v:q.worldView.discardPile)if(v.effectiveId==id&&v.nativeIdentity
  &&globalCardLifetimeManager().isLive(v.nativeIdentity)&&v.nativeIdentity->address==c
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())
  &&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()
  &&v.virtualCard==c->isVirtualCard()){seen=true;break;}
 if(!seen||declaration!=c->objectName()||declaration.isEmpty()
  ||p->getMark(QString::number(id)+"manjuanPile-Clear")<1
  ||p->getMark(QString::number(c->getNumber())+"manjuanNumber-Clear")>0)return false;
 // The ordinary cardsview selected this disclosed card. The original native
 // validator, not this receipt, still performs live locked/pattern/AG checks.
 return manjuanResponseCurrent(q,p);
}
bool rawManjuan(const Card*c,const QString&declaration,ServerPlayer*p){
 const auto*lua=dynamic_cast<const LuaSkillCard*>(c);
 if(!lua||c->objectName()!="kezhuanmanjuanVsCard"||c->getClassName()!="LuaSkillCard"
  ||c->rawSkillName()!="kezhuanmanjuanvs"||c->getActivationSkillName()!="kezhuanmanjuanvs"
  ||c->getSkillInstanceId()!=0||c->getActivationSkillInstanceId()!=0||c->getSourceSkillInstanceId()!=0
  ||!c->getSubcards().isEmpty()||lua->getUserString()!=declaration
  ||Sanguosha->getViewAsSkill("kezhuanmanjuanvs"))return false;
 // Preserve the original resolver's ordinary no-provider early return; do not
 // enter private attached-instance resolution under an inferred alias.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="kezhuanmanjuanvs")return false;
 return true;
}
bool sameManjuanCallback(const LuaFunction&a,const LuaFunction&b){
 if(a.isValid()!=b.isValid())return false;
 if(a.isValid()&&(!a.state()||!b.state()))return false;
 return a.state()==b.state()&&a.generation()==b.generation()&&a.registryRef()==b.registryRef();
}
bool sameRawManjuan(const Card*a,const Card*b){
 const auto*x=dynamic_cast<const LuaSkillCard*>(a);const auto*y=dynamic_cast<const LuaSkillCard*>(b);if(!x||!y)return false;
 return a->toString()==b->toString()&&a->getSuit()==b->getSuit()&&a->getNumber()==b->getNumber()
  &&a->getFlags()==b->getFlags()&&a->getHandlingMethod()==b->getHandlingMethod()
  &&a->willThrow()==b->willThrow()&&a->canRecast()==b->canRecast()&&a->targetFixed()==b->targetFixed()
  &&sameManjuanCallback(x->filter,y->filter)&&sameManjuanCallback(x->feasible,y->feasible)&&sameManjuanCallback(x->about_to_use,y->about_to_use)
  &&sameManjuanCallback(x->on_use,y->on_use)&&sameManjuanCallback(x->on_effect,y->on_effect)&&sameManjuanCallback(x->on_validate,y->on_validate)
  &&sameManjuanCallback(x->on_validate_in_response,y->on_validate_in_response);
}
}
const Card*constructAIManjuanRaw(const AIRequest&q,ServerPlayer*p,const QString&declaration,int*selected){
 int id;if(!manjuanSelection(q,p,declaration,id))return nullptr;
 const Card*c=Card::Parse("#kezhuanmanjuanVsCard:.:"+declaration);
 if(!rawManjuan(c,declaration,p)||!manjuanResponseCurrent(q,p))return nullptr;
 if(selected)*selected=id;return c;
}
bool hasAIManjuanRaw(const AIRequest&q,int id){
 if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.manjuanRawSource)return true;return false;
}
bool applyAIManjuanRaw(const AIRequest&q,ServerPlayer*p,const AICardSpec&s,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!manjuanResponseCurrent(q,p)||!q.conversionLedger||!targets.isEmpty()||!admit)return false;
 for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==s.conversionId&&t.manjuanRawSource){
  if(s.name!=t.name||s.skillName!=t.activationRef.key.skillName||s.suit!=t.suit||s.number!=t.number||s.subcardIds!=t.subcardIds
   ||!s.subcardIds.isEmpty()||t.activationRef.ownerObjectName!=p->objectName()
   ||t.activationRef.key.skillName!="kezhuanmanjuan"||!p->hasSkillInstance(t.activationRef.key.skillName,t.activationRef.key.instanceID))return false;
  int id;if(!manjuanSelection(q,p,t.declaredName,id)||id!=t.manjuanRawSelectedId)return false;
  const auto lease=q.conversionLedger->observedProducts.value(t.conversionId);const Card*original=lease?lease():nullptr;
  if(!original||!rawManjuan(original,t.declaredName,p))return false;
  const Card*parsed=Card::Parse("#kezhuanmanjuanVsCard:.:"+t.declaredName);
  if(!rawManjuan(parsed,t.declaredName,p)||!sameRawManjuan(original,parsed)||!manjuanResponseCurrent(q,p))return false;
  CardUseStruct actual(parsed,p);
  if(!admit(actual)||actual.card!=parsed||actual.activationRef.isValid()||actual.sourceRef.isValid()
   ||actual.hasSkillActivationRequest||!manjuanResponseCurrent(q,p))return false;
  int after;if(!manjuanSelection(q,p,t.declaredName,after)||after!=id)return false;
  actual.m_validateTargets=true;use=actual;return true;
 }
 return false;
}

// ---- manjuan ----
namespace {
constexpr const char*manjuanSlot="kezhuanmanjuan_id";
QJsonObject manjuanUnknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool manjuanCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool producer(const AIRequest&q){return q.kind==AIRequest::Activate||q.kind==AIRequest::RespondCard||q.kind==AIRequest::UseCard;}
bool reader(const AIRequest&q){return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="kezhuanmanjuan"&&q.choiceOptions.question=="askForAG";}
bool manjuanInteger(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
bool manjuanField(lua_State*L,int self){QSet<const void*>seen;lua_pushvalue(L,self);for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AILuaInspect::field(L,row,manjuanSlot);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool manjuanAssignment(lua_State*L,int self){AILuaInspect::field(L,self,manjuanSlot);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct ManjuanWrite{Room*r;const AIRequest*q;ServerPlayer*p;int id;const char*error=nullptr;};
int manjuanFail(lua_State*L,ManjuanWrite*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int manjuanAssign(lua_State*L){
 auto*w=static_cast<ManjuanWrite*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return manjuanFail(L,w,"Manjuan assignment frame capacity unavailable");
 lua_pushstring(L,manjuanSlot);int key=lua_absindex(L,-1);lua_pushinteger(L,w->id);int value=lua_absindex(L,-1);
 if(!manjuanCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L))return manjuanFail(L,w,"Manjuan current actual Self unavailable before write");int self=lua_absindex(L,-1);
 if(!manjuanAssignment(L,self)||!manjuanCurrent(*w->r,*w->q,w->p))return manjuanFail(L,w,"Manjuan source assignment handler changed");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 if(!manjuanCurrent(*w->r,*w->q,w->p)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return manjuanFail(L,w,"Manjuan current Self changed after source prefix");
 AILuaInspect::field(L,self,manjuanSlot);if(!lua_rawequal(L,value,-1))return manjuanFail(L,w,"Manjuan assigned source slot changed");return 0;
}
bool same(Room&r,ServerPlayer*p,const AICardView&v,const Card*c,int id){
 if(!c||v.effectiveId!=id||c->getEffectiveId()!=id||v.legacyIdentityToken.isEmpty()||v.legacyIdentityToken!=aiLegacyCardIdentity(r,p,c))return false;
 if(v.nativeIdentity&&(!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=c))return false;
 return v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard();
}
const Card*manjuanDisclosed(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 // Classify lawful location BEFORE reading/comparing any current face. A hidden
 // foreign card is always unknown, even if its old printed/known face matches.
 const auto place=r.getCardPlace(id);const bool own=r.getCardOwner(id)==p;
 if(place!=Player::DiscardPile&&!own)return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(place==Player::DiscardPile){for(const auto&v:q.worldView.discardPile)if(same(r,p,v,c,id))return c;return nullptr;}
 if(place==Player::PlaceHand){for(const auto&v:q.worldView.handCards)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceEquip){for(const auto&v:q.worldView.self.equips)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceJudge){for(const auto&v:q.worldView.self.judgingArea)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceSpecial){for(const auto&pile:q.worldView.self.piles)if(p->getPile(pile.name).contains(id))for(const auto&v:pile.cards)if(same(r,p,v,c,id))return c;}
 return nullptr;
}
}
QJsonObject queryAIManjuan(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&input){
 if(!manjuanCurrent(r,q,p))return manjuanUnknown("Manjuan requires current native owner request");
 if(op=="read"){
  if(!reader(q)||!input.isEmpty())return manjuanUnknown("Manjuan reader requires exact current AG question");
  auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return manjuanUnknown("Manjuan gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
  if(!manjuanCurrent(r,q,p)||!pushAIActualAISelf(r,p,L)||!manjuanField(L,lua_absindex(L,-1)))return manjuanUnknown("Manjuan actual source field lookup unavailable");
  QJsonObject out{{"known",true}};switch(lua_type(L,-1)){case LUA_TNIL:out.insert("type","nil");break;case LUA_TBOOLEAN:out.insert("type","boolean");out.insert("value",bool(lua_toboolean(L,-1)));break;case LUA_TNUMBER:{double n=lua_tonumber(L,-1);if(!std::isfinite(n))return manjuanUnknown("Manjuan nonfinite actual scalar unsupported");out.insert("type","number");out.insert("value",n);break;}case LUA_TSTRING:{size_t n;const char*s=lua_tolstring(L,-1,&n);if(n>256)return manjuanUnknown("Manjuan non-source string exceeds scalar domain");out.insert("type","string");out.insert("value",QString::fromUtf8(s,int(n)));break;}default:return manjuanUnknown("Manjuan actual slot is not a scalar");}
  if(!manjuanCurrent(r,q,p))return manjuanUnknown("Manjuan request changed during read");return out;
 }
 if(!producer(q))return manjuanUnknown("Manjuan cardsview source requires an active use/response request");
 // Fixed collector cardsView enumerates owned skill instances. include_lose=true
 // checks ownership only, not unrelated native activation/invalidity rules.
 // The body enters its catalogue loop only while its actual owner has no hand.
 if(!p->hasSkill("kezhuanmanjuan",true)||!p->isKongcheng()||!manjuanCurrent(r,q,p))return manjuanUnknown("Manjuan source requires its actual skill owner and empty hand");
 if(op=="count"){if(!input.isEmpty())return manjuanUnknown("Manjuan count takes no arguments");return{{"known",true},{"value",Sanguosha->getCardCount()}};}
 int id;if(!manjuanInteger(input.value("id"),id)||id>=Sanguosha->getCardCount())return manjuanUnknown("Manjuan index outside actual source catalogue");
 if(op=="step"){
  if(input.size()!=1)return manjuanUnknown("Manjuan step argument shape changed");
  // Exact source order: getCard is an opaque reference acquisition; no getter
  // or public-face demand precedes the unconditional actual Self assignment.
  const Card*c=Sanguosha->getCard(id);const auto identity=c?aiLegacyCardIdentity(r,p,c):QString();
  auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return manjuanUnknown("Manjuan gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
  ManjuanWrite w{&r,&q,p,id};lua_pushcfunction(L,manjuanAssign);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return manjuanUnknown(w.error?w.error:"Manjuan protected source assignment failed");
  if(!manjuanCurrent(r,q,p))return manjuanUnknown("Manjuan source prefix changed current request");
  return{{"known",true},{"exists",c!=nullptr},{"identity",identity}};
 }
 const bool kind=op=="kind",locked=op=="locked";
 if((op!="number"&&op!="name"&&!kind&&!locked)||input.size()!=(kind||locked?3:2)||!input.value("identity").isString())return manjuanUnknown("Manjuan finite card query shape unsupported");
 if(kind&&(!input.value("class").isString()||input.value("class").toString().isEmpty()))return manjuanUnknown("Manjuan source class missing");
 if(locked&&input.value("player")!=p->objectName())return manjuanUnknown("Manjuan foreign private locked rule is unavailable");
 const Card*c=manjuanDisclosed(r,q,p,id);if(!c||input.value("identity")!=aiLegacyCardIdentity(r,p,c))return manjuanUnknown("Manjuan current live face is not lawfully disclosed with same identity");
 QJsonValue value;
 if(op=="number")value=c->getNumber();else if(op=="name")value=c->objectName();else if(kind)value=c->isKindOf(input.value("class").toString().toUtf8().constData());
 else{auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);if(!vm.state())return manjuanUnknown("Manjuan owner rule gameplay binding unavailable");value=p->isLocked(c);}
 if(!manjuanCurrent(r,q,p)||manjuanDisclosed(r,q,p,id)!=c)return manjuanUnknown("Manjuan card/current request changed during source query");
 return{{"known",true},{"value",value}};
}

// ---- guandu-shicai ----
namespace {
class ShicaiLedger final : public QObject {
public:
    struct Row {
        int id = -1;
        bool pending = true;
        quint64 serial = 0;
        QPointer<const Card> card;
        QJsonObject face;
        QMetaObject::Connection changed;
    };
    explicit ShicaiLedger(Room &room) : QObject(&room) {}
    QHash<QString, Row> rows;
    quint64 serial = 0;
    void erase(const QString &name) {
        auto it=rows.find(name);if(it==rows.end())return;
        QObject::disconnect(it->changed);rows.erase(it);
    }
};
ShicaiLedger *guanduShicaiLedger(Room &room, bool create) {
    const QString name=QStringLiteral("_isolated_round29_shicai_preview");
    auto *object=room.findChild<QObject *>(name,Qt::FindDirectChildrenOnly);
    if(object)return dynamic_cast<ShicaiLedger *>(object);
    if(!create)return nullptr;
    auto *result=new ShicaiLedger(room);result->setObjectName(name);return result;
}
QJsonObject guanduShicaiFace(const Card *c) {
    return {{"id",c->getEffectiveId()},{"effective_id",c->getEffectiveId()},
        {"name",c->objectName()},{"class_name",c->getClassName()},
        {"type_id",int(c->getTypeId())},{"suit",int(c->getSuit())},
        {"suit_string",c->getSuitString()},{"number",c->getNumber()},
        {"kind_of",QJsonArray::fromStringList(c->getKindOfNames())},
        {"red",c->isRed()},{"black",c->isBlack()},{"name_length",c->nameLength()},
        {"flags_complete",false},{"skill_names_complete",false}};
}
QJsonObject read(Room &room, ServerPlayer *viewer, const std::function<bool()> &skillQuery = {}) {
    const QJsonObject unknown{{"known",false}};
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()
        ||viewer->getPhase()!=Player::Play||!(skillQuery ? skillQuery() : viewer->hasSkill("guandu_shicai")))return unknown;
    auto *l=guanduShicaiLedger(room,false);if(!l)return unknown;
    const auto it=l->rows.constFind(viewer->objectName());
    if(it==l->rows.cend()||it->pending||!it->card)return unknown;
    const auto row=*it;
    // HeavenMove sets the owner tag after sending both notification halves.
    // An arbitrary tag cannot mint knowledge: a matching live witness is required.
    bool ok=false;const int tagged=viewer->getTag("HeavenMove").toInt(&ok);
    if(!ok||tagged!=row.id||room.getDrawPile().isEmpty()
        ||room.getDrawPile().first()!=row.id||room.getCardPlace(row.id)!=Player::DrawPile
        ||room.getCardOwner(row.id)!=nullptr)return unknown;
    const Card *current=room.getCard(row.id);
    if(current!=row.card.data()||current->isVirtualCard()||guanduShicaiFace(current)!=row.face) {
        l->erase(viewer->objectName());return unknown;
    }
    return {{"known",true},{"serial",QString::number(row.serial)},{"card",row.face}};
}
}

int aiGuanduTwyjPrototype(const AIRequest &r,const ServerPlayer *p,
                           const QString &skill,const QString &prototype) {
    if(!p||!p->getRoom()||p->getRoom()->roomRuntime()->isClosing()
        ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        ||!p->hasSkill(skill))return -1;
    struct Row {const char *skill;const char *prototype;int capabilities;};
    static const Row rows[]={
        {"gd_zezhu","#gd_zezhu",0},
        {"guandu_shicai","#guandu_shicai",1},
        {"guandu_yuanlue","#guandu_yuanlue",1},
        {"twyj_huzhu","#twyj_huzhu",0},
        {"twyj_qijia","#twyj_qijia",1},
        {"twyj_rangyi","#twyj_rangyi",0},
        {"twyj_zhuchen","#twyj_zhuchen",1}};
    for(const auto &row:rows)
        if(skill==QLatin1String(row.skill)&&prototype==QLatin1String(row.prototype))return row.capabilities;
    return -1;
}

void recordAIShicaiPreview(Room &room,ServerPlayer *recipient,
                                 const QList<CardsMoveStruct> &moves,bool isLostPhase) {
    if(!recipient||recipient->getRoom()!=&room||room.roomRuntime()->isClosing())return;
    for(const auto &m:moves) {
        if(m.reason.m_skillName!="guandu_shicai"
            ||m.reason.m_reason!=CardMoveReason::S_REASON_PUT
            ||m.reason.m_playerId!=recipient->objectName())continue;
        const QString name=recipient->objectName();
        if(m.from==recipient&&m.to==nullptr&&m.from_place==Player::PlaceSpecial
            &&m.to_place==Player::PlaceTable&&m.from_pile_name=="&talent") {
            if(auto *l=guanduShicaiLedger(room,false))l->erase(name);
            continue;
        }
        if(m.from!=nullptr||m.to!=recipient||m.from_place!=Player::PlaceTable
            ||m.to_place!=Player::PlaceSpecial||m.to_pile_name!="&talent")continue;
        auto *l=guanduShicaiLedger(room,true);
        if(!l)continue;
        if(!m.open||m.card_ids.size()!=1||m.card_ids.first()<0
            ||recipient->getPhase()!=Player::Play||!recipient->hasSkill("guandu_shicai")) {
            l->erase(name);continue;
        }
        const int id=m.card_ids.first();
        if(room.getDrawPile().isEmpty()||room.getDrawPile().first()!=id
            ||room.getCardPlace(id)!=Player::DrawPile||room.getCardOwner(id)!=nullptr) {
            l->erase(name);continue;
        }
        if(isLostPhase) {
            l->erase(name);ShicaiLedger::Row row;row.id=id;l->rows.insert(name,row);continue;
        }
        auto it=l->rows.find(name);
        if(it==l->rows.end()||!it->pending||it->id!=id) {l->erase(name);continue;}
        const Card *card=room.getCard(id);
        if(!card||card->isVirtualCard()||card->getEffectiveId()!=id) {l->erase(name);continue;}
        it->card=card;it->face=guanduShicaiFace(card);it->pending=false;it->serial=++l->serial;
        if(const auto *wrapped=qobject_cast<const WrappedCard *>(card))
            it->changed=QObject::connect(wrapped,&WrappedCard::identityChanged,l,
                [&room,id](){invalidateAIShicaiPreview(room,id);},Qt::DirectConnection);
    }
}
void invalidateAIShicaiPreview(Room &room,int id) {
    if(auto *l=guanduShicaiLedger(room,false)) {
        const auto keys=l->rows.keys();
        for(const auto &name:keys)if(l->rows.value(name).id==id)l->erase(name);
    }
}
void clearAIShicaiPreview(Room &room) {
    if(auto *l=guanduShicaiLedger(room,false)) {
        const auto keys=l->rows.keys();for(const auto &name:keys)l->erase(name);
    }
}
void projectAIShicaiPreviewWorld(AIWorldView &world,Room &room,ServerPlayer *viewer) {
    if(!viewer||world.self.objectName!=viewer->objectName()||viewer->getRoom()!=&room)return;
    world.strategyFacts.insert("extra29",QJsonObject{{"shicai_preview",read(room,viewer,[&] {
        std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("guandu_shicai");
    })}});
}
const Card *aiShicaiPreview(const AIRequest &r,Room &room,ServerPlayer *viewer) {
    if(!viewer||viewer->getRoom()!=&room||r.viewerObjectName!=viewer->objectName()
        ||r.worldView.self.objectName!=viewer->objectName()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        ||r.stateRevision!=room.roomRuntime()->stateRevision())return nullptr;
    const auto snapshot=r.worldView.strategyFacts.value("extra29").toObject().value("shicai_preview").toObject();
    const auto current=read(room,viewer);
    if(!snapshot.value("known").toBool()||snapshot!=current)return nullptr;
    return room.getCard(current.value("card").toObject().value("effective_id").toInt(-1));
}

// ---- assassins-happy2v2-yongjian ----
int aiAssassinsHappy2v2ReadonlyPrototype(const QString &skill, const QString &prototype)
{
    // b6025 ai/happy2v2-ai.lua:5,18 and ai/assassins-ai.lua:269,292-323.
    // Native metaobjects: happy2v2.cpp:522; assassins.cpp:1127.
    // Bit 1 permits an ordered material wire, never a user-string declaration.
    if ((skill == QLatin1String("kuiji") && prototype == QLatin1String("KuijiCard"))
        || (skill == QLatin1String("mixin") && prototype == QLatin1String("MixinCard"))) return 1;
    // Empty source constructors only: assassins-ai.lua:95 and :372. Native
    // V2 factories select no materials (assassins.cpp:356-371,799-811).
    if ((skill == QLatin1String("mizhao") && prototype == QLatin1String("MizhaoCard"))
        || (skill == QLatin1String("duyi") && prototype == QLatin1String("DuyiCard"))) return 0;
    return -1;
}

bool aiFenyueEvaluation(const AIRequest &request, const QString &skill,
                              const QString &name, const QJsonArray &costs)
{
    // ai/yongjian-ai.lua:620-637, dummyCard() default NoSuit (6), number 0;
    // extensions/yongjian.lua:1719 supplies this exact public choice. Source
    // sets raw attribution _zd_fenyue and never selects payment material here.
    return skill == QLatin1String("_zd_fenyue") && name == QLatin1String("slash")
        && costs.isEmpty() && request.kind == AIRequest::Choice
        && request.choiceOptions.reason == QLatin1String("zd_fenyue")
        && request.choiceOptions.choices.contains(QStringLiteral("zd_fenyue2"));
}
