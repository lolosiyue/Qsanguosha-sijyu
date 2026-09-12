#include "effects-profile.h"

#include <QtGlobal>
#include <QtMath>

const char *const EffectsProfileContract::SettingsKey = "EffectsProfile";
const char *const EffectsProfileContract::FlagEffectsProfile = "--effects-profile";

namespace {

// Reduced is not "slightly faster" but "noticeably shorter": 0.3 turns a 600ms card
// move into 180ms - you can still see where the card went, without the wait.
const qreal kReducedDurationScale = 0.3;

}

EffectsProfile EffectsProfileContract::defaultProfile()
{
    return EffectsProfile::Full;
}

QString EffectsProfileContract::profileName(EffectsProfile profile)
{
    switch (profile) {
    case EffectsProfile::Full:
        return QStringLiteral("full");
    case EffectsProfile::Reduced:
        return QStringLiteral("reduced");
    case EffectsProfile::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("full");
}

QStringList EffectsProfileContract::profileNames()
{
    return QStringList{
        QStringLiteral("full"),
        QStringLiteral("reduced"),
        QStringLiteral("none")
    };
}

bool EffectsProfileContract::parseProfileName(const QString &text, EffectsProfile *profile)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized.isEmpty())
        return false;
    if (normalized == QLatin1String("full")) {
        if (profile) *profile = EffectsProfile::Full;
        return true;
    }
    if (normalized == QLatin1String("reduced")) {
        if (profile) *profile = EffectsProfile::Reduced;
        return true;
    }
    if (normalized == QLatin1String("none")) {
        if (profile) *profile = EffectsProfile::None;
        return true;
    }
    return false;
}

bool EffectsProfileContract::animationsEnabled(EffectsProfile profile)
{
    return profile != EffectsProfile::None;
}

bool EffectsProfileContract::spineEnabled(EffectsProfile profile)
{
    // Spine is the most expensive kind of effect (skeleton + atlas texture + GL
    // context); Reduced skips it too, keeping only the static portrait.
    return profile == EffectsProfile::Full;
}

bool EffectsProfileContract::gifEnabled(EffectsProfile profile)
{
    // Reduced 保留 QMovie 但只會用首幀（見 VisualEffectsPolicy::gifPlaybackAllowed）。
    return profile != EffectsProfile::None;
}

bool EffectsProfileContract::videoEnabled(EffectsProfile profile)
{
    return profile == EffectsProfile::Full;
}

bool EffectsProfileContract::qmlEffectsEnabled(EffectsProfile profile)
{
    // 全屏 QML 技能特效同 Spine 一樣係 overlay 級數的開銷。
    return profile == EffectsProfile::Full;
}

bool EffectsProfileContract::decorativeDelayAllowed(EffectsProfile profile)
{
    return profile != EffectsProfile::None;
}

bool EffectsProfileContract::stateFeedbackEnabled(EffectsProfile)
{
    // All three profiles still provide state feedback - None just reaches the final
    // state immediately; that is not the same as showing nothing.
    return true;
}

qreal EffectsProfileContract::durationScale(EffectsProfile profile)
{
    switch (profile) {
    case EffectsProfile::Full:
        return 1.0;
    case EffectsProfile::Reduced:
        return kReducedDurationScale;
    case EffectsProfile::None:
        return 0.0;
    }
    return 1.0;
}

int EffectsProfileContract::scaledDuration(EffectsProfile profile, int durationMs)
{
    if (durationMs <= 0)
        return 0;
    if (profile == EffectsProfile::None)
        return 0;
    const int scaled = qRound(durationMs * durationScale(profile));
    // Reduced must not drop to 0: a zero-duration animation emits finished()
    // synchronously inside QAbstractAnimation::start(), reentering the call site.
    return qMax(1, scaled);
}

EffectsProfileContract::CliOverride
EffectsProfileContract::parseCliOverride(const QStringList &arguments)
{
    CliOverride result;
    const QString flag = QString::fromLatin1(FlagEffectsProfile);
    const QString inlinePrefix = flag + QLatin1Char('=');

    QString raw;
    bool sawFlag = false;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument.startsWith(inlinePrefix)) {
            sawFlag = true;
            raw = argument.mid(inlinePrefix.size());
            continue;  // later occurrences override earlier ones, consistent with other flags in this repo
        }
        if (argument == flag) {
            sawFlag = true;
            raw = (i + 1 < arguments.size()) ? arguments.at(i + 1) : QString();
            // If the next token is another flag, treat it as "no value given"
            if (raw.startsWith(QLatin1String("--")))
                raw.clear();
            continue;
        }
    }

    if (!sawFlag)
        return result;

    result.present = true;
    result.value = raw;
    if (raw.trimmed().isEmpty()) {
        result.error = QStringLiteral("%1 requires a value (%2)")
            .arg(flag, profileNames().join(QLatin1Char('/')));
        return result;
    }
    EffectsProfile parsed = EffectsProfile::Full;
    if (!parseProfileName(raw, &parsed)) {
        result.error = QStringLiteral("unknown effects profile '%1' (expected %2)")
            .arg(raw.trimmed(), profileNames().join(QLatin1Char('/')));
        return result;
    }
    result.valid = true;
    result.profile = parsed;
    return result;
}

EffectsProfileContract::Resolution
EffectsProfileContract::resolve(const QStringList &arguments, const QVariant &settingsValue)
{
    Resolution resolution;
    resolution.profile = defaultProfile();
    resolution.source = QStringLiteral("default");

    EffectsProfile fromSettings = defaultProfile();
    bool settingsUsable = false;
    QString settingsError;
    const QString settingsText = settingsValue.toString().trimmed();
    if (!settingsText.isEmpty()) {
        if (parseProfileName(settingsText, &fromSettings)) {
            settingsUsable = true;
        } else {
            settingsError = QStringLiteral("unknown effects profile '%1' in settings (expected %2)")
                .arg(settingsText, profileNames().join(QLatin1Char('/')));
        }
    }

    if (settingsUsable) {
        resolution.profile = fromSettings;
        resolution.source = QStringLiteral("settings");
    }
    resolution.error = settingsError;

    const CliOverride cli = parseCliOverride(arguments);
    if (cli.present) {
        if (cli.valid) {
            resolution.profile = cli.profile;
            resolution.source = QStringLiteral("cli");
            // Once the CLI override succeeds, stop carrying the settings error - it is irrelevant now.
            resolution.error = settingsError;
        } else {
            resolution.error = cli.error;
        }
    }

    return resolution;
}
