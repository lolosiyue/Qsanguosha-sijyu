// Linux GUI M2B-B 效果 profile 的契約測試。
//
// 只依賴 Qt Core：profile 解析、feature gate、duration scale、CLI override 同
// exactly-once completion 都唔應該要開 QApplication、OpenGL 或者任何美術資產先
// 驗到。真正嘅 RoomScene／Spine／QMovie 行為由 --effects-smoke 喺 Xvfb 下驗。
#include "effects-completion.h"
#include "effects-profile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        printf("PASS %s\n", what);
        return;
    }
    printf("FAIL %s\n", what);
    ++failures;
}

// 只有一個 qreal property 嘅最小動畫目標，唔使拉 GUI 入嚟。
class AnimationTarget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal value READ value WRITE setValue)

public:
    qreal value() const { return m_value; }
    void setValue(qreal value) { m_value = value; }

private:
    qreal m_value = 0.0;
};

// 行 event loop 直到 predicate 成立或者夠鐘。completeNow() 係 queued 嘅，
// 所以每個等待都一定要真係入過 event loop。
template <typename Predicate>
void spin(Predicate predicate, int budgetMs = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < budgetMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    // 再多行一轉，令 deleteLater 之類嘅 deferred event 有機會落地。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// ── profile 名稱同預設 ───────────────────────────────────────────────────

void testProfileNames()
{
    check(EffectsProfileContract::defaultProfile() == EffectsProfile::Full,
        "default profile is full");
    check(EffectsProfileContract::profileName(EffectsProfile::Full) == QLatin1String("full"),
        "full profile name is stable");
    check(EffectsProfileContract::profileName(EffectsProfile::Reduced) == QLatin1String("reduced"),
        "reduced profile name is stable");
    check(EffectsProfileContract::profileName(EffectsProfile::None) == QLatin1String("none"),
        "none profile name is stable");
    check(EffectsProfileContract::profileNames().size() == 3,
        "exactly three profiles exist");
    check(QLatin1String(EffectsProfileContract::SettingsKey) == QLatin1String("EffectsProfile"),
        "settings key is stable");
    check(QLatin1String(EffectsProfileContract::FlagEffectsProfile)
            == QLatin1String("--effects-profile"),
        "CLI flag is stable");
}

void testProfileParsing()
{
    EffectsProfile parsed = EffectsProfile::Full;
    check(EffectsProfileContract::parseProfileName(QStringLiteral("none"), &parsed)
            && parsed == EffectsProfile::None,
        "parses 'none'");
    check(EffectsProfileContract::parseProfileName(QStringLiteral("  REDUCED "), &parsed)
            && parsed == EffectsProfile::Reduced,
        "parsing trims and is case insensitive");
    check(!EffectsProfileContract::parseProfileName(QStringLiteral("low"), &parsed),
        "rejects an unknown profile name");
    check(!EffectsProfileContract::parseProfileName(QString(), &parsed),
        "rejects an empty profile name");
    check(!EffectsProfileContract::parseProfileName(QStringLiteral("nonexistent"), nullptr),
        "tolerates a null out parameter");
}

// ── feature gate ─────────────────────────────────────────────────────────

void testFeatureGates()
{
    check(EffectsProfileContract::animationsEnabled(EffectsProfile::Full),
        "full enables animations");
    check(EffectsProfileContract::animationsEnabled(EffectsProfile::Reduced),
        "reduced keeps animations");
    check(!EffectsProfileContract::animationsEnabled(EffectsProfile::None),
        "none disables animations");

    check(EffectsProfileContract::spineEnabled(EffectsProfile::Full),
        "full enables spine");
    check(!EffectsProfileContract::spineEnabled(EffectsProfile::Reduced),
        "reduced disables spine");
    check(!EffectsProfileContract::spineEnabled(EffectsProfile::None),
        "none disables spine");

    check(EffectsProfileContract::gifEnabled(EffectsProfile::Full),
        "full enables gif");
    check(EffectsProfileContract::gifEnabled(EffectsProfile::Reduced),
        "reduced still creates gif objects (first frame only)");
    check(!EffectsProfileContract::gifEnabled(EffectsProfile::None),
        "none creates no QMovie at all");

    check(EffectsProfileContract::videoEnabled(EffectsProfile::Full),
        "full enables video");
    check(!EffectsProfileContract::videoEnabled(EffectsProfile::Reduced),
        "reduced disables video");
    check(!EffectsProfileContract::videoEnabled(EffectsProfile::None),
        "none disables video");

    check(EffectsProfileContract::qmlEffectsEnabled(EffectsProfile::Full),
        "full enables qml overlays");
    check(!EffectsProfileContract::qmlEffectsEnabled(EffectsProfile::Reduced),
        "reduced disables qml overlays");
    check(!EffectsProfileContract::qmlEffectsEnabled(EffectsProfile::None),
        "none disables qml overlays");

    check(EffectsProfileContract::decorativeDelayAllowed(EffectsProfile::Reduced),
        "reduced still allows (shortened) decorative delays");
    check(!EffectsProfileContract::decorativeDelayAllowed(EffectsProfile::None),
        "none allows no decorative delay");

    // 三個 profile 都要保留狀態回饋 —— 否則玩家會睇唔到 pending action。
    check(EffectsProfileContract::stateFeedbackEnabled(EffectsProfile::Full)
            && EffectsProfileContract::stateFeedbackEnabled(EffectsProfile::Reduced)
            && EffectsProfileContract::stateFeedbackEnabled(EffectsProfile::None),
        "every profile keeps state feedback");
}

// ── duration scale ───────────────────────────────────────────────────────

void testDurationScale()
{
    check(qFuzzyCompare(EffectsProfileContract::durationScale(EffectsProfile::Full), 1.0),
        "full keeps the original duration");
    check(EffectsProfileContract::durationScale(EffectsProfile::Reduced) < 0.5
            && EffectsProfileContract::durationScale(EffectsProfile::Reduced) > 0.0,
        "reduced shortens durations significantly but not to zero");
    check(qFuzzyIsNull(EffectsProfileContract::durationScale(EffectsProfile::None)),
        "none scales durations to zero");

    check(EffectsProfileContract::scaledDuration(EffectsProfile::Full, 600) == 600,
        "full leaves 600ms alone");
    check(EffectsProfileContract::scaledDuration(EffectsProfile::Reduced, 600) == 180,
        "reduced turns 600ms into 180ms");
    check(EffectsProfileContract::scaledDuration(EffectsProfile::None, 600) == 0,
        "none turns 600ms into 0ms");

    // 呢個係防重入嘅關鍵：Reduced 唔可以壓到 0，因為 zero-duration 動畫喺
    // QAbstractAnimation::start() 入面就同步 emit finished()。
    check(EffectsProfileContract::scaledDuration(EffectsProfile::Reduced, 1) == 1,
        "reduced never scales a positive duration down to zero");
    check(EffectsProfileContract::scaledDuration(EffectsProfile::Reduced, 2) == 1,
        "reduced clamps very short durations to 1ms");
    check(EffectsProfileContract::scaledDuration(EffectsProfile::Full, 0) == 0,
        "a zero duration stays zero");
    check(EffectsProfileContract::scaledDuration(EffectsProfile::Full, -5) == 0,
        "a negative duration is normalised to zero");
}

// ── CLI override ─────────────────────────────────────────────────────────

void testCliOverride()
{
    {
        const auto cli = EffectsProfileContract::parseCliOverride(QStringList{});
        check(!cli.present, "no flag means no override");
    }
    {
        const auto cli = EffectsProfileContract::parseCliOverride(
            QStringList{ QStringLiteral("--effects-profile"), QStringLiteral("none") });
        check(cli.present && cli.valid && cli.profile == EffectsProfile::None,
            "separate-token form parses");
    }
    {
        const auto cli = EffectsProfileContract::parseCliOverride(
            QStringList{ QStringLiteral("--effects-profile=reduced") });
        check(cli.present && cli.valid && cli.profile == EffectsProfile::Reduced,
            "inline '=' form parses");
    }
    {
        const auto cli = EffectsProfileContract::parseCliOverride(
            QStringList{ QStringLiteral("--effects-profile"), QStringLiteral("turbo") });
        check(cli.present && !cli.valid && !cli.error.isEmpty(),
            "an unknown value is reported, not silently ignored");
    }
    {
        const auto cli = EffectsProfileContract::parseCliOverride(
            QStringList{ QStringLiteral("--effects-profile") });
        check(cli.present && !cli.valid && !cli.error.isEmpty(),
            "a missing value is reported");
    }
    {
        // 下一個 token 又係 flag：當冇畀值，唔好食咗人哋個 flag。
        const auto cli = EffectsProfileContract::parseCliOverride(
            QStringList{ QStringLiteral("--effects-profile"), QStringLiteral("--effects-smoke") });
        check(cli.present && !cli.valid,
            "a following flag is not consumed as the value");
    }
    {
        const auto cli = EffectsProfileContract::parseCliOverride(QStringList{
            QStringLiteral("--effects-profile"), QStringLiteral("full"),
            QStringLiteral("--effects-profile"), QStringLiteral("none") });
        check(cli.valid && cli.profile == EffectsProfile::None,
            "the last occurrence wins");
    }
}

// ── resolution：CLI > settings > default ─────────────────────────────────

void testResolution()
{
    {
        const auto resolution = EffectsProfileContract::resolve(QStringList{}, QVariant());
        check(resolution.profile == EffectsProfile::Full
                && resolution.source == QLatin1String("default")
                && resolution.error.isEmpty(),
            "no settings and no CLI resolves to the default");
    }
    {
        const auto resolution = EffectsProfileContract::resolve(QStringList{},
            QVariant(QStringLiteral("reduced")));
        check(resolution.profile == EffectsProfile::Reduced
                && resolution.source == QLatin1String("settings"),
            "a stored setting is honoured");
    }
    {
        const auto resolution = EffectsProfileContract::resolve(
            QStringList{ QStringLiteral("--effects-profile"), QStringLiteral("none") },
            QVariant(QStringLiteral("full")));
        check(resolution.profile == EffectsProfile::None
                && resolution.source == QLatin1String("cli"),
            "the CLI override beats the stored setting");
    }
    {
        const auto resolution = EffectsProfileContract::resolve(
            QStringList{ QStringLiteral("--effects-profile"), QStringLiteral("bogus") },
            QVariant(QStringLiteral("reduced")));
        check(resolution.profile == EffectsProfile::Reduced
                && resolution.source == QLatin1String("settings")
                && !resolution.error.isEmpty(),
            "a bad CLI value falls back to settings and reports why");
    }
    {
        const auto resolution = EffectsProfileContract::resolve(QStringList{},
            QVariant(QStringLiteral("sparkly")));
        check(resolution.profile == EffectsProfile::Full
                && resolution.source == QLatin1String("default")
                && !resolution.error.isEmpty(),
            "a bad stored setting falls back to the default and reports why");
    }
    {
        // 設定壞咗但 CLI 啱：照跑 CLI 嗰個 profile。
        const auto resolution = EffectsProfileContract::resolve(
            QStringList{ QStringLiteral("--effects-profile=none") },
            QVariant(QStringLiteral("sparkly")));
        check(resolution.profile == EffectsProfile::None
                && resolution.source == QLatin1String("cli"),
            "a valid CLI override survives a corrupt setting");
    }
}

// ── exactly-once completion ──────────────────────────────────────────────

void testCompletionOnFinish()
{
    AnimationTarget target;
    int calls = 0;
    QPropertyAnimation *animation = new QPropertyAnimation(&target, "value");
    animation->setDuration(20);
    animation->setEndValue(1.0);
    EffectsCompletion::whenFinished(animation, &target, [&calls]() { ++calls; });
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    spin([&calls]() { return calls > 0; });
    check(calls == 1, "a finished animation delivers exactly one completion");

    // 動畫 DeleteWhenStopped 之後仲會 emit destroyed —— 唔可以再派多一次。
    spin([]() { return false; }, 60);
    check(calls == 1, "the animation's later destruction does not re-deliver");
}

void testCompletionOnDestroyDuringAnimation()
{
    AnimationTarget target;
    int calls = 0;
    QPropertyAnimation *animation = new QPropertyAnimation(&target, "value");
    animation->setDuration(60000);   // 唔會自然播完
    animation->setEndValue(1.0);
    EffectsCompletion::whenFinished(animation, &target, [&calls]() { ++calls; });
    animation->start();

    // 播到一半拆咗個動畫：等緊佢嘅流程一定要繼續，唔可以永遠等唔到。
    delete animation;
    spin([&calls]() { return calls > 0; });
    check(calls == 1, "destroying a running animation still delivers one completion");
}

void testCompletionCancelledWhenContextDies()
{
    int calls = 0;
    AnimationTarget *context = new AnimationTarget;
    QPropertyAnimation *animation = new QPropertyAnimation(context, "value");
    animation->setDuration(30);
    animation->setEndValue(1.0);
    EffectsCompletion::whenFinished(animation, context, [&calls]() { ++calls; });
    animation->start();

    // context 死咗：callback 唔可以派落死物。
    delete context;
    spin([]() { return false; }, 120);
    check(calls == 0, "a completion is cancelled when its context dies");
    delete animation;
    spin([]() { return false; }, 30);
    check(calls == 0, "destroying the animation afterwards still delivers nothing");
}

void testCompletionTimeoutFallback()
{
    AnimationTarget target;
    int calls = 0;
    QPropertyAnimation *animation = new QPropertyAnimation(&target, "value");
    animation->setDuration(60000);   // 卡死嘅動畫
    animation->setEndValue(1.0);
    EffectsCompletion::whenFinished(animation, &target, [&calls]() { ++calls; }, 30);
    animation->start();

    spin([&calls]() { return calls > 0; });
    check(calls == 1, "a stalled animation completes once via the watchdog");
    animation->stop();
    delete animation;
    spin([]() { return false; }, 60);
    check(calls == 1, "the watchdog fallback is not followed by a second delivery");
}

void testCompleteNowIsQueuedAndExactlyOnce()
{
    AnimationTarget context;
    int calls = 0;
    bool reentered = false;
    bool returned = false;

    EffectsCompletion::completeNow(&context, [&]() {
        ++calls;
        // 呢個 callback 唔可以喺 completeNow() 未 return 之前就行 ——
        // 重入正正就係 duration=0 動畫嘅老問題。
        if (!returned)
            reentered = true;
    });
    returned = true;
    check(calls == 0, "completeNow does not run its callback synchronously");

    spin([&calls]() { return calls > 0; });
    check(calls == 1 && !reentered, "completeNow delivers exactly once, from the event loop");

    spin([]() { return false; }, 40);
    check(calls == 1, "completeNow never delivers twice");
}

void testCompleteNowDroppedWhenContextDies()
{
    int calls = 0;
    AnimationTarget *context = new AnimationTarget;
    EffectsCompletion::completeNow(context, [&calls]() { ++calls; });
    delete context;
    spin([]() { return false; }, 80);
    check(calls == 0, "a queued completion is dropped when its context dies first");
}

void testCompletionWithoutAnimation()
{
    AnimationTarget context;
    int calls = 0;
    EffectsCompletion::whenFinished(nullptr, &context, [&calls]() { ++calls; });
    spin([&calls]() { return calls > 0; });
    check(calls == 1, "a null animation still completes exactly once");
}

void testCompletionCounters()
{
    EffectsCompletion::resetCounters();
    check(EffectsCompletion::issuedCount() == 0 && EffectsCompletion::deliveredCount() == 0
            && EffectsCompletion::cancelledCount() == 0,
        "counters reset to zero");

    AnimationTarget context;
    int calls = 0;
    EffectsCompletion::completeNow(&context, [&calls]() { ++calls; });
    check(EffectsCompletion::issuedCount() == 1, "an issued completion is counted immediately");
    check(EffectsCompletion::pendingCount() == 1, "an unsettled completion counts as pending");
    spin([&calls]() { return calls > 0; });
    check(EffectsCompletion::deliveredCount() == 1, "a delivery is counted");
    check(EffectsCompletion::pendingCount() == 0, "a delivered completion is no longer pending");

    AnimationTarget *doomed = new AnimationTarget;
    EffectsCompletion::completeNow(doomed, [&calls]() { ++calls; });
    delete doomed;
    spin([]() { return false; }, 60);
    check(EffectsCompletion::cancelledCount() >= 1, "a cancellation is counted");
    check(EffectsCompletion::deliveredCount() == 1, "a cancellation is not counted as a delivery");
    // 呢個先至係 anti-hang 嘅可量度形式：開過幾多個，就要 settle 幾多個。
    check(EffectsCompletion::pendingCount() == 0,
        "every issued completion settles as either delivered or cancelled");
    check(EffectsCompletion::issuedCount()
            == EffectsCompletion::deliveredCount() + EffectsCompletion::cancelledCount(),
        "issued == delivered + cancelled");

    // whenFinished(nullptr) 會轉交畀 completeNow()：唔可以計兩次 issued。
    const quint64 before = EffectsCompletion::issuedCount();
    EffectsCompletion::whenFinished(nullptr, &context, [&calls]() { ++calls; });
    check(EffectsCompletion::issuedCount() == before + 1,
        "a null animation issues exactly one completion, not two");
    spin([]() { return false; }, 60);
    check(EffectsCompletion::pendingCount() == 0, "it settles too");
}

}

#include "effects-profile-test.moc"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testProfileNames();
    testProfileParsing();
    testFeatureGates();
    testDurationScale();
    testCliOverride();
    testResolution();

    testCompletionOnFinish();
    testCompletionOnDestroyDuringAnimation();
    testCompletionCancelledWhenContextDies();
    testCompletionTimeoutFallback();
    testCompleteNowIsQueuedAndExactlyOnce();
    testCompleteNowDroppedWhenContextDies();
    testCompletionWithoutAnimation();
    testCompletionCounters();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all effects profile checks passed\n");
    return 0;
}
