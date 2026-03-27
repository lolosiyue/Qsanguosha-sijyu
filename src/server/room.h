#ifndef _ROOM_H
#define _ROOM_H

class ProhibitSkill;
class ProhibitPindianSkill;
class Scenario;
class TrickCard;

struct lua_State;
struct LogMessage;
class ServerPlayer;
class RoomThread;
class RoomThread3v3;
class RoomThreadXMode;
class RoomThread1v1;

//#include "serverplayer.h"
//#include "roomthread.h"
#include "protocol.h"
#include "room-state.h"
#include "json.h"

class Room : public QThread
{
    Q_OBJECT
        Q_ENUMS(GuanxingType)

public:
    enum GuanxingType
    {
        GuanxingUpOnly = 1, GuanxingBothSides = 0, GuanxingDownOnly = -1
    };

    friend class RoomThread;
    friend class RoomThread3v3;
    friend class RoomThreadXMode;
    friend class RoomThread1v1;

    typedef void (Room::*Callback)(ServerPlayer*, const QVariant&);
    typedef bool (Room::*ResponseVerifyFunction)(ServerPlayer*, const QVariant&, void*);

    explicit Room(QObject*parent, const QString&mode);
    ~Room();
    ServerPlayer*addSocket(ClientSocket*socket);
    inline int getId() const
    {
        return _m_Id;
    }
    bool isFull() const;
    bool isFinished() const;
    bool canPause(ServerPlayer*p) const;
    void tryPause();
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
    QList<ServerPlayer *> getPathBetween(ServerPlayer *from, ServerPlayer *to, bool include_from = false, bool include_to = false) const;
    QList<ServerPlayer *> getClockwisePath(ServerPlayer *from, ServerPlayer *to, bool include_from = false, bool include_to = false) const;
    QList<ServerPlayer *> getCounterclockwisePath(ServerPlayer *from, ServerPlayer *to, bool include_from = false, bool include_to = false) const;
    void reversePlayOrder();
    bool isPlayOrderReversed() const;
    void output(const QString&message);
    void outputEventStack();
    void enterDying(ServerPlayer*player, DamageStruct*reason, HpLostStruct*hplost = nullptr);
    ServerPlayer*getCurrentDyingPlayer() const;
    ServerPlayer*getCardUser(const Card*card) const;
    void killPlayer(ServerPlayer*victim, DamageStruct*reason = nullptr, HpLostStruct*hplost = nullptr);
    void revivePlayer(ServerPlayer*player, bool sendlog = true, bool throw_mark = true, bool visible_only = false);
    void restPlayer(ServerPlayer*player, const QString&reason = QString(), bool discard_cards = false);
    void directRestPlayer(ServerPlayer*player, const QString&reason = QString(), bool discard_cards = false);
    void unrestPlayer(ServerPlayer*player, bool restore_full_hp = true);
    bool isRest(ServerPlayer*player) const;
    QList<ServerPlayer*> getRestPlayers() const;
    QStringList aliveRoles(ServerPlayer*except = nullptr) const;
    void gameOver(const QString&winner);
    void slashEffect(const SlashEffectStruct&effect);
    void slashResult(const SlashEffectStruct&effect, const Card*jink);
    void attachSkillToPlayer(ServerPlayer*player, const QString&skill_name);
    void detachSkillFromPlayer(ServerPlayer*player, const QString&skill_name, bool is_equip = false, bool acquire_only = false, bool event_and_log = true);
    void handleAcquireDetachSkills(ServerPlayer*player, const QStringList&skill_names, bool acquire_only = false, bool getmark = true, bool event_and_log = true);
    void handleAcquireDetachSkills(ServerPlayer*player, const QString&skill_names, bool acquire_only = false, bool getmark = true, bool event_and_log = true);
    void acquireOneTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names);
    void acquireOneTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names);
    void acquireNextTurnSkills(ServerPlayer*player,const QString&skill_name, const QStringList&skill_names);
    void acquireNextTurnSkills(ServerPlayer*player, const QString&skill_name, const QString&skill_names);
    void setPlayerFlag(ServerPlayer*player, const QString&flag);
    void setPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value);
    void setPlayerMark(ServerPlayer*player, const QString&mark, int value, QList<ServerPlayer*> only_viewers = QList<ServerPlayer*>());
    void addPlayerMark(ServerPlayer*player, const QString&mark, int add_num = 1, QList<ServerPlayer*> only_viewers = QList<ServerPlayer*>());
    void removePlayerMark(ServerPlayer*player, const QString&mark, int remove_num = 1);
    void setPlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
        const QString&pattern, bool single_turn);
    void removePlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
        const QString&pattern);
    void clearPlayerCardLimitation(ServerPlayer*player, bool single_turn);
    void addCardMark(int card_id, const QString&mark, int add_num = 1, ServerPlayer*who = nullptr);
    void addCardMark(const Card*card, const QString&mark, int add_num = 1, ServerPlayer*who = nullptr);
    void removeCardMark(int card_id, const QString&mark, int remove_num = 1);
    void removeCardMark(const Card*card, const QString&mark, int remove_num = 1);
    void setCardMark(const Card*card, const QString&mark, int value, ServerPlayer*who = nullptr);
    void setCardMark(int card_id, const QString&mark, int value, ServerPlayer*who = nullptr);
    void setCardFlag(const Card*card, const QString&flag, ServerPlayer*who = nullptr);
    void setCardFlag(int card_id, const QString&flag, ServerPlayer*who = nullptr);
    void clearCardFlag(const Card*card, ServerPlayer*who = nullptr);
    void clearCardFlag(int card_id, ServerPlayer*who = nullptr);
    void setCardTip(int card_id, const QString&tip);
    void clearCardTip(int card_id);
    CardUseStruct getUseStruct(const Card*card);
    bool useCard(const CardUseStruct&use, bool add_history = false);
    bool useCard(CardUseStruct&use, bool add_history = false);
    void damage(DamageStruct damage);
    void loseHp(ServerPlayer*victim, int lose = 1, bool ignore_hujia = true, ServerPlayer*from = nullptr, const QString&reason = "");
    void loseHp(const HpLostStruct&lost_data);
    void changePlayerMaxHp(ServerPlayer*player, int change = 1, const QString&reason = "");
    void loseMaxHp(ServerPlayer*victim, int lose = 1, const QString&reason = "");
    void gainMaxHp(ServerPlayer*player, int gain = 1, const QString&reason = "");
    bool changeMaxHpForAwakenSkill(ServerPlayer*player, int magnitude = -1, const QString&reason = "");
    void recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion = false);
    void changeKingdom(ServerPlayer*player, const QString&kingdom);
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
    int doGongxin(ServerPlayer*shenlvmeng, ServerPlayer*target, QList<int> enabled_ids = QList<int>(), QString skill_name = "gongxin");
    int drawCard(bool isTop = true);
    void fillAG(const QList<int>&card_ids, ServerPlayer*who = nullptr, const QList<int>&disabled_ids = QList<int>());
    void takeAG(ServerPlayer*player, int card_id, bool move_cards = true, QList<ServerPlayer*> to_notify = QList<ServerPlayer*>());
    void clearAG(ServerPlayer*player = nullptr);
    void provide(const Card*card, QList<ServerPlayer*> tos = QList<ServerPlayer*>());
    QList<ServerPlayer*> getLieges(const QString&kingdom, ServerPlayer*lord) const;
    void sendLog(const LogMessage&log, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void sendLog(const LogMessage&log, ServerPlayer*player);
    void sendCompulsoryTriggerLog(ServerPlayer*player, const QString&skill_name, bool notify_skill = true, bool broadcast = false, int type = -1);
    void sendCompulsoryTriggerLog(ServerPlayer*player, const Skill*skill, int type = -1);
    void sendShimingLog(ServerPlayer*player, const QString&skill_name, bool finish_or_failed = true, int index = -1);
    void sendShimingLog(ServerPlayer*player, const Skill*skill, bool finish_or_failed = true, int index = -1);
    void showCard(ServerPlayer*player, QList<int> card_ids, ServerPlayer*only_viewer , bool self_can_see = true);
    void showCard(ServerPlayer*player, int card_id, ServerPlayer*only_viewer , bool self_can_see = true);
    void showCard(ServerPlayer*player, int card_id, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void showCard(ServerPlayer*player, QList<int> card_ids, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void showAllCards(ServerPlayer*player, ServerPlayer*to);
    void showAllCards(ServerPlayer*player, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void retrial(const Card*card, ServerPlayer*player, JudgeStruct*judge, const QString&skill_name, bool exchange = false);

    // Ask a player to send a server request and returns the client response. Call is blocking until client
    // replies or server times out, whichever is earlier.
    // @param player
    //        The server player to carry out the command.
    // @param command
    //        Command to be executed on client side.
    // @param arg
    //        Command args.
    // @param timeOut
    //        Maximum milliseconds that server should wait for client response before returning.
    // @param wait
    //        If true, return immediately after sending the request without waiting for client reply.
    // @return True if the a valid response is returned from client.
    // Usage note: when you need a round trip request-response vector with a SINGLE client, use this command
    // with wait = true and read the reponse from player->getClientReply(). If you want to initiate a poll
    // where more than one clients can respond simultaneously, you have to do it in two steps:
    // 1. Use this command with wait = false once for each client involved in the poll (or you can call this
    //    command only once in all with broadcast = true if the poll is to everypody).
    // 2. Call getResult(player, timeout) on each player to retrieve the result. Read manual for getResults
    //    before you use.
    bool doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, time_t timeOut, bool wait);
    bool doRequest(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg, bool wait);

    // Broadcast a request to a list of players and get the client responses. Call is blocking until all client
    // replies or server times out, whichever is earlier. Check each player's m_isClientResponseReady to see if a valid
    // result has been received. The client response can be accessed by calling each player's getClientReply() function.
    // @param players
    //        The list of server players to carry out the command.
    // @param command
    //        Command to be executed on client side. Command arguments should be stored in players->m_commandArgs.
    // @param timeOut
    //        Maximum total milliseconds that server will wait for all clients to respond before returning. Any client
    //        response after the timeOut will be rejected.
    // @return True if the a valid response is returned from client.
    bool doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut);
    bool doBroadcastRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command);

    // Broadcast a request to a list of players and get the first valid client response. Call is blocking until the first
    // client response is received or server times out, whichever is earlier. Any client response is verified by the validation
    // function and argument passed in. When a response is verified to be invalid, the function will continue to wait for
    // the next client response.
    // @param validateFunc
    //        Validation function that verifies whether the reply is a valid one. The first parameter passed to the function
    //        is the response sender, the second parameter is the response content, the third parameter is funcArg passed in.
    // @return The player that first send a legal request to the server. nullptr if no such request is received.
    ServerPlayer*doBroadcastRaceRequest(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut, ResponseVerifyFunction validateFunc = nullptr, void*funcArg = nullptr);

    // Notify a player of a event by sending S_SERVER_NOTIFICATION packets. No reply should be expected from
    // the client for S_SERVER_NOTIFICATION as it's a one way notice. Any message from the client in reply to this call
    // will be rejected.
    bool doNotify(ServerPlayer*player, QSanProtocol::CommandType command, const QVariant&arg);

    // Broadcast a event to a list of players by sending S_SERVER_NOTIFICATION packets. No replies should be expected from
    // the clients for S_SERVER_NOTIFICATION as it's a one way notice. Any message from the client in reply to this call
    // will be rejected.
    bool doBroadcastNotify(QSanProtocol::CommandType command, const QVariant&arg);
    bool doBroadcastNotify(const QList<ServerPlayer*>&players, QSanProtocol::CommandType command, const QVariant&arg);

    bool doNotify(ServerPlayer*player, int command, const char*arg);
    bool doBroadcastNotify(int command, const char*arg);
    bool doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const char*arg);

    bool doNotify(ServerPlayer*player, int command, const QVariant&arg);
    bool doBroadcastNotify(int command, const QVariant&arg);
    bool doBroadcastNotify(const QList<ServerPlayer*>&players, int command, const QVariant&arg);

    // Ask a server player to wait for the client response. Call is blocking until client replies or server times out,
    // whichever is earlier.
    // @param player
    //        The server player to retrieve the client response.
    // @param timeOut
    //        Maximum milliseconds that server should wait for client response before returning.
    // @return True if the a valid response is returned from client.

    // Usage note: this function is only supposed to be either internally used by doRequest (wait = true) or externally
    // used in pair with doRequest (wait = false). Any other usage could result in unexpected synchronization errors.
    // When getResult returns true, it's guaranteed that the expected client response has been stored and can be accessed by
    // calling player->getClientReply(). If getResult returns false, the value stored in player->getClientReply() could be
    // corrupted or in response to an unrelevant server request. Therefore, if the return value is false, do not poke into
    // player->getClientReply(), use the default value directly. If the return value is true, the reply value should still be
    // examined as a malicious client can have tampered with the content of the package for cheating purposes.
    bool getResult(ServerPlayer*player, time_t timeOut);
    ServerPlayer*getRaceResult(QList<ServerPlayer*> players, QSanProtocol::CommandType command, time_t timeOut,
        ResponseVerifyFunction validateFunc = nullptr, void*funcArg = nullptr);

    // Verification functions
    bool verifyNullificationResponse(ServerPlayer*, const QVariant&, void*);

    // Notification functions
    bool notifyMoveFocus(ServerPlayer*player);
    bool notifyMoveFocus(ServerPlayer*player, QSanProtocol::CommandType command);
    bool notifyMoveFocus(const QList<ServerPlayer*>&players, QSanProtocol::CommandType command, QSanProtocol::Countdown countdown);

    // Notify client side to move cards from one place to another place. A movement should always be completed by
    // calling notifyMoveCards in pairs, one with isLostPhase equaling true followed by one with isLostPhase
    // equaling false. The two phase design is needed because the target player doesn't necessarily gets the
    // cards that the source player lost. Any trigger during the movement can cause either the target player to
    // be dead or some of the cards to be moved to another place before the target player actually gets it.
    // @param isLostPhase
    //        Specify whether this is a S_COMMAND_LOSE_CARD notification.
    // @param move
    //        Specify all movements need to be broadcasted.
    // @param forceVisible
    //        If true, all players will be able to see the face of card regardless of whether the movement is
    //        relevant or not.
    bool notifyMoveCards(bool isLostPhase, QList<CardsMoveStruct>&move, bool forceVisible, QList<ServerPlayer*> players = QList<ServerPlayer*>());
    bool notifyProperty(ServerPlayer*player, const ServerPlayer*owner, const char*property_name, const QString&value = "");
    bool notifyUpdateCard(ServerPlayer*player, int cardId, const Card*newCard);
    bool broadcastUpdateCard(const QList<ServerPlayer*>&players, int cardId, const Card*newCard);
    bool notifyResetCard(ServerPlayer*player, int cardId);
    bool broadcastResetCard(const QList<ServerPlayer*>&players, int cardId);

    bool broadcastProperty(ServerPlayer*owner, const char*property_name, const QString&value = "");
    void notifySkillInvoked(ServerPlayer*player, const QString&skill_name);
    void broadcastSkillInvoke(const QString&skillName, const ServerPlayer*player = nullptr);
    void broadcastSkillInvoke(const QString&skillName, const QString&category);
    void broadcastSkillInvoke(const QString&skillName, int type, const ServerPlayer*player = nullptr);
    void broadcastSkillInvoke(const QString&skillName, bool isMale, int type);
    void broadcastSkillInvoke(const Skill*skill, int type = -1, const ServerPlayer*player = nullptr);
    void doLightbox(const QString&lightboxName, int duration = 2000, int pixelSize = 0);
    void doSuperLightbox(const QString&heroName, const QString&skillName, bool delay = true);
    void doSuperLightbox(ServerPlayer*player, const QString&skillName, bool delay = true);
    void doAnimate(QSanProtocol::AnimateType type, const QString&arg1 = "", const QString&arg2 = "",
        QList<ServerPlayer*> players = QList<ServerPlayer*>());

    inline void doAnimate(int type, const QString&arg1 = "", const QString&arg2 = "",
        QList<ServerPlayer*> players = QList<ServerPlayer*>())
    {
        doAnimate((QSanProtocol::AnimateType)type, arg1, arg2, players);
    }

    void preparePlayers();
    void changePlayerGeneral(ServerPlayer*player, const QString&new_general);
    void changePlayerGeneral2(ServerPlayer*player, const QString&new_general);
    void filterCards(ServerPlayer*player, QList<const Card*> cards, bool refilter);

    void acquireSkill(ServerPlayer*player, const Skill*skill, bool open = true, bool getmark = true, bool event_and_log = true);
    void acquireSkill(ServerPlayer*player, const QString&skill_name, bool open = true, bool getmark = true, bool event_and_log = true);
    void adjustSeats();
    void swapPile();
    inline QList<int> getDiscardPile()
    {
        return*m_discardPile;
    }
    inline QList<int>&getDrawPile()
    {
        return*m_drawPile;
    }
    int getCardFromPile(const QString&card_name);
    ServerPlayer*findPlayer(const QString&general_name, bool include_dead = false) const;
    QList<ServerPlayer*> findPlayersBySkillName(const QString&skill_name) const;
    ServerPlayer*findPlayerBySkillName(const QString&skill_name, bool include_lose = false) const;
    ServerPlayer*findPlayerByObjectName(const QString&objectName, bool include_dead = false) const;
    void installEquip(ServerPlayer*player, const QString&equip_name);
    void resetAI(ServerPlayer*player);
    void changeHero(ServerPlayer*player, const QString&new_general, bool full_state, bool invoke_start = true,
        bool isSecondaryHero = false, bool sendLog = true, int start_hp = 0);
    void swapSeat(ServerPlayer*a, ServerPlayer*b);
    lua_State*getLuaState() const;
    void setFixedDistance(Player*from, const Player*to, int distance);
    void removeFixedDistance(Player*from, const Player*to, int distance);
    void insertAttackRangePair(Player*from, const Player*to);
    void removeAttackRangePair(Player*from, const Player*to);
    void reverseFor3v3(const Card*card, ServerPlayer*player, QList<ServerPlayer*>&list);
    bool hasWelfare(const ServerPlayer*player) const;
    ServerPlayer*getFront(ServerPlayer*a, ServerPlayer*b) const;
    void signup(ServerPlayer*player, const QString&screen_name, const QString&avatar, bool is_robot);
    ServerPlayer*getOwner() const;
    void updateStateItem();

    void reconnect(ServerPlayer*player, ClientSocket*socket);
    void marshal(ServerPlayer*player);

    void sortByActionOrder(QList<ServerPlayer*>&players);

    const ProhibitSkill*isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*>&others = QList<const Player*>()) const;
    const ProhibitPindianSkill*isPindianProhibited(const Player*from, const Player*to) const;

    void setTag(const QString&key, const QVariant&value);
    QVariant getTag(const QString&key) const;
    void removeTag(const QString&key);

    void setEmotion(ServerPlayer*target, const QString&emotion);
    void setLoopEmotion(ServerPlayer*target, const QString&emotion);

    void changeTableBg(const QString&tableBg);
    void changeBackground(const QString name, QList<ServerPlayer *> players = QList<ServerPlayer *>());

    Player::Place getCardPlace(int card_id) const;
    ServerPlayer*getCardOwner(int card_id) const;
    void setCardMapping(int card_id, ServerPlayer*owner, Player::Place place);

    QList<int> drawCardsList(ServerPlayer*player, int n, const QString&reason = "", bool isTop = true, bool visible = false);
    void drawCards(ServerPlayer*player, int n, const QString&reason = "", bool isTop = true, bool visible = false);
    void drawCards(QList<ServerPlayer*> players, int n, const QString&reason = "", bool isTop = true, bool visible = false);
    void drawCards(QList<ServerPlayer*> players, QList<int> n_list, const QString&reason = "", bool isTop = true, bool visible = false);
    void obtainCard(ServerPlayer*target, const Card*card, bool visible = true);
    void obtainCard(ServerPlayer*target, int card_id, bool visible = true);
    void obtainCard(ServerPlayer*target, const Card*card, const CardMoveReason&reason, bool visible = true);
    void obtainCard(ServerPlayer*target, const Card*card, const QString&skill_name, bool visible = true);
    void obtainCard(ServerPlayer*target, int card_id, const QString&skill_name, bool visible = true);

    void throwCard(int card_id, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(const Card*card, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(const Card*card, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(int card_id, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(const Card*card, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(QList<int> card_ids, const QString&skill_name, ServerPlayer*who, ServerPlayer*thrower = nullptr);
    void throwCard(QList<int> card_ids, const CardMoveReason&reason, ServerPlayer*who, ServerPlayer*thrower = nullptr);

    void recastCard(ServerPlayer *player, const Card *card, const QString &skill_name = QString());
    void recastCard(ServerPlayer *player, int card_id, const QString &skill_name = QString());
    void recastCards(ServerPlayer *player, const QList<int> &card_ids, const QString &skill_name = QString());
    void recastCardWithDraw(ServerPlayer *player, const Card *card, int draw_count, const QString &skill_name = QString());
    void recastCardWithDraw(ServerPlayer *player, int card_id, int draw_count, const QString &skill_name = QString());
    void recastCardsWithDraw(ServerPlayer *player, const QList<int> &card_ids, int draw_count, const QString &skill_name = QString());

    void moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, bool visible = false, bool guanxin = false);
    void moveCardTo(const Card*card, ServerPlayer*dstPlayer, Player::Place dstPlace, const CardMoveReason&reason,
        bool visible = false, bool guanxin = false);
    void moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
        const CardMoveReason&reason, bool visible = false, bool guanxin = false);
    void moveCardTo(const Card*card, ServerPlayer*srcPlayer, ServerPlayer*dstPlayer, Player::Place dstPlace,
        const QString&pileName, const CardMoveReason&reason, bool visible = false, bool guanxin = false);
    void moveCardsAtomic(QList<CardsMoveStruct> cards_move, bool visible, bool guanxing = false);
    void moveCardsAtomic(CardsMoveStruct cards_move, bool visible, bool guanxing = false);
    QList<CardsMoveStruct> _breakDownCardMoves(QList<CardsMoveStruct> cards_moves);

    // interactive methods
    void activate(ServerPlayer*player, CardUseStruct&card_use);
    void askForLuckCard(QList<CardsMoveStruct>&cards_moves);
    Card::Suit askForSuit(ServerPlayer*player, const QString&reason);
    QString askForKingdom(ServerPlayer*player, const QString&reason, QStringList kingdoms, bool send_log = true);
    QString askForKingdom(ServerPlayer*player, const QString&reason = "", const QString&kingdoms = "", bool send_log = true);
    bool askForSkillInvoke(ServerPlayer*player, const QString&skill_name, const QVariant&data = QVariant(), bool notify = true);
    QString askForChoice(ServerPlayer*player, const QString&skill_name, const QString&choices, const QVariant&data = QVariant(),
                        const QString&except_choices = "", const QString&tip = "");
    Card*askForDiscard(ServerPlayer*player, const QString&reason, int discard_num, int min_num,
        bool optional = false, bool include_equip = false, const QString&prompt = "", const QString&pattern = ".",
        const QString&skill_name = "");
    Card*askForExchange(ServerPlayer*player, const QString&reason, int exchange_num, int min_num,
        bool include_equip = false, const QString&prompt = "", bool optional = false, const QString&pattern = ".");
    const Card*askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive);
    bool useNullified(const Card*use_card);
    const Card*isCanceled(const CardEffectStruct&effect);
    int askForCardChosen(ServerPlayer*player, ServerPlayer*who, const QString&flags, const QString&reason,
        bool handcard_visible = false, Card::HandlingMethod method = Card::MethodNone,
        const QList<int>&disabled_ids = QList<int>(), bool can_cancel = false);
    const Card*askForCard(ServerPlayer*player, const QString&pattern, const QString&prompt, const QVariant&data, const QString&skill_name);
    const Card*askForCard(ServerPlayer*player, const QString&pattern, const QString&prompt, const QVariant&data = QVariant(),
        Card::HandlingMethod method = Card::MethodDiscard, ServerPlayer*m_who = nullptr, bool isRetrial = false,
        const QString&skill_name = "", bool isProvision = false, const Card*m_toCard = nullptr);
    const Card*askForUseCard(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index = -1,
        Card::HandlingMethod method = Card::MethodUse, bool addHistory = true, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    CardUseStruct askForUseCardStruct(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index = -1,
        Card::HandlingMethod method = Card::MethodUse, bool addHistory = true, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    const Card*askForUseSlashTo(ServerPlayer*slasher, ServerPlayer*victim, const QString&prompt,
        bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    const Card*askForUseSlashTo(ServerPlayer*slasher, QList<ServerPlayer*> victims, const QString&prompt,
        bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    CardUseStruct askForUseSlashToStruct(ServerPlayer*slasher, ServerPlayer*victim, const QString&prompt,
        bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    CardUseStruct askForUseSlashToStruct(ServerPlayer*slasher, QList<ServerPlayer*> victims, const QString&prompt,
        bool distance_limit = true, bool disable_extra = false, bool addHistory = false, ServerPlayer*who = nullptr, const Card*whocard = nullptr,
        QString flag = "");
    int askForAG(ServerPlayer*player, const QList<int>&card_ids, bool refusable, const QString&reason, const QString&prompt = "");
    const Card*askForCardShow(ServerPlayer*player, ServerPlayer*requestor, const QString&reason);
    ServerPlayer*askForYiji(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name = "",
        bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
        QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
        const QString&prompt = "", bool notify_skill = false);
    QList<int> askForyiji(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name = "",
        bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
        QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
        const QString&prompt = "", bool notify_skill = false);
    CardsMoveStruct askForYijiStruct(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name = "",
        bool is_preview = false, bool visible = false, bool optional = true, int max_num = -1,
        QList<ServerPlayer*> players = QList<ServerPlayer*>(), CardMoveReason reason = CardMoveReason(),
        const QString&prompt = "", bool notify_skill = false, bool get = true);
    const Card*askForPindian(ServerPlayer*player, ServerPlayer*from, const QString&reason);
    QList<const Card*> askForPindianRace(ServerPlayer*from, ServerPlayer*to, const QString&reason);
    ServerPlayer*askForPlayerChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets, const QString&reason,
        const QString&prompt = "", bool optional = false, bool notify_skill = false);
    QList<ServerPlayer*> askForPlayersChosen(ServerPlayer*player, const QList<ServerPlayer*>&targets,
        const QString&reason, int min_num = 0, int max_num = 2, const QString&prompt = "",
        bool notify_skill = false, bool sort_ActionOrder = true);
    QString askForGeneral(ServerPlayer*player, const QStringList&generals, const QString&default_choice = "");
    QString askForGeneral(ServerPlayer*player, const QString&generals, const QString&default_choice = "");
    const Card*askForSinglePeach(ServerPlayer*player, ServerPlayer*dying);
    void addPlayerHistory(ServerPlayer*player, const QString&key, int times = 1);
    bool changeBGM(const QString&bgm_name, bool reset = false, QList<ServerPlayer*> to_assign = QList<ServerPlayer*>());
    void playAudioEffect(const QString&filename, bool superpose = true);

    void toggleReadyCommand(ServerPlayer*player, const QVariant&);
    void speakCommand(ServerPlayer*player, const QVariant&arg);
    void trustCommand(ServerPlayer*player, const QVariant&arg);
    void pauseCommand(ServerPlayer*player, const QVariant&arg);
    void processResponse(ServerPlayer*player, const QSanProtocol::Packet*arg);
    void addRobotCommand(ServerPlayer*player, const QVariant&arg);
    void broadcastInvoke(const QSanProtocol::AbstractPacket*packet, ServerPlayer*except = nullptr);
    void broadcastInvoke(const char*method, const QString&arg = ".", ServerPlayer*except = nullptr);
    void networkDelayTestCommand(ServerPlayer*player, const QVariant&);
    void moveCardsToEndOfDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible = false, bool guanxing = false);
    void moveCardsInToDrawpile(ServerPlayer*player, const Card*card, const QString&skill_name, int n = 0, bool visible = false);
    void moveCardsInToDrawpile(ServerPlayer*player, int card_id, const QString&skill_name, int n = 0, bool visible = false);
    void moveCardsInToDrawpile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, int n = 0, bool visible = false);
    void shuffleIntoDrawPile(ServerPlayer*player, QList<int> card_ids, const QString&skill_name, bool visible = false);
    void removeDerivativeCards();
    void giveCard(ServerPlayer*from, ServerPlayer*to, const Card*card, const QString&reason, bool visible = false);
    void giveCard(ServerPlayer*from, ServerPlayer*to, QList<int> give_ids, const QString&reason, bool visible = false);
    void swapCards(ServerPlayer*first, ServerPlayer*second, const QString&flags = "h", const QString&reason = "", bool visible = false);
    void swapCards(ServerPlayer*first, ServerPlayer*second, QList<int> first_ids, QList<int> second_ids, const QString&reason = "", bool visible = false);
    void setPlayerChained(ServerPlayer*player);
    void setPlayerChained(ServerPlayer*player, bool is_chained);
    void addMaxCards(ServerPlayer*player, int num, bool one_turn = false, const QString& reason = "", ServerPlayer* source = nullptr);
    void addAttackRange(ServerPlayer*player, int num, bool one_turn = true);
    void addSlashCishu(ServerPlayer*player, int num, bool one_turn = true);
    void addSlashJuli(ServerPlayer*player, int num, bool one_turn = true);
    void addSlashMubiao(ServerPlayer*player, int num, bool one_turn = true);
    void addSlashBuff(ServerPlayer*player, const QString&flags, int num, bool one_turn = true);
    void addDistance(ServerPlayer*player, int num, bool player_isfrom = true, bool one_turn = true);
    QList<int> getAvailableCardList(ServerPlayer*player, const QString&flags = "basic", const QString&skill_name = "", const Card*card = nullptr,
                                    bool except_delayedtrick = true);
    QList<ServerPlayer*> getCardTargets(ServerPlayer*from, const Card*card, QList<ServerPlayer*> except_players = QList<ServerPlayer*>());
    bool canMoveField(const QString&flags = "ej", QList<ServerPlayer*> froms = QList<ServerPlayer*>(),
                      QList<ServerPlayer*> tos = QList<ServerPlayer*>());
    bool moveField(ServerPlayer*player, const QString&reason, bool optional = false, const QString&flags = "ej",
                   QList<ServerPlayer*> froms = QList<ServerPlayer*>(), QList<ServerPlayer*> tos = QList<ServerPlayer*>());
    void changeTranslation(ServerPlayer*player, const QString&skill_name, const QString&new_translation, int num = 0);
    void changeTranslation(ServerPlayer*player, const QString&skill_name, int num = 1);
    int getChangeSkillState(ServerPlayer*player, const QString&skill_name);
    void setChangeSkillState(ServerPlayer*player, const QString&skill_name, int n);
    bool CardInPlace(const Card*card, Player::Place place);
    bool CardInTable(const Card*card);
    bool hasCurrent(bool need_alive = false);
    QList<int> showDrawPile(ServerPlayer*player, int num, const QString&skill_name, bool liangchu = true, bool isTop = true);
    void ignoreCards(ServerPlayer*player, QList<int> ids);
    void ignoreCards(ServerPlayer*player, int id);
    void ignoreCards(ServerPlayer*player, const Card*card);
    void breakCard(QList<int> ids, ServerPlayer*player = nullptr);
    void breakCard(int id, ServerPlayer*player = nullptr);
    void breakCard(const Card*card, ServerPlayer*player = nullptr);
    void notifyMoveToPile(ServerPlayer*player, const QList<int>&cards, const QString&reason, Player::Place place = Player::PlaceUnknown, bool in = true, bool visible = true);
    QString ZhizheCardViewAsEquip(const Card*card);
    void notifyWeaponRange(const QString&weapon_name, int range = 1);
    void updateCardDescription(const QString &card_name, const QVariantMap &placeholders);

    inline RoomState*getRoomState()
    {
        return&_m_roomState;
    }
    inline Card*getCard(int cardId) const
    {
		return _m_roomState.getCard(cardId);
    }
    inline void resetCard(int cardId) const
    {
        _m_roomState.resetCard(cardId);
    }
    inline void setCurrentCardUse(const QString&newPattern, CardUseStruct::CardUseReason reason)
    {
		_m_roomState.setCurrentCardUsePattern(newPattern);
		_m_roomState.setCurrentCardUseReason(reason);
    }
    void updateCardsChange(const CardsMoveStruct&move);

    int getBossModeExpMult(int level) const;

    const Card*_askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive);
    void safeSetPlayerProperty(ServerPlayer*player, const char*property_name, const QVariant&value);

protected:
    virtual void run();
    int _m_Id;

private:

    void _setAreaMark(ServerPlayer*player, int i, bool flag);

    struct _MoveSourceClassifier
    {
        inline _MoveSourceClassifier(const CardsMoveStruct&move)
        {
            m_from = move.from;
			m_from_place = move.from_place;
            m_from_pile_name = move.from_pile_name;
			m_from_player_name = move.from_player_name;
        }
        inline void copyTo(CardsMoveStruct&move)
        {
            move.from = m_from;
			move.from_place = m_from_place;
            move.from_pile_name = m_from_pile_name;
			move.from_player_name = m_from_player_name;
        }
        inline bool operator == (const _MoveSourceClassifier&other) const
        {
            return m_from == other.m_from && m_from_place == other.m_from_place
				// && m_from_player_name == other.m_from_player_name
                && m_from_pile_name == other.m_from_pile_name;
        }
        inline bool operator < (const _MoveSourceClassifier&other) const
        {
            return m_from < other.m_from || m_from_place < other.m_from_place
				//|| m_from_player_name < other.m_from_player_name
                || m_from_pile_name < other.m_from_pile_name;
        }
        Player*m_from;
        Player::Place m_from_place;
        QString m_from_pile_name, m_from_player_name;
    };

    struct _MoveMergeClassifier
    {
        inline _MoveMergeClassifier(const CardsMoveStruct&move)
        {
            m_from = move.from;
			m_to = move.to;
            m_to_place = move.to_place;
            m_to_pile_name = move.to_pile_name;
			m_reason = move.reason;
            m_is_last_handcard = move.is_last_handcard;
        }
        inline bool operator == (const _MoveMergeClassifier&other) const
        {
            return m_from == other.m_from && m_to == other.m_to && m_to_place == other.m_to_place
				 && m_to_pile_name == other.m_to_pile_name;// && m_reason == other.m_reason;
        }
        inline bool operator < (const _MoveMergeClassifier&other) const
        {
            return m_from < other.m_from || m_to < other.m_to || m_to_place < other.m_to_place
				|| m_to_pile_name < other.m_to_pile_name;// || m_reason < other.m_reason;
        }
        Player*m_from,*m_to;
        Player::Place m_to_place;
        QString m_to_pile_name;
		CardMoveReason m_reason;
        bool m_is_last_handcard;
    };

    struct _MoveSeparateClassifier
    {
        inline _MoveSeparateClassifier(const CardsMoveOneTimeStruct&moveOneTime, int index)
        {
            m_from = moveOneTime.from;
			m_to = moveOneTime.to;
            m_from_place = moveOneTime.from_places[index];
			m_to_place = moveOneTime.to_place;
            m_from_pile_name = moveOneTime.from_pile_names[index];
			m_to_pile_name = moveOneTime.to_pile_name;
            m_open = moveOneTime.open[index];
            m_reason = moveOneTime.reason;
            //m_is_last_handcard = moveOneTime.is_last_handcard;
        }
        inline bool operator == (const _MoveSeparateClassifier&other) const
        {
            return m_from == other.m_from && m_to == other.m_to && m_from_place == other.m_from_place
				 && m_to_place == other.m_to_place && m_from_pile_name == other.m_from_pile_name
				 && m_to_pile_name == other.m_to_pile_name// && m_open == other.m_open
				// && m_is_last_handcard == other.m_is_last_handcard
				 && m_reason == other.m_reason;
        }
        inline bool operator < (const _MoveSeparateClassifier&other) const
        {
            return m_from < other.m_from || m_to < other.m_to || m_from_place < other.m_from_place
				|| m_to_place < other.m_to_place || m_from_pile_name < other.m_from_pile_name
				|| m_to_pile_name < other.m_to_pile_name;// || m_open < other.m_open;
				//|| m_is_last_handcard < other.m_is_last_handcard
				//|| m_reason < other.m_reason;
        }
        Player*m_from,*m_to;
        Player::Place m_from_place, m_to_place;
        QString m_from_pile_name, m_to_pile_name;
        bool m_open;//, m_is_last_handcard;
        CardMoveReason m_reason;
    };

    int _m_lastMovementId;
    void _fillMoveInfo(CardsMoveStruct&moves, int id) const;
    QList<CardsMoveOneTimeStruct> _mergeMoves(QList<CardsMoveStruct> cards_moves);
    QList<CardsMoveStruct> _separateMoves(QList<CardsMoveOneTimeStruct> moveOneTimes);
    QString _chooseDefaultGeneral(ServerPlayer*player) const;
    bool _setPlayerGeneral(ServerPlayer*player, const QString&generalName, bool isFirst);
    QString mode;
    QList<ServerPlayer*> m_players, m_alivePlayers;
    int player_count;
    ServerPlayer*current;
    QList<int> pile1, pile2, table_cards;
    QList<int>*m_drawPile,*m_discardPile;
    QStack<DamageStruct> m_damageStack;
    int game_state;
    //bool game_started;
    //bool game_finished;
    bool game_paused;
    lua_State*m_lua;
    QList<AI*> ais;
	bool AIHumanized;

    RoomThread*thread;
    RoomThread3v3*thread_3v3;
    RoomThreadXMode*thread_xmode;
    RoomThread1v1*thread_1v1;
    QSemaphore _m_semRaceRequest; // When race starts, server waits on his semaphore for the first replier
    QSemaphore _m_semRoomMutex; // Provide per-room  (rather than per-player) level protection of any shared variables

    QHash<QSanProtocol::CommandType, Callback> m_callbacks; // Stores the callbacks for client request. Do not use this
    // this map for anything else but S_CLIENT_REQUEST!!!!!
    QHash<QSanProtocol::CommandType, QSanProtocol::CommandType> m_requestResponsePair;
    // Stores the expected client response for each server request, any unmatched client response will be discarded.

    QElapsedTimer _m_timeSinceLastSurrenderRequest; // Timer used to ensure that surrender polls are not initiated too frequently
    bool _m_isFirstSurrenderRequest; // We allow the first surrender poll to go through regardless of the timer.

    //helper variables for race request function
    bool _m_raceStarted;
    ServerPlayer*_m_raceWinner;

    QMap<int, Player::Place> place_map;
    QMap<int, ServerPlayer*> owner_map;

    QVariantMap tag;
    const Scenario*scenario;

    bool m_surrenderRequestReceived;
    bool _virtual;
    bool m_playOrderReversed;
    RoomState _m_roomState;

    JsonArray m_fillAGarg;
    QList<JsonArray> m_takeAGargs;

    QWaitCondition m_waitCond;
    mutable QMutex m_mutex;

    volatile bool playerPropertySet;
    //QMutex mutexPlayerProperty;
    //QWaitCondition wcPlayerProperty;

    static QString generatePlayerName();
    void prepareForStart();
    void assignGeneralsForPlayers(const QList<ServerPlayer*>&to_assign);
    void assignGeneralsForPlayersOfJianGeDefenseMode(const QList<ServerPlayer*>&to_assign);
    void chooseGenerals(QList<ServerPlayer*> players = QList<ServerPlayer*>());
    void chooseGeneralsOfJianGeDefenseMode();
    AI*cloneAI(ServerPlayer*player);
    void broadcast(const QString&message, ServerPlayer*except = nullptr);
    void initCallbacks();
    QString askForOrder(ServerPlayer*player, const QString&default_choice);
    QString askForRole(ServerPlayer*player, const QStringList&roles, const QString&scheme);

    //process client requests
    void processRequestCheat(ServerPlayer*player, const QVariant&arg);
    void processRequestSurrender(ServerPlayer*player, const QVariant&arg);

    bool makeSurrender(ServerPlayer*player);
    bool makeCheat(ServerPlayer*player);
    void makeDamage(const QString&source, const QString&target, QSanProtocol::CheatCategory nature, int point);
    void makeKilling(const QString&killer, const QString&victim);
    void makeReviving(const QString&name);
    void doScript(const QString&script);
    void stateChange(const QString&target, QSanProtocol::StateEditorCheat nature, int point);

    //helper functions and structs
    struct _NullificationAiHelper
    {
        const Card*m_trick;
        ServerPlayer*m_from;
        ServerPlayer*m_to;
    };
    void _setupChooseGeneralRequestArgs(ServerPlayer*player);

private slots:
    void reportDisconnection();
    void processClientPacket(const QString&packet);
    void assignRoles();
    void startGame();
    void slotSetProperty(ServerPlayer*player, const char*property_name, const QVariant&value);

signals:
    void room_message(const QString&msg);
    void game_start();
    void game_over(const QString&winner);
    void signalSetProperty(ServerPlayer*player, const char*property_name, const QVariant&value);
};

#endif
