#ifndef EFFECTS_PROFILE_H
#define EFFECTS_PROFILE_H

#include <QString>
#include <QStringList>
#include <QVariant>

// The Linux GUI M2B-B effect profile contract.
//
// This header depends only on Qt Core: profile names, feature gates, duration
// scales and CLI / settings parsing are all pure functions, verifiable directly in
// CTest with no QApplication, no OpenGL and no art assets. The runtime facade that
// actually reads Config and counts is VisualEffectsPolicy (effects-policy.h).
//
// The three profiles only affect what is seen, never game rules or network replies:
//
//   Full     完整動畫 + Spine + GIF + QML 特效 + 影片
//   Reduced  保留必要狀態提示，縮短動畫，停用 Spine／影片／QML 全屏特效
//   None     all decorative animations complete instantly, no Spine / QMovie / video objects created
enum class EffectsProfile {
    Full,
    Reduced,
    None
};

class EffectsProfileContract
{
public:
    // User settings and the test CLI follow the same policy, so keys and flags are defined here once.
    static const char *const SettingsKey;         // "EffectsProfile"
    static const char *const FlagEffectsProfile;  // "--effects-profile"

    static EffectsProfile defaultProfile();       // Full

    static QString profileName(EffectsProfile profile);
    static QStringList profileNames();            // {"full","reduced","none"}
    // Case-insensitive, surrounding whitespace trimmed; an empty string means "unspecified", not an error.
    static bool parseProfileName(const QString &text, EffectsProfile *profile);

    // ── Feature gate ────────────────────────────────────────────────────
    // Everything can only narrow: a profile never re-enables what the user turned
    // off in settings; that logic is joined with Config inside VisualEffectsPolicy.
    static bool animationsEnabled(EffectsProfile profile);
    static bool spineEnabled(EffectsProfile profile);
    static bool gifEnabled(EffectsProfile profile);
    static bool videoEnabled(EffectsProfile profile);
    static bool qmlEffectsEnabled(EffectsProfile profile);
    // Purely decorative waits (e.g. "wait 444ms before showing the pindian result").
    // Reduced shortens them; None is always 0: game state must reach its final position immediately.
    static bool decorativeDelayAllowed(EffectsProfile profile);
    // Visual feedback of the "what you must do right now" kind: selection frames,
    // damage, card moves. Reduced always keeps these, or the player cannot see the pending action.
    static bool stateFeedbackEnabled(EffectsProfile profile);

    // ── Duration ────────────────────────────────────────────────────────
    static qreal durationScale(EffectsProfile profile);   // 1.0 / 0.3 / 0.0
    // Milliseconds after scaling. Reduced never compresses to 0 (0 makes Qt emit
    // finished() synchronously inside start(), causing reentry); None is always 0,
    // but None call sites should skip the animation entirely and finish via EffectsCompletion::completeNow().
    static int scaledDuration(EffectsProfile profile, int durationMs);

    // ── CLI ─────────────────────────────────────────────────────────────
    // 同時接受 "--effects-profile none" 同 "--effects-profile=none"。
    struct CliOverride {
        bool present = false;
        bool valid = false;
        EffectsProfile profile = EffectsProfile::Full;
        QString value;
        QString error;
    };
    static CliOverride parseCliOverride(const QStringList &arguments);

    // ── Resolution ──────────────────────────────────────────────────────
    // Priority: CLI override > user setting > default. An invalid CLI value falls
    // back to the setting, with the reason stated in error - silently treating it as unspecified is the hardest to debug.
    struct Resolution {
        EffectsProfile profile = EffectsProfile::Full;
        QString source;   // "cli" / "settings" / "default"
        QString error;    // non-empty = invalid CLI or setting value (already fell back to the next tier)
    };
    static Resolution resolve(const QStringList &arguments, const QVariant &settingsValue);
};

#endif
