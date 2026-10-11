#include "ai-strategy-round44-focus.h"
#include "ai-strategy-round25-helpers.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>

namespace {
const QString damageMark = QStringLiteral("FocusDamageCount");
const QString recoverMark = QStringLiteral("FocusRecoverCount");
bool currentViewer(const AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() == &room
        && world.self.objectName == viewer->objectName()
        && room.getAllPlayers(true).contains(viewer);
}
QJsonObject counters(const AIPlayerView &view)
{
    // Only existing observer-visible mark evidence, including owner-complete
    // absence. A known source writer does not authorize private restored values.
    QJsonObject row;
    for (const QString &name : {damageMark, recoverMark}) {
        if (view.publicMarks.contains(name)) row.insert(name, view.publicMarks.value(name));
        else if (view.marksComplete) row.insert(name, 0);
    }
    return row;
}
}
void projectAIRound25ImagineWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    world.strategyFacts.remove(QStringLiteral("imagine_focus_event"));
    if (!currentViewer(world, room, viewer)) return;
    QJsonObject all;
    const auto add = [&](const AIPlayerView &view) {
        const auto row = counters(view);
        if (!row.isEmpty()) all.insert(view.objectName, row);
    };
    add(world.self);
    for (const AIPlayerView &view : world.players) add(view);
    world.strategyFacts.insert(QStringLiteral("imagine"),
                               QJsonObject{{QStringLiteral("focus_marks"), all},
        {"native_callback_bridge", hasAIRound44FocusCallbacks(room)}});
}
void projectAIRound25ImagineEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
                                 const AIEventView &event)
{
    world.strategyFacts.remove(QStringLiteral("imagine_focus_event"));
    if (!currentViewer(world, room, viewer)
        || event.revision != room.roomRuntime()->stateRevision() || event.amount < 0)
        return;
    QString name;
    if (event.triggerEvent == Damaged && event.kind == QStringLiteral("damage")) name = damageMark;
    else if (event.triggerEvent == HpRecover && event.kind == QStringLiteral("recover")) name = recoverMark;
    else return;
    const QString actor = event.details.value(QStringLiteral("player")).toString();
    ServerPlayer *player = room.findPlayerByObjectName(actor, true);
    if (!player || (event.privateEvent && event.privateViewer != viewer->objectName())) return;
    const auto baseline = world.strategyFacts.value(QStringLiteral("imagine")).toObject()
        .value(QStringLiteral("focus_marks")).toObject().value(actor).toObject().value(name);
    if (!baseline.isDouble()) return;
    world.strategyFacts.insert(QStringLiteral("imagine_focus_event"), QJsonObject{
        {QStringLiteral("player"), actor}, {QStringLiteral("name"), name},
        {QStringLiteral("amount"), event.amount},
        {QStringLiteral("baseline"), baseline}});
}
