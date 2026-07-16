#ifndef _PLAYER_H
#define _PLAYER_H

#include "general.h"
#include "card.h"
#include "skill-instance-types.h"
#include <QMutex>
//#include "wrapped-card.h"

class EquipCard;
class Weapon;
class Horse;
class DelayedTrick;
class WrappedCard;

class Player : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString screenname READ screenName WRITE setScreenName)
    Q_PROPERTY(int hp READ getHp WRITE setHp)
    Q_PROPERTY(int maxhp READ getMaxHp WRITE setMaxHp)
    Q_PROPERTY(QString kingdom READ getKingdom WRITE setKingdom)
    Q_PROPERTY(QString role READ getRole WRITE setRole)
    Q_PROPERTY(QString general READ getGeneralName WRITE setGeneralName)
    Q_PROPERTY(QString general2 READ getGeneral2Name WRITE setGeneral2Name)
    Q_PROPERTY(QString state READ getState WRITE setState)
    Q_PROPERTY(int handcard_num READ getHandcardNum)
    Q_PROPERTY(int seat READ getSeat WRITE setSeat)
    Q_PROPERTY(int player_seat READ getPlayerSeat WRITE setPlayerSeat)
    Q_PROPERTY(QString phase READ getPhaseString WRITE setPhaseString)
    Q_PROPERTY(bool faceup READ faceUp WRITE setFaceUp)
    Q_PROPERTY(bool alive READ isAlive WRITE setAlive)
    Q_PROPERTY(QString flags READ getFlags WRITE setFlags)
    Q_PROPERTY(bool chained READ isChained WRITE setChained)
    Q_PROPERTY(bool owner READ isOwner WRITE setOwner)
    Q_PROPERTY(bool role_shown READ hasShownRole WRITE setShownRole)
    Q_PROPERTY(General::Gender gender READ getGender WRITE setGender)
    Q_PROPERTY(bool general_showed READ hasShownGeneral WRITE setGeneralShowed)
    Q_PROPERTY(bool general2_showed READ hasShownGeneral2 WRITE setGeneral2Showed)
    //Q_PROPERTY(QList<int> equip_area READ getEquipAreas WRITE setEquipAreas)
    Q_PROPERTY(bool weapon_area READ hasWeaponArea WRITE setWeaponArea)
    Q_PROPERTY(bool armor_area READ hasArmorArea WRITE setArmorArea)
    Q_PROPERTY(bool defensive_horse_area READ hasDefensiveHorseArea WRITE setDefensiveHorseArea)
    Q_PROPERTY(bool offensive_horse_area READ hasOffensiveHorseArea WRITE setOffensiveHorseArea)
    Q_PROPERTY(bool treasure_area READ hasTreasureArea WRITE setTreasureArea)
    Q_PROPERTY(bool hasjudgearea READ hasJudgeArea WRITE setJudgeArea)

    Q_ENUMS(Phase)
    Q_ENUMS(Place)
    Q_ENUMS(Role)

public:
    enum Phase
    {
        RoundStart, Start, Judge, Draw, Play, Discard, Finish, NotActive, PhaseNone
    };
    enum Place
    {
        PlaceHand, PlaceEquip, PlaceDelayedTrick, PlaceJudge,
        PlaceSpecial, DiscardPile, DrawPile, PlaceTable, PlaceUnknown,
        PlaceWuGu
    };
    enum Role
    {
        Lord, Loyalist, Rebel, Renegade
    };

    explicit Player(QObject *parent);

    void setScreenName(const QString &screen_name);
    QString screenName() const;

    // property setters/getters
    int getHp() const;
    void setHp(int hp);
    int getMaxHp() const;
    void setMaxHp(int max_hp);
    int getLostHp() const;
    bool isWounded() const;
    General::Gender getGender() const;
    virtual void setGender(General::Gender gender);
    bool isMale() const;
    bool isFemale() const;
    bool isNeuter() const;

    bool isOwner() const;
    void setOwner(bool owner);

    bool hasShownRole() const;
    void setShownRole(bool shown);

    virtual int getMaxCards() const;

    QString getKingdom() const;
    void setKingdom(const QString &kingdom);

    void setRole(const QString &role);
    QString getRole() const;
    Role getRoleEnum() const;

    void setGeneral(const General *general);
    void setGeneralName(const QString &general_name);
    QString getGeneralName() const;

    void setGeneral2Name(const QString &general_name);
    QString getGeneral2Name() const;
    const General *getGeneral2() const;

    void setState(const QString &state);
    QString getState() const;

    int getSeat() const;
    void setSeat(int seat);
    int getPlayerSeat() const;
    void setPlayerSeat(int player_seat);
    bool isAdjacentTo(const Player *another) const;
    QString getPhaseString() const;
    void setPhaseString(const QString &phase_str);
    Phase getPhase() const;
    void setPhase(Phase phase);

    int getAttackRange(bool include_weapon = true) const;
    bool inMyAttackRange(const Player *other, int distance_fix = 0, bool chengwu = true) const;
    bool inMyAttackRange(const Player *other, QList<int> card_ids, bool chengwu = true) const;

    bool isAlive() const;
    bool isDead() const;
    bool isRest() const;
    void setAlive(bool alive);

    QString getFlags() const;
    QStringList getFlagList() const;
    virtual void setFlags(const QString &flag);
    bool hasFlag(const QString &flag) const;
    void clearFlags();

    bool faceUp() const;
    void setFaceUp(bool face_up);

    bool isRemoved() const;
    void setRemoved(bool removed);

    virtual int aliveCount(bool includeRemoved = false) const = 0;
    void setFixedDistance(const Player *player, int distance);
    void removeFixedDistance(const Player *player, int distance);
    void insertAttackRangePair(const Player *player);
    void removeAttackRangePair(const Player *player);
    int distanceTo(const Player *other, int distance_fix = 0) const;
    const General *getAvatarGeneral() const;
    const General *getGeneral() const;

    bool isLord() const;

    int acquireSkill(const QString &skill_name, bool head = true, int instanceId = -1);
    void detachSkill(const QString &skill_name);
    void detachSkill(const QString &skill_name, bool head);
    void detachAllSkills();
    virtual void addSkill(const QString &skill_name);
    virtual void addSkill(const QString &skill_name, bool head_skill);
    virtual void loseSkill(const QString &skill_name);
    virtual void loseSkill(const QString &skill_name, bool head);
    bool hasSkill(const QString &skill_name, bool include_lose = false) const;
    bool hasSkill(const Skill *skill, bool include_lose = false) const;
    bool hasSkills(const QString &skill_name, bool include_lose = false) const;
    bool ownsSkill(const QString &skill_name) const;
    bool hasInnateSkill(const QString &skill_name) const;
    bool hasInnateSkill(const Skill *skill) const;
    bool hasLordSkill(const QString &skill_name, bool include_lose = false) const;
    bool hasLordSkill(const Skill *skill, bool include_lose = false) const;
    bool isSkillInvalid(const Skill *skill, int instanceId = 0) const;
    bool isSkillInvalid(const QString &skill_name, int instanceId = 0) const;

    // === 技能多實例權威容器 (SSOT) ===
    int createSkillInstance(const QString &skillName, SkillInstanceSource source, bool visible = true);
    int createSkillInstance(const QString &skillName, SkillInstanceSource source, const QString &parentSkillName, int parentInstanceID, bool visible = true);
    int createSkillInstance(const QString &skillName, SkillInstanceSource source, const SkillInstanceRef &parentRef, bool visible = true);
    bool removeSkillInstance(const QString &skillName, int instanceID);
    bool hasSkillInstance(const QString &skillName, int instanceID) const;
    const SkillInstance *findSkillInstance(const QString &skillName, int instanceID) const;
    QList<SkillInstanceKey> getChildSkillInstanceKeys(const SkillInstanceKey &parent) const;
    QList<SkillInstance> getSkillInstances() const;
    void clearSkillInstances();
    void upsertSkillInstance(const SkillInstance &instance);
    QList<int> getSkillInstanceIdsForName(const QString &skillName) const;
    void setSkillInstanceState(const QString &skillName, int instanceID, const QVariantMap &state);
    QVariantMap getSkillInstanceState(const QString &skillName, int instanceID) const;
    void removeSkillInstanceState(const QString &skillName, int instanceID);
    void setSkillInstanceStateValue(const QString &skillName, int instanceID, const QString &key, const QVariant &value);
    QVariant getSkillInstanceStateValue(const QString &skillName, int instanceID, const QString &key, const QVariant &defaultValue = QVariant()) const;
    void removeSkillInstanceStateValue(const QString &skillName, int instanceID, const QString &key);
    virtual QString getGameMode() const = 0;
    bool isClientPlayer() const;

    void setEquip(const Card *equip);
    void removeEquip(const Card *equip);
    bool hasEquip(const Card *card) const;
    bool hasEquip() const;

    QList<const Card *> getJudgingArea() const;
    QList<int> getJudgingAreaID() const;
    void addDelayedTrick(const Card *trick);
    void removeDelayedTrick(const Card *trick);
    bool containsTrick(const QString &trick_name) const;

    virtual int getHandcardNum() const;
    virtual void removeCard(int id, Place place);
    virtual void addCard(int id, Place place);
    virtual QList<const Card *> getHandcards() const;
    QList<int> handCards() const;
    int getRandomHandCardId() const;
    const Card *getRandomHandCard() const;
    void drawCard(const Card *card);

    const EquipCard *getWeapon() const;
    const EquipCard *getArmor() const;
    const EquipCard *getDefensiveHorse() const;
    const EquipCard *getOffensiveHorse() const;
    const EquipCard *getTreasure() const;

    QList<const EquipCard *> getWeapons() const;
    QList<const EquipCard *> getArmors() const;
    QList<const EquipCard *> getDefensiveHorses() const;
    QList<const EquipCard *> getOffensiveHorses() const;
    QList<const EquipCard *> getTreasures() const;
    bool hasWeapons() const;
    bool hasArmors() const;
    bool hasDefensiveHorses() const;
    bool hasOffensiveHorses() const;
    bool hasTreasures() const;
    int getWeaponsCount() const;
    int getArmorsCount() const;
    int getDefensiveHorsesCount() const;
    int getOffensiveHorsesCount() const;
    int getTreasuresCount() const;
    QList<int> getEquipRealSlots(int card_id) const;

    QList<const Card *> getEquips(int index = -1) const;
    QList<int> getEquipsId() const;
    const EquipCard *getEquip(int index) const;

    bool viewAsEquip(const QString &equip_name) const;
    bool hasWeapon(const QString &weapon_name, const Player *sourcePlayer = nullptr, bool need_area = true) const;
    bool hasArmorEffect(const QString &armor_name, const Player *sourcePlayer = nullptr, bool need_area = true) const;
    bool hasDefensiveHorse(const QString &horse_name, bool need_area = true) const;
    bool hasOffensiveHorse(const QString &horse_name, bool need_area = true) const;
    bool hasTreasure(const QString &treasure_name, bool need_area = true) const;

    bool isKongcheng() const;
    bool isNude() const;
    bool isAllNude() const;

    bool canDiscard(const Player *to, const QString &flags) const;
    bool canDiscard(const Player *to, int card_id) const;
    bool canDiscard(const QString &flags, const Player *to = nullptr) const;
    bool canDiscard(int card_id, const Player *to = nullptr) const;

    void addMark(const QString &mark, int add_num = 1);
    void removeMark(const QString &mark, int remove_num = 1);
    virtual void setMark(const QString &mark, int value);
    int getMark(const QString &mark) const;
    int getHujia() const;
    QStringList getMarkNames() const;

    bool hasClub(const QString &club_name) const;
    bool hasClub() const;
    QString getClubName() const;

    void setChained(bool chained);
    bool isChained() const;

    bool canSlash(const Player *other, const Card *slash, bool distance_limit = true, int rangefix = 0, const QList<const Player *> &others = QList<const Player *>()) const;
    bool canSlash(const Player *other, bool distance_limit = true, int rangefix = 0, const QList<const Player *> &others = QList<const Player *>()) const;
    int getCardCount(bool include_equip = true, bool include_judging = false) const;

    QList<int> getPile(const QString &pile_name) const;
    QStringList getPileNames() const;
    QString getPileName(int card_id) const;
    bool pileOpen(const QString &pile_name, const QString &player) const;
    void setPileOpen(const QString &pile_name, const QString &player);
    void removePileOpen(const QString &pile_name, const QString &player);
    virtual QList<int> getHandPile() const;

    QStringList getGeneralPile(const QString &pile_name) const;
    QStringList getGeneralPileNames() const;
    QString getGeneralPileName(const QString &general_name) const;
    bool generalPileOpen(const QString &pile_name, const QString &player) const;
    void setGeneralPileOpen(const QString &pile_name, const QString &player);

    void addHistory(const QString &name, int times = 1);
    void clearHistory(const QString &name = "");
    bool hasUsed(const QString &card_class, bool actual = false) const;
    int usedTimes(const QString &card_class, bool actual = false) const;
    int getSlashCount() const;

    bool hasEquipSkill(const QString &skill_name) const;
    QSet<const TriggerSkill *> getTriggerSkills() const;
    QSet<const Skill *> getSkills(bool include_equip = false, bool visible_only = true) const;
    QList<const Skill *> getSkillList(bool include_equip = false, bool visible_only = true) const;
    QSet<const Skill *> getVisibleSkills(bool include_equip = false) const;
    QList<const Skill *> getVisibleSkillList(bool include_equip = false) const;
    QStringList getAcquiredSkills() const;
    QStringList getSkillNames() const;
    bool hasAcquiredSkill(const QString &skill_name) const;
    int getSkillInstanceId(const QString &skill_name) const;
    QList<int> getSkillInstanceIds(const QString &skill_name) const;
    // 有效（未被 SkillInvalidityRecords 封禁）的同名技能實例 ID 清單；所有真實實例 ID 均為正整數。
    QList<int> getValidSkillInstanceIds(const QString &skill_name) const;
    QString getSkillDescription() const;

    // === 技能後置數值覆寫 (Skill Amount Override) ===
    // 遊戲中可後置改動單一技能實例每實例貢獻的數值（modified_amount）。
    // instanceId = 0：全體覆寫（套用至所有同名實例，含 innate）
    // instanceId = N：僅套用至 #N 實例
    // 結算優先序：單實例覆寫 > 全體覆寫 > 技能原生回傳值
    bool hasSkillAmountOverride(const QString &skill_name, int instanceId = 0) const;
    int getSkillAmountOverride(const QString &skill_name, int instanceId = 0) const;
    void setSkillAmountOverride(const QString &skill_name, int amount, int instanceId = 0);
    void removeSkillAmountOverride(const QString &skill_name, int instanceId = 0);
    void clearSkillAmountOverrides();

    virtual bool isProhibited(const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;
    virtual bool isPindianProhibited(const Player *to) const;
    bool canSlashWithoutCrossbow(const Card *slash = nullptr) const;
    virtual bool isLastHandCard(const Card *card, bool contain = false) const;

    bool hasEquipArea(int i) const;
    bool hasEquipArea() const;
    void setEquipArea(int i, bool flag);
    void setEquipAreaCount(int i, int count);
    int getEquipArea(int i = -1) const;
    void addEquipArea(int i);
    bool hasWeaponArea() const;
    bool hasArmorArea() const;
    bool hasDefensiveHorseArea() const;
    bool hasOffensiveHorseArea() const;
    bool hasTreasureArea() const;
    void setWeaponArea(bool flag);
    void setArmorArea(bool flag);
    void setDefensiveHorseArea(bool flag);
    void setOffensiveHorseArea(bool flag);
    void setTreasureArea(bool flag);
    bool hasJudgeArea() const;
    void setJudgeArea(bool flag);
    bool canPindian(const Player *target = nullptr, bool except_self = true) const;

    QHash<QString, int> getHistory() const { return history; }
    //bool canPindian(bool except_self = true) const;
    bool canBePindianed(bool except_self = true) const;
    bool isYourFriend(const Player *fri) const;
    int getChangeSkillState(const QString &skill_name) const;
    bool hasCard(const Card *card) const;
    bool hasCard(int id) const;
    QList<int> getdrawPile() const;
    QList<int> getdiscardPile() const;
    QString getDeathReason() const;
    bool isJieGeneral() const;
    bool isJieGeneral(const QString &name, const QString &except_name = "") const;
    bool hasHideSkill(int general = 1) const;
    bool inYinniState() const;
    bool canSeeHandcard(const Player *player) const;
    void addEquipsNullified(const QString &pattern, const QString &reason = "", bool single_turn = true);
    void removeEquipsNullified(const QString &pattern, const QString &reason = "", bool single_turn = true);
    bool isEquipsNullified(const Card *card, const Player *sourcePlayer = nullptr) const;
    bool hasTurn() const;

    QList<int> getShownHandcards() const;
    void setShownHandcards(QList<int> &ids);
    bool isShownHandcard(int id) const;
    QList<int> getBrokenEquips() const;
    void setBrokenEquips(QList<int> &ids);
    bool isBrokenEquip(int id) const;
    bool isAbnormal() const;

    inline bool isJilei(const Card *card, bool isHandcard = false) const
    {
        return isCardLimited(card, Card::MethodDiscard, isHandcard);
    }/*
    inline bool isLocked(const Card *card, bool isHandcard = false) const
    {
		return isCardLimited(card, Card::MethodUse, isHandcard);
    }*/
    bool isLocked(const Card *card, bool isHandcard = false) const;

    bool isWeidi() const
    {
		return role!="lord"&&hasSkill("weidi");
    }

    void setCardLimitation(const QString &limit_list, const QString &pattern, const QString &reason = "", bool single_turn = false);
    void removeCardLimitation(const QString &limit_list, const QString &pattern, const QString &reason = "");
    void removeCardLimitationByReason(const QString &reason);
    void clearCardLimitation(bool single_turn = false);
    bool isCardLimited(const Card *card, Card::HandlingMethod method, bool isHandcard = false) const;
    QStringList getCardLimitationReasons(Card::HandlingMethod method) const;

    bool canMove(const Player *to, const QString &flags) const;
    bool canMove(const Player *to, int card_id) const;
    bool canMove(const QString &flags, const Player *to = nullptr) const;
    bool canMove(int card_id, const Player *to = nullptr) const;
    QString getLogName() const;
    void sortHandCards(const QString &hands);
    void sortHandCards(QList<int>hands);

    // just for convenience
    void addQinggangTag(const Card *card);
    void removeQinggangTag(const Card *card);

    void copyFrom(Player *p);

    QList<const Player *> getSiblings(bool include_self = false) const;
    QList<const Player *> getAliveSiblings(bool include_self = false) const;
    void setSkillDescriptionSwap(const QString &skill_name, const QString &key, const QString &value, int instanceId = 0);
    QHash<QString, QString> getSkillDescriptionSwap(const QString &skill_name, int instanceId = 0) const;
    const QMap<QString, QHash<QString, QString> > &getAllSkillDescriptionSwaps() const;

    void setCardDescriptionSwap(const QString &card_name, const QString &key, const QString &value);
    QHash<QString, QString> getCardDescriptionSwap(const QString &card_name) const;
    const QMap<QString, QHash<QString, QString> > &getAllCardDescriptionSwaps() const;

    void setTag(const QString &key, const QVariant &value);
    QVariant getTag(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void removeTag(const QString &key);

    bool setProperty(const char* name, const QVariant& value);

    static bool isNostalGeneral(const Player *p, const QString &general_name);
    
    bool hasLordSkillKingdom(const QString &kingdom, const Player *player = nullptr) const;

    bool isFriendWith(const Player *player, bool considerAnjiang = false) const;
    bool willBeFriendWith(const Player *player) const;
    QList<const Player *> getFormation() const;
    bool hasShownOneGeneral() const;
    bool hasShownGeneral() const;
    bool hasShownGeneral2() const;
    void setGeneralShowed(bool showed);
    void setGeneral2Showed(bool showed);
    bool canShowGeneral(const QString &position) const;
    bool inHeadSkills(const QString &skill_name) const;
    bool inDeputySkills(const QString &skill_name) const;
    void setSkillPreshowed(const QString &skill, bool preshowed = true);
    void setSkillsPreshowed(const QString &flag = "hd", bool preshowed = true);
    bool hasPreshowedSkill(const QString &name) const;
    bool hasPreshowedSkill(const Skill *skill) const;
    bool hasShownSkill(const QString &skill_name) const;
    bool hasShownSkill(const Skill *skill) const;
    bool isHidden(bool head_general) const;
    virtual Player *getNextAlive(int n = 1) const = 0;
    virtual Player *getLastAlive(int n = 1) const = 0;


protected:
    QMap<QString, int> marks;
    QMap<QString, QList<int> > piles;
    QMap<QString, QStringList> general_piles;
    QMap<QString, QStringList> general_pile_open;
    QStringList skills, acquired_skills;
    QMap<QString, bool> head_skills;
    QMap<QString, bool> deputy_skills;
    QSet<QString> head_acquired_skills, deputy_acquired_skills;
    bool general_showed;
    bool general2_showed;
    mutable QMutex m_skillCacheMutex;
    mutable QMap<QString, bool> m_skillValidityCache;

    // 技能多實例權威容器（唯一真實來源）
    // m_skillInstances[skillName][instanceID] = SkillInstance
    QMap<QString, QMap<int, SkillInstance>> m_skillInstances;
    // 每個技能名的 next-ID 計數器（單調遞增，永不重用）
    QMap<QString, int> m_nextSkillInstanceIds;
    QHash<QString, int> history;
    QSet<QString> flags;
    QMap<QString, QHash<QString, QString> > description_s2k2v;
    QMap<QString, QHash<QString, QString> > card_description_swaps;
    // key: "skillName" (全體覆寫) 或 "skillName#N" (單實例覆寫)
    // value: 該實例每實例貢獻的 modified_amount
    QMap<QString, int> m_skillAmountOverride;
    QVariantMap tag;
    QList<int> shown_handcards;
    QList<int> broken_equips;

private:
    QString screen_name;
    bool owner;
    const General *general, *general2;
    General::Gender m_gender;
    int hp, max_hp;
    QString kingdom, role, state;
    int seat, player_seat;
    bool alive;
    bool removed;

    Phase phase;
    QList<int> equip_area;
    QList<const EquipCard *> equips;
    //WrappedCard *weapon, *armor, *defensive_horse, *offensive_horse, *treasure;
    bool face_up;
    bool chained;
    bool weapon_area;
    bool armor_area;
    bool defensive_horse_area;
    bool offensive_horse_area;
    bool treasure_area;
    bool hasjudgearea;
    bool role_shown;
    QMap<QString, QStringList> pile_open;
    QList<const Card *> handcards, judging_area;

    QMultiHash<const Player *, int> fixed_distance;
    QList<const Player *> attack_range_pair;

    QMap<Card::HandlingMethod, QStringList> card_limitation;
    QMap<Card::HandlingMethod, QMap<QString, QString>> card_limitation_reasons;

signals:
    void general_changed();
    void general2_changed();
    void role_changed(const QString &new_role);
    void state_changed();
    void hp_changed();
    void kingdom_changed();
    void phase_changed();
    void owner_changed(bool owner);
    void showncards_changed();
    void brokenEquips_changed();
};

#endif
