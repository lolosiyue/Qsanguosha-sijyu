#include "update-catalog.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <algorithm>
#include <cmath>

namespace {
QString tr(const char *s) { return QCoreApplication::translate("UpdateCatalog", s); }
bool digest(const QString &s) {
    return QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(s).hasMatch();
}
bool numericSize(const QJsonValue &v, qint64 limit) {
    const double n = v.toDouble(-1);
    return v.isDouble() && n > 0 && n <= double(limit) && std::floor(n) == n;
}
bool basename(const QString &s) {
    return s.size() <= 180 && QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"))
        .match(s).hasMatch();
}
struct Version { bool valid = false, date = false; QList<quint64> parts; QString pre; };
Version version(QString s) {
    if (s.startsWith('v')) s.remove(0, 1);
    Version v;
    if (QRegularExpression(QStringLiteral("^[0-9]{8}$")).match(s).hasMatch()) {
        v.date = true;
        v.valid = QDate::fromString(s, QStringLiteral("yyyyMMdd")).isValid();
        v.parts << s.toULongLong();
        return v;
    }
    const auto m = QRegularExpression(QStringLiteral(
        "^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"))
        .match(s);
    if (!m.hasMatch() || s.size() > 128) return v;
    v.valid = true;
    for (int i = 1; i <= 3; ++i) v.parts << m.captured(i).toULongLong();
    v.pre = m.captured(4);
    for (const QString &part : v.pre.split('.'))
        if (QRegularExpression(QStringLiteral("^[0-9]+$")).match(part).hasMatch()
            && (part.size() > 9 || (part.size() > 1 && part.startsWith('0')))) v.valid = false;
    return v;
}
bool compatibleAsset(const QString &name, const QString &platform) {
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("server")) || n.contains(QStringLiteral("tui"))
        || QRegularExpression(QStringLiteral("(?:^|[-_.])(?:xp|winxp)(?:[-_.]|$)")).match(n).hasMatch()
        || n.contains(QStringLiteral("symbols")) || n.contains(QStringLiteral("arm64"))
        || n.contains(QStringLiteral("aarch64")) || n.contains(QStringLiteral("i686"))) return false;
    if (platform == QStringLiteral("linux-x86_64"))
        return ((n.contains(QStringLiteral("linux")) && n.contains(QStringLiteral("x86_64"))
                 && n.endsWith(QStringLiteral(".tar.zst")))
                || (n.contains(QStringLiteral("x86_64")) && n.endsWith(QStringLiteral(".appimage"))
                    && !n.contains(QStringLiteral("windows")) && !n.contains(QStringLiteral("win64"))
                    && !n.contains(QStringLiteral("macos")) && !n.contains(QStringLiteral("android"))));
    if (platform == QStringLiteral("windows-x86_64"))
        return (n.contains(QStringLiteral("windows")) || n.contains(QStringLiteral("win64")))
            && (n.contains(QStringLiteral("x64")) || n.contains(QStringLiteral("x86_64"))
                || n.contains(QStringLiteral("win64")))
            && (n.endsWith(QStringLiteral(".zip")) || n.endsWith(QStringLiteral(".msi"))
                || n.endsWith(QStringLiteral(".exe")));
    // APK ABI cannot be inferred safely from an arbitrary filename.
    return false;
}
}

namespace QSanUpdates {
bool safeHttps(const QUrl &url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userName().isEmpty() && url.password().isEmpty() && !url.hasFragment()
        && (url.port(-1) == -1 || url.port() == 443);
}
int compareVersions(const QString &a, const QString &b, bool *comparable) {
    const Version x = version(a), y = version(b);
    const bool ok = x.valid && y.valid && x.date == y.date;
    if (comparable) *comparable = ok;
    if (!ok) return 0;
    for (int i = 0; i < x.parts.size(); ++i)
        if (x.parts[i] != y.parts[i]) return x.parts[i] < y.parts[i] ? -1 : 1;
    if (x.pre == y.pre) return 0;
    if (x.pre.isEmpty()) return 1;
    if (y.pre.isEmpty()) return -1;
    const auto xp = x.pre.split('.'), yp = y.pre.split('.');
    for (int i = 0; i < qMin(xp.size(), yp.size()); ++i) {
        if (xp[i] == yp[i]) continue;
        const bool xn = QRegularExpression(QStringLiteral("^[0-9]+$")).match(xp[i]).hasMatch();
        const bool yn = QRegularExpression(QStringLiteral("^[0-9]+$")).match(yp[i]).hasMatch();
        if (xn != yn) return xn ? -1 : 1;
        if (xn) return xp[i].toULongLong() < yp[i].toULongLong() ? -1 : 1;
        return xp[i] < yp[i] ? -1 : 1;
    }
    return xp.size() < yp.size() ? -1 : 1;
}
QString platform() {
#ifdef Q_OS_WIN
    const QString os = QStringLiteral("windows");
#elif defined(Q_OS_ANDROID)
    const QString os = QStringLiteral("android");
#elif defined(Q_OS_LINUX)
    const QString os = QStringLiteral("linux");
#else
    const QString os = QStringLiteral("unsupported");
#endif
    return os + '-' + QSysInfo::buildCpuArchitecture();
}
Catalog materials(const QByteArray &json, const QString &gameVersion,
                  const QMap<QString, QString> &installed, const QMap<QString, QString> &pending) {
    Catalog result;
    QJsonParseError error;
    const auto doc = json.size() <= CatalogLimit ? QJsonDocument::fromJson(json, &error) : QJsonDocument();
    const auto root = doc.object();
    if (doc.isNull() || error.error != QJsonParseError::NoError || !doc.isObject()
        || root.value(QStringLiteral("schema_version")).toDouble(-1) != 1
        || !root.value(QStringLiteral("packages")).isArray()
        || root.value(QStringLiteral("packages")).toArray().size() > 1000) {
        result.error = tr("Invalid or oversized material catalog."); return result;
    }
    QSet<QString> ids;
    for (const auto &value : root.value(QStringLiteral("packages")).toArray()) {
        const auto o = value.toObject();
        Entry e;
        e.material = true;
        e.id = o.value(QStringLiteral("id")).toString();
        e.version = o.value(QStringLiteral("version")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.notes = o.value(QStringLiteral("notes")).toString();
        e.url = QUrl(o.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        e.sha256 = o.value(QStringLiteral("sha256")).toString();
        e.currentVersion = installed.value(e.id);
        if (!value.isObject() || !QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,127}$"))
            .match(e.id).hasMatch() || e.id == QStringLiteral("core") || ids.contains(e.id)
            || !version(e.version).valid || !safeHttps(e.url) || !digest(e.sha256)
            || !numericSize(o.value(QStringLiteral("size")), PackageLimit)
            || e.name.size() > 180 || e.notes.size() > 65536
            || (o.contains(QStringLiteral("name")) && !o.value(QStringLiteral("name")).isString())
            || (o.contains(QStringLiteral("notes")) && !o.value(QStringLiteral("notes")).isString())
            || !o.value(QStringLiteral("game_version")).isString()) {
            result.entries.clear(); result.error = tr("Invalid material package entry."); return result;
        }
        e.size = qint64(o.value(QStringLiteral("size")).toDouble());
        ids.insert(e.id);
        bool comparable = true;
        const int order = e.currentVersion.isEmpty() ? 1 : compareVersions(e.version, e.currentVersion, &comparable);
        e.downloadable = o.value(QStringLiteral("game_version")).toString() == gameVersion
            && comparable && order > 0 && !pending.contains(e.id);
        if (o.value(QStringLiteral("game_version")).toString() != gameVersion)
            e.reason = tr("This package requires a different game version.");
        else if (pending.contains(e.id)) e.reason = tr("A package change is already pending. Restart or manage packages first.");
        else if (!comparable) e.reason = tr("The installed version cannot be compared safely.");
        else if (order <= 0) e.reason = tr("Installed version is current or newer.");
        if (e.name.isEmpty()) e.name = e.id;
        result.entries << e;
    }
    return result;
}
Catalog releases(const QByteArray &json, const QString &currentVersion, const QString &platform,
                 bool includePrerelease) {
    Catalog result;
    QJsonParseError error;
    const auto doc = json.size() <= CatalogLimit ? QJsonDocument::fromJson(json, &error) : QJsonDocument();
    if (doc.isNull() || error.error != QJsonParseError::NoError || !doc.isArray()
        || doc.array().size() > 100) {
        result.error = tr("Invalid or oversized release catalog."); return result;
    }
    for (const auto &value : doc.array()) {
        if (!value.isObject()) { result.entries.clear(); result.error = tr("Invalid release entry."); return result; }
        const auto r = value.toObject();
        if (!r.value(QStringLiteral("draft")).isBool() || !r.value(QStringLiteral("prerelease")).isBool()
            || !r.value(QStringLiteral("tag_name")).isString() || !r.value(QStringLiteral("assets")).isArray()) {
            result.entries.clear(); result.error = tr("Invalid release entry."); return result;
        }
        if (r.value(QStringLiteral("draft")).toBool() || (!includePrerelease && r.value(QStringLiteral("prerelease")).toBool())) continue;
        const QString tag = r.value(QStringLiteral("tag_name")).toString();
        bool comparable;
        const int order = compareVersions(tag, currentVersion, &comparable);
        if (!comparable || order < 0 || (!includePrerelease && !version(tag).pre.isEmpty())) continue;
        Entry e;
        e.version = tag; e.currentVersion = currentVersion;
        e.name = r.value(QStringLiteral("name")).toString().left(180);
        if (e.name.isEmpty()) e.name = tag;
        e.notes = r.value(QStringLiteral("body")).toString().left(65536);
        e.releasePage = QUrl(r.value(QStringLiteral("html_url")).toString(), QUrl::StrictMode);
        if (!safeHttps(e.releasePage) || e.releasePage.host() != QStringLiteral("github.com")
            || !e.releasePage.path().startsWith(QStringLiteral("/lolosiyue/Qsanguosha-sijyu/releases/"))) continue;
        e.reason = tr("No compatible verified download. Use the release page for manual installation.");
        const auto assets = r.value(QStringLiteral("assets")).toArray();
        if (assets.size() > 100) { result.entries.clear(); result.error = tr("Invalid release entry."); return result; }
        for (const auto &asset : assets) {
            const auto a = asset.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            const QString d = a.value(QStringLiteral("digest")).toString();
            const QUrl url(a.value(QStringLiteral("browser_download_url")).toString(), QUrl::StrictMode);
            if (!basename(name) || !compatibleAsset(name, platform) || !d.startsWith(QStringLiteral("sha256:"))
                || !digest(d.mid(7)) || !numericSize(a.value(QStringLiteral("size")), GameLimit)
                || !safeHttps(url) || url.host() != QStringLiteral("github.com")
                || !url.path().startsWith(QStringLiteral("/lolosiyue/Qsanguosha-sijyu/releases/download/"))) continue;
            Entry candidate = e;
            candidate.name = name; candidate.url = url; candidate.sha256 = d.mid(7);
            candidate.size = qint64(a.value(QStringLiteral("size")).toDouble());
            candidate.downloadable = true; candidate.reason.clear();
            result.entries << candidate;
        }
        if (std::none_of(result.entries.cbegin(), result.entries.cend(), [&tag](const Entry &entry) { return entry.version == tag; }))
            result.entries << e;
    }
    std::stable_sort(result.entries.begin(), result.entries.end(), [](const Entry &a, const Entry &b) {
        return compareVersions(a.version, b.version) > 0;
    });
    return result;
}
}
