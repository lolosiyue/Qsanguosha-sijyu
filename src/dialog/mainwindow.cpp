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
#ifdef Q_OS_ANDROID
#include "android-content-dialog.h"
#endif
#include "recorder.h"
#include "lua.hpp"
#include "engine.h"
#include "connectiondialog.h"
#include "configdialog.h"
#include "clientstruct.h"
#include "client.h"
#ifdef Q_OS_ANDROID
#include "client-live-session.h"
#include "room.h"
#include "protocol/session/session-payloads.h"
#endif
#include "clientplayer.h"
#include "game-session-config.h"
#include "game-snapshot.h"
#include "replay-takeover-validation.h"
// Automated-test diagnostics and the --asset-root forwarding both use QSanRuntimePaths, not just the XP legacy path.
#include "runtime-paths.h"
#ifdef QSAN_XP_LEGACY
#include "local-server-controller.h"
#include <QInputDialog>
#endif
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
#include <QFrame>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#ifdef Q_OS_ANDROID
#include <QMenu>
#include <QPointer>
#include <QToolButton>
#include <QInputMethod>
#include <QSaveFile>
#include <QStandardPaths>
#include <QWindow>
#endif
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

QString localLoadingBackdropPath()
{
	QStringList candidates;
	const QString configuredPath = Config.BackgroundImage.trimmed();
	if (!configuredPath.isEmpty()) {
		candidates << (QDir::isAbsolutePath(configuredPath)
			? configuredPath : QSanRuntimePaths::assetPath(configuredPath));
	}
	candidates << QSanRuntimePaths::assetPath(
		QStringLiteral("image/system/backdrop/default.jpg"));
	candidates << QSanRuntimePaths::assetPath(
		QStringLiteral("image/system/backdrop/new-version.jpg"));

	for (const QString &candidate : candidates) {
		if (QFileInfo(candidate).isFile())
			return candidate;
	}
	return QString();
}

class LocalLoadingPage final : public QWidget
{
public:
	explicit LocalLoadingPage(QWidget *parent = nullptr)
		: QWidget(parent)
	{
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	void setBackgroundImage(const QString &path)
	{
		if (path == m_backgroundPath)
			return;
		m_backgroundPath = path;
		m_background = QPixmap(path);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.fillRect(rect(), QColor(QStringLiteral("#081321")));

		if (!m_background.isNull() && width() > 0 && height() > 0) {
			const qreal targetRatio = qreal(width()) / qreal(height());
			const qreal imageRatio = qreal(m_background.width())
				/ qreal(m_background.height());
			QRectF sourceRect(QPointF(0, 0), m_background.size());
			if (targetRatio > imageRatio) {
				const qreal sourceHeight = m_background.width() / targetRatio;
				sourceRect.setTop((m_background.height() - sourceHeight) / 2.0);
				sourceRect.setHeight(sourceHeight);
			} else {
				const qreal sourceWidth = m_background.height() * targetRatio;
				sourceRect.setLeft((m_background.width() - sourceWidth) / 2.0);
				sourceRect.setWidth(sourceWidth);
			}
			painter.drawPixmap(QRectF(rect()), m_background, sourceRect);
		}

		QLinearGradient shade(0, 0, width(), height());
		shade.setColorAt(0.0, QColor(3, 10, 19, 220));
		shade.setColorAt(0.55, QColor(8, 20, 33, 168));
		shade.setColorAt(1.0, QColor(3, 10, 18, 205));
		painter.fillRect(rect(), shade);

		QLinearGradient lowerFade(0, height() * 0.35, 0, height());
		lowerFade.setColorAt(0.0, QColor(5, 13, 23, 0));
		lowerFade.setColorAt(1.0, QColor(5, 13, 23, 145));
		painter.fillRect(rect(), lowerFade);
	}

private:
	QString m_backgroundPath;
	QPixmap m_background;
};

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
#ifdef QSAN_XP_LEGACY
	setupLocalServerController();
#endif
	pageStack->addWidget(gameView);

	setCentralWidget(pageStack);
#if QSAN_ENABLE_QML
	m_pointerOverlay = new PointerEffectOverlay(this);
#endif
	restoreFromConfig();
#ifdef Q_OS_ANDROID
	setupAndroidUi();
	connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
		&MainWindow::handleApplicationStateChanged);
#endif

	setupHomePage();
#ifdef Q_OS_ANDROID
	restoreAndroidOfflineMarker();
#endif
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
	LocalLoadingPage *loadingPage = new LocalLoadingPage(pageStack);
	loadingPage->setBackgroundImage(localLoadingBackdropPath());
	localLoadingPage = loadingPage;
	localLoadingPage->setObjectName(QStringLiteral("localRoomLoadingPage"));
	localLoadingPage->setStyleSheet(QStringLiteral(
		"QFrame#localLoadingPanel {"
		" background-color: rgba(8, 18, 31, 218);"
		" border: 1px solid rgba(226, 190, 119, 108);"
		" border-radius: 18px;"
		"}"
		"QLabel#localLoadingEyebrow { color: #E6C179; font-size: 12px; font-weight: 700; }"
		"QLabel#localLoadingTitle { color: #FFF8E9; }"
		"QLabel#localLoadingSubtitle { color: #B9C9DA; font-size: 14px; }"
		"QLabel#localLoadingStatus { color: #F7FAFE; font-size: 15px; font-weight: 600; }"
		"QFrame#localLoadingDivider { background: rgba(226, 190, 119, 72); border: none; }"
		"QProgressBar#localLoadingProgress {"
		" min-height: 8px; max-height: 8px; border: none; border-radius: 4px;"
		" background: rgba(226, 236, 247, 35);"
		"}"
		"QProgressBar#localLoadingProgress::chunk {"
		" border-radius: 4px; background: #D9AD5C;"
		"}"
		"QPushButton#localLoadingCancel {"
		" min-width: 150px; padding: 8px 22px; color: #FFF8E9;"
		" border: 1px solid rgba(226, 190, 119, 120); border-radius: 6px;"
		" background: rgba(8, 18, 31, 205);"
		"}"
		"QPushButton#localLoadingCancel:hover { background: rgba(217, 173, 92, 95); }"));

	QVBoxLayout *layout = new QVBoxLayout(localLoadingPage);
	layout->setContentsMargins(42, 36, 42, 36);
	layout->addStretch(2);

	QFrame *panel = new QFrame(localLoadingPage);
	panel->setObjectName(QStringLiteral("localLoadingPanel"));
	panel->setMinimumWidth(520);
	panel->setMaximumWidth(680);
	QVBoxLayout *panelLayout = new QVBoxLayout(panel);
	panelLayout->setContentsMargins(54, 42, 54, 44);

	QLabel *eyebrow = new QLabel(tr("LOCAL GAME"), panel);
	eyebrow->setObjectName(QStringLiteral("localLoadingEyebrow"));
	eyebrow->setAlignment(Qt::AlignCenter);
	panelLayout->addWidget(eyebrow);
	panelLayout->addSpacing(9);

	QLabel *title = new QLabel(tr("Preparing local game"), panel);
	title->setObjectName(QStringLiteral("localLoadingTitle"));
	QFont titleFont = title->font();
	titleFont.setPointSize(qMax(24, titleFont.pointSize() + 12));
	titleFont.setBold(true);
	title->setFont(titleFont);
	title->setAlignment(Qt::AlignCenter);
	panelLayout->addWidget(title);

	QLabel *subtitle = new QLabel(
		tr("Rules, AI, and room services are being prepared."), panel);
	subtitle->setObjectName(QStringLiteral("localLoadingSubtitle"));
	subtitle->setAlignment(Qt::AlignCenter);
	subtitle->setWordWrap(true);
	panelLayout->addSpacing(8);
	panelLayout->addWidget(subtitle);

	QFrame *divider = new QFrame(panel);
	divider->setObjectName(QStringLiteral("localLoadingDivider"));
	divider->setFixedHeight(1);
	panelLayout->addSpacing(24);
	panelLayout->addWidget(divider);

	localLoadingStatus = new QLabel(panel);
	localLoadingStatus->setObjectName(QStringLiteral("localLoadingStatus"));
	localLoadingStatus->setAlignment(Qt::AlignCenter);
	localLoadingStatus->setWordWrap(true);
	localLoadingStatus->setMinimumHeight(42);
	panelLayout->addSpacing(18);
	panelLayout->addWidget(localLoadingStatus);

	localLoadingProgress = new QProgressBar(panel);
	localLoadingProgress->setObjectName(QStringLiteral("localLoadingProgress"));
	localLoadingProgress->setRange(0, 0);
	localLoadingProgress->setTextVisible(false);
	localLoadingProgress->setMaximumWidth(500);
	panelLayout->addSpacing(7);
	panelLayout->addWidget(localLoadingProgress);

	layout->addWidget(panel, 0, Qt::AlignHCenter);
	layout->addStretch(3);

	pageStack->addWidget(localLoadingPage);
}

void MainWindow::showLocalLoadingPage(const QString &status)
{
	static_cast<LocalLoadingPage *>(localLoadingPage)->setBackgroundImage(
		localLoadingBackdropPath());
	QString displayStatus = status;
	if (status == QLatin1String("Authenticating local server..."))
		displayStatus = tr("Authenticating local server...");
	else if (status == QLatin1String("Validating shared rules..."))
		displayStatus = tr("Validating shared rules...");
	else if (status == QLatin1String("Initializing rules and extensions..."))
		displayStatus = tr("Initializing rules and extensions...");
	else if (status == QLatin1String("Preparing initial room..."))
		displayStatus = tr("Preparing initial room...");
	if (localLoadingStatus)
		localLoadingStatus->setText(displayStatus);
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
#ifdef Q_OS_ANDROID
	m_androidAwaitingStateSync = false;
	gameView->setEnabled(!m_androidApplicationBackgrounded);
	menuBar()->setEnabled(!m_androidApplicationBackgrounded);
	m_androidLocalRoomActive = false;
	m_androidLocalServerListening = false;
	clearAndroidOfflineMarker();
	qApp->setProperty("androidOfflineRoomDead", false);
	if (m_androidMenuButton)
		m_androidMenuButton->hide();
#endif
#ifdef QSAN_XP_LEGACY
	if (localServer && localServer->active()) {
		if (localServer->hostOnly() && localServer->isReady()) {
			StartScene *management = new StartScene;
			management->switchToServer(localServer);
			showGamePage(management);
			return;
		}
		localServer->stop();
	}
	m_localClientPending = false;
#endif
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
#ifdef Q_OS_ANDROID
	updateAndroidSafeArea();
	if (m_androidMenuButton)
		m_androidMenuButton->show();
#endif
}

#ifdef Q_OS_ANDROID
void MainWindow::setupAndroidUi()
{
	m_androidMenuButton = new QToolButton(pageStack);
	m_androidMenuButton->setObjectName(QStringLiteral("androidOverflowButton"));
	m_androidMenuButton->setText(QStringLiteral("\u22EE"));
	m_androidMenuButton->setToolTip(tr("More actions"));
	m_androidMenuButton->setMinimumSize(QSize(52, 52));
	m_androidMenuButton->setAutoRaise(false);
	m_androidMenu = new QMenu(m_androidMenuButton);
	m_androidMenu->addAction(tr("Resources and extensions..."), this, [this]() {
		AndroidContentDialog::openManager(this);
	});
	m_androidMenu->addSeparator();
	m_androidMenu->addAction(ui->actionView_Discarded);
	m_androidMenu->addAction(ui->actionView_distance);
	m_androidMenu->addAction(ui->actionView_Maxcards);
	m_androidMenu->addAction(ui->actionServerInformation);
	m_androidMenu->addAction(ui->actionSaveRecord);
	m_androidMenu->addAction(ui->actionPause_Resume);
	m_androidMenu->addAction(ui->actionHide_Show_chat_box);
	m_androidMenu->addAction(ui->actionSurrender);
	m_androidMenuButton->setMenu(m_androidMenu);
	m_androidMenuButton->setPopupMode(QToolButton::InstantPopup);
	m_androidMenuButton->hide();
	updateAndroidSafeArea();
}

void MainWindow::updateAndroidSafeArea()
{
	if (!m_androidMenuButton || !pageStack)
		return;
	QMargins margins;
	if (windowHandle())
		margins = windowHandle()->safeAreaMargins();
	const QRect windowRect = geometry();
	QRect keyboardRect = qApp->inputMethod()->keyboardRectangle().toRect();
	if (QWindow *focusWindow = QGuiApplication::focusWindow())
		keyboardRect.translate(focusWindow->mapToGlobal(QPoint(0, 0)));
	if (!keyboardRect.isEmpty() && windowRect.intersects(keyboardRect))
		margins.setBottom(qMax(margins.bottom(), qBound(0,
			windowRect.bottom() - keyboardRect.top() + 1, windowRect.height())));
	const int right = qMax(8, margins.right() + 8);
	const int top = qMax(8, margins.top() + 8);
	m_androidMenuButton->move(pageStack->width() - m_androidMenuButton->width() - right, top);
	gameView->setSafeAreaMargins(margins);
	if (scene) {
		if (RoomScene *roomScene = qobject_cast<RoomScene *>(scene))
			roomScene->setSafeAreaMargins(margins);
	}
}

void MainWindow::updateAndroidLocalRoomLifecycle(bool backgrounded)
{
	if (!server || !m_androidLocalRoomActive)
		return;
	const QList<Room *> rooms = server->findChildren<Room *>();
	for (Room *room : rooms) {
		if (room && room->isSinglePlayerMode()) {
			room->setApplicationBackgrounded(backgrounded);
			return;
		}
	}
}

void MainWindow::handleApplicationStateChanged(Qt::ApplicationState state)
{
	const bool backgrounded = state != Qt::ApplicationActive;
	qApp->setProperty("qsan.application_suspended", backgrounded);
	qApp->setProperty("qsan.application_suspended_offline", m_androidLocalRoomActive);
	if (backgrounded == m_androidApplicationBackgrounded) {
		updateAndroidSafeArea();
		return;
	}
	m_androidApplicationBackgrounded = backgrounded;
	menuBar()->setEnabled(!backgrounded && !m_androidAwaitingStateSync);
	if (gameView)
		gameView->setEnabled(!backgrounded && !m_androidAwaitingStateSync);
	if (m_androidMenuButton)
		m_androidMenuButton->setEnabled(!backgrounded && !m_androidAwaitingStateSync);
	if (RoomScene *roomScene = qobject_cast<RoomScene *>(scene))
		roomScene->setApplicationSuspended(backgrounded, m_androidLocalRoomActive);
#ifdef AUDIO_SUPPORT
	Audio::setApplicationSuspended(backgrounded);
#endif
	if (backgrounded) {
		updateAndroidLocalRoomLifecycle(true);
		return;
	}
	updateAndroidLocalRoomLifecycle(false);
	if (!m_androidLocalRoomActive && qobject_cast<RoomScene *>(scene)
		&& ClientInstance && !ClientInstance->isReplayState()) {
		// A reconnect snapshot reconstructs legacy ClientPlayer and card models.
		// Destroy their scene first, while Self and ClientInstance still refer to
		// the old client; its destructor must not clear a newly created Self.
		m_androidAwaitingStateSync = true;
		gameView->setEnabled(false);
		menuBar()->setEnabled(false);
		if (m_androidMenuButton) m_androidMenuButton->setEnabled(false);
		showLocalLoadingPage(tr("Reconnecting to the game..."));
		gameView->setScene(nullptr);
		delete scene;
		scene = nullptr;
		ClientInstance->disconnect(this);
		ClientInstance->disconnectFromHost();
		delete ClientInstance;
		startConnectionWithReconnect(true);
	}
	if (m_androidLocalRoomWaitingForForeground) {
		m_androidLocalRoomWaitingForForeground = false;
		QTimer::singleShot(0, this, [this]() { completeLocalRoomStart(); });
	}
	updateAndroidSafeArea();
}

QString MainWindow::androidOfflineMarkerPath() const
{
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
		+ QStringLiteral("/userdata/offline-session.json");
}

void MainWindow::writeAndroidOfflineMarker(const QString &reason)
{
	const QString path = androidOfflineMarkerPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	QJsonObject marker;
	marker.insert(QStringLiteral("state"), QStringLiteral("dead"));
	marker.insert(QStringLiteral("reason"), reason);
	marker.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	file.write(QJsonDocument(marker).toJson(QJsonDocument::Compact));
	file.commit();
}

void MainWindow::clearAndroidOfflineMarker()
{
	QFile::remove(androidOfflineMarkerPath());
}

void MainWindow::restoreAndroidOfflineMarker()
{
	QFile file(androidOfflineMarkerPath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	const QJsonObject marker = QJsonDocument::fromJson(file.readAll()).object();
	file.close();
	clearAndroidOfflineMarker();
	if (marker.value(QStringLiteral("state")).toString() != QLatin1String("dead"))
		return;
	QMessageBox::information(this, tr("Local game stopped"),
		tr("The previous offline game stopped unexpectedly. A new local game can be started from the home page."));
}
#endif

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
#ifdef Q_OS_ANDROID
	// A completed close is a normal exit; leave the marker only for process death.
	clearAndroidOfflineMarker();
#endif
#ifdef QSAN_XP_LEGACY
	if (localServer && localServer->active()) {
		event->ignore();
		m_closeAfterServer = true;
		if (ClientInstance) ClientInstance->disconnectFromHost();
		showLocalLoadingPage(tr("Stopping local server..."));
		localServer->stop();
		return;
	}
#endif
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
#ifdef Q_OS_ANDROID
	updateAndroidSafeArea();
#endif
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
#ifdef Q_OS_ANDROID
	startLocalConsoleGame();
	return;
#endif
#ifdef QSAN_XP_LEGACY
	if (localServer->active()) return;
#endif
	static ServerDialog *dialog = new ServerDialog(this);
	int accept_type = dialog->config();
	if (accept_type == 0)
		return;

#ifdef QSAN_XP_LEGACY
	showLocalLoadingPage(tr("Starting local server..."));
	GameSessionConfig session;
	const QVariant seed = qApp->property("xpGameSeed");
	if (seed.isValid()) session.seed = seed.toULongLong();
	localServer->start(LocalServerController::Ownership::OwnedHost, accept_type == 1, session);
#else
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
#endif
}

#ifdef QSAN_XP_LEGACY
void MainWindow::setupLocalServerController()
{
	localServer = new LocalServerController(this);
	QPushButton *cancel = new QPushButton(tr("Cancel"), localLoadingPage);
	cancel->setObjectName(QStringLiteral("localLoadingCancel"));
	localLoadingPage->layout()->addWidget(cancel, 0, Qt::AlignHCenter);
	connect(cancel, &QPushButton::clicked, this, [this]() {
		if (m_takeoverInProgress) rollbackTakeover(QString());
		else { localServer->stop(); showHomePage(); }
	});
	connect(localServer, &LocalServerController::progress, this,
		[this](const QString &phase) { if (!m_closeAfterServer) showLocalLoadingPage(phase); });
	connect(localServer, &LocalServerController::ready, this, &MainWindow::completeLocalRoomStart);
	connect(localServer, &LocalServerController::failed, this, [this](const QString &error) {
		if (m_closeAfterServer) return;
		if (m_takeoverInProgress) rollbackTakeover(error);
		else failLocalRoomStart(error);
	});
	connect(localServer, &LocalServerController::takeoverReady, this, [this]() {
		m_takeoverGameStarted = true;
		m_takeoverInProgress = false;
		m_replayRestoreState = ReplayRestoreState();
	});
	connect(localServer, &LocalServerController::takeoverFailed, this, &MainWindow::rollbackTakeover);
	connect(localServer, &LocalServerController::stopped, this, [this](bool graceful) {
		if (!graceful) qWarning("XP helper exit was not a completed graceful shutdown");
		if (m_closeAfterServer) { close(); return; }
		ui->actionStart_Game->disconnect();
		connect(ui->actionStart_Game, SIGNAL(triggered()), connection_dialog, SLOT(exec()));
		ui->actionStart_Server->setEnabled(true);
	});
	connect(localServer, &LocalServerController::commandResult, this,
		[this](const QString &, bool ok, const QJsonObject &body) {
			if (!ok && !m_closeAfterServer && body.value("code").toString() != "cancelled")
				QMessageBox::warning(this, tr("Server command failed"), body.value("code").toString());
		});
}
#endif

void MainWindow::startLocalConsoleGame()
{
#ifdef Q_OS_ANDROID
	// The first offline game is gated on the private media bundle being complete.
	if (!AndroidContentDialog::prepareForGame())
		return;
	m_androidLocalRoomActive = true;
	writeAndroidOfflineMarker(QStringLiteral("process_started"));
#endif
#ifdef QSAN_XP_LEGACY
	if (localServer->active()) return;
	showLocalLoadingPage(tr("Initializing local rules and AI..."));
	GameSessionConfig session;
	const QVariant seed = qApp->property("xpGameSeed");
	if (seed.isValid()) session.seed = seed.toULongLong();
	localServer->start(LocalServerController::Ownership::OwnedPrivate, false, session);
#else
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
#endif
}

void MainWindow::completeLocalRoomStart()
{
#ifdef QSAN_XP_LEGACY
	if (!localServer->isReady()) return;
	if (localServer->hostOnly()) {
		ui->actionStart_Game->disconnect();
		connect(ui->actionStart_Game, SIGNAL(triggered()), this, SLOT(startGameInAnotherInstance()));
		StartScene *management = new StartScene;
		management->switchToServer(localServer);
		showGamePage(management);
		if (Config.value("EnableMinimizeDialog").toBool()) on_actionMinimize_to_system_tray_triggered();
		return;
	}
	Config.HostAddress = localServer->endpoint();
	m_localClientPending = true;
	showLocalLoadingPage(tr("Connecting to local room..."));
	startConnectionWithReconnect(false);
#else
	if (!server)
		return;
	#ifdef Q_OS_ANDROID
	if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
		m_androidLocalRoomWaitingForForeground = true;
		updateAndroidLocalRoomLifecycle(true);
		showLocalLoadingPage(tr("Local room is ready; waiting for foreground..."));
		return;
	}
	#endif
	showLocalLoadingPage(tr("Starting local server..."));
	#ifdef Q_OS_ANDROID
	if (!m_androidLocalServerListening && !server->listen()) {
	#else
	if (!server->listen()) {
	#endif
		failLocalRoomStart(tr("Can not start server!"));
		return;
	}
	#ifdef Q_OS_ANDROID
	m_androidLocalServerListening = true;
	#endif

	#ifndef Q_OS_ANDROID
	server->checkUpnpAndListServer();
	#endif
	Config.HostAddress = QStringLiteral("127.0.0.1");
	showLocalLoadingPage(tr("Connecting to local room..."));
	QTimer::singleShot(0, this, [this]() {
		#ifdef Q_OS_ANDROID
		if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
			m_androidLocalRoomWaitingForForeground = true;
			return;
		}
		#endif
		if (server)
			startConnectionWithReconnect(false);
	});
#endif
}

void MainWindow::failLocalRoomStart(const QString &error)
{
#ifdef QSAN_XP_LEGACY
	localServer->stop();
#endif
	Server *failedServer = server;
	server = nullptr;
#ifdef Q_OS_ANDROID
	m_androidLocalRoomWaitingForForeground = false;
	m_androidLocalRoomActive = false;
	m_androidLocalServerListening = false;
#endif
	if (failedServer)
		failedServer->deleteLater();
	showHomePage();
#ifdef Q_OS_ANDROID
#endif
	QMessageBox::warning(this, tr("Warning"), error.isEmpty()
		? tr("Can not prepare local room!") : error);
}

bool MainWindow::preflightTakeover(const QString &snapshotPath,
    const QString &seatName, QString *error) const
{
    return validateReplayTakeover(m_replayRestoreState.valid ? m_replayRestoreState.path : QString(),
                                 snapshotPath, seatName, error);
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
#ifdef QSAN_XP_LEGACY
	if (localServer->active()) return;
#endif
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
#ifdef QSAN_XP_LEGACY
	showLocalLoadingPage(tr("Preparing replay takeover..."));
	localServer->start(LocalServerController::Ownership::OwnedPrivate, false,
		sessionConfig, m_replayRestoreState.path);
#else
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
#endif
}

void MainWindow::rollbackTakeover(const QString &reason)
{
#ifdef QSAN_XP_LEGACY
	localServer->stop();
#endif
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
		QFile diag(QSanRuntimePaths::userDataPath("client_autotest_diag.log"));
		if (diag.open(QIODevice::Append | QIODevice::Text)) {
			QTextStream(&diag) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
				<< " checkVersion(autotest): server_mod='" << server_mod
				<< "' card_num=" << card_num
				<< " local_card=" << Sanguosha->getCardCount()
				<< " server_ver='" << server_version << "'\n";
		}
		Client *client = qobject_cast<Client *>(sender());
		if (client) {
			connect(client, SIGNAL(server_connected()), this, SLOT(enterRoom()), Qt::UniqueConnection);
			client->signup();
		}
		return;
	}

#ifdef Q_OS_ANDROID
	if (m_androidAwaitingStateSync && (Sanguosha->getMODName() != server_mod
		|| Sanguosha->getCardCount() != card_num || Sanguosha->getVersionNumber() != server_version)) {
		networkError(tr("The server rules or version changed; the game cannot be restored."));
		return;
	}
#endif
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
		connect(client, SIGNAL(server_connected()), this, SLOT(enterRoom()), Qt::UniqueConnection);
		client->signup();
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
	bool fallbackToFreshSignup = true;
#ifdef Q_OS_ANDROID
	// Foreground recovery must restore this seat, never silently join a new room.
	fallbackToFreshSignup = !m_androidAwaitingStateSync;
#endif
	Client *client = new Client(this, QString(), nullptr, m_takeoverInProgress,
		reconnectRequested, fallbackToFreshSignup);
#ifdef Q_OS_ANDROID
	for (ClientLiveSession *session : client->findChildren<ClientLiveSession *>()) {
		connect(session, &ClientLiveSession::frontendMessageReceived, client,
			[this, client](const QSanProtocol::ProtocolMessage &message) {
				if (client != ClientInstance || !m_androidAwaitingStateSync
					|| message.command != QSanProtocol::S_COMMAND_STATE_SYNC) return;
				QSanProtocol::StateSyncPayload sync;
				QString error;
				if (QSanProtocol::StateSyncPayload::parse(message.payload, &sync, &error)
					&& sync.phase == QLatin1String("end")) {
					m_androidAwaitingStateSync = false;
					gameView->setEnabled(!m_androidApplicationBackgrounded);
					menuBar()->setEnabled(!m_androidApplicationBackgrounded);
					if (m_androidMenuButton)
						m_androidMenuButton->setEnabled(!m_androidApplicationBackgrounded);
				}
			});
	}
#endif

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
#ifdef Q_OS_ANDROID
	if (m_androidAwaitingStateSync) {
		// The session may still be dispatching its fatal-error signal. Tear down
		// after that stack unwinds, with the scene preceding its player models.
		QPointer<Client> failedClient = ClientInstance;
		QTimer::singleShot(0, this, [this, failedClient, error_msg]() {
			if (!failedClient || failedClient != ClientInstance) return;
			gameView->setScene(nullptr);
			delete scene;
			scene = nullptr;
			failedClient->disconnect(this);
			failedClient->disconnectFromHost();
			delete failedClient.data();
			showHomePage();
			if (isVisible()) QMessageBox::warning(this, tr("Network error"), error_msg);
		});
		return;
	}
#endif
	if (m_takeoverInProgress) {
		rollbackTakeover(error_msg);
		return;
	}
#ifdef QSAN_XP_LEGACY
	if (m_localClientPending) {
		m_localClientPending = false;
		failLocalRoomStart(error_msg);
		return;
	}
#endif
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
#ifdef QSAN_XP_LEGACY
	m_localClientPending = false;
	// A private ephemeral endpoint is session state, not a saved address.
	if (!localServer->active())
#endif
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
#ifdef Q_OS_ANDROID
	room_scene->setTouchUiEnabled(true);
	room_scene->setApplicationSuspended(m_androidApplicationBackgrounded,
		m_androidLocalRoomActive);
#endif
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
#ifdef Q_OS_ANDROID
	updateAndroidSafeArea();
#endif

	// 自動化測試: --auto-robots 由 owner 自動填滿 AI (填滿後伺服器端自動開局)
	if (Config.AutoAddRobots || m_takeoverInProgress) {
		const bool takeoverRobotFill = m_takeoverInProgress;
		QFile diag(QSanRuntimePaths::userDataPath("client_autotest_diag.log"));
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
			QFile diag(QSanRuntimePaths::userDataPath("client_autotest_diag.log"));
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
#ifdef QSAN_XP_LEGACY
	if (!localServer->isReady()) return;
	QProcess::startDetached(QApplication::applicationFilePath(),
		QStringList() << ("-connect:" + localServer->endpoint())
		<< "--asset-root" << QSanRuntimePaths::assetRoot(), QApplication::applicationDirPath());
#else
	QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
#endif
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
#ifdef QSAN_XP_LEGACY
	if (!localServer->isReady()) {
		QMessageBox::warning(this, tr("Warning"), tr("Server is not started yet!"));
		return;
	}
	bool accepted = false;
	const QString text = QInputDialog::getMultiLineText(this, tr("Broadcast"),
		tr("Please input the message to broadcast"), QString(), &accepted);
	if (accepted && !text.isEmpty()) localServer->request("broadcast", {{"message", text}});
#else
	Server *server = findChild<Server *>();
	if (server == nullptr) {
		QMessageBox::warning(this, tr("Warning"), tr("Server is not started yet!"));
		return;
	}

	static BroadcastBox *dialog = new BroadcastBox(server, this);
	dialog->exec();
#endif
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
#ifdef QSAN_XP_LEGACY
	BanIpDialog *dlg = new BanIpDialog(this, localServer);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
#else
	static BanIpDialog *dlg = new BanIpDialog(this, server);
#endif
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
	// Linux does not link FMOD: this dialog reports the actually active backend,
	// otherwise on the Qt backend it would show an FMOD-unrelated version number that looks as if FMOD were really loaded.
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
