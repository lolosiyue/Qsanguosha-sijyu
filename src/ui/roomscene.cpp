#include "roomscene.h"
//#include "settings.h"
#include "carditem.h"
#include "engine.h"
#include "oracle_helper.h"
#include "cardoverview.h"
#include "distanceviewdialog.h"
#include "maxcardsviewdialog.h"
#include "playercarddialog.h"
#include "choosegeneraldialog.h"
#include "window.h"
#include "button.h"
#include "cardcontainer.h"
#include "recorder.h"
#include "indicatoritem.h"
#include "generaloverview.h"
#include "pixmapanimation.h"
#include "audio.h"
//#include "skin-bank.h"
#include "record-analysis.h"
#include "mountain.h"
#include "bubblechatbox.h"
#include "mountain.h"
//#include "clientplayer.h"
#include "clientstruct.h"
#include "photo.h"
#include "dashboard.h"
#include "table-pile.h"
#include "aux-skills.h"
#include "clientlogbox.h"
#include "chatwidget.h"
#include "emotionpanel.h"
#include "gifchatbox.h"
#include "giftitem.h"
#include "sprite.h"
#include "EmbeddedQmlLoader.h"
#include "SpineGlItem.h"
#include "graphicspixmaphoveritem.h"
#include <QOpenGLWidget>
#include <QMutexLocker>

#include "ui-utils.h"

using namespace QSanProtocol;

//RoomScene*RoomSceneInstance = nullptr;
RoomScene*RoomSceneInstance;

void RoomScene::resetPiles()
{
	// @todo: fix this...
}

#include "qsanbutton.h"


RoomScene::RoomScene(QMainWindow*main_window)
	: main_window(main_window),m_tableBgPixmap(1,1),m_tableBgPixmapOrig(1,1),game_started(false)
{
	setParent(main_window);

	m_choiceDialog = nullptr;
	RoomSceneInstance = this;
	_m_last_front_item = nullptr;
	_m_last_front_ZValue = 0;
	ren_widget = nullptr;

	_m_roomSkin =&(QSanSkinFactory::getInstance().getCurrentSkinScheme().getRoomSkin());
	_m_roomLayout =&(G_ROOM_SKIN.getRoomLayout());
	_m_photoLayout =&(G_ROOM_SKIN.getPhotoLayout());
	_m_commonLayout =&(G_ROOM_SKIN.getCommonLayout());

	m_skillButtonSank = false;
	m_ShefuAskState = ShefuAskNecessary;
	guhuo_log = "";
	gift_selection_mode = false;
	current_gift_type = QString();

	// create photos
	for (int i = 1;i < Sanguosha->getPlayerCount(ServerInfo.GameMode);i++){
		Photo*photo = new Photo;
		photo->setZValue(1);
		photos << photo;
		addItem(photo);
	}

	// create table pile
	m_tablePile = new TablePile;
	//m_tablePile->setZValue(0);
	addItem(m_tablePile);
	connect(ClientInstance,SIGNAL(card_used()),m_tablePile,SLOT(clear()));

	// create dashboard
	dashboard = new Dashboard(createDashboardButtons());
	dashboard->setObjectName("dashboard");
	dashboard->setZValue(2);
	addItem(dashboard);

	dashboard->setPlayer(Self);
	connect(Self,SIGNAL(general_changed()),dashboard,SLOT(updateAvatar()));
	connect(Self,SIGNAL(general2_changed()),dashboard,SLOT(updateSmallAvatar()));
	connect(dashboard,SIGNAL(card_selected(const Card*)),this,SLOT(enableTargets(const Card*)));
	connect(dashboard,SIGNAL(card_to_use()),this,SLOT(doOkButton()));
	//connect(dashboard,SIGNAL(add_equip_skill(const Skill*,bool)),this,SLOT(addSkillButton(const Skill*,bool)));
	//connect(dashboard,SIGNAL(remove_equip_skill(QString)),this,SLOT(detachSkill(QString)));

	connect(Self,SIGNAL(pile_changed(QString)),dashboard,SLOT(updatePile(QString)));

	// add role ComboBox
	connect(Self,SIGNAL(role_changed(QString)),dashboard,SLOT(updateRole(QString)));

	m_replayControl = nullptr;
	if(ClientInstance->getReplayer()){
		dashboard->hideControlButtons();
		createReplayControlBar();
	}

	response_skill = new ResponseSkill;
	response_skill->setParent(this);
	showorpindian_skill = new ShowOrPindianSkill;
	showorpindian_skill->setParent(this);
	discard_skill = new DiscardSkill;
	discard_skill->setParent(this);
	yiji_skill = new NosYijiViewAsSkill;
	yiji_skill->setParent(this);
	choose_skill = new ChoosePlayerSkill;
	choose_skill->setParent(this);

	miscellaneous_menu = new QMenu(main_window);

	change_general_menu = new QMenu(main_window);
	to_change = nullptr;

	m_add_robot_menu = new QMenu(main_window);

	// do signal-slot connections
	connect(ClientInstance,SIGNAL(player_added(ClientPlayer*)),SLOT(addPlayer(ClientPlayer*)));
	connect(ClientInstance,SIGNAL(player_removed(QString)),SLOT(removePlayer(QString)));
	connect(ClientInstance,SIGNAL(generals_got(QStringList)),this,SLOT(chooseGeneral(QStringList)));
	connect(ClientInstance,SIGNAL(generals_viewed(QString,QStringList)),this,SLOT(viewGenerals(QString,QStringList)));
	connect(ClientInstance,SIGNAL(suits_got(QStringList)),this,SLOT(chooseSuit(QStringList)));
	connect(ClientInstance,SIGNAL(options_got(QString,QStringList,QString,QString)),this,
			SLOT(chooseOption(QString,QStringList,QString,QString)));
	connect(ClientInstance,SIGNAL(cards_got(const ClientPlayer*,QString,QString,bool,Card::HandlingMethod,QList<int>,bool)),
		this,SLOT(chooseCard(const ClientPlayer*,QString,QString,bool,Card::HandlingMethod,QList<int>,bool)));
	connect(ClientInstance,SIGNAL(roles_got(QString,QStringList)),this,SLOT(chooseRole(QString,QStringList)));
	connect(ClientInstance,SIGNAL(directions_got()),this,SLOT(chooseDirection()));
	connect(ClientInstance,SIGNAL(orders_got(QSanProtocol::Game3v3ChooseOrderCommand)),this,SLOT(chooseOrder(QSanProtocol::Game3v3ChooseOrderCommand)));
	connect(ClientInstance,SIGNAL(kingdoms_got(QStringList)),this,SLOT(chooseKingdom(QStringList)));
	connect(ClientInstance,SIGNAL(seats_arranged(QList<const ClientPlayer*>)),SLOT(arrangeSeats(QList<const ClientPlayer*>)));
	connect(ClientInstance,SIGNAL(status_changed(Client::Status,Client::Status)),this,SLOT(updateStatus(Client::Status,Client::Status)));
	connect(ClientInstance,SIGNAL(avatars_hiden()),this,SLOT(hideAvatars()));
	connect(ClientInstance,SIGNAL(hp_changed(QString,int,int,int)),SLOT(changeHp(QString,int,int,int)));
	connect(ClientInstance,SIGNAL(maxhp_changed(QString,int)),SLOT(changeMaxHp(QString,int)));
	connect(ClientInstance,SIGNAL(pile_reset()),this,SLOT(resetPiles()));
	connect(ClientInstance,SIGNAL(update_areas(QString)),this,SLOT(updateAreas(QString)));
	connect(ClientInstance,SIGNAL(update_handcards(QString)),this,SLOT(updateHandcards(QString)));
	//connect(ClientInstance,SIGNAL(round_add()),this,SLOT(addRound()));
	connect(ClientInstance,SIGNAL(player_killed(QString)),this,SLOT(killPlayer(QString)));
	connect(ClientInstance,SIGNAL(player_revived(QString)),this,SLOT(revivePlayer(QString)));
	connect(ClientInstance,SIGNAL(card_shown(QString,QList<int>)),this,SLOT(showCard(QString,QList<int>)));
	connect(ClientInstance,SIGNAL(gongxin(QList<int>,bool,QList<int>)),this,SLOT(doGongxin(QList<int>,bool,QList<int>)));
	connect(ClientInstance,SIGNAL(focus_moved(QStringList,QSanProtocol::Countdown,int)),this,SLOT(moveFocus(QStringList,QSanProtocol::Countdown,int)));
	connect(ClientInstance,SIGNAL(emotion_set(QString,QString)),this,SLOT(setEmotion(QString,QString)));
	connect(ClientInstance,SIGNAL(change_table_bg(QString)),this,SLOT(changeTableBg(QString)));
	connect(ClientInstance,SIGNAL(skill_invoked(QString,QString)),this,SLOT(showSkillInvocation(QString,QString)));
	connect(ClientInstance,SIGNAL(skill_acquired(const ClientPlayer*,QString)),this,SLOT(acquireSkill(const ClientPlayer*,QString)));
	connect(ClientInstance,SIGNAL(animated(int,QStringList)),this,SLOT(doAnimation(int,QStringList)));
	connect(ClientInstance,SIGNAL(role_state_changed(QString)),this,SLOT(updateRoles(QString)));
	connect(ClientInstance,SIGNAL(event_received(const QVariant)),this,SLOT(handleGameEvent(const QVariant)));

	connect(ClientInstance,SIGNAL(game_started()),this,SLOT(onGameStart()));
	connect(ClientInstance,SIGNAL(game_over()),this,SLOT(onGameOver()));
	connect(ClientInstance,SIGNAL(standoff()),this,SLOT(onStandoff()));

	connect(ClientInstance,SIGNAL(move_cards_lost(int,QList<CardsMoveStruct>)),this,SLOT(loseCards(int,QList<CardsMoveStruct>)));
	connect(ClientInstance,SIGNAL(move_cards_got(int,QList<CardsMoveStruct>)),this,SLOT(getCards(int,QList<CardsMoveStruct>)));

	connect(ClientInstance,SIGNAL(nullification_asked(bool)),dashboard,SLOT(controlNullificationButton(bool)));
	connect(ClientInstance,SIGNAL(card_tip()),dashboard,SLOT(cardTip()));

	connect(ClientInstance,SIGNAL(assign_asked()),this,SLOT(startAssign()));
	connect(ClientInstance,SIGNAL(start_in_xs()),this,SLOT(startInXs()));

	connect(ClientInstance,&Client::skill_updated,this,&RoomScene::updateSkill);
	connect(ClientInstance,SIGNAL(card_description_updated(QString)),this,SLOT(updateCardDescription(QString)));

	guanxing_x_box = new GuanxingXBox;
	guanxing_x_box->hide();
	addItem(guanxing_x_box);
	guanxing_x_box->setZValue(21);

	connect(ClientInstance,SIGNAL(guanxing(QList<int>,int)),guanxing_x_box,SLOT(doGuanxing(QList<int>,int)));
	guanxing_x_box->moveBy(-120,0);

	guanxing_box3 = new GuanxingBox("image/system/guanxing-box3.png");
	guanxing_box3->hide();
	guanxing_box3->setZValue(21);
	guanxing_box3->moveBy(-120,0);
	guanxing_x_box->addBox3(guanxing_box3);
	addItem(guanxing_box3);
	guanxing_box7 = new GuanxingBox("image/system/guanxing-box7.png");
	guanxing_box7->hide();
	guanxing_box7->setZValue(21);
	guanxing_box7->moveBy(-120,0);
	guanxing_x_box->addBox7(guanxing_box7);
	addItem(guanxing_box7);
	guanxing_box9 = new GuanxingBox("image/system/guanxing-box9.png");
	guanxing_box9->hide();
	guanxing_box9->setZValue(21);
	guanxing_box9->moveBy(-120,0);
	guanxing_x_box->addBox9(guanxing_box9);
	addItem(guanxing_box9);

	card_container = new CardContainer();
	card_container->hide();
	addItem(card_container);
	card_container->setZValue(12);

	connect(card_container,SIGNAL(item_chosen(int)),ClientInstance,SLOT(onPlayerChooseAG(int)));
	connect(card_container,SIGNAL(item_gongxined(int)),ClientInstance,SLOT(onPlayerReplyGongxin(int)));

	connect(ClientInstance,SIGNAL(ag_filled(QList<int>,QList<int>)),this,SLOT(fillCards(QList<int>,QList<int>)));
	connect(ClientInstance,SIGNAL(ag_taken(ClientPlayer*,int,bool)),this,SLOT(takeAmazingGrace(ClientPlayer*,int,bool)));
	connect(ClientInstance,SIGNAL(ag_cleared()),card_container,SLOT(clear()));

	card_container->moveBy(-120,0);

	connect(ClientInstance,SIGNAL(skill_attached(QString)),this,SLOT(attachSkill(QString)));
	connect(ClientInstance,SIGNAL(skill_detached(QString)),this,SLOT(detachSkill(QString)));

	enemy_box = nullptr;
	self_box = nullptr;

	if(ServerInfo.GameMode=="06_3v3"||ServerInfo.GameMode=="02_1v1"||ServerInfo.GameMode=="06_XMode"){
		if(ServerInfo.GameMode!="06_XMode"){
			connect(ClientInstance,SIGNAL(generals_filled(QStringList)),this,SLOT(fillGenerals(QStringList)));
			connect(ClientInstance,SIGNAL(general_asked()),this,SLOT(startGeneralSelection()));
			connect(ClientInstance,SIGNAL(general_taken(QString,QString,QString)),this,SLOT(takeGeneral(QString,QString,QString)));
			connect(ClientInstance,SIGNAL(general_recovered(int,QString)),this,SLOT(recoverGeneral(int,QString)));
		}
		connect(ClientInstance,SIGNAL(arrange_started(QString)),this,SLOT(startArrange(QString)));

		QAction*action = change_general_menu->addAction(tr("Change general ..."));
		static FreeChooseDialog*general_changer = new FreeChooseDialog("",main_window);
		connect(action,SIGNAL(triggered()),general_changer,SLOT(exec()));
		connect(general_changer,SIGNAL(general_chosen(QString)),this,SLOT(changeGeneral(QString)));

		arrange_button = nullptr;

		if(ServerInfo.GameMode=="02_1v1"){
			enemy_box = new KOFOrderBox(false,this);
			self_box = new KOFOrderBox(true,this);

			enemy_box->hide();
			self_box->hide();

			connect(ClientInstance,SIGNAL(general_revealed(bool,QString)),this,SLOT(revealGeneral(bool,QString)));
		}
	}

	// chat box
	chat_box = new GifChatBox;
	chat_box->setObjectName("chat_box");
	chat_box_widget = addWidget(chat_box);
	chat_box_widget->setObjectName("chat_box_widget");
	chat_box_widget->setZValue(7);
	chat_box->setReadOnly(true);
	chat_box->setStyleSheet(QString("QTextEdit { color: %1;}").arg(Config.TextEditColor.name()));
	connect(ClientInstance,SIGNAL(line_spoken(QString)),chat_box,SLOT(append(QString)));
	connect(ClientInstance,SIGNAL(player_speak(const QString&,const QString&)),
		this,SLOT(showBubbleChatBox(const QString&,const QString&)));

	QScrollBar*bar = chat_box->verticalScrollBar();
	QFile file("qss/scroll.qss");
	if(file.open(QIODevice::ReadOnly)){
		QTextStream stream(&file);
		bar->setStyleSheet(stream.readAll());
	}

	// chat edit
	chat_edit = new QLineEdit;
	chat_edit->setObjectName("chat_edit");
	chat_edit->setMaxLength(500);
	chat_edit_widget = addWidget(chat_edit);
	chat_edit_widget->setObjectName("chat_edit_widget");
	chat_edit_widget->setZValue(8);
	connect(chat_edit,SIGNAL(returnPressed()),this,SLOT(speak()));
	chat_edit->setPlaceholderText(tr("Please enter text to chat ... "));

	chat_widget = new ChatWidget();
	chat_widget->setZValue(8);
	addItem(chat_widget);
	connect(chat_widget,SIGNAL(return_button_click()),this,SLOT(speak()));
	connect(chat_widget,SIGNAL(chat_widget_msg(QString)),this,SLOT(appendChatEdit(QString)));
	connect(chat_widget, SIGNAL(gift_mode_activated(QString)), this, SLOT(onGiftModeActivated(QString)));

	m_emotionPanel = new EmotionPanel();
	connect(m_emotionPanel, SIGNAL(emotionSelected(int)), this, SLOT(onEmotionIconSelected(int)));

	if(ServerInfo.DisableChat)
		chat_edit_widget->hide();

	// log box
	log_box = new ClientLogBox;
	log_box->setObjectName("log_box");
	log_box->setTextColor(Config.TextEditColor);

	log_box_widget = addWidget(log_box);
	log_box_widget->setZValue(8);
	log_box_widget->setParent(this);
	log_box_widget->setObjectName("log_box_widget");
	connect(ClientInstance,SIGNAL(log_received(QStringList)),log_box,SLOT(appendLog(QStringList)));

	m_timerLabel = new TimerLabel(this);
	m_timerLabel->resize(60,30);
	m_timerLabel->setStyleSheet("background-color:transparent"); //半透明

	/*prompt_box = new Window(nullptr,QSize(480,200));
	prompt_box->setOpacity(0);
	prompt_box->setFlag(QGraphicsItem::ItemIsMovable);
	prompt_box->shift();
	prompt_box->setZValue(11);
	prompt_box->keepWhenDisappear();

	prompt_box_widget = new PromptInfoItem();
	prompt_box_widget->setParent(prompt_box);
	addItem(prompt_box_widget);
	prompt_box_widget->setDocument(ClientInstance->getPromptDoc());*/

	prompt_box = new Window("提示",QSize(650,200));
	prompt_box->setFlag(QGraphicsItem::ItemIsMovable);
	prompt_box->setOpacity(0);
	prompt_box->shift();
	prompt_box->setZValue(13);
	prompt_box->keepWhenDisappear();

	prompt_box_widget = new QGraphicsTextItem(prompt_box);
	prompt_box_widget->setParent(prompt_box);
	prompt_box_widget->setPos(15,30);
	prompt_box_widget->setDefaultTextColor(Qt::yellow);

	QTextDocument*prompt_doc = ClientInstance->getPromptDoc();
	prompt_doc->setTextWidth(630);//(prompt_box->boundingRect().width());
	prompt_box_widget->setDocument(prompt_doc);

	QFont qf = Config.SmallFont;
	qf.setPixelSize(21);
	qf.setStyleStrategy(QFont::PreferAntialias);
	qf.setFamily("KaiTi");
	qf.setBold(true);
	prompt_box_widget->setFont(qf);

	addItem(prompt_box);

	m_tableBg = new QGraphicsPixmapItem;
	m_tableBg->setZValue(-1);

	addItem(m_tableBg);

	QHBoxLayout*skill_dock_layout = new QHBoxLayout;
	QMargins margins = skill_dock_layout->contentsMargins();
	margins.setTop(0);
	margins.setBottom(5);
	skill_dock_layout->setContentsMargins(margins);
	skill_dock_layout->addStretch();

	m_rolesBoxBackground.load("image/system/state.png");
	m_rolesBox = new QGraphicsPixmapItem;
	addItem(m_rolesBox);
	QString roles = Sanguosha->getRoles(ServerInfo.GameMode);
	m_pileCardNumInfoTextBox = addText("");
	m_pileCardNumInfoTextBox->setParentItem(m_rolesBox);
	m_pileCardNumInfoTextBox->setDocument(ClientInstance->getLinesDoc());
	m_pileCardNumInfoTextBox->setDefaultTextColor(Config.TextEditColor);
	updateRoles(roles);

	control_panel = addRect(0,0,500,150,Qt::NoPen);
	//control_panel->hide();

	add_robot = nullptr;
	start_game = nullptr;
	return_to_main_menu = nullptr;
	if(ServerInfo.EnableAI){
		add_robot = new Button(tr("Add robots"));
		add_robot->setParentItem(control_panel);
		add_robot->setTransform(QTransform::fromTranslate(-add_robot->boundingRect().width()/2,-add_robot->boundingRect().height()/2),true);
		add_robot->setPos(0,-add_robot->boundingRect().height()-10);
		add_robot->hide();

		start_game = new Button(tr("Start new game"));
		start_game->setParentItem(control_panel);
		start_game->setToolTip(tr("Fill robots and start a new game"));
		start_game->setTransform(QTransform::fromTranslate(-start_game->boundingRect().width()/2,-start_game->boundingRect().height()/2),true);
		start_game->setPos(0,0);
		start_game->hide();

		connect(add_robot,SIGNAL(clicked()),this,SLOT(addRobot()));
		connect(start_game,SIGNAL(clicked()),this,SLOT(fillRobots()));
		connect(Self,SIGNAL(owner_changed(bool)),this,SLOT(showOwnerButtons(bool)));
	}

	return_to_main_menu = new Button(tr("Return to main menu"));
	return_to_main_menu->setParentItem(control_panel);
	return_to_main_menu->setTransform(QTransform::fromTranslate(-return_to_main_menu->boundingRect().width()/2,-return_to_main_menu->boundingRect().height()/2),true);
	return_to_main_menu->setPos(0,return_to_main_menu->boundingRect().height()+10);
	return_to_main_menu->show();

	connect(return_to_main_menu,SIGNAL(clicked()),this,SIGNAL(return_to_start()));
	//control_panel->show();

	animations = new EffectAnimation();
	animations->setParent(this);

	// ── Spine pop-out action controller ──
	_spineActionController = new CharacterSpineActionController(this, this);
	_spineActionController->setAssetPathPrefix("assets/dynamic");
	_spineActionController->loadConfigFromJson("assets/dynamic/dynamicSkinConfig.json");
	connect(_spineActionController, &CharacterSpineActionController::actionStarted,
		this, [](const QString &, ActionType) {});
	connect(_spineActionController, &CharacterSpineActionController::actionFinished,
		this, [](const QString &, ActionType) {});
	connect(_spineActionController, &CharacterSpineActionController::skinBackgroundChanged,
		this, [this](const QString &playerId, const QString &bgPath) {
		// Find the matching Photo or Dashboard and set its dynamic background
		if (Self && playerId == Self->objectName()) {
			if (dashboard) dashboard->setDynamicBackground(bgPath);
		} else if (name2photo.contains(playerId)) {
			name2photo[playerId]->setDynamicBackground(bgPath);
		}
	});
	connect(_spineActionController, &CharacterSpineActionController::actionUnavailable,
		this, [](const QString &, ActionType) {});

	pausing_item = new QGraphicsRectItem;
	pausing_text = new QGraphicsSimpleTextItem(tr("Paused ..."));
	addItem(pausing_item);
	addItem(pausing_text);

	pausing_item->setOpacity(0.36);
	pausing_item->setZValue(33);

	QFont font = Config.BigFont;
	font.setPixelSize(100);
	pausing_text->setFont(font);
	pausing_text->setBrush(Qt::white);
	pausing_text->setZValue(34);

	pausing_item->hide();
	pausing_text->hide();

	pindian_box = new Window(tr("pindian"),QSize(255,200),"image/system/pindian.png");
	pindian_box->setFlag(QGraphicsItem::ItemIsMovable);
	pindian_box->setOpacity(0);
	pindian_box->shift();
	pindian_box->setZValue(15);
	pindian_box->keepWhenDisappear();
	addItem(pindian_box);

	pindian_from_card = nullptr;
	pindian_to_card = nullptr;

	_m_bgEnabled = false;

	_m_isInDragAndUseMode = false;
	_m_superDragStarted = false;/*

#ifndef QT_DEBUG
	_m_animationEngine = new QQmlEngine(this);
	_m_animationContext = new QQmlContext(_m_animationEngine->rootContext(),this);
	_m_animationComponent = new QQmlComponent(_m_animationEngine,QUrl::fromLocalFile("ui-script/animation.qml"),this);
#endif*/
}

RoomScene::~RoomScene()
{
	/*if(RoomSceneInstance==this)
		RoomSceneInstance = nullptr;*/

	// Explicitly destroy the spine controller BEFORE the scene tears down its
	// child QGraphicsItems.  Otherwise the controller's destructor tries to
	// remove items from a half-destroyed scene, causing stack corruption.
	if (_spineActionController) {
		delete _spineActionController;
		_spineActionController = nullptr;
	}
}

void RoomScene::handleGameEvent(const QVariant&args)
{
	JsonArray arg = args.value<JsonArray>();
	if(arg.isEmpty()) return;

	switch ((GameEventType)arg[0].toInt()){
	case S_GAME_EVENT_PLAYER_DYING: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->setSaveMeIcon(true);
		Photo*photo = qobject_cast<Photo*>(container);
		if(photo) photo->setFrame(Photo::S_FRAME_SOS);
		break;
	}
	case S_GAME_EVENT_PLAYER_QUITDYING: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->setSaveMeIcon(false);
		Photo*photo = qobject_cast<Photo*>(container);
		if(photo) photo->setFrame(Photo::S_FRAME_NO_FRAME);
		break;
	}
	case S_GAME_EVENT_HUASHEN: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		QString huashenGeneral = arg[2].toString();
		QString huashenSkill = arg[3].toString();
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->startHuaShen(huashenGeneral,huashenSkill);
		break;
	}
	case S_GAME_EVENT_PLAY_EFFECT: {
		int type = arg[3].toInt();
		if(type==0) break;
		QString skillName = arg[1].toString();

		ClientPlayer*player = ClientInstance->getPlayer(arg[4].toString());
		//const Card*card = Sanguosha->findChild<const Card*>(skillName);
		if(player){//&&!card
			const General*general = player->getGeneral();
			if(general&&general->hasSkill(skillName,true)){
				int skin_index = Config.value(QString("HeroSkin/%1").arg(general->objectName()),0).toInt();
				QString actualGn = Sanguosha->getResourceAlias("heroskin", general->objectName());
				if(skin_index > 0){
					QString heroskin = QString("image/heroskin/audio/%1_%2/skill").arg(actualGn).arg(skin_index);
					if(QFile::exists(heroskin)){
						QStringList oggs,files;
						QDir dir(heroskin);
						oggs << "*.ogg";
						foreach (QString file,dir.entryList(oggs,QDir::Files|QDir::Readable,QDir::Name)){
							if(file.startsWith(skillName)&&file.endsWith(".ogg"))
								files << file;
						}
						if(files.length()>0){
							QString file = QString("%1%2.ogg").arg(skillName).arg(type);
							if(!files.contains(file)){
								if(type>0) file = files.at(type%files.length());
								else file = files.at(qrand()%files.length());
							}
							Sanguosha->playAudioEffect(heroskin+"/"+file);
							break;
						}
					}
				}
				type = Sanguosha->revisesAudioType(general->objectName(),skillName,type);
			}
			general = player->getGeneral2();
			if(general&&general->hasSkill(skillName,true)){
				int skin_index = Config.value(QString("HeroSkin/%1").arg(general->objectName()),0).toInt();
				QString actualGn = Sanguosha->getResourceAlias("heroskin", general->objectName());
				if(skin_index > 0){
					QString heroskin = QString("image/heroskin/audio/%1_%2/skill").arg(actualGn).arg(skin_index);
					if(QFile::exists(heroskin)){
						QStringList oggs,files;
						QDir dir(heroskin);
						oggs << "*.ogg";
						foreach (QString file,dir.entryList(oggs,QDir::Files|QDir::Readable,QDir::Name)){
							if(file.startsWith(skillName)&&file.endsWith(".ogg"))
								files << file;
						}
						if(files.length()>0){
							QString file = QString("%1%2.ogg").arg(skillName).arg(type);
							if(!files.contains(file)){
								if(type>0) file = files.at(type%files.length());
								else file = files.at(qrand()%files.length());
							}
							Sanguosha->playAudioEffect(heroskin+"/"+file);
							break;
						}
					}
				}
				type = Sanguosha->revisesAudioType(general->objectName(),skillName,type);
			}
		}
		QString category;
		if(JsonUtils::isBool(arg[2])) category = arg[2].toBool() ? "male" : "female";
		else category = arg[2].toString();

		Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(skillName,category,type));
		break;
	}
	case S_GAME_EVENT_JUDGE_RESULT: {
		int cardId = arg[1].toInt();
		bool takeEffect = arg[2].toBool();
		m_tablePile->showJudgeResult(cardId,takeEffect);
		break;
	}
	case S_GAME_EVENT_DETACH_SKILL: {
		QString player_name = arg[1].toString();
		QString skill_name = arg[2].toString();

		ClientPlayer*player = ClientInstance->getPlayer(player_name);
		player->detachSkill(skill_name);
		if(player==Self) detachSkill(skill_name);

		if(arg.size()>3)
			Sanguosha->addTranslationEntry(":"+skill_name,Sanguosha->translate(":"+skill_name+arg[3].toString()));

		// stop huashen animation
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		if(skill_name.contains("huashen")) container->stopHuaShen();
		//container->updateAvatarTooltip();
		break;
	}
	case S_GAME_EVENT_ACQUIRE_SKILL: {
		QString player_name = arg[1].toString();
		QString skill_name = arg[2].toString();

		ClientPlayer*player = ClientInstance->getPlayer(player_name);
		player->acquireSkill(skill_name);
		if(player==Self) acquireSkill(player,skill_name);/*

		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateAvatarTooltip();*/
		break;
	}
	case S_GAME_EVENT_ADD_SKILL: {
		QString player_name = arg[1].toString();
		QString skill_name = arg[2].toString();

		ClientPlayer*player = ClientInstance->getPlayer(player_name);
		player->addSkill(skill_name);/*

		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateAvatarTooltip();*/
		break;
	}
	case S_GAME_EVENT_LOSE_SKILL: {
		QString player_name = arg[1].toString();
		QString skill_name = arg[2].toString();

		ClientPlayer*player = ClientInstance->getPlayer(player_name);
		player->loseSkill(skill_name);/*

		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateAvatarTooltip();*/
		break;
	}
	case S_GAME_EVENT_PREPARE_SKILL:
		updateSkillButtons(true);
	case S_GAME_EVENT_UPDATE_SKILL: {/*
		if(arg.size()>3){
			QString skill_name = ":"+arg[2].toString();
			if(JsonUtils::isNumber(arg[3]))
				Sanguosha->addTranslationEntry(skill_name,Sanguosha->translate(skill_name+arg[3].toString()));
			else
				Sanguosha->addTranslationEntry(skill_name,QString::fromUtf8(QByteArray::fromBase64(arg[3].toString().toLatin1())));
		}
		foreach(Photo*photo,photos)
			photo->updateAvatarTooltip();
		dashboard->updateAvatarTooltip();*/
		break;
	}
	case S_GAME_EVENT_CHANGE_GENDER: {
		QString player_name = arg[1].toString();
		General::Gender gender = (General::Gender)arg[2].toInt();

		ClientPlayer*player = ClientInstance->getPlayer(player_name);
		player->setGender(gender);

		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateAvatar();// For Lu Boyan
		break;
	}
	case S_GAME_EVENT_AVATAR_ICON: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());/*
		const General*general = player->getGeneral();
		if(arg[2].toBool()) general = player->getGeneral2();
		const_cast<General*>(general)->setObjectName(arg[3].toString());*/
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		if(arg[2].toBool()) container->updateSmallAvatar();
		else container->updateAvatar();
		break;
	}
	case S_GAME_EVENT_CHANGE_HERO: {
		QString playerName = arg[1].toString();
		QString newHeroName = arg[2].toString();
		bool isSecondaryHero = arg[3].toBool();
		bool sendLog = arg[4].toBool();
		const General*hero = Sanguosha->getGeneral(newHeroName);
		if(hero==nullptr) break;
		ClientPlayer*player = ClientInstance->getPlayer(playerName);
		PlayerCardContainer *container = qobject_cast<PlayerCardContainer *>(
			_getGenericCardContainer(Player::PlaceHand, player));
		if (container) {
			if (isSecondaryHero || player->getGeneral2())
				container->updateSmallAvatar();
			else
				container->updateAvatar();
		}
		if(sendLog){
			QString arg2,type = "#Transfigure";
			if(player->getGeneral2()||isSecondaryHero){
				type = "#TransfigureDual";
				if(isSecondaryHero) arg2 = "GeneralB";
				else arg2 = "GeneralA";
			}
			log_box->appendLog(type,playerName,QStringList(),"",newHeroName,arg2);
		}
		if(player->getMark("secondMode")>0&&player->getGeneralName()=="shenlvbu1"&&(newHeroName.startsWith("shenlvbu")))
			Sanguosha->playSystemAudioEffect("stagechange");

		// ── Spine pop-out: register dynamic skin for the NEW hero ──
		// This is the deferred activation path: if the new hero has a
		// dynamic skin entry in JSON, register + preload + entrance now.
		if (_spineActionController && player) {
			bool isPrimary = !isSecondaryHero;
			registerDynamicSkinForPlayer(player->objectName(), newHeroName, isPrimary);
			if (_spineActionController->hasDynamicSkin(newHeroName)) {
				_spineActionController->preloadPlayer(player->objectName());
				bool isLocal = (player == Self);
				_spineActionController->triggerAction(
					player->objectName(), ActionType::Entrance,
					QList<QPointF>(), isLocal);
			}
		}

		if(player!=Self) break;
		const General*ghero = isSecondaryHero ? player->getGeneral2() : player->getGeneral();
		if(ghero){
			foreach(const Skill*skill,ghero->getVisibleSkills()){
				detachSkill(skill->objectName());
				if(skill->objectName().contains("huashen"))
					dashboard->stopHuaShen();
			}
		}
		foreach(const Skill*skill,hero->getVisibleSkills())
			attachSkill(skill->objectName());
		break;
	}
	case S_GAME_EVENT_PLAYER_REFORM: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateReformState();
		break;
	}
	case S_GAME_EVENT_SKILL_INVOKED: {
		QString skill_name = arg[2].toString();/*
		const Skill*skill = Sanguosha->getSkill(skill_name);
		if(skill&&(skill->isAttachedLordSkill()||skill->inherits("SPConvertSkill"))) return;*/

		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		if(player&&player!=Self){
			Photo*photo = (Photo*)_getGenericCardContainer(Player::PlaceHand,player);
			if(photo) photo->showSkillName(skill_name);
		}
		break;
	}
	case S_GAME_EVENT_PAUSE: {
		bool paused = arg[1].toBool();
		if(pausing_item->isVisible()!=paused){
			if(paused){
				m_timerLabel->pause();
				QBrush pausing_brush(QColor(qrand() % 256,qrand() % 256,qrand() % 256));
				pausing_item->setBrush(pausing_brush);
				bringToFront(pausing_item);
				bringToFront(pausing_text);
			}else
				m_timerLabel->resume();
			pausing_item->setVisible(paused);
			if(ServerInfo.GameMode=="04_boss")
				pausing_text->setText(tr("Boss Mode Level %1").arg(ClientInstance->m_bossLevel+1));
			else
				pausing_text->setText(tr("Paused ..."));
			pausing_text->setVisible(paused);
		}
		break;
	}
	case S_GAME_EVENT_REVEAL_PINDIAN: {
		QString from_name = arg[1].toString(),to_name = arg[3].toString();
		int from_id = arg[2].toInt(),to_id = arg[4].toInt();
		bool success = arg[5].toBool();
		pindian_success = success;
		QString reason = arg[6].toString();

		if(Config.value("EnablePindianBox",true).toBool())
			showPindianBox(from_name,from_id,to_name,to_id,reason);
		else
			setEmotion(from_name,success ? "success" : "no-success");
		break;
	}
	case S_GAME_EVENT_CHANGE_BGM: {
		if(arg.length()<3||Config.BGMVolume<=0) break;

#ifdef AUDIO_SUPPORT
		if(arg[2].toBool()) Audio::stopBGM();
		Audio::playBGM(arg[1].toString());
		Audio::setBGMVolume(Config.BGMVolume);
#endif
		break;
	}
	case S_GAME_EVENT_SORT_HAND: {
		ClientPlayer*player = ClientInstance->getPlayer(arg[1].toString());
		if(player==Self)
			dashboard->sortHandCards(ListS2I(arg[2].toString().split("+")));
		player->sortHandCards(ListS2I(arg[2].toString().split("+")));
		break;
	}
	default:
		break;
	}
}

QGraphicsPixmapItem*RoomScene::createDashboardButtons()
{
	QGraphicsPixmapItem*widget = new QGraphicsPixmapItem(G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_DASHBOARD_BUTTON_SET_BG)
		.scaled(G_DASHBOARD_LAYOUT.m_buttonSetSize));

	ok_button = new QSanButton("platter","confirm",widget);
	ok_button->setRect(G_DASHBOARD_LAYOUT.m_confirmButtonArea);
	cancel_button = new QSanButton("platter","cancel",widget);
	cancel_button->setRect(G_DASHBOARD_LAYOUT.m_cancelButtonArea);
	discard_button = new QSanButton("platter","discard",widget);
	discard_button->setRect(G_DASHBOARD_LAYOUT.m_discardButtonArea);
	connect(ok_button,SIGNAL(clicked()),this,SLOT(doOkButton()));
	connect(cancel_button,SIGNAL(clicked()),this,SLOT(doCancelButton()));
	connect(discard_button,SIGNAL(clicked()),this,SLOT(doDiscardButton()));

	trust_button = new QSanButton("platter","trust",widget);
	trust_button->setStyle(QSanButton::S_STYLE_TOGGLE);
	trust_button->setRect(G_DASHBOARD_LAYOUT.m_trustButtonArea);
	connect(trust_button,SIGNAL(clicked()),this,SLOT(trust()));
	connect(Self,SIGNAL(state_changed()),this,SLOT(updateTrustButton()));

	// set them all disabled
	ok_button->setEnabled(false);
	cancel_button->setEnabled(false);
	discard_button->setEnabled(false);
	trust_button->setEnabled(false);
	return widget;
}

QRectF ReplayerControlBar::boundingRect() const
{
	return QRectF(0,0,S_BUTTON_WIDTH*4+S_BUTTON_GAP*3,S_BUTTON_HEIGHT);
}

void ReplayerControlBar::paint(QPainter*,const QStyleOptionGraphicsItem*,QWidget*)
{
}

ReplayerControlBar::ReplayerControlBar(Dashboard*dashboard)
{
	QSanButton*play,*uniform,*slow_down,*speed_up;

	uniform = new QSanButton("replay","uniform",this);
	slow_down = new QSanButton("replay","slow-down",this);
	play = new QSanButton("replay","pause",this);
	speed_up = new QSanButton("replay","speed-up",this);
	play->setStyle(QSanButton::S_STYLE_TOGGLE);
	uniform->setStyle(QSanButton::S_STYLE_TOGGLE);

	int step = S_BUTTON_GAP+S_BUTTON_WIDTH;
	uniform->setPos(0,0);
	slow_down->setPos(step,0);
	play->setPos(step*2,0);
	speed_up->setPos(step*3,0);

	time_label = new QLabel;
	time_label->setAttribute(Qt::WA_NoSystemBackground);
	time_label->setText("-----------------------------------------------------");
	QPalette palette;
	palette.setColor(QPalette::WindowText,Config.TextEditColor);
	time_label->setPalette(palette);

	QGraphicsProxyWidget*widget = new QGraphicsProxyWidget(this);
	widget->setWidget(time_label);
	widget->setPos(step*4,0);

	Replayer*replayer = ClientInstance->getReplayer();
	connect(play,SIGNAL(clicked()),replayer,SLOT(toggle()));
	connect(uniform,SIGNAL(clicked()),replayer,SLOT(uniform()));
	connect(slow_down,SIGNAL(clicked()),replayer,SLOT(slowDown()));
	connect(speed_up,SIGNAL(clicked()),replayer,SLOT(speedUp()));
	connect(replayer,SIGNAL(elasped(int)),this,SLOT(setTime(int)));
	connect(replayer,SIGNAL(speed_changed(qreal)),this,SLOT(setSpeed(qreal)));

	speed = replayer->getSpeed();
	setParentItem(dashboard);
	setPos(S_BUTTON_GAP,-S_BUTTON_GAP-S_BUTTON_HEIGHT);

	duration_str = FormatTime(replayer->getDuration());
}

QString ReplayerControlBar::FormatTime(int secs)
{
	int minutes = secs/60;
	int remainder = secs % 60;
	return QString("%1:%2").arg(minutes,2,10,QChar('0')).arg(remainder,2,10,QChar('0'));
}

void ReplayerControlBar::setSpeed(qreal speed)
{
	this->speed = speed;
}

void ReplayerControlBar::setTime(int secs)
{
	time_label->setText(QString("<b>x%1 </b> [%2/%3]").arg(speed).arg(FormatTime(secs)).arg(duration_str));
}

void RoomScene::createReplayControlBar()
{
	m_replayControl = new ReplayerControlBar(dashboard);
}

void RoomScene::_getSceneSizes(QSize&minSize,QSize&maxSize)
{
	if(photos.size() >= 8){
		minSize = _m_roomLayout->m_minimumSceneSize10Player;
		maxSize = _m_roomLayout->m_maximumSceneSize10Player;
	} else {
		minSize = _m_roomLayout->m_minimumSceneSize;
		maxSize = _m_roomLayout->m_maximumSceneSize;
	}
}

void RoomScene::adjustItems()
{
	QRectF displayRegion = sceneRect();

	QSanSkinFactory&factory = QSanSkinFactory::getInstance();
	QSize minSize,maxSize;
	_getSceneSizes(minSize,maxSize);

	// update the sizes since we have reloaded the skin.
	_getSceneSizes(minSize,maxSize);

	if(displayRegion.left()!=0||displayRegion.top()!=0
		|| displayRegion.bottom() < minSize.height()
		|| displayRegion.right() < minSize.width()){
		displayRegion.setLeft(0);displayRegion.setTop(0);
		double sy = minSize.height()/displayRegion.height();
		double sx = minSize.width()/displayRegion.width();
		double scale = qMax(sx,sy);
		displayRegion.setBottom(scale*displayRegion.height());
		displayRegion.setRight(scale*displayRegion.width());
		setSceneRect(displayRegion);
	}

	int padding = _m_roomLayout->m_scenePadding;
	displayRegion.moveLeft(displayRegion.x()+padding);
	displayRegion.moveTop(displayRegion.y()+padding);
	displayRegion.setWidth(displayRegion.width()-padding*2);
	displayRegion.setHeight(displayRegion.height()-padding*2);

	// set dashboard
	dashboard->setX(displayRegion.x());
	dashboard->setWidth(displayRegion.width());
	dashboard->setY(displayRegion.height()-dashboard->boundingRect().height());

	// set infoplane
	_m_infoPlane.setWidth(displayRegion.width()*_m_roomLayout->m_infoPlaneWidthPercentage);
	_m_infoPlane.moveRight(displayRegion.right());
	_m_infoPlane.setTop(displayRegion.top()+_m_roomLayout->m_roleBoxHeight);
	_m_infoPlane.setBottom(displayRegion.bottom()-dashboard->getAvatarAreaSceneBoundingRect().height()-_m_roomLayout->m_chatTextBoxHeight);
	m_rolesBoxBackground = m_rolesBoxBackground.scaled(_m_infoPlane.width(),_m_roomLayout->m_roleBoxHeight);
	m_rolesBox->setPixmap(m_rolesBoxBackground);
	m_rolesBox->setPos(_m_infoPlane.left(),displayRegion.top());

	log_box_widget->setPos(_m_infoPlane.topLeft());
	log_box->resize(_m_infoPlane.width(),_m_infoPlane.height()*_m_roomLayout->m_logBoxHeightPercentage);
	chat_box_widget->setPos(_m_infoPlane.left(),_m_infoPlane.bottom()-_m_infoPlane.height()*_m_roomLayout->m_chatBoxHeightPercentage);
	chat_box->resize(_m_infoPlane.width(),_m_infoPlane.bottom()-chat_box_widget->y());
	chat_edit_widget->setPos(_m_infoPlane.left(),_m_infoPlane.bottom());
	chat_edit->resize(_m_infoPlane.width()-chat_widget->boundingRect().width(),_m_roomLayout->m_chatTextBoxHeight);
	chat_widget->setPos(_m_infoPlane.right()-chat_widget->boundingRect().width(),
		chat_edit_widget->y()+(_m_roomLayout->m_chatTextBoxHeight-chat_widget->boundingRect().height())/2);

	padding += _m_roomLayout->m_photoRoomPadding;
	if(self_box)
		self_box->setPos(_m_infoPlane.left()-padding-self_box->boundingRect().width(),
		sceneRect().height()-padding-self_box->boundingRect().height()
		- G_DASHBOARD_LAYOUT.m_normalHeight-G_DASHBOARD_LAYOUT.m_floatingAreaHeight);
	if(enemy_box)
		enemy_box->setPos(padding*2,padding*2);

	//padding -= _m_roomLayout->m_photoRoomPadding;
	m_tablew = displayRegion.width();
	m_tableh = displayRegion.height();

	if(m_tableBgPixmapOrig.width()==1||m_tableBgPixmapOrig.height()==1)
		m_tableBgPixmapOrig = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_TABLE_BG);

	m_tableBgPixmap = m_tableBgPixmapOrig.scaled(m_tablew,m_tableh,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);

	m_tableBg->setPos(0,0);
	m_tableBg->setPixmap(m_tableBgPixmap);

	m_tableh -= _m_roomLayout->m_photoDashboardPadding;

	updateTable();
	updateRolesBox();
	setChatBoxVisible(chat_box_widget->isVisible());

	QMapIterator<QString,BubbleChatBox*> iter(bubbleChatBoxes);
	while (iter.hasNext()){
		iter.next();
		iter.value()->setArea(getBubbleChatBoxShowArea(iter.key()));
	}
}

void RoomScene::_dispersePhotos(QList<Photo*>&photos,QRectF fillRegion,
	Qt::Orientation orientation,Qt::Alignment align)
{
	int numPhotos = photos.size();
	if(numPhotos==0) return;
	double photoWidth = _m_photoLayout->m_normalWidth;
	double photoHeight = _m_photoLayout->m_normalHeight;
	Qt::Alignment hAlign = align&Qt::AlignHorizontal_Mask;
	Qt::Alignment vAlign = align&Qt::AlignVertical_Mask;

	double startX = 0, startY = 0, stepX = 0, stepY = 0;

	if(orientation==Qt::Horizontal){
		stepX = qMax(photoWidth+G_ROOM_LAYOUT.m_photoHDistance, fillRegion.width()/numPhotos);
	} else {
		stepY = G_ROOM_LAYOUT.m_photoVDistance+photoHeight;
	}

	switch (vAlign){
	case Qt::AlignTop: startY = fillRegion.top()+photoHeight/2;break;
	case Qt::AlignBottom: startY = fillRegion.bottom()-photoHeight/2-stepY*(numPhotos-1);break;
	case Qt::AlignVCenter: startY = fillRegion.center().y()-stepY*(numPhotos-1)/2.0;break;
	default: Q_ASSERT(false);
	}
	switch (hAlign){
	case Qt::AlignLeft: startX = fillRegion.left()+photoWidth/2;break;
	case Qt::AlignRight: startX = fillRegion.right()-photoWidth/2-stepX*(numPhotos-1);break;
	case Qt::AlignHCenter: startX = fillRegion.center().x()-stepX*(numPhotos-1)/2.0;break;
	default: Q_ASSERT(false);
	}

	for (int i = 0;i < numPhotos;i++)
		photos[i]->setPos(QPointF(startX+stepX*i, startY+stepY*i));
}

void RoomScene::updateTable()
{
	int pad = _m_roomLayout->m_scenePadding+_m_roomLayout->m_photoRoomPadding;
	int tablew = log_box_widget->x()-pad*2;
	int tableh = sceneRect().height()-pad*2-dashboard->boundingRect().height();
	if((ServerInfo.GameMode=="04_1v3"||ServerInfo.GameMode=="06_3v3")&&game_started)
		tableh -= _m_roomLayout->m_photoVDistance;
	int photow = _m_photoLayout->m_normalWidth;
	int photoh = _m_photoLayout->m_normalHeight;

	// Layout:
	//    col1           col2
	// _______________________
	// |_2_|______1_______|_0_| row1
	// |   |              |   |
	// | 4 |    table     | 3 |
	// |___|______________|___|
	// |      dashboard       |
	// ------------------------
	// region 5 = 0+3,region 6 = 2+4,region 7 = 0+1+2

	static int regularSeatIndex[][20] = {
        { 1 },
        { 5, 6 },
        { 5, 1, 6 },
        { 3, 1, 1, 4 },
        { 3, 1, 1, 1, 4 },
        { 5, 5, 1, 1, 6, 6 },
        { 5, 5, 1, 1, 1, 6, 6 },
        { 3, 3, 7, 7, 7, 7, 4, 4 },
        { 3, 3, 7, 7, 7, 7, 7, 4, 4 },
        { 3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4 },        // 11 players
        { 3, 3, 3, 7, 7, 7, 7, 7, 7, 4, 4, 4 },     // 12 players
        { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 },  // 13 players
        { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 }, // 14 players
        { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 }, // 15 players
        { 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4 }, // 16 players
        { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 }, // 17 players
        { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 }, // 18 players
        { 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4 }, // 19 players
        {3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4, 4}//20 players
    };
	static int hulaoSeatIndex[][3] = {
		{ 1,1,1 },// if self is shenlvbu
		{ 3,3,1 },
		{ 3,1,4 },
		{ 1,4,4 }
	};
	static int kof3v3SeatIndex[][5] = {
		{ 3,1,1,1,4 },// lord
		{ 1,1,1,4,4 },// rebel (left),same with loyalist (left)
		{ 3,3,1,1,1 } // loyalist (right),same with rebel (right)
	};

	double hGap = _m_roomLayout->m_photoHDistance;
	double vGap = _m_roomLayout->m_photoVDistance;
	double col1 = photow+hGap;
	double col2 = tablew-col1;
	double row1 = photoh+vGap;
	double row2 = tableh;

	const int C_NUM_REGIONS = 8;
	QRectF seatRegions[] = {
		QRectF(col2,pad,col1,row1),
		QRectF(col1,pad,col2-col1,row1),
		QRectF(pad,pad,col1,row1),
		QRectF(col2,row1,col1,row2-row1),
		QRectF(pad,row1,col1,row2-row1),
		QRectF(col2,pad,col1,row2),
		QRectF(pad,pad,col1,row2),
		QRectF(pad,pad,col1+col2,row1)
	};

	static Qt::Alignment aligns[] = {
		Qt::AlignRight | Qt::AlignTop,
		Qt::AlignHCenter | Qt::AlignTop,
		Qt::AlignLeft | Qt::AlignTop,
		Qt::AlignRight | Qt::AlignVCenter,
		Qt::AlignLeft | Qt::AlignVCenter,
		Qt::AlignRight | Qt::AlignVCenter,
		Qt::AlignLeft | Qt::AlignVCenter,
		Qt::AlignHCenter | Qt::AlignTop,
	};

	static Qt::Alignment kofAligns[] = {
		Qt::AlignRight | Qt::AlignTop,
		Qt::AlignHCenter | Qt::AlignTop,
		Qt::AlignLeft | Qt::AlignTop,
		Qt::AlignRight | Qt::AlignBottom,
		Qt::AlignLeft | Qt::AlignBottom,
		Qt::AlignRight | Qt::AlignBottom,
		Qt::AlignLeft | Qt::AlignBottom,
		Qt::AlignHCenter | Qt::AlignTop,
	};

	Qt::Orientation orients[] = {
		Qt::Horizontal,
		Qt::Horizontal,
		Qt::Horizontal,
		Qt::Vertical,
		Qt::Vertical,
		Qt::Vertical,
		Qt::Vertical,
		Qt::Horizontal
	};

	QRectF tableRect(col1,row1,col2-col1,row2-row1);

	QRect tableBottomBar(0,0,log_box_widget->x()-col1,G_DASHBOARD_LAYOUT.m_floatingAreaHeight);
	tableBottomBar.moveBottomLeft(QPoint((int)tableRect.left(),0));
	dashboard->setFloatingArea(tableBottomBar);

	m_tableCenterPos = tableRect.center();
	control_panel->setPos(m_tableCenterPos);
	m_tablePile->setPos(m_tableCenterPos);
	m_tablePile->setSize(qMax((int)tableRect.width()-_m_roomLayout->m_discardPilePadding*2,
		_m_roomLayout->m_discardPileMinWidth),_m_commonLayout->m_cardNormalHeight);
	m_tablePile->adjustCards();
	card_container->setPos(m_tableCenterPos);
	guanxing_x_box->setPos(m_tableCenterPos);
	guanxing_box3->setPos(m_tableCenterPos);
	guanxing_box7->setPos(m_tableCenterPos);
	guanxing_box9->setPos(m_tableCenterPos);

	m_timerLabel->setPos(QPointF(width()*0.77,-1));

	/*if(nullptr!=prompt_box_widget){
		QRectF promptBoxRect = prompt_box_widget->boundingRect();
		int promptBoxWidth = promptBoxRect.width();
		int promptBoxHeight = promptBoxRect.height();
		QRectF progressBarRect = dashboard->getProgressBarSceneBoundingRect();
		int xShift = (promptBoxWidth-progressBarRect.width())/2;
		prompt_box_widget->setPos(progressBarRect.x()-xShift,
			progressBarRect.y()-promptBoxHeight);
	}*/
	QRectF progressBarRect = dashboard->getProgressBarSceneBoundingRect();
	QRectF promptBoxRect = prompt_box_widget->boundingRect();
	int promptBoxWidth = promptBoxRect.width();
	int promptBoxHeight = promptBoxRect.height();
	prompt_box->setPos(progressBarRect.left()+promptBoxWidth/2,progressBarRect.top()-promptBoxHeight/2);

	pausing_text->setPos(m_tableCenterPos-pausing_text->boundingRect().center());
	pausing_item->setRect(sceneRect());
	pausing_item->setPos(0,0);

	bool pkMode = false;
	int*seatToRegion = regularSeatIndex[photos.length()-1];
	if((ServerInfo.GameMode=="04_1v3"||ServerInfo.GameMode=="04_boss")&&game_started){
		seatToRegion = hulaoSeatIndex[Self->getSeat()-1];
		pkMode = true;
	} else if(ServerInfo.GameMode=="06_3v3"&&game_started){
		seatToRegion = kof3v3SeatIndex[(Self->getSeat()-1) % 3];
		pkMode = true;
	}
	QList<Photo*> photosInRegion[C_NUM_REGIONS];
	for (int i = 0;i < photos.length();i++){
		int regionIndex = seatToRegion[i];
		if(regionIndex==4||regionIndex==6)
			photosInRegion[regionIndex].append(photos[i]);
		else
			photosInRegion[regionIndex].prepend(photos[i]);
	}
	for (int i = 0;i < C_NUM_REGIONS;i++){
		if(photosInRegion[i].isEmpty()) continue;
		Qt::Alignment align = aligns[i];
		if(pkMode) align = kofAligns[i];

		QRect floatingArea(0,0,G_ROOM_LAYOUT.m_photoHDistance,G_PHOTO_LAYOUT.m_normalHeight);
		// if the photo is on the right edge of table
		if(i==0||i==3||i==5||i==8) floatingArea.moveRight(0);
		else floatingArea.moveLeft(G_PHOTO_LAYOUT.m_normalWidth);

		foreach(Photo*photo,photosInRegion[i])
			photo->setFloatingArea(floatingArea);
		_dispersePhotos(photosInRegion[i],seatRegions[i],orients[i],align);
	}

	// ── Spine pop-out: refresh seat geometry after layout changes ──
	updateSpineSeatGeometry();
}

void RoomScene::addPlayer(ClientPlayer*player)
{
	for (int i = 0;i < photos.length();i++){
		if(photos[i]->getPlayer()==nullptr){
			photos[i]->setPlayer(player);
			name2photo[player->objectName()] = photos[i];
			if(!Self->hasFlag("marshalling"))
				Sanguosha->playSystemAudioEffect("add-player",false);
			break;
		}
	}
}

void RoomScene::removePlayer(const QString&player_name)
{
	Photo*photo = name2photo[player_name];
	if(photo){
		photo->setPlayer(nullptr);
		name2photo.remove(player_name);
		Sanguosha->playSystemAudioEffect("remove-player",false);
	}
}

void RoomScene::arrangeSeats(const QList<const ClientPlayer*>&seats)
{
	// rearrange the photos
	Q_ASSERT(seats.length()==photos.length());

	for (int i = 0;i < seats.length();i++){
		for (int j = i;j < photos.length();j++){
			if(photos.at(j)->getPlayer()==seats.at(i)){
				photos.swapItemsAt(i,j);
				break;
			}
		}
	}
	game_started = true;
	QParallelAnimationGroup*group = new QParallelAnimationGroup(this);
	updateTable();

	group->start(QAbstractAnimation::DeleteWhenStopped);

	// set item to player mapping
	if(item2player.isEmpty()){
		item2player.insert(dashboard,Self);
		connect(dashboard,SIGNAL(selected_changed()),this,SLOT(updateSelectedTargets()));
		foreach (Photo*photo,photos){
			item2player.insert(photo,photo->getPlayer());
			connect(photo,SIGNAL(selected_changed()),this,SLOT(updateSelectedTargets()));
			connect(photo,SIGNAL(enable_changed()),this,SLOT(onEnabledChange()));
		}
	}

	bool all_robot = true;
	foreach (const ClientPlayer*p,ClientInstance->getPlayers()){
		if(p!=Self&&p->getState()!="robot"){
			all_robot = false;
			break;
		}
	}
	if(all_robot)
		setChatBoxVisible(false);

	//update the positions of bubbles after setting seats
	foreach (const QString&who,name2photo.keys()){
		if(bubbleChatBoxes.contains(who))
			bubbleChatBoxes[who]->setArea(getBubbleChatBoxShowArea(who));
	}
}

void RoomScene::mousePressEvent(QGraphicsSceneMouseEvent*event)
{
	QGraphicsScene::mousePressEvent(event);
}

void RoomScene::mouseReleaseEvent(QGraphicsSceneMouseEvent*event)
{
	QGraphicsScene::mouseReleaseEvent(event);

	if(_m_isInDragAndUseMode){
		if((ok_button->isEnabled()||dashboard->currentSkill())
			&&(!dashboard->isUnderMouse()||dashboard->isAvatarUnderMouse())){
			if(ok_button->isEnabled())
				ok_button->click();
			else
				dashboard->adjustCards(true);
		} else {
			enableTargets(nullptr);
			dashboard->unselectAll();
		}
		_m_isInDragAndUseMode = false;
	}
}

void RoomScene::mouseMoveEvent(QGraphicsSceneMouseEvent*event)
{
	QGraphicsScene::mouseMoveEvent(event);

	if(!Config.EnableSuperDrag)
		return;

	QGraphicsObject*obj = static_cast<QGraphicsObject*>(focusItem());
	CardItem*card_item = qobject_cast<CardItem*>(obj);
	if(!card_item||!card_item->isUnderMouse()||!dashboard->hasHandCard(card_item))
		return;

	static bool wasOutsideDashboard = false;
	const bool isOutsideDashboard = !dashboard->isUnderMouse()||dashboard->isAvatarUnderMouse();
	if(isOutsideDashboard!=wasOutsideDashboard){
		wasOutsideDashboard = isOutsideDashboard;
		if(wasOutsideDashboard&&!_m_isInDragAndUseMode){
			if(!_m_superDragStarted&&!dashboard->getPendings().isEmpty())
				dashboard->clearPendings();
			dashboard->selectCard(card_item,true);
			if(dashboard->currentSkill()&&!dashboard->getPendings().contains(card_item)){
				dashboard->addPending(card_item);
				dashboard->updatePending();
			}
			_m_isInDragAndUseMode = true;
			_m_superDragStarted = true;
			if(!dashboard->currentSkill()
				&&(ClientInstance->getStatus()==Client::Playing
				|| ClientInstance->getStatus()==Client::RespondingUse)){
				enableTargets(card_item->getCard());
			}
		}
	}

	PlayerCardContainer*victim = nullptr;

	foreach (Photo*photo,photos){
		if(photo->isUnderMouse())
			victim = photo;
	}

	if(dashboard->isAvatarUnderMouse())
		victim = dashboard;

	if(victim!=nullptr&&victim->canBeSelected()){
		victim->setSelected(true);
		updateSelectedTargets();
	}
}

void RoomScene::enableTargets(const Card*card)
{
	Client::Status status = ClientInstance->getStatus();
	if(card!=nullptr){
		bool enabled = true;
		if(status==Client::Playing)
			enabled = card->isAvailable(Self);
		else if(status==Client::Responding||status==Client::RespondingUse){
			Card::HandlingMethod method = card->getHandlingMethod();
			if(status==Client::Responding&&method==Card::MethodUse)
				method = Card::MethodResponse;
			enabled = !Self->isCardLimited(card,method);
			if(enabled&&status==Client::RespondingUse&&ClientInstance->m_respondingUseFixedTarget)
				enabled = !Sanguosha->isProhibited(Self,ClientInstance->m_respondingUseFixedTarget,card);
		}else if(status==Client::RespondingForDiscard)
			enabled = !Self->isCardLimited(card,Card::MethodDiscard);
		if(!enabled){
			ok_button->setEnabled(false);
			return;
		}
	}
	selected_targets.clear();
	// unset avatar and all photo
	foreach(PlayerCardContainer*item,item2player.keys())
		item->setSelected(false);
	if(card==nullptr){
		foreach (PlayerCardContainer*item,item2player.keys()){
			animations->effectOut(item->getMouseClickReceiver());
			item->setFlag(QGraphicsItem::ItemIsSelectable,false);
			item->setEnabled(true);
		}
		ok_button->setEnabled(false);
		return;
	}
	if(card->targetFixed()||((status&Client::ClientStatusBasicMask)==Client::Responding
		&&(status==Client::Responding||(card->getTypeId()!=Card::TypeSkill&&status!=Client::RespondingUse)))
		|| status==Client::AskForShowOrPindian){
		foreach (PlayerCardContainer*item,item2player.keys()){
			animations->effectOut(item->getMouseClickReceiver());
			item->setFlag(QGraphicsItem::ItemIsSelectable,false);
		}
		ok_button->setEnabled(true);
		return;
	}
	updateTargetsEnablity(card);
	if(selected_targets.isEmpty()){
		if(card->isKindOf("Slash")&&Self->hasFlag("slashTargetFixToOne")){
			unselectAllTargets();
			foreach (Photo*photo,photos){
				if(photo->flags()&QGraphicsItem::ItemIsSelectable){
					if(photo->isSelected()) continue;
					photo->setSelected(true);
					break;
				}
			}
		} else if(Config.EnableAutoTarget){
			if(!card->targetsFeasible(selected_targets,Self)){
				unselectAllTargets();
				int count = 0;
				foreach(Photo*photo,photos)
					if(photo->flags()&QGraphicsItem::ItemIsSelectable)
						count++;
				if(dashboard->flags()&QGraphicsItem::ItemIsSelectable)
					count++;
				if(count==1)
					selectNextTarget(false);
			}
		}
	}
	ok_button->setEnabled(card->targetsFeasible(selected_targets,Self));
}

void RoomScene::updateTargetsEnablity(const Card*card)
{
	/*QMapIterator<PlayerCardContainer*,const ClientPlayer*> itor(item2player);
	while (itor.hasNext()){
		itor.next();
		PlayerCardContainer*item = itor.key();
		int maxVotes = 0;
		if(card){
			card->targetFilter(selected_targets,itor.value(),Self,maxVotes);
			item->setMaxVotes(maxVotes);
		}
		if(item->isSelected()) continue;
		QGraphicsItem*animationTarget = item->getMouseClickReceiver();
		if(!card||maxVotes > 0)
			animations->effectOut(animationTarget);
		else if(!animationTarget->graphicsEffect()||!animationTarget->graphicsEffect()->inherits("SentbackEffect"))
			animations->sendBack(animationTarget);
		if(card)
			item->setFlag(QGraphicsItem::ItemIsSelectable,!card||maxVotes > 0);
	}*/
	foreach (PlayerCardContainer*item,item2player.keys()){
		int maxVotes = 0;
		if(card){
			card->targetFilter(selected_targets,item2player[item],Self,maxVotes);
			item->setMaxVotes(maxVotes);
		}
		if(item->isSelected()) continue;
		QGraphicsItem*animationTarget = item->getMouseClickReceiver();
		if(!card||maxVotes > 0)
			animations->effectOut(animationTarget);
		else if(!animationTarget->graphicsEffect()||!animationTarget->graphicsEffect()->inherits("SentbackEffect"))
			animations->sendBack(animationTarget);
		if(card)
			item->setFlag(QGraphicsItem::ItemIsSelectable,maxVotes > 0);
	}
}

void RoomScene::updateSelectedTargets()
{
	PlayerCardContainer*item = qobject_cast<PlayerCardContainer*>(sender());
	if(item==nullptr) return;
	const Card*card = dashboard->getSelected();
	if(card){
		const ClientPlayer*player = item2player.value(item,nullptr);
		if(item->isSelected())
			selected_targets.append(player);
		else {
			selected_targets.removeAll(player);
			foreach (const Player*cp,selected_targets){
				QList<const Player*> tempPlayers = QList<const Player*>(selected_targets);
				tempPlayers.removeAll(cp);
				if(card->targetFilter(tempPlayers,cp,Self)) continue;
				selected_targets.clear();
				unselectAllTargets();
				return;
			}
		}
		ok_button->setEnabled(card->targetsFeasible(selected_targets,Self));
	} else
		selected_targets.clear();
	updateTargetsEnablity(card);
}

void RoomScene::keyReleaseEvent(QKeyEvent*event)
{
	if(!Config.EnableHotKey) return;
	if(chat_edit->hasFocus()) return;

	bool control_is_down = event->modifiers()&Qt::ControlModifier;
	bool alt_is_down = event->modifiers()&Qt::AltModifier;

	switch (event->key()){
	case Qt::Key_F1: trust();break;
	case Qt::Key_F2: chooseSkillButton();break;
	case Qt::Key_F8: {
		if (m_emotionPanel) {
			if (m_emotionPanel->isVisible())
				m_emotionPanel->hidePanel();
			else
				m_emotionPanel->showPanel();
		}
		break;
	}
	case Qt::Key_F3: dashboard->beginSorting();break;
	case Qt::Key_F4: dashboard->reverseSelection();break;
	case Qt::Key_F5: {
		adjustItems();
		break;
	}
	case Qt::Key_F7: {
		if(control_is_down){
			if(add_robot&&add_robot->isVisible())
				ClientInstance->addRobot(1);
		} else if(start_game&&start_game->isVisible()){
			int left = Sanguosha->getPlayerCount(ServerInfo.GameMode)-ClientInstance->getPlayers().length();
			ClientInstance->addRobot(left);
		}
		break;
	}
	case Qt::Key_F11: {
		if (!Self) break;

		QStringList available_piles;
		QStringList excluded_piles = { "yinni_general" }; // 排除

		// 1. 獲取所有屬性名稱 (包含動態屬性與靜態 Q_PROPERTY)
		QList<QByteArray> all_props = Self->dynamicPropertyNames();
		const QMetaObject *meta = Self->metaObject();
		for (int i = 0; meta && i < meta->propertyCount(); ++i) {
			if (meta->property(i).name()) {
				all_props << meta->property(i).name();
			}
		}

		foreach (const QByteArray &prop_name, all_props) {
			QString tag = QString::fromLatin1(prop_name);

			// 檢查後綴、去重複、排除特定牌堆
			if (tag.endsWith("_general", Qt::CaseInsensitive) && 
				!available_piles.contains(tag) && 
				!excluded_piles.contains(tag)) {

				// 解析武將清單 (神殺屬性標準：以 '+' 分隔的字串)
				QStringList generals = Self->property(prop_name).toString().split("+", QString::SkipEmptyParts);
				if (generals.isEmpty()) continue;

				// 驗證是否所有名稱都是合法武將
				bool all_valid = true;
				foreach (const QString &gen_name, generals) {
					if (!Sanguosha->getGeneral(gen_name)) {
						all_valid = false;
						break;
					}
				}

				// 完全合法才加入選單
				if (all_valid) {
					available_piles << tag;
				}
			}
		}

		if (available_piles.isEmpty()) break;

		// 3. UI 顯示邏輯
		if (available_piles.length() == 1) {
			showGeneralPile(available_piles.first());
		} else {
			QDialog *dialog = new QDialog(main_window);
			dialog->setWindowTitle(tr("Select skill"));

			QVBoxLayout *layout = new QVBoxLayout;
			foreach (const QString &tag, available_piles) {
				QString skill_name = tag.left(tag.length() - 8);
				QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(skill_name));
				connect(button, &QCommandLinkButton::clicked, this, [this, tag]() { showGeneralPile(tag); });
				connect(button, SIGNAL(clicked()), dialog, SLOT(accept()));
				layout->addWidget(button);
			}

			dialog->setLayout(layout);
			dialog->exec();
		}
		break;
	}

	case Qt::Key_Q: dashboard->selectCard("Q");break;
	case Qt::Key_W: dashboard->selectCard("W");break;
	case Qt::Key_E: dashboard->selectCard("E");break;
	case Qt::Key_R: dashboard->selectCard("R");break;
	case Qt::Key_T: dashboard->selectCard("T");break;
	case Qt::Key_Y: dashboard->selectCard("Y");break;
	case Qt::Key_U: dashboard->selectCard("U");break;
	case Qt::Key_I: dashboard->selectCard("I");break;
	case Qt::Key_O: dashboard->selectCard("O");break;
	case Qt::Key_P: dashboard->selectCard("P");break;
	case Qt::Key_A: dashboard->selectCard("A");break;
	case Qt::Key_S: dashboard->selectCard("S");break;
	case Qt::Key_D: dashboard->selectCard("D");break;
	case Qt::Key_F: dashboard->selectCard("F");break;
	case Qt::Key_G: dashboard->selectCard("G");break;
	case Qt::Key_H: dashboard->selectCard("H");break;
	case Qt::Key_J: dashboard->selectCard("J");break;
	case Qt::Key_K: dashboard->selectCard("K");break;
	case Qt::Key_L: dashboard->selectCard("L");break;
	case Qt::Key_Z: dashboard->selectCard("Z");break;
	case Qt::Key_X: dashboard->selectCard("X");break;
	case Qt::Key_C: dashboard->selectCard("C");break;
	case Qt::Key_V: dashboard->selectCard("V");break;
	case Qt::Key_B: dashboard->selectCard("B");break;
	case Qt::Key_N: dashboard->selectCard("N");break;
	case Qt::Key_M: dashboard->selectCard("M");break;
	/*case Qt::Key_S: dashboard->selectCard("slash"); break;
	case Qt::Key_J: dashboard->selectCard("jink");break;
	case Qt::Key_P: dashboard->selectCard("peach");break;
	case Qt::Key_O: dashboard->selectCard("analeptic");break;

	case Qt::Key_E: dashboard->selectCard("equip");break;
	case Qt::Key_W: dashboard->selectCard("weapon");break;
	case Qt::Key_F: dashboard->selectCard("armor");break;
	case Qt::Key_H: dashboard->selectCard("defensive_horse+offensive_horse");break;

	case Qt::Key_T: dashboard->selectCard("trick");break;
	case Qt::Key_A: dashboard->selectCard("aoe");break;
	case Qt::Key_N: dashboard->selectCard("nullification");break;
	case Qt::Key_Q: dashboard->selectCard("snatch");break;
	case Qt::Key_C: dashboard->selectCard("dismantlement");break;
	case Qt::Key_U: dashboard->selectCard("duel");break;
	case Qt::Key_L: dashboard->selectCard("lightning");break;
	case Qt::Key_I: dashboard->selectCard("indulgence");break;
	case Qt::Key_B: dashboard->selectCard("supply_shortage");break;*/

	case Qt::Key_Left: dashboard->selectCard(".",false,control_is_down);break;
	case Qt::Key_Right: dashboard->selectCard(".",true,control_is_down);break;// iterate all cards

	case Qt::Key_Return: {
		doOkButton();
		break;
	}
	case Qt::Key_Escape: {
		if(ClientInstance->getStatus()==Client::Playing){
			dashboard->unselectAll();
			enableTargets(nullptr);
		} else
			dashboard->unselectAll();
		if(cancel_button->isEnabled())
			doCancelButton();
		break;
	}
	case Qt::Key_Space: {
		if(cancel_button->isEnabled())
			doCancelButton();
		else if(discard_button->isEnabled())
			doDiscardButton();
		break;
	}

	case Qt::Key_0:
	case Qt::Key_1:
	case Qt::Key_2:
	case Qt::Key_3:
	case Qt::Key_4: {
		int position = event->key()-Qt::Key_0;
		if(position!=0&&alt_is_down){
			dashboard->selectEquip(position);
		}
		break;
	}
	case Qt::Key_5:
	case Qt::Key_6:
	case Qt::Key_7:
	case Qt::Key_8:
	case Qt::Key_9: {
		int order = event->key()-Qt::Key_0;
		selectTarget(order,control_is_down);
		break;
	}

	case Qt::Key_F9: {
		if(Self==nullptr) return;
		foreach (Photo*photo,photos){
			if(photo->getPlayer()&&photo->getPlayer()->isAlive())
				photo->showDistance();
		}
		break;
	}
	case Qt::Key_F10: {
		if(dashboard){
			m_skillButtonSank = !m_skillButtonSank;
			dashboard->updateSkillButton();
		}
		break;
	}
	/*case Qt::Key_D: {
		if(Self==nullptr) return;
		foreach (Photo*photo,photos){
			if(photo->getPlayer()&&photo->getPlayer()->isAlive())
				photo->showDistance();
		}
		break;
	}
	case Qt::Key_Z: {
		if(dashboard){
			m_skillButtonSank = !m_skillButtonSank;
			dashboard->updateSkillButton();
		}
		break;
	}*/
	}
}

void RoomScene::contextMenuEvent(QGraphicsSceneContextMenuEvent*event)
{
	QTransform transform;
	QGraphicsScene::contextMenuEvent(event);
	QGraphicsItem*item = itemAt(event->scenePos(),transform);
	if(!item||item->zValue() < 0){
		miscellaneous_menu->clear();
		miscellaneous_menu->setTitle(tr("Miscellaneous"));

		QMenu*pile = miscellaneous_menu->addMenu(tr("Private Piles"));
		pile->setEnabled(false);
		foreach (const ClientPlayer*player,item2player.values()){
			foreach (QString pile_name,player->getPileNames()){
				if(pile_name.startsWith("#")) continue;
				foreach (int id,player->getPile(pile_name)){
					if(id==Card::S_UNKNOWN_CARD_ID) continue;
					QAction*action = pile->addAction(G_ROOM_SKIN.getCardAvatarPixmap(player->getGeneralName()),QString("%1 %2").arg(player->getLogName()).arg(Sanguosha->translate(pile_name)));
					action->setData(QString("%1.%2").arg(player->objectName()).arg(pile_name));
					connect(action,SIGNAL(triggered()),this,SLOT(showPlayerCards()));
					pile->setEnabled(true);
					break;
				}
			}
		}
		miscellaneous_menu->addSeparator();

		pile = miscellaneous_menu->addMenu(tr("Known cards"));
		foreach (const ClientPlayer*player,item2player.values()){
			if(player==Self) continue;
			QList<const Card*> known = player->getKnownCards();
			if(Self->canSeeHandcard(player)){
				known.clear();
				//QStringList handcard = player->property("My_Visible_HandCards").toString().split("+");
				foreach (int id,player->handCards())
					known << Sanguosha->getEngineCard(id);
			}else if(!ServerInfo.EnableCheat)
				known.clear();

			QPixmap ap = G_ROOM_SKIN.getCardAvatarPixmap(player->getGeneralName());
			if(known.isEmpty()){
				pile->addAction(ap,player->getLogName())->setEnabled(false);
			} else {
				QMenu*submenu = pile->addMenu(ap,player->getLogName());
				QAction*action = submenu->addAction(ap,tr("View in new dialog"));
				action->setData(player->objectName());
				connect(action,SIGNAL(triggered()),this,SLOT(showPlayerCards()));

				submenu->addSeparator();
				foreach (const Card*card,known){
					const Card*engine_card = Sanguosha->getEngineCard(card->getId());
					submenu->addAction(G_ROOM_SKIN.getCardSuitPixmap(engine_card->getSuit()),engine_card->getFullName());
				}
			}
		}
		miscellaneous_menu->addSeparator();

		pile = miscellaneous_menu->addMenu(tr("View Maxcards"));
		foreach (const ClientPlayer*player,item2player.values()){
			pile->addAction(G_ROOM_SKIN.getCardAvatarPixmap(player->getGeneralName()),QString("%1：%2").arg(player->getLogName()).arg(player->getMaxCards()));
		}
		//QAction*maxcards = miscellaneous_menu->addAction(tr("View Maxcards"));
		//connect(maxcards,SIGNAL(triggered()),this,SLOT(viewMaxCards()));
		QAction*distance = miscellaneous_menu->addAction(tr("View distance"));
		connect(distance,SIGNAL(triggered()),this,SLOT(viewDistance()));
		QAction*discard = miscellaneous_menu->addAction(tr("View Discard pile"));
		connect(discard,SIGNAL(triggered()),this,SLOT(toggleDiscards()));

		miscellaneous_menu->addSeparator();

		miscellaneous_menu->popup(event->screenPos());
	} else if(ServerInfo.FreeChoose&&arrange_button){
		QGraphicsObject*obj = item->toGraphicsObject();
		if(obj&&Sanguosha->getGeneral(obj->objectName())){
			to_change = qobject_cast<CardItem*>(obj);
			change_general_menu->popup(event->screenPos());
		}
	}
}

void RoomScene::chooseGeneral(const QStringList&generals)
{
	QApplication::alert(main_window);
	if(!main_window->isActiveWindow())
		Sanguosha->playSystemAudioEffect("prelude");

	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	if(generals.isEmpty())
		m_choiceDialog = new FreeChooseDialog("",main_window);
	else
		m_choiceDialog = new ChooseGeneralDialog(generals,main_window);
}

void RoomScene::chooseSuit(const QStringList&suits)
{
	QDialog*dialog = new QDialog;
	QVBoxLayout*layout = new QVBoxLayout;

	foreach (QString suit,suits){
		QCommandLinkButton*button = new QCommandLinkButton;
		button->setIcon(QIcon(QString("image/system/cardsuit/%1.png").arg(suit)));
		button->setText(Sanguosha->translate(suit));
		button->setObjectName(suit);

		layout->addWidget(button);

		connect(button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerChooseSuit()));
		connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
	}

	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerChooseSuit()));

	dialog->setObjectName(".");
	dialog->setWindowTitle(tr("Please choose a suit"));
	dialog->setLayout(layout);
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::chooseKingdom(const QStringList&kingdoms)
{
	QDialog*dialog = new QDialog;
	QVBoxLayout*layout = new QVBoxLayout;

	foreach (QString kingdom,kingdoms){
		QCommandLinkButton*button = new QCommandLinkButton;
		QPixmap kingdom_pixmap(QString("image/kingdom/icon/%1.png").arg(kingdom));

		button->setIcon(QIcon(kingdom_pixmap));
		button->setIconSize(kingdom_pixmap.size());
		button->setText(Sanguosha->translate(kingdom));
		button->setObjectName(kingdom);

		layout->addWidget(button);

		connect(button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerChooseKingdom()));
		connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
	}
	dialog->setObjectName(".");
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerChooseKingdom()));

	//dialog->setObjectName(".");
	dialog->setWindowTitle(tr("Please choose a kingdom"));
	dialog->setLayout(layout);
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

QGroupBox*RoomScene::createOptionBox(const QString&skillName,const QStringList&options,QDialog*dialog,bool enabled)
{
	QGroupBox*box = new QGroupBox(tr("Please choose:"));
	QHBoxLayout*layout = new QHBoxLayout;

	QGroupBox*box1 = new QGroupBox("");
	QVBoxLayout*layout1 = new QVBoxLayout;

	foreach (QString option,options){
		QCommandLinkButton*button = new QCommandLinkButton;
		button->setObjectName(option);
		QString src,arg,arg2;
		if(option.contains("=")){
			QStringList _options = option.split("=");
			option = _options.first();
			src = _options.at(1);
			if(_options.length() >= 3)
				arg = _options.at(2);
			if(_options.length() >= 4)
				arg2 = _options.at(3);
		}

		QString text = QString("%1:%2").arg(skillName).arg(option);
		QString translated = Sanguosha->translate(text);
		if(text==translated)
			translated = Sanguosha->translate(option);

		if(!src.isEmpty())  //考虑到tip是数字的问题
			translated.replace("%src",ClientInstance->getPlayerName(src));
		if(!arg2.isEmpty())
			translated.replace("%arg2",ClientInstance->getPlayerName(arg2));
		if(!arg.isEmpty())
			translated.replace("%arg",ClientInstance->getPlayerName(arg));

		button->setText(translated);

		const Skill*s = Sanguosha->getSkill(option);
		if(s){
			LuaLocker locker;
			button->setToolTip(buildOracleTooltip(s->getOracleText(Self), s->getDescription(Self)));
		}else{
			QString original_tooltip = QString(":%1").arg(text);
			QString tooltip = Sanguosha->translate(original_tooltip);
			if(tooltip==original_tooltip){
				original_tooltip = QString(":%1").arg(option);
				tooltip = Sanguosha->translate(original_tooltip);
			}
			if(tooltip!=original_tooltip)
				button->setToolTip(tooltip);
		}

		if(enabled){
			connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
			connect(button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerMakeChoice()));
		}else
			button->setEnabled(false);

		layout1->addWidget(button);
	}

	box->setLayout(layout);
	box1->setLayout(layout1);

	layout->addWidget(box1);
	return box;
}

void RoomScene::chooseOption(const QString&skillName,const QStringList&options,const QString&except_options,const QString&tip)
{
	QDialog*dialog = new QDialog;
	QVBoxLayout*layout = new QVBoxLayout;
	dialog->setWindowTitle(Sanguosha->translate(skillName));

	if(skillName.startsWith("BossModeExpStore")){
		ClientPlayer*clientplayer = ClientInstance->getPlayer(Self->property("bossmodeexp").toString());
		layout->addWidget(new QLabel(QString("%1 %2").arg(clientplayer->getLogName()).arg(clientplayer->getMark("@bossExp"))));
	}

	if(skillName=="BossModeExpStore"){
		QGroupBox*box = nullptr;
		QGridLayout*gridlayout = nullptr;
		int index = 0,row = 0,column = 0;
		QList<QCommandLinkButton*> buttons;
		QCommandLinkButton*cancel_button = nullptr;
		QStringList acquiredskills = Self->property("bossmodeacquiredskills").toString().split("+");
		foreach (QString option,Self->property("bossmodeexpallchoices").toString().split("+")){
			QCommandLinkButton*button = new QCommandLinkButton;
			QStringList optionlist = option.split("|");
			if(optionlist.length()==2){
				QString text = QString("%1:%2").arg(skillName).arg(optionlist.last());
				QString translated = optionlist.first()+Sanguosha->translate(text);
				button->setObjectName(option);
				button->setText(translated);
				if(!options.contains(option))
					button->setEnabled(false);
				buttons << button;
			} else if(optionlist.length()==3){ // skill names
				if(!box){
					box = new QGroupBox(dialog);
					box->setTitle(Sanguosha->translate("skill"));
					gridlayout = new QGridLayout;
				}
				QString skill = optionlist.last();
				QString translated = optionlist.first()+Sanguosha->translate(skill);
				if(skill.startsWith("nos"))
					translated.append(Sanguosha->translate("nosskill"));
				if(acquiredskills.contains(skill))
					translated.append(Sanguosha->translate("(acquired)"));
				button->setObjectName(option);
				button->setText(translated);
				if(!options.contains(option))
					button->setEnabled(false);

				const Skill *s = Sanguosha->getSkill(skill);
				button->setToolTip(buildOracleTooltip(s ? s->getOracleText(Self) : QString(), Sanguosha->translate(":"+skill)));

				Q_ASSERT(box&&gridlayout);
				row = index/6;
				column = index % 6;
				index++;
				gridlayout->addWidget(button,row,column);
			} else if(option=="cancel"){
				QString text = QString("%1:%2").arg(skillName).arg(option);
				QString translated = Sanguosha->translate(text);

				button->setObjectName(option);
				button->setText(translated);
				cancel_button = button;
			}
			connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
			connect(button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerMakeChoice()));
		}
		if(buttons.length() > 0){
			QHBoxLayout*hlayout = new QHBoxLayout;
			foreach(QCommandLinkButton*button,buttons)
				hlayout->addWidget(button);
			layout->addLayout(hlayout);
		}
		if(box){
			box->setLayout(gridlayout);
			layout->addWidget(box);
		}
		if(cancel_button)
			layout->addWidget(cancel_button);
		dialog->setLayout(layout);
	} else {
		QHBoxLayout*hlayout = new QHBoxLayout;
		QString new_tip = tip;

		if(skillName.contains("guhuo")&&!guhuo_log.isEmpty()&&tip.isEmpty()){
			new_tip = guhuo_log;
			guhuo_log = "";
		}

		if(!new_tip.isEmpty()){
			QString tip_text = QString("%1:%2").arg(skillName).arg(new_tip);
			QString tip_translated = Sanguosha->translate(tip_text);
			if(tip_text==tip_translated)
				tip_translated = Sanguosha->translate(new_tip);
			QLabel*tip_lab = new QLabel(tip_translated,dialog);
			tip_lab->setObjectName("tip_text");
			tip_lab->setMaximumWidth(240);
			tip_lab->setWordWrap(true);
			hlayout->addWidget(tip_lab);
		}

		int x = 0;
		for (int i = 1;i < options.length();i++){
			x = options.length()/i;
			if(x<10) break;
		}
		QStringList options1;
		for (int i = 0;i < options.length();i++){
			options1 << options.at(i);
			if(options1.length()>x||i==options.length()-1){
				hlayout->addWidget(createOptionBox(skillName,options1,dialog));
				options1.clear();
			}
		}
		if(!except_options.isEmpty())
			hlayout->addWidget(createOptionBox(skillName,except_options.split("+"),dialog,false));

		dialog->setLayout(hlayout);
	}

	dialog->setObjectName("cancel");
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerMakeChoice()));

	Sanguosha->playSystemAudioEffect("pop-up");
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::chooseCard(const ClientPlayer*player,const QString&flags,const QString&reason,
	bool handcard_visible,Card::HandlingMethod method,QList<int> disabled_ids,bool can_cancel)
{
	PlayerCardDialog*dialog = new PlayerCardDialog(player,flags,handcard_visible,method,disabled_ids,can_cancel);
	dialog->setWindowTitle(Sanguosha->translate(reason));
	connect(dialog,SIGNAL(card_id_chosen(int)),ClientInstance,SLOT(onPlayerChooseCard(int)));
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerChooseCard()));
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::chooseOrder(QSanProtocol::Game3v3ChooseOrderCommand reason)
{
	QDialog*dialog = new QDialog;
	if(reason==S_REASON_CHOOSE_ORDER_SELECT)
		dialog->setWindowTitle(tr("The order who first choose general"));
	else if(reason==S_REASON_CHOOSE_ORDER_TURN)
		dialog->setWindowTitle(tr("The order who first in turn"));

	QLabel*prompt = new QLabel(tr("Please select the order"));
	OptionButton*warm_button = new OptionButton("image/system/3v3/warm.png",tr("Warm"));
	warm_button->setObjectName("warm");
	OptionButton*cool_button = new OptionButton("image/system/3v3/cool.png",tr("Cool"));
	cool_button->setObjectName("cool");

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addWidget(warm_button);
	hlayout->addWidget(cool_button);

	QVBoxLayout*layout = new QVBoxLayout;
	layout->addWidget(prompt);
	layout->addLayout(hlayout);
	dialog->setLayout(layout);

	connect(warm_button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerChooseOrder()));
	connect(cool_button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerChooseOrder()));
	connect(warm_button,SIGNAL(clicked()),dialog,SLOT(accept()));
	connect(cool_button,SIGNAL(clicked()),dialog,SLOT(accept()));
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerChooseOrder()));
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::chooseRole(const QString&scheme,const QStringList&roles)
{
	QDialog*dialog = new QDialog;
	dialog->setWindowTitle(tr("Select role"));

	QLabel*prompt = new QLabel(tr("Please select a role"));
	QVBoxLayout*layout = new QVBoxLayout;

	layout->addWidget(prompt);

	static QMap<QString,QString> jargon;
	if(jargon.isEmpty()){
		jargon["lord"] = tr("Warm leader");
		jargon["loyalist"] = tr("Warm guard");
		jargon["renegade"] = tr("Cool leader");
		jargon["rebel"] = tr("Cool guard");

		jargon["leader1"] = tr("Leader of Team 1");
		jargon["guard1"] = tr("Guard of Team 1");
		jargon["leader2"] = tr("Leader of Team 2");
		jargon["guard2"] = tr("Guard of Team 2");
	}

	foreach (QString role,roles){
		QCommandLinkButton*button = new QCommandLinkButton(jargon[role]);
		if(scheme=="AllRoles")
			button->setIcon(QIcon(QString("image/system/roles/%1.png").arg(role)));
		layout->addWidget(button);
		button->setObjectName(role);
		connect(button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerChooseRole3v3()));
		connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
	}

	QCommandLinkButton*abstain_button = new QCommandLinkButton(tr("Abstain"));
	connect(abstain_button,SIGNAL(clicked()),dialog,SLOT(reject()));
	layout->addWidget(abstain_button);

	dialog->setObjectName("abstain");
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerChooseRole3v3()));

	dialog->setLayout(layout);
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::chooseDirection()
{
	QDialog*dialog = new QDialog;
	dialog->setWindowTitle(tr("Please select the direction"));

	QLabel*prompt = new QLabel(dialog->windowTitle());

	OptionButton*cw_button = new OptionButton("image/system/3v3/cw.png",tr("CW"));
	cw_button->setObjectName("cw");

	OptionButton*ccw_button = new OptionButton("image/system/3v3/ccw.png",tr("CCW"));
	ccw_button->setObjectName("ccw");

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addWidget(cw_button);
	hlayout->addWidget(ccw_button);

	QVBoxLayout*layout = new QVBoxLayout;
	layout->addWidget(prompt);
	layout->addLayout(hlayout);
	dialog->setLayout(layout);

	dialog->setObjectName("ccw");
	connect(ccw_button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerMakeChoice()));
	connect(ccw_button,SIGNAL(clicked()),dialog,SLOT(accept()));
	connect(cw_button,SIGNAL(clicked()),ClientInstance,SLOT(onPlayerMakeChoice()));
	connect(cw_button,SIGNAL(clicked()),dialog,SLOT(accept()));
	connect(dialog,SIGNAL(rejected()),ClientInstance,SLOT(onPlayerMakeChoice()));
	if(m_choiceDialog!=nullptr)
		delete m_choiceDialog;
	m_choiceDialog = dialog;
}

void RoomScene::toggleDiscards()
{
	CardOverview*overview = new CardOverview;
	overview->setWindowTitle(tr("Discarded pile"));
	QList<const Card*> cards;
	foreach(int id,ClientInstance->discarded_list)
		cards << Sanguosha->getEngineCard(id);
	overview->loadFromList(cards);
	overview->show();
}

GenericCardContainer*RoomScene::_getGenericCardContainer(Player::Place place,const Player*player)
{
	if(place==Player::DiscardPile||place==Player::PlaceJudge||place==Player::DrawPile||place==Player::PlaceTable)
		return m_tablePile;// @todo: AG must be a pile with name rather than simply using the name special...
	if(player==Self) return dashboard;
	if(player) return name2photo.value(player->objectName(),nullptr);
	if(place==Player::PlaceSpecial) return card_container;
	Q_ASSERT(false);
	return nullptr;
}

bool RoomScene::_shouldIgnoreDisplayMove(CardsMoveStruct&movement)
{
	if(movement.to_pile_name.startsWith('#')||movement.from_pile_name.startsWith('#'))
		return true;
	if(movement.to_place==Player::DiscardPile)
		return movement.from_place==Player::PlaceTable||movement.from_place==Player::PlaceJudge;
	//static QList<Player::Place> ignore_place;
	//if(ignore_place.isEmpty()) ignore_place << Player::DiscardPile << Player::PlaceTable << Player::PlaceJudge;
	return false;//movement.reason.m_skillName!="manjuan"&&ignore_place.contains(movement.from_place)&&ignore_place.contains(movement.to_place);
}

bool RoomScene::_processCardsMove(CardsMoveStruct&move,bool isLost)
{
	if(isLost){
		if(move.from_place==Player::PlaceTable&&RenPile.length()>0){
			QList<int> ids;
			foreach(int id,move.card_ids){
				if(RenPile.contains(id)){
					RenPile.removeAll(id);
					ids << id;
				}
			}
			if(ids.length()>0){
				QStringList Rens;
				foreach(int id,RenPile){
					const Card*c = Sanguosha->getEngineCard(id);
					if(c) Rens << c->getLogName();
				}
				ren_widget->setVisible(Rens.length()>0);
				QPushButton*button = (QPushButton*)ren_widget->widget();
				button->setText(QString("仁（%1）").arg(Rens.length()));
				button->setToolTip(Rens.join("<br/>"));
				log_box->appendLog("$removeRenPile","",QStringList(),ListI2S(ids).join("+"),QString::number(ids.length()),"ren_pile");
			}
		}
	}else{
		if(move.to_place==Player::PlaceTable&&move.to_pile_name=="ren_pile"){
			if(ren_widget==nullptr){
				QPushButton*button = new QPushButton;
				button->setObjectName("ren_widget");
				button->setProperty("private_pile","true");
				ren_widget = new QGraphicsProxyWidget(dashboard);
				ren_widget->setObjectName("ren_widget");
				ren_widget->setWidget(button);
				ren_widget->resize(70,30);
				ren_widget->setParent(this);
				ren_widget->setZValue(10);
				ren_widget->setPos(QPointF(width()*0.15,-height()*0.1));
			}
			QStringList Rens;
			RenPile << move.card_ids;
			foreach(int id,RenPile){
				const Card*c = Sanguosha->getEngineCard(id);
				if(c) Rens << c->getLogName();
			}
			ren_widget->setVisible(true);
			QPushButton*button = (QPushButton*)ren_widget->widget();
			button->setText(QString("仁（%1）").arg(Rens.length()));
			button->setToolTip(Rens.join("<br/>"));
			log_box->appendLog("$addRenPile",move.reason.m_playerId,QStringList(),ListI2S(move.card_ids).join("+"),QString::number(move.card_ids.length()),"ren_pile");
		}
	}
	/*_MoveCardsClassifier cls(move);
	// delayed trick processed;
	if(move.from_place==Player::PlaceDelayedTrick&&move.to_place==Player::PlaceTable){
		if(isLost) m_move_cache[cls] = move;
		return true;
	}
	CardsMoveStruct tmpMove = m_move_cache.value(cls,CardsMoveStruct());
	if(tmpMove.from_place!=Player::PlaceUnknown){
		move.from_pile_name = tmpMove.from_pile_name;
		move.from_place = tmpMove.from_place;
		move.from = tmpMove.from;
	}
	if(!isLost) m_move_cache.remove(cls);*/
	return false;
}

void RoomScene::getCards(int moveId,QList<CardsMoveStruct> card_moves)
{
	for (int i = 0;i < card_moves.length();i++){
		_processCardsMove(card_moves[i],false);
		if(_shouldIgnoreDisplayMove(card_moves[i])) continue;
		card_container->m_currentPlayer = (ClientPlayer*)card_moves[i].to;
		QList<CardItem*> cards = _m_cardsMoveStash[moveId].takeFirst();
		foreach (CardItem*card,cards){
			card->setFlag(QGraphicsItem::ItemIsMovable,false);
			if(card_moves[i].card_ids.contains(card->getId())){
				card->setFootnote(_translateMovement(card_moves[i]));
				card->setEnabled(true);
				card->hideFootnote();
			}else{
				cards.removeOne(card);
				card->setVisible(false);
				card->deleteLater();
			}
		}
		keepGetCardLog(card_moves[i]);
		GenericCardContainer*to_container = _getGenericCardContainer(card_moves[i].to_place,card_moves[i].to);
		if(card_moves[i].from||card_moves[i].from_place==Player::PlaceTable){
			foreach (Photo*photo,photos)
				photo->setZValue(to_container==photo?2:1);
		}
		bringToFront(to_container);
		//if(card_moves[i].reason.m_skillName=="zhizhe") continue;
		to_container->addCardItems(cards,card_moves[i]);
	}
}

void RoomScene::loseCards(int moveId,QList<CardsMoveStruct> card_moves)
{
	for (int i = 0;i < card_moves.length();i++){
		_processCardsMove(card_moves[i],true);
		if(_shouldIgnoreDisplayMove(card_moves[i])) continue;
		card_container->m_currentPlayer = (ClientPlayer*)card_moves[i].to;
		GenericCardContainer*from_container = _getGenericCardContainer(card_moves[i].from_place,card_moves[i].from);
		QList<CardItem*> cards = from_container->removeCardItems(card_moves[i].card_ids,card_moves[i].from_place);
		foreach(CardItem*card,cards)
			card->setEnabled(false);
		_m_cardsMoveStash[moveId].append(cards);
		keepLoseCardLog(card_moves[i]);
	}
}

QString RoomScene::_translateMovement(const CardsMoveStruct&move)
{
	if(move.reason.m_reason==CardMoveReason::S_REASON_UNKNOWN) return "";

	QString playerName,targetName;

	if(move.reason.m_playerId==Self->objectName())
		playerName = QString("%1(%2)").arg(Self->getGeneral()->getBriefName()).arg(Sanguosha->translate("yourself"));
	else{
		Photo*srcPhoto = name2photo[move.reason.m_playerId];
		if(srcPhoto) playerName = srcPhoto->getPlayer()->getGeneral()->getBriefName();
	}

	if(move.reason.m_targetId==Self->objectName())
		targetName = QString("%1%2(%3)").arg(Sanguosha->translate("use upon")).arg(Self->getGeneral()->getBriefName()).arg(Sanguosha->translate("yourself"));
	else{
		Photo*dstPhoto = name2photo[move.reason.m_targetId];
		if(dstPhoto) targetName = Sanguosha->translate("use upon").append(dstPhoto->getPlayer()->getGeneral()->getBriefName());
	}

	QString result(playerName+targetName);
	result.append(Sanguosha->translate(move.reason.m_skillName));
	result.append(Sanguosha->translate(move.reason.m_eventName));
	if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE&&move.reason.m_skillName.isEmpty()){
		result.append(Sanguosha->translate("use"));
	} else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_RESPONSE){
		if(move.reason.m_reason==CardMoveReason::S_REASON_RETRIAL)
			result.append(Sanguosha->translate("retrial"));
		else if(move.reason.m_skillName.isEmpty())
			result.append(Sanguosha->translate("response"));
	} else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
		if(move.reason.m_reason==CardMoveReason::S_REASON_RULEDISCARD)
			result.append(Sanguosha->translate("discard"));
		else if(move.reason.m_reason==CardMoveReason::S_REASON_THROW||move.reason.m_reason==CardMoveReason::S_REASON_DISMANTLE)
			result.append(Sanguosha->translate("throw"));
		else if(move.reason.m_reason==CardMoveReason::S_REASON_CHANGE_EQUIP)
			result.append(Sanguosha->translate("change equip"));
	} else if(move.reason.m_reason==CardMoveReason::S_REASON_RECAST){
		result.append(Sanguosha->translate("recast"));
	} else if(move.reason.m_reason==CardMoveReason::S_REASON_PINDIAN){
		result.append(Sanguosha->translate("pindian"));
	} else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_SHOW){
		if(move.reason.m_reason==CardMoveReason::S_REASON_JUDGE)
			result.append(Sanguosha->translate("judge"));
		else if(move.reason.m_reason==CardMoveReason::S_REASON_TURNOVER)
			result.append(Sanguosha->translate("turnover"));
		else if(move.reason.m_reason==CardMoveReason::S_REASON_DEMONSTRATE)
			result.append(Sanguosha->translate("show"));
		else if(move.reason.m_reason==CardMoveReason::S_REASON_PREVIEW)
			result.append(Sanguosha->translate("preview"));
	} else if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_PUT){
		if(move.reason.m_reason==CardMoveReason::S_REASON_PUT){
			result.append(Sanguosha->translate("put"));
			if(move.to_place==Player::DiscardPile)
				result.append(Sanguosha->translate("discardPile"));
			else if(move.to_place==Player::DrawPile)
				result.append(Sanguosha->translate("drawPileTop"));
		} else if(move.reason.m_reason==CardMoveReason::S_REASON_NATURAL_ENTER){
			result.append(Sanguosha->translate("enter"));
			if(move.to_place==Player::DiscardPile)
				result.append(Sanguosha->translate("discardPile"));
			else if(move.to_place==Player::DrawPile)
				result.append(Sanguosha->translate("drawPileTop"));
		} else if(move.reason.m_reason==CardMoveReason::S_REASON_JUDGEDONE){
			result.append(Sanguosha->translate("judgedone"));
		} else if(move.reason.m_reason==CardMoveReason::S_REASON_REMOVE_FROM_PILE){
			result.append(Sanguosha->translate("backinto"));
		} else if(move.reason.m_reason==CardMoveReason::S_REASON_PUT_END){
			result.append(Sanguosha->translate("put"));
			result.append(Sanguosha->translate("drawPileEnd"));
		} else if(move.reason.m_reason==CardMoveReason::S_REASON_SHUFFLE){
			result.append(Sanguosha->translate("shuffle"));
			result.append(Sanguosha->translate("drawPile"));
		}
	}
	return result;
}

void RoomScene::keepLoseCardLog(const CardsMoveStruct&move)
{
	if(move.from_place==Player::DrawPile){
		if(move.to_place==Player::PlaceHand){
			for (int i = 0;i < move.card_ids.length();i++){
				Sanguosha->playSystemAudioEffect(QString("draw%1").arg(qrand()%2+1));
				if(i>1) break;
			}
		}else
			Sanguosha->playSystemAudioEffect("judge");
	}else if(move.from_place==Player::PlaceEquip)
		log_box->appendLog("#Uninstall",move.from_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
}

void RoomScene::keepGetCardLog(const CardsMoveStruct&move)
{
	if(move.to_place==Player::PlaceHand){
		if(move.from_place==Player::DrawPile){
			/*if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID))
				log_box->appendLog("#DrawNCards",move.to_player_name,QStringList()<<move.to_player_name,"",QString::number(move.card_ids.length()));
			else*/
				log_box->appendLog("$DrawCards",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"),QString::number(move.card_ids.length()));
		}else if(move.from_place==Player::DiscardPile)
			log_box->appendLog("$RecycleCard",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
		else if(move.from_place==Player::PlaceSpecial){//&&move.reason.m_reason==CardMoveReason::S_REASON_EXCHANGE_FROM_PILE
			//if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID))
				log_box->appendLog("#GotNCardFromPile",move.to_player_name,QStringList()<<move.from_player_name,ListI2S(move.card_ids).join("+"),move.from_pile_name,QString::number(move.card_ids.length()));
			//else log_box->appendLog("$GotCardFromPile",move.to_player_name,QStringList()<<move.from_player_name,ListI2S(move.card_ids).join("+"),move.from_pile_name);
		}else if(move.from_place==Player::PlaceTable||move.from_place==Player::PlaceJudge){
			if(move.reason.m_reason!=CardMoveReason::S_REASON_PREVIEW&&!move.card_ids.contains(Card::S_UNKNOWN_CARD_ID)){
				if(move.reason.m_reason==CardMoveReason::S_REASON_EXCLUSIVE)
					log_box->appendLog("$TakeAG",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
				else log_box->appendLog("$GotCardBack",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
			}
		}else if(move.from){
			if(move.from==move.to&&move.from_place==Player::PlaceEquip){
				log_box->appendLog("$GotCardBack",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
			}else{
				if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID))
					log_box->appendLog("#MoveNCards",move.from_player_name,QStringList()<<move.to_player_name,"",QString::number(move.card_ids.length()));
				else log_box->appendLog("$MoveCard",move.from_player_name,QStringList()<<move.to_player_name,ListI2S(move.card_ids).join("+"));
			}
		}
	}else if(move.to_place==Player::PlaceSpecial){
		if(!move.to_pile_name.startsWith('#')){// private pile
			if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID)) log_box->appendLog("#RemoveFromGame",move.to_player_name,QStringList(),"",move.to_pile_name,QString::number(move.card_ids.length()));
			else log_box->appendLog("$AddToPile",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"),move.to_pile_name);
		}
	}else if(move.from){
		if(move.to_place==Player::PlaceDelayedTrick){
			QString type = "$LightningMove";// both src and dest are player
			if(move.from_place!=Player::PlaceDelayedTrick&&move.reason.m_reason!=CardMoveReason::S_REASON_TRANSFER)
				type = "$PasteCard";
			if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID)) type = "#LightningMove";
			log_box->appendLog(type,move.from_player_name,QStringList()<<move.to_player_name,ListI2S(move.card_ids).join("+"),QString::number(move.card_ids.length()));
		}else if(move.to_place==Player::DrawPile){
			if(move.reason.m_reason==CardMoveReason::S_REASON_PUT&&move.reason.m_skillName=="luck_card") return;
			QString type = "$PutCard";
			if(move.reason.m_reason==CardMoveReason::S_REASON_SHUFFLE)
				type = "$ShuffleCard";
			else if(move.reason.m_reason==CardMoveReason::S_REASON_PUT_END)
				type = "$PutCardEnd";
			log_box->appendLog(type,move.from_player_name,QStringList(),ListI2S(move.card_ids).join("+"),QString::number(move.card_ids.length()));
		}
	}
	if(move.to_place==Player::PlaceEquip){
		if(move.from&&move.from!=move.to){
			if(move.card_ids.contains(Card::S_UNKNOWN_CARD_ID)) log_box->appendLog("#MoveNCards",move.from_player_name,QStringList()<<move.to_player_name,"",QString::number(move.card_ids.length()));
			else log_box->appendLog("$MoveCard",move.from_player_name,QStringList()<<move.to_player_name,ListI2S(move.card_ids).join("+"));
		}
		log_box->appendLog("#Install",move.to_player_name,QStringList(),ListI2S(move.card_ids).join("+"));
	}
	if(move.reason.m_reason==CardMoveReason::S_REASON_TURNOVER)
		log_box->appendLog("$TurnOver",move.reason.m_playerId,QStringList(),ListI2S(move.card_ids).join("+"));
}

void RoomScene::addSkillButton(const Skill*skill)
{
	if(!skill) return;
	//if(!skill||skill->inherits("SPConvertSkill")) return;
	// check duplication
	foreach(QSanSkillButton*button,m_skillButtons)
		if(button->getSkill()==skill) return;
	
	QSanSkillButton*btn = dashboard->addSkillButton(skill->objectName());
	if(!btn||m_skillButtons.contains(btn)) return;
	if(!m_replayControl){
		if(btn->getViewAsSkill()){
			connect(btn,SIGNAL(skill_activated()),dashboard,SLOT(skillButtonActivated()));
			connect(btn,SIGNAL(skill_activated()),this,SLOT(onSkillActivated()));
			connect(btn,SIGNAL(skill_deactivated()),dashboard,SLOT(skillButtonDeactivated()));
			connect(btn,SIGNAL(skill_deactivated()),this,SLOT(onSkillDeactivated()));
		}

		QDialog*dialog = skill->getDialog();
		if(dialog){
			dialog->setParent(main_window,Qt::Dialog);
			connect(btn,SIGNAL(skill_activated()),dialog,SLOT(popup()));
			connect(btn,SIGNAL(skill_deactivated()),dialog,SLOT(reject()));
			disconnect(btn,SIGNAL(skill_activated()),this,SLOT(onSkillActivated()));
			connect(dialog,SIGNAL(onButtonClick()),this,SLOT(onSkillActivated()));
			if(dialog->objectName()=="qice")
				connect(dialog,SIGNAL(onButtonClick()),dashboard,SLOT(selectAll()));
		}
	}
	m_skillButtons << btn;
}

void RoomScene::acquireSkill(const ClientPlayer*,const QString&skill_name)
{
	/*log_box->appendLog("#AcquireSkill",player->objectName(),QStringList(),"",skill_name);*/

	addSkillButton(Sanguosha->getSkill(skill_name));
}

void RoomScene::updateSkillButtons(bool isPrepare)
{
	QList<const Skill*> skill_list;
	if(isPrepare){
		if(Self->getGeneral())
			skill_list << Self->getGeneral()->getVisibleSkillList();
		if(Self->getGeneral2())
			skill_list << Self->getGeneral2()->getVisibleSkillList();
	} else
		skill_list = Self->getVisibleSkillList();
	foreach (const Skill*skill,skill_list){
		if(skill->isLordSkill()&&!Self->hasLordSkill(skill,true))
			continue;
		addSkillButton(skill);
	}

	// disable all skill buttons
	foreach(QSanSkillButton*button,m_skillButtons){
		if(Self->hasSkill(button->objectName(),true))
			button->setEnabled(false);
		else
			detachSkill(button->objectName());
	}
}

void RoomScene::useSelectedCard()
{
	switch (ClientInstance->getStatus()&Client::ClientStatusBasicMask){
	case Client::Playing: {
		const Card*card = dashboard->getSelected();
		if(card) useCard(card);
		break;
	}
	case Client::Responding:
	case Client::AskForShowOrPindian: {
		const Card*card = dashboard->getSelected();
		if(card){
			prompt_box->disappear();
			ClientInstance->onPlayerResponseCard(card,selected_targets);
		}
		dashboard->unselectAll();
		break;
	}
	case Client::Discarding:
	case Client::Exchanging: {
		const Card*card = dashboard->pendingCard();
		if(card){
			dashboard->stopPending();
			prompt_box->disappear();
			ClientInstance->onPlayerDiscardCards(card);
		}
		break;
	}
	case Client::NotActive: {
		QMessageBox::warning(main_window,tr("Warning"),
			tr("The OK button should be disabled when client is not active!"));
		return;
	}
	case Client::AskForAG: {
		prompt_box->disappear();
		ClientInstance->onPlayerChooseAG(-1);
		return;
	}
	case Client::ExecDialog: {
		QMessageBox::warning(main_window,tr("Warning"),
			tr("The OK button should be disabled when client is in executing dialog"));
		return;
	}
	case Client::AskForSkillInvoke: {
		prompt_box->disappear();
		dashboard->highlightEquip(ClientInstance->getSkillNameToInvoke(),false);
		ClientInstance->onPlayerInvokeSkill(true);
		break;
	}
	case Client::AskForPlayerChoose: {
		prompt_box->disappear();
		ClientInstance->onPlayerChoosePlayer(selected_targets);
		break;
	}
	case Client::AskForYiji: {
		const Card*card = dashboard->pendingCard();
		if(card){
			dashboard->stopPending();
			prompt_box->disappear();
			ClientInstance->onPlayerReplyYiji(card,selected_targets.first());
		}
		break;
	}
	case Client::AskForGuanxing: {
		guanxing_x_box->reply();
		break;
	}
	case Client::AskForGongxin: {
		card_container->clear();
		ClientInstance->onPlayerReplyGongxin();
		break;
	}
	}

	if(dashboard->currentSkill())
		dashboard->stopPending();
	else{
		foreach (const QString&pile,Self->getPileNames()){
			if(pile.startsWith("&")||pile=="wooden_ox")
				dashboard->retractPileCards(pile);
		}
	}
}

void RoomScene::onEnabledChange()
{
	QGraphicsItem*photo = qobject_cast<QGraphicsItem*>(sender());
	if(!photo) return;
	if(photo->isEnabled())
		animations->effectOut(photo);
	else
		animations->sendBack(photo);
}

void RoomScene::useCard(const Card*card)
{
	if(card->targetFixed()||card->targetsFeasible(selected_targets,Self))
		ClientInstance->onPlayerResponseCard(card,selected_targets);
	enableTargets(nullptr);
}

void RoomScene::callViewAsSkill()
{
	const Card*card = dashboard->pendingCard();
	if(card&&card->isAvailable(Self)){
		// use card
		dashboard->stopPending();
		useCard(card);
	}
}

void RoomScene::cancelViewAsSkill()
{
	dashboard->stopPending();
	Client::Status status = ClientInstance->getStatus();
	updateStatus(status,status);
}

void RoomScene::selectTarget(int order,bool multiple)
{
	if(!multiple) unselectAllTargets();

	QGraphicsItem*to_select = nullptr;
	if(order==0)
		to_select = dashboard;
	else if(order > 0&&order <= photos.length())
		to_select = photos.at(order-1);

	if(!to_select) return;
	if(!(to_select->isSelected()
		|| (!to_select->isSelected()&&(to_select->flags()&QGraphicsItem::ItemIsSelectable))))
		return;

	to_select->setSelected(!to_select->isSelected());
}

void RoomScene::selectNextTarget(bool multiple)
{
	if(!multiple)
		unselectAllTargets();

	QList<QGraphicsItem*> targets;
	foreach (Photo*photo,photos){
		if(photo->flags()&QGraphicsItem::ItemIsSelectable)
			targets << photo;
	}

	if(dashboard->flags()&QGraphicsItem::ItemIsSelectable)
		targets << dashboard;

	for (int i = 0;i < targets.length();i++){
		if(targets.at(i)->isSelected()){
			for (int j = i+1;j < targets.length();j++){
				if(!targets.at(j)->isSelected()){
					targets.at(j)->setSelected(true);
					return;
				}
			}
		}
	}

	foreach (QGraphicsItem*target,targets){
		if(!target->isSelected()){
			target->setSelected(true);
			break;
		}
	}
}

void RoomScene::unselectAllTargets(const QGraphicsItem*except)
{
	if(dashboard!=except) dashboard->setSelected(false);

	foreach (Photo*photo,photos){
		if(photo!=except&&photo->isSelected())
			photo->setSelected(false);
	}
}

void RoomScene::doTimeout()
{
	switch (ClientInstance->getStatus()&Client::ClientStatusBasicMask){
	case Client::Playing: {
		discard_button->click();
		break;
	}
	case Client::Responding:
	case Client::Discarding:
	case Client::Exchanging:
	case Client::ExecDialog:
	case Client::AskForShowOrPindian: {
		doCancelButton();
		break;
	}
	case Client::AskForPlayerChoose: {
		ClientInstance->onPlayerChoosePlayer(QList<const Player*>());
		dashboard->stopPending();
		prompt_box->disappear();
		break;
	}
	case Client::AskForAG: {
		prompt_box->disappear();
		int card_id = card_container->getFirstEnabled();
		ClientInstance->onPlayerChooseAG(card_id);
		break;
	}
	case Client::AskForSkillInvoke: {
		cancel_button->click();
		break;
	}
	case Client::AskForYiji: {
		if(cancel_button->isEnabled())
			cancel_button->click();
		else
			doCancelButton();
		break;
	}
	case Client::AskForGuanxing:
	case Client::AskForGongxin: {
		ok_button->click();
		break;
	}
	case Client::AskForGeneralTaken: {
		break;
	}
	case Client::AskForArrangement: {
		arrange_items << down_generals.mid(0,3-arrange_items.length());
		finishArrange();
	}
	default:
		break;
	}
}

void RoomScene::showPromptBox()
{
	bringToFront(prompt_box);
	prompt_box->appear();
}

void RoomScene::updateStatus(Client::Status oldStatus,Client::Status newStatus)
{
	if(Self->property("onsole_target").toString()!=onsole_target){
		onsole_target = Self->property("onsole_target").toString();
		QList<Photo*>photos2;
		if(onsole_target.isEmpty()){
			ClientPlayer*self = ClientInstance->getPlayer(Self->getTag("onsoleFrom").toString());
			item2player.insert(dashboard,self);
			dashboard->setPlayer(self);
			for (int i = 0;i < photos.length();i++){
				if(photos[i]->getPlayer()==self){
					photos[i]->setPlayer(Self);
					item2player.insert(photos[i],Self);
					Self = self;
					photos[i]->repaintAll(true);
				}
			}
			dashboard->repaintAll(true);
			foreach (QSanSkillButton*button,m_skillButtons){
				dashboard->removeSkillButton(button->objectName());
				m_skillButtons.removeAll(button);
				button->deleteLater();
			}
			int n = -1;
			updateSkillButtons();
			foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
				if(cp==Self) n = 0;
				else if(n==0){
					for (int i = 0;i < photos.length();i++){
						if(photos[i]->getPlayer()==cp)
							photos2 << photos[i];
					}
				}
			}
			foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
				if(cp==Self) break;
				for (int i = 0;i < photos.length();i++){
					if(photos[i]->getPlayer()==cp)
						photos2 << photos[i];
				}
			}
		}else{
			ClientPlayer*player = ClientInstance->getPlayer(onsole_target);
			player->setTag("onsoleFrom", Self->objectName());
			item2player.insert(dashboard,player);
			dashboard->setPlayer(player);
			for (int i = 0;i < photos.length();i++){//swapItemsAt
				if(photos[i]->getPlayer()==player){
					item2player.insert(photos[i],Self);
					photos[i]->setPlayer(Self);
					Self = player;
					photos[i]->repaintAll(true);
				}
			}
			dashboard->repaintAll(true);
			foreach (QSanSkillButton*button,m_skillButtons){
				dashboard->removeSkillButton(button->objectName());
				m_skillButtons.removeAll(button);
				button->deleteLater();
			}
			int n = -1;
			updateSkillButtons();
			foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
				if(cp==Self) n = 0;
				else if(n==0){
					for (int i = 0;i < photos.length();i++){
						if(photos[i]->getPlayer()==cp)
							photos2 << photos[i];
					}
				}
			}
			foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
				if(cp==Self) break;
				for (int i = 0;i < photos.length();i++){
					if(photos[i]->getPlayer()==cp)
						photos2 << photos[i];
				}
			}
		}
		photos = photos2;
		updateTable();
	}
	foreach (QSanSkillButton*button,m_skillButtons){
		const ViewAsSkill*vsSkill = button->getViewAsSkill();
		if(vsSkill){
			QString pattern = Sanguosha->getCurrentCardUsePattern();
			CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
			if(newStatus==Client::Playing){
				reason = CardUseStruct::CARD_USE_REASON_PLAY;
				QDialog*dialog = button->getSkill()->getDialog();
				if(dialog&&dialog->parent()!=main_window){
					dialog->setParent(main_window,Qt::Dialog);
					connect(button,SIGNAL(skill_activated()),dialog,SLOT(popup()));
					connect(button,SIGNAL(skill_deactivated()),dialog,SLOT(reject()));
					disconnect(button,SIGNAL(skill_activated()),this,SLOT(onSkillActivated()));
					connect(dialog,SIGNAL(onButtonClick()),this,SLOT(onSkillActivated()));
					if(dialog->objectName()=="qice")
						connect(dialog,SIGNAL(onButtonClick()),dashboard,SLOT(selectAll()));
				}
			} else if((newStatus&Client::ClientStatusBasicMask)==Client::Responding){
				if(newStatus==Client::RespondingUse)
					reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
				else{
					static QRegExp rx("@@?([_A-Za-z]+)(\\d+)?!?");
					if(rx.exactMatch(pattern)||newStatus==Client::Responding)
						reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
				}
			}
			button->setEnabled(/*!pattern.endsWith("!")&&*/vsSkill->isAvailable(Self,reason,pattern));
		} else
			button->setEnabled(button->getSkill()->getFrequency(Self)==Skill::Wake&&Self->getMark(button->objectName())>0);
	}

	_m_superDragStarted = false;

	switch (newStatus&Client::ClientStatusBasicMask){
	case Client::NotActive: {
		if(oldStatus==Client::ExecDialog){
			if(m_choiceDialog!=nullptr&&m_choiceDialog->isVisible())
				m_choiceDialog->hide();
		} else if(oldStatus==Client::AskForGuanxing||oldStatus==Client::AskForGongxin){
			guanxing_x_box->clear();
			if(!card_container->retained())
				card_container->clear();
		}
		prompt_box->disappear();
		ClientInstance->getPromptDoc()->clear();

		dashboard->disableAllCards();
		selected_targets.clear();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(false);

		if(dashboard->currentSkill())
			dashboard->stopPending();

		dashboard->hideProgressBar();
		break;
	}
	case Client::Responding: {
		showPromptBox();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(ClientInstance->m_isDiscardActionRefusable);
		discard_button->setEnabled(false);

		QString pattern = Sanguosha->getCurrentCardUsePattern();
		static QRegExp rx("@@?([_A-Za-z]+)(\\d+)?!?");
		if(rx.exactMatch(pattern)){
			QString skill_name = rx.capturedTexts().at(1);
			if(skill_name.endsWith("!")) skill_name.chop(1);
			const ViewAsSkill*skill = Sanguosha->getViewAsSkill(skill_name);
			if(skill){
				CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
				if(newStatus==Client::RespondingUse) reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
				Self->addMark("ViewAsSkill_"+skill_name+"Effect");
				bool available = skill->isAvailable(Self,reason,pattern);
				Self->removeMark("ViewAsSkill_"+skill_name+"Effect");
				if(!available){
					ClientInstance->onPlayerResponseCard(nullptr);
					return;
				}
				foreach (QSanSkillButton*button,m_skillButtons){
					if(button->getViewAsSkill()!=skill) continue;
					if(skill->isAvailable(Self,reason,pattern)){
						button->click();
						QDialog*dialog = button->getSkill()->getDialog();
						if(dialog&&dialog->parent()!=main_window){
							dialog->setParent(main_window,Qt::Dialog);
							connect(button,SIGNAL(skill_activated()),dialog,SLOT(popup()));
							connect(button,SIGNAL(skill_deactivated()),dialog,SLOT(reject()));
							disconnect(button,SIGNAL(skill_activated()),this,SLOT(onSkillActivated()));
							connect(dialog,SIGNAL(onButtonClick()),this,SLOT(onSkillActivated()));
							if(dialog->objectName()=="qice")
								connect(dialog,SIGNAL(onButtonClick()),dashboard,SLOT(selectAll()));
						}
					}
					break;
				}
				dashboard->startPending(skill);
				if(skill->inherits("OneCardViewAsSkill")&&Config.EnableIntellectualSelection)
					dashboard->selectOnlyCard();
			}
		} else {
			if(pattern.endsWith("!")) pattern.chop(1);
			response_skill->setPattern(pattern);
			if(newStatus==Client::RespondingForDiscard)
				response_skill->setRequest(Card::MethodDiscard);
			else if(newStatus==Client::RespondingNonTrigger)
				response_skill->setRequest(Card::MethodNone);
			else if(newStatus==Client::RespondingUse)
				response_skill->setRequest(Card::MethodUse);
			else
				response_skill->setRequest(Card::MethodResponse);
			dashboard->startPending(response_skill);
			if(Config.EnableIntellectualSelection)
				dashboard->selectOnlyCard();
		}
		break;
	}
	case Client::AskForShowOrPindian: {
		showPromptBox();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(false);

		showorpindian_skill->setPattern(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
		dashboard->startPending(showorpindian_skill);
		break;
	}
	case Client::Playing: {
		dashboard->enableCards();
		bringToFront(dashboard);
		ok_button->setEnabled(false);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(true);
		break;
	}
	case Client::Discarding:
	case Client::Exchanging: {
		showPromptBox();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(ClientInstance->m_isDiscardActionRefusable);
		discard_button->setEnabled(false);

		discard_skill->setNum(ClientInstance->discard_num);
		discard_skill->setMinNum(ClientInstance->min_num);
		discard_skill->setIncludeEquip(ClientInstance->m_canDiscardEquip);
		discard_skill->setIsDiscard(newStatus!=Client::Exchanging);
		discard_skill->setPattern(ClientInstance->m_cardDiscardPattern);
		dashboard->startPending(discard_skill);
		break;
	}
	case Client::ExecDialog: {
		if(m_choiceDialog!=nullptr){
			m_choiceDialog->setParent(main_window,Qt::Dialog);
			m_choiceDialog->show();
			ok_button->setEnabled(false);
			cancel_button->setEnabled(false);
			discard_button->setEnabled(false);
		}
		break;
	}
	case Client::AskForSkillInvoke: {
		QString skill_name = ClientInstance->getSkillNameToInvoke();
		if(skill_name=="shefu_cancel"){
			QString data = ClientInstance->getSkillNameToInvokeData().split(":").last();
			if(m_ShefuAskState==ShefuAskNone||(m_ShefuAskState==ShefuAskNecessary&&Self->getMark("Shefu_"+data)==0)){
				ClientInstance->onPlayerInvokeSkill(false);
				return;
			}
		}
		dashboard->highlightEquip(skill_name,true);
		foreach (QSanSkillButton*button,m_skillButtons){
			if(button->getSkill()->objectName()==skill_name){
				if(button->getStyle()==QSanSkillButton::S_STYLE_TOGGLE
					&&button->isEnabled()&&button->isDown()){
					ClientInstance->onPlayerInvokeSkill(true);
					return;
				}
			}
		}

		showPromptBox();
		ok_button->setEnabled(true);
		cancel_button->setEnabled(true);
		discard_button->setEnabled(false);
		break;
	}
	case Client::AskForPlayerChoose: {
		showPromptBox();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(ClientInstance->m_isDiscardActionRefusable);
		discard_button->setEnabled(false);

		choose_skill->setPlayerNames(ClientInstance->players_to_choose,ClientInstance->choose_max_num,ClientInstance->choose_min_num);
		dashboard->startPending(choose_skill);
		break;
	}
	case Client::AskForAG: {
		dashboard->disableAllCards();

		ok_button->setEnabled(false);
		cancel_button->setEnabled(ClientInstance->m_isDiscardActionRefusable);
		discard_button->setEnabled(false);

		showPromptBox();

		card_container->startChoose();
		break;
	}
	case Client::AskForYiji: {
		ok_button->setEnabled(false);
		cancel_button->setEnabled(ClientInstance->m_isDiscardActionRefusable);
		discard_button->setEnabled(false);

		QStringList yiji_info = Sanguosha->currentRoomState()->getCurrentCardUsePattern().split("=");
		yiji_skill->setPlayerNames(yiji_info.last().split("+"),yiji_info.first().toInt(),yiji_info.at(1));
		dashboard->startPending(yiji_skill);

		showPromptBox();
		break;
	}
	case Client::AskForGuanxing: {
		ok_button->setEnabled(true);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(false);
		break;
	}
	case Client::AskForGongxin: {
		ok_button->setEnabled(true);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(false);
		break;
	}
	case Client::AskForGeneralTaken:
	case Client::AskForArrangement: {
		ok_button->setEnabled(false);
		cancel_button->setEnabled(false);
		discard_button->setEnabled(false);
		break;
	}
	}
	if(newStatus!=oldStatus&&newStatus!=Client::Playing&&newStatus!=Client::NotActive)
		QApplication::alert(QApplication::focusWidget());

	if(ServerInfo.OperationTimeout<1)
		return;

	// do timeout
	if(newStatus!=Client::NotActive&&newStatus!=oldStatus){
		QApplication::alert(main_window);
		connect(dashboard,SIGNAL(progressBarTimedOut()),this,SLOT(doTimeout()));
		dashboard->showProgressBar(ClientInstance->getCountdown());
	}
}

void RoomScene::onSkillDeactivated()
{
	const ViewAsSkill*current = dashboard->currentSkill();
	if(current) cancel_button->click();
}

void RoomScene::onSkillActivated()
{
	QSanSkillButton*button = qobject_cast<QSanSkillButton*>(sender());
	const ViewAsSkill*skill = nullptr;
	if(button)
		skill = button->getViewAsSkill();
	else {
		QDialog*dialog = qobject_cast<QDialog*>(sender());
		if(dialog) skill = Sanguosha->getViewAsSkill(dialog->objectName());
	}

	if(skill){
		dashboard->startPending(skill);
		//ok_button->setEnabled(false);
		cancel_button->setEnabled(true);

		QString prompt = skill->objectName()+"-click";
		if(Sanguosha->translate(prompt)!=prompt){
			prompt_box->disappear();
			prompt_box_widget->setHtml(Sanguosha->translate(prompt));
			showPromptBox();
		}

		const Card*card = dashboard->pendingCard();
		if(card&&card->targetFixed()&&card->isAvailable(Self)){
			if(!skill->inherits("ZeroCardViewAsSkill")){
				foreach (const Card*c,Self->getKnownCards()){
					if(skill->viewFilter(QList<const Card*>(),c))
						return;
				}
				foreach (const Card*c,Self->getEquips()){
					if(skill->viewFilter(QList<const Card*>(),c))
						return;
				}
				foreach (const QString&name,dashboard->getPileExpanded()){
					foreach(int id,Self->getPile(name)){
						if(skill->viewFilter(QList<const Card*>(),Sanguosha->getCard(id)))
							return;
					}
				}
			}
			useSelectedCard();
		} else if(skill->inherits("OneCardViewAsSkill")&&Config.EnableIntellectualSelection&&!skill->getDialog())
			dashboard->selectOnlyCard(ClientInstance->getStatus()==Client::Playing);
	}
}

void RoomScene::updateTrustButton()
{
	if(!ClientInstance->getReplayer()){
		bool trusting = Self->getState()=="trust";
		trust_button->update();
		dashboard->setTrust(trusting);
	}
}

void RoomScene::doOkButton()
{
	if(!ok_button->isEnabled()) return;
	if(card_container->retained()) card_container->clear();
	useSelectedCard();
	if(onsole_target!=""){
		onsole_target = "";
		ClientPlayer*self = ClientInstance->getPlayer(Self->getTag("onsoleFrom").toString());
		dashboard->setPlayer(self);
		item2player.insert(dashboard,self);
		for (int i = 0;i < photos.length();i++){
			if(photos[i]->getPlayer()==self){
				photos[i]->setPlayer(Self);
				item2player.insert(photos[i],Self);
				Self = self;
				photos[i]->repaintAll(true);
			}
		}
		dashboard->repaintAll(true);
		foreach (QSanSkillButton*button,m_skillButtons){
			dashboard->removeSkillButton(button->objectName());
			m_skillButtons.removeAll(button);
			button->deleteLater();
		}
		updateSkillButtons();
		int n = -1;
		QList<Photo*>photos2;
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) n = 0;
			else if(n==0){
				for (int i = 0;i < photos.length();i++){
					if(photos[i]->getPlayer()==cp){
						photos2 << photos[i];
					}
				}
			}
		}
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) break;
			for (int i = 0;i < photos.length();i++){
				if(photos[i]->getPlayer()==cp){
					photos2 << photos[i];
				}
			}
		}
		photos = photos2;
		updateTable();
	}
}

void RoomScene::doCancelButton()
{
	if(card_container->retained()) card_container->clear();
	switch (ClientInstance->getStatus()&Client::ClientStatusBasicMask){
	case Client::Playing: {
		dashboard->skillButtonDeactivated();
		const ViewAsSkill*skill = dashboard->currentSkill();
		dashboard->unselectAll();
		if(skill) cancelViewAsSkill();
		else dashboard->stopPending();
		dashboard->enableCards();
		break;
	}
	case Client::Responding: {
		dashboard->skillButtonDeactivated();
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()) break;
		dashboard->unselectAll();
		if(!pattern.startsWith("@")){
			const ViewAsSkill*skill = dashboard->currentSkill();
			if(skill&&!skill->inherits("ResponseSkill")){
				cancelViewAsSkill();
				break;
			}
		}
		prompt_box->disappear();
		dashboard->stopPending();
		ClientInstance->onPlayerResponseCard(nullptr);
		break;
	}
	case Client::AskForShowOrPindian: {
		dashboard->unselectAll();
		prompt_box->disappear();
		dashboard->stopPending();
		ClientInstance->onPlayerResponseCard(nullptr);
		break;
	}
	case Client::Discarding:
	case Client::Exchanging: {
		prompt_box->disappear();
		dashboard->unselectAll();
		dashboard->stopPending();
		ClientInstance->onPlayerDiscardCards(nullptr);
		break;
	}
	case Client::ExecDialog: {
		m_choiceDialog->reject();
		break;
	}
	case Client::AskForSkillInvoke: {
		prompt_box->disappear();
		dashboard->highlightEquip(ClientInstance->getSkillNameToInvoke(),false);
		ClientInstance->onPlayerInvokeSkill(false);
		break;
	}
	case Client::AskForYiji: {
		prompt_box->disappear();
		dashboard->stopPending();
		ClientInstance->onPlayerReplyYiji(nullptr,nullptr);
		break;
	}
	case Client::AskForPlayerChoose: {
		prompt_box->disappear();
		dashboard->stopPending();
		ClientInstance->onPlayerChoosePlayer(QList<const Player*>());
		break;
	}
	case Client::AskForAG: {
		prompt_box->disappear();
		ClientInstance->onPlayerChooseAG(-1);
		break;
	}
	default:
		break;
	}
	if(onsole_target!=""){
		onsole_target = "";
		ClientPlayer*self = ClientInstance->getPlayer(Self->getTag("onsoleFrom").toString());
		dashboard->setPlayer(self);
		item2player.insert(dashboard,self);
		for (int i = 0;i < photos.length();i++){
			if(photos[i]->getPlayer()==self){
				photos[i]->setPlayer(Self);
				item2player.insert(photos[i],Self);
                Self = self;
				photos[i]->repaintAll(true);
			}
		}
		dashboard->repaintAll(true);
		foreach (QSanSkillButton*button,m_skillButtons){
			dashboard->removeSkillButton(button->objectName());
			m_skillButtons.removeAll(button);
			button->deleteLater();
		}
		updateSkillButtons();
		int n = -1;
		QList<Photo*>photos2;
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) n = 0;
			else if(n==0){
				for (int i = 0;i < photos.length();i++){
					if(photos[i]->getPlayer()==cp){
						photos2 << photos[i];
					}
				}
			}
		}
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) break;
			for (int i = 0;i < photos.length();i++){
				if(photos[i]->getPlayer()==cp){
					photos2 << photos[i];
				}
			}
		}
		photos = photos2;
		updateTable();
	}
}

void RoomScene::doDiscardButton()
{
	dashboard->stopPending();
	dashboard->unselectAll();
	if(card_container->retained()) card_container->clear();
	if(ClientInstance->getStatus()==Client::Playing)
		ClientInstance->onPlayerResponseCard(nullptr);
	if(onsole_target!=""){
		onsole_target = "";
		ClientPlayer*self = ClientInstance->getPlayer(Self->getTag("onsoleFrom").toString());
		dashboard->setPlayer(self);
		item2player.insert(dashboard,self);
		for (int i = 0;i < photos.length();i++){
			if(photos[i]->getPlayer()==self){
				photos[i]->setPlayer(Self);
				item2player.insert(photos[i],Self);
				Self = self;
				photos[i]->repaintAll(true);
			}
		}
		dashboard->repaintAll(true);
		foreach (QSanSkillButton*button,m_skillButtons){
			dashboard->removeSkillButton(button->objectName());
			m_skillButtons.removeAll(button);
			button->deleteLater();
		}
		updateSkillButtons();
		int n = -1;
		QList<Photo*>photos2;
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) n = 0;
			else if(n==0){
				for (int i = 0;i < photos.length();i++){
					if(photos[i]->getPlayer()==cp){
						photos2 << photos[i];
					}
				}
			}
		}
		foreach (const ClientPlayer*cp,ClientInstance->getPlayers()){
			if(cp==Self) break;
			for (int i = 0;i < photos.length();i++){
				if(photos[i]->getPlayer()==cp){
					photos2 << photos[i];
				}
			}
		}
		photos = photos2;
		updateTable();
	}
}

void RoomScene::hideAvatars()
{
	if(control_panel) control_panel->hide();
}

void RoomScene::startInXs()
{
	if(add_robot) add_robot->hide();
	if(start_game) start_game->hide();
	if(return_to_main_menu) return_to_main_menu->hide();
}

void RoomScene::changeHp(const QString&who,int delta,int nature,int losthj)
{
	// update
	Photo*photo = name2photo.value(who,nullptr);
	if(photo) photo->updateHp();
	else dashboard->update();
	ClientPlayer*player = ClientInstance->getPlayer(who);
	if(delta <= 0){
		if(losthj>0)
			Sanguosha->playSystemAudioEffect("hujia");
		if(nature<0){
			if(losthj<=0)
				Sanguosha->playSystemAudioEffect("hplost");
			log_box->appendLog("#LoseHp",who,QStringList(),"",QString::number(-delta));
		}else{
			QString damage_effect = "injure3";
			if(delta>=-1)
				damage_effect = "injure1";
			else if(delta==-2)
				damage_effect = "injure2";
			if(losthj<=0)
				Sanguosha->playSystemAudioEffect(damage_effect);
			if(nature==DamageStruct::Fire){
				doAnimation(S_ANIMATE_FIRE,QStringList() << who);
				damage_effect.append("_fire");
			}else if(nature==DamageStruct::Thunder){
				doAnimation(S_ANIMATE_LIGHTNING,QStringList() << who);
				damage_effect.append("_thunder");
			}else if(nature==DamageStruct::Ice){
				doAnimation(S_ANIMATE_ICE,QStringList() << who);
				damage_effect.append("_ice");
			}
			Sanguosha->playSystemAudioEffect(damage_effect);
			if(photo){
				setEmotion(who,"damage");
				photo->tremble();
			}
		}
	} else {
		if(nature!=0)
			Sanguosha->playSystemAudioEffect("recover");
		log_box->appendLog("#Recover",who,QStringList(),"",QString::number(delta));
	}
	log_box->appendLog("#GetHp",who,QStringList(),"",QString::number(player->getHp()+delta+losthj),QString::number(player->getMaxHp()));
}

void RoomScene::changeMaxHp(const QString&who,int delta)
{
	if(delta < 0)
		Sanguosha->playSystemAudioEffect("maxhplost");

	ClientPlayer*player = ClientInstance->getPlayer(who);

	log_box->appendLog("#GetHp",who,QStringList(),"",QString::number(player->getHp()),QString::number(player->getMaxHp()));
}

void RoomScene::onStandoff()
{
	log_box->append(QString(tr("<font color='%1'>---------- Game Finish ----------</font>").arg(Config.TextEditColor.name())));

	freeze();
	Sanguosha->playSystemAudioEffect("standoff");

	QDialog*dialog = new QDialog(main_window);
	dialog->resize(500,600);
	dialog->setWindowTitle(tr("Standoff"));

	QVBoxLayout*layout = new QVBoxLayout;

	QTableWidget*table = new QTableWidget;
	fillTable(table,ClientInstance->getPlayers());

	layout->addWidget(table);
	dialog->setLayout(layout);

	addRestartButton(dialog);

	dialog->exec();
}

void RoomScene::onGameOver()
{
	log_box->append(QString(tr("<font color='%1'>---------- Game Finish ----------</font>").arg(Config.TextEditColor.name())));

	freeze();

	bool victory = Self->property("win").toBool();
	QString win_effect = "audio/system/lose.ogg";
	if(victory){
		win_effect = "audio/system/win.ogg";
		foreach (const Player*player,ClientInstance->getPlayers()){
			if(!(player->isAlive()&&player->property("win").toBool())) continue;
			if(Sanguosha->playAudioEffect("audio/win/"+player->getGeneralName()+".ogg")){
#ifdef AUDIO_SUPPORT
				Audio::stop();
				showBubbleChatBox(player->objectName(),Sanguosha->translate("$"+player->getGeneralName()));
				break;
			}else if(player->getGeneral2()&&Sanguosha->playAudioEffect("audio/win/"+player->getGeneral2Name()+".ogg")){
				Audio::stop();
#endif
				showBubbleChatBox(player->objectName(),Sanguosha->translate("$"+player->getGeneral2Name()));
				break;
			}
		}
	}
	Sanguosha->playAudioEffect(win_effect);
	QDialog*dialog = new QDialog(main_window);
	dialog->resize(800,600);
	dialog->setWindowTitle(victory ? tr("Victory") : tr("Failure"));

	QGroupBox*winner_box = new QGroupBox(tr("Winner(s)"));
	QGroupBox*loser_box = new QGroupBox(tr("Loser(s)"));

	QTableWidget*winner_table = new QTableWidget;
	QTableWidget*loser_table = new QTableWidget;

	QVBoxLayout*winner_layout = new QVBoxLayout;
	winner_layout->addWidget(winner_table);
	winner_box->setLayout(winner_layout);

	QVBoxLayout*loser_layout = new QVBoxLayout;
	loser_layout->addWidget(loser_table);
	loser_box->setLayout(loser_layout);

	QVBoxLayout*layout = new QVBoxLayout;
	layout->addWidget(winner_box);
	layout->addWidget(loser_box);
	dialog->setLayout(layout);

	QList<const ClientPlayer*> winner_list,loser_list;
	foreach (const ClientPlayer*player,ClientInstance->getPlayers()){
		if(player->property("win").toBool())
			winner_list << player;
		else
			loser_list << player;
	}

	fillTable(winner_table,winner_list);
	fillTable(loser_table,loser_list);

	recorderAutoSave();

	addRestartButton(dialog);
	connect(dialog,SIGNAL(rejected()),this,SIGNAL(game_over_dialog_rejected()));
	dialog->exec();
}

void RoomScene::addRestartButton(QDialog*dialog)
{
	dialog->resize(main_window->width()/2,dialog->height());
	bool goto_next = false;
	if(ServerInfo.GameMode.contains("_mini_")&&Self->property("win").toBool())
		goto_next = (_m_currentStage < Sanguosha->getMiniSceneCounts());

	QPushButton*restart_button = new QPushButton(goto_next ? tr("Next Stage") : tr("Restart Game"));
	QPushButton*return_button = new QPushButton(tr("Return to main menu"));
	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(restart_button);

	QPushButton*save_button = new QPushButton(tr("Save record"));
	hlayout->addWidget(save_button);
	hlayout->addWidget(return_button);

	QVBoxLayout*layout = qobject_cast<QVBoxLayout*>(dialog->layout());
	if(layout) layout->addLayout(hlayout);

	connect(restart_button,SIGNAL(clicked()),dialog,SLOT(accept()));
	connect(return_button,SIGNAL(clicked()),dialog,SLOT(accept()));

	connect(save_button,SIGNAL(clicked()),this,SLOT(saveReplayRecord()));
	connect(restart_button,SIGNAL(clicked()),this,SIGNAL(restart()));
	connect(return_button,SIGNAL(clicked()),this,SIGNAL(return_to_start()));
}

void RoomScene::saveReplayRecord()
{
	QString filename = QFileDialog::getSaveFileName(main_window,tr("Save replay record"),
		QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
		tr("Pure text replay file (*.txt);;Image replay file (*.png)"));

	if(!filename.isEmpty()) ClientInstance->save(filename);
}

ScriptExecutor::ScriptExecutor(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Script execution"));
	QVBoxLayout*vlayout = new QVBoxLayout;
	vlayout->addWidget(new QLabel(tr("Please input the script that should be executed at server side:\n P = you,R = your room")));

	QTextEdit*box = new QTextEdit;
	box->setObjectName("scriptBox");
	vlayout->addWidget(box);

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();

	QPushButton*ok_button = new QPushButton(tr("OK"));
	hlayout->addWidget(ok_button);

	vlayout->addLayout(hlayout);

	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	connect(this,SIGNAL(accepted()),this,SLOT(doScript()));

	setLayout(vlayout);
}

void ScriptExecutor::doScript()
{
	QTextEdit*box = findChild<QTextEdit*>("scriptBox");
	if(box==nullptr) return;

	QString script = box->toPlainText();
	QByteArray data = script.toLatin1();
	data = qCompress(data);
	script = data.toBase64();

	ClientInstance->requestCheatRunScript(script);
}

DeathNoteDialog::DeathNoteDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Death note"));

	killer = new QComboBox;
	RoomScene::FillPlayerNames(killer,true);

	victim = new QComboBox;
	RoomScene::FillPlayerNames(victim,false);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));

	QFormLayout*layout = new QFormLayout;
	layout->addRow(tr("Killer"),killer);
	layout->addRow(tr("Victim"),victim);

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);
	layout->addRow(hlayout);

	setLayout(layout);
}

void DeathNoteDialog::accept()
{
	QDialog::accept();
	ClientInstance->requestCheatKill(killer->itemData(killer->currentIndex()).toString(),
		victim->itemData(victim->currentIndex()).toString());
}

DamageMakerDialog::DamageMakerDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Damage maker"));

	damage_source = new QComboBox;
	RoomScene::FillPlayerNames(damage_source,true);

	damage_target = new QComboBox;
	RoomScene::FillPlayerNames(damage_target,false);

	damage_nature = new QComboBox;
	damage_nature->addItem(tr("Normal"),S_CHEAT_NORMAL_DAMAGE);
	damage_nature->addItem(tr("Thunder"),S_CHEAT_THUNDER_DAMAGE);
	damage_nature->addItem(tr("Fire"),S_CHEAT_FIRE_DAMAGE);
	damage_nature->addItem(tr("Ice"),S_CHEAT_ICE_DAMAGE);
	damage_nature->addItem(tr("God"),S_CHEAT_GOD_DAMAGE);
	damage_nature->addItem(tr("Recover HP"),S_CHEAT_HP_RECOVER);
	damage_nature->addItem(tr("Lose HP"),S_CHEAT_HP_LOSE);
	damage_nature->addItem(tr("Lose Max HP"),S_CHEAT_MAX_HP_LOSE);
	damage_nature->addItem(tr("Reset Max HP"),S_CHEAT_MAX_HP_RESET);
	damage_nature->addItem(tr("Gain Hujia"),S_CHEAT_HUJIA_GET);
	damage_nature->addItem(tr("Lose Hujia"),S_CHEAT_HUJIA_LOSE);

	damage_point = new QSpinBox;
	damage_point->setRange(1,INT_MAX);
	damage_point->setValue(1);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);

	QFormLayout*layout = new QFormLayout;

	layout->addRow(tr("Damage source"),damage_source);
	layout->addRow(tr("Damage target"),damage_target);
	layout->addRow(tr("Damage nature"),damage_nature);
	layout->addRow(tr("Damage point"),damage_point);
	layout->addRow(hlayout);

	setLayout(layout);

	connect(damage_nature,SIGNAL(currentIndexChanged(int)),this,SLOT(disableSource()));
}

void DamageMakerDialog::disableSource()
{
	QString nature = damage_nature->itemData(damage_nature->currentIndex()).toString();
	damage_source->setEnabled(nature!="L");
}

StateEditorDialog::StateEditorDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle("状态编辑器");//(tr("State editor"));

	target = new QComboBox;
	RoomScene::FillPlayerNames(target,false);

	type = new QComboBox;
	type->addItem(QString("改变手牌上限"),S_CHEAT_CHANGE_MAXCARDS);//tr("Change maxcards")
	type->addItem(QString("改变其余角色到目标的距离"),S_CHEAT_CHANGE_DISTANCE);//tr("Change distance")
	type->addItem(QString("改变目标到其余角色的距离"),S_CHEAT_CHANGE_DISTANCE_TO_OTHERS);//tr("Change distance to others")
	type->addItem(QString("改变攻击范围"),S_CHEAT_CHANGE_ATTACKRANGE);//tr("Change attack range")
	type->addItem(QString("改变【杀】上限"),S_CHEAT_CHANGE_SLASHCISHU);//tr("Change slash cishu")
	type->addItem(QString("改变【杀】范围"),S_CHEAT_CHANGE_SLASHJULI);//tr("Change slash juli")
	type->addItem(QString("改变【杀】目标数"),S_CHEAT_CHANGE_SLASHMUBIAO);//tr("Change slash mubiao")
	type->addItem(QString("摸牌"),S_CHEAT_DrawCards);//tr("Draw Cards")
	type->addItem(QString("弃置所有牌"),S_CHEAT_ThrowAllHandCardsAndEquips);//tr("Throw All HandCardsAndEquips")
	type->addItem(QString("弃置所有手牌"),S_CHEAT_ThrowAllHandCards);//tr("Throw All HandCards")
	type->addItem(QString("弃置所有装备区牌"),S_CHEAT_ThrowAllEquips);//tr("Throw All Equips")
	type->addItem(QString("弃置区域内所有牌"),S_CHEAT_ThrowAllCards);//tr("Throw All Cards")
	type->addItem(QString("弃置牌"),S_CHEAT_ThrowCards);//tr("Throw Cards")
	type->addItem(QString("弃置手牌"),S_CHEAT_ThrowCardsWithoutEquips);//tr("Throw Cards Without Equips")
	type->addItem(QString("横置或重置"),S_CHEAT_SetChained);//tr("Set Chained")
	type->addItem(QString("翻面"),S_CHEAT_TurnOver);//tr("Turn Over")
	type->addItem(QString("视为使用【酒】（不计次）"),S_CHEAT_UseAnaleptic);//tr("Use Analeptic")

	point = new QSpinBox;
	point->setRange(INT_MIN,INT_MAX);
	point->setValue(1);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);

	QFormLayout*layout = new QFormLayout;

	layout->addRow(QString("目标"),target);//tr("Editor target")
	layout->addRow(QString("类型"),type);//tr("Editor type")
	layout->addRow(QString("数量"),point);//tr("Editor point")
	layout->addRow(hlayout);

	setLayout(layout);
}

void RoomScene::FillPlayerNames(QComboBox*ComboBox,bool add_none)
{
	if(add_none) ComboBox->addItem(tr("None"),".");
	ComboBox->setIconSize(G_COMMON_LAYOUT.m_tinyAvatarSize);
	foreach (const ClientPlayer*player,ClientInstance->getPlayers()){
		if(!player->getGeneral()) continue;
		QString general_name = Sanguosha->translate(player->getGeneralName());
		QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(player->getGeneralName(),QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
		ComboBox->addItem(QIcon(pixmap),
			QString("%1 [%2]").arg(general_name).arg(player->screenName()),
			player->objectName());
	}
}

void DamageMakerDialog::accept()
{
	QDialog::accept();

	ClientInstance->requestCheatDamage(damage_source->itemData(damage_source->currentIndex()).toString(),
		damage_target->itemData(damage_target->currentIndex()).toString(),
		(DamageStruct::Nature)damage_nature->itemData(damage_nature->currentIndex()).toInt(),
		damage_point->value());
}

void StateEditorDialog::accept()
{
	QDialog::accept();

	ClientInstance->requestCheatchangestate(target->itemData(target->currentIndex()).toString(),
		type->itemData(type->currentIndex()).toInt(),
		point->value());
}

void RoomScene::makeDamage()
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(main_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	DamageMakerDialog*damage_maker = new DamageMakerDialog(main_window);
	damage_maker->exec();
}

void RoomScene::changeState()
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(main_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	StateEditorDialog*state_editor = new StateEditorDialog(main_window);
	state_editor->exec();
}

void RoomScene::makeKilling()
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(main_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	DeathNoteDialog*dialog = new DeathNoteDialog(main_window);
	dialog->exec();
}

void RoomScene::makeReviving()
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(main_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	QStringList items;
	QList<const ClientPlayer*> victims;
	foreach (const ClientPlayer*player,ClientInstance->getPlayers()){
		if(player->isDead()){
			QString general_name = Sanguosha->translate(player->getGeneralName());
			items << QString("%1 [%2]").arg(player->screenName()).arg(general_name);
			victims << player;
		}
	}

	if(items.isEmpty()){
		QMessageBox::warning(main_window,tr("Warning"),tr("No victims now!"));
		return;
	}

	bool ok;
	QString item = QInputDialog::getItem(main_window,tr("Reviving wand"),
		tr("Please select a player to revive"),items,0,false,&ok);
	if(ok){
		int index = items.indexOf(item);
		ClientInstance->requestCheatRevive(victims.at(index)->objectName());
	}
}

void RoomScene::doScript()
{
	ScriptExecutor*dialog = new ScriptExecutor(main_window);
	dialog->exec();
}

void RoomScene::viewGenerals(const QString&reason,const QStringList&names)
{
	QDialog*dialog = new ChooseGeneralDialog(names,main_window,true,Sanguosha->translate(reason));
	connect(dialog,SIGNAL(rejected()),dialog,SLOT(deleteLater()));
	dialog->setParent(main_window,Qt::Dialog);
	dialog->show();
}

void RoomScene::fillTable(QTableWidget*table,const QList<const ClientPlayer*>&players)
{
	table->setColumnCount(10);
	table->setRowCount(players.length());
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);

	RecAnalysis record(ClientInstance->getReplayPath());
	QMap<QString,PlayerRecordStruct*> record_map = record.getRecordMap();

	static QStringList labels;
	if(labels.isEmpty()){
		labels << tr("General") << tr("Name");// << tr("Alive");
		if(ServerInfo.EnableHegemony)
			labels << tr("Nationality");
		else
			labels << tr("Role");

		labels << tr("TurnCount");
		labels << tr("Recover") << tr("Damage") << tr("Damaged") << tr("Kill") << tr("Designation");
		labels << tr("Handcards");

	}
	table->setHorizontalHeaderLabels(labels);
	table->setSelectionBehavior(QTableWidget::SelectRows);

	for (int i = 0;i < players.length();i++){
		const ClientPlayer*player = players[i];

		QTableWidgetItem*item = new QTableWidgetItem;
		item->setText(player->getLogName());
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,0,item);

		item = new QTableWidgetItem;
		item->setText(player->screenName());
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,1,item);/*

		item = new QTableWidgetItem;
		if(player->isAlive())
			item->setText(tr("Alive"));
		else
			item->setText(tr("Dead"));
		table->setItem(i,2,item);*/

		item = new QTableWidgetItem;

		if(ServerInfo.EnableHegemony){
			QIcon icon(QString("image/kingdom/icon/%1.png").arg(player->getKingdom()));
			item->setIcon(icon);
			item->setText(Sanguosha->translate(player->getKingdom()));
		} else {
			QIcon icon(QString("image/system/roles/%1.png").arg(player->getRole()));
			item->setIcon(icon);
			QString role = player->getRole();
			if(ServerInfo.GameMode.startsWith("06_")){
				if(role=="lord"||role=="renegade")
					role = "leader";
				else
					role = "guard";
			} else if(ServerInfo.GameMode=="04_1v3"){
				int seat = player->getSeat();
				switch (seat){
				case 1: role = "lvbu";break;
				case 2: role = "vanguard";break;
				case 3: role = "mainstay";break;
				case 4: role = "general";break;
				}
			} else if(ServerInfo.GameMode=="02_1v1"){
				if(role=="lord")
					role = "defensive";
				else
					role = "offensive";
			}
			item->setText(Sanguosha->translate(role));
		}
		if(!player->isAlive())
			item->setFlags(item->flags()&~Qt::ItemIsEnabled);
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,2,item);

		item = new QTableWidgetItem;
		item->setText(QString::number(player->getMark("Global_TurnCount")));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,3,item);

		PlayerRecordStruct*rec = record_map.value(player->objectName());
		if(!rec) return;
		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_recover));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,4,item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_damage));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,5,item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_damaged));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,6,item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_kill));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,7,item);

		item = new QTableWidgetItem;
		item->setText(rec->m_designation.join(","));
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,8,item);

		item = new QTableWidgetItem;
		QStringList handcards;
		foreach(const Card*h,player->getHandcards())
			handcards << h->getLogName();
		QString handcard = handcards.join(",");
		handcard.replace("<img src='image/system/log/spade.png' height=12/>",tr("Spade"));
		handcard.replace("<img src='image/system/log/heart.png' height=12/>",tr("Heart"));
		handcard.replace("<img src='image/system/log/club.png' height=12/>",tr("Club"));
		handcard.replace("<img src='image/system/log/diamond.png' height=12/>",tr("Diamond"));
		item->setText(handcard);
		item->setTextAlignment(Qt::AlignCenter);
		table->setItem(i,9,item);
	}

	for (int i = 2;i < 10;i++)
		table->resizeColumnToContents(i);
}

void RoomScene::updateCardDescription(const QString &card_name)
{
    if (dashboard) {
        dashboard->_updateEquips();
        dashboard->refreshHandCardTooltips();
    }

    foreach (Photo *photo, photos) {
        if (photo) {
            photo->_updateEquips();
        }
    }
}

void RoomScene::updateAreas(const QString&who)
{
	ClientPlayer*player = ClientInstance->getPlayer(who);
	if(player){
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->_updateEquips();
		container->updateDelayedTricks();
	}
}

void RoomScene::updateHandcards(const QString&who)
{
	ClientPlayer*player = ClientInstance->getPlayer(who);
	if(player){
		PlayerCardContainer*container = (PlayerCardContainer*)_getGenericCardContainer(Player::PlaceHand,player);
		container->updateHandcardNum();
	}
}

void RoomScene::killPlayer(const QString&who)
{
	const General*general = nullptr;
	if(who==Self->objectName()){
		dashboard->stopHuaShen();
		dashboard->killPlayer();
		dashboard->update();
		general = Self->getGeneral();
		item2player.remove(dashboard);
		if(ServerInfo.GameMode=="02_1v1")
			self_box->killPlayer(general->objectName());/*
		foreach (const Skill*skill,Self->getVisibleSkills())
			detachSkill(skill->objectName());*/
	} else {
		Photo*photo = name2photo[who];
		photo->stopHuaShen();
		photo->killPlayer();
		photo->setFrame(Photo::S_FRAME_NO_FRAME);
		photo->update();
		item2player.remove(photo);
		general = photo->getPlayer()->getGeneral();
		if(ServerInfo.GameMode=="02_1v1")
			enemy_box->killPlayer(general->objectName());
	}
	if(Config.EnableEffects&&Config.EnableLastWord&&!Self->hasFlag("marshalling"))
		general->lastWord();

	// ── Spine pop-out: mark player dead → fade out any running action ──
	if (_spineActionController)
		_spineActionController->setPlayerAlive(who, false);
}

void RoomScene::revivePlayer(const QString&who)
{
	if(who==Self->objectName()){
		dashboard->revivePlayer();
		item2player.insert(dashboard,Self);
		updateSkillButtons();
		//dashboard->updateAvatarTooltip();
	} else {
		Photo*photo = name2photo[who];
		photo->revivePlayer();
		item2player.insert(photo,photo->getPlayer());
		//photo->updateAvatarTooltip();
	}

	// ── Spine pop-out: revive → allow actions again + trigger entrance ──
	if (_spineActionController) {
		qWarning("[RoomScene] revivePlayer '%s': marking alive + triggering entrance", qPrintable(who));
		_spineActionController->setPlayerAlive(who, true);
		// Trigger entrance animation as a visual revival effect
		bool isLocal = (who == Self->objectName());
		_spineActionController->triggerAction(who, ActionType::Entrance,
		                                      QList<QPointF>(), isLocal);
	}
}

void RoomScene::takeAmazingGrace(ClientPlayer*taker,int card_id,bool move_cards)
{
	m_tablePile->clear();

	card_container->m_currentPlayer = taker;
	QList<CardItem*>copys = card_container->removeCardItems(QList<int>()<<card_id,Player::PlaceHand);
	if(copys.isEmpty()) return;

	if(taker){
		GenericCardContainer*container = _getGenericCardContainer(Player::PlaceHand,taker);
		bringToFront(container);
		if(move_cards){
			log_box->appendLog("$TakeAG",taker->objectName(),QStringList(),QString::number(card_id));
			CardsMoveStruct move;
			move.card_ids.append(card_id);
			move.from_place = Player::PlaceWuGu;
			move.to_place = Player::PlaceHand;
			move.to = taker;
			container->addCardItems(copys,move);
			return;
		}
	}
	delete copys.last();
}

void RoomScene::showCard(const QString&player_name,QList<int> card_ids)
{
	Player*player = ClientInstance->getPlayer(player_name);

	GenericCardContainer*container = _getGenericCardContainer(Player::PlaceHand,player);
	QList<CardItem*> card_items = container->cloneCardItems(card_ids);
	bringToFront(m_tablePile);
	CardsMoveStruct move;
	move.from_place = Player::PlaceHand;
	move.to_place = Player::PlaceTable;
	move.reason = CardMoveReason(CardMoveReason::S_REASON_DEMONSTRATE,player_name);
	foreach (CardItem*card_item,card_items)
		card_item->setFootnote(_translateMovement(move));
	m_tablePile->addCardItems(card_items,move);

	log_box->appendLog("$ShowCard",player_name,QStringList(),ListI2S(card_ids).join("+"));
}

void RoomScene::chooseSkillButton()
{
	QList<QSanSkillButton*> enabled_buttons;
	foreach (QSanSkillButton*btn,m_skillButtons){
		if(btn->isEnabled()) enabled_buttons << btn;
	}

	if(enabled_buttons.isEmpty()) return;

	QDialog*dialog = new QDialog(main_window);
	dialog->setWindowTitle(tr("Select skill"));

	QVBoxLayout*layout = new QVBoxLayout;

	foreach (QSanSkillButton*btn,enabled_buttons){
		QCommandLinkButton*button = new QCommandLinkButton(Sanguosha->translate(btn->getSkill()->objectName()));
		connect(button,SIGNAL(clicked()),btn,SLOT(click()));
		connect(button,SIGNAL(clicked()),dialog,SLOT(accept()));
		layout->addWidget(button);
	}

	dialog->setLayout(layout);
	dialog->exec();
}

void RoomScene::attachSkill(const QString&skill_name)
{
	const Skill*skill = Sanguosha->getSkill(skill_name);
	//if(skill&&!Self->hasSkill(skill_name,true))  如果不添加的话，再变身一次就没这些图标了
	if(skill){
		if(skill->isLordSkill()&&!Self->isLord()) return;
		addSkillButton(skill);
	}
}

void RoomScene::detachSkill(const QString&skill_name)
{
	// for all the skills has a ViewAsSkill Effect { Client::setMark(const Json::Value&) }
	// this is a DIRTY HACK!!! for we should prevent the ViewAsSkill button been removed temporily by duanchang
	if(Self->getMark("ViewAsSkill_"+skill_name+"Effect")>0) return;
	QSanSkillButton*btn = dashboard->removeSkillButton(skill_name);
	if(!btn) return;//be care LordSkill and SPConvertSkill
	m_skillButtons.removeAll(btn);
	btn->deleteLater();
}

void RoomScene::updateSkill(const QString&skill_name)
{
	foreach(QSanSkillButton*button,m_skillButtons){
		if(button->getSkill()->objectName()==skill_name)
		{
			LuaLocker locker;
			const Skill *s = button->getSkill();
			button->setToolTip(buildOracleTooltip(s->getOracleText(Self), s->getDescription(Self)));
		}
	}/*
	bool effectMark = false;
	QString effectMarkName = "ViewAsSkill_"+skill_name+"Effect";// Be care!! Before using RoomScene::detachSkill to remove the skill button,we should guarentee this mark doesn't exist
	if(Self&&Self->getMark(effectMarkName) > 0){
		Self->setMark(effectMarkName,0);
		effectMark = true;
	}
	detachSkill(skill_name);
	attachSkill(skill_name);

	if(effectMark)
		Self->setMark(effectMarkName,1);*/
}

void RoomScene::viewDistance()
{
	DistanceViewDialog*dialog = new DistanceViewDialog(main_window);
	dialog->show();
}

void RoomScene::viewMaxCards()
{
	MaxCardsViewDialog*dialog = new MaxCardsViewDialog(main_window);
	dialog->show();
}

void RoomScene::speak()
{
	if(game_started&&ServerInfo.DisableChat)
		chat_box->append(tr("This room does not allow chatting!"));
	else {
		bool broadcast = true;
		QString text = chat_edit->text();
		if(text==".StartBgMusic"){
			broadcast = false;
			//Config.EnableBgMusic = true;
			//Config.setValue("EnableBgMusic",true);
			_m_bgEnabled = true;
			_m_bgMusicPath = Config.value("BackgroundMusic","audio/system/background.ogg").toString();
#ifdef AUDIO_SUPPORT
			Audio::stopBGM();
			if(Config.BGMVolume>0){
				Audio::playBGM(_m_bgMusicPath);
				Audio::setBGMVolume(Config.BGMVolume);
			}
		} else if(text.startsWith(".StartBgMusic=")){
			broadcast = false;
			//Config.EnableBgMusic = true;
			//Config.setValue("EnableBgMusic",true);
			_m_bgEnabled = true;
			QString path = text.mid(14);
			if(path.startsWith("|")){
				path = path.mid(1);
				Config.setValue("BackgroundMusic",path);
				_m_bgMusicPath = path;
			}
			Audio::stopBGM();
			if(Config.BGMVolume>0){
				Audio::playBGM(path);
				Audio::setBGMVolume(Config.BGMVolume);
			}
		} else if(text==".StopBgMusic"){
			Audio::stopBGM();
#endif
			broadcast = false;
			//Config.EnableBgMusic = false;
			//Config.setValue("EnableBgMusic",false);
			_m_bgEnabled = false;
		}
		if(broadcast)
			ClientInstance->speakToServer(text);
		else {
			QString title;
			if(Self){
				title = Self->getGeneralName();
				title = Sanguosha->translate(title);
				title.append(QString("(%1)").arg(Self->screenName()));
				title = QString("<b>%1</b>").arg(title);
			}
			QString line = tr("<font color='%1'>[%2] said: %3 </font>")
				.arg(Config.TextEditColor.name()).arg(title).arg(text);
			chat_box->append(QString("<p style=\"margin:3px 2px;\">%1</p>").arg(line));
		}
	}
	chat_edit->clear();
}

void RoomScene::onEmotionIconSelected(int emotionId)
{
	QString emotionText = QString("<#%1#>").arg(emotionId);
	ClientInstance->speakToServer(emotionText);
}

void RoomScene::fillCards(const QList<int>&card_ids,const QList<int>&disabled_ids)
{
	bringToFront(card_container);
	card_container->fillCards(card_ids,disabled_ids);
	card_container->show();
}

void RoomScene::doGongxin(const QList<int>&card_ids,bool enable_heart,QList<int> enabled_ids)
{
	fillCards(card_ids);
	if(enable_heart)
		card_container->startGongxin(enabled_ids);
	else{
		card_container->addCloseButton();
		//QTimer::singleShot(6666,this,SLOT(hideContainer()));
	}
}

void RoomScene::hideContainer()
{
    card_container->clear();
}

void RoomScene::showOwnerButtons(bool owner)
{
	if(add_robot&&start_game&&!game_started&&ServerInfo.EnableAI){
		add_robot->setVisible(owner);
		start_game->setVisible(owner);
	}
}

void RoomScene::showPlayerCards()
{
	QAction*action = qobject_cast<QAction*>(sender());
	if(action){
		QStringList names = action->data().toString().split(".");
		const ClientPlayer*player = ClientInstance->getPlayer(names.first());
		if(names.length() > 1){
			QList<const Card*> cards;
			foreach (int id,player->getPile(names.last())){
				const Card*card = Sanguosha->getEngineCard(id);
				if(card) cards << card;
			}

			CardOverview*overview = new CardOverview;
			overview->setWindowTitle(QString("%1 %2").arg(player->getLogName()).arg(Sanguosha->translate(names.last())));
			overview->loadFromList(cards);
			overview->show();
		} else {
			QList<const Card*> known = player->getKnownCards();

			if(Self->canSeeHandcard(player))
				known = player->getHandcards();
				//QStringList handcard = player->property("My_Visible_HandCards").toString().split("+");
			CardOverview*overview = new CardOverview;
			overview->setWindowTitle(QString("%1 %2").arg(player->getLogName()).arg(tr("Known cards")));
			overview->loadFromList(known);
			overview->show();
		}
	}
}

KOFOrderBox::KOFOrderBox(bool self,QGraphicsScene*scene)
{
	QString basename = self ? "self" : "enemy";
	QString path = QString("image/system/1v1/%1.png").arg(basename);
	setPixmap(QPixmap(path));
	scene->addItem(this);

	for (int i = 0;i < 3;i++){
		avatars[i] = new QSanSelectableItem;
		avatars[i]->load("image/system/1v1/unknown.png",QSize(122,50));
		avatars[i]->setParentItem(this);
		avatars[i]->setPos(5,23+62*i);
		avatars[i]->setObjectName("unknown");
	}

	revealed = 0;
}

void KOFOrderBox::revealGeneral(const QString&name)
{
	if(revealed < 3){
		avatars[revealed]->setPixmap(G_ROOM_SKIN.getGeneralPixmap(name,QSanRoomSkin::S_GENERAL_ICON_SIZE_KOF));
		avatars[revealed]->setObjectName(name);
		const General*general = Sanguosha->getGeneral(name);
		if(general)
			avatars[revealed]->setToolTip(buildOracleTooltip(general->getOracleText(), general->getSkillDescription(true)));
		revealed++;
	}
}

void KOFOrderBox::killPlayer(const QString&general_name)
{
	for (int i = 0;i < revealed;i++){
		if(avatars[i]->isEnabled()&&avatars[i]->objectName()==general_name){
			QPixmap pixmap("image/system/death/unknown.png");
			QGraphicsPixmapItem*death = new QGraphicsPixmapItem(pixmap,avatars[i]);
			death->setScale(0.5);
			death->moveBy(15,0);
			avatars[i]->makeGray();
			avatars[i]->setEnabled(false);
			return;
		}
	}
}

void RoomScene::onGameStart()
{
	main_window->activateWindow();
	if(ServerInfo.GameMode.contains("_mini_")){
		QString id = Config.GameMode.mode_id;
		id.replace("_mini_","");
		_m_currentStage = id.toInt();
	} else if(ServerInfo.GameMode=="06_3v3"||ServerInfo.GameMode=="06_XMode"||ServerInfo.GameMode=="02_1v1"){
		log_box->show();

		if(self_box&&enemy_box){
			self_box->show();
			enemy_box->show();
		}
	}

	if(!ClientInstance->isReplayState())
		m_timerLabel->start();

	if(control_panel)
		control_panel->hide();

	if(Self&&!Self->hasFlag("marshalling"))
		log_box->append(QString(tr("<font color='%1'>---------- Game Start ----------</font>").arg(Config.TextEditColor.name())));

	trust_button->setEnabled(true);

	game_started = true;

	// ── Spine pop-out: register skins + preload once game starts ──
	// Only register and preload for players that actually have dynamic skins
	// in the JSON config. Players without skins pay zero cost.
	registerDynamicSkinsForAllPlayers();
	updateSpineSeatGeometry();
	// Preload only players that got at least one skin registered
	if (_spineActionController) {
		foreach (Photo *photo, photos) {
			const ClientPlayer *p = photo->getPlayer();
			if (!p) continue;
			const General *g = p->getGeneral();
			if (g && _spineActionController->hasDynamicSkin(g->objectName()))
				_spineActionController->preloadPlayer(p->objectName());
		}
		if (Self) {
			const General *g = Self->getGeneral();
			if (g && _spineActionController->hasDynamicSkin(g->objectName()))
				_spineActionController->preloadPlayer(Self->objectName());
		}

		// Trigger entrance animations only for players with an entrance action
		foreach (Photo *photo, photos) {
			const ClientPlayer *p = photo->getPlayer();
			if (!p) continue;
			const General *g = p->getGeneral();
			if (g && _spineActionController->hasDynamicSkin(g->objectName()))
				_spineActionController->triggerAction(
					p->objectName(), ActionType::Entrance);
		}
		if (Self) {
			const General *g = Self->getGeneral();
			if (g && _spineActionController->hasDynamicSkin(g->objectName()))
				_spineActionController->triggerAction(
					Self->objectName(), ActionType::Entrance, QList<QPointF>(), true);
		}
	}

	// for tablebg change
	if(Config.EnableAutoBackgroundChange&&Self){
		QString kingdom = Self->getKingdom();
		if(isNormalGameMode(ServerInfo.GameMode)){
			foreach (const Player*p,Self->getSiblings()){
				if(p->isLord()){
					kingdom = p->getKingdom();
					break;
				}
			}
		}
		if(Sanguosha->getKingdoms().contains(kingdom)){
			QPixmap pixmap = G_ROOM_SKIN.getPixmap("tableBg"+kingdom);
			if(pixmap.width()==1||pixmap.height()==1){
				// we treat this condition as error and do not use it
			} else {
				m_tableBgPixmapOrig = pixmap;
				m_tableBgPixmap = pixmap.scaled(m_tablew,m_tableh+_m_roomLayout->m_photoDashboardPadding,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
				m_tableBg->setPixmap(m_tableBgPixmap);
			}
		}
	}
	// end
	if(Config.BGMVolume>0){
		// start playing background music
		_m_bgMusicPath = Config.value("BackgroundMusic","audio/system/background.ogg").toString();
#ifdef AUDIO_SUPPORT
		Audio::stopBGM();
		Audio::playBGM(_m_bgMusicPath);
		Audio::setBGMVolume(Config.BGMVolume);
#endif
		_m_bgEnabled = true;
	} else
		_m_bgEnabled = false;
}

void RoomScene::freeze()
{
	dashboard->setEnabled(false);
	dashboard->stopHuaShen();
	foreach (Photo*photo,photos){
		photo->hideProgressBar();
		photo->stopHuaShen();
		photo->setEnabled(false);
	}
	m_timerLabel->stop();
	item2player.clear();
	chat_edit->setEnabled(false);
#ifdef AUDIO_SUPPORT
	Audio::stopBGM();
#endif
	dashboard->hideProgressBar();
	main_window->setStatusBar(nullptr);

	// ── Spine pop-out: cancel all running actions on game freeze ──
	if (_spineActionController) {
		if (Self) _spineActionController->cancelAction(Self->objectName());
		foreach (Photo *photo, photos) {
			const ClientPlayer *p = photo->getPlayer();
			if (p) _spineActionController->cancelAction(p->objectName());
		}
	}
}

void RoomScene::_cancelAllFocus()
{
	foreach (Photo*photo,photos){
		photo->hideProgressBar();
		if(photo->getPlayer()->getPhase()==Player::NotActive)
			photo->setFrame(Photo::S_FRAME_NO_FRAME);
	}
}

void RoomScene::moveFocus(const QStringList&players,Countdown countdown,int command)
{
	_cancelAllFocus();
	foreach (QString player,players){
		Photo*photo = name2photo[player];
		if(photo){
			if(ServerInfo.OperationTimeout>0&&command!=S_COMMAND_NULLIFICATION)
				photo->showProgressBar(countdown);
			else if(photo->getPlayer()->getPhase()==Player::NotActive)
				photo->setFrame(Photo::S_FRAME_RESPONDING);
		}else
			Q_ASSERT(player==Self->objectName());
	}
}

void RoomScene::setEmotion(const QString&who,const QString&emotion)
{
	if(emotion=="chain")
		Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(emotion,QString("common")),true);
	else if(emotion.startsWith("weapon/")||emotion.startsWith("armor/")){
		if(Config.value("NoEquipAnim").toBool()) return;
		Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(emotion.split("/").last(),QString("equip")));
	}

	Photo*photo = name2photo[who];
	if(photo){
		photo->setEmotion(emotion,emotion.contains("question"));
	}else {
		PixmapAnimation*pma = PixmapAnimation::GetPixmapAnimation(dashboard,emotion);
		if(pma){
			pma->moveBy(0,-dashboard->boundingRect().height()/1.5);
			pma->setZValue(20);
		}
	}
}

void RoomScene::changeTableBg(const QString&tableBg)
{
	QPixmap pixmap = G_ROOM_SKIN.getPixmap(tableBg);
	if(pixmap.width()==1||pixmap.height()==1){
		// we treat this condition as error and do not use it
	} else {
		m_tableBgPixmapOrig = pixmap;
		m_tableBgPixmap = pixmap.scaled(m_tablew,m_tableh+_m_roomLayout->m_photoDashboardPadding,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
		m_tableBg->setPixmap(m_tableBgPixmap);
	}
}

void RoomScene::showSkillInvocation(const QString&who,const QString&skill_name)
{
	//ClientPlayer*player = ClientInstance->getPlayer(who);
	//if(player&&(player->hasSkill(skill_name,true)||player->hasEquipSkill(skill_name))){
		//const Skill*skill = Sanguosha->getSkill(skill_name);
		//if(skill&&skill->inherits("SPConvertSkill")) return;
		log_box->appendLog("#InvokeSkill",who,QStringList(),"",skill_name);
	//}

	// ── Spine pop-out: trigger Special action on non-attack skill invocation ──
	if (_spineActionController && game_started) {
		bool isLocal = (who == Self->objectName());
		_spineActionController->triggerAction(who, ActionType::Special,
		                                      QList<QPointF>(), isLocal);
	}
}

void RoomScene::removeLightBox()
{
	PixmapAnimation*pma = qobject_cast<PixmapAnimation*>(sender());
	if(pma){
		removeItem(pma->parentItem());
	} else {
		QPropertyAnimation*animation = qobject_cast<QPropertyAnimation*>(sender());
		QGraphicsTextItem*line = qobject_cast<QGraphicsTextItem*>(animation->targetObject());
		if(line){
			removeItem(line->parentItem());
		} else {
			QSanSelectableItem*line = qobject_cast<QSanSelectableItem*>(animation->targetObject());
			removeItem(line->parentItem());
		}
	}
}

QGraphicsObject*RoomScene::getAnimationObject(const QString&name) const
{
	if(name=="tablePile")
		return m_tablePile;
	if(name==Self->objectName())
		return dashboard;
	return name2photo.value(name);
}

void RoomScene::doMovingAnimation(const QString&name,const QStringList&args)
{
	QSanSelectableItem*item = new QSanSelectableItem(QString("image/system/animation/%1.png").arg(name));
	item->setZValue(16);
	addItem(item);

	QGraphicsObject*fromItem = getAnimationObject(args.at(0));
	QGraphicsObject*toItem = getAnimationObject(args.at(1));

	QPointF from = fromItem->scenePos();
	QPointF to = toItem->scenePos();
	if(fromItem==dashboard)
		from.setX(fromItem->boundingRect().width()/2);
	if(toItem==dashboard)
		to.setX(toItem->boundingRect().width()/2);

	QSequentialAnimationGroup*group = new QSequentialAnimationGroup;

	QPropertyAnimation*move = new QPropertyAnimation(item,"pos");
	move->setStartValue(from);
	move->setEndValue(to);
	move->setDuration(1000);

	QPropertyAnimation*disappear = new QPropertyAnimation(item,"opacity");
	disappear->setEndValue(0.0);
	disappear->setDuration(1000);

	group->addAnimation(move);
	group->addAnimation(disappear);

	group->start(QAbstractAnimation::DeleteWhenStopped);
	connect(group,SIGNAL(finished()),item,SLOT(deleteLater()));
}

void RoomScene::doAppearingAnimation(const QString&name,const QStringList&args)
{
	QSanSelectableItem*item = new QSanSelectableItem(QString("image/system/animation/%1.png").arg(name));
	item->setZValue(16);
	addItem(item);

	QGraphicsObject*fromItem = getAnimationObject(args.at(0));
	QPointF from = fromItem->scenePos();
	if(fromItem==dashboard)
		from.setX(fromItem->boundingRect().width()/2);
	item->setPos(from);

	QPropertyAnimation*disappear = new QPropertyAnimation(item,"opacity");
	disappear->setEndValue(0.0);
	disappear->setDuration(1000);

	disappear->start(QAbstractAnimation::DeleteWhenStopped);
	connect(disappear,SIGNAL(finished()),item,SLOT(deleteLater()));
}

void RoomScene::doLightboxAnimation(const QString&,const QStringList&args)
{
	QString word = args.first();
	word = Sanguosha->translate(word);
	QStringList disp_arg = args.value(1,"2000:0").split(":");
	int duration = disp_arg.first().toInt();
	int pixelSize = disp_arg.last().toInt();

	// 支援動態參數替換：duration:pixelSize:arg1:arg2:...
	if (disp_arg.size() > 2) {
		for (int i = 2; i < disp_arg.size(); ++i) {
			QString arg = disp_arg.at(i);
			QString translatedArg = Sanguosha->translate(arg);
			word = word.arg(translatedArg);
		}
	}

	QRect rect = main_window->rect();
	QGraphicsRectItem*lightbox = addRect(rect);

    if(!word.startsWith("skill=")&&!word.startsWith("ghost=")&&!word.startsWith("background=")&&!word.startsWith("spine=")){
		lightbox->setBrush(QColor(32,32,32,204));
		lightbox->setZValue(21);
		word = Sanguosha->translate(word);
	}

	if(word.startsWith("image=")){
		QSanSelectableItem*line = new QSanSelectableItem(word.mid(6));
		addItem(line);

		QRectF line_rect = line->boundingRect();
		line->setParentItem(lightbox);
		line->setPos(m_tableCenterPos-line_rect.center());

		QPropertyAnimation*appear = new QPropertyAnimation(line,"opacity");
		appear->setStartValue(0.0);
		appear->setKeyValueAt(0.7,1.0);
		appear->setEndValue(0.0);

		appear->setDuration(duration);
		appear->start(QAbstractAnimation::DeleteWhenStopped);

		connect(appear,SIGNAL(finished()),line,SLOT(deleteLater()));
		connect(appear,SIGNAL(finished()),this,SLOT(removeLightBox()));
	} else if(word.startsWith("anim=")){
		PixmapAnimation*pma = PixmapAnimation::GetPixmapAnimation(lightbox,word.mid(5));
		if(pma){
			pma->setZValue(22);
			pma->moveBy(-sceneRect().width()*_m_roomLayout->m_infoPlaneWidthPercentage/2,0);
			connect(pma,SIGNAL(finished()),this,SLOT(removeLightBox()));
		}
	}
#ifndef Q_OS_WINRT
    else if(word.startsWith("skill=")){  // 重新启用武将特效的使用异步动画
        QString hero = word.mid(6);
        const QString skill = args.value(1,QString());

        // 提取武將名字和皮膚編號
        QString heroName = hero;
        int skinId = 0;  // 0 表示原皮
        if (hero.contains("/")) {
            int lastSlash = hero.lastIndexOf("/");
            QString fileName = hero.mid(lastSlash + 1);
            int dotPos = fileName.lastIndexOf(".");
            if (dotPos > 0) fileName = fileName.left(dotPos);
            int underscorePos = fileName.lastIndexOf("_");
            if (underscorePos > 0) {
                QString suffix = fileName.mid(underscorePos + 1);
                bool isNumber = false;
                int num = suffix.toInt(&isNumber);
                if (isNumber) {
                    skinId = num;
                    fileName = fileName.left(underscorePos);
                }
            }
            heroName = fileName;
        }

        // 使用嵌入式QML加载器
        QString qmlPath = "ui-script/animation.qml";
        int effectWidth = sceneRect().width();
        int effectHeight = sceneRect().height();

        // 配置上面的参数
        QVariantMap params;
        params.insert("hero",hero);
        params.insert("heroName",Sanguosha->translate(hero));
        params.insert("skinId", skinId);
        params.insert("heroName",Sanguosha->translate(heroName));
        params.insert("skill",Sanguosha->translate(skill));
        params.insert("sceneWidth",effectWidth);
        params.insert("sceneHeight",effectHeight);
        params.insert("tableWidth",m_tableCenterPos.x()*2);

        // 设置QML参数

        // 创建嵌入式QML加载器
        EmbeddedQmlLoader*embeddedLoader = new EmbeddedQmlLoader(this);

        // 连接信号
        connect(embeddedLoader,&EmbeddedQmlLoader::effectFinished,[embeddedLoader,this](){
#ifdef ANDROID
            // 安卓平台：特效结束后更新按钮位置
            QPointer<Dashboard> safeDashboard = dashboard;
            if(safeDashboard){
                QTimer::singleShot(300,[safeDashboard](){
                    // 使用QPointer确保dashboard仍然存在
                    if(safeDashboard){
                        //safeDashboard->_updateMobileBigButtonsPosition();
                    }
                });
            }
#endif
            // embeddedLoader 会自动清理自己
        });

        connect(embeddedLoader,&EmbeddedQmlLoader::effectError,[embeddedLoader](const QString&){
            embeddedLoader->deleteLater();
        });

        // 加载QML覆盖层
        QWidget*parentWidget = nullptr;
        if(!this->views().isEmpty()){
            parentWidget = this->views().first(); // 获取第一个QGraphicsView
        } else {
            parentWidget = main_window; // 回退到主窗口
        }

        bool success = embeddedLoader->loadQmlOverlay(
            parentWidget,
            qmlPath,
            effectWidth,
            effectHeight,
            params
        );

        if(!success)
			embeddedLoader->deleteLater();
    }
    else if(word.startsWith("ghost=")){
        const QString hero = word.mid(6);
        const QString skill = args.value(1,QString());

        // 使用嵌入式QML加载器
        QString qmlPath = "ui-script/animation.qml";

        // 使用游戏场景的实际尺寸
        int effectWidth = sceneRect().width();
        int effectHeight = sceneRect().height();

        // 配置上面的参数
        QVariantMap params;
        params.insert("hero",hero);
        params.insert("heroName",Sanguosha->translate(hero));
        params.insert("skill",Sanguosha->translate(skill));
        params.insert("sceneWidth",effectWidth);
        params.insert("sceneHeight",effectHeight);
        params.insert("tableWidth",m_tableCenterPos.x()*2);

        // 设置幽灵效果参数

        // 创建嵌入式QML加载器
        EmbeddedQmlLoader*embeddedLoader = new EmbeddedQmlLoader(this);

        // 连接信号
        connect(embeddedLoader,&EmbeddedQmlLoader::effectFinished,[embeddedLoader,this](){
#ifdef ANDROID
            // 安卓平台：幽灵特效结束后更新按钮位置
            QPointer<Dashboard> safeDashboard = dashboard;
            if(safeDashboard){
                QTimer::singleShot(300,[safeDashboard](){
                    // 使用QPointer确保dashboard仍然存在
                    if(safeDashboard){
                        //safeDashboard->_updateMobileBigButtonsPosition();
                    }
                });
            }
#endif
            // embeddedLoader 会自动清理自己
        });

        connect(embeddedLoader,&EmbeddedQmlLoader::effectError,[embeddedLoader](const QString&){
            embeddedLoader->deleteLater();
        });

        // 加载QML覆盖层，启用点击穿透
        QWidget*parentWidget = nullptr;
        if(!this->views().isEmpty()){
            parentWidget = this->views().first(); // 获取第一个QGraphicsView
        } else {
            parentWidget = main_window; // 回退到主窗口
        }

        bool success = embeddedLoader->loadQmlOverlay(
            parentWidget,
            qmlPath,
            effectWidth,
            effectHeight,
            params,
            true// 启用点击穿透模式
        );

        if(!success)
            embeddedLoader->deleteLater();
    }
    else if(word.startsWith("spine=")){
        // Spine 动态全屏特效——render as QGraphicsItem (SpineGlItem)
        // NOTE: Cannot use QOpenGLWidget overlay because FitView already
        // uses QOpenGLWidget as viewport; Qt 5 forbids nested QOpenGLWidgets.
        QString spineArg = word.mid(6);
        QString animName;
		QString runtimeVersion;

		int atPos = spineArg.lastIndexOf('@');
		if (atPos > 0) {
			runtimeVersion = spineArg.mid(atPos + 1).trimmed();
			spineArg = spineArg.left(atPos);
		}

        if(spineArg.contains(":")){
            QStringList parts = spineArg.split(":");
            spineArg = parts.at(0);
            animName = parts.at(1);
        }

		qWarning("[RoomScene] spine= branch: spineArg='%s' animName='%s' runtime='%s'",
				 qPrintable(spineArg), qPrintable(animName), qPrintable(runtimeVersion));

        // Ensure the viewport's GL context is current so that
        // atlas texture loading (QOpenGLTexture) succeeds.
        QOpenGLWidget *glViewport = nullptr;
        if(!this->views().isEmpty()){
            QGraphicsView *gv = this->views().first();
            glViewport = qobject_cast<QOpenGLWidget*>(gv->viewport());
            if(glViewport){
                glViewport->makeCurrent();
                qWarning("[RoomScene] GL viewport context made current for texture loading");
            }
        }

        SpineGlItem *spineItem = new SpineGlItem();
		if (!runtimeVersion.isEmpty())
			spineItem->setRuntimeVersionHint(runtimeVersion);
        addItem(spineItem);
		spineItem->setZValue(9999); // force on top of all scene elements

        bool loaded = spineItem->loadSpine(spineArg, animName);

        if(glViewport)
            glViewport->doneCurrent();

        if(loaded){
            // Position: fill the entire scene
            QRectF sr = sceneRect();
            spineItem->setRenderRect(sr);
            float cx = (float)(sr.width()  / 2.0);
            float cy = (float)(sr.height() / 2.0);
            spineItem->setSpinePosition(QPointF(cx, cy));

            spineItem->play(false);
            qWarning("[RoomScene] SpineGlItem playing, duration=%.3f",
                     spineItem->animationDuration());

            // Auto-remove when the animation finishes
            connect(spineItem, &SpineGlItem::animationFinished, spineItem, [spineItem](){
                qWarning("[RoomScene] SpineGlItem animation finished, removing");
                spineItem->stop();
                if(spineItem->scene())
                    spineItem->scene()->removeItem(spineItem);
                spineItem->deleteLater();
            });

            // Safety timer: remove even if signal doesn't fire
            float dur = spineItem->animationDuration();
            if(dur > 0){
                QTimer::singleShot((int)(dur * 1000) + 1000, spineItem, [spineItem](){
                    if(spineItem->isPlaying()){
                        qWarning("[RoomScene] SpineGlItem safety timer fired, removing");
                        spineItem->stop();
                        if(spineItem->scene())
                            spineItem->scene()->removeItem(spineItem);
                        spineItem->deleteLater();
                    }
                });
            }
        } else {
            qWarning("[RoomScene] SpineGlItem loadSpine FAILED for '%s'", qPrintable(spineArg));
            if(spineItem->scene())
                spineItem->scene()->removeItem(spineItem);
            delete spineItem;
        }

        // Remove lightbox since spine overlay handles its own display
        delete lightbox;
        return;
    }
    else if (word.startsWith("background=")) {
        QString bg = word.mid(11);
        QPixmap pixmap = G_ROOM_SKIN.getPixmap("image/system/backdrop/" + bg);
        if (pixmap.width() > 1 && pixmap.height() > 1) {
            m_tableBgPixmapOrig = pixmap;
            m_tableBgPixmap = pixmap.scaled(m_tablew, m_tableh + _m_roomLayout->m_photoDashboardPadding, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_tableBg->setPixmap(m_tableBgPixmap);
        }
        removeItem(lightbox);
        delete lightbox;
        return;
    }
#endif
	else {
		QFont font = Config.BigFont;
		if(pixelSize > 0) font.setPixelSize(pixelSize);
		QGraphicsTextItem*line = addText(word,font);
		line->setDefaultTextColor(Qt::white);

		QRectF line_rect = line->boundingRect();
		line->setParentItem(lightbox);
		line->setPos(m_tableCenterPos-line_rect.center());

		QPropertyAnimation*appear = new QPropertyAnimation(line,"opacity");
		appear->setStartValue(0.0);
		appear->setKeyValueAt(0.7,1.0);
		appear->setEndValue(0.0);

		appear->setDuration(duration);
		appear->start(QAbstractAnimation::DeleteWhenStopped);

		connect(appear,SIGNAL(finished()),this,SLOT(removeLightBox()));
	}
}

void RoomScene::doHuashen(const QString&,const QStringList&args)
{
	Q_ASSERT(args.length() >= 2);

	QList<CardItem*> generals;
	foreach (QString arg,args.last().split(":")){
		CardItem*item = new CardItem(arg);
		item->setPos(m_tableCenterPos);
		addItem(item);
		generals.append(item);
	}
	CardsMoveStruct move;
	move.to = ClientInstance->getPlayer(args.first());
	move.from_place = Player::DrawPile;
	move.to_place = Player::PlaceSpecial;
	move.to_pile_name = "huashen";

	GenericCardContainer*container = _getGenericCardContainer(Player::PlaceHand,move.to);
	container->addCardItems(generals,move);
}

void RoomScene::showIndicator(const QString&from,const QString&to)
{
	if(from==to||Config.value("NoIndicator").toBool())
		return;

	QGraphicsObject*obj1 = getAnimationObject(from);
	QGraphicsObject*obj2 = getAnimationObject(to);

	if(!obj1||!obj2)
		return;

	QPointF start = obj1->sceneBoundingRect().center();
	QPointF finish = obj2->sceneBoundingRect().center();

	IndicatorItem*indicator = new IndicatorItem(start,finish,ClientInstance->getPlayer(from));
	indicator->setPos(qMin(start.x(),finish.x()),qMin(start.y(),finish.y()));
	indicator->setZValue(INT_MAX);

	addItem(indicator);
	indicator->doAnimation();

	// ── Spine pop-out: trigger Attack action on indicator (from → to) ──
	if (_spineActionController && game_started) {
		bool isLocal = (from == Self->objectName());
		QList<QPointF> targets;
		targets << finish;
		QPointF dir = finish - start;
		double len = qSqrt(dir.x() * dir.x() + dir.y() * dir.y());
		if (len > 0.001) dir /= len;
		_spineActionController->triggerAction(from, ActionType::Attack,
		                                      targets, isLocal, dir);
	}
}

void RoomScene::doIndicate(const QString&,const QStringList&args)
{
	showIndicator(args.first(),args.last());
}

void RoomScene::doAnimation(int name,const QStringList&args)
{
	static QMap<AnimateType,AnimationFunc> map;
	if(map.isEmpty()){
		map[S_ANIMATE_NULLIFICATION] =&RoomScene::doMovingAnimation;

		map[S_ANIMATE_FIRE] =&RoomScene::doAppearingAnimation;
		map[S_ANIMATE_LIGHTNING] =&RoomScene::doAppearingAnimation;
		map[S_ANIMATE_ICE] =&RoomScene::doAppearingAnimation;

		map[S_ANIMATE_LIGHTBOX] =&RoomScene::doLightboxAnimation;
		map[S_ANIMATE_HUASHEN] =&RoomScene::doHuashen;
		map[S_ANIMATE_INDICATE] =&RoomScene::doIndicate;
		map[S_ANIMATE_FLOWER] =&RoomScene::doGiftAnimation;
		map[S_ANIMATE_EGG] =&RoomScene::doGiftAnimation;
	}

	static QMap<AnimateType,QString> anim_name;
	if(anim_name.isEmpty()){
		anim_name[S_ANIMATE_NULLIFICATION] = "nullification";

		anim_name[S_ANIMATE_FIRE] = "fire";
		anim_name[S_ANIMATE_LIGHTNING] = "lightning";
		anim_name[S_ANIMATE_ICE] = "ice";

		anim_name[S_ANIMATE_LIGHTBOX] = "lightbox";
		anim_name[S_ANIMATE_HUASHEN] = "huashen";
		anim_name[S_ANIMATE_INDICATE] = "indicate";
		anim_name[S_ANIMATE_FLOWER] = "flower";
		anim_name[S_ANIMATE_EGG] = "egg";
	}

	AnimationFunc func = map.value((AnimateType)name,nullptr);
	if(func) (this->*func)(anim_name.value((AnimateType)name,""),args);
}

void RoomScene::doGiftAnimation(const QString &gift_type, const QStringList &args)
{
	if (args.size() < 2) return;

	QString from = args.first();
	QString to = args.last();

	if (from == to || Config.value("NoIndicator", false).toBool())
		return;

	QGraphicsObject *obj1 = getAnimationObject(from);
	QGraphicsObject *obj2 = getAnimationObject(to);

	if (!obj1 || !obj2)
		return;

	QPointF start = obj1->sceneBoundingRect().center();
	QPointF finish = obj2->sceneBoundingRect().center();

	GiftItem *gift = new GiftItem(start, finish, gift_type, ClientInstance->getPlayer(from));
	gift->setPos(start);
	gift->setZValue(INT_MAX);

	addItem(gift);
	gift->doAnimation();
}

void RoomScene::onGiftModeActivated(const QString &gift_type)
{
	if (gift_selection_mode) {
		exitGiftSelectionMode();
	}

	gift_selection_mode = true;
	current_gift_type = gift_type;

	gift_highlighted_photos.clear();
	foreach (Photo *photo, photos) {
		const ClientPlayer *player = photo->getPlayer();
		if (player && player != Self) {
			photo->setGiftHighlight(true);
			gift_highlighted_photos.append(photo);
		}
	}

	static bool gift_signals_connected = false;
	if (!gift_signals_connected) {
		foreach (Photo *photo, photos) {
			connect(photo, SIGNAL(giftClicked()), this, SLOT(onPlayerSelectedForGift()));
		}
		gift_signals_connected = true;
	}
}

void RoomScene::onPlayerSelectedForGift()
{
	if (!gift_selection_mode) return;

	Photo *photo = qobject_cast<Photo *>(sender());
	if (!photo) return;

	handleGiftSelection(photo);
}

void RoomScene::handleGiftSelection(Photo *photo)
{
	if (!gift_selection_mode || !photo) return;

	const ClientPlayer *target = photo->getPlayer();
	if (!target || target == Self) return;

	if (!gift_highlighted_photos.contains(photo)) return;

	QString command = QString(".%1 %2").arg(current_gift_type).arg(target->objectName());
	chat_edit->setText(command);
	speak();

	exitGiftSelectionMode();
}

void RoomScene::exitGiftSelectionMode()
{
	if (!gift_selection_mode) return;

	gift_selection_mode = false;
	current_gift_type = QString();

	foreach (Photo *photo, gift_highlighted_photos) {
		if (photo) {
			photo->setGiftHighlight(false);
		}
	}
	gift_highlighted_photos.clear();
}

void RoomScene::showServerInformation()
{
	QDialog*dialog = new QDialog(main_window);
	dialog->setWindowTitle(tr("Server information"));

	QHBoxLayout*layout = new QHBoxLayout;
	ServerInfoWidget*widget = new ServerInfoWidget;
	widget->fill(ServerInfo,Config.HostAddress);
	layout->addWidget(widget);
	dialog->setLayout(layout);

	dialog->show();
}

void RoomScene::surrender()
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(main_window,tr("Warning"),tr("You can only initiate a surrender poll at your play phase!"));
		return;
	}

	QMessageBox::StandardButton button;
	button = QMessageBox::question(main_window,tr("Surrender"),tr("Are you sure to surrender ?"));
	if(button==QMessageBox::Ok||button==QMessageBox::Yes)
		ClientInstance->requestSurrender();
}

void RoomScene::fillGenerals1v1(const QStringList&names)
{
	int len = names.length()/2;
	QString path = QString("image/system/1v1/select%1.png").arg(len==5 ? "" : "2");
	selector_box = new QSanSelectableItem(path,true);
	selector_box->setFlag(QGraphicsItem::ItemIsMovable);
	selector_box->setPos(m_tableCenterPos);
	addItem(selector_box);
	selector_box->setZValue(10);

	const static int start_x = 42+G_COMMON_LAYOUT.m_cardNormalWidth/2;
	const static int width = 86;
	const static int start_y = 59+G_COMMON_LAYOUT.m_cardNormalHeight/2;
	const static int height=121;

	foreach (QString name,names){
		CardItem*item = new CardItem(name);
		item->setObjectName(name);
		general_items << item;
	}

	qShuffle(general_items);

	int n = names.length();
	double scaleRatio = 116.0/G_COMMON_LAYOUT.m_cardNormalHeight;
	for (int i = 0;i < n;i++){
		int row,column;
		if(i < len){
			row = 1;
			column = i;
		} else {
			row = 2;
			column = i-len;
		}

		CardItem*general_item = general_items.at(i);
		general_item->scaleSmoothly(scaleRatio);
		general_item->setParentItem(selector_box);
		general_item->setPos(start_x+width*column,start_y+height*row);
		general_item->setHomePos(general_item->pos());
	}
}

void RoomScene::fillGenerals3v3(const QStringList&names)
{
	QString temperature;
	if(Self->getRole().startsWith("l"))
		temperature = "warm";
	else
		temperature = "cool";

	QString path = QString("image/system/3v3/select-%1.png").arg(temperature);
	selector_box = new QSanSelectableItem(path,true);
	selector_box->setFlag(QGraphicsItem::ItemIsMovable);
	addItem(selector_box);
	selector_box->setZValue(10);
	selector_box->setPos(m_tableCenterPos);

	const static int start_x = 109;
	const static int width = 86;
	const static int row_y[4] = { 150,271,394,516 };

	int n = names.length();
	double scaleRatio = 116.0/G_COMMON_LAYOUT.m_cardNormalHeight;
	for (int i = 0;i < n;i++){
		int row,column;
		if(i < 8){
			row = 1;
			column = i;
		} else {
			row = 2;
			column = i-8;
		}

		CardItem*general_item = new CardItem(names.at(i));
		general_item->scaleSmoothly(scaleRatio);
		general_item->setParentItem(selector_box);
		general_item->setPos(start_x+width*column,row_y[row]);
		general_item->setHomePos(general_item->pos());
		general_item->setObjectName(names.at(i));

		general_items << general_item;
	}
}

void RoomScene::fillGenerals(const QStringList&names)
{
	if(ServerInfo.GameMode=="06_3v3")
		fillGenerals3v3(names);
	else if(ServerInfo.GameMode=="02_1v1")
		fillGenerals1v1(names);
}

void RoomScene::bringToFront(QGraphicsItem*front_item)
{
	if(_m_last_front_item!=nullptr)
		_m_last_front_item->setZValue(_m_last_front_ZValue);
	_m_last_front_item = front_item;
	_m_last_front_ZValue = front_item->zValue();
	if(pindian_box&&front_item!=pindian_box&&pindian_box->isVisible()){
		bringToFront(pindian_box);
		front_item->setZValue(9);
	} else
		front_item->setZValue(10);
}

void RoomScene::takeGeneral(const QString&who,const QString&name,const QString&rule)
{
	bool self_taken;
	if(who=="warm")
		self_taken = Self->getRole().startsWith("l");
	else
		self_taken = Self->getRole().startsWith("r");
	QList<CardItem*>*to_add = self_taken ?&down_generals :&up_generals;

	CardItem*general_item = nullptr;
	foreach (CardItem*item,general_items){
		if(item->objectName()==name){
			general_item = item;
			break;
		}
	}

	Q_ASSERT(general_item);

	general_item->disconnect(this);
	general_items.removeOne(general_item);
	to_add->append(general_item);

	int x,y;
	if(ServerInfo.GameMode=="06_3v3"){
		x = 63+(to_add->length()-1)*(148-62);
		y = self_taken ? 452 : 85;
	} else {
		x = 43+(to_add->length()-1)*86;
		y = self_taken ? 60+120*3 : 60;
	}
	x = x+G_COMMON_LAYOUT.m_cardNormalWidth/2;
	y = y+G_COMMON_LAYOUT.m_cardNormalHeight/2;
	general_item->setHomePos(QPointF(x,y));
	general_item->goBack(true);

	if(((ServerInfo.GameMode=="06_3v3"&&Self->getRole()!="lord"&&Self->getRole()!="renegade")
		|| (ServerInfo.GameMode=="02_1v1"&&rule=="2013"))
		&&general_items.isEmpty()){
		if(selector_box){
			selector_box->hide();
			delete selector_box;
			selector_box = nullptr;
		}
	}
}

void RoomScene::recoverGeneral(int index,const QString&name)
{
	QString obj_name = QString("x%1").arg(index);
	foreach (CardItem*item,general_items){
		if(item->objectName()==obj_name){
			item->changeGeneral(name);
			break;
		}
	}
}

void RoomScene::startGeneralSelection()
{
	foreach (CardItem*item,general_items){
		item->setFlag(QGraphicsItem::ItemIsFocusable);
		connect(item,SIGNAL(double_clicked()),this,SLOT(selectGeneral()));
	}
}

void RoomScene::selectGeneral()
{
	CardItem*item = qobject_cast<CardItem*>(sender());
	if(item){
		ClientInstance->replyToServer(S_COMMAND_ASK_GENERAL,item->objectName());
		foreach (CardItem*item,general_items){
			item->setFlag(QGraphicsItem::ItemIsFocusable,false);
			item->disconnect(this);
		}
		ClientInstance->setStatus(Client::NotActive);
	}
}

void RoomScene::changeGeneral(const QString&general)
{
	if(to_change&&arrange_button) to_change->changeGeneral(general);
}

void RoomScene::revealGeneral(bool self,const QString&general)
{
	if(self)
		self_box->revealGeneral(general);
	else
		enemy_box->revealGeneral(general);
}

void RoomScene::trust()
{
	if(Self->getState()!="trust")
		doCancelButton();
	ClientInstance->trust();
}

void RoomScene::startArrange(const QString&to_arrange)
{
	arrange_items.clear();
	QString mode;
	QList<QPointF> positions;
	if(ServerInfo.GameMode=="06_3v3"){
		mode = "3v3";
		positions << QPointF(279,356) << QPointF(407,356) << QPointF(535,356);
	} else if(ServerInfo.GameMode=="02_1v1"){
		mode = "1v1";
		if(down_generals.length()==5)
			positions << QPointF(130,335) << QPointF(260,335) << QPointF(390,335);
		else
			positions << QPointF(173,335) << QPointF(303,335) << QPointF(433,335);
	}

	if(ServerInfo.GameMode=="06_XMode"){
		QStringList arrangeList = to_arrange.split("+");
		if(arrangeList.length()==5)
			positions << QPointF(130,335) << QPointF(260,335) << QPointF(390,335);
		else
			positions << QPointF(173,335) << QPointF(303,335) << QPointF(433,335);
		QString path = QString("image/system/XMode/arrange%1.png").arg((arrangeList.length()==5) ? 1 : 2);
		selector_box = new QSanSelectableItem(path,true);
		selector_box->setFlag(QGraphicsItem::ItemIsMovable);
		selector_box->setPos(m_tableCenterPos);
		addItem(selector_box);
		selector_box->setZValue(10);
	} else {
		QString suffix = (mode=="1v1"&&down_generals.length()==6) ? "2" : "";
		QString path = QString("image/system/%1/arrange%2.png").arg(mode).arg(suffix);
		selector_box->load(path);
		selector_box->setPos(m_tableCenterPos);
	}

	if(ServerInfo.GameMode=="06_XMode"){
		Q_ASSERT(!to_arrange.isEmpty());
		down_generals.clear();
		foreach (QString name,to_arrange.split("+")){
			CardItem*item = new CardItem(name);
			item->setObjectName(name);
			item->scaleSmoothly(116.0/G_COMMON_LAYOUT.m_cardNormalHeight);
			item->setParentItem(selector_box);
			int x = 43+down_generals.length()*86;
			int y = 60+120*3;
			x = x+G_COMMON_LAYOUT.m_cardNormalWidth/2;
			y = y+G_COMMON_LAYOUT.m_cardNormalHeight/2;
			item->setPos(x,y);
			item->setHomePos(QPointF(x,y));
			down_generals << item;
		}
	}
	foreach (CardItem*item,down_generals){
		item->setFlag(QGraphicsItem::ItemIsFocusable);
		item->setAutoBack(false);
		connect(item,SIGNAL(released()),this,SLOT(toggleArrange()));
	}

	static QRect rect(0,0,80,120);

	foreach (QPointF pos,positions){
		QGraphicsRectItem*rect_item = new QGraphicsRectItem(rect,selector_box);
		rect_item->setPos(pos);
		rect_item->setPen(Qt::NoPen);
		arrange_rects << rect_item;
	}

	arrange_button = new Button(tr("Complete"),0.8);
	arrange_button->setParentItem(selector_box);
	arrange_button->setPos(600,330);
	connect(arrange_button,SIGNAL(clicked()),this,SLOT(finishArrange()));
}

void RoomScene::toggleArrange()
{
	CardItem*item = qobject_cast<CardItem*>(sender());
	if(item==nullptr) return;

	QGraphicsItem*arrange_rect = nullptr;
	int index = -1;
	for (int i = 0;i < 3;i++){
		QGraphicsItem*rect = arrange_rects.at(i);
		if(item->collidesWithItem(rect)){
			arrange_rect = rect;
			index = i;
		}
	}

	if(arrange_rect==nullptr){
		if(arrange_items.contains(item)){
			arrange_items.removeOne(item);
			down_generals << item;
		}
	} else {
		arrange_items.removeOne(item);
		down_generals.removeOne(item);
		arrange_items.insert(index,item);
	}

	int n = qMin(arrange_items.length(),3);
	for (int i = 0;i < n;i++){
		QPointF pos = arrange_rects.at(i)->pos();
		CardItem*item = arrange_items.at(i);
		item->setHomePos(pos);
		item->goBack(true);
	}

	while (arrange_items.length() > 3){
		CardItem*last = arrange_items.takeLast();
		down_generals << last;
	}

	for (int i = 0;i < down_generals.length();i++){
		QPointF pos;
		if(ServerInfo.GameMode=="06_3v3")
			pos = QPointF(65+G_COMMON_LAYOUT.m_cardNormalWidth/2+i*86,
			452+G_COMMON_LAYOUT.m_cardNormalHeight/2);
		else
			pos = QPointF(43+G_COMMON_LAYOUT.m_cardNormalWidth/2+i*86,
			60+G_COMMON_LAYOUT.m_cardNormalHeight/2+3*120);
		CardItem*item = down_generals.at(i);
		item->setHomePos(pos);
		item->goBack(true);
	}
}

void RoomScene::finishArrange()
{
	if(arrange_items.length()!=3) return;

	arrange_button->deleteLater();

	QStringList names;
	foreach(CardItem*item,arrange_items)
		names << item->objectName();

	if(selector_box)
		selector_box->deleteLater();

	arrange_rects.clear();

	ClientInstance->replyToServer(S_COMMAND_ARRANGE_GENERAL,JsonUtils::toJsonArray(names));
	ClientInstance->setStatus(Client::NotActive);
}

void RoomScene::showPindianBox(const QString&from_name,int from_id,const QString&to_name,int to_id,const QString&reason)
{
	pindian_box->setOpacity(0.0);
	pindian_box->setPos(m_tableCenterPos);

	if(reason.isEmpty()) pindian_box->setTitle(tr("pindian"));
	else pindian_box->setTitle(Sanguosha->translate(reason));

	if(pindian_from_card){
		delete pindian_from_card;
		pindian_from_card = nullptr;
	}
	if(pindian_to_card){
		delete pindian_to_card;
		pindian_to_card = nullptr;
	}

	pindian_from_card = new CardItem(Sanguosha->getEngineCard(from_id));
	pindian_from_card->setParentItem(pindian_box);
	pindian_from_card->setPos(QPointF(77.5, 112));
	//pindian_from_card->setFlag(QGraphicsItem::ItemIsMovable,false);
	pindian_from_card->setHomePos(pindian_from_card->pos());
	pindian_from_card->setFootnote(ClientInstance->getPlayerName(from_name));
	//pindian_to_card->showAvatar(ClientInstance->getPlayer(from_name)->getGeneralName());

	pindian_to_card = new CardItem(Sanguosha->getEngineCard(to_id));
	pindian_to_card->setParentItem(pindian_box);
	pindian_to_card->setPos(QPointF(174.5, 112));
	//pindian_to_card->setFlag(QGraphicsItem::ItemIsMovable,false);
	pindian_to_card->setHomePos(pindian_to_card->pos());
	pindian_to_card->setFootnote(ClientInstance->getPlayerName(to_name));
	//pindian_to_card->showAvatar(ClientInstance->getPlayer(to_name)->getGeneralName());

	bringToFront(pindian_box);
	pindian_box->appear();
	QTimer::singleShot(444,this,SLOT(doPindianAnimation()));
}

void RoomScene::doPindianAnimation()
{
	if(pindian_box->isVisible()&&pindian_from_card){
		QString emotion = pindian_success ? "success" : "no-success";
		PixmapAnimation*pma = PixmapAnimation::GetPixmapAnimation(pindian_from_card,emotion);
		if(pma) connect(pma,SIGNAL(finished()),pindian_box,SLOT(disappear()));
		else pindian_box->disappear();
	}
}

static void AddRoleIcon(QMap<QChar,QPixmap>&map,char c,const QString&role)
{
	QPixmap pixmap(QString("image/system/roles/small-%1.png").arg(role));

	QChar qc(c);
	map[qc.toUpper()] = pixmap;

	QSanUiUtils::makeGray(pixmap);
	map[qc.toLower()] = pixmap;
}

void RoomScene::updateRoles(const QString&roles)
{
	foreach(QGraphicsItem*item,role_items)
		removeItem(item);

	role_items.clear();
	if(ServerInfo.EnableHegemony) return;

	static QMap<QChar,QPixmap> map;
	if(map.isEmpty()){
		AddRoleIcon(map,'Z',"lord");
		AddRoleIcon(map,'C',"loyalist");
		AddRoleIcon(map,'F',"rebel");
		AddRoleIcon(map,'N',"renegade");
	}

	foreach (QChar c,roles){
		if(map.contains(c)){
			QGraphicsPixmapItem*item = addPixmap(map.value(c));
			role_items << item;
		}
	}
	updateRolesBox();
}

void RoomScene::updateRolesBox()
{
	double centerX = m_rolesBox->boundingRect().width()/2;
	int n = role_items.length();
	for (int i = 0;i < n;i++){
		QGraphicsPixmapItem*item = role_items[i];
		item->setParentItem(m_rolesBox);
		item->setPos(21*(i-n/2)+centerX,6);
	}
	m_pileCardNumInfoTextBox->setTextWidth(m_rolesBox->boundingRect().width());
	m_pileCardNumInfoTextBox->setPos(0,35);
}

void RoomScene::appendChatEdit(QString txt)
{
	chat_edit->setText(chat_edit->text()+txt);
	chat_edit->setFocus();
}

void RoomScene::showBubbleChatBox(const QString&who,const QString&line)
{
	if(Config.BubbleChatBoxKeepTime==0) return;
	if(!bubbleChatBoxes.keys().contains(who)){
		BubbleChatBox*bubbleChatBox = new BubbleChatBox(getBubbleChatBoxShowArea(who));
		addItem(bubbleChatBox);
		bubbleChatBox->setZValue(INT_MAX);
		bubbleChatBoxes.insert(who,bubbleChatBox);
	}
	bubbleChatBoxes[who]->setText(line);
}

QRect RoomScene::getBubbleChatBoxShowArea(const QString&who) const
{
	Photo*photo = name2photo.value(who,nullptr);
	if(photo){
		QRectF rect = photo->sceneBoundingRect();
		QPoint center = rect.center().toPoint();
		return QRect(QPoint(center.x(),center.y()-90),G_COMMON_LAYOUT.m_bubbleChatBoxShowAreaSize);
	} else {
		QRectF rect = dashboard->getAvatarAreaSceneBoundingRect();
		return QRect(QPoint(rect.left()+45,rect.top()-20),G_COMMON_LAYOUT.m_bubbleChatBoxShowAreaSize);
	}
}

void RoomScene::setChatBoxVisible(bool show)
{
	if(show){
		chat_box_widget->show();
		chat_edit->show();
		chat_widget->show();
		log_box->resize(_m_infoPlane.width(),
			_m_infoPlane.height()*_m_roomLayout->m_logBoxHeightPercentage);
	} else {
		chat_box_widget->hide();
		chat_edit->hide();
		chat_widget->hide();
		log_box->resize(_m_infoPlane.width(),
			_m_infoPlane.height()*(_m_roomLayout->m_logBoxHeightPercentage
			+ _m_roomLayout->m_chatBoxHeightPercentage)
			+ _m_roomLayout->m_chatTextBoxHeight);
	}
}

void RoomScene::setChatBoxVisibleSlot()
{
	setChatBoxVisible(!chat_box_widget->isVisible());
}

void RoomScene::pause()
{
	if(!Self||!Self->isOwner()||ClientInstance->getPlayers().length() < Sanguosha->getPlayerCount(ServerInfo.GameMode)) return;
	foreach (const ClientPlayer*p,ClientInstance->getPlayers()){
		if(p!=Self&&p->isAlive()&&p->getState()!="robot")
			return;
	}
	bool paused = pausing_text->isVisible();
	ClientInstance->notifyServer(S_COMMAND_PAUSE,!paused);
}

void RoomScene::addRobot()
{
	int left = Sanguosha->getPlayerCount(ServerInfo.GameMode)-ClientInstance->getPlayerCount();
	if(left==1){
		ClientInstance->addRobot(1);
	} else {
		QMenu*menu = m_add_robot_menu;
		menu->clear();
		menu->setTitle(tr("Add robots"));

		for (int i = 1;i <= left;i++){
			QAction*action = menu->addAction(tr("%1 robots").arg(i));
			action->setData(i);
			connect(action,SIGNAL(triggered()),this,SLOT(doAddRobotAction()));
		}

		QPointF posf = QCursor::pos();
		menu->popup(QPoint(posf.x(),posf.y()));
	}
}

void RoomScene::doAddRobotAction()
{
	int num = 1;
	QAction*action = qobject_cast<QAction*>(sender());
	if(action) num = action->data().toInt();

	ClientInstance->addRobot(num);
}

void RoomScene::fillRobots()
{
	ClientInstance->addRobot(-1);
}

void RoomScene::updateVolumeConfig()
{
	if(!game_started) return;
	if(Config.BGMVolume>0){
		// start playing background music
		QString bgMusicPath = Config.value("BackgroundMusic","audio/system/background.ogg").toString();
#ifdef AUDIO_SUPPORT
		bool modified = bgMusicPath!=_m_bgMusicPath;
		if(modified){
			_m_bgMusicPath = bgMusicPath;
			Audio::stopBGM();
		}
		if(modified||!_m_bgEnabled)
			Audio::playBGM(_m_bgMusicPath);
		Audio::setBGMVolume(Config.BGMVolume);
#endif
		_m_bgEnabled = true;
	} else {
#ifdef AUDIO_SUPPORT
		Audio::stopBGM();
#endif
		_m_bgEnabled = false;
	}
}

void RoomScene::redrawDashboardButtons()
{
	ok_button->redraw();
	ok_button->setRect(G_DASHBOARD_LAYOUT.m_confirmButtonArea);

	cancel_button->redraw();
	cancel_button->setRect(G_DASHBOARD_LAYOUT.m_cancelButtonArea);

	discard_button->redraw();
	discard_button->setRect(G_DASHBOARD_LAYOUT.m_discardButtonArea);

	trust_button->redraw();
	trust_button->setRect(G_DASHBOARD_LAYOUT.m_trustButtonArea);
}

void RoomScene::recorderAutoSave()
{
	if(ClientInstance->getReplayer()||!Config.value("recorder/autosave",true).toBool())
		return;

	if(Config.value("recorder/networkonly",true).toBool()){
		bool is_network = false;
		foreach(const ClientPlayer*player,ClientInstance->getPlayers()){
			is_network = player!=Self&&player->getState()!="robot";
			if(is_network) break;
		}
		if(!is_network)
			return;
	}

	QString path = QDir::currentPath()+"/record";
	if(!QDir(path).exists())
		QDir().mkpath(path);
	QString filename = path+"/"+QDateTime::currentDateTime().toString("yyyy年MM月dd日HH时mm分ss秒")+".txt";
	ClientInstance->save(filename);
}

PromptInfoItem::PromptInfoItem(QGraphicsItem*parent)
	: QGraphicsTextItem(parent)
{
}

void PromptInfoItem::setHtml(const QString&painter)
{
	QGraphicsTextItem::setHtml(painter);
	setPos(325-boundingRect().width()/2,30);
	setDocument(ClientInstance->getPromptDoc());
}

/*QRectF PromptInfoItem::boundingRect() const
{
	return QRectF(0,0,G_COMMON_LAYOUT.m_promptInfoSize.width(),
		G_COMMON_LAYOUT.m_promptInfoSize.height());
}

void PromptInfoItem::paint(QPainter*painter,const QStyleOptionGraphicsItem*,QWidget*)
{
	//过滤掉多余的前后空白字符
	QString text = toPlainText();
	if(!text.isEmpty()){
		QStringList texts;
		foreach (const QString&plainText,text.split("\n"))
			texts.append(plainText.trimmed());
		text = texts.join("\n");

		//经测试发现，ttf字体显示不出“红桃”、“黑桃”等这些图形符号，故将它们替换为相应的文字说明
		text.replace(Sanguosha->translate("spade_char"),Sanguosha->translate("spade"));
		text.replace(Sanguosha->translate("club_char"),Sanguosha->translate("club"));
		text.replace(Sanguosha->translate("heart_char"),Sanguosha->translate("heart"));
		text.replace(Sanguosha->translate("diamond_char"),Sanguosha->translate("diamond"));

		if(!text.isEmpty()){
			G_COMMON_LAYOUT.m_promptInfoFont.paintText(painter,boundingRect().toRect(),
				(Qt::AlignmentFlag)((int)Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWrapAnywhere),text);
		}
	}
}*/

// ═══════════════════════════════════════════════════════════════════════════
//  Spine pop-out action controller helpers
// ═══════════════════════════════════════════════════════════════════════════

void RoomScene::registerDynamicSkinForPlayer(const QString &playerName,
                                              const QString &generalName,
                                              bool isPrimary)
{
	if (!_spineActionController || generalName.isEmpty())
		return;
	if (!_spineActionController->hasDynamicSkin(generalName))
		return;

	QString skinName = _spineActionController->defaultSkinNameForGeneral(generalName);
	if (skinName.isEmpty())
		return;

	SkinConfig config;
	if (!_spineActionController->buildSkinConfigForGeneral(generalName, skinName, config)) {
		qWarning("[RoomScene] buildSkinConfig FAILED: '%s'/'%s'",
		         qPrintable(generalName), qPrintable(skinName));
		return;
	}

	QString skinId = generalName + "/" + skinName;
	_spineActionController->registerSkin(playerName, skinId, config, isPrimary);
}

void RoomScene::registerDynamicSkinsForAllPlayers()
{
	if (!_spineActionController) return;

	// Dashboard (self)
	if (Self) {
		const General *g1 = Self->getGeneral();
		const General *g2 = Self->getGeneral2();
		if (g1) registerDynamicSkinForPlayer(Self->objectName(), g1->objectName(), true);
		if (g2) registerDynamicSkinForPlayer(Self->objectName(), g2->objectName(), false);
	}

	// All photo players (remote)
	foreach (Photo *photo, photos) {
		const ClientPlayer *player = photo->getPlayer();
		if (!player) continue;
		const General *g1 = player->getGeneral();
		const General *g2 = player->getGeneral2();
		if (g1) registerDynamicSkinForPlayer(player->objectName(), g1->objectName(), true);
		if (g2) registerDynamicSkinForPlayer(player->objectName(), g2->objectName(), false);
	}
}

void RoomScene::updateSpineSeatGeometry()
{
	if (!_spineActionController) return;

	// Dashboard (self) — use the avatar area (right frame), not the entire dashboard
	if (Self && dashboard) {
		QRectF rect = dashboard->getAvatarAreaSceneBoundingRect();
		_spineActionController->updateSeatGeometry(
			Self->objectName(), rect.topLeft(), rect.size());
	}

	// Photo players — use the avatar icon rect if available, else photo rect
	foreach (Photo *photo, photos) {
		const ClientPlayer *player = photo->getPlayer();
		if (!player) continue;
		QRectF rect;
		GraphicsPixmapHoverItem *avatarIcon = photo->getAvartarItem();
		if (avatarIcon) {
			rect = avatarIcon->sceneBoundingRect();
		} else {
			rect = photo->sceneBoundingRect();
		}
		_spineActionController->updateSeatGeometry(
			player->objectName(), rect.topLeft(), rect.size());
	}
}

void RoomScene::showGeneralPile(const QString &tag_name) {
	if (Self == nullptr || tag_name.isEmpty())
		return;
	HuashenDialog *dialog = new HuashenDialog(tag_name);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->popup();
}

