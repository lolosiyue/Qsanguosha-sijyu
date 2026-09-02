#include "engine-bootstrap.h"
#include "engine.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "protocol/protocol-runtime.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"

#include <QDebug>

#include <cstdio>

using namespace QSanProtocol;

namespace {

class BreakAfterDirtyV2Skill : public TriggerSkillV2
{
public:
    BreakAfterDirtyV2Skill()
        : TriggerSkillV2(QStringLiteral("test-roomthread-dirty-v2"))
    {
        events << HpChanged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player,
                            QVariant &) const override
    {
        TriggerList result;
        if (event == HpChanged && player && player->hasSkill(objectName()))
            result[player] << objectName();
        return result;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        // RoomThread dispatches this fixture through the TriggerSkillV2 path.
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
    {
        return true;
    }
};

class PacketRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QByteArray &frame) {
            ProtocolMessage message;
            if (!m_router.decode(frame, &message).success) {
                m_parseFailed = true;
                return;
            }
            m_messages << message;
        });
    }

    void clear()
    {
        m_messages.clear();
        m_parseFailed = false;
    }

    int playerUiStateCount(const QString &playerName) const
    {
        int count = 0;
        foreach (const ProtocolMessage &message, m_messages) {
            if (message.command == S_COMMAND_UPDATE_PLAYER_UI_STATE
                && message.payload.toMap().value(QStringLiteral("player_name")).toString()
                    == playerName)
                ++count;
        }
        return count;
    }

    int distancePropertyCount() const
    {
        int count = 0;
        foreach (const ProtocolMessage &message, m_messages) {
            const QVariantMap payload = message.payload.toMap();
            if (message.command == S_COMMAND_SET_PROPERTY
                && payload.value(QStringLiteral("property_name")).toString()
                    .startsWith(QStringLiteral("distanceTo_")))
                ++count;
        }
        return count;
    }

    QList<int> distancePropertyValues(const QString &fromName,
                                      const QString &toName) const
    {
        QList<int> values;
        const QString propertyName = QStringLiteral("distanceTo_") + toName;
        foreach (const ProtocolMessage &message, m_messages) {
            const QVariantMap payload = message.payload.toMap();
            if (message.command == S_COMMAND_SET_PROPERTY
                && payload.value(QStringLiteral("player_name")).toString() == fromName
                && payload.value(QStringLiteral("property_name")).toString() == propertyName) {
                values << payload.value(QStringLiteral("string_value")).toInt();
            }
        }
        return values;
    }

    bool parseFailed() const { return m_parseFailed; }

private:
    ProtocolCodecRouter m_router;
    QList<ProtocolMessage> m_messages;
    bool m_parseFailed = false;
};

static bool v2BrokenStillFlushesDeferredClientState()
{
    BreakAfterDirtyV2Skill skill;
    Sanguosha->addSkills(QList<const Skill *>() << &skill);

    Room room(nullptr, QStringLiteral("02_1v1"));
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(
        room, QStringLiteral("dirty-owner"));
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(
        room, QStringLiteral("dirty-other"));
    RoomTestAccess::resetAlive(room);
    owner->setGeneralName(QStringLiteral("caocao"));
    other->setGeneralName(QStringLiteral("liubei"));
    owner->setMaxHp(4);
    owner->setHp(4);
    other->setMaxHp(4);
    other->setHp(4);

    PacketRecorder recorder;
    recorder.watch(owner);
    if (room.acquireSkill(owner, &skill, false, false, false) <= 0)
        return false;
    recorder.clear();

    owner->setHp(3);
    QVariant data = -1;
    const bool broken = room.getThread()->trigger(HpChanged, &room, owner, data);
    const int firstUiStateCount = recorder.playerUiStateCount(owner->objectName());
    const int firstDistanceCount = recorder.distancePropertyCount();
    if (!broken || recorder.parseFailed()
        || firstUiStateCount != 1 || firstDistanceCount == 0) {
        std::fprintf(stderr,
            "first flush: broken=%d parse_failed=%d ui_state=%d distance=%d\n",
            broken, recorder.parseFailed(), firstUiStateCount, firstDistanceCount);
        return false;
    }

    // A second dirty event without state changes must not resend cached values.
    recorder.clear();
    data = -1;
    const bool brokenAgain = room.getThread()->trigger(HpChanged, &room, owner, data);
    const int secondUiStateCount = recorder.playerUiStateCount(owner->objectName());
    const int secondDistanceCount = recorder.distancePropertyCount();
    const bool passed = brokenAgain && !recorder.parseFailed()
        && secondUiStateCount == 0 && secondDistanceCount == 0;
    if (!passed) {
        std::fprintf(stderr,
            "second flush: broken=%d parse_failed=%d ui_state=%d distance=%d\n",
            brokenAgain, recorder.parseFailed(), secondUiStateCount, secondDistanceCount);
    }
    return passed;
}

static bool fixedDistanceChangesInvalidateDistanceSync()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(
        room, QStringLiteral("fixed-owner"));
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(
        room, QStringLiteral("fixed-other"));
    owner->setSeat(1);
    other->setSeat(2);
    RoomTestAccess::resetAlive(room);

    PacketRecorder recorder;
    recorder.watch(owner);

    room.setFixedDistance(owner, other, 3);
    QVariant data;
    room.getThread()->trigger(NonTrigger, &room, nullptr, data);
    const QList<int> fixedValues = recorder.distancePropertyValues(
        owner->objectName(), other->objectName());
    if (recorder.parseFailed() || fixedValues != QList<int>{3}) {
        std::fprintf(stderr,
            "set fixed distance: parse_failed=%d values=%d first=%d\n",
            recorder.parseFailed(), int(fixedValues.length()),
            fixedValues.isEmpty() ? -1 : fixedValues.first());
        return false;
    }

    recorder.clear();
    room.removeFixedDistance(owner, other, 3);
    room.getThread()->trigger(NonTrigger, &room, nullptr, data);
    const QList<int> restoredValues = recorder.distancePropertyValues(
        owner->objectName(), other->objectName());
    if (recorder.parseFailed() || restoredValues != QList<int>{1}) {
        std::fprintf(stderr,
            "remove fixed distance: parse_failed=%d values=%d first=%d\n",
            recorder.parseFailed(), int(restoredValues.length()),
            restoredValues.isEmpty() ? -1 : restoredValues.first());
        return false;
    }

    // A neutral event must not resend the unchanged synchronization matrix.
    recorder.clear();
    room.getThread()->trigger(NonTrigger, &room, nullptr, data);
    const int unchangedCount = recorder.distancePropertyCount();
    if (recorder.parseFailed() || unchangedCount != 0) {
        std::fprintf(stderr,
            "unchanged fixed distance: parse_failed=%d distance=%d\n",
            recorder.parseFailed(), unchangedCount);
        return false;
    }
    return true;
}

}

int runRoomThreadDeferredStateTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!fixedDistanceChangesInvalidateDistanceSync()) {
        qCritical() << "RoomThread fixed-distance invalidation regression failed";
        return 2;
    }
    if (!v2BrokenStillFlushesDeferredClientState()) {
        qCritical() << "RoomThread deferred state regression failed";
        return 3;
    }
    qInfo() << "RoomThread deferred state regression passed";
    return 0;
}
