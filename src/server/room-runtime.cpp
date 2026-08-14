#include "room-runtime.h"

#include "engine.h"
#include "general.h"
#include "lua-wrapper.h"
#include "package.h"
#include "room.h"

#include <QDebug>

RoomRuntime::RoomRuntime(Room *room)
    : m_room(room), m_lua(LuaRuntime::Game), m_ai(room), m_roomState(false),
      m_loadingDefinitions(false), m_definitionsLoaded(false), m_nextCardId(0),
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
    m_nextCardId = Sanguosha->getCardCount();
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
    if (!package || m_packages.contains(package->objectName()))
        return;
    package->setParent(&m_definitionRoot);
    m_packages.insert(package->objectName(), package);
    indexPackage(package);
}

void RoomRuntime::setPackage(Package *package)
{
    if (!package)
        return;
    if (!m_packages.contains(package->objectName())) {
        addPackage(package);
        return;
    }
    indexPackage(package);
}

void RoomRuntime::indexPackage(Package *package)
{
    const QList<int> bootstrapIds = Sanguosha->m_packageCardIds.value(package->objectName());
    const QList<Card *> packageCards = package->findChildren<Card *>();
    for (int index = 0; index < packageCards.size(); ++index) {
        Card *card = packageCards.at(index);
        if (m_cardIds.contains(card)) {
            card->setId(m_cardIds.value(card));
            continue;
        }
        int id = index < bootstrapIds.size() ? bootstrapIds.at(index) : m_nextCardId++;
        if (m_cards.contains(id) && m_cards.value(id) != card) {
            qWarning("Room package '%s' card id %d collided; assigning a room-local id.",
                     qPrintable(package->objectName()), id);
            while (m_cards.contains(m_nextCardId))
                ++m_nextCardId;
            id = m_nextCardId++;
        }
        card->setId(id);
        m_cards.insert(id, card);
        m_cardIds.insert(card, id);
        if (!m_cardTemplates.contains(card->objectName()))
            m_cardTemplates.insert(card->objectName(), card);
        if (!m_cardTemplates.contains(card->getClassName()))
            m_cardTemplates.insert(card->getClassName(), card);
    }

    const QMap<QString, const CardPattern *> packagePatterns = package->getPatterns();
    for (auto it = packagePatterns.cbegin(); it != packagePatterns.cend(); ++it)
        m_patterns.insert(it.key(), it.value());
    m_relatedSkills.unite(package->getRelatedSkills());

    QList<const Skill *> packageSkills = package->getSkills();
    packageSkills << package->findChildren<const Skill *>();
    addSkills(packageSkills);

    foreach (General *general, package->findChildren<General *>())
        m_generals.insert(general->objectName(), general);
}

void RoomRuntime::addSkills(const QList<const Skill *> &skills)
{
    foreach (const Skill *skill, skills) {
        if (!skill)
            continue;
        Skill *mutableSkill = const_cast<Skill *>(skill);
        if (!mutableSkill->parent())
            mutableSkill->setParent(&m_definitionRoot);
        m_skills.add(skill);
    }
}

void RoomRuntime::addTranslationEntry(const QString &key, const QString &value)
{
    if (!m_translations.contains(key))
        m_initialTranslations.insert(key, value);
    m_translations.insert(key, value);
}

QString RoomRuntime::translate(const QString &key, bool initial) const
{
    const QHash<QString, QString> &catalog = initial ? m_initialTranslations : m_translations;
    return catalog.value(key, key);
}

bool RoomRuntime::hasTranslation(const QString &key) const
{
    return m_translations.contains(key);
}

const Package *RoomRuntime::package(const QString &name) const
{
    return m_packages.value(name, nullptr);
}

Package *RoomRuntime::packageOverlay(const Package *basePackage)
{
    if (!basePackage)
        return nullptr;
    Package *overlay = m_packages.value(basePackage->objectName(), nullptr);
    if (overlay)
        return overlay;

    overlay = Sanguosha->clonePackageDefinition(basePackage->objectName());
    if (!overlay) {
        qWarning("Package '%s' has no factory for a room-local overlay.",
                 qPrintable(basePackage->objectName()));
        return nullptr;
    }
    addPackage(overlay);
    return overlay;
}

QList<const Package *> RoomRuntime::packages() const
{
    QList<const Package *> result;
    foreach (Package *package, m_packages)
        result << package;
    return result;
}

const General *RoomRuntime::general(const QString &name) const
{
    return m_generals.value(name, nullptr);
}

QList<const General *> RoomRuntime::generals() const
{
    return m_generals.values();
}

const Skill *RoomRuntime::skill(const QString &name) const
{
    return m_skills.find(name);
}

QStringList RoomRuntime::skillNames() const
{
    return m_skills.names();
}

QList<const Skill *> RoomRuntime::allSkills() const
{
    return m_skills.allSkills();
}

const Card *RoomRuntime::engineCard(int id) const
{
    return m_cards.value(id, nullptr);
}

const Card *RoomRuntime::cardTemplate(const QString &name) const
{
    return m_cardTemplates.value(name, nullptr);
}

int RoomRuntime::cardCount(int bootstrapCount) const
{
    int count = bootstrapCount;
    foreach (int id, m_cards.keys())
        count = qMax(count, id + 1);
    return count;
}

const CardPattern *RoomRuntime::pattern(const QString &name) const
{
    return m_patterns.value(name, nullptr);
}

QStringList RoomRuntime::relatedSkillNames(const QString &name) const
{
    return m_relatedSkills.values(name);
}

QList<const ProhibitSkill *> RoomRuntime::prohibitSkills() const { return m_skills.prohibitSkills(); }
QList<const DistanceSkill *> RoomRuntime::distanceSkills() const { return m_skills.distanceSkills(); }
QList<const MaxCardsSkill *> RoomRuntime::maxCardsSkills() const { return m_skills.maxCardsSkills(); }
QList<const TargetModSkill *> RoomRuntime::targetModSkills() const { return m_skills.targetModSkills(); }
QList<const InvaliditySkill *> RoomRuntime::invaliditySkills() const { return m_skills.invaliditySkills(); }
QList<const TriggerSkill *> RoomRuntime::globalTriggerSkills() const { return m_skills.globalTriggerSkills(); }
QList<const AttackRangeSkill *> RoomRuntime::attackRangeSkills() const { return m_skills.attackRangeSkills(); }
QList<const ViewAsEquipSkill *> RoomRuntime::viewAsEquipSkills() const { return m_skills.viewAsEquipSkills(); }
QList<const CardLimitSkill *> RoomRuntime::cardLimitSkills() const { return m_skills.cardLimitSkills(); }
QList<const ProhibitPindianSkill *> RoomRuntime::prohibitPindianSkills() const { return m_skills.prohibitPindianSkills(); }

void RoomRuntime::registerLuaSkillCard(const QString &name, const LuaSkillCard *card)
{
    LuaSkillCard *ownedCard = const_cast<LuaSkillCard *>(card);
    if (ownedCard && !ownedCard->parent())
        ownedCard->setParent(&m_definitionRoot);
    if (!name.isEmpty() && card)
        m_luaSkillCards.insert(name, card);
}

const LuaSkillCard *RoomRuntime::luaSkillCard(const QString &name) const
{
    return m_luaSkillCards.value(name, nullptr);
}

QObject *RoomRuntime::runtimeObject()
{
    return m_room;
}
