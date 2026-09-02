#include "engine-bootstrap.h"
#include "engine.h"
#include "card-lifetime-manager.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "protocol/protocol-runtime.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"

#include <QDebug>

#include <cstdio>

using namespace QSanProtocol;

namespace {

class SettingsOverrideGuard
{
public:
    SettingsOverrideGuard(const QString &key, const QVariant &value)
        : m_previous(Config.valueOverrides())
    {
        QVariantMap overrides = m_previous;
        overrides.insert(key, value);
        Config.setValueOverrides(overrides);
    }

    ~SettingsOverrideGuard()
    {
        Config.setValueOverrides(m_previous);
    }

private:
    QVariantMap m_previous;
};

class OrderedV2Probe : public TriggerSkillV2
{
public:
    OrderedV2Probe(const QString &name, TriggerEvent event, int priority,
                   QStringList *order)
        : TriggerSkillV2(name), m_priority(priority), m_order(order)
    {
        events << event;
    }

    int getPriority(TriggerEvent) const override
    {
        return m_priority;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        if (m_order)
            m_order->append(objectName());
        return {};
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        return false;
    }

private:
    int m_priority;
    QStringList *m_order;
};

class LegacyPriorityProbe : public TriggerSkill
{
public:
    LegacyPriorityProbe(const QString &name, TriggerEvent event, int priority)
        : TriggerSkill(name), m_priority(priority)
    {
        events << event;
    }

    int getPriority(TriggerEvent) const override
    {
        return m_priority;
    }

    bool triggerable(ServerPlayer *, Room *, TriggerEvent, ServerPlayer *, QVariant) const override
    {
        return false;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        return false;
    }

private:
    int m_priority;
};

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

static bool v2PartitionPreservesOrderAndPrivatePriorityState()
{
    SettingsOverrideGuard profiling(QStringLiteral("RoomThreadPerfTrace"), true);
    QStringList v2Order;
    OrderedV2Probe low(QStringLiteral("test-roomthread-v2-low"), DrawNCards, 1, &v2Order);
    OrderedV2Probe highFirst(QStringLiteral("test-roomthread-v2-high-first"),
                             DrawNCards, 3, &v2Order);
    OrderedV2Probe highSecond(QStringLiteral("test-roomthread-v2-high-second"),
                              DrawNCards, 3, &v2Order);
    LegacyPriorityProbe legacy(QStringLiteral("test-roomthread-legacy"), ChoiceMade, 2);
    Room room(nullptr, QStringLiteral("02_1v1"));
    RoomTestAccess::attachThread(room);
    room.getThread()->addTriggerSkill(&low);
    room.getThread()->addTriggerSkill(&highFirst);
    room.getThread()->addTriggerSkill(&highSecond);
    room.getThread()->addTriggerSkill(&legacy);

    // Shared Skill definitions must remain untouched by per-Room ordering.
    low.setDynamicPriority(-101.0);
    highFirst.setDynamicPriority(-202.0);
    highSecond.setDynamicPriority(-303.0);

    QVariant data;
    room.getThread()->trigger(ChoiceMade, &room, nullptr, data);
    const QVariantMap afterLegacy = room.getThread()->triggerDispatchProfile();
    if (afterLegacy.value(QStringLiteral("v2_candidate_count")).toLongLong() != 0
        || afterLegacy.value(QStringLiteral("v2_empty_dispatch_count")).toLongLong() != 1) {
        std::fprintf(stderr, "legacy-only event visited V2 candidates\n");
        return false;
    }

    room.getThread()->trigger(DrawNCards, &room, nullptr, data);
    const QVariantMap profile = room.getThread()->triggerDispatchProfile();
    const QStringList expectedOrder{
        QStringLiteral("test-roomthread-v2-high-first"),
        QStringLiteral("test-roomthread-v2-high-second"),
        QStringLiteral("test-roomthread-v2-low")
    };
    const bool passed = v2Order == expectedOrder
        && low.getDynamicPriority() == -101.0
        && highFirst.getDynamicPriority() == -202.0
        && highSecond.getDynamicPriority() == -303.0
        && profile.value(QStringLiteral("trigger_count")).toLongLong() == 2
        && profile.value(QStringLiteral("priority_rebuild_count")).toLongLong() == 1
        && profile.value(QStringLiteral("priority_skill_count")).toLongLong() == 3
        && profile.value(QStringLiteral("priority_sort_count")).toLongLong() == 2
        && profile.value(QStringLiteral("v2_dispatch_count")).toLongLong() == 2
        && profile.value(QStringLiteral("v2_empty_dispatch_count")).toLongLong() == 1
        && profile.value(QStringLiteral("v2_candidate_count")).toLongLong() == 3
        && profile.value(QStringLiteral("main_table_candidate_visit_count")).toLongLong() == 4;
    if (!passed) {
        std::fprintf(stderr,
            "RoomThread perf profile/order mismatch: order=%d trigger=%lld rebuild=%lld "
            "skills=%lld sorts=%lld v2_dispatch=%lld v2_empty=%lld v2_candidates=%lld "
            "main_table_candidates=%lld\n",
            int(v2Order.length()),
            profile.value(QStringLiteral("trigger_count")).toLongLong(),
            profile.value(QStringLiteral("priority_rebuild_count")).toLongLong(),
            profile.value(QStringLiteral("priority_skill_count")).toLongLong(),
            profile.value(QStringLiteral("priority_sort_count")).toLongLong(),
            profile.value(QStringLiteral("v2_dispatch_count")).toLongLong(),
            profile.value(QStringLiteral("v2_empty_dispatch_count")).toLongLong(),
            profile.value(QStringLiteral("v2_candidate_count")).toLongLong(),
            profile.value(QStringLiteral("main_table_candidate_visit_count")).toLongLong());
    }
    return passed;
}

static bool cardLifetimeMutexProfileCountsLocks()
{
    CardLifetimeManager disabled(CardLifetimeMode::ObserveOnly, nullptr, false);
    disabled.enterScope();
    disabled.leaveScope();
    const CardLifetimeMutexProfile disabledProfile = disabled.mutexProfile();
    if (disabledProfile.enabled || disabledProfile.lock_count != 0)
        return false;

    CardLifetimeManager enabled(CardLifetimeMode::ObserveOnly, nullptr, true);
    enabled.enterScope();
    enabled.leaveScope();
    const CardLifetimeMutexProfile profile = enabled.mutexProfile();
    return profile.enabled && profile.lock_count == 2
        && profile.contended_count <= profile.lock_count
        && profile.wait_ns >= profile.max_wait_ns;
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

int runRoomThreadPerfTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!v2PartitionPreservesOrderAndPrivatePriorityState()) {
        qCritical() << "RoomThread V2 partition/private priority regression failed";
        return 2;
    }
    if (!cardLifetimeMutexProfileCountsLocks()) {
        qCritical() << "CardLifetime mutex profile regression failed";
        return 3;
    }
    qInfo() << "ROOMTHREAD_PERF_TEST_RESULT status=PASS";
    return 0;
}
