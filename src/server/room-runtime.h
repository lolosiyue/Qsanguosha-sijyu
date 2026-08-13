#ifndef QSAN_ROOM_RUNTIME_H
#define QSAN_ROOM_RUNTIME_H

#include "engine-runtime-context.h"
#include "ai-runtime.h"
#include "game-rng.h"
#include "lua-runtime.h"
#include "room-state.h"
#include "skill-execution-registry.h"
#include "skill-instance-attachment-registry.h"
#include "skill-registry.h"

#include <QHash>
#include <QMultiMap>
#include <QObject>

class CardPattern;
class General;
class LuaSkillCard;
class Package;
class Room;

class RoomRuntime : public EngineRuntimeContext
{
public:
    enum StateMutation {
        CardsMoved,
        PlayerPropertyChanged,
        PlayerLifecycleChanged,
        PlayerMarkChanged,
        SkillSetChanged,
        TurnStateChanged,
        CardLimitationChanged,
        SkillInstanceStateChanged
    };

    explicit RoomRuntime(Room *room);
    ~RoomRuntime() override;

    bool initialize(QString *error = nullptr);
    bool isLoadingDefinitions() const { return m_loadingDefinitions; }
    bool definitionsLoaded() const { return m_definitionsLoaded; }
    void finishDefinitionLoading() { m_definitionsLoaded = true; }

    LuaRuntime &lua() { return m_lua; }
    const LuaRuntime &lua() const { return m_lua; }
    AiLuaRuntime &ai() { return m_ai; }
    const AiLuaRuntime &ai() const { return m_ai; }
    GameRng &rng() { return m_rng; }
    RoomState &state() { return m_roomState; }
    SkillExecutionRegistry &skillExecutions() { return m_skillExecutions; }
    SkillInstanceAttachmentRegistry &attachedSkills() { return m_attachedSkills; }
    quint64 nextDecisionId() { return ++m_nextDecisionId; }
    quint64 stateRevision() const { return m_stateRevision; }
    void advanceStateRevision(StateMutation mutation);
    void seedRandom(quint32 seed);

    void addPackage(Package *package);
    void setPackage(Package *package);
    void addSkills(const QList<const Skill *> &skills);
    void addTranslationEntry(const QString &key, const QString &value);
    QString translate(const QString &key, bool initial = false) const;
    bool hasTranslation(const QString &key) const;

    const Package *package(const QString &name) const;
    Package *packageOverlay(const Package *basePackage);
    QList<const Package *> packages() const;
    const General *general(const QString &name) const;
    QList<const General *> generals() const;
    const Skill *skill(const QString &name) const;
    QStringList skillNames() const;
    QList<const Skill *> allSkills() const;
    const Card *engineCard(int id) const;
    const Card *cardTemplate(const QString &name) const;
    int cardCount(int bootstrapCount) const;
    const CardPattern *pattern(const QString &name) const;
    QStringList relatedSkillNames(const QString &name) const;

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

    QObject *runtimeObject() override;
    RoomState *roomState() override { return &m_roomState; }
    const Player *cardOwner(int) const override { return nullptr; }
    Player::Place cardPlace(int) const override { return Player::PlaceUnknown; }
    Card *card(int cardId) const override { return m_roomState.getCard(cardId); }
    RoomRuntime *roomRuntime() override { return this; }

private:
    void indexPackage(Package *package);

    Room *m_room;
    LuaRuntime m_lua;
    AiLuaRuntime m_ai;
    GameRng m_rng;
    RoomState m_roomState;
    SkillExecutionRegistry m_skillExecutions;
    SkillInstanceAttachmentRegistry m_attachedSkills;
    SkillRegistry m_skills;
    bool m_loadingDefinitions;
    bool m_definitionsLoaded;
    int m_nextCardId;
    quint64 m_nextDecisionId;
    quint64 m_stateRevision;
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
};

#endif
