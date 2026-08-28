#include "startscene.h"
#include "engine.h"
#include "audio.h"
#include "settings.h"
#include "button.h"
#include "effects/effects-policy.h"
#include "qsan-selectable-item.h"
#include "server.h"

StartScene::StartScene()
{
	// game logo
	logo = new QSanSelectableItem("image/logo/logo.png", true);
	logo->moveBy(0, -UiConfig.Rect.height() / 4.8);
	addItem(logo);

	//the website URL
	QString forum_url = "http://mogara.org";
	QFont website_font(UiConfig.SmallFont);
	website_font.setStyle(QFont::StyleItalic);
	QGraphicsSimpleTextItem *website_text = addSimpleText(forum_url, website_font);
	website_text->setBrush(Qt::white);
	website_text->setPos(UiConfig.Rect.width() / 2 - website_text->boundingRect().width(),
		UiConfig.Rect.height() / 2 - website_text->boundingRect().height());
	server_log = nullptr;
	m_server = nullptr;
	m_currentIndex = -1;
}

StartScene::~StartScene()
{
	delete logo;

	foreach (Button *b, buttons)
		delete b;
	buttons.clear();
}

void StartScene::addButton(QAction *action)
{
	Button *button = new Button(action->text());
	button->setMute(false);

	connect(button, SIGNAL(clicked()), action, SLOT(trigger()));
	addItem(button);

	QRectF rect = button->boundingRect();
	int n = buttons.length();
	if (n < 4)
		button->setPos(-rect.width() - 4, (n - 0.5) * (rect.height() * 1.2));
	else
		button->setPos(4, (n - 4.5) * (rect.height() * 1.2));

	buttons << button;
}

void StartScene::setServerLogBackground()
{
	if (server_log) {
		// make its background the same as background, looks transparent
		QPalette palette;
		palette.setBrush(QPalette::Base, backgroundBrush());
		server_log->setPalette(palette);
	}
}

void StartScene::switchToServer(Server *server)
{
	m_server = server;
#ifdef AUDIO_SUPPORT
	Audio::quit();
#endif
	// performs leaving animation
	QPropertyAnimation *logo_shift = new QPropertyAnimation(logo, "pos");
	logo_shift->setEndValue(QPointF(UiConfig.Rect.center().rx() - 200, UiConfig.Rect.center().ry() - 175));

	QPropertyAnimation *logo_shrink = new QPropertyAnimation(logo, "scale");
	logo_shrink->setEndValue(0.5);

	logo_shift->setDuration(G_EFFECTS.scaledDuration(logo_shift->duration()));
	logo_shrink->setDuration(G_EFFECTS.scaledDuration(logo_shrink->duration()));

	QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
	group->addAnimation(logo_shift);
	group->addAnimation(logo_shrink);
	G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
	group->start(QAbstractAnimation::DeleteWhenStopped);

	foreach(Button *button, buttons)
		delete button;
	buttons.clear();

	server_log = new QTextEdit();
	server_log->setReadOnly(true);
	server_log->resize(700, 420);
	server_log->move(-400, -180);
	server_log->setFrameShape(QFrame::NoFrame);
#ifdef Q_OS_LINUX
	server_log->setFont(QFont("DroidSansFallback", 12));
#else
	server_log->setFont(QFont("Verdana", 12));
#endif
	server_log->setStyleSheet(QString("QTextEdit { color: %1; }").arg(UiConfig.TextEditColor.name()));
	setServerLogBackground();
	addWidget(server_log);

	QFile file("qss/scroll.qss");
	if (file.open(QIODevice::ReadOnly)) {
		QTextStream stream(&file);
		server_log->verticalScrollBar()->setStyleSheet(stream.readAll());
	}

	printServerInfo();
	connect(server, SIGNAL(logMessage(QString)), server_log, SLOT(append(QString)));
	update();
	//QString logt = server_log->toPlainText();
}

void StartScene::printServerInfo()
{
	if (!m_server)
		return;
    foreach (const QString &message, m_server->startupMessages())
        server_log->append(message);
}

void StartScene::keyPressEvent(QKeyEvent *event)
{
	if (buttons.isEmpty()) {
		QGraphicsScene::keyPressEvent(event);
		return;
	}

	switch (event->key()) {
	case Qt::Key_Up:
		navigateUp();
		break;
	case Qt::Key_Down:
		navigateDown();
		break;
	case Qt::Key_Left:
		navigateLeft();
		break;
	case Qt::Key_Right:
		navigateRight();
		break;
	case Qt::Key_Enter:
	case Qt::Key_Return:
		if (m_currentIndex >= 0 && m_currentIndex < buttons.length()) {
			emit buttons.at(m_currentIndex)->clicked();
		}
		break;
	default:
		QGraphicsScene::keyPressEvent(event);
		break;
	}
}

void StartScene::selectButton(int index)
{
	if (index < 0 || index >= buttons.length()) return;

	if (m_currentIndex >= 0 && m_currentIndex < buttons.length()) {
		Button *oldBtn = buttons.at(m_currentIndex);
		oldBtn->setGlow(0);
		oldBtn->clearFocus();
	}

	m_currentIndex = index;
	Button *btn = buttons.at(index);
	btn->setGlow(5);
	btn->setFocus();
}

void StartScene::navigateUp()
{
	if (buttons.isEmpty()) return;

	if (m_currentIndex <= 0) {
		selectButton(buttons.length() - 1);
	} else {
		selectButton(m_currentIndex - 1);
	}
}

void StartScene::navigateDown()
{
	if (buttons.isEmpty()) return;

	if (m_currentIndex < 0 || m_currentIndex >= buttons.length() - 1) {
		selectButton(0);
	} else {
		selectButton(m_currentIndex + 1);
	}
}

void StartScene::navigateLeft()
{
	if (buttons.isEmpty()) return;

	int col = (m_currentIndex < 4) ? 0 : 1;
	int row = (m_currentIndex < 4) ? m_currentIndex : (m_currentIndex - 4);

	int targetIndex;
	if (col == 1) {
		targetIndex = row;
	} else {
		targetIndex = row - 1;
		if (targetIndex < 0) targetIndex = 3;
	}

	if (targetIndex >= buttons.length()) {
		targetIndex = buttons.length() - 1;
	}

	selectButton(targetIndex);
}

void StartScene::navigateRight()
{
	if (buttons.isEmpty()) return;

	int col = (m_currentIndex < 4) ? 0 : 1;
	int row = (m_currentIndex < 4) ? m_currentIndex : (m_currentIndex - 4);

	int targetIndex;
	if (col == 0) {
		targetIndex = 4 + row;
	} else {
		targetIndex = row + 1;
		if (targetIndex >= 4) targetIndex = 0;
	}

	if (targetIndex >= buttons.length()) {
		targetIndex = buttons.length() - 1;
	}

	selectButton(targetIndex);
}

