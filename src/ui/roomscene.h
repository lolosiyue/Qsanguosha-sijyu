#ifndef _ROOM_SCENE_H
#define _ROOM_SCENE_H

//#include "photo.h"
//#include "dashboard.h"
//#include "table-pile.h"
//#include "card.h"
#include "client.h"
#include "build-features.h"
//#include "aux-skills.h"
//#include "clientlogbox.h"
//#include "chatwidget.h"
#include "skin-bank.h"
//#include "sprite.h"
#include "timed-progressbar.h"
#include "room-layout-engine.h"
#include <QMargins>
#include <QPointer>

namespace RoomLayoutEngine {
struct Input;
struct Result;
}
#if QSAN_ENABLE_SPINE
#include "CharacterSpineActionController.h"
#endif

class Window;
class Button;
class CardContainer;
class PileContainer;
class GuanxingBox;
class GuanxingXBox;
class GuhuoBox;
class QSanButton;
class QGroupBox;
class BubbleChatBox;
class ChooseTriggerOrderBox;
struct RoomLayout;
class Photo;
class Dashboard;
class GenericCardContainer;
class TablePile;
class PlayerCardContainer;
class ResponseSkill;
class ShowOrPindianSkill;
class DiscardSkill;
class NosYijiViewAsSkill;
class ChoosePlayerSkill;
class ClientLogBox;
class ChatWidget;
class EmotionPanel;
class GifChatBox;
class RoomOverlayHost;
class KofArrangeController;
class RoomReplayController;
class QSanSelectableItem;
class EffectAnimation;
class GiftItem;
class SpineGlItem;
class PlayerCardBox;
class DesktopGamePresentation;
class LargeRoomOverview;
class QMovie;

#if !defined(Q_OS_WINRT) && QSAN_ENABLE_QML
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#endif

class KOFOrderBox : public QGraphicsPixmapItem
{
public:
    KOFOrderBox(bool self, QGraphicsScene *scene);
    void revealGeneral(const QString &name);
    void killPlayer(const QString &general_name);

private:
    QSanSelectableItem *avatars[3];
    int revealed;
};

class PromptInfoItem : public QGraphicsTextItem
{
public:
    explicit PromptInfoItem(QGraphicsItem *parent = 0);

    //virtual QRectF boundingRect() const;
    //virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *);
    virtual void setHtml(const QString &painter);

};

class RoomScene : public QGraphicsScene
{
    Q_OBJECT
    friend class DesktopGamePresentation;
    friend class ReplayerControlBar;
    friend class LocalResponseUiProbe;
    friend class NetworkUiSmokeController;
    friend class NetworkUiSmokeResponder;

public:
    enum ShefuAskState
    {
        ShefuAskAll, ShefuAskNecessary, ShefuAskNone
    };

    RoomScene(QMainWindow *main_window);
    ~RoomScene();
    void showGameStateSnapshot();
    void showGameControlPanel();
    void changeTextEditBackground();
    void adjustItems();
    void adjustItems(const QSizeF &viewportSize);
#if !defined(QSAN_XP_LEGACY)
    void attachOverlay(RoomOverlayHost *overlay);
    void setResponsiveLayout(const RoomLayoutEngine::ResponsiveInput &input, bool enabled);
    const RoomLayoutEngine::ResponsiveResult &responsiveLayout() const { return m_responsiveLayout; }
    bool largeRoomRequired() const { return photos.size() >= 20 && photos.size() <= 49; }
    DesktopGamePresentation *gamePresentation();
#endif
    void applyUiElementScale(qreal scale);
    void setTouchUiEnabled(bool enabled);
    void setSafeAreaMargins(const QMargins &margins);
    bool touchUiEnabled() const;
    void setApplicationSuspended(bool suspended, bool offline);
    void refreshTouchTargets(qreal viewportScale);
    void showIndicator(const QString &from, const QString &to);
    void showPromptBox();
    void closeAllDialogs();
    static void FillPlayerNames(QComboBox *ComboBox, bool add_none);
    void updateTable();
    void updateVolumeConfig();
    void redrawDashboardButtons();
    void layoutDashboardButtons(const RoomLayoutEngine::DashboardGeometry &geometry);
    const ClientPlayer *getDashboardPlayer() const;
    inline QMainWindow *mainWindow()
    {
        return main_window;
    }

    inline QPointF tableCenterPos() const
    {
        return m_tableCenterPos;
    }

    inline QList<int> getRenPile() const
    {
        return RenPile;
    }

    inline bool isCancelButtonEnabled() const
    {
        return cancel_button != nullptr && cancel_button->isEnabled();
    }
    inline void setGuhuoLog(const QString &log)
    {
        guhuo_log = log;
    }
    bool m_skillButtonSank;
    ShefuAskState m_ShefuAskState;

public slots:
    void addPlayer(ClientPlayer *player);
    void removePlayer(const QString &player_name);
    void updateSkillButtons(bool isPrepare = false);
    void loseCards(int moveId, QList<CardsMoveStruct> moves);
    void getCards(int moveId, QList<CardsMoveStruct> moves);
    void keepLoseCardLog(const CardsMoveStruct &move);
    void keepGetCardLog(const CardsMoveStruct &move);
    // choice dialog
    void chooseGeneral(const QStringList &generals);
    void chooseSuit(const QStringList &suits);
    void chooseCard(const ClientPlayer *playerName, const QString &flags, const QString &reason,
        bool handcard_visible, Card::HandlingMethod method, QList<int> disabled_ids, bool can_cancel);
    void chooseKingdom(const QStringList &kingdoms);
    QGroupBox *createOptionBox(const QString &skillName, const QStringList &options, QDialog *dialog, bool enabled = true);
    void chooseOption(const QString &skillName, const QStringList &options, const QString &except_options, const QString &tip);
    void chooseOrder(QSanProtocol::Game3v3ChooseOrderCommand reason);
    void chooseRole(const QString &scheme, const QStringList &roles);
    void chooseDirection();
    void chooseTriggerOrder(const QVariantList &options, bool optional);

    void bringToFront(QGraphicsItem *item);
    void arrangeSeats(const QList<const ClientPlayer *> &seats);
    void toggleDiscards();
    void enableTargets(const Card *card);
    void useSelectedCard();
    void updateStatus(Client::Status oldStatus, Client::Status newStatus);
    void killPlayer(const QString &who);
    void revivePlayer(const QString &who);
    void updateAreas(const QString &who);
    void updateHandcards(const QString &who);
    void updateCardDescription(const QString &player_name, const QString &card_name);
    void showServerInformation();
    void surrender();
    void saveReplayRecord();
    void makeDamage();
    void changeState();
    void makeKilling();
    void makeReviving();
    void doScript();
    void viewGenerals(const QString &reason, const QStringList &names);

    void handleGameEvent(const QVariant &arg);

    void doOkButton();
    void doCancelButton();
    void doDiscardButton();

    void doGongxin(const QList<int> &card_ids, bool enable_heart, QList<int> enabled_ids);
    void showPile(const QList<int> &card_ids, const QString &pile_name);
    void hidePile();

    void setChatBoxVisibleSlot();
    void onEmotionIconSelected(int emotionId);
    void showTouchCardPreview(CardItem *card);
    void pause();

    void addRobot();
    void doAddRobotAction();
    void fillRobots();

    void onPlayerSelectedForGift();
    void exitGiftSelectionMode();
    void handleGiftSelection(Photo *photo);

protected:
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *event);
    virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);
    virtual void keyReleaseEvent(QKeyEvent *event);
    //this method causes crashes
    virtual void contextMenuEvent(QGraphicsSceneContextMenuEvent *event);

private:
    RoomLayoutEngine::Input layoutInput(const QRectF &viewport, bool clampScene) const;
    void applyLayout(const RoomLayoutEngine::Result &layout);
    void applyTableLayout(const RoomLayoutEngine::Result &layout);
#if !defined(QSAN_XP_LEGACY)
    DesktopGamePresentation *m_gamePresentation = nullptr;
    QPointer<RoomOverlayHost> m_overlayHost;
    bool m_responsiveEnabled = false;
    bool m_legacyPromptVisible = false;
    RoomLayoutEngine::ResponsiveInput m_responsiveInput;
    RoomLayoutEngine::ResponsiveResult m_responsiveLayout;
    LargeRoomOverview *m_largeRoomOverview = nullptr;
    void applyResponsiveLayout();
#endif
    bool _shouldIgnoreDisplayMove(CardsMoveStruct &movement);
    QString _describeMoveForDiagnostics(const CardsMoveStruct &move) const;
    bool _processCardsMove(CardsMoveStruct &move, bool isLost);
    bool _m_isInDragAndUseMode;
    bool _m_superDragStarted;
    const QSanRoomSkin::RoomLayout *_m_roomLayout;
    const QSanRoomSkin::PhotoLayout *_m_photoLayout;
    const QSanRoomSkin::CommonLayout *_m_commonLayout;
    const QSanRoomSkin* _m_roomSkin;
    QGraphicsItem *_m_last_front_item;
    double _m_last_front_ZValue;
    GenericCardContainer *_getGenericCardContainer(Player::Place place, const Player *player = nullptr);
    QMap<int, QList<QList<CardItem *> > > _m_cardsMoveStash;
    Button *add_robot, *start_game, *return_to_main_menu;
    QList<Photo *> photos;
    QMap<QString, Photo *> name2photo;
    Dashboard *dashboard;
    TablePile *m_tablePile;
    QMainWindow *main_window;
    QSanButton *ok_button, *cancel_button, *discard_button;
    QSanButton *trust_button;
    bool m_touchUiEnabled = false;
    QMargins m_safeAreaMargins;
    bool m_applicationSuspended = false;
    bool m_suspendOffline = false;
    bool m_timerPausedByApplication = false;
    bool m_dashboardEnabledBeforeSuspend = true;
    QList<bool> m_photoEnabledBeforeSuspend;
    QList<QPointer<QAbstractAnimation>> m_appPausedAnimations;
    QList<QPointer<QMovie>> m_appPausedMovies;
    QMenu *miscellaneous_menu, *change_general_menu;
    Window *prompt_box, *pindian_box;
    CardItem *pindian_from_card, *pindian_to_card;
    QGraphicsItem *control_panel;
    QMap<PlayerCardContainer *, const ClientPlayer *> item2player;
    QDialog *m_choiceDialog; // Dialog for choosing generals, suits, card/equip, or kingdoms
    PlayerCardBox *m_playerCardBox;

    int m_autoPickGeneralAskCount = 0; // 自動化測試: 本次遊戲第幾次選將詢問 (雙將模式第 2 次 = 副將)

    QGraphicsRectItem *pausing_item;
    QGraphicsSimpleTextItem *pausing_text;

    QString guhuo_log, onsole_target, onsole_owner;

    QList<QGraphicsPixmapItem *> role_items;
    QString m_roleState;
    CardContainer *card_container;
    PileContainer *pileContainer;

    QList<QSanSkillButton *> m_skillButtons;
    QSanSkillButton *m_presentedDialogSkillButton;
    QDialog *m_presentedDialog;
    quint64 m_presentedDialogRequest = 0;

    ResponseSkill *response_skill;
    ShowOrPindianSkill *showorpindian_skill;
    DiscardSkill *discard_skill;
    NosYijiViewAsSkill *yiji_skill;
    ChoosePlayerSkill *choose_skill;

    QList<const Player *> selected_targets;

	QList<int> RenPile;

	ChooseTriggerOrderBox *m_chooseTriggerOrderBox;

	GuanxingBox *m_guanxingBox;
	GuhuoBox *m_guhuoBox;

	QList<CardItem *> gongxin_items;

    ClientLogBox *log_box;
    GifChatBox *chat_box;
    QLineEdit *chat_edit;
    QGraphicsProxyWidget *chat_box_widget;
    QGraphicsProxyWidget *log_box_widget;
    QGraphicsProxyWidget *chat_edit_widget;
    QGraphicsTextItem *prompt_box_widget;
    ChatWidget *chat_widget;
    EmotionPanel *m_emotionPanel;
    QPixmap m_rolesBoxBackgroundOrig;
    QPixmap m_rolesBoxBackground;
    QGraphicsPixmapItem *m_rolesBox;
    QGraphicsTextItem *m_pileCardNumInfoTextBox;

    QGraphicsPixmapItem *m_tableBg;
    QPixmap m_tableBgPixmap;
    QPixmap m_tableBgPixmapOrig;
    qreal m_pixmapDeviceScale;
    int m_tablew;
    int m_tableh;
    int m_photoWidth;
    int m_photoHeight;
    qreal m_photoScale;

    QMenu *m_add_robot_menu;

    TimerLabel *m_timerLabel;

    QMap<QString, BubbleChatBox *> bubbleChatBoxes;

    bool gift_selection_mode;
    QString current_gift_type;
    QList<Photo *> gift_highlighted_photos;

    // for 3v3 & 1v1 mode
    KofArrangeController *m_kofArrange;
    KOFOrderBox *enemy_box, *self_box;
    QPointF m_tableCenterPos;
    RoomReplayController *m_replay;
    QAction *m_switchPerspectiveAction;
    QString m_currentPerspective;

    struct _MoveCardsClassifier
    {
        inline _MoveCardsClassifier(const CardsMoveStruct &move)
        {
            m_card_ids = move.card_ids;
        }
        inline bool operator ==(const _MoveCardsClassifier &other) const
        {
            return m_card_ids == other.m_card_ids;
        }
        inline bool operator <(const _MoveCardsClassifier &other) const
        {
            return m_card_ids.first() < other.m_card_ids.first();
        }
        QList<int> m_card_ids;
    };

    QMap<_MoveCardsClassifier, CardsMoveStruct> m_move_cache;

    // @todo: this function shouldn't be here. But it's here anyway, before someone find a better
    // home for it.
    QString _translateMovement(const CardsMoveStruct &move);

    void useCard(const Card *card);
    void fillTable(QTableWidget *table, const QList<const ClientPlayer *> &players);
    void chooseSkillButton();

    void selectTarget(int order, bool multiple);
    void selectNextTarget(bool multiple);
    void unselectAllTargets(const QGraphicsItem *except = nullptr);
    void updateTargetsEnablity(const Card *card = nullptr);
    QString currentOnsoleTarget() const;
    void enterOnsoleContext(const QString &target_name);
    void exitOnsoleContext();

    void callViewAsSkill();
    void cancelViewAsSkill();

    void freeze();
    void addRestartButton(QDialog *dialog);
    QGraphicsPixmapItem *createDashboardButtons();

    void showPindianBox(const QString &from_name, int from_id, const QString &to_name, int to_id, const QString &reason);
    void setChatBoxVisible(bool show);
    QRect getBubbleChatBoxShowArea(const QString &who) const;

    // animation related functions
    typedef void (RoomScene::*AnimationFunc)(const QString &, const QStringList &);
    QGraphicsObject *getAnimationObject(const QString &name) const;

    void doMovingAnimation(const QString &name, const QStringList &args);
    void doAppearingAnimation(const QString &name, const QStringList &args);
    void doLightboxAnimation(const QString &name, const QStringList &args);
    void doHuashen(const QString &name, const QStringList &args);
    void doIndicate(const QString &name, const QStringList &args);
    void doGiftAnimation(const QString &name, const QStringList &args);

    EffectAnimation *animations;
    bool pindian_success;

    // ─── Spine pop-out action controller ────────────────────────
#if QSAN_ENABLE_SPINE
    CharacterSpineActionController *_spineActionController = nullptr;

    /// Active fullscreen SpineGlItem instances (for resize handling)
    QList<SpineGlItem *> _activeSpineItems;

    /// Register skins for all known players whose generals have dynamic skins.
    void registerDynamicSkinsForAllPlayers();

    /// Register dynamic skin for one player (primary or deputy general).
    void registerDynamicSkinForPlayer(const QString &playerName,
                                      const QString &generalName,
                                      bool isPrimary);

    /// Update _spineActionController seat geometry for all photos + dashboard.
    void updateSpineSeatGeometry();
#endif

    // re-layout attempts
    bool game_started;

    void _cancelAllFocus();
    bool isPrimarySkill(const Skill *skill) const;
    // for miniscenes
    int _m_currentStage;

    QRectF _m_infoPlane;
    QSize m_logSizeWithChat = QSize(0, 0);
    QSize m_logSizeWithoutChat = QSize(0, 0);

    bool _m_bgEnabled;
    QString _m_bgMusicPath;

    bool shouldUseDashboardDialogPresenter(QDialog *dialog) const;
    void wireSkillDialog(QSanSkillButton *button, QDialog *dialog);
    void presentSkillDialog(QSanSkillButton *button, QDialog *dialog);
    void clearPresentedDialogSkill(bool resetButtonState = false);
    void activateSkill(const ViewAsSkill *skill);
    bool applyPresentedDialogOption(const QString &optionName);
    bool isPresentedDialogOptionEnabled(const QString &optionName) const;

#if !defined(Q_OS_WINRT) && QSAN_ENABLE_QML
    // for animation effects
    QQmlEngine *_m_animationEngine;
    QQmlContext *_m_animationContext;
    QQmlComponent *_m_animationComponent;
#endif

private slots:
    void fillCards(const QList<int> &card_ids, const QList<int> &disabled_ids = QList<int>());
    void acquireSkill(const ClientPlayer *player, const QString &skill_name);
    void updateSelectedTargets();
    void updateTrustButton();
    void onSkillActivated();
    void onPresentedDialogSkillActivated();
    void onAnytimeSkillActivated();
    void onAnytimeSkillDone(const QString &skill_name);
#if QSAN_ENABLE_QML
    void onQmlInteract(const QString &qmlPath, const QVariantMap &params);
    void onQmlResultReady(const QVariant &result);
#endif
    void onSkillDeactivated();
    void onDialogOptionSelectionChanged(bool hasSelection);
    void doTimeout();
    void startInXs();
    void hideAvatars();
    void changeHp(const QString &who, int delta, int nature, int losthj);
    void changeMaxHp(const QString &who, int delta);
    void moveFocus(const QStringList &who, QSanProtocol::Countdown, int command);
    void setEmotion(const QString &who, const QString &emotion);
    void changeTableBg(const QString &tableBg);
    void showSkillInvocation(const QString &who, const QString &skill_name);
    void doAnimation(int name, const QStringList &args);
    void showOwnerButtons(bool owner);
    void showPlayerCards();
    void updateRolesBox();
    void updateRoles(const QString &roles);
    void addSkillButton(const Skill *skill);
    void addSkillButton(const QString &skillInstanceName);
    void refreshSkillInstanceButtonLabels(const QString &baseName);

    void resetPiles();
    void removeLightBox();

    void showCard(const QString &player_name, QList<int> card_ids);
    void showVirtualCard(const QString &player_name, const QString &card_name,
        const QString &suit, int number, const QString &skill_name, const QList<int> &subcard_ids,
        const QString &target_name);
    void viewDistance();
    void viewMaxCards();

    void speak();

void onGameStart();
    void onGameOver();
    void onStandoff();

    void appendChatEdit(QString txt);
    void showBubbleChatBox(const QString &who, const QString &line);
    void showGeneralPile(const QString &tag_name);

    void onGiftModeActivated(const QString &gift_type);

    //animations
    void onEnabledChange();

    void takeAmazingGrace(ClientPlayer *taker, int card_id, bool move_cards);

    void attachSkill(const ClientPlayer *player, const QString &skill_name);
    void attachSkill(const QString &skill_name);
    void detachSkill(const ClientPlayer *player, const QString &skill_name);
    void detachSkill(const QString &skill_name);
    void onSkillButtonDestroyed(QObject *button);
    void updateSkill(const QString &skill_name);

    void hideContainer();
    void switchControlContext(const QString &target_name);
    void switchReplayPerspective(const QString &player_name);

    void startAssign();

    void doPindianAnimation();

    // 3v3 mode & 1v1 mode
    void revealGeneral(bool self, const QString &general);
    void trust();

    void onCardActionButtonClicked(const QString &buttonId, int cardId);


signals:
    void takeoverRequested(const QString &snapshotPath, const QString &seatObjectName);
    void responsiveGeometryChanged();
    void seatCountChanged();
    void restart();
    void return_to_start();
    void game_over_dialog_rejected();
};

extern RoomScene *RoomSceneInstance;

#endif
