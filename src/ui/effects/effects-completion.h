#ifndef EFFECTS_COMPLETION_H
#define EFFECTS_COMPLETION_H

#include <QObject>
#include <QPointer>

#include <functional>

class QAbstractAnimation;
class QTimer;

// The shared completion guarantee of all major effects: exactly once.
//
//   - animation finished  -> one callback
//   - animation skipped / not started -> one callback (queued, no reentry at the call site)
//   - context destroyed midway -> safe cancel, no callback into a dead object
//   - watchdog timeout    -> one callback (fallback when the animation is stuck)
//   - animation object destroyed -> one callback
//
// Neither a double callback nor a missing callback is allowed: this class is the
// reason the NONE profile cannot hang. Depends only on Qt Core, so CTest can verify it directly.
//
// Usage:
//     EffectsCompletion::whenFinished(animation, this, [this]{ continueFlow(); });
//     EffectsCompletion::completeNow(this, [this]{ continueFlow(); });
class EffectsCompletion
{
public:
    // A nullptr animation is equivalent to completeNow(). timeoutMs > 0 starts a
    // watchdog: if the animation does not finish in time it counts as complete (still exactly one callback).
    static void whenFinished(QAbstractAnimation *animation, QObject *context,
        std::function<void()> callback, int timeoutMs = 0);

    // Complete immediately, but always deliver through the event loop: a duration=0
    // animation emits finished() synchronously inside start(), and calling the
    // callback directly would reenter the call site before it returns (the root of double callbacks and use-after-free).
    static void completeNow(QObject *context, std::function<void()> callback);

    // Diagnostics: how many completions this process started / delivered / cancelled.
    // The effects smoke verifies two things with it:
    //   1. a skipped animation still delivers its callback (delivered never falls short);
    //   2. at shutdown issued == delivered + cancelled - i.e. no completion is
    //      left dangling. One dangling completion means one flow that waits forever.
    static quint64 issuedCount();
    static quint64 deliveredCount();
    static quint64 cancelledCount();
    static quint64 pendingCount();
    static void resetCounters();
};

// Guard used inside whenFinished(). Lives in the header because it needs Q_OBJECT (moc).
// Instantiating it directly is meaningless; always go through EffectsCompletion's static functions.
class EffectsCompletionGuard final : public QObject
{
    Q_OBJECT

public:
    // animation may be nullptr (the completeNow case): then completion relies only
    // on the context's lifetime plus a single queued deliverNow().
    EffectsCompletionGuard(QAbstractAnimation *animation, QObject *context,
        std::function<void()> callback, int timeoutMs);
    ~EffectsCompletionGuard() override;

public slots:
    // The queued entry point used by completeNow(). A no-op once it has run.
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
