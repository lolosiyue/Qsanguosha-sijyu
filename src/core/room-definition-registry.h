#ifndef QSAN_ROOM_DEFINITION_REGISTRY_H
#define QSAN_ROOM_DEFINITION_REGISTRY_H

#include "skill-registry.h"

#include <QHash>
#include <QMap>
#include <QMultiMap>
#include <QObject>
#include <QSet>

class Card;
class CardPattern;
class Engine;
class General;
class LuaSkillCard;
class Package;

class RoomDefinitionRegistry
{
public:
    explicit RoomDefinitionRegistry(Engine &engine);

    void setBaselineAddresses(const QSet<const void *> &addresses) { m_baselineAddresses = addresses; }
    void clear();

    void addPackage(Package *package);
    void setPackage(Package *package);

    const Package *package(const QString &name) const;
    Package *packageOverlay(const Package *basePackage);
    QList<const Package *> packages() const;

    void addSkills(const QList<const Skill *> &skills);
    const Skill *skill(const QString &name) const;
    QStringList skillNames() const;
    QList<const Skill *> allSkills() const;

    const General *general(const QString &name) const;
    QList<const General *> generals() const;

    const Card *engineCard(int id) const;
    bool isEngineCard(const Card *card) const;
    const Card *cardTemplate(const QString &name) const;
    int cardCount(int bootstrapCount) const;

    const CardPattern *pattern(const QString &name) const;
    QStringList relatedSkillNames(const QString &name) const;

    void addTranslationEntry(const QString &key, const QString &value);
    QString translate(const QString &key, bool initial = false) const;
    bool hasTranslation(const QString &key) const;

    QList<const ProhibitSkill *> prohibitSkills() const;
    QList<const DistanceSkill *> distanceSkills() const;
    QList<const MaxCardsSkill *> maxCardsSkills() const;
    QList<const TargetModSkill *> targetModSkills() const;
    QList<const InvaliditySkill *> invaliditySkills() const;
    QList<const TriggerSkill *> globalTriggerSkills() const;
    QList<const AttackRangeSkill *> attackRangeSkills() const;
    QList<const ViewAsEquipSkill *> viewAsEquipSkills() const;
    QList<const CardLimitSkill *> cardLimitSkills() const;
    QList<const ProhibitPindianSkill *> prohibitPindianSkills() const;

    void registerLuaSkillCard(const QString &name, const LuaSkillCard *card);
    const LuaSkillCard *luaSkillCard(const QString &name) const;

private:
    void indexPackage(Package *package);

    Engine &m_engine;
    SkillRegistry m_skills;
    int m_nextCardId;

    QHash<QString, Package *> m_packages;
    QHash<QString, const General *> m_generals;

    QHash<int, const Card *> m_cards;
    QHash<const Card *, int> m_cardIds;
    QHash<QString, const Card *> m_cardTemplates;

    QMap<QString, const CardPattern *> m_patterns;
    QMultiMap<QString, QString> m_relatedSkills;

    QHash<QString, QString> m_translations;
    QHash<QString, QString> m_initialTranslations;

    QHash<QString, const LuaSkillCard *> m_luaSkillCards;

    QObject m_definitionRoot;
    QSet<const void *> m_baselineAddresses;
};

#endif
