#ifndef EFFECTS_POLICY_H
#define EFFECTS_POLICY_H

#include "effects-profile.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The only place in the GUI that asks "is this effect allowed".
//
// Never scatter this across UI code:
//
//     #ifdef Q_OS_LINUX
//         skipAnimation();
//     #endif
//
// Always ask this policy instead. A profile may only narrow: what the user turned
// off in settings, even the Full profile will not turn back on.
//
// Lifetime: main() calls initialize() after QApplication and before any UI.
// Call sites that ask before initialize() get the default profile (Full) and do not crash.
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
    // Reduced uses only the first frame: the objects are still created (the character frame must not vanish for lack of a GIF) but never played.
    bool gifPlaybackAllowed() const;
    bool videoEnabled() const;
    bool qmlEffectsEnabled() const;
    bool decorativeDelayAllowed() const;
    // Shortcut for profile == None: skip the animation, go straight to the final state + completeNow().
    bool immediate() const { return m_profile == EffectsProfile::None; }

    // Duration of purely decorative animations. None -> 0, Reduced -> noticeably shortened, Full -> original value.
    int scaledDuration(int durationMs) const;
    // Decorative delays (QTimer::singleShot and the like). None -> 0.
    int scaledDelay(int delayMs) const;

    // ── Instrumentation ─────────────────────────────────────────────────
    // The effects smoke uses these counters to verify that "NONE really creates no
    // Spine / QMovie / video objects" and that "REDUCED loads fewer high-cost effects than FULL".
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
