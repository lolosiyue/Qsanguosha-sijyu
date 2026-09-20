#include "room-replay-controller.h"

#include "client.h"
#include "clientplayer.h"
#include "dashboard.h"
#include "engine.h"
#include "game-snapshot.h"
#include "qsanbutton.h"
#include "recorder.h"
#include "replay-diagnostic-exporter.h"
#include "replay-index.h"
#include "replay-timeline.h"
#include "room.h"
#include "roomscene.h"
#include "runtime-paths.h"
#include "settings.h"
#ifdef QSAN_XP_LEGACY
#include "local-server-controller.h"
#include "mainwindow.h"
#endif

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsProxyWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

QRectF ReplayerControlBar::boundingRect() const
{
	// Four skin buttons, two 52px text actions and the same 49px allowance
	// the original control bar reserved after its time-label origin.
	return QRectF(0, 0,
		(S_BUTTON_WIDTH + S_BUTTON_GAP) * 4 + (52 + S_BUTTON_GAP) * 2 + 49,
		S_BUTTON_HEIGHT);
}

void ReplayerControlBar::paint(QPainter*,const QStyleOptionGraphicsItem*,QWidget*)
{
}

ReplayerControlBar::ReplayerControlBar(Dashboard*dashboard)
	: time_label(nullptr), takeover_button(nullptr), export_button(nullptr),
	  speed(1.0), export_in_progress(false)
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

	// This is a normal widget instead of a skin button because takeover is a
	// new action and old skin packs do not contain a replay/takeover sprite.
	takeover_button = new QPushButton(tr("接管"));
	takeover_button->setFixedSize(52, S_BUTTON_HEIGHT);
	QGraphicsProxyWidget *takeoverWidget = new QGraphicsProxyWidget(this);
	takeoverWidget->setWidget(takeover_button);
	takeoverWidget->setPos(step*4, 0);
	connect(takeover_button, &QPushButton::clicked,
		this, &ReplayerControlBar::requestTakeover);

	export_button = new QPushButton(QStringLiteral("匯出"));
	export_button->setToolTip(QStringLiteral("匯出 Bug 診斷包"));
	export_button->setFixedSize(52, S_BUTTON_HEIGHT);
	QGraphicsProxyWidget *exportWidget = new QGraphicsProxyWidget(this);
	exportWidget->setWidget(export_button);
	exportWidget->setPos(step*4 + 52 + S_BUTTON_GAP, 0);
	connect(export_button, &QPushButton::clicked,
		this, &ReplayerControlBar::exportRequested);

	time_label = new QLabel;
	time_label->setAttribute(Qt::WA_NoSystemBackground);
	time_label->setText("-----------------------------------------------------");
	QPalette palette;
	palette.setColor(QPalette::WindowText,UiConfig.TextEditColor);
	time_label->setPalette(palette);

	QGraphicsProxyWidget*widget = new QGraphicsProxyWidget(this);
	widget->setWidget(time_label);
	widget->setPos(step*4 + (52 + S_BUTTON_GAP)*2, 0);

	Replayer*replayer = ClientInstance->getReplayer();
	connect(play,SIGNAL(clicked()),replayer,SLOT(toggle()));
	connect(uniform,SIGNAL(clicked()),replayer,SLOT(uniform()));
	connect(slow_down,SIGNAL(clicked()),replayer,SLOT(slowDown()));
	connect(speed_up,SIGNAL(clicked()),replayer,SLOT(speedUp()));
	connect(replayer, &Replayer::elasped, this, &ReplayerControlBar::setTime,
		Qt::QueuedConnection);
	connect(replayer,SIGNAL(speed_changed(qreal)),this,SLOT(setSpeed(qreal)));

	speed = replayer->getSpeed();
	setParentItem(dashboard);
	setPos(S_BUTTON_GAP,-S_BUTTON_GAP-S_BUTTON_HEIGHT);

	duration_str = FormatTime(replayer->getDuration());
	updateTakeoverAvailability();
	connect(replayer, &Replayer::seek_finished,
		this, &ReplayerControlBar::updateTakeoverAvailability);
	connect(replayer, &Replayer::node_reached,
		this, [this](int) { updateTakeoverAvailability(); });
}

void ReplayerControlBar::updateTakeoverAvailability()
{
	Replayer *replayer = ClientInstance ? ClientInstance->getReplayer() : nullptr;
	if (takeover_button) {
		takeover_button->setEnabled(!export_in_progress && replayer
			&& replayer->getNearestTakeoverNodeAtOrBeforeCurrent() >= 0);
	}
	if (export_button) {
		export_button->setEnabled(!export_in_progress && replayer
			&& replayer->isValid()
			&& replayer->getPath().endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
			&& replayer->hasTakeoverSnapshots());
	}
}

void ReplayerControlBar::setExportInProgress(bool inProgress)
{
	export_in_progress = inProgress;
	updateTakeoverAvailability();
}

void ReplayerControlBar::requestTakeover()
{
	Replayer *replayer = ClientInstance ? ClientInstance->getReplayer() : nullptr;
	if (!replayer)
		return;

	const int nodeIndex = replayer->getNearestTakeoverNodeAtOrBeforeCurrent();
	auto snapshot = replayer->getSnapshot(nodeIndex);
	if (nodeIndex < 0 || !snapshot)
		return;

	const GlobalSnapshot state = snapshot->getState();
	QList<int> aliveRows;
	QDialog dialog(QApplication::activeWindow());
	dialog.setWindowTitle(tr("接管座位"));
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(tr("選擇要接管的座位："), &dialog));
	QComboBox *seatBox = new QComboBox(&dialog);

	int preferredRow = -1;
	RoomScene *roomScene = qobject_cast<RoomScene *>(scene());
	QString preferredSeat = roomScene ? roomScene->m_currentPerspective : QString();
	bool preferredAlive = false;
	for (const PlayerSnapshot &player : state.players) {
		if (player.objectName == preferredSeat && player.alive)
			preferredAlive = true;
	}
	if (!preferredAlive)
		preferredSeat = state.currentPlayer;

	for (const PlayerSnapshot &player : state.players) {
		const QString label = player.screenName.isEmpty()
			? player.objectName
			: QStringLiteral("%1 (%2)").arg(player.screenName, player.objectName);
		seatBox->addItem(label, player.objectName);
		const int row = seatBox->count() - 1;
		if (player.alive)
			aliveRows << row;
		else if (QStandardItemModel *model = qobject_cast<QStandardItemModel *>(seatBox->model()))
			if (QStandardItem *item = model->item(row))
				item->setEnabled(false);
		if (player.objectName == preferredSeat && player.alive)
			preferredRow = row;
	}
	if (preferredRow >= 0)
		seatBox->setCurrentIndex(preferredRow);
	else if (!aliveRows.isEmpty())
		seatBox->setCurrentIndex(aliveRows.first());
	else
		return;
	layout->addWidget(seatBox);

	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted)
		return;

	const QString seatName = seatBox->currentData().toString();
	if (seatName.isEmpty() || !aliveRows.contains(seatBox->currentIndex()))
		return;

	QString actor = state.currentPlayer;
	for (const PlayerSnapshot &player : state.players) {
		if (player.objectName == state.currentPlayer) {
			actor = player.screenName.isEmpty() ? player.objectName : player.screenName;
			break;
		}
	}
	const QString prompt = tr("從第 %1 回合（%2 的回合）接管？")
		.arg(QString::number(snapshot->getTurnSerial()), actor);
	if (QMessageBox::question(QApplication::activeWindow(), tr("接管"), prompt,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;

	emit takeoverRequested(replayer->getTakeoverSnapshotPath(nodeIndex), seatName);
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
	updateTakeoverAvailability();
}

RoomReplayController::RoomReplayController(RoomScene *scene, QMainWindow *mainWindow)
	: QObject(scene), m_scene(scene), m_mainWindow(mainWindow),
	  m_replayControl(nullptr), m_replayTimeline(nullptr)
{
#ifdef QSAN_XP_LEGACY
	MainWindow *ownerWindow = qobject_cast<MainWindow *>(m_mainWindow);
	LocalServerController *owner = ownerWindow ? ownerWindow->localServerController() : nullptr;
	if (owner && owner->isReady() && !owner->hostOnly()
		&& Config.HostAddress == owner->endpoint() && !ClientInstance->getReplayer()) {
		m_xpReplayGeneration = owner->generation();
		connect(owner, &LocalServerController::replayFinalized, this,
			[this, owner](const QString &path, bool ok, const QString &detail) {
				if (owner->generation() != m_xpReplayGeneration || !m_xpReplayExports.contains(path)) return;
				const bool reportFailure = m_xpReplayExports.take(path);
				if (ok) qInfo().noquote() << "Replay snapshot manifest saved:" << path;
				else {
					qWarning().noquote() << "Replay snapshot manifest was not saved:" << detail;
					if (reportFailure && detail != QStringLiteral("cancelled"))
						QMessageBox::warning(m_mainWindow, tr("Save replay record"),
							tr("The replay was saved, but its takeover snapshots could not be saved: %1").arg(detail));
				}
			});
	}
#endif
}

RoomReplayController::~RoomReplayController()
{
	if (m_pendingReplayCaptureId != 0 && ClientInstance
		&& ClientInstance->getReplayer()) {
		ClientInstance->getReplayer()->cancelStateCaptureBoundary(
			m_pendingReplayCaptureId);
	}
	if (m_replayExportInProgress && QApplication::overrideCursor())
		QApplication::restoreOverrideCursor();
}

void RoomReplayController::createPlaybackUi(Dashboard *dashboard)
{
	createReplayControlBar(dashboard);
	createReplayTimeline();
}

void RoomReplayController::createReplayControlBar(Dashboard *dashboard)
{
	m_replayControl = new ReplayerControlBar(dashboard);
	connect(m_replayControl, &ReplayerControlBar::takeoverRequested,
		this, &RoomReplayController::takeoverRequested);
	connect(m_replayControl, &ReplayerControlBar::exportRequested,
		this, &RoomReplayController::exportReplayDiagnosticBundle);
	connect(ClientInstance, &Client::replayStateCaptureReady,
		this, &RoomReplayController::onReplayStateCaptureReady);
	Replayer *replayer = ClientInstance->getReplayer();
	connect(replayer, &QThread::finished, this, [this]() {
		if (!m_replayExportInProgress || m_pendingReplayCaptureId == 0)
			return;
		finishReplayDiagnosticExport(QJsonObject(), false,
			QStringLiteral("Replay 已播放完畢，無法建立事件 barrier"));
	});
}

void RoomReplayController::setReplayExportInProgress(bool inProgress)
{
	m_replayExportInProgress = inProgress;
	if (m_replayControl) {
		m_replayControl->setEnabled(!inProgress);
		m_replayControl->setExportInProgress(inProgress);
	}
	if (m_replayTimeline)
		m_replayTimeline->setEnabled(!inProgress);
}

void RoomReplayController::exportReplayDiagnosticBundle()
{
	if (m_replayExportInProgress || !ClientInstance)
		return;

	Replayer *replayer = ClientInstance->getReplayer();
	if (!replayer || !replayer->isValid()
		|| !replayer->getPath().endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
		|| !replayer->hasTakeoverSnapshots())
		return;

	const QFileInfo replayInfo(replayer->getPath());
	const QString timestamp = QDateTime::currentDateTimeUtc().toString(
		QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
	const QString defaultName = QStringLiteral("%1-bug-%2.qsgbug.zip")
		.arg(replayInfo.completeBaseName(), timestamp);
	QString outputPath = QFileDialog::getSaveFileName(m_mainWindow,
		QStringLiteral("匯出 Bug 診斷包"),
		QDir(replayInfo.absolutePath()).filePath(defaultName),
		QStringLiteral("QSanguosha Bug 診斷包 (*.qsgbug.zip)"));
	if (outputPath.isEmpty())
		return;
	if (outputPath.endsWith(QStringLiteral(".qsgbug.zip"), Qt::CaseInsensitive)) {
		// Keep the canonical extension selected by the dialog.
	} else if (outputPath.endsWith(QStringLiteral(".qsgbug"), Qt::CaseInsensitive))
		outputPath += QStringLiteral(".zip");
	else if (outputPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
		outputPath.chop(4);
		outputPath += QStringLiteral(".qsgbug.zip");
	} else {
		outputPath += QStringLiteral(".qsgbug.zip");
	}

	m_pendingReplayBundlePath = outputPath;
	setReplayExportInProgress(true);
	QApplication::setOverrideCursor(Qt::WaitCursor);

	const quint64 requestId = replayer->requestStateCaptureBoundary();
	if (requestId == 0) {
		finishReplayDiagnosticExport(QJsonObject(), false,
			QStringLiteral("Replay 已結束或事件 barrier 無法建立"));
		return;
	}
	m_pendingReplayCaptureId = requestId;

	QTimer::singleShot(2000, this, [this, requestId]() {
		if (!m_replayExportInProgress
			|| m_pendingReplayCaptureId != requestId)
			return;
		finishReplayDiagnosticExport(QJsonObject(), false,
			QStringLiteral("等待精確事件 barrier 逾時 2 秒"));
	});
}

void RoomReplayController::onReplayStateCaptureReady(quint64 requestId,
	const QJsonObject &clientCore, int lastAppliedPairIndex, qint64 elapsedMs)
{
	if (!m_replayExportInProgress
		|| requestId != m_pendingReplayCaptureId) {
		Replayer *replayer = ClientInstance ? ClientInstance->getReplayer() : nullptr;
		if (replayer)
			replayer->releaseStateCaptureBoundary(requestId);
		return;
	}

	QJsonObject stateNow{
		{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("restorable"), false},
		{QStringLiteral("alignment"), lastAppliedPairIndex < 0
			? QStringLiteral("before-first-event")
			: QStringLiteral("after-event")},
		{QStringLiteral("lastAppliedPairIndex"), lastAppliedPairIndex},
		{QStringLiteral("elapsedMs"), elapsedMs},
		{QStringLiteral("clientCore"), clientCore}
	};
	finishReplayDiagnosticExport(stateNow, true, QString());
}

void RoomReplayController::finishReplayDiagnosticExport(const QJsonObject &stateNow,
	bool includeStateNow, const QString &stateNowOmission)
{
	Replayer *replayer = ClientInstance ? ClientInstance->getReplayer() : nullptr;
	ReplayDiagnosticExportResult result;
	if (!replayer) {
		result.error = QStringLiteral("Replay 已關閉");
	} else {
		ReplayDiagnosticExportRequest request;
		request.replayPath = replayer->getPath();
		request.manifestPath = replayer->getTakeoverManifestPath();
		request.snapshotPaths = replayer->getTakeoverSnapshotPaths();
		request.includeStateNow = includeStateNow;
		request.stateNow = stateNow;
		request.stateNowOmission = stateNowOmission;
		request.includeDiagnostics = true;
		request.diagnostics = ReplayDiagnosticExporter::createDiagnostics(*replayer);
		result = ReplayDiagnosticExporter::exportBundle(
			m_pendingReplayBundlePath, request);
	}

	const quint64 requestId = m_pendingReplayCaptureId;
	if (requestId != 0 && replayer) {
		if (includeStateNow)
			replayer->releaseStateCaptureBoundary(requestId);
		else
			replayer->cancelStateCaptureBoundary(requestId);
	}
	m_pendingReplayCaptureId = 0;
	setReplayExportInProgress(false);
	if (QApplication::overrideCursor())
		QApplication::restoreOverrideCursor();

	const QString outputPath = m_pendingReplayBundlePath;
	m_pendingReplayBundlePath.clear();
	if (!result.success) {
		QMessageBox::critical(m_mainWindow, QStringLiteral("匯出失敗"),
			result.error.isEmpty() ? QStringLiteral("無法建立診斷包") : result.error);
		return;
	}

	QString stateStatus = QStringLiteral("已包含");
	if (result.omittedFiles.contains(QStringLiteral("state-now.json"))) {
		stateStatus = QStringLiteral("已省略：%1").arg(
			result.omittedFiles.value(QStringLiteral("state-now.json")));
	}
	QString diagnosticsStatus = QStringLiteral("已包含");
	if (result.omittedFiles.contains(QStringLiteral("diagnostics.json"))) {
		diagnosticsStatus = QStringLiteral("已省略：%1").arg(
			result.omittedFiles.value(QStringLiteral("diagnostics.json")));
	}

	QMessageBox::information(m_mainWindow, QStringLiteral("匯出完成"),
		QStringLiteral("診斷包：%1\nstate-now.json：%2\ndiagnostics.json：%3\n\n"
			"注意：Replay snapshot 可能包含本機路徑；Replay 與 state-now "
			"也可能包含玩家名稱、聊天、房間或連線中繼資料。")
			.arg(QDir::toNativeSeparators(outputPath), stateStatus,
				diagnosticsStatus));
}

void RoomReplayController::createReplayTimeline()
{
	if (!ClientInstance->getReplayer())
		return;

	m_replayTimeline = new ReplayTimeline();
	m_scene->addItem(m_replayTimeline);

	ReplayIndex *index = ClientInstance->getReplayer()->getIndex();
	if (index) {
		m_replayTimeline->setIndex(index);
	}
	m_replayTimeline->setDuration(ClientInstance->getReplayer()->getDuration());

	connect(m_replayTimeline, &ReplayTimeline::timeChanged, this, &RoomReplayController::onReplayTimelineTimeChanged);
	connect(m_replayTimeline, &ReplayTimeline::nodeClicked, this, &RoomReplayController::onReplayTimelineNodeClicked);
	connect(ClientInstance->getReplayer(), &Replayer::elasped,
		this, &RoomReplayController::updateReplayTimeline, Qt::QueuedConnection);

	m_replayTimeline->setPos(100, m_scene->sceneRect().height() - 50);
}

void RoomReplayController::updateReplayTimeline(int secs)
{
	if (m_replayTimeline) {
		m_replayTimeline->setCurrentTime(secs);
	}
}

void RoomReplayController::onReplayTimelineTimeChanged(int secs)
{
	if (ClientInstance->getReplayer()) {
		ClientInstance->getReplayer()->jumpToElapsed(secs * 1000);
	}
}

void RoomReplayController::onReplayTimelineNodeClicked(int nodeIndex)
{
	if (ClientInstance->getReplayer()) {
		ClientInstance->getReplayer()->jumpToNode(nodeIndex);
	}
}

void RoomReplayController::saveReplayRecord()
{
	QString filename = QFileDialog::getSaveFileName(m_mainWindow,tr("Save replay record"),
		QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
		tr("Pure text replay file (*.txt);;Image replay file (*.png)"));

	if (filename.isEmpty()) return;
#ifdef QSAN_XP_LEGACY
	if (m_xpReplayExports.contains(QFileInfo(filename).absoluteFilePath())) return;
#endif
	if (!ClientInstance->save(filename))
		return;

#ifdef QSAN_XP_LEGACY
	if (localReplayController() && !filename.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
		finalizeLocalReplay(filename, true);
		return;
	}
#endif

	Room *room = Sanguosha->currentRoom();
	if (room && !filename.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
		QString error;
		if (!room->finalizeSnapshotManifest(filename, &error))
			qWarning().noquote() << "Replay snapshot manifest was not saved:" << error;
	}
}

void RoomReplayController::recorderAutoSave()
{
	if(ClientInstance->getReplayer()||!Config.value("recorder/autosave",true).toBool())
		return;

	Room *room = Sanguosha->currentRoom();
	bool takeoverSession = room && room->isTakeoverSession();
#ifdef QSAN_XP_LEGACY
	LocalServerController *owner = localReplayController();
	if (owner) takeoverSession = owner->takeoverSession();
#endif
	if(Config.value("recorder/networkonly",true).toBool() && !takeoverSession){
		bool is_network = false;
		foreach(const ClientPlayer*player,ClientInstance->getPlayers()){
			is_network = player!=Self&&player->getState()!="robot";
			if(is_network) break;
		}
		if(!is_network)
			return;
	}

	QString filename;
	if (room && !room->getReplayPath().isEmpty())
		filename = room->getReplayPath();
	else {
#ifdef QSAN_XP_LEGACY
		// Stable ASCII paths also keep the XP snapshot sidecar portable across locales.
		filename = QSanRuntimePaths::recordDir()+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".txt";
#else
		filename = QSanRuntimePaths::recordDir()+"/"+QDateTime::currentDateTime().toString("yyyy年MM月dd日HH时mm分ss秒")+".txt";
#endif
	}
#ifdef QSAN_XP_LEGACY
	if (m_xpReplayExports.contains(QFileInfo(filename).absoluteFilePath())) return;
#endif
	if (!ClientInstance->save(filename))
		return;

#ifdef QSAN_XP_LEGACY
	if (owner) {
		finalizeLocalReplay(filename, false);
		return;
	}
#endif

	if (room) {
		QString error;
		if (!room->finalizeSnapshotManifest(filename, &error))
			qWarning().noquote() << "Replay snapshot manifest was not saved:" << error;
	}
}

#ifdef QSAN_XP_LEGACY
LocalServerController *RoomReplayController::localReplayController() const
{
	MainWindow *window = qobject_cast<MainWindow *>(m_mainWindow);
	LocalServerController *owner = window ? window->localServerController() : nullptr;
	return owner && owner->active() && !owner->hostOnly() && !m_xpReplayGeneration.isEmpty()
		&& owner->generation() == m_xpReplayGeneration ? owner : nullptr;
}

void RoomReplayController::finalizeLocalReplay(const QString &filename, bool reportFailure)
{
	LocalServerController *owner = localReplayController();
	const QString path = QFileInfo(filename).absoluteFilePath();
	m_xpReplayExports.insert(path, reportFailure);
	// Only the owned helper can access this game's authoritative snapshots.
	if (!owner || !Self || owner->finalizeReplay(path, Self->objectName()).isEmpty()) {
		m_xpReplayExports.remove(path);
		qWarning().noquote() << "Replay snapshot export could not be queued:" << path;
		if (reportFailure)
			QMessageBox::warning(m_mainWindow, tr("Save replay record"), tr("The local server is not ready to save takeover snapshots."));
	}
}
#endif
