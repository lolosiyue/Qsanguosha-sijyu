#include "room-definition-registry.h"
#include "distance-skill-cache.h"

#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "general.h"
#include "h-rule-cards.h"
#include "lua-wrapper.h"
#include "package.h"
#include "room.h"
#include "room-runtime.h"
#include "ai-strategy-round56-bootstrap-source.h"

#include <QDebug>
#include <QThread>

RoomDefinitionRegistry::RoomDefinitionRegistry(Engine &engine)
    : m_engine(engine), m_distanceSkillCache(new DistanceSkillCache),
      m_definitionListCaches(new DefinitionListCaches),
      m_nextCardId(int(engine.cards.size()))
{
    // Rule operations are room-owned definitions, independent of package selection.
    addSkills(createHegemonyRuleSkills(&m_definitionRoot));
}

RoomDefinitionRegistry::~RoomDefinitionRegistry() = default;

bool RoomDefinitionRegistry::moveOwnedObjectsToThread(QThread *targetThread, QString *error)
{
    if (!targetThread) {
        if (error)
            *error = QStringLiteral("Room definition target thread is null");
        return false;
    }
    if (m_definitionRoot.thread() != QThread::currentThread()) {
        if (error)
            *error = QStringLiteral("Room definitions can only move from their owning thread");
        return false;
    }

    QObjectList ownedObjects{&m_definitionRoot};
    ownedObjects.append(m_definitionRoot.findChildren<QObject *>());
    for (const QObject *object : std::as_const(ownedObjects)) {
        if (object && object->thread() != QThread::currentThread()) {
            if (error)
                *error = QStringLiteral("Room definition QObject tree has mixed affinity");
            return false;
        }
    }

    // Packages and every QObject they own follow this single root. This lets
    // deferred initialization create them on the worker and hand them back as
    // one affinity-safe tree before the Room is published on the main thread.
    m_definitionRoot.moveToThread(targetThread);
    for (const QObject *object : std::as_const(ownedObjects)) {
        if (object && object->thread() != targetThread) {
            if (error)
                *error = QStringLiteral("Room definition QObject affinity handoff failed");
            return false;
        }
    }
    return true;
}

void RoomDefinitionRegistry::clear()
{
    observeFactoryMapWriter();
    const QObjectList children = m_definitionRoot.children();
    CardLifetimeManager &manager = globalCardLifetimeManager();
    // Keep externally borrowed identity distinct from the additional direct
    // child entries used only for shutdown accounting below.
    const QSet<const void *> originalBaselineAddresses = m_baselineAddresses;
    QSet<const void *> baselineAddresses = m_baselineAddresses;
    for (const Card *card : std::as_const(m_engine.cards))
        if (card)
            baselineAddresses.insert(card);
    for (QObject *child : children) {
        const QList<Card *> cards = child->findChildren<Card *>();
        if (const Card *card = qobject_cast<const Card *>(child))
            baselineAddresses.insert(card);
        for (const Card *card : cards) {
            if (card && m_engine.cards.contains(const_cast<Card *>(card)))
                baselineAddresses.insert(card);
        }
    }
    if (baselineAddresses != m_baselineAddresses) {
        m_baselineAddresses = baselineAddresses;
        manager.setDomainBaseline(this, baselineAddresses);
    }
    const auto borrowedCard = [this](const Card *card) {
        return card && (m_baselineAddresses.contains(card)
            || m_engine.cards.contains(const_cast<Card *>(card)));
    };
    for (QObject *child : children) {
        bool borrowedRoot = false;
        QList<Card *> cards = child->findChildren<Card *>();
        if (Card *card = qobject_cast<Card *>(child))
            cards.prepend(card);
        for (Card *card : cards) {
            // Published globals cannot enter this ownership tree through an
            // admitted API. Do not silently delete or unseal one after a native
            // caller bypasses the contract with an explicit QObject base call.
            if (isPublishedGlobalCard(card))
                qFatal("Published global card illegally owned by Room definition tree");
            if (borrowedCard(card)) {
                borrowedRoot = borrowedRoot || (card == child
                    && originalBaselineAddresses.contains(card));
                card->setParent(m_engine.cards.contains(const_cast<Card *>(card)) ? &m_engine : nullptr);
                continue;
            }
            manager.observeCard(card);
        }
        if (!borrowedRoot) delete child;
    }
    if (!m_definitionRoot.children().isEmpty())
        qFatal("Room definition root retained children after shutdown");
    m_packages.clear();
    m_generals.clear();
    m_printedSkillNames.clear();
    ++m_printedSkillGeneration;
    m_cards.clear();
    m_cardSpan = 0;
    m_cardIds.clear();
    m_cardTemplates.clear();
    m_patterns.clear();
    m_relatedSkills.clear();
    m_translations.clear();
    m_initialTranslations.clear();
    m_luaSkillCards.clear();
}

void RoomDefinitionRegistry::addPackage(Package *package)
{
    observeFactoryMapWriter();
    if (!canReparentDefinitionObject(package, nullptr)) {
        qWarning("Room registration of published global card owner rejected");
        return;
    }
    if (!package || m_packages.contains(package->objectName()))
        return;
    if (!package->parent())
        package->setParent(&m_definitionRoot);
    m_packages.insert(package->objectName(), package);
    indexPackage(package);
}

void RoomDefinitionRegistry::setPackage(Package *package)
{
    observeFactoryMapWriter();
    if (!canReparentDefinitionObject(package, nullptr)) {
        qWarning("Room registration of published global card owner rejected");
        return;
    }
    if (!package)
        return;
    if (!m_packages.contains(package->objectName())) {
        addPackage(package);
        return;
    }
    indexPackage(package);
}

void RoomDefinitionRegistry::indexPackage(Package *package)
{
    observeFactoryMapWriter();
    const QList<int> bootstrapIds = m_engine.m_packageCardIds.value(package->objectName());
    const QList<Card *> packageCards = package->findChildren<Card *>();
    for (int index = 0; index < packageCards.size(); ++index) {
        Card *card = packageCards.at(index);
        if (m_cardIds.contains(card)) {
            card->aiRound56ReassertPhysicalDefinitionId(m_cardIds.value(card));
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
        card->aiRound56RecordPhysicalDefinitionIndexSource(id);
        m_cards.insert(id, card);
        m_cardSpan = qMax(m_cardSpan, id + 1);
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

void RoomDefinitionRegistry::addSkills(const QList<const Skill *> &skills)
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

void RoomDefinitionRegistry::addTranslationEntry(const QString &key, const QString &value)
{
    if (!m_translations.contains(key))
        m_initialTranslations.insert(key, value);
    m_translations.insert(key, value);
}

QString RoomDefinitionRegistry::translate(const QString &key, bool initial) const
{
    const QHash<QString, QString> &catalog = initial ? m_initialTranslations : m_translations;
    return catalog.value(key, key);
}

bool RoomDefinitionRegistry::hasTranslation(const QString &key) const
{
    return m_translations.contains(key);
}

const Package *RoomDefinitionRegistry::package(const QString &name) const
{
    return m_packages.value(name, nullptr);
}

Package *RoomDefinitionRegistry::packageOverlay(const Package *basePackage)
{
    if (!basePackage)
        return nullptr;

    Package *overlay = m_packages.value(basePackage->objectName(), nullptr);
    if (overlay)
        return overlay;

    overlay = m_engine.clonePackageDefinition(basePackage->objectName());
    if (!overlay) {
        qWarning("Package '%s' has no factory for a room-local overlay.",
                 qPrintable(basePackage->objectName()));
        return nullptr;
    }
    addPackage(overlay);
    return overlay;
}

QList<const Package *> RoomDefinitionRegistry::packages() const
{
    QList<const Package *> result;
    foreach (Package *package, m_packages)
        result << package;
    return result;
}

const General *RoomDefinitionRegistry::general(const QString &name) const
{
    return m_generals.value(name, nullptr);
}

QList<const General *> RoomDefinitionRegistry::generals() const
{
    return m_generals.values();
}

const Skill *RoomDefinitionRegistry::skill(const QString &name) const
{
    return m_skills.find(name);
}

QStringList RoomDefinitionRegistry::skillNames() const
{
    return m_skills.names();
}

QList<const Skill *> RoomDefinitionRegistry::allSkills() const
{
    return m_skills.allSkills();
}

const Card *RoomDefinitionRegistry::engineCard(int id) const
{
    return m_cards.value(id, nullptr);
}

bool RoomDefinitionRegistry::isEngineCard(const Card *card) const
{
    return card && m_engine.cards.contains(const_cast<Card *>(card));
}

bool RoomDefinitionRegistry::printedSkillName(const Card *card, QString *value) const
{
    const auto it = m_printedSkillNames.constFind(card);
    if (it == m_printedSkillNames.cend()) return false;
    if (value) *value = it.value();
    return true;
}

bool RoomDefinitionRegistry::setPrintedSkillName(const Card *card, const QString &value)
{
    // This is not an arbitrary-card mutation API: it only stores the existing
    // compiled BasicCard field. Owned definitions/clones keep ordinary storage.
    if (!card || !isEngineCard(card) || card->getTypeId() != Card::TypeBasic) return false;
    const auto it = m_printedSkillNames.constFind(card);
    if (it == m_printedSkillNames.cend() || it.value() != value) {
        m_printedSkillNames.insert(card, value);
        ++m_printedSkillGeneration;
    }
    return true;
}

QMap<int, QString> RoomDefinitionRegistry::printedSkillSnapshot() const
{
    QMap<int, QString> result;
    for (auto it = m_printedSkillNames.cbegin(); it != m_printedSkillNames.cend(); ++it)
        result.insert(it.key()->getId(), it.value());
    return result;
}

bool RoomDefinitionRegistry::restorePrintedSkillSnapshot(const QMap<int, QString> &snapshot, QString *error)
{
    QHash<const Card *, QString> prepared;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        const Card *card = m_engine.cards.value(it.key(), nullptr);
        if (!card || card->getId() != it.key() || card->getTypeId() != Card::TypeBasic) {
            if (error) *error = QStringLiteral("invalid compiled BasicCard attribution identity");
            return false;
        }
        prepared.insert(card, it.value());
    }
    // Full validation precedes mutation; underscore and explicit empty strings
    // are raw field values, not normalized getSkillName() results.
    m_printedSkillNames.swap(prepared);
    ++m_printedSkillGeneration;
    return true;
}

const Card *RoomDefinitionRegistry::cardTemplate(const QString &name) const
{
    return m_cardTemplates.value(name, nullptr);
}

int RoomDefinitionRegistry::cardCount(int bootstrapCount) const
{
    // IDs can be sparse; retain the upper bound at insertion instead of
    // rebuilding a key list in every deck/snapshot enumeration condition.
    return qMax(bootstrapCount, m_cardSpan);
}

const CardPattern *RoomDefinitionRegistry::pattern(const QString &name) const
{
    return m_patterns.value(name, nullptr);
}

QStringList RoomDefinitionRegistry::relatedSkillNames(const QString &name) const
{
    return m_relatedSkills.values(name);
}

QList<const ProhibitSkill *> RoomDefinitionRegistry::prohibitSkills() const
{
    return m_skills.prohibitSkills();
}

QList<const DistanceSkill *> RoomDefinitionRegistry::distanceSkills() const
{
    return m_skills.distanceSkills();
}

QList<const MaxCardsSkill *> RoomDefinitionRegistry::maxCardsSkills() const
{
    return m_skills.maxCardsSkills();
}

QList<const TargetModSkill *> RoomDefinitionRegistry::targetModSkills() const
{
    return m_skills.targetModSkills();
}

QList<const InvaliditySkill *> RoomDefinitionRegistry::invaliditySkills() const
{
    return m_skills.invaliditySkills();
}

QList<const TriggerSkill *> RoomDefinitionRegistry::globalTriggerSkills() const
{
    return m_skills.globalTriggerSkills();
}

QList<const AttackRangeSkill *> RoomDefinitionRegistry::attackRangeSkills() const
{
    return m_skills.attackRangeSkills();
}

QList<const ViewAsEquipSkill *> RoomDefinitionRegistry::viewAsEquipSkills() const
{
    return m_skills.viewAsEquipSkills();
}

QList<const CardLimitSkill *> RoomDefinitionRegistry::cardLimitSkills() const
{
    return m_skills.cardLimitSkills();
}

QList<const ProhibitPindianSkill *> RoomDefinitionRegistry::prohibitPindianSkills() const
{
    return m_skills.prohibitPindianSkills();
}

void RoomDefinitionRegistry::registerLuaSkillCard(const QString &name, const LuaSkillCard *card)
{
    if (isPublishedGlobalCard(card)) {
        qWarning("Room registration of published global card template rejected");
        return;
    }
    LuaSkillCard *ownedCard = const_cast<LuaSkillCard *>(card);
    if (ownedCard && !ownedCard->parent())
        ownedCard->setParent(&m_definitionRoot);
    if (!name.isEmpty() && card)
        m_luaSkillCards.insert(name, card);
}

const LuaSkillCard *RoomDefinitionRegistry::luaSkillCard(const QString &name) const
{
    return m_luaSkillCards.value(name, nullptr);
}

// Issued only from the original RoomRuntime::initialize, before namespace birth.
// The member starts Pristine at native construction; no map inspection seeds it.
void RoomDefinitionRegistry::beginFactorySourceOrigin(Room &room) noexcept
{
    if (m_factorySourcePhase != FactorySourcePhase::Pristine) {
        m_factorySourcePhase = FactorySourcePhase::Retired;
        return;
    }
    m_factorySourceRoom = &room;
    m_factorySourcePhase = FactorySourcePhase::Collecting;
}

void RoomDefinitionRegistry::observeFactoryMapWriter() noexcept
{
    // Even null, duplicate and equal/no-op public attempts retire after seal.
    // During bootstrap, only the original synchronous definition-loading graph
    // can collect writers; the complete source token closes that graph later.
    if (m_factorySourcePhase != FactorySourcePhase::Collecting
        || !m_factorySourceRoom
        || !isAIRound56OriginalDefinitionProducer(*m_factorySourceRoom, this))
        m_factorySourcePhase = FactorySourcePhase::Retired;
}

bool RoomDefinitionRegistry::hasFactorySourceOrigin(const Room &room, lua_State *L) const noexcept
{
    auto *runtime = const_cast<Room &>(room).roomRuntime();
    return m_factorySourcePhase == FactorySourcePhase::Sealed
        && m_factorySourceRoom == &room && runtime && &runtime->definitions() == this
        && !runtime->isClosing() && runtime->lua().isCurrentThreadOwner()
        && runtime->lua().rawState() == L
        && runtime->lua().generation() == m_factorySourceGeneration;
}
