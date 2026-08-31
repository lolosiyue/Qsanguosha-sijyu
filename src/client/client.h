#ifndef _CLIENT_H
#define _CLIENT_H

#include "standard.h"
#include "engine-runtime-context.h"
#include "client-core.h"
#include "client-interaction-presenter.h"
#include "custom-interaction-registry.h"
#include "protocol/protocol-message.h"
//#include "skill.h"
#include "room-state.h"
//#include "protocol.h"
// Client 的 signal/slot 以 ClientPlayer * / const ClientPlayer * /
// QList<const ClientPlayer *> 為參數，moc 產生的 metatype array 需要完整型別
// (Q_DECLARE_METATYPE(T*) 會 static_assert(sizeof(T)))，所以必須完整 include。
// clientplayer.h 只前置宣告 class Client，不會造成 circular include。
#include "clientplayer.h"
#include <QJsonArray>
#include <QRegularExpression>

class Recorder;
class Replayer;
class DesktopInteractionView;
class QTextDocument;
class ClientSocket;
class ClientLiveSession;
class ReplayTakeoverManager;

class Client : public QObject, public EngineRuntimeContext,
    public IClientInteractionPresenter
{
    Q_OBJECT
    Q_PROPERTY(Client::Status status READ getStatus WRITE setStatus)

public:
    enum Status
    {
        NotActive = 0x00,
        Responding = 0x01,
        Playing = 0x02,
        Discarding = 0x03,
        Exchanging = 0x04,
        ExecDialog = 0x05,
        AskForSkillInvoke = 0x06,
        AskForAG = 0x07,
        AskForPlayerChoose = 0x08,
        AskForYiji = 0x09,
        AskForGuanxing = 0x0A,
        AskForGongxin = 0x0B,
        AskForShowOrPindian = 0x0C,
        AskForGeneralTaken = 0x0D,
        AskForArrangement = 0x0E,
        AskForTriggerOrder = 0x0F,

        AskForQml = 0x10,

        RespondingUse = 0x11,
        RespondingForDiscard = 0x21,
        RespondingNonTrigger = 0x31,

        ClientStatusBasicMask = 0x0F
    };
    Q_ENUM(Status)

    // injectedSocket ownership is transferred to Client when it is non-null.
    explicit Client(QObject *parent, const QString &filename = "", ClientSocket *injectedSocket = nullptr);
    ~Client();

    // cheat functions
    void requestCheatGetOneCard(int card_id);
    void requestCheatChangeGeneral(const QString &name, bool isSecondaryHero = false);
    void requestCheatKill(const QString &killer, const QString &victim);
    void requestCheatDamage(const QString &source, const QString &target, DamageStruct::Nature nature, int points);
    void requestCheatRevive(const QString &name);
    void requestCheatRunScript(const QString &script);
    void requestCheatchangestate(const QString &target, int type, int points);

    // other client requests
    void requestSurrender();

    bool isReplayState() const { return nullptr != replayer; }
    bool isTakeoverMode() const;
    QString getTakeoverTarget() const;
    void enableTakeover(const QString &playerName);
    void disableTakeover();
    void saveTakeoverReplay(const QString &filepath);
    void processTakeoverRequest(const QSanProtocol::ProtocolMessage &message);

    void disconnectFromHost();
    void replyToServer(QSanProtocol::CommandType command, const QVariant &arg = QVariant());
    void requestServer(QSanProtocol::CommandType command, const QVariant &arg = QVariant());
    void notifyServer(QSanProtocol::CommandType command, const QVariant &arg = QVariant());
    void onPlayerResponseCard(const Card *card, const QList<const Player *> &targets = QList<const Player *>());
    void setStatus(Status status);
    Status getStatus() const;
    int alivePlayerCount() const;
    void onPlayerInvokeSkill(bool invoke);
    void onPlayerDiscardCards(const Card *card);
    void onPlayerReplyYiji(const Card *card, const Player *to);
    void onPlayerReplyGuanxing(const QList<int> &up_cards, const QList<int> &down_cards);
    void onPlayerDoGuanxingStep(int from, int to);
    void onPlayerAssignRole(const QList<QString> &names, const QList<QString> &roles);
    void onPlayerCancelAssignRole();
    void onPlayerChooseDraftGeneral(const QString &name);
    void onPlayerArrangeGenerals(const QStringList &names);
    void onPlayerChooseTriggerOrder(const QString &choice);
    QList<const ClientPlayer *> getPlayers() const;
    inline int getPlayerCount() const
    {
        return m_players.length();
    }
    void speakToServer(const QString &text);
    ClientPlayer *getPlayer(const QString &name);
    ClientPlayer *getNextPlayer(ClientPlayer *player) const;
    ClientPlayer *getLastPlayer(ClientPlayer *player) const;
    bool save(const QString &filename) const;
    QList<QByteArray> getRecords() const;
    QString getReplayPath() const;
    Replayer *getReplayer() const;
    QString getPlayerName(const QString &str);
    QString getSkillNameToInvoke() const;
    QString getSkillNameToInvokeData() const;
    lua_State *getLuaState() const;

    QTextDocument *getLinesDoc() const;
    QTextDocument *getPromptDoc() const;

    typedef void (Client::*Callback) (const QVariant &);

    void checkVersion(const QVariant &server_version);
    void setup(const QVariant &setup_str);
    void networkDelayTest(const QVariant &);
    void addPlayer(const QVariant &player_info);
    void onPlayerAddedMidGame(const QVariant &player_info);
    void removePlayer(const QVariant &player_name);
    void startInXs(const QVariant &);
    void arrangeSeats(const QVariant &seats);
    void activate(const QVariant &playerId);
    void startGame(const QVariant &pile);
    void hpChange(const QVariant &change_str);
    void maxhpChange(const QVariant &change_str);
    void resetPiles(const QVariant &arg);
    void addRound(const QVariant &);
    void setPileNumber(const QVariant &pile_str);
    void setTimeout(const QVariant &time);
    void synchronizeDiscardPile(const QVariant &discard_pile);
    void syncPile(const QVariant &pile_info);
    void gameOver(const QVariant &);
    void loseCards(const QVariant &);
    void getCards(const QVariant &);
    void updateProperty(const QVariant &);
    void updatePlayerUIState(const QVariant &);
    void stateSync(const QVariant &);
    void killPlayer(const QVariant &player_arg);
    void revivePlayer(const QVariant &player_arg);
    void warn(const QVariant &reason_json);
    void setMark(const QVariant &mark_str);
    void showCard(const QVariant &show_str);
    void showVirtualCard(const QVariant &arg);
    void cardProvenance(const QVariant &arg);
    void log(const QVariant &log_str);
    void speak(const QVariant &speak_data);
    void addHistory(const QVariant &history);
    void moveFocus(const QVariant &focus);
    void setEmotion(const QVariant &set_str);
    void changeTableBg(const QVariant &set_str);
    void skillInvoked(const QVariant &invoke_str);
    void animate(const QVariant &animate_str);
    void cardLimitation(const QVariant &limit);
    void setNullification(const QVariant &str);
    void enableSurrender(const QVariant &enabled);
    void exchangeKnownCards(const QVariant &players);
    void setKnownCards(const QVariant &set_str);
    void viewGenerals(const QVariant &str);
    void setFixedDistance(const QVariant &set_str);
    void setAttackRangePair(const QVariant &set_arg);
    void updateStateItem(const QVariant &state_str);
    void setAvailableCards(const QVariant &pile);
    void setCardMark(const QVariant &pattern_str);
    void setCardFlag(const QVariant &pattern_str);
    void updateCard(const QVariant &arg);
    void updateBossLevel(const QVariant &arg);
    void setSkillDescriptionSwap(const QVariant &reveal);
    void updateWeaponRange(const QVariant &arg);
    void playAudio(const QVariant &history);
    void addEquipArea(const QVariant &reveal);
    void setEquipAreaCount(const QVariant &reveal);
    void updateCardDescription(const QVariant &arg);

    void handleAnytimeSkillDone(const QVariant &arg);
    void setShownHandCards(const QVariant &arg);
    void setBrokenEquips(const QVariant &arg);
    void preshow(const QVariant &arg);

    void askForQml(const QVariant &arg);
    void replyQml(const QVariant &result);

    void fillAG(const QVariant &cards_str);
    void takeAG(const QVariant &take_str);
    void clearAG(const QVariant &);

    //interactive server callbacks
    void askForCardOrUseCard(const QVariant &);
    void askForAG(const QVariant &);
    void askForSinglePeach(const QVariant &);
    void askForCardShow(const QVariant &);
    void askForSkillInvoke(const QVariant &);
    void askForChoice(const QVariant &);
    void askForDiscard(const QVariant &);
    void askForExchange(const QVariant &);
    void askForSuit(const QVariant &);
    void askForKingdom(const QVariant &arg);
    void askForNullification(const QVariant &);
    void askForPindian(const QVariant &);
    void askForCardChosen(const QVariant &);
    void askForPlayerChosen(const QVariant &);
    void askForGeneral(const QVariant &);
    void askForYiji(const QVariant &);
    void askForGuanxing(const QVariant &);
    void showAllCards(const QVariant &);
    void mirrorGuanxingStep(const QVariant &arg);
    void askForGongxin(const QVariant &);
    void askForTriggerOrder(const QVariant &);
    void askForAssign(const QVariant &); // Assign roles at the beginning of game
    void askForSurrender(const QVariant &);
    void askForLuckCard(const QVariant &);
    void handleGameEvent(const QVariant &);
    //3v3 & 1v1
    void askForOrder(const QVariant &);
    void askForRole3v3(const QVariant &);
    void askForDirection(const QVariant &);

    // 3v3 & 1v1 methods
    void fillGenerals(const QVariant &generals);
    void askForGeneral3v3(const QVariant &);
    void takeGeneral(const QVariant &take_str);
    void startArrange(const QVariant &to_arrange);

    void recoverGeneral(const QVariant &);
    void revealGeneral(const QVariant &);

    void attachSkill(const QVariant &skill);
    void syncSkillInstances(const QVariant &payload);
    void updateSkill(const QVariant &args);

    inline RoomState *getRoomState()
    {
        return &_m_roomState;
    }

    QObject *runtimeObject() override { return this; }
    RoomState *roomState() override { return getRoomState(); }
    const Player *cardOwner(int card_id) const override { return getCardOwner(card_id); }
    Player::Place cardPlace(int card_id) const override { return getCardPlace(card_id); }
    Card *card(int card_id) const override { return getCard(card_id); }
    inline Card *getCard(int cardId) const
    {
        return _m_roomState.getCard(cardId);
    }

    inline void setCountdown(const QSanProtocol::Countdown &countdown)
    {
        m_mutexCountdown.lock();
        m_countdown = countdown;
        m_mutexCountdown.unlock();
    }

    inline QSanProtocol::Countdown getCountdown()
    {
        m_mutexCountdown.lock();
        QSanProtocol::Countdown countdown = m_countdown;
        m_mutexCountdown.unlock();
        return countdown;
    }

    inline QList<int> getAvailableCards() const
    {
        return available_cards;
    }

    const Player *getCardOwner(int card_id) const;
    Player::Place getCardPlace(int card_id) const;

    // public fields
    bool m_isDiscardActionRefusable;
    bool m_canDiscardEquip;
    QString m_cardDiscardPattern;
    bool m_noNullificationThisTime;
    QString m_noNullificationTrickName;
    QSet<QString> m_noNullificationPlayers;
    const ClientPlayer *m_respondingUseFixedTarget;
    int discard_num;
    int min_num;
    QString skill_name;
    QList<int> discarded_list;
    QStringList players_to_choose;
    int choose_max_num;
    int choose_min_num;
    int m_bossLevel;

    void setSelf(ClientPlayer *newSelf);

    // ── Client Architecture F1:結構化 interaction ────────────────────────
    //
    // Client 依然係 protocol／transport 層:佢收 packet、砌 InteractionRequest,
    // 交畀 ClientCore,再喺 core 接納答案之後先真正 replyToServer()。規則真相
    // 留喺 server,UI 只負責呈現同收集答案。
    //
    // 已遷移嘅五類:choose general／choice／choose player／skill invoke／
    // response card。其餘 interaction 仍然行舊路,見
    // docs/client-core-interaction-model.md。
    ClientCore *interactionCore() const { return m_interactionCore; }
    QJsonArray interactionInventory() const;

    // DesktopInteractionView 用嘅呈現 port。每一個都係原本 askForXxx() 尾段
    // 嗰一兩行(emit signal + setStatus)原封不動搬過嚟,所以 RoomScene／
    // Dashboard 一行都唔使改,desktop 外觀同操作亦保證唔變。
    void presentGeneralChoice(const InteractionRequest &request) override;
    void presentOptionChoice(const InteractionRequest &request) override;
    void presentPlayerChoice(const InteractionRequest &request) override;
    void presentSkillInvoke(const InteractionRequest &request) override;
    void presentCardResponse(const InteractionRequest &request) override;
    void presentRoleAssignment(const InteractionRequest &request) override;
    void presentDirectionChoice(const InteractionRequest &request) override;
    void presentCardExchange(const InteractionRequest &request) override;
    void presentCardDiscard(const InteractionRequest &request) override;
    void presentRespondingUse(const InteractionRequest &request) override;
    void presentShowOrPindian(const InteractionRequest &request) override;
    void presentPlayCard(const InteractionRequest &request) override;
    void presentGuanxing(const InteractionRequest &request) override;
    void presentGongxin(const InteractionRequest &request) override;
    void presentYiji(const InteractionRequest &request) override;
    void presentSuitChoice(const InteractionRequest &request) override;
    void presentKingdomChoice(const InteractionRequest &request) override;
    void presentTriggerOrder(const InteractionRequest &request) override;
    void presentAmazingGrace(const InteractionRequest &request) override;
    void presentChooseCard(const InteractionRequest &request) override;
    void presentOrderChoice(const InteractionRequest &request) override;
    void presentRole3v3(const InteractionRequest &request) override;
    void presentBooleanPrompt(const InteractionRequest &request) override;
    void presentDraftGeneral(const InteractionRequest &request) override;
    void presentArrangeGeneral(const InteractionRequest &request) override;
    void presentQmlInteraction(const InteractionRequest &request) override;

    // 唯一 UI reply 入口：填 identity、交畀 ClientCore validate/complete，
    // accepted 後由 typed encoder 產生並送出唯一一次 V2 wire reply。
    bool submitInteractionResponse(InteractionResponse response);

public slots:
    void signup();
    void processContextSwitch(const QVariant &target_name);
    void onPlayerChooseGeneral(const QString &_name);
    void onPlayerMakeChoice();
    void onPlayerChooseCard(int card_id = -2);
    void onPlayerChooseAG(int card_id);
    void onPlayerChoosePlayer(const QList<const Player *> &players);
    void trust();
    void addRobot(int num);

    void onPlayerReplyGongxin(int card_id = -1);

    void triggerAnytimeSkill(const QString &skill_name);
    bool isAnytimeSkillPending(const QString &skill_name) const;

protected:
    // operation countdown
    QSanProtocol::Countdown m_countdown;
    // sync objects
    QMutex m_mutexCountdown;
    Status status;
    int alive_count;
    int swap_pile;
    int add_round;
    RoomState _m_roomState;

private:
    ClientLiveSession *m_liveSession;
    bool m_isGameOver;
    bool m_isDisconnected;
    ClientPlayer *m_original_self;
    QHash<QSanProtocol::CommandType, Callback> m_interactions;
    QHash<QSanProtocol::CommandType, Callback> m_callbacks;
    QList<const ClientPlayer *> m_players;
    QStringList ban_packages;
    Recorder *recorder;
    Replayer *replayer;
    ReplayTakeoverManager *m_takeoverManager;
    quint64 m_pendingTakeoverRequestId = 0;
    QTextDocument *lines_doc, *prompt_doc;
    int pile_num;
    QString skill_to_invoke;
    QString skill_to_invoke_data;
    QList<int> available_cards;
    QList<int> m_amazingGraceCards;
    QList<int> m_amazingGraceDisabledCards;
    QList<int> m_amazingGraceTakenCards;
    QStringList m_filledGenerals;
    CustomInteractionRegistry m_customInteractionRegistry;
    QSet<QString> m_anytimeSkillPending;

    QMap<int, const Player*> owner_map;
    QMap<int, Player::Place> place_map;

    quint64 m_dispatchingRequestId = 0;
    lua_State *m_client_lua;
    ClientCore *m_interactionCore;
    DesktopInteractionView *m_desktopInteractionView;
    ClientGameState m_pendingStateSyncState;
    bool m_stateSyncActive = false;
    QString m_stateSyncId;

    void beginInteraction(InteractionRequest request);
    bool dispatchProtocolMessage(const QSanProtocol::ProtocolMessage &message,
                                 bool replayInput);
    void failProtocol(const QString &detail);
    InteractionRequest makeInteractionRequest(InteractionType type,
        InteractionPayload payload, bool cancelable = false) const;
    void cancelInteraction(InteractionType type, InteractionCancelReason reason);
    void syncInteractionState();

    void updatePileNum();
    // prompt_doc 都寫埋。
    QString setPromptList(const QStringList &text);
    // 淨係計字串,唔掂 prompt_doc:已遷移嘅 interaction 由 DesktopInteractionView
    // 負責呈現,所以 request builder 唔應該喺呢個階段寫 UI 文件。
    QString formatPromptList(const QStringList &text);
    QString _processCardPattern(const QString &pattern);
    void commandFormatWarning(const QString &str, const QRegularExpression &rx, const char *command);

    bool _loseSingleCard(int card_id, CardsMoveStruct move);
    bool _getSingleCard(int card_id, CardsMoveStruct move);

public slots:
    void processReplayMessage(const QSanProtocol::ProtocolMessage &message);

private slots:
    void processLiveProtocolMessage(const QSanProtocol::ProtocolMessage &message);
    bool processServerRequest(const QSanProtocol::ProtocolMessage &message);
    void notifyRoleChange(const QString &new_role);
    void onPlayerChooseSuit();
    void onPlayerChooseKingdom();
    void alertFocus();
    void onPlayerChooseRole3v3();

public slots:
    // 自動化測試: roomscene 直接呼叫自動選先手 (原為 private)
    void onPlayerChooseOrder();

signals:
    void version_checked(const QString &version_number, const QString &mod_name, int card_num);
    void server_connected();
    void error_message(const QString &msg);
    // 傳輸層觀測點:socket 真正接通 / 每個 server request 到埗 / 每個 client reply
    // 送出。自動化測試靠呢三個訊號分辨「連唔上」「連到但冇 request」「有 request
    // 但冇覆」,唔使解析 log 文字。無人連線時成本等同一次空 emit。
    void socket_connected();
    void socket_disconnected();
    void server_request(int commandType);
    void server_reply(int commandType);
    void player_added(ClientPlayer *new_player);
    void player_removed(const QString &player_name);
    void boss_level_changed();
    void add_equip_area(const QString &area);
    void card_description_updated(const QString &player_name, const QString &card_name);
    // choice signal
    void generals_got(const QStringList &generals);
    void kingdoms_got(const QStringList &kingdoms);
    void suits_got(const QStringList &suits);
    void options_got(const QString &skillName, const QStringList &options, const QString &except_options, const QString &tip);
    void cards_got(const ClientPlayer *player, const QString &flags, const QString &reason, bool handcard_visible,
        Card::HandlingMethod method, QList<int> disabled_ids, bool can_cancel);
    void roles_got(const QString &scheme, const QStringList &roles);
    void directions_got();
    void orders_got(QSanProtocol::Game3v3ChooseOrderCommand reason);
    void trigger_order_got(const QVariantList &options, bool optional);

    void seats_arranged(const QList<const ClientPlayer *> &seats);
    void hp_changed(const QString &who, int delta, int nature, int losthj);
    void maxhp_changed(const QString &who, int delta);
    void status_changed(Client::Status oldStatus, Client::Status newStatus);
    void avatars_hiden();
    void update_areas(const QString &who);
    void update_handcards(const QString &who);
    void pile_reset();
    //void round_add();
    void player_killed(const QString &who);
    void player_revived(const QString &who);
    void card_shown(const QString &player_name, QList<int> card_ids);
    void virtual_card_shown(const QString &player_name, const QString &card_name,
        const QString &suit, int number, const QString &skill_name, const QList<int> &subcard_ids,
        const QString &target_name);
    void log_received(const QStringList &log_str);
    void guanxing(const QList<int> &card_ids, int single_side);
    void mirror_guanxing_start(const QString &who, bool up_only, const QList<int> &cards);
    void mirror_guanxing_move(int from, int to);
    void mirror_guanxing_finish();
    void gongxin(const QList<int> &card_ids, bool enable_heart, QList<int> enabled_ids);
    void guhuoBox(const QString &phase, const QString &yuji, const QString &declared, int realId);
    void focus_moved(const QStringList &focus, QSanProtocol::Countdown countdown, int command);
    void emotion_set(const QString &target, const QString &emotion);
    void change_table_bg(const QString &tableBg);
    void skill_invoked(const QString &who, const QString &skill_name);
    void skill_acquired(const ClientPlayer *player, const QString &skill_name);
    void animated(int name, const QStringList &args);
    void player_speak(const QString &who, const QString &text);
    void line_spoken(const QString &line);
    void card_used();

    void game_started();
    void game_over();
    void standoff();
    void event_received(const QVariant &);

    void move_cards_lost(int moveId, QList<CardsMoveStruct> moves);
    void move_cards_got(int moveId, QList<CardsMoveStruct> moves);

    void skill_attached(const ClientPlayer *player, const QString &skill_name);
    void skill_detached(const ClientPlayer *player, const QString &skill_name);
    void skill_instances_reset();
    void skill_instance_amount_changed(const ClientPlayer *player, const QString &skill_name,
                                       int instance_id);
    void skill_instance_correct_state_changed(const ClientPlayer *player, const QString &skill_name,
                                              int instance_id, const QString &key);
    void skill_instance_state_changed(const ClientPlayer *player, const QString &skill_name,
                                      int instance_id, const QString &key);
    void do_filter();

    void nullification_asked(bool asked);
    void surrender_enabled(bool enabled);

    void ag_filled(const QList<int> &card_ids, const QList<int> &disabled_ids);
    void ag_taken(ClientPlayer *taker, int card_id, bool move_cards);
    void ag_cleared();

    void generals_filled(const QStringList &general_names);
    void general_taken(const QString &who, const QString &name, const QString &rule);
    void general_asked();
    void arrange_started(const QString &to_arrange);
    void general_recovered(int index, const QString &name);
    void general_revealed(bool self, const QString &general);

    void role_state_changed(const QString &state_str);
    void generals_viewed(const QString &reason, const QStringList &names);
    void card_tip();
    void switch_control_context(const QString &target_name);

    void assign_asked();
    void start_in_xs();

    void skill_updated(const QString &skill_name);
    void anytime_skill_done(const QString &skill_name);
    void qml_interact(const QString &qmlPath, const QVariantMap &params);
};

extern Client *ClientInstance;

#endif
