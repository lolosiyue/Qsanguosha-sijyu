#include "room-runtime.h"

#include "engine.h"
#include "room.h"

#include <QDebug>

RoomRuntime::RoomRuntime(Room *room)
    : m_room(room), m_definitions(*Sanguosha), m_lua(LuaRuntime::Game), m_ai(room),
      m_roomState(false), m_loadingDefinitions(false), m_definitionsLoaded(false),
      m_nextDecisionId(0), m_stateRevision(0)
{
}

RoomRuntime::~RoomRuntime()
{
    m_ai.shutdown();
    m_lua.shutdown();
}

bool RoomRuntime::initialize(QString *error)
{
    if (!m_lua.initialize(error))
        return false;
    LuaRuntime::Binding luaBinding(m_lua);
    if (!m_lua.addPackagePath(QStringLiteral("./lua/?.lua"), error)
        || !m_lua.addPackagePath(QStringLiteral("./lua/?/init.lua"), error))
        return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, this);
    m_loadingDefinitions = true;
    const bool loaded = m_lua.loadScript(QStringLiteral("lua/config.lua"), error)
        && m_lua.loadScript(QStringLiteral("lua/sanguosha.lua"), error);
    m_loadingDefinitions = false;
    if (!loaded)
        return false;
    if (!m_lua.loadScript(QStringLiteral("lua/ai/smart-ai.lua"), error))
        return false;

    QString aiError;
    if (!m_ai.initialize(&aiError))
        qWarning().noquote() << "AI Lua runtime disabled:" << aiError;
    return true;
}

void RoomRuntime::seedRandom(quint64 seed)
{
    m_rng.seed(seed);
    m_lua.setSeed(seed);
    m_ai.seed(seed);
}

void RoomRuntime::advanceStateRevision(StateMutation mutation)
{
    Q_UNUSED(mutation);
    ++m_stateRevision;
    if (m_stateRevision == 0)
        ++m_stateRevision;
}

void RoomRuntime::addPackage(Package *package)
{
    m_definitions.addPackage(package);
}

void RoomRuntime::setPackage(Package *package)
{
    m_definitions.setPackage(package);
}

void RoomRuntime::addSkills(const QList<const Skill *> &skills)
{
    m_definitions.addSkills(skills);
}

void RoomRuntime::addTranslationEntry(const QString &key, const QString &value)
{
    m_definitions.addTranslationEntry(key, value);
}

QString RoomRuntime::translate(const QString &key, bool initial) const
{
    return m_definitions.translate(key, initial);
}

bool RoomRuntime::hasTranslation(const QString &key) const
{
    return m_definitions.hasTranslation(key);
}

const Package *RoomRuntime::package(const QString &name) const
{
    return m_definitions.package(name);
}

Package *RoomRuntime::packageOverlay(const Package *basePackage)
{
    return m_definitions.packageOverlay(basePackage);
}

QList<const Package *> RoomRuntime::packages() const
{
    return m_definitions.packages();
}

const General *RoomRuntime::general(const QString &name) const
{
    return m_definitions.general(name);
}

QList<const General *> RoomRuntime::generals() const
{
    return m_definitions.generals();
}

const Skill *RoomRuntime::skill(const QString &name) const
{
    return m_definitions.skill(name);
}

QStringList RoomRuntime::skillNames() const
{
    return m_definitions.skillNames();
}

QList<const Skill *> RoomRuntime::allSkills() const
{
    return m_definitions.allSkills();
}

const Card *RoomRuntime::engineCard(int id) const
{
    return m_definitions.engineCard(id);
}

const Card *RoomRuntime::cardTemplate(const QString &name) const
{
    return m_definitions.cardTemplate(name);
}

int RoomRuntime::cardCount(int bootstrapCount) const
{
    return m_definitions.cardCount(bootstrapCount);
}

const CardPattern *RoomRuntime::pattern(const QString &name) const
{
    return m_definitions.pattern(name);
}

QStringList RoomRuntime::relatedSkillNames(const QString &name) const
{
    return m_definitions.relatedSkillNames(name);
}

QList<const ProhibitSkill *> RoomRuntime::prohibitSkills() const
{
    return m_definitions.prohibitSkills();
}

QList<const DistanceSkill *> RoomRuntime::distanceSkills() const
{
    return m_definitions.distanceSkills();
}

QList<const MaxCardsSkill *> RoomRuntime::maxCardsSkills() const
{
    return m_definitions.maxCardsSkills();
}

QList<const TargetModSkill *> RoomRuntime::targetModSkills() const
{
    return m_definitions.targetModSkills();
}

QList<const InvaliditySkill *> RoomRuntime::invaliditySkills() const
{
    return m_definitions.invaliditySkills();
}

QList<const TriggerSkill *> RoomRuntime::globalTriggerSkills() const
{
    return m_definitions.globalTriggerSkills();
}

QList<const AttackRangeSkill *> RoomRuntime::attackRangeSkills() const
{
    return m_definitions.attackRangeSkills();
}

QList<const ViewAsEquipSkill *> RoomRuntime::viewAsEquipSkills() const
{
    return m_definitions.viewAsEquipSkills();
}

QList<const CardLimitSkill *> RoomRuntime::cardLimitSkills() const
{
    return m_definitions.cardLimitSkills();
}

QList<const ProhibitPindianSkill *> RoomRuntime::prohibitPindianSkills() const
{
    return m_definitions.prohibitPindianSkills();
}

void RoomRuntime::registerLuaSkillCard(const QString &name, const LuaSkillCard *card)
{
    m_definitions.registerLuaSkillCard(name, card);
}

const LuaSkillCard *RoomRuntime::luaSkillCard(const QString &name) const
{
    return m_definitions.luaSkillCard(name);
}

QObject *RoomRuntime::runtimeObject()
{
    return m_room;
}
