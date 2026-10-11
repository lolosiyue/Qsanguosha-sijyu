#include "ai-strategy-response-observer.h"
#include "ai.h"
#include "ai-strategy-card-identity.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "server-info.h"
#include "skill.h"
#include "wrapped-card.h"
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QSet>

namespace {
QSet<QString> recipients(Room &room, const QList<ServerPlayer *> &specified = {})
{
    QSet<QString> result;
    for (const auto *p : specified.isEmpty() ? room.getAllPlayers(true) : specified)
        if (p && p->getRoom() == &room) result.insert(p->objectName());
    return result;
}
struct Frame {
    QString skill;
    QPointer<ServerPlayer> actor;
    int event = NonTrigger;
    QPointer<const Card> card;
    QStringList targets;
    QSet<QString> audience;
    QSet<QString> damageWindow;
    QSet<QString> completeAudience;
    QHash<QString, QSet<QString>> clearedTargets;
    bool eligible = false;
};
struct CardWitness {
    QString targets;
    bool damageFlag = true;
    QSet<QString> audience;
};
class Ledger final : public QObject {
public:
    explicit Ledger(Room &room) : QObject(&room) {}
    QList<Frame *> frames;
    QHash<const Card *, CardWitness> cards;
    QSet<const Card *> watched;
    QHash<QString, QHash<QString, bool>> shejian;
    QSet<QString> initialized;
};
Ledger *ledger(Room &room, bool create)
{
    const QString key = QStringLiteral("isolated-ai-response-observer");
    auto *found = room.findChild<QObject *>(key, Qt::FindDirectChildrenOnly);
    if (!found && create) { found = new Ledger(room); found->setObjectName(key); }
    return static_cast<Ledger *>(found);
}
void watch(Ledger *l, const Card *card)
{
    if (l->watched.contains(card)) return;
    l->watched.insert(card);
    QObject::connect(card, &QObject::destroyed, l, [l, card]() {
        l->cards.remove(card); l->watched.remove(card);
    }, Qt::DirectConnection);
    if (const auto *wrapped = qobject_cast<const WrappedCard *>(card))
        QObject::connect(wrapped, &WrappedCard::identityChanged, l,
            [l, card]() { l->cards.remove(card); }, Qt::DirectConnection);
}
bool matches(const Card *card, const CardWitness &w)
{
    // Validate only an already witnessed value. A mismatch reveals no new value.
    return card && card->hasFlag("dwqingshidamage") == w.damageFlag
        && card->getTag("dwqingshitarget").toString() == w.targets;
}
}
struct AIResponseObservationScope::Data {
    Room &room;
    Ledger *store;
    Frame frame;
    bool committed = false;
    int effectCallDepth;
    quint64 failureGeneration;
    explicit Data(Room &r) : room(r), store(ledger(r, true)),
        effectCallDepth(r.roomRuntime()->lua().protectedCallDepth() + 1),
        failureGeneration(r.roomRuntime()->lua().protectedCallFailureGenerationAtDepth(effectCallDepth)) {}
};
AIResponseObservationScope::AIResponseObservationScope(Room &room, const QString &skill,
    ServerPlayer *actor, int originalEvent, const SkillContext &context)
    : d(new Data(room))
{
    auto &f = d->frame;
    f.skill = skill; f.actor = actor; f.event = originalEvent;
    // Push even unrecognized effects: nested callbacks cannot donate mark
    // notifications to an enclosing audited effect.
    d->store->frames.append(&f);
    if (!actor || actor->getRoom() != &room || !context.original_data) return;
    f.audience = recipients(room);
    const QString publicSkill = skill == "#dwqingshibuff" ? QStringLiteral("dwqingshi") : skill;
    if (ServerInfo.EnableHegemony && !actor->hasShownSkill(publicSkill))
        f.audience = QSet<QString>{actor->objectName()};
    f.completeAudience = f.audience;
    if (skill == "dwqingshi" && originalEvent == CardUsed
        && context.original_data->canConvert<CardUseStruct>()) {
        const auto use = context.original_data->value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard") || use.from != actor) return;
        f.card = use.card;
        for (const auto *target : use.to) {
            if (!target || target->getRoom() != &room) return;
            f.targets.append(target->objectName());
        }
        f.eligible = true;
    } else if (skill == "#dwqingshibuff" && originalEvent == CardFinished
        && context.original_data->canConvert<CardUseStruct>()) {
        f.card = context.original_data->value<CardUseStruct>().card;
        f.eligible = f.card != nullptr;
    } else if (skill == "sk_shejian") {
        if (originalEvent == EventPhaseChanging && context.original_data->canConvert<PhaseChangeStruct>())
            f.eligible = context.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
        else if (originalEvent == Death && context.original_data->canConvert<DeathStruct>())
            f.eligible = context.original_data->value<DeathStruct>().who == actor;
    }
}
AIResponseObservationScope::~AIResponseObservationScope()
{
    d->store->frames.removeAll(&d->frame);
}
void AIResponseObservationScope::commit()
{
    if (d->committed) return;
    d->committed = true;
    // These audited Lua on_effect wrappers enter exactly one direct pcall at
    // P+1. Their own swallowed error invalidates completion. A deeper helper
    // error handled by the successful outer callback does not invalidate its
    // witnessed public operations. This is true pcall depth, not lifetime-pin
    // invocationDepth (which also includes manually entered scopes).
    if (d->room.roomRuntime()->lua().protectedCallFailureGenerationAtDepth(d->effectCallDepth)
        != d->failureGeneration) return;
    const auto &f = d->frame;
    if (!f.eligible) return;
    if (f.skill == "sk_shejian") {
        // Only the completed audited cleanup body, never a mere phase event,
        // proves this private clear. Do not consult/serialize other flags.
        for (const auto *p : d->room.getPlayers()) {
            if (p->hasFlag("shejian_used")) continue;
            for (const QString &observer : f.audience)
                d->store->shejian[observer].insert(p->objectName(), false);
        }
        return;
    }
    const Card *card = f.card.data();
    if (!card) return;
    if (f.skill == "#dwqingshibuff") {
        auto it = d->store->cards.find(card);
        if (it == d->store->cards.end()) return;
        auto updated = it.value(); updated.targets.clear();
        updated.audience.intersect(f.audience);
        if (!matches(card, updated)) { d->store->cards.erase(it); return; }
        it.value() = updated;
        return;
    }
    // The source records names in the original use.to order, not target-mark
    // notification order. Repeated targets are consumed by the first clear.
    QHash<QString, QSet<QString>> groups;
    for (const QString &observer : f.damageWindow) {
        if (!f.completeAudience.contains(observer)) continue;
        QStringList names; QSet<QString> used;
        const auto cleared = f.clearedTargets.value(observer);
        for (const QString &target : f.targets)
            if (cleared.contains(target) && !used.contains(target)) {
                names.append(target); used.insert(target);
            }
        if (!names.isEmpty()) groups[names.join('+')].insert(observer);
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        CardWitness candidate;
        candidate.targets = it.key(); candidate.audience = it.value();
        if (!matches(card, candidate)) continue;
        watch(d->store, card);
        d->store->cards.insert(card, candidate);
        break; // Only one full predicted value can match the same actual card.
    }
}
void recordAIResponseObservedMark(Room &room, ServerPlayer *target, const QString &mark,
    int value, const QList<ServerPlayer *> &audience)
{
    if (!target || target->getRoom() != &room || mark != "dwqingshitarget" || value != 0) return;
    auto *l = ledger(room, false);
    if (!l || l->frames.isEmpty()) return;
    auto *f = l->frames.last();
    if (!f->eligible || f->skill != "dwqingshi" || !f->targets.contains(target->objectName())) return;
    const auto actual = recipients(room, audience);
    f->completeAudience.intersect(actual);
    for (const QString &observer : f->damageWindow)
        if (actual.contains(observer)) f->clearedTargets[observer].insert(target->objectName());
}
void recordAIResponseObservedPlayerFlag(Room &room, ServerPlayer *target,
    const QString &operation, const QList<ServerPlayer *> &audience)
{
    if (!target || target->getRoom() != &room) return;
    if (operation == "shejian_used" || operation == "-shejian_used" || operation == ".") {
        const bool value = operation == "shejian_used";
        if (target->hasFlag("shejian_used") != value) return;
        auto *l = ledger(room, true);
        for (const QString &observer : recipients(room, audience))
            l->shejian[observer].insert(target->objectName(), value);
    }
    if (operation != "dwqingshidamage") return;
    auto *l = ledger(room, false);
    if (!l || l->frames.isEmpty()) return;
    auto *f = l->frames.last();
    if (!f->eligible || f->skill != "dwqingshi" || f->actor != target) return;
    auto actual = recipients(room, audience); actual.intersect(f->audience);
    f->damageWindow.unite(actual);
}
void initializeAIResponseObservations(Room &room, ServerPlayer *observer, bool freshGame)
{
    if (!freshGame || room.isRestoringTakeoverSnapshot() || !observer || observer->getRoom() != &room) return;
    auto *l = ledger(room, true);
    if (l->initialized.contains(observer->objectName())) return;
    l->initialized.insert(observer->objectName());
    // Fresh game construction is the witness for the literal's default absence.
    // A changed value before this observer's GameReady is not retroactively read.
    for (const auto *p : room.getAllPlayers(true))
        if (!p->hasFlag("shejian_used") && !l->shejian[observer->objectName()].contains(p->objectName()))
            l->shejian[observer->objectName()].insert(p->objectName(), false);
}
QJsonObject readAIResponseCardObservation(Room &room, ServerPlayer *observer, const Card *card)
{
    if (!observer || observer->getRoom() != &room || !card) return {};
    auto *l = ledger(room, false); if (!l) return {};
    auto it = l->cards.find(card);
    if (it == l->cards.end() || !it.value().audience.contains(observer->objectName())) return {};
    if (!matches(card, it.value())) { l->cards.erase(it); return {}; }
    return {{"legacy_identity_token", aiLegacyCardIdentity(room, observer, card)}, {"dwqingshidamage", it.value().damageFlag}};
}
void projectAIResponseObservations(AIWorldView &world, Room &room, ServerPlayer *observer)
{
    if (!observer || observer->getRoom() != &room || world.self.objectName != observer->objectName()) return;
    auto *l = ledger(room, false); if (!l) return;
    QJsonObject flags;
    for (const auto *target : room.getAllPlayers(true)) {
        auto &values = l->shejian[observer->objectName()];
        const auto value = values.find(target->objectName());
        if (value == values.end()) continue;
        if (target->hasFlag("shejian_used") != value.value()) {
            values.erase(value); // A later private reversal cannot revive it.
            continue;
        }
        flags.insert(target->objectName(), value.value());
    }
    auto sk = world.strategyFacts.value("sk").toObject();
    auto publicFlags = sk.value("public_flags").toObject();
    publicFlags.insert("shejian_used", flags); sk.insert("public_flags", publicFlags);
    world.strategyFacts.insert("sk", sk);
    QJsonObject cards;
    for (auto it = l->cards.begin(); it != l->cards.end();) {
        if (!matches(it.key(), it.value())) { it = l->cards.erase(it); continue; }
        if (it.value().audience.contains(observer->objectName())) {
            QJsonArray ordered;
            if (!it.value().targets.isEmpty())
                for (const auto &name : it.value().targets.split('+')) ordered.append(name);
            cards.insert(aiLegacyCardIdentity(room, observer, it.key()), QJsonObject{{"known", true}, {"dwqingshitarget", it.value().targets},
                {"ordered_targets", ordered}, {"dwqingshidamage", it.value().damageFlag}});
        }
        ++it;
    }
    auto ny = world.strategyFacts.value("NyarzFirst").toObject();
    ny.insert("card_observations", cards); world.strategyFacts.insert("NyarzFirst", ny);
}
