#ifndef _PLAYER_H
#define _PLAYER_H

#include "general.h"
#include "card.h"
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

    int getMaxCards() const;

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

    virtual int aliveCount() const = 0;
    void setFixedDistance(const Player *player, int distance);
    void removeFixedDistance(const Player *player, int distance);
    void insertAttackRangePair(const Player *player);
    void removeAttackRangePair(const Player *player);
    int distanceTo(const Player *other, int distance_fix = 0) const;
    const General *getAvatarGeneral() const;
    const General *getGeneral() const;

    bool isLord() const;

    void acquireSkill(const QString &skill_name);
    void detachSkill(const QString &skill_name);
    void detachAllSkills();
    virtual void addSkill(const QString &skill_name);
    virtual void loseSkill(const QString &skill_name);
    bool hasSkill(const QString &skill_name, bool include_lose = false) const;
    bool hasSkill(const Skill *skill, bool include_lose = false) const;
    bool hasSkills(const QString &skill_name, bool include_lose = false) const;
    bool hasInnateSkill(const QString &skill_name) const;
    bool hasInnateSkill(const Skill *skill) const;
    bool hasLordSkill(const QString &skill_name, bool include_lose = false) const;
    bool hasLordSkill(const Skill *skill, bool include_lose = false) const;
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
    bool hasWeapon(const QString &weapon_name, bool need_area = true) const;
    bool hasArmorEffect(const QString &armor_name, bool need_area = true) const;
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
    virtual QList<int> getHandPile() const;

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
    QString getSkillDescription() const;

    virtual bool isProhibited(const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;
    virtual bool isPindianProhibited(const Player *to) const;
    bool canSlashWithoutCrossbow(const Card *slash = nullptr) const;
    virtual bool isLastHandCard(const Card *card, bool contain = false) const;

    bool hasEquipArea(int i) const;
    bool hasEquipArea() const;
    void setEquipArea(int i, bool flag);
    int getEquipArea(int i = -1);
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
    void addEquipsNullified(const QString &pattern, bool single_turn = true);
    void removeEquipsNullified(const QString &pattern, bool single_turn = true);
    bool isEquipsNullified(const Card *card) const;
    bool hasTurn() const;

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

    void setCardLimitation(const QString &limit_list, const QString &pattern, bool single_turn = false);
    void removeCardLimitation(const QString &limit_list, const QString &pattern);
    void clearCardLimitation(bool single_turn = false);
    bool isCardLimited(const Card *card, Card::HandlingMethod method, bool isHandcard = false) const;
    QString getLogName() const;
    void sortHandCards(const QString &hands);
    void sortHandCards(QList<int>hands);

    // just for convenience
    void addQinggangTag(const Card *card);
    void removeQinggangTag(const Card *card);

    void copyFrom(Player *p);

    QList<const Player *> getSiblings(bool include_self = false) const;
    QList<const Player *> getAliveSiblings(bool include_self = false) const;
    void setSkillDescriptionSwap(const QString &skill_name, const QString &key, const QString &value);
    QHash<QString, QString> getSkillDescriptionSwap(const QString &skill_name) const;

    void setTag(const QString &key, const QVariant &value);
    QVariant getTag(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void removeTag(const QString &key);

    bool setProperty(const char* name, const QVariant& value);

    static bool isNostalGeneral(const Player *p, const QString &general_name);
    
    bool hasLordSkillKingdom(const QString &kingdom, const Player *player = nullptr) const;


protected:
    QMap<QString, int> marks;
    QMap<QString, QList<int> > piles;
    QStringList skills, acquired_skills;
    mutable QMutex m_skillCacheMutex;
    mutable QMap<QString, bool> m_skillValidityCache;
    QHash<QString, int> history;
    QSet<QString> flags;
    QMap<QString, QHash<QString, QString> > description_s2k2v;
    QVariantMap tag;

private:
    QString screen_name;
    bool owner;
    const General *general, *general2;
    General::Gender m_gender;
    int hp, max_hp;
    QString kingdom, role, state;
    int seat, player_seat;
    bool alive;

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

signals:
    void general_changed();
    void general2_changed();
    void role_changed(const QString &new_role);
    void state_changed();
    void hp_changed();
    void kingdom_changed();
    void phase_changed();
    void owner_changed(bool owner);
};

#endif
