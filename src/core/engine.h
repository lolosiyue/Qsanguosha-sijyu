#ifndef _ENGINE_H
#define _ENGINE_H

//#include "card.h"
#include "skill.h"
#include "audio.h"
#include "util.h"
#include <QPointer>
#include <QMutex>
#include <QReadWriteLock>
#include <QThread>

// Recursive mutex with yield/restore support.
// Unlike QRecursiveMutex, it allows atomically releasing all recursive
// lock levels (yield) and re-acquiring them later (restore), which is
// essential for preventing deadlocks when the server thread sleeps
// while holding the Lua lock.
class SafeLuaMutex {
public:
    void lock();
    void unlock();
    bool tryLock(int timeout = 0);
    int yield();
    void restore(int count);
private:
    QMutex m_mutex;
    QThread *m_owner = nullptr;
    int m_count = 0;
};

class Scenario;
class CardPattern;
class RoomState;
class ExpPattern;

class Engine : public QObject
{
    Q_OBJECT

public:
	Engine(bool isManualMode = false);
    ~Engine();

    void addTranslationEntry(const QString &key, const QString &value);
    QString translate(const QString &to_translate, bool initial = false) const;
    lua_State *getLuaState() const;
    SafeLuaMutex &getLuaMutex() const;
    void addModes(const QString &key, const QString &value, const QString &roles = "");

    int getMiniSceneCounts();

    void addPackage(Package *package);
    void addBanPackage(const QString &package_name);
    QList<const Package *> getPackages() const;
    Package *getPackage(const QString &package_name);
    void setPackage(Package *package);
    QStringList getBanPackages() const;
    QStringList getZhinangCards() const;
    void setZhinangCard(const QString &flag);
    Card *cloneCard(const Card *card) const;
    Card *cloneCard(const QString &name, Card::Suit suit = Card::NoSuit, int number = 0, const QStringList &flags = QStringList()) const;
    SkillCard *cloneSkillCard(const QString &name) const;
    QString getVersionNumber() const;
    QString getVersion() const;
    QString getVersionName() const;
    QString getMODName() const;
    QStringList getExtensions() const;
    QStringList getKingdoms() const;
    QColor getKingdomColor(const QString &kingdom) const;
    QMap<QString, QColor> getSkillTypeColorMap() const;
    QStringList getChattingEasyTexts() const;
    QString getSetupString() const;

    QMap<QString, QString> getAvailableModes() const;
    QString getModeName(const QString &mode) const;
    int getPlayerCount(const QString &mode) const;
    QString getRoles(const QString &mode) const;
    QStringList getRoleList(const QString &mode) const;
    int getRoleIndex() const;

    const CardPattern *getPattern(const QString &name, bool extra = true) const;
    bool matchPattern(const QString &pattern, const Player *player, const Card *card) const;
    bool matchExpPattern(const QString &pattern, const Player *player, const Card *card) const;
    Card::HandlingMethod getCardHandlingMethod(const QString &method_name) const;
    QList<const Skill *> getRelatedSkills(const QString &skill_name) const;
    const Skill *getMainSkill(const QString &skill_name) const;
    

    QStringList getModScenarioNames() const;
    void addScenario(Scenario *scenario);
    const Scenario *getScenario(const QString &name) const;
    void addPackage(const QString &name);

    const General *getGeneral(const QString &name) const;
    int getGeneralCount(bool include_banned = false, const QString &kingdom = "") const;
    const Skill *getSkill(const QString &skill_name) const;
    const Skill *getSkill(const EquipCard *card) const;
    QStringList getSkillNames() const;
    Skill *getRealSkill(const QString &skill_name);
    const TriggerSkill *getTriggerSkill(const QString &skill_name) const;
    const ViewAsSkill *getViewAsSkill(const QString &skill_name) const;
    const ViewAsEquipSkill *getViewAsEquipSkill(const QString &skill_name) const;
    const CardLimitSkill *getCardLimitSkill(const QString &skill_name) const;
    QList<const ProhibitSkill *> getProhibitSkills() const;
    QList<const DistanceSkill *> getDistanceSkills() const;
    QList<const MaxCardsSkill *> getMaxCardsSkills() const;
    QList<const TargetModSkill *> getTargetModSkills() const;
    QList<const InvaliditySkill *> getInvaliditySkills() const;
    QList<const TriggerSkill *> getGlobalTriggerSkills() const;
    QList<const AttackRangeSkill *> getAttackRangeSkills() const;
    QList<const ViewAsEquipSkill *> getViewAsEquipSkills() const;
    QList<const CardLimitSkill *> getCardLimitSkills() const;
    QList<const ProhibitPindianSkill *> getProhibitPindianSkills() const;
    void addSkills(QList<const Skill *> all_skills);

    int getCardCount() const;
    const Card *getEngineCard(int cardId) const;
    // @todo: consider making this const Card *
    Card *getCard(int cardId, bool need_Q_ASSERT = true);
    WrappedCard *getWrappedCard(int cardId);

    QStringList getLords(bool contain_banned = false) const;
    QStringList getRandomLords() const;
    QStringList getRandomGenerals(int count, const QSet<QString> &ban_set = QSet<QString>(), const QString &kingdom = "") const;
    QList<int> getRandomCards(bool derivative = false) const;
    QString getRandomGeneralName() const;
    QStringList getLimitedGeneralNames(const QString &kingdom = "" , bool available = true) const;
    QStringList getSlashNames() const;
    QStringList getCardNames(const QString &pattern = ".") const;
    bool hasCard(const QString &name) const;
    inline QList<const General *> getAllGenerals() const
    {
        return findChildren<const General *>();
    }
    bool sameNameWith(const QString &name1, const QString &name2) const;

    bool playSystemAudioEffect(const QString &name, bool superpose = true) const;
    bool playAudioEffect(const QString &filename, bool superpose = true) const;
    void setAudioType(const QString &general_name, const QString &filename, const QString &types);
    int revisesAudioType(const QString &general_name, const QString &filename, int type) const;
    void playSkillAudioEffect(const QString &skill_name, int index, bool superpose = true) const;

    const ProhibitSkill *isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;
    const ProhibitPindianSkill *isPindianProhibited(const Player *from, const Player *to) const;
    const CardLimitSkill *isCardLimited(const Player *player, const Card *card, Card::HandlingMethod method, bool isHandcard = false) const;
    int correctDistance(const Player *from, const Player *to, bool fixed = false) const;
    int correctMaxCards(const Player *target, bool fixed = false) const;
    int correctCardTarget(const TargetModSkill::ModType type, const Player *from, const Card *card, const Player *to = nullptr) const;
    bool correctSkillValidity(const Player *player, const Skill *skill) const;
    int correctAttackRange(const Player *target, bool include_weapon = true, bool fixed = false) const;

    void registerRoom(QObject *room);
    void unregisterRoom();
    QObject *currentRoomObject();
    Room *currentRoom();
    RoomState *currentRoomState();

    const Player *getCardOwner(int card_id);
    Player::Place getCardPlace(int card_id);

    QString getCurrentCardUsePattern();
    CardUseStruct::CardUseReason getCurrentCardUseReason();

    QString findConvertFrom(const QString &general_name) const;
    bool isGeneralHidden(const QString &general_name) const;

    QString removeNumberInQString(const QString &str) const;

    inline QMultiMap<QString, QString> spConvertPairs() const
    {
        return sp_convert_pairs;
    }

private:
    void _loadMiniScenarios();
    void _loadModScenarios();
    void godLottery(QStringList &) const;
	void godLottery(QSet<QString> &) const;
    QList<const Skill *> getSafeSkills() const;
    mutable QReadWriteLock m_rwLock;
    QHash<QString, QString> translations, engine_translations;
    QHash<QString, const General *> generals, available_generals;
    QHash<QString, const QMetaObject *> metaobjects;
    //QHash<QString, QString> className2objectName;
    QHash<QString, QPointer<Skill>> skills;
    QHash<QThread *, QObject *> m_rooms;
    mutable QMutex m_mutex;
    QMap<QString, QString> modes, mode_roles;
    QMultiMap<QString, QString> related_skills;
    mutable QMap<QString, const CardPattern *> patterns;
    mutable QMap<QString, ExpPattern *> exp_patterns;
    QHash<QString, QList<int> > audio_type;

    QList<Card *> cards;
    QStringList ban_package;
    QHash<QString, Scenario *> m_scenarios, m_miniScenes;
    Scenario *m_customScene;

    lua_State *lua;
    mutable SafeLuaMutex lua_mutex;

    //QHash<QString,const Card *> luaBasicCards, luaTrickCards;
    //QHash<QString,const Card *> luaWeapons, luaArmors ,luaTreasures;
    //QHash<QString,const Card *> LuaOffensiveHorses, LuaDefensiveHorses;
    QHash<QString,const Card *> name2cards;

    QMultiMap<QString, QString> sp_convert_pairs;
    QStringList extra_hidden_generals, removed_hidden_generals;
    QStringList extra_default_lords, removed_default_lords;
    QStringList lord_list, ZhinangCards;

#ifdef LOGNETWORK
signals:
	void logNetworkMessage(QString);
public slots:
	void handleNetworkMessage(QString);
private:
	QFile logFile;
#endif // LOGNETWORK

};

static inline QVariant GetConfigFromLuaState(lua_State *L, const char *key)
{
    return GetValueFromLuaState(L, "config", key);
}

extern Engine *Sanguosha;

// RAII helper: locks the Lua SafeLuaMutex for the current scope.
class LuaLocker {
public:
    LuaLocker() { Sanguosha->getLuaMutex().lock(); }
    ~LuaLocker() { Sanguosha->getLuaMutex().unlock(); }
    LuaLocker(const LuaLocker &) = delete;
    LuaLocker &operator=(const LuaLocker &) = delete;
};

// RAII helper: temporarily yields ALL recursive lua_mutex locks during blocking waits,
// so the UI thread can freely query card/skill state (isAvailable, targetFilter, etc.).
// The destructor restores all locks before the server thread resumes Lua execution.
// If the current thread does NOT hold the mutex, this is a safe no-op.
class LuaUnlocker {
public:
    LuaUnlocker() : m_savedCount(Sanguosha->getLuaMutex().yield()) {}
    ~LuaUnlocker() { Sanguosha->getLuaMutex().restore(m_savedCount); }
    LuaUnlocker(const LuaUnlocker &) = delete;
    LuaUnlocker &operator=(const LuaUnlocker &) = delete;
private:
    int m_savedCount;
};

#endif