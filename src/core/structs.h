#ifndef _STRUCTS_H
#define _STRUCTS_H

class Card;

#include "serverplayer.h"

struct GameModeStruct {
    GameModeStruct();
    GameModeStruct(const QString &mode_id, const QString &display_name = QString(),
                   int player_count = -1, const QString &roles = QString());

    QString mode_id;
    QString display_name;
    int player_count;
    QString roles;
    QString rule_mode;
    bool is_scenario;
    bool is_mini_scene;
    bool shuffle_roles;
    bool lord_welfare;

    bool isValid() const;
    bool operator==(const GameModeStruct &other) const;
    bool operator!=(const GameModeStruct &other) const;
    QString toString() const;
    
    void setShuffleRoles(bool shuffle) { shuffle_roles = shuffle; }
};


struct DamageStruct {
    enum Nature
    {
        Normal, // normal slash, duel and most damage caused by skill
        Fire,  // fire slash, fire attack and few damage skill (Yeyan, etc)
        Thunder, // lightning, thunder slash, and few damage skill (Leiji, etc)
        Ice, //ice slash
        God, //for godlailailai
        Poison  //for diy
    };

    DamageStruct();
    DamageStruct(const Card*card, ServerPlayer*from, ServerPlayer*to, int damage = 1, Nature nature = Normal);
    DamageStruct(const QString&reason, ServerPlayer*from, ServerPlayer*to, int damage = 1, Nature nature = Normal);

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
    bool prevented;
    QStringList tips;
    bool ignore_hujia;

    QString getReason() const;
};

struct HpLostStruct {
    HpLostStruct();
    HpLostStruct(ServerPlayer*to, int lose = 1, const QString&reason = "", ServerPlayer*from = nullptr, bool ignore_hujia = true);

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

    bool multiple; // helper to judge whether the card has multiple targets
    // does not make sense if the card inherits SkillCard
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
    enum CardUseReason
    {
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
    bool isValid(const QString&pattern) const;
    void parse(const QString&str, Room*room);
    bool tryParse(const QVariant&usage, Room*room);
    void clientReply();
    void changeCard(Card*newcard);

    const Card*card;
    ServerPlayer*from;
    QList<ServerPlayer*> to;
    bool m_isOwnerUse;
    bool m_addHistory;
    bool m_isHandcard;
    QStringList nullified_list;
    const Card*whocard;
    ServerPlayer*who;
    QStringList no_respond_list;
    QStringList no_offset_list;
    int extra_use;
};

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

    inline CardMoveReason()
    {
        m_reason = S_REASON_UNKNOWN;
    }

    inline CardMoveReason(int moveReason, QString playerId)
    {
        m_reason = moveReason;
        m_playerId = playerId;
    }

    inline CardMoveReason(int moveReason, QString playerId, QString skillName, QString eventName)
    {
        m_reason = moveReason;
        m_playerId = playerId;
        m_skillName = skillName;
        m_eventName = eventName;
    }

    inline CardMoveReason(int moveReason, QString playerId, QString targetId, QString skillName, QString eventName)
    {
        m_reason = moveReason;
        m_playerId = playerId;
        m_targetId = targetId;
        m_skillName = skillName;
        m_eventName = eventName;
    }

    bool tryParse(const QVariant&);
    QVariant toVariant() const;

    inline bool operator == (const CardMoveReason&other) const
    {
        return m_reason == other.m_reason
           && m_playerId == other.m_playerId
			&& m_targetId == other.m_targetId
           && m_skillName == other.m_skillName
           && m_eventName == other.m_eventName;
    }

    inline bool operator < (const CardMoveReason&other) const
    {
        return m_reason < other.m_reason
            || m_playerId < other.m_playerId
			|| m_targetId < other.m_targetId
            || m_skillName < other.m_skillName
            || m_eventName < other.m_eventName;
    }

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
    static const int S_REASON_TURNOVER = 0x18;          // show n cards from drawpile
    static const int S_REASON_JUDGE = 0x28;             // show a card from drawpile for judge
    static const int S_REASON_PREVIEW = 0x38;           // Not done yet, plan for view some cards for self only(guanxing yiji miji)
    static const int S_REASON_DEMONSTRATE = 0x48;       // show a card which copy one to move to table

    //subcategory of transfer
    static const int S_REASON_SWAP = 0x19;              // exchange card for two players
    static const int S_REASON_OVERRIDE = 0x29;          // exchange cards from cards in game
    static const int S_REASON_EXCHANGE_FROM_PILE = 0x39;// exchange cards from cards moved out of game (for qixing only)

    //subcategory of put
    static const int S_REASON_NATURAL_ENTER = 0x1A;     //  a card with no-owner move into discardpile
    //  e.g. delayed trick enters discardpile
    static const int S_REASON_REMOVE_FROM_PILE = 0x2A;  //  cards moved out of game go back into discardpile
    static const int S_REASON_JUDGEDONE = 0x3A;         //  judge card move into discardpile
    static const int S_REASON_CHANGE_EQUIP = 0x4A;      //  replace existed equip
    static const int S_REASON_SHUFFLE = 0x5A;           //  shuffle cards into drawpile
    static const int S_REASON_PUT_END = 0x6A;           //  move cards to end of drawpile

    static const int S_MASK_BASIC_REASON = 0x0F;
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

    inline void removeCardIds(const QList<int>&to_remove)
    {
        foreach (int id, to_remove) {
            int index = card_ids.indexOf(id);
            if (index > -1) {
                card_ids.removeAt(index);
                from_places.removeAt(index);
                from_pile_names.removeAt(index);
                open.removeAt(index);
            }
        }
    }
};

struct CardsMoveStruct {
    inline CardsMoveStruct()
    {
        from_place = Player::PlaceUnknown;
        to_place = Player::PlaceUnknown;
        from = nullptr;
        to = nullptr;
        is_last_handcard = false;
        //last_hand_suits = QStringList();
    }

    inline CardsMoveStruct(const QList<int>&ids, Player*from, Player*to, Player::Place from_place,
        Player::Place to_place, CardMoveReason reason)
    {
        this->card_ids = ids;
        this->from_place = from_place;
        this->to_place = to_place;
        this->from = from;
        this->to = to;
        this->reason = reason;
        this->is_last_handcard = false;
        //this->last_hand_suits = QStringList();
        if (from) this->from_player_name = from->objectName();
        if (to) this->to_player_name = to->objectName();
    }

    inline CardsMoveStruct(const QList<int>&ids, Player*to, Player::Place to_place, CardMoveReason reason)
    {
        this->card_ids = ids;
        this->from_place = Player::PlaceUnknown;
        this->to_place = to_place;
        this->from = nullptr;
        this->to = to;
        this->reason = reason;
        this->is_last_handcard = false;
        //this->last_hand_suits = QStringList();
        if (to) this->to_player_name = to->objectName();
    }

    inline CardsMoveStruct(int id, Player*from, Player*to, Player::Place from_place,
        Player::Place to_place, CardMoveReason reason)
    {
        this->card_ids << id;
        this->from_place = from_place;
        this->to_place = to_place;
        this->from = from;
        this->to = to;
        this->reason = reason;
        this->is_last_handcard = false;
        //this->last_hand_suits = QStringList();
        if (from) this->from_player_name = from->objectName();
        if (to) this->to_player_name = to->objectName();
    }

    inline CardsMoveStruct(int id, Player*to, Player::Place to_place, CardMoveReason reason)
    {
        this->card_ids << id;
        this->from_place = Player::PlaceUnknown;
        this->to_place = to_place;
        this->from = nullptr;
        this->to = to;
        this->reason = reason;
        this->is_last_handcard = false;
        //this->last_hand_suits = QStringList();
        if (to) this->to_player_name = to->objectName();
    }

    inline bool operator == (const CardsMoveStruct&other) const
    {
        return from == other.from&& from_place == other.from_place
			&& to == other.to&& reason == other.reason
			&& from_pile_name == other.from_pile_name;
    }

    inline bool operator < (const CardsMoveStruct&other) const
    {
        return from < other.from || from_place < other.from_place
			|| to < other.to || reason < other.reason
			|| from_pile_name < other.from_pile_name;
    }

    QList<int> card_ids;
    Player::Place to_place;
    Player::Place from_place;
    QString from_player_name;
    QString to_player_name;
    QString from_pile_name;
    QString to_pile_name;
    Player*from;
    Player*to;
    CardMoveReason reason;
    bool open; // helper to prevent sending card_id to unrelevant clients
    bool is_last_handcard;
    bool tryParse(const QVariant&arg);
    QVariant toVariant() const;
    QStringList last_hand_suits;
    inline bool isRelevant(const Player*player)
    {
		return to_place == Player::PlaceEquip || from_place == Player::PlaceEquip
		|| to_place == Player::PlaceDelayedTrick || from_place == Player::PlaceDelayedTrick
		|| to_place == Player::DiscardPile || from_place == Player::DiscardPile
		// any card from/to discard pile should be visible
		//|| to_place == Player::PlaceTable || from_place == Player::PlaceTable
		// only cards moved to hand/special can be invisible
		|| (to_place == Player::PlaceSpecial&& to->pileOpen(to_pile_name, player->objectName()))
		|| (from_place == Player::PlaceSpecial&& from->pileOpen(from_pile_name, player->objectName()))
		|| (reason.m_eventName=="yiji_give"&&reason.m_playerId==player->objectName())
		// any card from/to place table should be visible
		|| (to_place == Player::PlaceHand&& player->canSeeHandcard(to))
		|| (from_place == Player::PlaceHand&& player->canSeeHandcard(from));
		// pile open to specific players
	}
};

struct DyingStruct {
    DyingStruct();

    ServerPlayer*who; // who is ask for help
    DamageStruct*damage; // if it is nullptr that means the dying is caused by losing hp
    HpLostStruct*hplost;
};

struct DeathStruct {
    DeathStruct();

    ServerPlayer*who; // who is dead
    DamageStruct*damage; // if it is nullptr that means the dying is caused by losing hp
    HpLostStruct*hplost;
};

struct RecoverStruct {
    RecoverStruct(ServerPlayer*who = nullptr, const Card*card = nullptr, int recover = 1, const QString&reason = "");
    RecoverStruct(const QString&reason, ServerPlayer*who = nullptr, int recover = 1);

    int recover;
    ServerPlayer*who;
    const Card*card;
    QString reason;
};

struct MaxHpStruct {
    MaxHpStruct();
    MaxHpStruct(ServerPlayer*who, int change, const QString&reason = "");

    ServerPlayer*who;
    int change;
    QString reason;
};

struct PindianStruct {
    PindianStruct();
    bool isSuccess() const;

    ServerPlayer*from;
    ServerPlayer*to;
    const Card*from_card;
    const Card*to_card;
    int from_number;
    int to_number;
    QString reason;
    bool success;

    QList<int> numbers;
    QList<const Card*> cards;
    QList<ServerPlayer*> players;
    ServerPlayer*success_player;
};

struct JudgeStruct {
    JudgeStruct();
    bool isGood() const;
    bool isBad() const;
    bool isEffected() const;
    void updateResult();

    bool isGood(const Card*card) const; // For AI

    ServerPlayer*who;
    const Card*card;
    QString pattern;
    bool good;
    QString reason;
    bool time_consuming;
    bool negative;
    bool play_animation;
    bool throw_card;//是否将判定牌置入弃牌堆
    ServerPlayer*retrial_by_response; // record whether the current judge card is provided by a response retrial

private:
    enum TrialResult
    {
        TRIAL_RESULT_UNKNOWN,
        TRIAL_RESULT_GOOD,
        TRIAL_RESULT_BAD
    } _m_result;
};

struct PhaseChangeStruct {
    PhaseChangeStruct();
    Player::Phase from;
    Player::Phase to;
};

struct PhaseStruct {
    inline PhaseStruct()
    {
        phase = Player::PhaseNone;
        skipped = 0;
    }

    Player::Phase phase;
    int skipped; // 0 - not skipped; 1 - skipped by effect; -1 - skipped by cost
};

struct CardResponseStruct {
    inline CardResponseStruct()
    {
        m_card = nullptr;
        m_who = nullptr;
        m_isUse = false;
        m_isRetrial = false;
        m_toCard = nullptr;
        m_isHandcard = false;
        nullified = false;
    }

    inline CardResponseStruct(const Card*card, bool isUse)
    {
        m_card = card;
        m_who = nullptr;
        m_isUse = isUse;
        m_isRetrial = false;
        m_toCard = nullptr;
        m_isHandcard = false;
        nullified = false;
    }

    inline CardResponseStruct(const Card*card, ServerPlayer*who = nullptr, bool isUse = false)
    {
        m_card = card;
        m_who = who;
        m_isUse = isUse;
        m_isRetrial = false;
        m_toCard = nullptr;
        m_isHandcard = false;
        nullified = false;
    }

    void changeCard(Card*newcard);

    const Card*m_card;
    ServerPlayer*m_who;
    bool m_isUse;
    bool m_isHandcard;
    bool m_isRetrial;
    const Card*m_toCard;
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
    ServerPlayer*who;
    QString reason;
    int num;
    bool top;
    bool visible;
    QList<int> card_ids;
};

enum TriggerEvent {
    NonTrigger,

    GameReady,
    GameStart,
    TurnStart,
    TurnStarted,
    EventPhaseStart,
    EventPhaseProceeding,
    EventPhaseEnd,
    EventPhaseChanging,
    EventPhaseSkipping,
    EventPhaseSkipped,

    DrawNCards,
    AfterDrawNCards,
    DrawInitialCards,
    AfterDrawInitialCards,

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
    PreDamageDone,    // before reducing Hp
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

Q_DECLARE_METATYPE(DamageStruct)
Q_DECLARE_METATYPE(CardEffectStruct)
Q_DECLARE_METATYPE(SlashEffectStruct)
Q_DECLARE_METATYPE(CardUseStruct)
Q_DECLARE_METATYPE(CardsMoveStruct)
Q_DECLARE_METATYPE(CardsMoveOneTimeStruct)
Q_DECLARE_METATYPE(DyingStruct)
Q_DECLARE_METATYPE(DeathStruct)
Q_DECLARE_METATYPE(RecoverStruct)
Q_DECLARE_METATYPE(PhaseChangeStruct)
Q_DECLARE_METATYPE(CardResponseStruct)
Q_DECLARE_METATYPE(const Card*)
Q_DECLARE_METATYPE(ServerPlayer*)
Q_DECLARE_METATYPE(JudgeStruct*)
Q_DECLARE_METATYPE(PindianStruct*)
Q_DECLARE_METATYPE(MarkStruct)
Q_DECLARE_METATYPE(HpLostStruct)
Q_DECLARE_METATYPE(MaxHpStruct)
Q_DECLARE_METATYPE(DrawStruct)
#endif