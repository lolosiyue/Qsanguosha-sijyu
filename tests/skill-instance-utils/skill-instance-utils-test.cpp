#include "skill-instance-utils.h"
#include "skill-execution-registry.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
    bool expectParsed(const QString &input, const QString &expectedName, int expectedId)
    {
        QString actualName;
        int actualId = SkillInstanceUtils::parseName(input, actualName);
        if (actualName == expectedName && actualId == expectedId)
            return true;

        QTextStream(stderr) << "parseName failed: input=" << input
                            << ", expected=(" << expectedName << ", " << expectedId << ")"
                            << ", actual=(" << actualName << ", " << actualId << ")\n";
        return false;
    }

    bool expectEqual(const QString &label, const QString &actual, const QString &expected)
    {
        if (actual == expected)
            return true;

        QTextStream(stderr) << label << " failed: expected=" << expected
                            << ", actual=" << actual << "\n";
        return false;
    }

    bool expectEqual(const QString &label, bool actual, bool expected)
    {
        if (actual == expected)
            return true;

        QTextStream(stderr) << label << " failed: expected=" << expected
                            << ", actual=" << actual << "\n";
        return false;
    }

    bool expectEqual(const QString &label, int actual, int expected)
    {
        if (actual == expected)
            return true;

        QTextStream(stderr) << label << " failed: expected=" << expected
                            << ", actual=" << actual << "\n";
        return false;
    }

    bool expectRef(const QString &label, const SkillInstanceRef &actual, const SkillInstanceRef &expected)
    {
        if (actual == expected)
            return true;

        QTextStream(stderr) << label << " failed: expected=" << expected.ownerObjectName
                            << "/" << expected.key.skillName << "#" << expected.key.instanceID
                            << ", actual=" << actual.ownerObjectName << "/" << actual.key.skillName
                            << "#" << actual.key.instanceID << "\n";
        return false;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    ok = expectParsed("skill#1", "skill", 1) && ok;
    ok = expectParsed("#hiddenSkill", "#hiddenSkill", 0) && ok;
    ok = expectParsed("#hiddenSkill#2", "#hiddenSkill", 2) && ok;
    ok = expectParsed("skill", "skill", 0) && ok;

    ok = expectEqual("formatName(skill, 1)", SkillInstanceUtils::formatName("skill", 1), "skill#1") && ok;
    ok = expectEqual("formatName(#hiddenSkill, 2)", SkillInstanceUtils::formatName("#hiddenSkill", 2), "#hiddenSkill#2") && ok;
    ok = expectEqual("baseName(skill#1)", SkillInstanceUtils::baseName("skill#1"), "skill") && ok;
    ok = expectEqual("baseName(#hiddenSkill#2)", SkillInstanceUtils::baseName("#hiddenSkill#2"), "#hiddenSkill") && ok;
    ok = expectEqual("hasInstanceId(skill#1)", SkillInstanceUtils::hasInstanceId("skill#1"), true) && ok;
    ok = expectEqual("hasInstanceId(skill)", SkillInstanceUtils::hasInstanceId("skill"), false) && ok;
    ok = expectEqual("hasInstanceId(#hiddenSkill#2)", SkillInstanceUtils::hasInstanceId("#hiddenSkill#2"), true) && ok;
    ok = expectEqual("hasInstanceId(#hiddenSkill)", SkillInstanceUtils::hasInstanceId("#hiddenSkill"), false) && ok;

    const SkillInstanceRef activationA("owner_a", SkillInstanceKey("skill", 1));
    const SkillInstanceRef activationB("owner_b", SkillInstanceKey("skill", 1));
    const SkillInstanceRef sourceA("root_a", SkillInstanceKey("root_skill", 7));
    const SkillInstanceRef sourceB("root_b", SkillInstanceKey("root_skill", 7));
    ok = expectRef("activation reference", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_ActivationInstance, activationA, sourceA), activationA) && ok;
    ok = expectRef("source reference", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_SourceInstance, activationA, sourceA), sourceA) && ok;
    ok = expectRef("source reference shared across activation owners", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_SourceInstance, activationB, sourceA), sourceA) && ok;
    ok = expectRef("source owner isolation", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_SourceInstance, activationB, sourceB), sourceB) && ok;
    ok = expectRef("legacy activation fallback", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_ActivationInstance, SkillInstanceRef(), SkillInstanceRef(),
                       "legacy_owner", "legacy_skill", 9),
                       SkillInstanceRef("legacy_owner", SkillInstanceKey("legacy_skill", 9))) && ok;
    ok = expectEqual("source missing fails closed", SkillInstanceUtils::resolveUsageRef(
                       SkillInstanceUtils::UsageRef_SourceInstance, activationA, SkillInstanceRef()).isValid(), false) && ok;
    ok = expectEqual("unknown identity fails closed", SkillInstanceUtils::resolveUsageRef(
                       static_cast<SkillInstanceUtils::UsageRefKind>(99), activationB, sourceA).isValid(), false) && ok;

    const QString sourceMarkA = SkillInstanceUtils::formatUsageMarkKey(
        sourceA.key.skillName, sourceA.key.instanceID, "-Clear");
    const QString sourceKeyA = SkillInstanceUtils::formatUsageReservationKey(
        sourceA.ownerObjectName, sourceMarkA);
    const SkillInstanceRef sourceFromOtherActivation = SkillInstanceUtils::resolveUsageRef(
        SkillInstanceUtils::UsageRef_SourceInstance, activationB, sourceA);
    const QString sourceKeyAFromOtherActivation = SkillInstanceUtils::formatUsageReservationKey(
        sourceFromOtherActivation.ownerObjectName, SkillInstanceUtils::formatUsageMarkKey(
            sourceFromOtherActivation.key.skillName, sourceFromOtherActivation.key.instanceID, "-Clear"));
    const QString sourceKeyB = SkillInstanceUtils::formatUsageReservationKey(
        sourceB.ownerObjectName, SkillInstanceUtils::formatUsageMarkKey(
            sourceB.key.skillName, sourceB.key.instanceID, "-Clear"));
    const QString activationKeyA = SkillInstanceUtils::formatUsageReservationKey(
        activationA.ownerObjectName, SkillInstanceUtils::formatUsageMarkKey(
            activationA.key.skillName, activationA.key.instanceID, "-Clear"));
    const QString activationKeyB = SkillInstanceUtils::formatUsageReservationKey(
        activationB.ownerObjectName, SkillInstanceUtils::formatUsageMarkKey(
            activationB.key.skillName, activationB.key.instanceID, "-Clear"));
    ok = expectEqual("attached source identity shares one quota key",
                     sourceKeyAFromOtherActivation, sourceKeyA) && ok;
    ok = expectEqual("same source name/id with another root owner is isolated",
                     sourceKeyA == sourceKeyB, false) && ok;
    ok = expectEqual("activation identity remains owner-local",
                     activationKeyA == activationKeyB, false) && ok;
    ok = expectEqual("legacy reset mark keeps instance zero",
                     SkillInstanceUtils::formatUsageMarkKey("legacy_skill", 0, "_game"),
                     "Usage_legacy_skill_0_game") && ok;

    SkillInstanceUtils::UsageReservationLedger reservations;
    int committedUsage = 0;
    ok = expectEqual("nested reservation #1", reservations.reserve(sourceKeyA, committedUsage, 2), true) && ok;
    ok = expectEqual("nested reservation #2", reservations.reserve(sourceKeyA, committedUsage, 2), true) && ok;
    ok = expectEqual("nested reservation reaches max", reservations.reserve(sourceKeyA, committedUsage, 2), false) && ok;
    ok = expectEqual("nested reservation count", reservations.count(sourceKeyA), 2) && ok;

    ok = expectEqual("pay failure releases one reservation", reservations.release(sourceKeyA), true) && ok;
    ok = expectEqual("pay failure count", reservations.count(sourceKeyA), 1) && ok;
    ok = expectEqual("cancel releases remaining reservation", reservations.release(sourceKeyA), true) && ok;
    ok = expectEqual("cancel clears key", reservations.count(sourceKeyA), 0) && ok;

    ok = expectEqual("bypass reserves before commit", reservations.reserve(sourceKeyA, committedUsage, 2), true) && ok;
    ok = expectEqual("bypass commit consumes reservation", reservations.release(sourceKeyA), true) && ok;
    ++committedUsage;
    ok = expectEqual("committed usage participates in limit", reservations.reserve(sourceKeyA, committedUsage, 2), true) && ok;
    ok = expectEqual("committed plus reserved reaches max", reservations.reserve(sourceKeyA, committedUsage, 2), false) && ok;
    ok = expectEqual("interrupted execution releases in-flight reservation", reservations.release(sourceKeyA), true) && ok;
    committedUsage = 0;
    ok = expectEqual("reset permits full quota again", reservations.reserve(sourceKeyA, committedUsage, 2), true) && ok;
    ok = expectEqual("reset test cleanup", reservations.release(sourceKeyA), true) && ok;

    ok = expectEqual("custom scope does not create an empty generic key",
                     reservations.reserve(QString(), 0, 1), false) && ok;

    SkillExecutionRegistry executions;
    {
        SkillExecutionRegistry::Guard execution = executions.begin(QVariant(QString("backing")));
        SkillExecutionRegistry::Entry *entry = execution.get();
        entry->immutableContextData = QVariant(QString("source/activation"));
        entry->contextData = QVariant(QString("mutated"));
        ok = expectEqual("execution keeps immutable identity snapshot",
                         entry->immutableContextData.toString(), "source/activation") && ok;
        ok = expectEqual("execution context remains independently mutable",
                         entry->contextData.toString(), "mutated") && ok;
        ok = expectEqual("execution finishes once", execution.finish(SkillExecutionNoResult), true) && ok;
        ok = expectEqual("execution cannot finish twice", execution.finish(SkillExecutionCompleted), false) && ok;
    }
    ok = expectEqual("execution guard removes completed entry", executions.size(), 0) && ok;

    if (ok)
        QTextStream(stdout) << "skill-instance-utils tests passed\n";
    return ok ? 0 : 1;
}
