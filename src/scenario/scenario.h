#ifndef _SCENARIO_H
#define _SCENARIO_H

#include "package.h"
#include "ai.h"

class Room;
class ScenarioRule;

class Scenario : public Package
{
    Q_OBJECT

public:
    explicit Scenario(const QString &name);
    ScenarioRule *getRule() const;
    inline void setRule(ScenarioRule *scenario_rule)
    {
        rule = scenario_rule;
    }
	
    inline void setScenarioLord(const QString &lord)
    {
        this->lord = lord;
    }
    inline void addScenarioLoyalists(const QString &loyalist)
    {
        loyalists << loyalist;
    }
    inline void addScenarioRebels(const QString &rebel)
    {
        rebels << rebel;
    }
    inline void addScenarioRenegades(const QString &renegade)
    {
        renegades << renegade;
    }
    inline void setExposeRoles(bool expose)
    {
        this->expose = expose;
    }

    virtual bool exposeRoles() const;
    virtual int getPlayerCount() const;
    virtual QString getRoles() const;
    virtual void assign(QStringList &generals, QStringList &roles) const;
    virtual AI::Relation relationTo(const ServerPlayer *a, const ServerPlayer *b) const;
    virtual void onTagSet(Room *room, const QString &key) const;
    virtual bool generalSelection() const;

protected:
    QString lord;
    QStringList loyalists, rebels, renegades;
    ScenarioRule *rule;
    bool expose;
};

class LuaScenario : public Scenario
{
    Q_OBJECT

public:
    explicit LuaScenario(const QString &name);
    virtual void assign(QStringList &generals, QStringList &roles) const;
};

#endif

