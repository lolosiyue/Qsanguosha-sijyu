#include "scenario.h"
#include "engine.h"
//#include "util.h"
//#include "serverplayer.h"

Scenario::Scenario(const QString &name)
    : Package(name, Package::SpecialPack), rule(nullptr), expose(true)
{
}

int Scenario::getPlayerCount() const
{
    return 1 + loyalists.length() + rebels.length() + renegades.length();
}

ScenarioRule *Scenario::getRule() const
{
    return rule;
}

bool Scenario::exposeRoles() const
{
    return expose;
}

QString Scenario::getRoles() const
{
    QString roles = "Z";
    for (int i = 0; i < loyalists.length(); i++)
        roles.append('C');
    for (int i = 0; i < rebels.length(); i++)
        roles.append('N');
    for (int i = 0; i < rebels.length(); i++)
        roles.append('F');
    return roles;
}

void Scenario::assign(QStringList &generals, QStringList &roles) const
{
    generals << lord << loyalists << rebels << renegades;
    qShuffle(generals);
    foreach (QString general, generals) {
        if (general == lord) roles << "lord";
        else if (loyalists.contains(general)) roles << "loyalist";
        else if (rebels.contains(general)) roles << "rebel";
        else roles << "renegade";
    }
}

bool Scenario::generalSelection() const
{
    return false;
}

void Scenario::onTagSet(Room *, const QString &) const
{
}

AI::Relation Scenario::relationTo(const ServerPlayer *a, const ServerPlayer *b) const
{
    return AI::GetRelation(a, b);
}

LuaScenario::LuaScenario(const QString &name)
    : Scenario(name)
{
}

void LuaScenario::assign(QStringList &generals, QStringList &roles) const
{
    generals << lord << loyalists << rebels << renegades;
    qShuffle(generals);
	if (lord!="")
		roles << "lord";
    for (int i = 0; i < loyalists.length(); i++)
		roles << "loyalist";
    for (int i = 0; i < rebels.length(); i++)
		roles << "rebel";
    for (int i = 0; i < renegades.length(); i++)
		roles << "renegade";
}