#include "ai-strategy-history-context.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"
#include "runtime-paths.h"
#include <QDir>
#include <QHash>
#include <QObject>

namespace {
class HistoryKnowledge final : public AIRoomLedger {
public:
    explicit HistoryKnowledge(Room &room) : AIRoomLedger(room) { setObjectName("isolated-strategy-history-knowledge"); }
    QHash<QString, QSet<QString>> skills;
};
HistoryKnowledge &knowledge(Room &room) {
    for (QObject *child : room.children())
        if (auto *found = dynamic_cast<HistoryKnowledge *>(child)) return *found;
    return *new HistoryKnowledge(room);
}
QString profile(const AIWorldView &world)
{
    // Seat names alone never imply continuity. An administrator must explicitly
    // map this Room's seat to a stable profile before cross-Room writes exist.
    return Config.value("AiHistoryProfiles").toMap().value(world.self.objectName).toString();
}
QString root()
{
    return QSanRuntimePaths::userDataPath(QStringLiteral("lua/ai/data/isolated"));
}
QSet<QString> skills(const AIWorldView &world, Room &room)
{
    QSet<QString> &result = knowledge(room).skills[world.self.objectName];
    const auto add = [&](const AIPlayerView &player) {
        for (const AISkillView &skill : player.skills)
            if (Sanguosha->getSkill(skill.skillName)) result.insert(skill.skillName);
    };
    add(world.self);
    for (const auto &player : world.players) add(player);
    return result;
}
QSet<QString> cardNames()
{
    QSet<QString> result;
    for (const QString &name : Sanguosha->getCardNames()) {
        result.insert(name);
        const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        if (card) result.insert(card->getClassName());
    }
    return result;
}
}
void observeAIStrategyHistory(AIWorldView &world, Room &room, const AIEventView &event)
{
    auto &known = knowledge(room).skills[world.self.objectName];
    for (const QString &name : {event.reason, event.cardSkill,
        event.details.value("move").toObject().value("skill").toString()})
        if (!name.isEmpty() && Sanguosha->getSkill(name)) known.insert(name);
    projectAIStrategyHistory(world, room, room.findPlayerByObjectName(world.self.objectName, true));
}

void projectAIStrategyHistory(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    world.strategyFacts.remove("ai_data");
    world.strategyFacts.remove("ai_data_revision");
    world.strategyFacts.remove("ai_data_scope");
    QJsonArray allowed;
    for (const QString &name : skills(world, room)) allowed.append(name);
    world.strategyFacts.insert("ai_data_allowed_skills", allowed);
    world.strategyFacts.insert("ai_data_known", true);
    world.strategyFacts.insert("ai_data_persistent", false);
    if (!viewer || viewer->getRoom() != &room || viewer->objectName() != world.self.objectName) return;
    const QString identity = profile(world);
    if (identity.isEmpty()) return;
    for (const ServerPlayer *other : room.getAllPlayers(true)) {
        if (other != viewer && Config.value("AiHistoryProfiles").toMap().value(other->objectName()).toString() == identity) {
            world.strategyFacts.insert("ai_data_known", false);
            world.strategyFacts.insert("ai_data_error", "AI_HISTORY_PROFILE_COLLISION");
            return;
        }
    }
    if (!QDir().mkpath(root())) {
        world.strategyFacts.insert("ai_data_known", false);
        world.strategyFacts.insert("ai_data_error", "AI_HISTORY_ROOT_UNAVAILABLE");
        return;
    }
    AIStrategyHistoryStore store(root(), identity);
    const auto snapshot = store.read(skills(world, room), cardNames());
    world.strategyFacts.insert("ai_data_known", snapshot.known);
    if (!snapshot.known) {
        world.strategyFacts.insert("ai_data_error", snapshot.error);
        return;
    }
    world.strategyFacts.insert("ai_data_scope", store.scopeId());
    world.strategyFacts.insert("ai_data_persistent", true);
    world.strategyFacts.insert("ai_data_revision", snapshot.revision);
    world.strategyFacts.insert("ai_data", snapshot.data);
}
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyHistory(
    const AIWorldView &world, Room &room, const QString &revision, const QJsonObject &data, QString *error)
{
    ServerPlayer *viewer = room.findPlayerByObjectName(world.self.objectName, true);
    const QString identity = profile(world);
    if (!viewer || viewer->getRoom() != &room || identity.isEmpty()
        || !world.strategyFacts.value("ai_data_persistent").toBool()
        || world.strategyFacts.value("ai_data_revision").toString() != revision) {
        if (error) *error = "AI_HISTORY_UNAUTHORIZED";
        return nullptr;
    }
    AIStrategyHistoryStore store(root(), identity);
    if (world.strategyFacts.value("ai_data_scope").toString() != store.scopeId()) {
        if (error) *error = "AI_HISTORY_SCOPE_CHANGED";
        return nullptr;
    }
    for (const ServerPlayer *other : room.getAllPlayers(true)) {
        if (other != viewer && Config.value("AiHistoryProfiles").toMap().value(other->objectName()).toString() == identity) {
            if (error) *error = "AI_HISTORY_PROFILE_COLLISION";
            return nullptr;
        }
    }
    return store.prepare(revision, data, skills(world, room), cardNames(), error);
}
