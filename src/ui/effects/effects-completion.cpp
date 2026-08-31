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
    // 特登唔做 animation 嘅 child：callback 好多時會 delete 嗰個動畫，喺
    // parent 嘅 destructor 中途改 parent／被拆係 undefined 得滯。guard 靠下面
    // 幾條 connection 自己收工，冇一條路徑會留低佢。

    if (animation) {
        // 1. 動畫播完 → 派一次。
        connect(animation, &QAbstractAnimation::finished, this, [this]() { fire(); });

        // 2. 動畫喺播緊嘅時候俾人 delete（scene shutdown、同一個效果重新設定、
        //    DeleteWhenStopped）→ 一樣要派，否則等緊佢嘅流程永遠等唔到。
        connect(animation, &QObject::destroyed, this, [this]() { fire(); });
    }

    // 3. context 死咗 → 取消，唔好 callback 落死物。呢條 connection 亦係
    //    「取消」變成可觀察嘅唯一辦法：單靠 queued event 被 Qt 丟棄係靜音嘅。
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
    // 冇派過亦冇取消過就當取消。正常情況下唔會行到呢度 —— 只有 event loop
    // 喺 deleteLater() 之前收檔先會。
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
    // 唔使拆 connection：m_fired 已經令 finished／destroyed／timeout 任何
    // 一條之後嘅路徑變成 no-op，而 deleteLater() 之後 Qt 會自動清走。

    if (m_context.isNull()) {
        ++g_cancelledCount;
        deleteLater();
        return;
    }

    ++g_deliveredCount;
    std::function<void()> callback;
    callback.swap(m_callback);
    // callback 可能會 delete 動畫；guard 唔係佢嘅 child，所以呢度安全。
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
        // completeNow() 自己會記 issued，唔可以喺呢度重複計。
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
    // 一定要行 guard，唔可以就咁 invokeMethod 落 context：context 喺排隊期間
    // 死咗嘅話 Qt 只會靜靜丟棄個 event，「取消咗」就變成觀察唔到。guard 嘅
    // context-destroyed connection 令佢記得低。
    auto *guard = new EffectsCompletionGuard(nullptr, context, std::move(callback), 0);
    // QueuedConnection：唔可以喺 caller 嘅 stack 上面派 —— 重入正正就係
    // duration=0 動畫嘅老問題。
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
