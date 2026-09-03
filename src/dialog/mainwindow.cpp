#include "mainwindow.h"
#include "startscene.h"
#include "roomscene.h"
#include "server.h"
#include "generaloverview.h"
#include "cardoverview.h"
#include "ui_mainwindow.h"
#include "scenario-overview.h"
#include "window.h"
#include "pixmapanimation.h"
#include "record-analysis.h"
#include "banipdialog.h"
#include "recorder.h"
#include "lua.hpp"
#include "engine.h"
#include "connectiondialog.h"
#include "configdialog.h"
#include "clientstruct.h"
#include "client.h"
#include "clientplayer.h"
#include "game-session-config.h"
#include "game-snapshot.h"
#include "replay-index.h"
#include "settings.h"
#include "button.h"
#include "build-features.h"
#if QSAN_ENABLE_QML
#include "homecontroller.h"
#include "pointer-effect-overlay.h"
#endif
#include "game-view.h"
#include "crashhandler.h"
#ifdef AUDIO_SUPPORT
#include "audio.h"
#endif
#include <QStackedWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#if QSAN_ENABLE_QML
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickView>
#endif
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QCryptographicHash>
#include <QTextStream>
#if QSAN_ENABLE_QML
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QtQml>
#endif
#include <QFile>
#include <QDebug>
#include <algorithm>

namespace {

QString requestedHomeRenderHost()
{
    const QString prefix = QStringLiteral("--home-render-host=");
    const QStringList arguments = QCoreApplication::arguments();
    for (const QString &argument : arguments) {
        if (!argument.startsWith(prefix))
            continue;
        const QString value = argument.mid(prefix.size()).trimmed().toLower();
        if (value == QLatin1String("widget") || value == QLatin1String("view"))
            return value;
        qWarning().noquote() << "Unknown home render host:" << value
                            << "(using widget)";
        break;
    }
    return QStringLiteral("widget");
}

}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent), ui(new Ui::MainWindow), server(nullptr)
{
	ui->setupUi(this);

	setWindowTitle(tr("Sanguosha")+" 岁末 "+Sanguosha->getVersionNumber());

	// 啟動即在大廳,登記給 crash handler(進入對局/回放時由 RoomScene 更新)
	CrashHandler::setGamePhase(CrashHandler::PhaseLobby);

	scene = nullptr;

	connection_dialog = new ConnectionDialog(this);
	connect(ui->actionStart_Game, SIGNAL(triggered()), connection_dialog, SLOT(exec()));
	connect(connection_dialog, SIGNAL(accepted()), this, SLOT(startConnection()));

	config_dialog = new ConfigDialog(this);
	connect(ui->actionConfigure, SIGNAL(triggered()), config_dialog, SLOT(show()));
	connect(config_dialog, SIGNAL(bg_changed()), this, SLOT(changeBackground()));
	// 預覽視覺模式/背景時,重新載入主頁 QML 讓 MultiEffect 即時套用
	connect(config_dialog, &ConfigDialog::previewChanged, this, &MainWindow::reloadHomePage);
	connect(config_dialog, &ConfigDialog::uiScalePreviewChanged, this, &MainWindow::setUiScale);

	connect(ui->actionAbout_Qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
	connect(ui->actionAcknowledgement_2, SIGNAL(triggered()), this, SLOT(on_actionAcknowledgement_triggered()));

	pageStack = new QStackedWidget(this);

#if QSAN_ENABLE_QML
	homeController = new HomeController(this);
	connect(config_dialog, &ConfigDialog::liveVisualChanged, homeController, &HomeController::notifyVisualSettings);
	m_homeRenderHost = requestedHomeRenderHost();
	if (m_homeRenderHost == QLatin1String("view")) {
		homeWindow = new QQuickView;
		homeWindow->setResizeMode(QQuickView::SizeRootObjectToView);
		// HomeScene has no implicit size. Seed the native window before it is
		// embedded; afterwards the container owns geometry and keeps it synced.
		const QSize initialHomeSize = Config.value(
			QStringLiteral("WindowSize"), QSize(1366, 706)).toSize();
		homeWindow->resize(initialHomeSize.expandedTo(QSize(1, 1)));
		homePageWidget = QWidget::createWindowContainer(homeWindow, pageStack);
		homePageWidget->setObjectName(QStringLiteral("homeQuickViewContainer"));
		homePageWidget->setFocusPolicy(Qt::StrongFocus);
	} else {
		homeWidget = new QQuickWidget(pageStack);
		homeWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
		homePageWidget = homeWidget;
	}
	qInfo().noquote() << "Home render host:" << m_homeRenderHost;
#endif
	gameView = new FitView(nullptr, this);

#if QSAN_ENABLE_QML
	setHomeSceneClearColor(QColor(QStringLiteral("#0B1A2E")));
	pageStack->addWidget(homePageWidget);
#endif
	setupLocalLoadingPage();
	pageStack->addWidget(gameView);

	setCentralWidget(pageStack);
#if QSAN_ENABLE_QML
	m_pointerOverlay = new PointerEffectOverlay(this);
#endif
	restoreFromConfig();

	setupHomePage();
	showHomePage();

	addAction(ui->actionShow_Hide_Menu);
	addAction(ui->actionFullscreen);

	connect(ui->actionRestart_Game, SIGNAL(triggered()), this, SLOT(startConnection()));
	connect(ui->actionReturn_to_Main_Menu, &QAction::triggered, this, [this]() {
		showHomePage();
	});

	systray = nullptr;
}

void MainWindow::setupHomePage()
{
#if QSAN_ENABLE_QML
	const QUrl homeUrl(QStringLiteral("qrc:/QSanguosha/Home/HomeScene.qml"));

	qInfo().noquote() << "Home QRC exists:"
		<< QFile::exists(QStringLiteral(":/QSanguosha/Home/HomeScene.qml"));

	setHomeSceneClearColor(homeController && homeController->isDarkTheme()
		? QColor(QStringLiteral("#0B1A2E"))
		: QColor(QStringLiteral("#DCEEFF")));

	if (homeWidget) {
		connect(homeWidget, &QQuickWidget::statusChanged, this,
			[this](QQuickWidget::Status status) {
				const HomeSceneLoadState state = status == QQuickWidget::Ready
					? HomeSceneLoadState::Ready
					: status == QQuickWidget::Error
						? HomeSceneLoadState::Error
						: status == QQuickWidget::Loading
							? HomeSceneLoadState::Loading : HomeSceneLoadState::Null;
				updateHomeSceneLoadState(state);
			});
	} else if (homeWindow) {
		connect(homeWindow, &QQuickView::statusChanged, this,
			[this](QQuickView::Status status) {
				const HomeSceneLoadState state = status == QQuickView::Ready
					? HomeSceneLoadState::Ready
					: status == QQuickView::Error
						? HomeSceneLoadState::Error
						: status == QQuickView::Loading
							? HomeSceneLoadState::Loading : HomeSceneLoadState::Null;
				updateHomeSceneLoadState(state);
			});
	}

	homeRootContext()->setContextProperty(
		QStringLiteral("homeController"), homeController);
	homeRootContext()->setContextProperty(
		QStringLiteral("Config"), &Config);

	const QString qmlImportPath = QStringLiteral(QT_QML_IMPORT_PATH);
	qInfo().noquote() << "QML import path:" << qmlImportPath;
	homeQmlEngine()->addImportPath(qmlImportPath);

	qmlRegisterType<HomePointerFxItem>(
		"QSanguosha.HomeFx", 1, 0, "HomePointerFx");

	setHomeSceneSource(homeUrl);
	connect(homeController, &HomeController::qmlSceneRequested, this,
		[this](const QUrl &source) {
			setHomeSceneSource(source);
			focusHomeScene();
		});

	connect(homeController, &HomeController::quickJoinRequested,
		this, &MainWindow::startLocalConsoleGame);
	connect(homeController, &HomeController::joinGameRequested,
		ui->actionStart_Game, &QAction::trigger);
	connect(homeController, &HomeController::startServerRequested,
		ui->actionStart_Server, &QAction::trigger);
	connect(homeController, &HomeController::generalsRequested,
		ui->actionGeneral_Overview, &QAction::trigger);
	connect(homeController, &HomeController::cardsRequested,
		ui->actionCard_Overview, &QAction::trigger);
	connect(homeController, &HomeController::replaysRequested,
		ui->actionReplay, &QAction::trigger);
	connect(homeController, &HomeController::settingsRequested,
		ui->actionConfigure, &QAction::trigger);
	connect(homeController, &HomeController::aboutRequested,
		ui->actionAbout, &QAction::trigger);

	connect(config_dialog, &ConfigDialog::accepted,
		this, &MainWindow::reloadHomePage);

#else
	m_homeSceneReady = true;
	m_homeSceneError.clear();
	emit homeSceneReady();
#endif

	setUiScale(Config.UIScale);
}

bool MainWindow::isHomeSceneReady() const
{
	return m_homeSceneReady;
}

bool MainWindow::hasHomeSceneError() const
{
	return !m_homeSceneError.isEmpty();
}

QString MainWindow::homeSceneError() const
{
	return m_homeSceneError;
}

QQuickItem *MainWindow::homeSceneRootObject() const
{
#if QSAN_ENABLE_QML
	return homeWidget ? homeWidget->rootObject()
		: homeWindow ? homeWindow->rootObject() : nullptr;
#else
	return nullptr;
#endif
}

QUrl MainWindow::homeSceneSource() const
{
#if QSAN_ENABLE_QML
	return homeWidget ? homeWidget->source()
		: homeWindow ? homeWindow->source() : QUrl();
#else
	return QUrl();
#endif
}

QString MainWindow::homeRenderHostName() const
{
	return m_homeRenderHost;
}

HomeController *MainWindow::homeSceneController() const
{
	return homeController;
}

void MainWindow::reloadHomePage()
{
#if QSAN_ENABLE_QML
	if (pageStack->currentWidget() != homePageWidget)
		return;

	setHomeSceneSource(QUrl());
	setHomeSceneSource(QUrl(QStringLiteral("qrc:/QSanguosha/Home/HomeScene.qml")));
	setUiScale(Config.UIScale);
	focusHomeScene();
#else
	changeBackground();
	refitScene();
#endif
}

void MainWindow::setupLocalLoadingPage()
{
	localLoadingPage = new QWidget(pageStack);
	localLoadingPage->setObjectName(QStringLiteral("localRoomLoadingPage"));
	localLoadingPage->setStyleSheet(QStringLiteral(
		"#localRoomLoadingPage { background: #0B1A2E; color: #E7F1FF; }"
		"QLabel { color: #E7F1FF; }"));

	QVBoxLayout *layout = new QVBoxLayout(localLoadingPage);
	layout->setContentsMargins(48, 48, 48, 48);
	layout->addStretch();

	QLabel *title = new QLabel(tr("Preparing local game"), localLoadingPage);
	QFont titleFont = title->font();
	titleFont.setPointSize(qMax(18, titleFont.pointSize() + 8));
	titleFont.setBold(true);
	title->setFont(titleFont);
	title->setAlignment(Qt::AlignCenter);
	layout->addWidget(title);

	localLoadingStatus = new QLabel(localLoadingPage);
	localLoadingStatus->setAlignment(Qt::AlignCenter);
	localLoadingStatus->setWordWrap(true);
	layout->addSpacing(16);
	layout->addWidget(localLoadingStatus);

	localLoadingProgress = new QProgressBar(localLoadingPage);
	localLoadingProgress->setRange(0, 0);
	localLoadingProgress->setTextVisible(false);
	localLoadingProgress->setMaximumWidth(420);
	layout->addSpacing(12);
	layout->addWidget(localLoadingProgress, 0, Qt::AlignHCenter);
	layout->addStretch();

	pageStack->addWidget(localLoadingPage);
}

void MainWindow::showLocalLoadingPage(const QString &status)
{
	if (localLoadingStatus)
		localLoadingStatus->setText(status);
	if (localLoadingProgress)
		localLoadingProgress->show();
	menuBar()->hide();
	pageStack->setCurrentWidget(localLoadingPage);
#if QSAN_ENABLE_QML
	if (m_pointerOverlay)
		m_pointerOverlay->setPageEnabled(false);
#endif
}

#if QSAN_ENABLE_QML
QQmlContext *MainWindow::homeRootContext() const
{
	return homeWidget ? homeWidget->rootContext()
		: homeWindow ? homeWindow->rootContext() : nullptr;
}

QQmlEngine *MainWindow::homeQmlEngine() const
{
	return homeWidget ? homeWidget->engine()
		: homeWindow ? homeWindow->engine() : nullptr;
}

QStringList MainWindow::homeQmlErrors() const
{
	QStringList errorTexts;
	const QList<QQmlError> errors = homeWidget ? homeWidget->errors()
		: homeWindow ? homeWindow->errors() : QList<QQmlError>();
	for (const QQmlError &error : errors) {
		qCritical().noquote() << error.toString();
		errorTexts << error.toString();
	}
	return errorTexts;
}

void MainWindow::setHomeSceneSource(const QUrl &source)
{
	if (homeWidget)
		homeWidget->setSource(source);
	else if (homeWindow)
		homeWindow->setSource(source);
}

void MainWindow::setHomeSceneClearColor(const QColor &color)
{
	if (homeWidget)
		homeWidget->setClearColor(color);
	else if (homeWindow)
		homeWindow->setColor(color);
}

void MainWindow::focusHomeScene()
{
	if (homePageWidget)
		homePageWidget->setFocus();
	if (homeWindow)
		homeWindow->requestActivate();
	if (QQuickItem *root = homeSceneRootObject())
		root->forceActiveFocus();
}

void MainWindow::updateHomeSceneLoadState(HomeSceneLoadState state)
{
	qInfo().noquote() << "Home QML status:" << static_cast<int>(state)
		<< "host:" << m_homeRenderHost;
	if (state == HomeSceneLoadState::Null || state == HomeSceneLoadState::Loading) {
		// reloadHomePage() clears the source first. Reloading is neither ready nor failed.
		m_homeSceneReady = false;
		return;
	}
	if (state == HomeSceneLoadState::Ready) {
		m_homeSceneError.clear();
		m_homeSceneReady = homeSceneRootObject() != nullptr;
		if (m_homeSceneReady)
			emit homeSceneReady();
		else {
			m_homeSceneError =
				QStringLiteral("HomeScene reported Ready without a QML root object");
			emit homeSceneFailed(m_homeSceneError);
		}
		return;
	}

	const QStringList errorTexts = homeQmlErrors();
	m_homeSceneReady = false;
	m_homeSceneError = errorTexts.isEmpty()
		? QStringLiteral("HomeScene failed to load (no QQmlError reported)")
		: errorTexts.join(QLatin1Char('\n'));
	emit homeSceneFailed(m_homeSceneError);
}
#endif

void MainWindow::showHomePage()
{
	ServerInfo.DuringGame = false;
	delete systray;
	systray = nullptr;

	if (server) {
		server->deleteLater();
		server = nullptr;
	}
	if (Self) {
		Self->deleteLater();
		Self = nullptr;
		setEngineSelf(nullptr);
	}

	ui->actionStart_Game->setEnabled(true);
	ui->actionStart_Server->setEnabled(true);
	ui->actionReplay->setEnabled(true);
	ui->actionRestart_Game->setEnabled(false);
	ui->actionReturn_to_Main_Menu->setEnabled(false);

	ui->menuCheat->setEnabled(false);
	ui->actionDeath_note->disconnect();
	ui->actionDamage_maker->disconnect();
	ui->actionRevive_wand->disconnect();
	ui->actionSend_lowlevel_command->disconnect();
	ui->actionExecute_script_at_server_side->disconnect();
	ui->actionState_editor->disconnect();

	addAction(ui->actionShow_Hide_Menu);
	addAction(ui->actionFullscreen);

	ui->actionView_Discarded->setEnabled(false);
	ui->actionView_distance->setEnabled(false);
	ui->actionView_Maxcards->setEnabled(false);
	ui->actionServerInformation->setEnabled(false);
	ui->actionSurrender->setEnabled(false);
	ui->actionNever_nullify_my_trick->setEnabled(false);
	ui->actionSaveRecord->setEnabled(false);
	ui->actionPause_Resume->setEnabled(false);
	ui->actionHide_Show_chat_box->setEnabled(false);

	if (scene) {
		scene->deleteLater();
		scene = nullptr;
	}
	gameView->setScene(nullptr);

	menuBar()->hide();
#if QSAN_ENABLE_QML
	homeController->refreshCharacterImage();
	homeController->refreshPlayerInfo();
	pageStack->setCurrentWidget(homePageWidget);
	focusHomeScene();
	if (m_pointerOverlay)
		m_pointerOverlay->setPageEnabled(false);
#else
	StartScene *startScene = new StartScene;
	startScene->addButton(ui->actionStart_Game);
	startScene->addButton(ui->actionStart_Server);
	startScene->addButton(ui->actionReplay);
	startScene->addButton(ui->actionConfigure);
	startScene->addButton(ui->actionGeneral_Overview);
	startScene->addButton(ui->actionCard_Overview);
	startScene->addButton(ui->actionScenario_Overview);
	startScene->addButton(ui->actionAbout);
	scene = startScene;
	gameView->setScene(scene);
	gameView->refit();
	pageStack->setCurrentWidget(gameView);
	gameView->setFocus();
#endif

	if (ClientInstance) {
		ClientInstance->disconnectFromHost();
		delete ClientInstance;
		ClientInstance = nullptr;
	}
	if (Config.FrontBGMVolume > 0 && QFile::exists("audio/system/BGM/front-bgm.ogg")) {
#ifdef AUDIO_SUPPORT
		Audio::playBGM("audio/system/BGM/front-bgm.ogg");
		Audio::setBGMVolume(Config.FrontBGMVolume);
#endif
	}
}

void MainWindow::showGamePage(QGraphicsScene *newScene)
{
	if (scene && scene != newScene)
		scene->deleteLater();

	scene = newScene;

	menuBar()->show();
	gameView->setScene(scene);
	gameView->refit();

	pageStack->setCurrentWidget(gameView);
#if QSAN_ENABLE_QML
	if (m_pointerOverlay)
		m_pointerOverlay->setPageEnabled(true);
#endif
}

void MainWindow::restoreFromConfig()
{
	resize(Config.value("WindowSize", QSize(1366, 706)).toSize());
	move(Config.value("WindowPosition", QPoint(-8, -8)).toPoint());
	Qt::WindowStates window_state = (Qt::WindowStates)Config.value("WindowState").toInt();
	if (window_state != Qt::WindowMinimized)
		setWindowState(window_state);

	QFont font;
	if (UiConfig.UIFont != font)
		QApplication::setFont(UiConfig.UIFont, "QTextEdit");

	ui->actionEnable_Hotkey->setChecked(Config.EnableHotKey);
	ui->actionNever_nullify_my_trick->setChecked(Config.NeverNullifyMyTrick);
	ui->actionNever_nullify_my_trick->setEnabled(false);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	// 主視窗被關 = 正常退出。此後退出清理階段(Engine 析構、Lua 關閉、
	// __gc 終結器經 SWIG 回調 C++ 物件)出的崩潰不再上報 —— 玩家已主動退出。
	CrashHandler::beginShutdown();

	Config.setValue("WindowSize", size());
	Config.setValue("WindowPosition", pos());
	Config.setValue("WindowState", (int)windowState());

	QMainWindow::closeEvent(event);
	qApp->quit();
}

// 把當前主視窗幾何與所在螢幕登記給 crash handler,崩潰摘要裡用得到。
static void reportWindowState(QWidget *w)
{
	QRect g = w->geometry();
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
	QScreen *scr = QGuiApplication::screenAt(g.center());
#else
	QScreen *scr = Q_NULLPTR;
	const QList<QScreen *> screens = QGuiApplication::screens();
	foreach (QScreen *candidate, screens) {
		if (candidate->geometry().contains(g.center())) {
			scr = candidate;
			break;
		}
	}
	if (scr == Q_NULLPTR)
		scr = QGuiApplication::primaryScreen();
#endif
	QString name = scr ? scr->name() : QString();
	CrashHandler::setWindowState(g.x(), g.y(), g.width(), g.height(),
		(const wchar_t *)name.utf16());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	reportWindowState(this);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
	QMainWindow::moveEvent(event);
	reportWindowState(this);
}

MainWindow::~MainWindow()
{
	delete ui;
	gameView->deleteLater();
	if (scene) scene->deleteLater();
	QSanSkinFactory::destroyInstance();
}

void MainWindow::gotoScene(QGraphicsScene *newScene)
{
	fprintf(stderr, "gotoScene is deprecated, use showGamePage\n");
	showGamePage(newScene);
}

void MainWindow::refitScene()
{
	if (gameView)
		gameView->refit();
}

void MainWindow::setUiScale(qreal scale)
{
	if (gameView)
		gameView->setUiScale(scale);
#if QSAN_ENABLE_QML
	if (QQuickItem *root = homeSceneRootObject())
		root->setProperty("uiScale", scale);
#endif
}

void MainWindow::on_actionExit_triggered()
{
	QMessageBox::StandardButton result;
	result = QMessageBox::question(this,
		tr("Sanguosha"),
		tr("Are you sure to exit?"),
		QMessageBox::Ok | QMessageBox::Cancel);
	if (result == QMessageBox::Ok) {
		delete systray;
		systray = nullptr;
		close();
	}
}

void MainWindow::on_actionStart_Server_triggered()
{
	static ServerDialog *dialog = new ServerDialog(this);
	int accept_type = dialog->config();
	if (accept_type == 0)
		return;

	server = new Server(this);
	if (!server->listen()) {
		QMessageBox::warning(this, tr("Warning"), tr("Can not start server!"));
		return;
	}

	server->checkUpnpAndListServer();

	if (accept_type == 1) {
		server->daemonize();

		ui->actionStart_Game->disconnect();
		connect(ui->actionStart_Game, SIGNAL(triggered()), this, SLOT(startGameInAnotherInstance()));

		StartScene *start_scene = new StartScene;
		start_scene->switchToServer(server);
		showGamePage(start_scene);

		if (Config.value("EnableMinimizeDialog").toBool())
			on_actionMinimize_to_system_tray_triggered();
	} else {
		Config.HostAddress = "127.0.0.1";
		startConnectionWithReconnect(false);
	}
}

void MainWindow::startLocalConsoleGame()
{
	if (server) {
		server->deleteLater();
		server = nullptr;
	}

	showLocalLoadingPage(tr("Initializing local rules and AI..."));
	Server *pendingServer = new Server(this, GameSessionConfig(),
		Server::InitialRoomPolicy::Deferred);
	server = pendingServer;
	connect(pendingServer, &Server::initialRoomReady, this,
		[this, pendingServer]() {
			if (server == pendingServer)
				completeLocalRoomStart();
		});
	connect(pendingServer, &Server::initialRoomFailed, this,
		[this, pendingServer](const QString &error) {
			if (server == pendingServer)
				failLocalRoomStart(error);
		});

	QString error;
	if (!pendingServer->prepareInitialRoomAsync(&error))
		failLocalRoomStart(error);
}

void MainWindow::completeLocalRoomStart()
{
	if (!server)
		return;
	showLocalLoadingPage(tr("Starting local server..."));
	if (!server->listen()) {
		failLocalRoomStart(tr("Can not start server!"));
		return;
	}

	server->checkUpnpAndListServer();
	Config.HostAddress = QStringLiteral("127.0.0.1");
	showLocalLoadingPage(tr("Connecting to local room..."));
	QTimer::singleShot(0, this, [this]() {
		if (server)
			startConnectionWithReconnect(false);
	});
}

void MainWindow::failLocalRoomStart(const QString &error)
{
	Server *failedServer = server;
	server = nullptr;
	if (failedServer)
		failedServer->deleteLater();
	showHomePage();
	QMessageBox::warning(this, tr("Warning"), error.isEmpty()
		? tr("Can not prepare local room!") : error);
}

bool MainWindow::preflightTakeover(const QString &snapshotPath,
	const QString &seatName, QString *error) const
{
	const auto fail = [error](const QString &message) {
		if (error)
			*error = message;
		return false;
	};

	if (snapshotPath.isEmpty() || seatName.isEmpty())
		return fail(tr("Takeover requires a snapshot and a seat"));

	if (!m_replayRestoreState.valid || m_replayRestoreState.path.endsWith(
		QStringLiteral(".png"), Qt::CaseInsensitive)) {
		return fail(tr("Takeover is available only for a text replay with snapshots"));
	}

	const QFileInfo snapshotInfo(snapshotPath);
	if (!snapshotInfo.isFile())
		return fail(tr("Snapshot file does not exist"));

	// The manifest is the pairing boundary between a replay and its snapshots.
	// Replayer performs the hash/schema verification; checking its presence here
	// prevents a direct path from accidentally bypassing that contract.
	const QString snapshotDir = GameSnapshot::getSnapshotDir(m_replayRestoreState.path);
	if (QDir::cleanPath(snapshotInfo.absolutePath()) != QDir::cleanPath(snapshotDir))
		return fail(tr("Snapshot is not in the selected replay's snapshot directory"));
	const QFileInfo manifestInfo(snapshotDir + QLatin1String("/manifest.json"));
	if (!manifestInfo.isFile())
		return fail(tr("Replay snapshot manifest is missing"));

	QFile manifestFile(manifestInfo.absoluteFilePath());
	if (!manifestFile.open(QIODevice::ReadOnly))
		return fail(tr("Replay snapshot manifest cannot be opened"));
	QJsonParseError parseError;
	const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !manifest.isObject())
		return fail(tr("Replay snapshot manifest is invalid"));
	const QJsonObject manifestObject = manifest.object();
	if (manifestObject.value(QStringLiteral("schema")).toString()
		!= QStringLiteral("qsanguosha-takeover-manifest-v1")) {
		return fail(tr("Replay snapshot manifest schema is unsupported"));
	}
	if (manifestObject.value(QStringLiteral("sessionId")).toString().isEmpty()
		|| !manifestObject.value(QStringLiteral("snapshots")).isArray())
		return fail(tr("Replay snapshot manifest is incomplete"));

	QFile replayFile(m_replayRestoreState.path);
	if (!replayFile.open(QIODevice::ReadOnly))
		return fail(tr("The source replay cannot be opened"));
	const QByteArray replayHash = QCryptographicHash::hash(
		replayFile.readAll(), QCryptographicHash::Sha256).toHex();
	if (QString::fromLatin1(replayHash)
		!= manifestObject.value(QStringLiteral("replaySha256")).toString()) {
		return fail(tr("Replay and snapshot manifest do not match"));
	}

	QByteArray snapshotBytes;
	QFile snapshotHashFile(snapshotInfo.absoluteFilePath());
	if (snapshotHashFile.open(QIODevice::ReadOnly))
		snapshotBytes = snapshotHashFile.readAll();
	else
		return fail(tr("Snapshot cannot be opened"));
	const QString snapshotHash = QString::fromLatin1(
		QCryptographicHash::hash(snapshotBytes, QCryptographicHash::Sha256).toHex());
	bool manifestEntryFound = false;
	QString manifestTurnSerial;
	QString manifestPlayerName;
	int manifestPlayerTurnCount = 0;
	for (const QJsonValue &entryValue : manifestObject.value(QStringLiteral("snapshots")).toArray()) {
		const QJsonObject entry = entryValue.toObject();
		if (entry.value(QStringLiteral("file")).toString() != snapshotInfo.fileName())
			continue;
		if (manifestEntryFound)
			return fail(tr("Snapshot is listed more than once in the manifest"));
		manifestEntryFound = true;
		if (entry.value(QStringLiteral("sha256")).toString() != snapshotHash)
			return fail(tr("Snapshot and manifest do not match"));
		manifestTurnSerial = entry.value(QStringLiteral("turnSerial")).toString();
		manifestPlayerName = entry.value(QStringLiteral("playerName")).toString();
		manifestPlayerTurnCount = entry.value(
			QStringLiteral("playerTurnCount")).toInt(0);
	}
	if (!manifestEntryFound)
		return fail(tr("Snapshot is not listed in the replay manifest"));

	GameSnapshot snapshot(snapshotPath);
	if (!snapshot.isEligible())
		return fail(snapshot.getError().isEmpty()
			? tr("This snapshot is not eligible for takeover")
			: snapshot.getError());

	const GlobalSnapshot state = snapshot.getState();
	int expectedPlayerTurnCount = 1;
	bool currentPlayerFound = false;
	for (const PlayerSnapshot &player : state.players) {
		if (player.objectName != state.currentPlayer)
			continue;
		expectedPlayerTurnCount = player.marks.value(
			QStringLiteral("Global_TurnCount"), 0) + 1;
		currentPlayerFound = true;
		break;
	}
	if (manifestTurnSerial != QString::number(snapshot.getTurnSerial())
		|| !currentPlayerFound || manifestPlayerName != state.currentPlayer
		|| manifestPlayerTurnCount != expectedPlayerTurnCount)
		return fail(tr("Snapshot timeline identity does not match the manifest"));
	if (!state.unsupportedState.isEmpty() || state.players.isEmpty())
		return fail(tr("Snapshot contains unsupported or incomplete state"));
	if (state.currentPlayer.isEmpty() || !state.seatOrder.contains(state.currentPlayer))
		return fail(tr("Snapshot has no valid current player"));

	QString compatibilityError;
	if (!GameSnapshot::validateRuntimeCompatibility(state, &compatibilityError))
		return fail(compatibilityError);

	const auto playerIt = std::find_if(state.players.cbegin(), state.players.cend(),
		[&seatName](const PlayerSnapshot &player) {
			return player.objectName == seatName;
		});
	if (playerIt == state.players.cend())
		return fail(tr("Selected seat is not present in the snapshot"));
	if (!playerIt->alive)
		return fail(tr("A dead seat cannot be selected for takeover"));

	return true;
}

bool MainWindow::stopReplayForTakeover(Replayer *replayer, QString *error) const
{
	if (replayer == nullptr)
		return true;

	if (!replayer->stopAndWait(5000)) {
		if (error)
			*error = tr("Replay worker could not be stopped safely");
		return false;
	}
	return true;
}

void MainWindow::startTakeoverGame(const QString &snapshotPath, const QString &seatName)
{
	Client *oldClient = ClientInstance;
	Replayer *oldReplayer = oldClient ? oldClient->getReplayer() : nullptr;
	if (oldReplayer == nullptr || !oldReplayer->isValid()) {
		QMessageBox::warning(this, tr("Takeover"),
			tr("Takeover can only be started from a valid replay"));
		return;
	}

	ReplayRestoreState restore;
	restore.path = oldReplayer->getPath();
	restore.pairIndex = oldReplayer->getCurrentPairIndex();
	restore.perspective = Self ? Self->objectName() : QString();
	restore.previousGameMode = Config.GameMode;
	restore.wasPaused = true;
	restore.valid = true;

	restore.wasPaused = !oldReplayer->isPlaying();

	m_replayRestoreState = restore;
	QString error;
	if (!preflightTakeover(snapshotPath, seatName, &error)) {
		QMessageBox::warning(this, tr("Takeover"), error);
		m_replayRestoreState = ReplayRestoreState();
		return;
	}

	if (!stopReplayForTakeover(oldReplayer, &error)) {
		QMessageBox::warning(this, tr("Takeover"), error);
		return;
	}

	// Teardown happens only after preflight and replay-worker quiescence.  The
	// saved restore state is retained until the new branch has really started.
	oldClient->disconnectFromHost();
	delete oldClient;

	m_takeoverInProgress = true;
	m_takeoverGameStarted = false;
	GameSnapshot selectedSnapshot(snapshotPath);
	const GlobalSnapshot state = selectedSnapshot.getState();
	Config.GameMode = Sanguosha->getGameMode(state.gameMode);
	GameSessionConfig sessionConfig;
	sessionConfig.takeover = true;
	sessionConfig.takeoverSnapshotPath = snapshotPath;
	sessionConfig.takeoverSeatName = seatName;
	bool seedOk = false;
	if (!state.gameplayRng.seed.isEmpty()) {
		const quint64 seed = state.gameplayRng.seed.toULongLong(&seedOk);
		if (seedOk)
			sessionConfig.seed = seed;
	}
	server = new Server(this, sessionConfig);
	connect(server, &Server::takeoverReady, this, [this]() {
		m_takeoverGameStarted = true;
		m_takeoverInProgress = false;
		m_replayRestoreState = ReplayRestoreState();
	});
	connect(server, &Server::takeoverFailed,
		this, &MainWindow::rollbackTakeover);

	if (!server->listen()) {
		rollbackTakeover(tr("Can not start takeover server"));
		return;
	}
	server->checkUpnpAndListServer();
	Config.HostAddress = QStringLiteral("127.0.0.1");
	startConnectionWithReconnect(false);
	QTimer::singleShot(15000, this, [this]() {
		if (m_takeoverInProgress)
			rollbackTakeover(tr("Takeover session did not become ready in time"));
	});
}

void MainWindow::rollbackTakeover(const QString &reason)
{
	if (!reason.isEmpty())
		qWarning().noquote() << "Takeover failed:" << reason;

	const ReplayRestoreState restore = m_replayRestoreState;
	m_takeoverInProgress = false;
	m_takeoverGameStarted = false;
	if (restore.valid && restore.previousGameMode.isValid())
		Config.GameMode = restore.previousGameMode;

	if (ClientInstance) {
		ClientInstance->disconnectFromHost();
		delete ClientInstance;
		ClientInstance = nullptr;
	}
	if (server) {
		delete server;
		server = nullptr;
	}

	showHomePage();
	if (restore.valid)
		reopenReplay(restore);
	else if (!reason.isEmpty())
		QMessageBox::warning(this, tr("Takeover"), reason);
}

void MainWindow::reopenReplay(const ReplayRestoreState &state)
{
	if (state.path.isEmpty())
		return;

	Client *client = new Client(this, state.path);
	Replayer *replayer = client->getReplayer();
	if (replayer == nullptr || !replayer->isValid()) {
		const QString detail = replayer ? replayer->errorString()
			: tr("Replay loader is unavailable");
		delete client;
		QMessageBox::warning(this, tr("Replay error"), detail);
		return;
	}

	QMetaObject::Connection *restoreConnection = new QMetaObject::Connection;
	*restoreConnection = connect(client, &Client::server_connected, this,
		[this, client, state, restoreConnection]() {
		enterRoom();
		// The setup notification has already materialized the replay players.
		// Disconnect before seeking so replay setup does not create a second UI.
		disconnect(*restoreConnection);
		delete restoreConnection;
		QTimer::singleShot(0, this, [this, state]() {
			applyReplayRestoreState(state);
		});
	});
	client->signup();
}

void MainWindow::applyReplayRestoreState(const ReplayRestoreState &state)
{
	if (!ClientInstance || !ClientInstance->getReplayer())
		return;

	Replayer *replayer = ClientInstance->getReplayer();
	if (state.pairIndex > 0)
		replayer->seekToPosition(state.pairIndex);

	if (!state.perspective.isEmpty()) {
		ClientPlayer *target = ClientInstance->getPlayer(state.perspective);
		if (target)
			ClientInstance->setSelf(target);
	}

	if (state.wasPaused == replayer->isPlaying())
		replayer->toggle();
}

void MainWindow::checkVersion(const QString &server_version, const QString &server_mod, int card_num)
{
	// 自動化測試: 略過 MOD/卡牌數/版本檢查, 直接 signup (server/client 為不同 target, 載入套件數可能不同)
	const bool autotest = !m_takeoverInProgress
		&& (Config.AutoAddRobots || !Config.AutoPickGeneral.isEmpty());
	if (autotest) {
		QFile diag("client_autotest_diag.log");
		if (diag.open(QIODevice::Append | QIODevice::Text)) {
			QTextStream(&diag) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
				<< " checkVersion(autotest): server_mod='" << server_mod
				<< "' card_num=" << card_num
				<< " local_card=" << Sanguosha->getCardCount()
				<< " server_ver='" << server_version << "'\n";
		}
		Client *client = qobject_cast<Client *>(sender());
		if (client) {
			client->signup();
			connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));
		}
		return;
	}

	if (Sanguosha->getMODName() != server_mod) {
		if (m_takeoverInProgress) {
			rollbackTakeover(tr("Takeover server MOD does not match the client"));
			return;
		}
		QMessageBox::warning(this, tr("Warning"), tr("Client MOD name is not same as the server!"));
		return;
	}

	if (Sanguosha->getCardCount() != card_num) {
		if (m_takeoverInProgress) {
			rollbackTakeover(tr("Takeover server card catalog does not match the client"));
			return;
		}
		QMessageBox::warning(this, tr("Warning"), "你与服务器的卡牌数或将包数不同，无法加入游戏！");
		return;
	}

	Client *client = qobject_cast<Client *>(sender());
	QString client_version = Sanguosha->getVersionNumber();

	if (server_version == client_version) {
		client->signup();
		connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));
		return;
	}

	client->disconnectFromHost();
	if (m_takeoverInProgress) {
		rollbackTakeover(tr("Takeover server and client versions do not match"));
		return;
	}

	QString text = tr("Server version is %1, client version is %2 <br/>").arg(server_version).arg(client_version);
	if (server_version > client_version)
		text.append(tr("Your client version is older than the server's, please update it <br/>"));
	else
		text.append(tr("The server version is older than your client version, please ask the server to update<br/>"));

	static QString link = "https://gitee.com/L-T-Y/QSanguosha-v2";

	text.append(tr("Download link : <a href='%1'>%1</a> <br/>").arg(link));
	QMessageBox::warning(this, tr("Warning"), text);
}

void MainWindow::startConnection()
{
	startConnectionWithReconnect(Config.value("EnableReconnection", false).toBool());
}

void MainWindow::startConnectionWithReconnect(bool reconnectRequested)
{
	// A newly created in-process server has no reconnect target; local callers
	// explicitly pass false while external connections retain the saved option.
	Client *client = new Client(this, QString(), nullptr, m_takeoverInProgress,
		reconnectRequested);

	connect(client, SIGNAL(version_checked(QString, QString, int)), SLOT(checkVersion(QString, QString, int)));
	connect(client, SIGNAL(error_message(QString)), SLOT(networkError(QString)));
}

void MainWindow::on_actionReplay_triggered()
{
	QString location = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
	QString last_dir = Config.value("LastReplayDir").toString();
	if (!last_dir.isEmpty())
		location = last_dir;

	QString filename = QFileDialog::getOpenFileName(this,
		tr("Select a reply file"),
		location,
		tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

	if (filename.isEmpty())
		return;

	QFileInfo file_info(filename);
	last_dir = file_info.absoluteDir().path();
	Config.setValue("LastReplayDir", last_dir);

	Client *client = new Client(this, filename);
	Replayer *replayer = client->getReplayer();
	if (replayer == nullptr || !replayer->isValid()) {
		const QString detail = replayer != nullptr
			? replayer->errorString() : tr("Replay loader is unavailable");
		QMessageBox::warning(this, tr("Replay error"), detail);
		delete client;
		return;
	}
	connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));
	client->signup();
}

void MainWindow::networkError(const QString &error_msg)
{
	if (m_takeoverInProgress) {
		rollbackTakeover(error_msg);
		return;
	}
	if (isVisible())
		QMessageBox::warning(this, tr("Network error"), error_msg);
}

void BackLoader::preload()
{
	foreach (QString emotion, G_ROOM_SKIN.getAnimationFileNames()) {
		for (int i = 0; i < PixmapAnimation::GetFrameCount(emotion); i++)
			G_ROOM_SKIN.getPixmapFromFileName(QString("image/system/emotion/%1/%2.png").arg(emotion).arg(i), true);
	}
}

void MainWindow::enterRoom()
{
	if (!Config.HistoryIPs.contains(Config.HostAddress)) {
		Config.HistoryIPs << Config.HostAddress;
		Config.HistoryIPs.sort();
		Config.setValue("HistoryIPs", Config.HistoryIPs);
	}

	ui->actionStart_Game->setEnabled(false);
	ui->actionStart_Server->setEnabled(false);
	ui->actionReplay->setEnabled(false);
	ui->actionRestart_Game->setEnabled(false);
	ui->actionReturn_to_Main_Menu->setEnabled(false);

	RoomScene *room_scene = new RoomScene(this);
	ui->actionView_Discarded->setEnabled(true);
	ui->actionView_distance->setEnabled(true);
	ui->actionView_Maxcards->setEnabled(true);
	ui->actionServerInformation->setEnabled(true);
	ui->actionSurrender->setEnabled(true);
	ui->actionNever_nullify_my_trick->setEnabled(true);
	ui->actionSaveRecord->setEnabled(true);
	ui->actionPause_Resume->setEnabled(true);
	ui->actionHide_Show_chat_box->setEnabled(true);

	connect(ClientInstance, SIGNAL(surrender_enabled(bool)), ui->actionSurrender, SLOT(setEnabled(bool)));

	connect(ui->actionView_Discarded, SIGNAL(triggered()), room_scene, SLOT(toggleDiscards()));
	connect(ui->actionView_distance, SIGNAL(triggered()), room_scene, SLOT(viewDistance()));
	connect(ui->actionView_Maxcards, SIGNAL(triggered()), room_scene, SLOT(viewMaxCards()));
	connect(ui->actionServerInformation, SIGNAL(triggered()), room_scene, SLOT(showServerInformation()));
	connect(ui->actionSurrender, SIGNAL(triggered()), room_scene, SLOT(surrender()));
	connect(ui->actionSaveRecord, SIGNAL(triggered()), room_scene, SLOT(saveReplayRecord()));
	connect(ui->actionPause_Resume, SIGNAL(triggered()), room_scene, SLOT(pause()));
	connect(ui->actionHide_Show_chat_box, SIGNAL(triggered()), room_scene, SLOT(setChatBoxVisibleSlot()));

	if (ServerInfo.EnableCheat) {
		ui->menuCheat->setEnabled(true);

		connect(ui->actionDeath_note, SIGNAL(triggered()), room_scene, SLOT(makeKilling()));
		connect(ui->actionDamage_maker, SIGNAL(triggered()), room_scene, SLOT(makeDamage()));
		connect(ui->actionRevive_wand, SIGNAL(triggered()), room_scene, SLOT(makeReviving()));
		connect(ui->actionExecute_script_at_server_side, SIGNAL(triggered()), room_scene, SLOT(doScript()));
		connect(ui->actionState_editor, SIGNAL(triggered()), room_scene, SLOT(changeState()));
	} else {
		ui->menuCheat->setEnabled(false);
		ui->actionDeath_note->disconnect();
		ui->actionDamage_maker->disconnect();
		ui->actionRevive_wand->disconnect();
		ui->actionSend_lowlevel_command->disconnect();
		ui->actionExecute_script_at_server_side->disconnect();
		ui->actionState_editor->disconnect();
	}

	connect(room_scene, SIGNAL(restart()), this, SLOT(startConnection()));
	connect(room_scene, SIGNAL(return_to_start()), this, SLOT(showHomePage()));
	connect(room_scene, SIGNAL(game_over_dialog_rejected()), this, SLOT(enableDialogButtons()));
	connect(room_scene, &RoomScene::takeoverRequested,
		this, &MainWindow::startTakeoverGame);

	showGamePage(room_scene);

	// 自動化測試: --auto-robots 由 owner 自動填滿 AI (填滿後伺服器端自動開局)
	if (Config.AutoAddRobots || m_takeoverInProgress) {
		const bool takeoverRobotFill = m_takeoverInProgress;
		QFile diag("client_autotest_diag.log");
		if (Config.AutoAddRobots && diag.open(QIODevice::Append | QIODevice::Text)) {
			QTextStream(&diag) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
				<< " enterRoom: AutoAddRobots on, players=" << ClientInstance->getPlayers().length() << "\n";
		}
		QTimer *autoRobotTimer = new QTimer(room_scene);
		autoRobotTimer->setInterval(300);
		QObject::connect(autoRobotTimer, &QTimer::timeout, room_scene,
			[this, autoRobotTimer, takeoverRobotFill]() {
			if (!ClientInstance || (takeoverRobotFill && !m_takeoverInProgress)) {
				autoRobotTimer->stop();
				autoRobotTimer->deleteLater();
				return;
			}
			bool anyOwner = false;
			foreach (const ClientPlayer *p, ClientInstance->getPlayers()) {
				if (p->isOwner()) {
					anyOwner = true;
					break;
				}
			}
			QFile diag("client_autotest_diag.log");
			if (Config.AutoAddRobots && diag.open(QIODevice::Append | QIODevice::Text)) {
				QTextStream(&diag) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
					<< " tick: players=" << ClientInstance->getPlayers().length()
					<< " anyOwner=" << anyOwner << "\n";
			}
			if (anyOwner) {
				ClientInstance->addRobot(-1);
				autoRobotTimer->stop();
				autoRobotTimer->deleteLater();
			}
		});
		QTimer::singleShot(15000, autoRobotTimer, &QTimer::stop);
		autoRobotTimer->start();
	}

	emit roomSceneCreated(room_scene);
}

void MainWindow::gotoStartScene()
{
	fprintf(stderr, "gotoStartScene is deprecated, use showHomePage\n");
	showHomePage();
}

void MainWindow::enableDialogButtons()
{
	ui->actionRestart_Game->setEnabled(true);
	ui->actionReturn_to_Main_Menu->setEnabled(true);
}

void MainWindow::startGameInAnotherInstance()
{
	QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
}

void MainWindow::on_actionGeneral_Overview_triggered()
{
	GeneralOverview *overview = GeneralOverview::getInstance(this);
	overview->fillGenerals(Sanguosha->getAllGenerals());
	overview->show();
}

void MainWindow::on_actionCard_Overview_triggered()
{
	CardOverview *overview = CardOverview::getInstance(this);
	overview->loadFromAll();
	overview->show();
}

void MainWindow::on_actionEnable_Hotkey_toggled(bool checked)
{
	if (Config.EnableHotKey != checked) {
		Config.EnableHotKey = checked;
		Config.setValue("EnableHotKey", checked);
	}
}

void MainWindow::on_actionNever_nullify_my_trick_toggled(bool checked)
{
	if (Config.NeverNullifyMyTrick != checked) {
		Config.NeverNullifyMyTrick = checked;
		Config.setValue("NeverNullifyMyTrick", checked);
	}
}

void MainWindow::on_actionAbout_triggered()
{
	if (!scene) {
		QMessageBox::about(this, tr("About QSanguosha"),
			tr("QSanguosha %1").arg(Sanguosha->getVersion()));
		return;
	}

	QString content = "<center><img src='image/system/shencc.png'></center>";

	QString poem = tr("Disciples dressed in blue, my heart worries for you. You are the cause, of this song without pause <br/>"
		"\"A Short Song\" by Cao Cao");
	content.append(QString("<p align='right'><i>%1</i></p>").arg(poem));

	content.append(QString("<p align='right'><i>%1</i></p>").arg(tr("\"A Short Song\" by Cao Cao")));
	content.append(tr("QSanguosha to gamerule")+"<br/>");

	content.append(tr("This is the open source clone of the popular <b>Sanguosha</b> game,"
		"totally written in C++ Qt GUI framework <br/>"
		"My Email: <a href='mailto:%1' style = \"color:#0072c1; \">%1</a> <br/>"
		"My QQ: 365840793 <br/>"
		"My Weibo: http://weibo.com/moligaloo <br/>").arg("moligaloo@gmail.com"));

	QString config = "debug";

#ifdef QT_NO_DEBUG
	config = "release";
#endif

	content.append(tr("Current version: %1 %2 (%3)<br/>")
		.arg(Sanguosha->getVersion()).arg(config).arg(Sanguosha->getVersionName()));

	const char *date = __DATE__;
	const char *time = __TIME__;
	content.append(tr("Compilation time: %1 %2 <br/>").arg(date).arg(time));

	content.append(tr("Forum: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg("http://mogara.org"));

	content.append(tr("Source code: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg("https://gitee.com/L-T-Y/QSanguosha-v2"));

	Window *window = new Window(tr("About QSanguosha"), QSize(420, 470));
	window->setZValue(32766);
	scene->addItem(window);

	window->addContent(content);
	window->addCloseButton(tr("OK"));
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

	window->appear();
}

void MainWindow::setBackgroundBrush(bool centerAsOrigin)
{
    if (gameView)
        gameView->setBackgroundBrush(centerAsOrigin);
}

void MainWindow::changeBackground()
{
	setBackgroundBrush(scene && !scene->inherits("RoomScene"));
}

void MainWindow::on_actionFullscreen_triggered()
{
	if (isFullScreen())
		showNormal();
	else
		showFullScreen();
}

void MainWindow::on_actionShow_Hide_Menu_triggered()
{
	QMenuBar *menu_bar = menuBar();
	menu_bar->setVisible(!menu_bar->isVisible());
}

void MainWindow::on_actionMinimize_to_system_tray_triggered()
{
	if (systray == nullptr) {
		static QIcon icon("image/system/magatamas/5.png");
		systray = new QSystemTrayIcon(icon, this);

		QAction *appear = new QAction(tr("Show main window"), this);
		connect(appear, SIGNAL(triggered()), this, SLOT(show()));

		QMenu *menu = new QMenu;
		menu->addAction(appear);
		menu->addMenu(ui->menuGame);
		menu->addMenu(ui->menuView);
		menu->addMenu(ui->menuOptions);
		menu->addMenu(ui->menuHelp);

		systray->setContextMenu(menu);

		systray->show();
		systray->showMessage(windowTitle(), tr("Game is minimized"));

		hide();
	}
}

void MainWindow::on_actionRole_assign_table_triggered()
{
	if (!scene)
		return;

	QString content;

	QStringList headers;
	headers << tr("Count") << tr("Lord") << tr("Loyalist") << tr("Rebel") << tr("Renegade");
	foreach(QString header, headers)
		content += QString("<th>%1</th>").arg(header);

	content = QString("<tr>%1</tr>").arg(content);

	QStringList rows;
	rows << "2 1 0 1 0" << "3 1 0 1 1" << "4 1 0 2 1"
		<< "5 1 1 2 1" << "6 1 1 3 1" << "6d 1 1 2 2"
		<< "7 1 2 3 1" << "8 1 2 4 1" << "8d 1 2 3 2"
		<< "8z 1 3 4 0" << "9 1 3 4 1" << "10 1 3 4 2"
		<< "10z 1 4 5 0" << "10o 1 3 5 1";

	foreach (QString row, rows) {
		QStringList cells = row.split(" ");
		QString header = cells.takeFirst();
		if (header.endsWith("d")) {
			header.chop(1);
			header += tr(" (double renegade)");
		}
		if (header.endsWith("z")) {
			header.chop(1);
			header += tr(" (no renegade)");
		}
		if (header.endsWith("o")) {
			header.chop(1);
			header += tr(" (single renegade)");
		}

		QString row_content;
		row_content = QString("<td>%1</td>").arg(header);
		foreach(QString cell, cells)
			row_content += QString("<td>%1</td>").arg(cell);

		content += QString("<tr>%1</tr>").arg(row_content);
	}

	content = QString("<table border='1'>%1</table").arg(content);

	Window *window = new Window(tr("Role assign table"), QSize(240, 450));
	scene->addItem(window);

	window->addContent(content);
	window->addCloseButton(tr("OK"));
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);
	window->setZValue(32766);

	window->appear();
}

void MainWindow::on_actionScenario_Overview_triggered()
{
	static ScenarioOverview *dialog = new ScenarioOverview(this);
	dialog->show();
}

BroadcastBox::BroadcastBox(Server *server, QWidget *parent)
	: QDialog(parent), server(server)
{
	setWindowTitle(tr("Broadcast"));

	QVBoxLayout *layout = new QVBoxLayout;
	layout->addWidget(new QLabel(tr("Please input the message to broadcast")));

	text_edit = new QTextEdit;
	layout->addWidget(text_edit);

	QHBoxLayout *hlayout = new QHBoxLayout;
	hlayout->addStretch();
	QPushButton *ok_button = new QPushButton(tr("OK"));
	hlayout->addWidget(ok_button);

	layout->addLayout(hlayout);

	setLayout(layout);

	connect(ok_button, SIGNAL(clicked()), this, SLOT(accept()));
}

void BroadcastBox::accept()
{
	QDialog::accept();
	server->broadcast(text_edit->toPlainText());
}

void MainWindow::on_actionBroadcast_triggered()
{
	Server *server = findChild<Server *>();
	if (server == nullptr) {
		QMessageBox::warning(this, tr("Warning"), tr("Server is not started yet!"));
		return;
	}

	static BroadcastBox *dialog = new BroadcastBox(server, this);
	dialog->exec();
}

void MainWindow::on_actionAcknowledgement_triggered()
{
	if (!scene)
		return;

	Window *window = new Window("", QSize(1000, 677), "image/system/acknowledgement.png");
	scene->addItem(window);

	Button *button = window->addCloseButton(tr("OK"));
	button->moveBy(-85, -35);
	window->setZValue(32766);
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

	window->addContent(QString("<a style = \"color:#0072c1; \">%1</a>").arg(Sanguosha->translate("Acknowledgement")));
	window->appear();
}

void MainWindow::on_actionManage_Ban_IP_triggered()
{
	static BanIpDialog *dlg = new BanIpDialog(this, server);
	dlg->show();
}

void MainWindow::on_actionReplay_file_convert_triggered()
{
	QString filename = QFileDialog::getOpenFileName(this,
		tr("Please select a replay file"),
		Config.value("LastReplayDir").toString(),
		tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

	if (filename.isEmpty())
		return;

	QFile file(filename);
	bool success = false;
	if (file.open(QIODevice::ReadOnly)) {
		QFileInfo info(filename);
		QString tosave = info.absoluteDir().absoluteFilePath(info.baseName());
		QString suffix = filename.right(4).toLower();

		if (suffix == ".txt") {
			tosave.append(".png");

			Recorder::TXT2PNG(file.readAll()).save(tosave);
			success = true;
		} else if (suffix == ".png") {
			tosave.append(".txt");

			QByteArray data = Recorder::PNG2TXT(filename);

			QFile tosave_file(tosave);
			if (!data.isEmpty() && tosave_file.open(QIODevice::WriteOnly)) {
				tosave_file.write(data);
				success = true;
			}
		}
	}
	if (success)
		QMessageBox::warning(this, tr("Replay file convert"), tr("Conversion done!"));
	else
		QMessageBox::warning(this, tr("Replay file convert"), tr("Conversion failed!"));
}

void MainWindow::on_actionRecord_analysis_triggered()
{
	QString location = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
	QString filename = QFileDialog::getOpenFileName(this,
		tr("Load replay record"),
		location,
		tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

	if (filename.isEmpty()) return;

	QDialog *rec_dialog = new QDialog(this);
	rec_dialog->setWindowTitle(tr("Record Analysis"));
	rec_dialog->resize(800, 500);
	QTableWidget *table = new QTableWidget;

	RecAnalysis *record = new RecAnalysis(filename);
	QMap<QString, PlayerRecordStruct *> record_map = record->getRecordMap();
	table->setColumnCount(11);
	table->setRowCount(record_map.keys().length());
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);

	static QStringList labels;
	if (labels.isEmpty()) {
		labels << tr("ScreenName") << tr("General") << tr("Role") << tr("Living") << tr("WinOrLose") << tr("TurnCount")
			<< tr("Recover") << tr("Damage") << tr("Damaged") << tr("Kill") << tr("Designation");
	}
	table->setHorizontalHeaderLabels(labels);
	table->setSelectionBehavior(QTableWidget::SelectRows);

	int i = 0;
	foreach (PlayerRecordStruct *rec, record_map.values()) {
		QTableWidgetItem *item = new QTableWidgetItem;
		QString screen_name = Sanguosha->translate(rec->m_screenName);
		if (rec->m_statue == "robot")
			screen_name += "(" + Sanguosha->translate("robot") + ")";

		item->setText(screen_name);
		table->setItem(i, 0, item);

		item = new QTableWidgetItem;
		QString generals = Sanguosha->translate(rec->m_generalName);
		if (!rec->m_general2Name.isEmpty())
			generals += "/" + Sanguosha->translate(rec->m_general2Name);
		item->setText(generals);
		table->setItem(i, 1, item);

		item = new QTableWidgetItem;
		item->setText(Sanguosha->translate(rec->m_role));
		table->setItem(i, 2, item);

		item = new QTableWidgetItem;
		item->setText(rec->m_isAlive ? tr("Alive") : tr("Dead"));
		table->setItem(i, 3, item);

		item = new QTableWidgetItem;
		bool is_win = record->getRecordWinners().contains(rec->m_role)
			|| record->getRecordWinners().contains(record_map.key(rec));
		item->setText(is_win ? tr("Win") : tr("Lose"));
		table->setItem(i, 4, item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_turnCount));
		table->setItem(i, 5, item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_recover));
		table->setItem(i, 6, item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_damage));
		table->setItem(i, 7, item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_damaged));
		table->setItem(i, 8, item);

		item = new QTableWidgetItem;
		item->setText(QString::number(rec->m_kill));
		table->setItem(i, 9, item);

		item = new QTableWidgetItem;
		item->setText(rec->m_designation.join(", "));
		table->setItem(i, 10, item);
		i++;
	}

	table->resizeColumnsToContents();

	QLabel *label = new QLabel;
	label->setText(tr("Packages:"));

	QTextEdit *package_label = new QTextEdit;
	package_label->setReadOnly(true);
	package_label->setText(record->getRecordPackages().join(", "));

	QLabel *label_game_mode = new QLabel;
	label_game_mode->setText(tr("GameMode:") + Sanguosha->getModeName(record->getRecordGameMode()));

	QLabel *label_options = new QLabel;
	label_options->setText(tr("ServerOptions:") + record->getRecordServerOptions().join(","));

	QTextEdit *chat_info = new QTextEdit;
	chat_info->setReadOnly(true);
	chat_info->setText(record->getRecordChat());

	QLabel *table_chat_title = new QLabel;
	table_chat_title->setText(tr("Chat Information:"));

	QVBoxLayout *layout = new QVBoxLayout;
	layout->addWidget(label);
	layout->addWidget(package_label);
	layout->addWidget(label_game_mode);
	layout->addWidget(label_options);
	layout->addWidget(table);
	layout->addSpacing(15);
	layout->addWidget(table_chat_title);
	layout->addWidget(chat_info);
	rec_dialog->setLayout(layout);

	rec_dialog->exec();
}

void MainWindow::on_actionView_ban_list_triggered()
{
	static BanlistDialog *dialog = new BanlistDialog(this, true);
	dialog->exec();
}

void MainWindow::on_actionAbout_fmod_triggered()
{
	if (!scene)
		return;

	QString content = tr("FMOD is a proprietary audio library made by Firelight Technologies");
	content.append("<p align='center'> <img src='image/logo/fmod.png' /> </p> <br/>");

	QString address = "http://www.fmod.org";
	content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

#ifdef AUDIO_SUPPORT
	// Linux 唔會連 FMOD：呢個對話框而家報告實際生效嘅 backend，否則喺 Qt
	// backend 上面會顯示一個同 FMOD 無關嘅版本號，睇落好似 FMOD 真係載咗。
	content.append(tr("Audio backend in use: %1 <br/>").arg(Audio::backendName()));
	content.append(tr("Current versionn %1 <br/>").arg(Audio::getVersion()));
#endif

	Window *window = new Window(tr("About fmod"), QSize(500, 260));
	scene->addItem(window);

	window->addContent(content);
	window->addCloseButton(tr("OK"));
	window->setZValue(32766);
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

	window->appear();
}

void MainWindow::on_actionAbout_Lua_triggered()
{
	if (!scene)
		return;

	QString content = tr("Lua is a powerful, fast, lightweight, embeddable scripting language.");
	content.append("<p align='center'> <img src='image/logo/lua.png' /> </p> <br/>");

	QString address = "http://www.lua.org";
	content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

	content.append(tr("Current version %1 <br/>").arg(LUA_RELEASE));
	content.append(LUA_COPYRIGHT);

	Window *window = new Window(tr("About Lua"), QSize(500, 585));
	scene->addItem(window);

	window->addContent(content);
	window->addCloseButton(tr("OK"));
	window->setZValue(32766);
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

	window->appear();
}

void MainWindow::on_actionAbout_GPLv3_triggered()
{
	if (!scene)
		return;

	QString content = tr("The GNU General Public License is the most widely used free software license, which guarantees end users the freedoms to use, study, share, and modify the software.");
	content.append("<p align='center'> <img src='image/logo/gplv3.png' /> </p> <br/>");

	QString address = "http://gplv3.fsf.org";
	content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

	Window *window = new Window(tr("About GPLv3"), QSize(500, 225));
	scene->addItem(window);

	window->addContent(content);
	window->addCloseButton(tr("OK"));
	window->setZValue(32766);
	window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

	window->appear();
}

QGraphicsScene* MainWindow::getScene()
{
	return scene;
}
