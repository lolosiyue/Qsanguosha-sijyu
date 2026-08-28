#ifndef EFFECTS_POLICY_H
#define EFFECTS_POLICY_H

#include "effects-profile.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

// 全個 GUI 唯一問「呢個效果做唔做得」嘅地方。
//
// 唔准喺 UI code 散落
//
//     #ifdef Q_OS_LINUX
//         skipAnimation();
//     #endif
//
// 一律問呢個 policy。Profile 只可以收窄：使用者喺設定關咗嘅嘢，Full profile
// 都唔會幫佢開返。
//
// 生命周期：main() 喺 QApplication 之後、任何 UI 之前 call initialize()。
// 未 initialize 就問嘅 call site 會攞到預設 profile（Full），唔會 crash。
class VisualEffectsPolicy
{
public:
    static VisualEffectsPolicy &instance();

    // arguments 通常係 qApp->arguments()。CLI > 使用者設定 > 預設。
    void initialize(const QStringList &arguments);
    bool isInitialized() const { return m_initialized; }

    EffectsProfile profile() const { return m_profile; }
    QString profileName() const { return EffectsProfileContract::profileName(m_profile); }
    QString source() const { return m_source; }
    QString resolutionError() const { return m_error; }

    // 設定對話框用：即時生效，persist=true 會寫入 QSettings。
    void setProfile(EffectsProfile profile, bool persist);

    // ── Feature gate ────────────────────────────────────────────────────
    bool animationsEnabled() const;
    bool spineEnabled() const;
    bool gifEnabled() const;
    // Reduced 只用首幀：物件照建（角色框唔可以因為冇 GIF 就唔見），但唔會播。
    bool gifPlaybackAllowed() const;
    bool videoEnabled() const;
    bool qmlEffectsEnabled() const;
    bool decorativeDelayAllowed() const;
    // profile == None 嘅捷徑：唔好起動畫，直接到最終狀態 + completeNow()。
    bool immediate() const { return m_profile == EffectsProfile::None; }

    // 純裝飾嘅動畫時長。None → 0，Reduced → 明顯縮短，Full → 原值。
    int scaledDuration(int durationMs) const;
    // 裝飾性延遲（QTimer::singleShot 嗰類）。None → 0。
    int scaledDelay(int delayMs) const;

    // ── Instrumentation ─────────────────────────────────────────────────
    // effects smoke 靠呢啲數驗「NONE 真係冇建立 Spine／QMovie／video object」，
    // 同埋「REDUCED 載入嘅高成本效果比 FULL 少」。
    enum Counter {
        SpineItemsCreated,
        MovieObjectsCreated,
        QmlOverlaysCreated,
        VideoObjectsCreated,
        AnimationsStarted,
        AnimationsSkipped,
        DecorativeDelaysSkipped,
        CounterCount
    };
    void note(Counter counter);
    quint64 counter(Counter counter) const;
    void resetCounters();
    QJsonObject countersJson() const;
    QJsonObject describe() const;

private:
    VisualEffectsPolicy() = default;

    EffectsProfile m_profile = EffectsProfileContract::defaultProfile();
    QString m_source = QStringLiteral("default");
    QString m_error;
    bool m_initialized = false;
    quint64 m_counters[CounterCount] = {};
};

#define G_EFFECTS VisualEffectsPolicy::instance()

#endif
