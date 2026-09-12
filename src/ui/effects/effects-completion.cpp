#include "effects-completion.h"

#include <QAbstractAnimation>
#include <QMetaObject>
#include <QTimer>

namespace {

quint64 g_issuedCount = 0;
quint64 g_deliveredCount = 0;
quint64 g_cancelledCount = 0;

}

EffectsCompletionGuard::EffectsCompletionGuard(QAbstractAnimation *animation, QObject *context,
    std::function<void()> callback, int timeoutMs)
    : QObject(nullptr)
    , m_context(context)
    , m_callback(std::move(callback))
{
    // Deliberately not a child of the animation: the callback often deletes that
    // animation, and reparenting / being torn down midway through the parent's
    // destructor is far into undefined territory. The guard cleans up via the connections below; no path leaves it behind.

    if (animation) {
        // 1. 動畫播完 → 派一次。
        connect(animation, &QAbstractAnimation::finished, this, [this]() { fire(); });

        // 2. The animation is deleted while playing (scene shutdown, the same effect
        //    being reconfigured, DeleteWhenStopped) -> still must deliver, otherwise the flow waiting on it waits forever.
        connect(animation, &QObject::destroyed, this, [this]() { fire(); });
    }

    // 3. context died -> cancel, never call back into a dead object. This connection
    //    is also the only way to make "cancelled" observable: a queued event silently dropped by Qt is mute.
    if (context)
        connect(context, &QObject::destroyed, this, [this]() { cancel(); });

    if (timeoutMs > 0) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        m_watchdog->setInterval(timeoutMs);
        connect(m_watchdog, &QTimer::timeout, this, [this]() { fire(); });
        m_watchdog->start();
    }
}

EffectsCompletionGuard::~EffectsCompletionGuard()
{
    // Neither delivered nor cancelled counts as cancelled. Normally unreachable -
    // it only happens if the event loop winds down before deleteLater() runs.
    if (!m_fired) {
        m_fired = true;
        ++g_cancelledCount;
    }
}

void EffectsCompletionGuard::deliverNow()
{
    fire();
}

void EffectsCompletionGuard::fire()
{
    if (m_fired)
        return;
    m_fired = true;
    if (m_watchdog)
        m_watchdog->stop();
    // No need to disconnect: m_fired already makes any later finished / destroyed /
    // timeout path a no-op, and Qt cleans up automatically after deleteLater().

    if (m_context.isNull()) {
        ++g_cancelledCount;
        deleteLater();
        return;
    }

    ++g_deliveredCount;
    std::function<void()> callback;
    callback.swap(m_callback);
    // The callback may delete the animation; the guard is not its child, so this is safe.
    deleteLater();
    if (callback)
        callback();
}

void EffectsCompletionGuard::cancel()
{
    if (m_fired)
        return;
    m_fired = true;
    ++g_cancelledCount;
    m_callback = nullptr;
    deleteLater();
}

void EffectsCompletion::whenFinished(QAbstractAnimation *animation, QObject *context,
    std::function<void()> callback, int timeoutMs)
{
    if (animation == nullptr) {
        // completeNow() records issued itself; do not double-count here.
        completeNow(context, std::move(callback));
        return;
    }
    ++g_issuedCount;
    new EffectsCompletionGuard(animation, context, std::move(callback), timeoutMs);
}

void EffectsCompletion::completeNow(QObject *context, std::function<void()> callback)
{
    if (!callback)
        return;
    ++g_issuedCount;
    if (context == nullptr) {
        ++g_cancelledCount;
        return;
    }
    // Must go through the guard; a plain invokeMethod onto the context will not do:
    // if the context dies while queued, Qt silently drops the event and "cancelled"
    // becomes unobservable. The guard's context-destroyed connection records it.
    auto *guard = new EffectsCompletionGuard(nullptr, context, std::move(callback), 0);
    // QueuedConnection: must not deliver on the caller's stack - reentry is
    // exactly the old duration=0 animation problem.
    QMetaObject::invokeMethod(guard, "deliverNow", Qt::QueuedConnection);
}

quint64 EffectsCompletion::issuedCount()
{
    return g_issuedCount;
}

quint64 EffectsCompletion::deliveredCount()
{
    return g_deliveredCount;
}

quint64 EffectsCompletion::pendingCount()
{
    const quint64 settled = g_deliveredCount + g_cancelledCount;
    return g_issuedCount > settled ? g_issuedCount - settled : 0;
}

quint64 EffectsCompletion::cancelledCount()
{
    return g_cancelledCount;
}

void EffectsCompletion::resetCounters()
{
    g_issuedCount = 0;
    g_deliveredCount = 0;
    g_cancelledCount = 0;
}
