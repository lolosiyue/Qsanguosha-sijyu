#ifndef EFFECTS_COMPLETION_H
#define EFFECTS_COMPLETION_H

#include <QObject>
#include <QPointer>

#include <functional>

class QAbstractAnimation;
class QTimer;

// 所有重要效果嘅共同 completion 保證：exactly once。
//
//   - 動畫播完            → callback 一次
//   - 動畫被跳過／未起動  → callback 一次（queued，唔會喺 call site 重入）
//   - context 中途銷毀    → 安全取消，唔會 callback 落死物
//   - watchdog timeout    → callback 一次（動畫卡死時嘅兜底）
//   - 動畫 object 被銷毀  → callback 一次
//
// 唔可以 double callback，亦唔可以永遠唔 callback：呢個 class 就係 NONE profile
// 唔會 hang 嘅根據。淨係依賴 Qt Core，所以 CTest 直接驗得到。
//
// 用法：
//     EffectsCompletion::whenFinished(animation, this, [this]{ continueFlow(); });
//     EffectsCompletion::completeNow(this, [this]{ continueFlow(); });
class EffectsCompletion
{
public:
    // animation 為 nullptr 時等同 completeNow()。timeoutMs > 0 會開一個
    // watchdog：動畫喺限期內未 finish 就當完成（callback 依然只派一次）。
    static void whenFinished(QAbstractAnimation *animation, QObject *context,
        std::function<void()> callback, int timeoutMs = 0);

    // 即時完成，但一定經 event loop 派返出去：duration=0 嘅動畫喺 start()
    // 入面同步 emit finished()，直接 call callback 會令 call site 喺自己
    // 未 return 之前就被重入（double callback、use-after-free 都由此而來）。
    static void completeNow(QObject *context, std::function<void()> callback);

    // 診斷用：呢個 process 開過／派過／取消過幾多個 completion。
    // effects smoke 靠佢驗兩件事：
    //   1. 跳咗動畫都一定收到 callback（delivered 唔會少）；
    //   2. 收檔嗰陣 issued == delivered + cancelled —— 即係冇一個 completion
    //      吊喺半空。有一個吊住就代表有一條流程永遠等唔到。
    static quint64 issuedCount();
    static quint64 deliveredCount();
    static quint64 cancelledCount();
    static quint64 pendingCount();
    static void resetCounters();
};

// whenFinished() 內部用嘅 guard。放喺 header 係因為佢要 Q_OBJECT（moc）。
// 直接 new 佢冇意義，一律行 EffectsCompletion 嘅 static function。
class EffectsCompletionGuard final : public QObject
{
    Q_OBJECT

public:
    // animation 可以係 nullptr（completeNow 嘅情況）：咁樣就只靠 context
    // 嘅生死同一次 queued deliverNow() 收工。
    EffectsCompletionGuard(QAbstractAnimation *animation, QObject *context,
        std::function<void()> callback, int timeoutMs);
    ~EffectsCompletionGuard() override;

public slots:
    // completeNow() 用嘅 queued 入口。行過一次之後就係 no-op。
    void deliverNow();

private:
    void fire();
    void cancel();

    bool m_fired = false;
    QPointer<QObject> m_context;
    std::function<void()> m_callback;
    QTimer *m_watchdog = nullptr;
};

#endif
