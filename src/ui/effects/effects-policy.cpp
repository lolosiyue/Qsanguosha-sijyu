#include "effects-policy.h"

#include "settings.h"

#include <QDebug>

VisualEffectsPolicy &VisualEffectsPolicy::instance()
{
    static VisualEffectsPolicy policy;
    return policy;
}

void VisualEffectsPolicy::initialize(const QStringList &arguments)
{
#ifdef QSAN_XP_LEGACY
    Q_UNUSED(arguments);
    m_profile = EffectsProfile::None;
    m_source = QStringLiteral("xp-forced");
    m_error.clear();
    m_initialized = true;
    qInfo("[effects] profile=none source=xp-forced");
    return;
#endif

    const EffectsProfileContract::Resolution resolution =
        EffectsProfileContract::resolve(arguments,
            Config.value(QString::fromLatin1(EffectsProfileContract::SettingsKey)));

    m_profile = resolution.profile;
    m_source = resolution.source;
    m_error = resolution.error;
    m_initialized = true;

    if (!m_error.isEmpty())
        qWarning("[effects] %s", qPrintable(m_error));
    qInfo("[effects] profile=%s source=%s", qPrintable(profileName()), qPrintable(m_source));
}

void VisualEffectsPolicy::setProfile(EffectsProfile profile, bool persist)
{
#ifdef QSAN_XP_LEGACY
    Q_UNUSED(profile);
    Q_UNUSED(persist);
    m_profile = EffectsProfile::None;
    m_source = QStringLiteral("xp-forced");
    m_error.clear();
    m_initialized = true;
    return;
#endif

    m_profile = profile;
    m_source = QStringLiteral("settings");
    m_error.clear();
    m_initialized = true;
    if (persist) {
        Config.setValue(QString::fromLatin1(EffectsProfileContract::SettingsKey),
            EffectsProfileContract::profileName(profile));
    }
}

bool VisualEffectsPolicy::animationsEnabled() const
{
    return EffectsProfileContract::animationsEnabled(m_profile);
}

bool VisualEffectsPolicy::spineEnabled() const
{
    return EffectsProfileContract::spineEnabled(m_profile);
}

bool VisualEffectsPolicy::gifEnabled() const
{
    // 使用者嘅 EnableAnimatedGenerals 依然係最終否決權；profile 只可以再收窄。
    return EffectsProfileContract::gifEnabled(m_profile)
        && Config.value(QStringLiteral("EnableAnimatedGenerals"), true).toBool();
}

bool VisualEffectsPolicy::gifPlaybackAllowed() const
{
    return gifEnabled() && m_profile == EffectsProfile::Full;
}

bool VisualEffectsPolicy::videoEnabled() const
{
    return EffectsProfileContract::videoEnabled(m_profile) && Config.EnableBackgroundVideo;
}

bool VisualEffectsPolicy::qmlEffectsEnabled() const
{
    return EffectsProfileContract::qmlEffectsEnabled(m_profile);
}

bool VisualEffectsPolicy::decorativeDelayAllowed() const
{
    return EffectsProfileContract::decorativeDelayAllowed(m_profile);
}

int VisualEffectsPolicy::scaledDuration(int durationMs) const
{
    return EffectsProfileContract::scaledDuration(m_profile, durationMs);
}

int VisualEffectsPolicy::scaledDelay(int delayMs) const
{
    if (!decorativeDelayAllowed())
        return 0;
    return EffectsProfileContract::scaledDuration(m_profile, delayMs);
}

void VisualEffectsPolicy::note(Counter counter)
{
    if (counter >= 0 && counter < CounterCount)
        ++m_counters[counter];
}

quint64 VisualEffectsPolicy::counter(Counter counter) const
{
    if (counter >= 0 && counter < CounterCount)
        return m_counters[counter];
    return 0;
}

void VisualEffectsPolicy::resetCounters()
{
    for (int i = 0; i < CounterCount; ++i)
        m_counters[i] = 0;
}

QJsonObject VisualEffectsPolicy::countersJson() const
{
    QJsonObject counters;
    counters.insert(QStringLiteral("spine_items"),
        static_cast<double>(m_counters[SpineItemsCreated]));
    counters.insert(QStringLiteral("movie_objects"),
        static_cast<double>(m_counters[MovieObjectsCreated]));
    counters.insert(QStringLiteral("qml_overlays"),
        static_cast<double>(m_counters[QmlOverlaysCreated]));
    counters.insert(QStringLiteral("video_objects"),
        static_cast<double>(m_counters[VideoObjectsCreated]));
    counters.insert(QStringLiteral("animations_started"),
        static_cast<double>(m_counters[AnimationsStarted]));
    counters.insert(QStringLiteral("animations_skipped"),
        static_cast<double>(m_counters[AnimationsSkipped]));
    counters.insert(QStringLiteral("decorative_delays_skipped"),
        static_cast<double>(m_counters[DecorativeDelaysSkipped]));
    return counters;
}

QJsonObject VisualEffectsPolicy::describe() const
{
    QJsonObject description;
    description.insert(QStringLiteral("profile"), profileName());
    description.insert(QStringLiteral("source"), m_source);
    description.insert(QStringLiteral("initialized"), m_initialized);
    if (!m_error.isEmpty())
        description.insert(QStringLiteral("error"), m_error);
    description.insert(QStringLiteral("animations"), animationsEnabled());
    description.insert(QStringLiteral("spine"), spineEnabled());
    description.insert(QStringLiteral("gif"), gifEnabled());
    description.insert(QStringLiteral("gif_playback"), gifPlaybackAllowed());
    description.insert(QStringLiteral("video"), videoEnabled());
    description.insert(QStringLiteral("qml_effects"), qmlEffectsEnabled());
    description.insert(QStringLiteral("decorative_delay"), decorativeDelayAllowed());
    description.insert(QStringLiteral("duration_scale"),
        EffectsProfileContract::durationScale(m_profile));
    return description;
}
