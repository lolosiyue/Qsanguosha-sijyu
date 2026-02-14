#include "timed-progressbar.h"
#include "clientstruct.h"
#include "skin-bank.h"

void TimedProgressBar::show()
{
    m_mutex.lock();
    if (!m_hasTimer || m_max <= 0) {
        m_mutex.unlock();
        return;
    }
    if (m_timer != 0) {
        killTimer(m_timer);
        m_timer = 0;
    }
    m_timer = startTimer(m_step);
    this->setMaximum(m_max);
    this->setValue(m_val);
    QProgressBar::show();
    m_mutex.unlock();
}

void TimedProgressBar::hide()
{
    m_mutex.lock();
    if (m_timer != 0) {
        killTimer(m_timer);
        m_timer = 0;
    }
    m_mutex.unlock();
    QProgressBar::hide();
}

void TimedProgressBar::timerEvent(QTimerEvent *)
{
    bool emitTimeout = false;
    bool doHide = false;
    int val = 0;
    m_mutex.lock();
    m_val += m_step;
    if (m_val >= m_max) {
        m_val = m_max;
        if (m_autoHide)
            doHide = true;
        else {
            killTimer(m_timer);
            m_timer = 0;
        }
        emitTimeout = true;
    }
    val = m_val;
    m_mutex.unlock();
    this->setValue(val);
    if (doHide) hide();
    if (emitTimeout) emit timedOut();
}

using namespace QSanProtocol;

QSanCommandProgressBar::QSanCommandProgressBar()
{
    m_step = Config.S_PROGRESS_BAR_UPDATE_INTERVAL;
    m_hasTimer = ServerInfo.OperationTimeout > 0;
    m_instanceType = S_CLIENT_INSTANCE;
}

void QSanCommandProgressBar::setCountdown(CommandType command)
{
    m_mutex.lock();
    m_max = ServerInfo.getCommandTimeout(command, m_instanceType);
    m_mutex.unlock();
}

void QSanCommandProgressBar::paintEvent(QPaintEvent *)
{
    m_mutex.lock();
    int val = this->m_val;
    int max = this->m_max;
    m_mutex.unlock();
    int width = this->width();
    int height=this->height();
    QPainter painter(this);
    if (orientation() == Qt::Vertical) {
        painter.translate(0, height);
        qSwap(width, height);
        painter.rotate(-90);
    }
    QPixmap progBg = G_ROOM_SKIN.getProgressBarPixmap(0);
    painter.drawPixmap(0, 0, width, height, progBg);
    double percent = 1 - (double)val / max;
    QPixmap prog = G_ROOM_SKIN.getProgressBarPixmap((int)(percent * 100));
    int drawWidth = percent * prog.width();
    painter.drawPixmap(0, 0, percent * width, height, prog, 0, 0, drawWidth, prog.height());
}

void QSanCommandProgressBar::setCountdown(Countdown countdown)
{
    m_mutex.lock();
    m_hasTimer = (countdown.type != Countdown::S_COUNTDOWN_NO_LIMIT);
    m_max = countdown.max;
    m_val = countdown.current;
    m_mutex.unlock();
}

//#include <QGraphicsScene>
//#include <QGraphicsProxyWidget>
//#include <QTimer>

TimerLabel::TimerLabel(QGraphicsScene *const scene, QWidget *const parent)
    : QLabel(parent), m_proxyWidget(nullptr), m_timer(nullptr), m_paused(false)
{
    if (scene) {
        setObjectName("GameTimer");
        m_proxyWidget = scene->addWidget(this);
        m_proxyWidget->setParent(scene);

        resetTimeSpan();
        showTimeFmtStr();

        m_timer = new QTimer(this);
        connect(m_timer, SIGNAL(timeout()), this, SLOT(recordTiming()));
    }
}

TimerLabel::~TimerLabel()
{
    if (m_timer)
        m_timer->stop();
}

void TimerLabel::setPos(const QPointF &pos)
{
    if (m_proxyWidget)
        m_proxyWidget->setPos(pos);
}

void TimerLabel::start()
{
    if (m_timer) {
        resetTimeSpan();
        showTimeFmtStr();

        m_timer->start(1000);
        m_paused = false;
    }
}

void TimerLabel::pause()
{
    if (m_timer) {
        m_timer->stop();
        m_paused = true;
    }
}

void TimerLabel::resume()
{
    if (m_timer && m_paused) {
        m_timer->start(1000);
        m_paused = false;
    }
}

void TimerLabel::stop()
{
    if (m_timer) {
        m_timer->stop();
        m_paused = false;
    }
}

void TimerLabel::showTimeFmtStr()
{
    setText(m_timeSpan.toString("hh:mm:ss"));
}

void TimerLabel::resetTimeSpan()
{
    m_timeSpan.setHMS(0, 0, 0);
}

void TimerLabel::recordTiming()
{
    m_timeSpan = m_timeSpan.addSecs(1);
    showTimeFmtStr();
}

