#include "asset-manifest.h"

#include "runtime-paths.h"
#include "version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVariantList>

namespace
{
void collect(const QJsonObject &object, const QString &key, bool required,
             const QDir &root, QList<QSanAssetManifest::Entry> &entries)
{
    const QJsonArray array = object.value(key).toArray();
    for (const QJsonValue &value : array) {
        const QString path = value.toString().trimmed();
        if (path.isEmpty())
            continue;
        QSanAssetManifest::Entry entry;
        entry.path = path;
        entry.required = required;
        // 目錄同檔案都要算，所以用 exists() 而唔係 QFile::exists()。
        entry.present = QFileInfo::exists(root.filePath(path));
        entries.append(entry);
    }
}
}

namespace QSanAssetManifest
{
QStringList Report::missingRequired() const
{
    QStringList missing;
    for (const Entry &entry : entries) {
        if (entry.required && !entry.present)
            missing.append(entry.path);
    }
    return missing;
}

QStringList Report::missingOptional() const
{
    QStringList missing;
    for (const Entry &entry : entries) {
        if (!entry.required && !entry.present)
            missing.append(entry.path);
    }
    return missing;
}

Report inspect(const QString &assetRoot, const QString &manifestPath)
{
    Report report;
    report.assetRoot = assetRoot.isEmpty() ? QSanRuntimePaths::assetRoot() : assetRoot;
    report.gameVersion = QString::fromLatin1(QSanVersion::Number);
    if (report.assetRoot.isEmpty()) {
        report.error = QStringLiteral("no asset root has been resolved");
        return report;
    }

    const QDir root(report.assetRoot);
    report.manifestPath = manifestPath.isEmpty()
        ? root.filePath(QStringLiteral("assets-manifest.json"))
        : QFileInfo(manifestPath).absoluteFilePath();
    QFile file(report.manifestPath);
    if (!file.exists()) {
        // 冇 manifest 唔係錯：開發樹同舊有部署都冇。呢個時候唔會扮成
        // 「所有資產齊全」，而係老實講「冇 manifest 可以驗」。
        return report;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        report.error = QStringLiteral("unable to read %1: %2")
            .arg(report.manifestPath, file.errorString());
        return report;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        report.error = QStringLiteral("%1 is not a valid asset manifest: %2")
            .arg(report.manifestPath, parseError.errorString());
        return report;
    }

    report.manifestPresent = true;
    const QJsonObject object = document.object();
    report.schemaVersion = object.value(QStringLiteral("schema_version")).toInt();
    if (report.schemaVersion != 1) {
        report.error = QStringLiteral("unsupported asset manifest schema_version %1 (expected 1)")
            .arg(report.schemaVersion);
        return report;
    }
    const QString declaredGameVersion =
        object.value(QStringLiteral("game_version")).toString().trimmed();
    if (!declaredGameVersion.isEmpty())
        report.gameVersion = declaredGameVersion;
    report.assetPackVersion =
        object.value(QStringLiteral("asset_pack_version")).toString().trimmed();

    collect(object, QStringLiteral("required_paths"), true, root, report.entries);
    collect(object, QStringLiteral("optional_paths"), false, root, report.entries);
    return report;
}

QStringList diagnostics(const Report &report)
{
    QStringList lines;
    lines.append(QStringLiteral("asset root: %1").arg(report.assetRoot));
    if (!report.error.isEmpty()) {
        lines.append(QStringLiteral("asset manifest error: %1").arg(report.error));
        return lines;
    }
    if (!report.manifestPresent) {
        lines.append(QStringLiteral(
            "asset manifest: not found (%1) — asset completeness cannot be verified")
                         .arg(report.manifestPath));
        return lines;
    }
    lines.append(QStringLiteral("asset manifest: %1 (game %2, asset pack %3)")
                     .arg(report.manifestPath, report.gameVersion,
                          report.assetPackVersion.isEmpty() ? QStringLiteral("unspecified")
                                                            : report.assetPackVersion));
    const QStringList missingRequired = report.missingRequired();
    const QStringList missingOptional = report.missingOptional();
    if (!missingRequired.isEmpty()) {
        lines.append(QStringLiteral("MISSING REQUIRED (%1): %2")
                         .arg(missingRequired.size())
                         .arg(missingRequired.join(QStringLiteral(", "))));
        lines.append(QStringLiteral(
            "This installation is incomplete; reinstall the package or pass --asset-root."));
    }
    if (!missingOptional.isEmpty()) {
        // 大型美術／音訊包唔喺 core package 入面，缺咗係常態。
        lines.append(QStringLiteral("missing optional (%1): %2")
                         .arg(missingOptional.size())
                         .arg(missingOptional.join(QStringLiteral(", "))));
        lines.append(QStringLiteral(
            "Optional content is absent; the game runs with placeholder visuals and no voice-over."));
    }
    if (missingRequired.isEmpty() && missingOptional.isEmpty())
        lines.append(QStringLiteral("all manifest paths are present"));
    return lines;
}

QVariantMap describe(const Report &report)
{
    QVariantMap map;
    map.insert(QStringLiteral("schema_version"), report.schemaVersion);
    map.insert(QStringLiteral("manifest_present"), report.manifestPresent);
    map.insert(QStringLiteral("manifest_path"), report.manifestPath);
    map.insert(QStringLiteral("asset_root"), report.assetRoot);
    map.insert(QStringLiteral("game_version"), report.gameVersion);
    map.insert(QStringLiteral("asset_pack_version"), report.assetPackVersion);
    map.insert(QStringLiteral("complete"), report.complete());
    if (!report.error.isEmpty())
        map.insert(QStringLiteral("error"), report.error);
    QVariantList present;
    QVariantList missingRequired;
    QVariantList missingOptional;
    for (const Entry &entry : report.entries) {
        if (entry.present)
            present.append(entry.path);
        else if (entry.required)
            missingRequired.append(entry.path);
        else
            missingOptional.append(entry.path);
    }
    map.insert(QStringLiteral("present"), present);
    map.insert(QStringLiteral("missing_required"), missingRequired);
    map.insert(QStringLiteral("missing_optional"), missingOptional);
    return map;
}
}
