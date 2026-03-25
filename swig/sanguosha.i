%module sgs

%{

//#include "structs.h"
#include "engine.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QMutexLocker>

//#include <QDir>

%}

%include "naturalvar.i"
%include "native.i"
%include "qvariant.i"
%include "list.i"

// ----------------------------------------

class QObject {
public:
	QString objectName() const;
	void setObjectName(const char*name);
	bool inherits(const char*class_name);
	bool setProperty(const char*name, const QVariant&value);
	QVariant property(const char*name) const;
	void setParent(QObject*parent);
	void deleteLater();
};

class General: public QObject {
public:
	explicit General(Package*package, const char*name, const char*kingdom,
		int max_hp = 4, bool male = true, bool hidden = false, bool never_shown = false, int start_hp = pow(2, 31) - 1, int start_hujia = 0);

	// property getters/setters
	int getMaxHp() const;
	QString getKingdom() const;
	QString getKingdoms() const;
	bool isMale() const;
	bool isFemale() const;
	bool isNeuter() const;
	bool isSexless() const;
	bool isLord() const;
	bool isHidden() const;
	bool isTotallyHidden() const;
	void setStartHp(int hp);
	int getStartHp() const;
	void setStartHujia(int hujia);
	int getStartHujia() const;

	enum Gender { Sexless, Male, Female, Neuter };
	Gender getGender() const;
	void setGender(Gender gender);

	void addSkill(Skill*skill);
	void addSkill(const char*skill_name);
	bool hasSkill(const char*skill_name, bool related = false) const;
	bool hasHideSkill() const;
	QList<const Skill*> getSkillList() const;
	QList<const Skill*> getVisibleSkillList() const;
	QSet<const Skill*> getVisibleSkills() const;
	QSet<const TriggerSkill*> getTriggerSkills() const;

	void addRelateSkill(const char*skill_name);
	QStringList getRelatedSkillNames() const;

	QString getPackage() const;
	QString getSkillDescription(bool include_name = false) const;
	QString getBriefName() const;

	void setAudioType(const char*filename, const char*types);

	void lastWord() const;
};

class Player: public QObject {
public:
	enum Phase { RoundStart, Start, Judge, Draw, Play, Discard, Finish, NotActive, PhaseNone };
	enum Place { PlaceHand, PlaceEquip, PlaceDelayedTrick, PlaceJudge, PlaceSpecial, DiscardPile, DrawPile, PlaceTable, PlaceUnknown, PlaceWuGu };
	enum Role { Lord, Loyalist, Rebel, Renegade };

	explicit Player(QObject*parent);

	void setScreenName(const char*screen_name);
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

	bool hasShownRole() const;
	void setShownRole(bool shown);

	int getMaxCards() const;

	QString getKingdom() const;
	void setKingdom(const char*kingdom);

	void setRole(const char*role);
	QString getRole() const;
	Role getRoleEnum() const;

	void setGeneral(const General*general);
	void setGeneralName(const char*general_name);
	QString getGeneralName() const;

	void setGeneral2Name(const char*general_name);
	QString getGeneral2Name() const;
	const General*getGeneral2() const;

	void setState(const char*state);
	QString getState() const;

	int getSeat() const;
	void setSeat(int seat);
	int getPlayerSeat() const;
	void setPlayerSeat(int player_seat);
	bool isAdjacentTo(const Player*another) const;
	QString getPhaseString() const;
	void setPhaseString(const char*phase_str);
	Phase getPhase() const;
	void setPhase(Phase phase);

	int getAttackRange(bool include_weapon = true) const;
	bool inMyAttackRange(const Player*other, int distance_fix = 0, bool chengwu = true) const;
	bool inMyAttackRange(const Player*other, QList<int> card_ids, bool chengwu = true) const;

	bool isAlive() const;
	bool isDead() const;
	bool isRest() const;
	void setAlive(bool alive);


	QString getFlags() const;
	QStringList getFlagList() const;
	void setFlags(const char*flag);
	bool hasFlag(const char*flag) const;
	void clearFlags();

	bool faceUp() const;
	void setFaceUp(bool face_up);

	virtual int aliveCount() const = 0;
	int distanceTo(const Player*other, int distance_fix = 0) const;
	void setFixedDistance(const Player*player, int distance);
	void removeFixedDistance(const Player*player, int distance);
	void insertAttackRangePair(const Player*player);
	void removeAttackRangePair(const Player*player);
	const General*getAvatarGeneral() const;
	const General*getGeneral() const;

	bool isLord() const;
	void acquireSkill(const char*skill_name);
	void detachSkill(const char*skill_name);
	void detachAllSkills();
	virtual void addSkill(const char*skill_name);
	virtual void loseSkill(const char*skill_name);
	bool hasSkill(const char*skill_name, bool include_lose = false) const;
	bool hasSkill(const Skill*skill, bool include_lose = false) const;
	bool hasSkills(const char*skill_name, bool include_lose = false) const;
	bool hasLordSkill(const char*skill_name, bool include_lose = false) const;
	bool hasLordSkill(const Skill*skill, bool include_lose = false) const;
	bool hasInnateSkill(const char*skill_name) const;
	bool hasInnateSkill(const Skill*skill) const;
	virtual QString getGameMode() const = 0;

	void setEquip(Card*equip);
	void removeEquip(Card*equip);
	bool hasEquip(const Card*card) const;
	bool hasEquip() const;

	QList<const Card*> getJudgingArea() const;
	QList<int> getJudgingAreaID() const;
	void addDelayedTrick(const Card*trick);
	void removeDelayedTrick(const Card*trick);
	bool containsTrick(const char*trick_name) const;

	virtual int getHandcardNum() const;
	virtual void removeCard(int id, Place place);
	virtual void addCard(int id, Place place);
	virtual QList<const Card*> getHandcards() const;
	QList<int> handCards() const;

	WrappedCard*getWeapon() const;
	WrappedCard*getArmor() const;
	WrappedCard*getDefensiveHorse() const;
	WrappedCard*getOffensiveHorse() const;
	WrappedCard*getTreasure() const;
	QList<const Card*> getEquips() const;
	QList<int> getEquipsId() const;
	const EquipCard*getEquip(int index) const;

	bool viewAsEquip(const char*equip_name) const;
	bool hasWeapon(const char*weapon_name, bool need_area = true) const;
	bool hasArmorEffect(const char*armor_name, bool need_area = true) const;
	bool hasDefensiveHorse(const char*horse_name, bool need_area = true) const;
	bool hasOffensiveHorse(const char*horse_name, bool need_area = true) const;
	bool hasTreasure(const char*treasure_name, bool need_area = true) const;

	bool isKongcheng() const;
	bool isNude() const;
	bool isAllNude() const;

	bool canDiscard(const Player*to, const char*flags) const;
	bool canDiscard(const Player*to, int card_id) const;
    bool canDiscard(const char*flags, const Player*to = nullptr) const;
    bool canDiscard(int card_id, const Player*to = nullptr) const;

	void addMark(const char*mark, int add_num = 1);
	void removeMark(const char*mark, int remove_num = 1);
	virtual void setMark(const char*mark, int value);
	int getMark(const char*mark) const;
	int getHujia() const;
	QStringList getMarkNames() const;

	void setChained(bool chained);
	bool isChained() const;

	bool canSlash(const Player*other, const Card*slash, bool distance_limit = true,
				int rangefix = 0, const QList<const Player*>&others = QList<const Player*>()) const;
	bool canSlash(const Player*other, bool distance_limit = true,
				int rangefix = 0, const QList<const Player*>&others = QList<const Player*>()) const;
	int getCardCount(bool include_equip = true, bool include_judging = false) const;

	QList<int> getPile(const char*pile_name);
	QStringList getPileNames() const;
	QString getPileName(int card_id) const;
	bool pileOpen(const char*pile_name, const char*player) const;
	void setPileOpen(const char*pile_name, const char*player);

	QList<int> getHandPile() const;

	void addHistory(const char*name, int times = 1);
	void clearHistory(const char*name = "");
	bool hasUsed(const char*card_class, bool actual = false) const;
	int usedTimes(const char*card_class, bool actual = false) const;
	int getSlashCount() const;

	bool hasEquipSkill(const char*skill_name) const;
	QSet<const TriggerSkill*> getTriggerSkills() const;
	QSet<const Skill*> getSkills(bool include_equip = false, bool visible_only = true) const;
	QList<const Skill*> getSkillList(bool include_equip = false, bool visible_only = true) const;
	QSet<const Skill*> getVisibleSkills(bool include_equip = false) const;
	QList<const Skill*> getVisibleSkillList(bool include_equip = false) const;
	QStringList getAcquiredSkills() const;
	QString getSkillDescription() const;

	virtual bool isProhibited(const Player*to, const Card*card, const QList<const Player*>&others = QList<const Player*>()) const;
	virtual bool isPindianProhibited(const Player*to) const;
	bool canSlashWithoutCrossbow() const;
	virtual bool isLastHandCard(const Card*card, bool contain = false) const;

	bool hasEquipArea(int i) const;
	bool hasEquipArea() const;
	void setEquipArea(int i, bool flag);
	void addEquipArea(int i);
	int getEquipArea(int i = -1);
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
	bool canPindian(const Player*target = nullptr, bool except_self = true) const;
	/*bool canPindian(bool except_self = true) const;*/
	bool canBePindianed(bool except_self = true) const;
	bool isYourFriend(const Player*fri) const;
	bool isWeidi() const;
	int getChangeSkillState(const char*skill_name) const;
	bool hasCard(const Card*card) const;
	bool hasCard(int id) const;
	QList<int> getdrawPile() const;
	QList<int> getdiscardPile() const;
	QString getDeathReason() const;
	bool isJieGeneral() const;
	bool isJieGeneral(const char*name, const char*except_name = nullptr) const;
	bool hasHideSkill(int general = 1) const;
	bool inYinniState() const;
	bool canSeeHandcard(const Player*player) const;
	void addEquipsNullified(char* pattern, bool single_turn = true);
	void removeEquipsNullified(char* pattern, bool single_turn = true);
	bool isEquipsNullified(const Card*card) const;
    bool hasTurn() const;

	bool isJilei(const Card*card, bool isHandcard = false) const;
	bool isLocked(const Card*card, bool isHandcard = false) const;

	void setCardLimitation(const char*limit_list, const char*pattern, bool single_turn = false);
	void removeCardLimitation(const char*limit_list, const char*pattern);
	void clearCardLimitation(bool single_turn = false);
	bool isCardLimited(const Card*card, Card::HandlingMethod method, bool isHandcard = false) const;
	QString getLogName() const;
	void sortHandCards(const char*hands);
	void sortHandCards(QList<int>hands);

	// just for convenience
	void addQinggangTag(const Card*card);
	void removeQinggangTag(const Card*card);

	void copyFrom(Player*p);

	QList<const Player*> getSiblings(bool include_self = false) const;
	QList<const Player*> getAliveSiblings(bool include_self = false) const;
	void setSkillDescriptionSwap(const char*skill_name, const char*key, const char*value);
	QHash<QString, QString> getSkillDescriptionSwap(const char*skill_name) const;

	static bool isNostalGeneral(const Player*p, const char*general_name);
	bool hasLordSkillKingdom(const char*kingdom, const Player*player = nullptr) const;

	void drawCard(const Card*card);
	int getRandomHandCardId() const;
	const Card*getRandomHandCard() const;
};

%extend Player {
	void setTag(const char*key, QVariant&value) {
		$self->tag[key] = value;
	}

	QVariant getTag(const char*key) {
		return $self->tag[key];
	}

	void removeTag(const char*tag_name) {
		$self->tag.remove(tag_name);
	}
};

class ServerPlayer: public Player {
public:
	ServerPlayer(Room*room);

	void setSocket(ClientSocket*socket);
	QString reportHeader() const;
	Room*getRoom() const;
	void setOnsoleOwner(ServerPlayer*onsole_owner);
	ServerPlayer*getOnsoleOwner() const;
	void broadcastSkillInvoke(const Card*card) const;
	void broadcastSkillInvoke(const char*card_name) const;
	void peiyin(const Skill*skill, int type = -1);
	void peiyin(const char*skillName, int type = -1);
	void obtainCard(const Card*card, bool visible = true);
	void throwAllEquips(const char*reason = "");
	void throwAllHandCards(const char*reason = "");
	void throwAllHandCardsAndEquips(const char*reason = "");
	void throwAllCards(const char*reason = "");
	void bury();
	void throwAllMarks(bool visible_only = true);
	void clearOnePrivatePile(const char*pile_name);
	void clearPrivatePiles();
	QList<int> drawCardsList(int n, const char*reason = nullptr, bool isTop = true, bool visible = false);
	void drawCards(int n, const char*reason = nullptr, bool isTop = true, bool visible = false);
	bool askForSkillInvoke(const char*skill_name, const QVariant&data = QVariant(), bool notify = true);
	bool askForSkillInvoke(const Skill*skill, const QVariant&data = QVariant(), bool notify = true);
	bool askForSkillInvoke(const char*skill_name, ServerPlayer*player, bool notify = true);
	bool askForSkillInvoke(const Skill*skill, ServerPlayer*player, bool notify = true);
	
	QList<int> forceToDiscard(int discard_num, bool include_equip, bool is_discard = true, const char*pattern = ".");
	QList<const Card*> getCards(const char*flags) const;
	DummyCard*wholeHandCards() const;
	bool hasNullification() const;
	bool pindian(ServerPlayer*target, const char*reason, const Card*card1 = nullptr);
	int pindianInt(ServerPlayer*target, const char*reason, const Card*card1 = nullptr);
	PindianStruct*PinDian(ServerPlayer*target, const char*reason, const Card*card1 = nullptr);
	void turnOver();
	void play(QList<Player::Phase> set_phases = QList<Player::Phase>());
	bool changePhase(Player::Phase from, Player::Phase to);

	QList<Player::Phase>&getPhases();
	void skip(Player::Phase phase, bool isCost = false);
	void insertPhase(Player::Phase phase);
	bool isSkipped(Player::Phase phase);

	void gainMark(const char*mark, int n = 1);
	void loseMark(const char*mark, int n = 1);
	void loseAllMarks(const char*mark_name);
	
	void gainHujia(int n = 1, int max_num = 5);
	void loseHujia(int n = 1);
	void loseAllHujias();

	void setAI(AI*ai);
	AI*getAI() const;
	AI*getSmartAI() const;

	virtual int aliveCount() const;
	virtual void removeCard(int id, Place place);
	virtual void addCard(int id, Place place);

	void addVictim(ServerPlayer*victim);
	QList<ServerPlayer*> getVictims() const;

	void startRecord();
	void saveRecord(const char*filename);

	void setNext(ServerPlayer*next);
	ServerPlayer*getNext() const;
	ServerPlayer*getNextAlive(int n = 1) const;

	// 3v3 methods
	void addToSelected(const char*general);
	QStringList getSelected() const;
	QString findReasonable(QStringList generals, bool no_unreasonable = false);
	void clearSelected();

	int getGeneralMaxHp() const;
	int getGeneralStartHp() const;
	int getGeneralStartHujia() const;
	virtual QString getGameMode() const;

	QString getIp() const;
	void introduceTo(ServerPlayer*player);
	void marshal(ServerPlayer*player) const;

	void addToPile(const char*pile_name, const Card*card, bool open = true, QList<ServerPlayer*> open_players = QList<ServerPlayer*>());
	void addToPile(const char*pile_name, int card_id, bool open = true, QList<ServerPlayer*> open_players = QList<ServerPlayer*>());
	void addToPile(const char*pile_name, QList<int> card_ids, bool open = true, QList<ServerPlayer*> open_players = QList<ServerPlayer*>());
	void addToPile(const char*pile_name, QList<int> card_ids, bool open, QList<ServerPlayer*> open_players, CardMoveReason reason);
	void addToRenPile(const Card*card, const char*skill_name = "");
	void addToRenPile(int card_id, const char*skill_name = "");
	void addToRenPile(QList<int> card_ids, const char*skill_name = "");
	void exchangeFreelyFromPrivatePile(const char*skill_name, const char*pile_name, int upperlimit = 1000, bool include_equip = false);
	void gainAnExtraTurn(QList<Player::Phase> phases = QList<Player::Phase>());

	void throwEquipArea(int i);
	void throwEquipArea(QList<int> list);
	void throwEquipArea();
	void addEquipArea(int i);
	void obtainEquipArea(QList<int> list);
	void obtainEquipArea(int i);
	void obtainEquipArea();
	void throwJudgeArea();
	void obtainJudgeArea();
	ServerPlayer*getSaver() const;
	bool isLowestHpPlayer(bool only = false);
	void ViewAsEquip(const char*equip_name, bool can_duplication = false);
	void removeViewAsEquip(const char*equip_name, bool all_duplication = true);
	bool canUse(const Card*card, QList<ServerPlayer*> players = QList<ServerPlayer*>(), bool player_must_be_target = false);
	bool canUse(const Card*card, ServerPlayer*player, bool player_must_be_target = false);
	void endPlayPhase(bool sendLog = true);
	void breakYinniState();
	void enterYinniState(int type = 0);
	int getDerivativeCard(const char*card_name, Player::Place place = Player::PlaceEquip, bool visible = true) const;
	void setCanWake(const char*skill_name, const char*waked_skill_name);
	bool canWake(const char*waked_skill_name);
	QList<ServerPlayer*> assignmentCards(QList<int>&cards, const char*prompt = "", QList<ServerPlayer*> players = QList<ServerPlayer*>(), int max_num = -1, int min_num = 0, bool visible = false);
	void skillInvoked(const char*skill_name, int type = -1, ServerPlayer*owner = nullptr);
	void skillInvoked(const Skill*skill, int type = -1, ServerPlayer*owner = nullptr);
	QList<ServerPlayer*> getRandomTargets(const Card*card, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	void setSkillDescriptionSwap(const char*skill_name, const char*key, const char*value);
	void setAvatarIcon(const char*avatar_name, bool small = false);
	bool damageRevises(QVariant&data, int n);

	QString getClientReplyString();
	const QVariant&getClientReply() const;
};

%extend ServerPlayer {
	void speak(const char*msg) {
		QString str = QByteArray(msg).toBase64();
		$self->getRoom()->speakCommand($self, str);
	}

	void removePileByName(const char*pile_name) {
		$self->clearOnePrivatePile(pile_name);
	}
};

class ClientPlayer: public Player {
public:
	explicit ClientPlayer(Client*client);
	virtual int aliveCount() const;
	virtual int getHandcardNum() const;
	virtual QList<const Card*> getKnownCards() const;
	virtual QList<const Card*> getHandcards() const;
	virtual void removeCard(int id, Place place);
	virtual void addCard(int id, Place place);
	virtual void addKnownHandCard(const Card*card);
	virtual bool isLastHandCard(const Card*card, bool contain = false) const;
};

extern ClientPlayer*Self;

class CardMoveReason {
public:
	int m_reason;
	QString m_playerId; // the cause (not the source) of the movement, such as "lusu" when "dimeng", or "zhanghe" when "qiaobian"
	QString m_targetId; // To keep this structure lightweight, currently this is only used for UI purpose.
						// It will be set to empty if multiple targets are involved. NEVER use it for trigger condition
						// judgement!!! It will not accurately reflect the real reason.
	QString m_skillName; // skill that triggers movement of the cards, such as "longdang", "dimeng"
	QString m_eventName; // additional arg such as "lebusishu" on top of "S_REASON_JUDGE"
	QVariant m_extraData; // additional data and will not be parsed to clients
	CardUseStruct m_useStruct;

	CardMoveReason();
	CardMoveReason(int moveReason, char*playerId);
	CardMoveReason(int moveReason, char*playerId, char*skillName, char*eventName);
	CardMoveReason(int moveReason, char*playerId, char*targetId, char*skillName, char*eventName);

	static const int S_REASON_UNKNOWN = 0x00;
	static const int S_REASON_USE = 0x01;
	static const int S_REASON_RESPONSE = 0x02;
	static const int S_REASON_DISCARD = 0x03;
	static const int S_REASON_RECAST = 0x04;          // ironchain etc.
	static const int S_REASON_PINDIAN = 0x05;
	static const int S_REASON_DRAW = 0x06;
	static const int S_REASON_GOTCARD = 0x07;
	static const int S_REASON_SHOW = 0x08;
	static const int S_REASON_TRANSFER = 0x09;
	static const int S_REASON_PUT = 0x0A;

	//subcategory of use
	static const int S_REASON_LETUSE = 0x11;           // use a card when self is not current

	//subcategory of response
	static const int S_REASON_RETRIAL = 0x12;

	//subcategory of discard
	static const int S_REASON_RULEDISCARD = 0x13;       //  discard at one's Player::Discard for gamerule
	static const int S_REASON_THROW = 0x23;             /*  gamerule(dying or punish)
															as the cost of some skills  */
	static const int S_REASON_DISMANTLE = 0x33;         //  one throw card of another

	//subcategory of gotcard
	static const int S_REASON_GIVE = 0x17;              // from one hand to another hand
	static const int S_REASON_EXTRACTION = 0x27;        // from another's place to one's hand
	static const int S_REASON_GOTBACK = 0x37;           // from placetable to hand
	static const int S_REASON_RECYCLE = 0x47;           // from discardpile to hand
	static const int S_REASON_ROB = 0x57;               // got a definite card from other's hand
	static const int S_REASON_PREVIEWGIVE = 0x67;       // give cards after previewing, i.e. Yiji& Miji
	static const int S_REASON_EXCLUSIVE = 0x68;         // get exclusive cards

	//subcategory of show
	static const int S_REASON_TURNOVER = 0x18;          // show n cards  from drawpile
	static const int S_REASON_JUDGE = 0x28;             // show a card  from drawpile for judge
	static const int S_REASON_PREVIEW = 0x38;           // Not done yet, plan for view some cards for self only(guanxing yiji miji)
	static const int S_REASON_DEMONSTRATE = 0x48;       // show a card which copy one to move to table

	//subcategory of transfer
	static const int S_REASON_SWAP = 0x19;              // exchange card for two players
	static const int S_REASON_OVERRIDE = 0x29;          // exchange cards from cards in game
	static const int S_REASON_EXCHANGE_FROM_PILE = 0x39;// exchange cards from cards moved out of game (for qixing only)

	//subcategory of put
	static const int S_REASON_NATURAL_ENTER = 0x1A;     //  a card with no-owner move into discardpile
	static const int S_REASON_REMOVE_FROM_PILE = 0x2A;  //  cards moved out of game go back into discardpile
	static const int S_REASON_JUDGEDONE = 0x3A;         //  judge card move into discardpile
	static const int S_REASON_CHANGE_EQUIP = 0x4A;      //  replace existed equip
	static const int S_REASON_SHUFFLE = 0x5A;           //  shuffle cards into drawpile
	static const int S_REASON_PUT_END = 0x6A;           //  move cards to end of drawpile

	static const int S_MASK_BASIC_REASON = 0x0F;
};

struct DamageStruct {
	DamageStruct();
	DamageStruct(const Card*card, ServerPlayer*from, ServerPlayer*to, int damage = 1, DamageStruct::Nature nature = Normal);
	DamageStruct(const char*reason, ServerPlayer*from, ServerPlayer*to, int damage = 1, DamageStruct::Nature nature = Normal);

	enum Nature {
		Normal, // normal slash, duel and most damage caused by skill
		Fire,  // fire slash, fire attack and few damage skill (Yeyan, etc)
		Thunder, // lightning, thunder slash, and few damage skill (Leiji, etc)
		Ice, //ice slash
		God, //for godlailailai
		Poison //for diy
	};

	ServerPlayer*from;
	ServerPlayer*to;
	const Card*card;
	int damage;
	Nature nature;
	bool chain;
	bool transfer;
	bool by_user;
	QString reason;
	QString transfer_reason;
	QStringList tips;
	bool prevented;
	bool ignore_hujia;

	QString getReason() const;
};

struct HpLostStruct {
	HpLostStruct();
	HpLostStruct(ServerPlayer*to, int lose = 1, const char*reason = nullptr, ServerPlayer*from = nullptr, bool ignore_hujia = true);
	ServerPlayer*from;
	ServerPlayer*to;
	QString reason;
	int lose;
	bool ignore_hujia;
};

struct CardEffectStruct {
	CardEffectStruct();

	const Card*card;
	
	const Card*offset_card;//抵消这张即将生效牌的牌
	int offset_num;//需要用多少张牌抵消

	ServerPlayer*from;
	ServerPlayer*to;

	bool multiple;
	bool nullified;
	bool no_respond;
	bool no_offset;
    int extra_effect;
};

struct SlashEffectStruct {
	SlashEffectStruct();

	int jink_num;

	const Card*slash;
	const Card*jink;

	ServerPlayer*from;
	ServerPlayer*to;

	int drank;

	DamageStruct::Nature nature;
	
	bool multiple;
	bool nullified;
	bool no_respond;
	bool no_offset;
};

struct CardUseStruct {
	enum CardUseReason {
		CARD_USE_REASON_UNKNOWN = 0x00,
		CARD_USE_REASON_PLAY = 0x01,
		CARD_USE_REASON_RESPONSE = 0x02,
		CARD_USE_REASON_RESPONSE_USE = 0x12
	};

	CardUseStruct();
	CardUseStruct(const Card*card, ServerPlayer*from, QList<ServerPlayer*> to, bool isOwnerUse = true,
				const Card*whocard = nullptr, ServerPlayer*who = nullptr);
	CardUseStruct(const Card*card, ServerPlayer*from, ServerPlayer*target = nullptr, bool isOwnerUse = true,
				const Card*whocard = nullptr, ServerPlayer*who = nullptr);
	bool isValid(const char*pattern) const;
	void parse(const char*str, Room*room);
	void clientReply();
	void changeCard(Card*newcard);

	const Card*card;
	ServerPlayer*from;
	QList<ServerPlayer*> to;
	bool m_isOwnerUse;
	bool m_addHistory;
	bool m_isHandcard;
	QStringList nullified_list;
	const Card*whocard;  //令你使用牌的牌，例如【借刀杀人】
	ServerPlayer*who;  //令你使用牌的角色
	QStringList no_respond_list; //不能被响应
	QStringList no_offset_list; //不能被抵消
    int extra_use;
};

struct CardsMoveStruct {
	CardsMoveStruct();
	CardsMoveStruct(const QList<int>&ids, Player*from, Player*to, Player::Place from_place, Player::Place to_place, CardMoveReason reason);
	CardsMoveStruct(const QList<int>&ids, Player*to, Player::Place to_place, CardMoveReason reason);
	CardsMoveStruct(int id, Player*from, Player*to, Player::Place from_place, Player::Place to_place, CardMoveReason reason);
	CardsMoveStruct(int id, Player*to, Player::Place to_place, CardMoveReason reason);

	QList<int> card_ids;
	Player::Place from_place, to_place;
	QString from_player_name, to_player_name;
	QString from_pile_name, to_pile_name;
	Player*from,*to;
	CardMoveReason reason;
	bool open;
	bool is_last_handcard;
	QStringList last_hand_suits;
};

struct CardsMoveOneTimeStruct {
	QList<int> card_ids;
	QList<Player::Place> from_places;
	Player::Place to_place;
	CardMoveReason reason;
	Player*from,*to;
	QStringList from_pile_names;
	QString to_pile_name;

	QList<bool> open; // helper to prevent sending card_id to unrelevant clients
	bool is_last_handcard;
	QStringList last_hand_suits;

	void removeCardIds(const QList<int>&to_remove);
};

struct DyingStruct {
	DyingStruct();

	ServerPlayer*who; // who is ask for help
	DamageStruct*damage; // if it is nullptr that means the dying is caused by losing hp
	HpLostStruct*hplost;
};

struct DeathStruct {
	DeathStruct();

	ServerPlayer*who; // who is ask for help
	DamageStruct*damage; // if it is nullptr that means the dying is caused by losing hp
	HpLostStruct*hplost;
};

struct RecoverStruct {
	RecoverStruct(ServerPlayer*who = nullptr, const Card*card = nullptr, int recover = 1, const char*reason = nullptr);
	RecoverStruct(const char*reason, ServerPlayer*who = nullptr, int recover = 1);
	
	int recover;
	ServerPlayer*who;
	const Card*card;
	QString reason;
};

struct MaxHpStruct {
	MaxHpStruct();
	MaxHpStruct(ServerPlayer*who, int change, const char*reason = nullptr);

	ServerPlayer*who;
	int change;
	QString reason;
};

struct JudgeStruct {
	JudgeStruct();
	bool isGood() const;
	bool isBad() const;
	bool isEffected() const;
	void updateResult();

	bool isGood(const Card*card) const; // For AI

	bool negative;
	bool play_animation;
	ServerPlayer*who;
	const Card*card;
	QString pattern;
	bool good;
	QString reason;
	bool time_consuming;
	bool throw_card;//是否将判定牌置入弃牌堆
	ServerPlayer*retrial_by_response; // record whether the current judge card is provided by a response retrial
};

struct PindianStruct {
	PindianStruct();

	ServerPlayer*from;
	ServerPlayer*to;
	const Card*from_card;
	const Card*to_card;
	int from_number;
	int to_number;
	QString reason;
	bool success;
};

struct PhaseChangeStruct {
	PhaseChangeStruct();

	Player::Phase from;
	Player::Phase to;
};

struct CardResponseStruct {
	CardResponseStruct();

	void changeCard(Card*newcard);

	const Card*m_card;
	ServerPlayer*m_who;
	bool m_isUse;
	bool m_isRetrial;
	bool m_isHandcard;
	const Card*m_toCard;  //令你使用或打出牌的牌
	bool nullified;//响应无效
};

struct MarkStruct {
	MarkStruct();
	ServerPlayer*who;
	QString name;
	int count;
	int gain;
};

struct DrawStruct {
	DrawStruct();
	ServerPlayer*who;//摸牌者
	QString reason;//摸牌信息
	int num;//摸牌数
	bool top;//正向摸牌（从牌堆顶摸）
	bool visible;//摸牌可见
	QList<int> card_ids;//摸到的牌id表
};

enum TriggerEvent {
	NonTrigger,

	GameReady,
	GameStart,
	TurnStart,
	EventPhaseStart,
	EventPhaseProceeding,
	EventPhaseEnd,
	EventPhaseChanging,
	EventPhaseSkipping,
	EventPhaseSkipped,
	EventPlayPhaseLoop,

	DrawNCards,
	AfterDrawNCards,
	DrawInitialCards,//已废除，不再触发
	AfterDrawInitialCards,//已废除，不再触发

	StartHpRecover,
	PreHpRecover,
	HpRecover,
	PreHpLost,
	HpLost,
	HpChanged,
	MaxHpChange,
	MaxHpChanged,

	EventLoseSkill,
	EventAcquireSkill,

	StartJudge,
	AskForRetrial,
	AfterRetrial,
	FinishRetrial,
	FinishJudge,

	AskforPindianCard,
	PindianVerifying,
	Pindian,

	TurnOver,
	TurnedOver,
	ChainStateChange,
	ChainStateChanged,

	ConfirmDamage,    // confirm the damage's count and damage's nature
	Predamage,        // trigger the certain skill -- jueqing
	DamageForseen,    // the first event in a damage -- kuangfeng dawu
	DamageCaused,     // the moment for -- qianxi..
	DamageInflicted,  // the moment for -- tianxiang..
	PreDamageDone,    // before reducing Hp --已废除，不再触发
	DamageDone,       // it's time to do the damage
	Damage,           // the moment for -- lieren..
	Damaged,          // the moment for -- yiji..
	DamageComplete,   // the moment for trigger iron chain

	EnterDying,
	Dying,
	QuitDying,
	AskForPeaches,
	AskForPeachesDone,
	Death,
	BuryVictim,
	BeforeGameOverJudge,
	GameOverJudge,
	GameOver,
	GameFinished,
	PreventPeach,
	AfterPreventPeach,

	Revive,
	Revived,

	KingdomChange,
	KingdomChanged,

	GeneralChange,
	GeneralChanged,

	PreChangeSlash,
	ChangeSlash,
	SlashEffected,//已废除，不再触发
	SlashProceed,//已废除，不再触发
	SlashHit,//已废除，不再触发
	SlashMissed,//已废除，不再触发

	JinkEffect,//已废除，不再触发
	NullificationEffect,//已废除，不再触发

	CardAsked,
	PreCardResponded,
	CardResponded,
	PostCardResponded,

	BeforeCardsMove, // sometimes we need to record cards before the move
	CardsMoveOneTime,

	PreCardUsed, // for AI to filter events only.
	CardUsed,
	TargetSpecifying,
	TargetConfirming,
	TargetSpecified,
	TargetConfirmed,
	CardEffect, // for AI to filter events only
	CardEffected,
	PostCardEffected,
	CardFinished,
	TrickCardCanceling,
	TrickEffect,//已废除，不再触发
	CardOnEffect,
	CardOffset,

	ChoiceMade,

	MarkChange,
	MarkChanged,

	GainHujia,
	GainedHujia,
	LoseHujia,
	LostHujia,

	RoundStart,
	RoundEnd,

	ThrowEquipArea,
	ObtainEquipArea,
	ThrowJudgeArea,
	ObtainJudgeArea,

	Appear, // For yinni only

	SwapPile,
	SwappedPile,

	InvokeSkill,
	SkillTriggered,

	ShowCards,

	StageChange, // For hulao pass only
	FetchDrawPileCard, // For miniscenarios only
	ActionedReset, // For 3v3 only
	Debut, // For 1v1 only

	TurnBroken, // For the skill 'DanShou'. Do not use it to trigger events

	EventForDiy, // For lua or diy to trigger special event

	NumOfEvents
};

class Card: public QObject {
public:
	// enumeration type
	enum Suit {
		Spade, Club, Heart, Diamond,
		NoSuitBlack, NoSuitRed, NoSuit, SuitToBeDecided
		};
	enum HandlingMethod {
		MethodNone, MethodUse, MethodResponse,
		MethodDiscard, MethodRecast, MethodPindian,
		MethodIgnore, MethodEffect, MethodPlay
		};
	enum Color { Red, Black, Colorless };
	static const Suit AllSuits[4];

	// card types
	enum CardType { TypeSkill, TypeBasic, TypeTrick, TypeEquip };

	// constructor
	Card(Suit suit, int number, bool target_fixed = false, bool damage_card = false, bool is_gift = false, bool single_target = false);

	virtual QString objectName(bool different_slash = true) const;

	// property getters/setters
	QString getSuitString() const;
	bool isRed() const;
	bool isBlack() const;
	int getId() const;
	void setId(int id);
	int getEffectiveId() const;

	int getNumber() const;
	void setNumber(int number);
	QString getNumberString() const;

	Suit getSuit() const;
	void setSuit(Suit suit);

	bool sameColorWith(const Card*other) const;
	bool sameNameWith(const Card*card, bool different_slash = false) const;
	bool sameNameWith(const char*card_name, bool different_slash = false) const;
	Color getColor() const;
	QString getColorString() const;
	bool hasSuit() const;
	bool isEquipped() const;

	QString getPackage() const;
	QString getFullName(bool include_suit = false) const;
	QString getLogName() const;
	QString getName() const;
	int nameLength() const;
	QString getSkillName(bool removePrefix = true) const;
	QStringList getSkillNames(bool removePrefix = true) const;
	void setSkillName(const char*skill_name);
	void addCharTag(const char*tag);
	QString getDescription() const;
	bool isGift() const;
	void setGift(bool flag);
	bool isDamageCard() const;
	void setDamageCard(bool flag);
	bool isSingleTargetCard() const;
	void setSingleTargetCard(bool flag);

	bool isVirtualCard(bool include_filter = false) const;
	virtual bool match(const char*pattern) const;

	void addSubcard(int card_id);
	void addSubcard(const Card*card);
	QList<int> getSubcards() const;
	void clearSubcards();
	QString subcardString() const;
	void addSubcards(const QList<const Card*>&cards);
	void addSubcards(const QList<int>&subcards_list);
	int subcardsLength() const;

	virtual QString getType() const = 0;
	virtual QString getSubtype() const = 0;
	virtual CardType getTypeId() const = 0;
	virtual QString toString(bool hidden = false) const;
	bool isNDTrick() const;

	// card target selection
	bool targetFixed() const;
	virtual bool targetsFeasible(const QList<const Player*>&targets, const Player*self) const;
	virtual bool targetFilter(const QList<const Player*>&targets, const Player*to_select, const Player*self) const;
	virtual bool targetFilter(const QList<const Player*>&targets, const Player*to_select, const Player*self, int&maxVotes) const;
	virtual bool isAvailable(const Player*player) const;
	virtual const Card*validate(CardUseStruct&card_use) const;
	virtual const Card*validateInResponse(ServerPlayer*user) const;

	bool isMute() const;
	void setMute(bool flag);
	bool willThrow() const;
	bool canRecast() const;
	bool hasPreAction() const;
	Card::HandlingMethod getHandlingMethod() const;
	void setCanRecast(bool can);

	void addMark(const char*mark, int add_num = 1) const;
	void removeMark(const char*mark, int remove_num = 1) const;
	void setMark(const char*mark, int value) const;
	int getMark(const char*mark) const;
	QStringList getMarkNames() const;

	void setFlags(const char*flag) const;
	bool hasFlag(const char*flag) const;
	void clearFlags() const;
	virtual QStringList getTips(bool split = true) const;
	bool hasTip(const char*tip, bool split = true) const;

	void setTag(const char*key, const QVariant&data) const;
	void removeTag(const char*key) const;

	virtual void onUse(Room*room, CardUseStruct&card_use) const;
	virtual void use(Room*room, ServerPlayer*source, QList<ServerPlayer*>&targets) const;
	virtual void onEffect(CardEffectStruct&effect) const;
	virtual bool isCancelable(const CardEffectStruct&effect) const;
	virtual bool isZhinangCard() const;

	virtual bool isKindOf(const char*cardType) const;
	virtual QStringList getFlags() const;
	virtual bool isModified() const;
	virtual QString getClassName() const;
	virtual const Card*getRealCard() const;

	// static functions
	static bool CompareByNumber(const Card*a, const Card*b);
	static bool CompareBySuit(const Card*a, const Card*b);
	static bool CompareByType(const Card*a, const Card*b);
	static const Card*Parse(const char*str);
	static Card*Clone(const Card*card);
	static QString Suit2String(Suit suit);
};

%extend Card {
	EquipCard*toEquipCard() {
		return qobject_cast<EquipCard*>($self);
	}

	Weapon*toWeapon() {
		return qobject_cast<Weapon*>($self);
	}

	Horse*toHorse() {
		return qobject_cast<Horse*>($self);
	}

	WrappedCard*toWrapped() {
		return qobject_cast<WrappedCard*>($self);
	}

	TrickCard*toTrick() {
		return qobject_cast<TrickCard*>($self);
	}

	DelayedTrick*toDelayedTrick() {
		return qobject_cast<DelayedTrick*>($self);
	}

	void cardOnUse(Room*room, CardUseStruct&card_use) const{
		$self->Card::onUse(room, card_use);
	}

	bool cardIsAvailable(const Player*player) const{
		return $self->Card::isAvailable(player);
	}

	QVariant getTag(const char*key) const{
		return $self->tag[key];
	}
};

class WrappedCard: public Card {
public:
	void takeOver(Card*card);
	void copyEverythingFrom(Card*card);
	void setModified(bool modified);
};

class SkillCard: public Card {
public:
	SkillCard();
	void setUserString(const char*user_string);
	QString getUserString() const;

	virtual QString getSubtype() const;
	virtual QString getType() const;
	virtual CardType getTypeId() const;
	virtual QString toString(bool hidden = false) const;

protected:
	QString user_string;
};

class DummyCard: public SkillCard {
public:
	DummyCard();
	DummyCard(const QList<int>&subcards);
};

class Package: public QObject {
public:
	enum Type { GeneralPack, CardPack, SpecialPack };

	Package(const char*name, Type pack_type = GeneralPack);
	void insertRelatedSkills(const char*main_skill, const char*related_skill);
	void insertConvertPairs(const char*from, const char*to);
	void addSkills(const Skill*skill);
};

class Scenario : public QObject {
public:
	Scenario(const char*name);
	ScenarioRule*getRule() const;
	void setRule(ScenarioRule*scenario_rule);

	void setScenarioLord(const char*lord);
	void addScenarioLoyalists(const char*loyalist);
	void addScenarioRebels(const char*rebel);
	void addScenarioRenegades(const char*renegade);

	virtual bool exposeRoles() const;
	void setExposeRoles(bool expose);
	virtual void assign(QStringList&generals, QStringList&roles) const;
	virtual int getPlayerCount() const;
	virtual QString getRoles() const;
};

class LuaScenario : public Scenario {
public:
	LuaScenario(const char*name);
};

class Engine: public QObject {
public:
	void addTranslationEntry(const char*key, const char*value);
	QString translate(const char*to_translate, bool initial = false) const;
	void addModes(const char*key, const char*value, const char*roles = "");

	void addPackage(Package*package);
	void addBanPackage(const char*package_name);
	Package*getPackage(const char*package_name);
	void setPackage(Package*package);
	QStringList getBanPackages() const;
	QStringList getZhinangCards() const;
	void setZhinangCard(const char*flag);
	Card*cloneCard(const Card*card) const;
	Card*cloneCard(const char*name, Card::Suit suit = Card::NoSuit, int number = 0, const QStringList&flags = QStringList()) const;
	SkillCard*cloneSkillCard(const char*name) const;
	QString getVersion() const;
	QString getVersionName() const;
	QStringList getExtensions() const;
	QStringList getKingdoms() const;
	QColor getKingdomColor(const char*kingdom) const;
	QString getSetupString() const;

	QMap<QString, QString> getAvailableModes() const;
	QString getModeName(const char*mode) const;
	int getPlayerCount(const char*mode) const;
	QString getRoles(const char*mode) const;
	QStringList getRoleList(const char*mode) const;
	int getRoleIndex() const;

	const CardPattern*getPattern(const char*name) const;
	bool matchPattern(const char*pattern, const Player*player, const Card*card) const;
	bool matchExpPattern(const char*pattern, const Player*player, const Card*card) const;
	Card::HandlingMethod getCardHandlingMethod(const char*method_name) const;
	QList<const Skill*> getRelatedSkills(const char*skill_name) const;
	const Skill*getMainSkill(const char*skill_name) const;

	QStringList getModScenarioNames() const;
	void addScenario(Scenario*scenario);
	const Scenario*getScenario(const char*name) const;

	const General*getGeneral(const char*name) const;
	int getGeneralCount(bool include_banned = false, const char*kingdom = "") const;
	const Skill*getSkill(const char*skill_name) const;
	QStringList getSkillNames() const;
	Skill*getRealSkill(const char*skill_name);
	const TriggerSkill*getTriggerSkill(const char*skill_name) const;
	const ViewAsSkill*getViewAsSkill(const char*skill_name) const;
	const ViewAsEquipSkill*getViewAsEquipSkill(const char*skill_name) const;
	const CardLimitSkill*getCardLimitSkill(const char*skill_name) const;
	
	QList<const ProhibitSkill*> getProhibitSkills() const;
	QList<const DistanceSkill*> getDistanceSkills() const;
	QList<const MaxCardsSkill*> getMaxCardsSkills() const;
	QList<const TargetModSkill*> getTargetModSkills() const;
	QList<const InvaliditySkill*> getInvaliditySkills() const;
	QList<const TriggerSkill*> getGlobalTriggerSkills() const;
	QList<const AttackRangeSkill*> getAttackRangeSkills() const;
	QList<const ViewAsEquipSkill*> getViewAsEquipSkills() const;
	QList<const CardLimitSkill*> getCardLimitSkills() const;
	QList<const ProhibitPindianSkill*> getProhibitPindianSkills() const;
	void addSkills(QList<const Skill*> all_skills);

	int getCardCount() const;
	const Card*getEngineCard(int cardId) const;
	Card*getCard(int cardId);
	WrappedCard*getWrappedCard(int cardId);

	QStringList getLords(bool contain_banned = false) const;
	QStringList getRandomLords() const;
	QStringList getRandomGenerals(int count, const QSet<QString>&ban_set = QSet<QString>(), const char*kingdom = "") const;
	QList<int> getRandomCards(bool derivative = false) const;
	QString getRandomGeneralName() const;
	QStringList getLimitedGeneralNames(const char*kingdom = "", bool available = true) const;
	QStringList getSlashNames() const;
	QStringList getCardNames(const char*pattern = ".") const;
	bool hasCard(const char*name) const;
	QList<const General*> getAllGenerals() const;
	bool sameNameWith(const char*name1, const char*name2) const;

	bool playSystemAudioEffect(const char*name, bool superpose = true) const;
	bool playAudioEffect(const char*filename, bool superpose = true) const;
	void setAudioType(const char*general_name, const char*filename, const char*types);
	int revisesAudioType(const char*general_name, const char*filename, int type) const;
	void playSkillAudioEffect(const char*skill_name, int index, bool superpose = true) const;

	const ProhibitSkill*isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*>&others = QList<const Player*>()) const;
	const ProhibitPindianSkill*isPindianProhibited(const Player*from, const Player*to) const;
	const CardLimitSkill*isCardLimited(const Player*player, const Card*card, Card::HandlingMethod method, bool isHandcard = false) const;
	int correctDistance(const Player*from, const Player*to, bool fixed = false) const;
	int correctMaxCards(const Player*target, bool fixed = false) const;
	int correctCardTarget(const TargetModSkill::ModType type, const Player*from, const Card*card, const Player*to = nullptr) const;
	bool correctSkillValidity(const Player*player, const Skill*skill) const;
	int correctAttackRange(const Player*target, bool include_weapon = true, bool fixed = false) const;

	Room*currentRoom();

	const Player*getCardOwner(int card_id);
	Player::Place getCardPlace(int card_id);

	QString getCurrentCardUsePattern();
	CardUseStruct::CardUseReason getCurrentCardUseReason();

	QString findConvertFrom(const char*general_name) const;
	bool isGeneralHidden(const char*general_name) const;
	
	QString removeNumberInQString(const char*str) const;
};

extern Engine*Sanguosha;

class Skill: public QObject {
public:
	enum Frequency { Frequent, NotFrequent, Compulsory, Limited, Wake, NotCompulsory, }; //Change

	explicit Skill(const char*name, Frequency frequent = NotFrequent);
	bool isLordSkill() const;
	bool isAttachedLordSkill() const;
	bool isChangeSkill() const;
	bool isLimitedSkill() const;
	bool isHideSkill() const;
	bool isShiMingSkill() const;
	virtual bool shouldBeVisible(const Player*Self) const;
	QString getDescription(const Player*target = nullptr) const;
	QString getNotice(int index) const;
	bool isVisible() const;

	virtual int getEffectIndex(const ServerPlayer*player, const Card*card) const;
	virtual QDialog*getDialog() const;

	void initMediaSource();
	void playAudioEffect(int index = -1, bool superpose = true) const;
	virtual Frequency getFrequency(const Player*target = nullptr) const;
	QStringList getSources() const;
	QString getLimitMark() const;
	QString getWakedSkills() const;
	bool setProperty(const char*name, const QVariant&value);
};

%extend Skill {
	const TriggerSkill*toTriggerSkill() const{
		return qobject_cast<const TriggerSkill*>($self);
	}
};

class TriggerSkill: public Skill {
public:
	TriggerSkill(const char*name);
	const ViewAsSkill*getViewAsSkill() const;
	QList<TriggerEvent> getTriggerEvents() const;

	virtual int getPriority(TriggerEvent event) const;
	virtual bool triggerable(ServerPlayer*target, Room*room, TriggerEvent event, ServerPlayer*owner, QVariant&data) const;
	virtual bool triggerable(const ServerPlayer*target, Room*room, TriggerEvent event) const;
	virtual bool triggerable(const ServerPlayer*target) const;
	virtual bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant&data, ServerPlayer*owner) const;
	virtual bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant&data) const = 0;
	virtual bool hasEvent(TriggerEvent event) const;
	virtual bool hasCan(ServerPlayer*target, ServerPlayer*owner) const;
	virtual bool canWake(TriggerEvent event, ServerPlayer*player, QVariant&data, Room*room) const;

	bool isGlobal() const;
};

class QThread: public QObject {
};

struct LogMessage {
	LogMessage();

	QString type;
	ServerPlayer*from;
	QList<ServerPlayer*> to;
	QString card_str;
	QString arg;
	QString arg2;
	QString arg3;
	QString arg4;
	QString arg5;
};

class RoomThread: public QThread {
public:
	explicit RoomThread(Room*room);
	void constructTriggerTable();
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*target, QVariant&data);
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*target);

	void addPlayerSkills(ServerPlayer*player, bool invoke_game_start = false);

	void addTriggerSkill(const TriggerSkill*skill);
	void delay(unsigned long msecs = 1000);
};

class Room: public QThread {
public:
	enum GuanxingType { GuanxingUpOnly = 1, GuanxingBothSides = 0, GuanxingDownOnly = -1 };

	explicit Room(QObject*parent, const char*mode);
	ServerPlayer*addSocket(ClientSocket*socket);
	bool isFull() const;
	bool isFinished() const;
	int getLack() const;
	QString getMode() const;
	const Scenario*getScenario() const;
	RoomThread*getThread() const;
	ServerPlayer*getCurrent() const;
	void setCurrent(ServerPlayer*current);
	int alivePlayerCount() const;
	QList<ServerPlayer*> getOtherPlayers(ServerPlayer*except, bool include_dead = false) const;
	QList<ServerPlayer*> getPlayers() const;
	QList<ServerPlayer*> getAllPlayers(bool include_dead = false) const;
	QList<ServerPlayer*> getAlivePlayers() const;
	QList<ServerPlayer*> getPathBetween(ServerPlayer*from, ServerPlayer*to, bool include_from = false, bool include_to = false) const;
	QList<ServerPlayer*> getClockwisePath(ServerPlayer*from, ServerPlayer*to, bool include_from = false, bool include_to = false) const;
	QList<ServerPlayer*> getCounterclockwisePath(ServerPlayer*from, ServerPlayer*to, bool include_from = false, bool include_to = false) const;
	void enterDying(ServerPlayer*player, DamageStruct*reason, HpLostStruct*hplost = nullptr);
	ServerPlayer*getCurrentDyingPlayer() const;
	ServerPlayer*getCardUser(const Card*card) const;
	void killPlayer(ServerPlayer*victim, DamageStruct*reason = nullptr, HpLostStruct*hplost = nullptr);
	void revivePlayer(ServerPlayer*player, bool sendlog = true, bool throw_mark = true, bool visible_only = false);
	void restPlayer(ServerPlayer*player, const char*reason = nullptr, bool discard_cards = false);
	void directRestPlayer(ServerPlayer*player, const char*reason = nullptr, bool discard_cards = false);
	void unrestPlayer(ServerPlayer*player, bool restore_full_hp = true);
	bool isRest(ServerPlayer*player) const;
	QList<ServerPlayer*> getRestPlayers() const;
	QStringList aliveRoles(ServerPlayer*except = nullptr) const;
	void gameOver(const char*winner);
	void slashEffect(const SlashEffectStruct&effect);
	void slashResult(const SlashEffectStruct&effect, const Card*jink);
	void attachSkillToPlayer(ServerPlayer*player, const char*skill_name);
	void detachSkillFromPlayer(ServerPlayer*player, const char*skill_name, bool is_equip = false, bool acquire_only = false, bool event_and_log = true);
	void handleAcquireDetachSkills(ServerPlayer*player, const char*skill_names, bool acquire_only = false, bool getmark = true, bool event_and_log = true);
	void acquireOneTurnSkills(ServerPlayer*player, const char*skill_name, const char*skill_names);
	void acquireNextTurnSkills(ServerPlayer*player, const char*skill_name, const char*skill_names);
	void setPlayerFlag(ServerPlayer*player, const char*flag);
	void setPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value);
	void setPlayerMark(ServerPlayer*player, const char*mark, int value, QList<ServerPlayer*> only_viewers = QList<ServerPlayer*>());
	void addPlayerMark(ServerPlayer*player, const char*mark, int add_num = 1, QList<ServerPlayer*> only_viewers = QList<ServerPlayer*>());
	void removePlayerMark(ServerPlayer*player, const char*mark, int remove_num = 1);
	void setPlayerCardLimitation(ServerPlayer*player, const char*limit_list, const char*pattern, bool single_turn);
	void removePlayerCardLimitation(ServerPlayer*player, const char*limit_list, const char*pattern);
	void clearPlayerCardLimitation(ServerPlayer*player, bool single_turn);
	void addCardMark(int card_id, const char*mark, int add_num = 1, ServerPlayer*who = nullptr);
	void addCardMark(const Card*card, const char*mark, int add_num = 1, ServerPlayer*who = nullptr);
	void removeCardMark(int card_id, const char*, int remove_num = 1);
	void removeCardMark(const Card*card, const char*mark, int remove_num = 1);
	void setCardMark(const Card*card, const char*mark, int value, ServerPlayer*who = nullptr);
	void setCardMark(int card_id, const char*mark, int value, ServerPlayer*who = nullptr);
	void setCardFlag(const Card*card, const char*flag, ServerPlayer*who = nullptr);
	void setCardFlag(int card_id, const char*flag, ServerPlayer*who = nullptr);
	void clearCardFlag(const Card*card, ServerPlayer*who = nullptr);
	void clearCardFlag(int card_id, ServerPlayer*who = nullptr);
	void setCardTip(int card_id, const char*tip);
	void clearCardTip(int card_id);
	CardUseStruct getUseStruct(const Card*card);
	bool useCard(CardUseStruct&use, bool add_history = false);
	void damage(DamageStruct damage);
	void loseHp(ServerPlayer*victim, int lose = 1, bool ignore_hujia = true, ServerPlayer*from = nullptr, const char*reason = nullptr);
	void loseHp(const HpLostStruct&data);
	void changePlayerMaxHp(ServerPlayer*player, int change = 1, const char*reason = nullptr);
	void loseMaxHp(ServerPlayer*victim, int lose = 1, const char*reason = nullptr);
	void gainMaxHp(ServerPlayer*player, int gain = 1, const char*reason = nullptr);
	bool changeMaxHpForAwakenSkill(ServerPlayer*player, int magnitude = -1, const char*reason = nullptr);
	void recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion = false);
	void changeKingdom(ServerPlayer*player, const char*kingdom);
	ServerPlayer*getSaver(ServerPlayer*player) const;
	bool cardEffect(const Card*card, ServerPlayer*from, ServerPlayer*to, bool multiple = false);
	bool cardEffect(CardEffectStruct&effect);
	bool isJinkEffected(ServerPlayer*user, const Card*jink);
	void judge(JudgeStruct&judge_struct);
	void sendJudgeResult(const JudgeStruct*judge);
	QList<int> getNCards(int n, bool update_pile_number = true, bool isTop = true);
	ServerPlayer*getLord() const;
	QList<int> askForGuanxing(ServerPlayer*zhuge, const QList<int>&cards, GuanxingType guanxing_type = GuanxingBothSides, bool sendLod = true);
	void returnToTopDrawPile(QList<int> cards);
	void returnToEndDrawPile(QList<int> cards);
	int doGongxin(ServerPlayer*shenlvmeng, ServerPlayer*target, QList<int> enabled_ids = QList<int>(), const char*skill_name = "gongxin");
	int drawCard(bool isTop = true);
	void fillAG(const QList<int>&card_ids, ServerPlayer*who = nullptr, const QList<int>&disabled_ids = QList<int>());
	void takeAG(ServerPlayer*player, int card_id, bool move_cards = true, QList<ServerPlayer*> to_notify = QList<ServerPlayer*>());
	void clearAG(ServerPlayer*player = nullptr);
	void provide(const Card*card, QList<ServerPlayer*> tos = QList<ServerPlayer*>());
	QList<ServerPlayer*> getLieges(const char*kingdom, ServerPlayer*lord) const;
	void sendLog(const LogMessage&log, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	void sendLog(const LogMessage&log, ServerPlayer*player);
	void sendCompulsoryTriggerLog(ServerPlayer*player, const char*skill_name, bool notify_skill = true, bool broadcast = false, int type = -1);
	void sendCompulsoryTriggerLog(ServerPlayer*player, const Skill*skill, int type = -1);
	void sendShimingLog(ServerPlayer*player, const char*skill_name, bool finish_or_failed = true, int index = -1);
	void sendShimingLog(ServerPlayer*player, const Skill*skill, bool finish_or_failed = true, int index = -1);
    void showCard(ServerPlayer*player, QList<int> card_ids, ServerPlayer*only_viewer , bool self_can_see = true);
    void showCard(ServerPlayer*player, int card_id, ServerPlayer*only_viewer , bool self_can_see = true);
    void showCard(ServerPlayer*player, int card_id, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void showCard(ServerPlayer*player, QList<int> card_ids, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void showAllCards(ServerPlayer*player, ServerPlayer*to);
    void showAllCards(ServerPlayer*player, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	void retrial(const Card*card, ServerPlayer*player, JudgeStruct*judge, const char*skill_name, bool exchange = false);
	void notifySkillInvoked(ServerPlayer*player, const char*skill_name);
	void broadcastSkillInvoke(const char*skillName, const ServerPlayer*player = nullptr);
	void broadcastSkillInvoke(const char*skillName, const char*category);
	void broadcastSkillInvoke(const char*skillName, int type, const ServerPlayer*player = nullptr);
	void broadcastSkillInvoke(const char*skillName, bool isMale, int type);
	void broadcastSkillInvoke(const Skill*skill, int type = -1, const ServerPlayer*player = nullptr);
	void doLightbox(const char*lightboxName, int duration = 2000, int pixelSize = 0);
	void doAnimate(int type, const char*arg1 = nullptr, const char*arg2 = nullptr, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	void setPlayerChained(ServerPlayer*player);
	void setPlayerChained(ServerPlayer*player, bool is_chained);
	void moveCardsToEndOfDrawpile(ServerPlayer*player, QList<int> card_ids, const char*skill_name, bool visible = false, bool guanxing = false);
	void moveCardsInToDrawpile(ServerPlayer*player, const Card*card, const char*skill_name, int n = 0, bool visible = false);
	void moveCardsInToDrawpile(ServerPlayer*player, int card_id, const char*skill_name, int n = 0, bool visible = false);
	void moveCardsInToDrawpile(ServerPlayer*player, QList<int> card_ids, const char*skill_name, int n = 0, bool visible = false);  //card_ids的顺序不会被打乱
	void shuffleIntoDrawPile(ServerPlayer*player, QList<int> card_ids, const char*skill_name, bool visible = false);  //card_ids的顺序会被打乱
	void removeDerivativeCards();
	void giveCard(ServerPlayer*from, ServerPlayer*to, const Card*card, const char*skill_name, bool visible = false);
	void giveCard(ServerPlayer*from, ServerPlayer*to, QList<int> give_ids, const char*skill_name, bool visible = false);
	void swapCards(ServerPlayer*first, ServerPlayer*second, const char*flags = "h", const char*reason = "", bool visible = false);
	void swapCards(ServerPlayer*first, ServerPlayer*second, QList<int> first_ids, QList<int> second_ids, const char*reason = "", bool visible = false);

	void doSuperLightbox(const char*heroName, const char*skillName);
	void doSuperLightbox(ServerPlayer*player, const char*skillName);

	bool notifyMoveCards(bool isLostPhase, QList<CardsMoveStruct>&cards_moves, bool forceVisible, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	bool notifyUpdateCard(ServerPlayer*player, int cardId, const Card*newCard);
	bool broadcastUpdateCard(const QList<ServerPlayer*>&players, int cardId, const Card*newCard);
	bool notifyResetCard(ServerPlayer*player, int cardId);
	bool broadcastResetCard(const QList<ServerPlayer*>&players, int cardId);
	void resetCard(int cardId) const;

	void changePlayerGeneral(ServerPlayer*player, const char*new_general);
	void changePlayerGeneral2(ServerPlayer*player, const char*new_general);
	void filterCards(ServerPlayer*player, QList<const Card*> cards, bool refilter);
	//const Card*filterCard(ServerPlayer*owner, const Card*card, Player::Place place, bool refilter);

	void acquireSkill(ServerPlayer*player, const Skill*skill, bool open = true, bool getmark = true, bool event_and_log = true);
	void acquireSkill(ServerPlayer*player, const char*skill_name, bool open = true, bool getmark = true, bool event_and_log = true);
	void adjustSeats();
	void swapPile();
	QList<int> getDiscardPile();
	QList<int>&getDrawPile();
	int getCardFromPile(const char*card_name);
	ServerPlayer*findPlayer(const char*general_name, bool include_dead = false) const;
	ServerPlayer*findPlayerBySkillName(const char*skill_name, bool include_lose = false) const;
	ServerPlayer*findPlayerByObjectName(const char*objectName, bool include_dead = false) const;
	QList<ServerPlayer*> findPlayersBySkillName(const char*skill_name) const;
	void installEquip(ServerPlayer*player, const char*equip_name);
	void resetAI(ServerPlayer*player);
	void changeHero(ServerPlayer*player, const char*new_general, bool full_state, bool invokeStart = true, bool isSecondaryHero = false, bool sendLog = true, int start_hp = 0);
	void swapSeat(ServerPlayer*a, ServerPlayer*b);
	lua_State*getLuaState() const;
	void setFixedDistance(Player*from, const Player*to, int distance);
	void removeFixedDistance(Player*from, const Player*to, int distance);
	void insertAttackRangePair(Player*from, const Player*to);
	void removeAttackRangePair(Player*from, const Player*to);
	void reverseFor3v3(const Card*card, ServerPlayer*player, QList<ServerPlayer*>&list);
	bool hasWelfare(const ServerPlayer*player) const;
	ServerPlayer*getFront(ServerPlayer*a, ServerPlayer*b) const;
	void signup(ServerPlayer*player, const char*screen_name, const char*avatar, bool is_robot);
	ServerPlayer*getOwner() const;

	void sortByActionOrder(QList<ServerPlayer*>&players);

	const ProhibitSkill*isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*>&others = QList<const Player*>()) const;
	const ProhibitPindianSkill*isPindianProhibited(const Player*from, const Player*to) const;

	void setTag(const char*key, const QVariant&value);
	QVariant getTag(const char*key) const;
	void removeTag(const char*key);

	void setEmotion(ServerPlayer*target, const char*emotion);
	void setLoopEmotion(ServerPlayer*target, const char*emotion);
	void changeTableBg(const char*tableBg);
	void changeBackground(const char*name, QList<ServerPlayer*> players = QList<ServerPlayer*>());
	void reversePlayOrder();
	bool isPlayOrderReversed() const;

	Player::Place getCardPlace(int card_id) const;
	ServerPlayer*getCardOwner(int card_id) const;
	void setCardMapping(int card_id, ServerPlayer*owner, Player::Place place);

	QList<int> drawCardsList(ServerPlayer*player, int n, const char*reason = nullptr, bool isTop = true, bool visible = false);
	void drawCards(ServerPlayer*player, int n, const char*reason = nullptr, bool isTop = true, bool visible = false);
	void drawCards(QList<ServerPlayer*> players, int n, const char*reason = nullptr, bool isTop = true, bool visible = false);
	void drawCards(QList<ServerPlayer*> players, QList<int> n_list, const char*reason = nullptr, bool isTop = true, bool visible = false);
	void obtainCard(ServerPlayer*target, const Card*card, bool visible = true);
	void obtainCard(ServerPlayer*target, int card_id, bool visible = true);
	void obtainCard(ServerPlayer*target, const Card*card, const CardMoveReason&reason, bool visible = true);
	void obtainCard(ServerPlayer*target, const Card*card, const char*skill_name, bool visible = true);
	void obtainCard(ServerPlayer*target, int card_id, const char*skill_name, bool visible = true);

	void throwCard(int card_id, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(const Card*card, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(const Card*card, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(int card_id, const char*skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(const Card*card, const char*skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(QList<int> card_ids, const char*skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void throwCard(QList<int> card_ids, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower = nullptr);
	void recastCard(ServerPlayer*player, const Card*card, const char*skill_name = nullptr);
	void recastCard(ServerPlayer*player, int card_id, const char*skill_name = nullptr);
	void recastCards(ServerPlayer*player, const QList<int>&card_ids, const char*skill_name = nullptr);
	void recastCardWithDraw(ServerPlayer*player, const Card*card, int draw_count, const char*skill_name = nullptr);
	void recastCardWithDraw(ServerPlayer*player, int card_id, int draw_count, const char*skill_name = nullptr);
	void recastCardsWithDraw(ServerPlayer*player, const QList<int>&card_ids, int draw_count, const char*skill_name = nullptr);
	
	void moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, bool visible = false, bool guanxin = false);
	void moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, const CardMoveReason&reason,
					bool visible = false, bool guanxin = false);
	void moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace, const CardMoveReason&reason,
					bool visible = false, bool guanxin = false);
	void moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace, const char*pileName,
					const CardMoveReason&reason, bool visible = false, bool guanxin = false);
	void moveCardsAtomic(QList<CardsMoveStruct> cards_move, bool visible, bool guanxin = false);
	void moveCardsAtomic(CardsMoveStruct cards_move, bool visible, bool guanxin = false);

	// interactive methods
	void activate(ServerPlayer*player, CardUseStruct&card_use);
	void askForLuckCard(QList<CardsMoveStruct>&cards_moves);
	Card::Suit askForSuit(ServerPlayer*player, const char*reason);
	QString askForKingdom(ServerPlayer*player, const char*reason = "", const char*kingdoms = "", bool send_log = true);
	bool askForSkillInvoke(ServerPlayer*player, const char*skill_name, const QVariant&data = QVariant(), bool notify = true);
	QString askForChoice(ServerPlayer*player, const char*skill_name, const char*choices, const QVariant&data = QVariant(),
						const char*except_choices = nullptr, const char*tip = nullptr);
	Card*askForDiscard(ServerPlayer*target, const char*reason, int discard_num, int min_num,
					bool optional = false, bool include_equip = false, const char*prompt = nullptr, const char*pattern = ".", const char*skill_name = nullptr);
	Card*askForExchange(ServerPlayer*player, const char*reason, int exchange_num, int min_num,
							bool include_equip = false, const char*prompt = nullptr, bool optional = false, const char*pattern = ".");
	bool askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive);
	bool useNullified(const Card*use_card);
	const Card*isCanceled(const CardEffectStruct&effect);
	int askForCardChosen(ServerPlayer*player, ServerPlayer*who, const char*flags, const char*reason,
						bool handcard_visible = false, Card::HandlingMethod method = Card::MethodNone, const QList<int>&disabled_ids = QList<int>(), bool can_cancel = false);
	const Card*askForCard(ServerPlayer*player, const char*pattern, const char*prompt, const QVariant&data, const char*skill_name);
	const Card*askForCard(ServerPlayer*player, const char*pattern, const char*prompt, const QVariant&data = QVariant(),
		Card::HandlingMethod method = Card::MethodDiscard, ServerPlayer*to = nullptr, bool isRetrial = false,
		const char*skill_name = nullptr, bool isProvision = false, const Card*m_toCard = nullptr);
	const Card*askForUseCard(ServerPlayer*player, const char*pattern, const char*prompt, int notice_index = -1,
		Card::HandlingMethod method = Card::MethodUse, bool addHistory = true, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	CardUseStruct askForUseCardStruct(ServerPlayer*player, const char*pattern, const char*prompt, int notice_index = -1,
		Card::HandlingMethod method = Card::MethodUse, bool addHistory = true, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	const Card*askForUseSlashTo(ServerPlayer*slasher, ServerPlayer*victim, const char*prompt,
		bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	const Card*askForUseSlashTo(ServerPlayer*slasher, QList<ServerPlayer*> victims, const char*prompt,
		bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	CardUseStruct askForUseSlashToStruct(ServerPlayer*slasher, ServerPlayer*victim, const char*prompt,
		bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	CardUseStruct askForUseSlashToStruct(ServerPlayer*slasher, QList<ServerPlayer*> victims, const char*prompt,
		bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr, const char*flag = nullptr);
	int askForAG(ServerPlayer*player, const QList<int>&card_ids, bool refusable, const char*reason, const char*prompt = nullptr);
	const Card*askForCardShow(ServerPlayer*player, ServerPlayer*requestor, const char*reason);
	ServerPlayer*askForYiji(ServerPlayer*guojia, QList<int>&cards, const char*skill_name = nullptr,
					bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
					QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
					const char*prompt = nullptr, bool notify_skill = false);
	QList<int> askForyiji(ServerPlayer*guojia, QList<int>&cards, const char*skill_name = nullptr,
					bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
					QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
					const char*prompt = nullptr, bool notify_skill = false);
	CardsMoveStruct askForYijiStruct(ServerPlayer*guojia, QList<int>&cards, const char*skill_name = nullptr,
					bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
					QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
					const char*prompt = nullptr, bool notify_skill = false, bool get = true);
	const Card*askForPindian(ServerPlayer*player, ServerPlayer*from, const char*reason);
	QList<const Card*> askForPindianRace(ServerPlayer*from, ServerPlayer*to, const char*reason);
	ServerPlayer*askForPlayerChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const char*reason,
									const char*prompt = nullptr, bool optional = false, bool notify_skill = false);
	QList<ServerPlayer*> askForPlayersChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets,
		const char*reason, int min_num = 0, int max_num = 2, const char*prompt = "",
		bool notify_skill = false, bool sort_ActionOrder = true);
	QString askForGeneral(ServerPlayer*player, const char*generals, const char*default_choice = "");
	const Card*askForSinglePeach(ServerPlayer*player, ServerPlayer*dying);
	void addPlayerHistory(ServerPlayer*player, const char*key, int times = 1);
	bool changeBGM(const char*bgm_name, bool reset = false, QList<ServerPlayer*> to_assign = QList<ServerPlayer*>());
	void playAudioEffect(const char*filename, bool superpose = true);

	void addMaxCards(ServerPlayer*player, int num, bool one_turn = true);
	void addAttackRange(ServerPlayer*player, int num, bool one_turn = true);
	void addSlashCishu(ServerPlayer*player, int num, bool one_turn = true);
	void addSlashJuli(ServerPlayer*player, int num, bool one_turn = true);
	void addSlashMubiao(ServerPlayer*player, int num, bool one_turn = true);
	void addSlashBuff(ServerPlayer*player, const char*flags, int num, bool one_turn = true);
	void addDistance(ServerPlayer*player, int num, bool player_isfrom = true, bool one_turn = true);
	QList<int> getAvailableCardList(ServerPlayer*player, const char*flags = "basic", const char*skill_name = nullptr, const Card*card = nullptr, bool except_delayedtrick = true);
	QList<ServerPlayer*> getCardTargets(ServerPlayer*from, const Card*card, QList<ServerPlayer*> except_players = QList<ServerPlayer*>());
	bool canMoveField(const char*flags = "ej", QList<ServerPlayer*> froms = QList<ServerPlayer*>(),
						QList<ServerPlayer*> tos = QList<ServerPlayer*>());
	bool moveField(ServerPlayer*player, const char*reason, bool optional = false, const char*flags = "ej", QList<ServerPlayer*> froms = QList<ServerPlayer*>(),
					QList<ServerPlayer*> tos = QList<ServerPlayer*>());
	void changeTranslation(ServerPlayer*player, const char*skill_name, const char*new_translation, int num = 0);
	void changeTranslation(ServerPlayer*player, const char*skill_name, int num = 1);
	int getChangeSkillState(ServerPlayer*player, const char*skill_name);
	void setChangeSkillState(ServerPlayer*player, const char*skill_name, int n);
	bool CardInPlace(const Card*card, Player::Place place);
	bool CardInTable(const Card*card);
	bool hasCurrent(bool need_alive = false);
	QList<int> showDrawPile(ServerPlayer*player, int num, const char*skill_name, bool liangchu = true, bool isTop = true);
	void ignoreCards(ServerPlayer*player, QList<int> ids);
	void ignoreCards(ServerPlayer*player, int id);
	void ignoreCards(ServerPlayer*player, const Card*card);
	void breakCard(QList<int> ids, ServerPlayer*player = nullptr);
	void breakCard(int id, ServerPlayer*player = nullptr);
	void breakCard(const Card*card, ServerPlayer*player = nullptr);
	void notifyMoveToPile(ServerPlayer*player, const QList<int>&cards, const char*reason, Player::Place place = Player::PlaceUnknown, bool in = true, bool visible = true);
	QString ZhizheCardViewAsEquip(const Card*card);
	void notifyWeaponRange(const char*weapon_name, int range = 1);

	void broadcastInvoke(const char*method, const char*arg = ".", ServerPlayer*except = nullptr);
	bool doNotify(ServerPlayer*player, int command, const char*arg);
	bool doBroadcastNotify(int command, const char*arg);
	bool doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const char*arg);
	
	bool doNotify(ServerPlayer*player, int command, const QVariant&arg);
	bool doBroadcastNotify(int command, const QVariant&arg);
	bool doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const QVariant&arg);

	void setCurrentCardUse(const char*newPattern, CardUseStruct::CardUseReason reason);

	void updateStateItem();
	bool notifyProperty(ServerPlayer*playerToNotify, const ServerPlayer*propertyOwner, const char*propertyName, const char*value = nullptr);
	bool broadcastProperty(ServerPlayer*player, const char*property_name, const char*value = nullptr);

	int getBossModeExpMult(int level) const;
};

%extend Room {
	ServerPlayer*nextPlayer() const{
		return $self->getCurrent()->getNextGamePlayer();
	}
	void updateCardDescription(const char*card_name, const QVariant&placeholders) {
		$self->updateCardDescription(card_name, placeholders.toMap());
	}
	void output(const char*msg) {
		if(Config.value("DebugOutput", false).toBool())
			$self->output(msg);
	}
	void outputEventStack() {
		if(Config.value("DebugOutput", false).toBool())
			$self->outputEventStack();
	}
	void writeToConsole(const char*msg) {
		$self->output(msg);
		qWarning("%s", msg);
	}
	void throwEvent(const TriggerEvent event) {
		Q_UNUSED($self);
		throw event;
	}
};

%{

void Room::doScript(const QString&script)
{
	LuaLocker locker;
	SWIG_NewPointerObj(m_lua, this, SWIGTYPE_p_Room, 0);
	lua_setglobal(m_lua, "R");

	SWIG_NewPointerObj(m_lua, current, SWIGTYPE_p_ServerPlayer, 0);
	lua_setglobal(m_lua, "P");

	int err = luaL_dostring(m_lua, script.toLatin1());
	if (err) {
		QString err_str = lua_tostring(m_lua, -1);
		lua_pop(m_lua, 1);
		output(err_str);
	}
}

%}

%include "card.i"
%include "luaskills.i"
%include "ai.i"
