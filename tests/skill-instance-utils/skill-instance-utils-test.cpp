#include "skill-instance-utils.h"

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

    if (ok)
        QTextStream(stdout) << "skill-instance-utils tests passed\n";
    return ok ? 0 : 1;
}
