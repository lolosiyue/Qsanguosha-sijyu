#include "rules-content-manifest.h"

#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>

namespace QSanRules {
namespace {
const QStringList &coreFiles()
{
    static const QStringList files{
        QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
        QStringLiteral("lua/utilities.lua"), QStringLiteral("lua/sgs_ex.lua"),
        QStringLiteral("lua/lib/json.lua")};
    return files;
}

bool traverses(const QString &path)
{
    if (path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')))
        return true;
    const auto segments = path.split(QLatin1Char('/'));
    for (const auto &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".")
            || segment == QLatin1String(".."))
            return true;
    }
    return false;
}

bool matches(const QString &path, const QRegularExpression &expression)
{
    return expression.match(path).hasMatch();
}

QString validatePath(const QString &role, const QString &path)
{
    if (traverses(path))
        return QStringLiteral("path traversal is not allowed");
    static const QRegularExpression scriptPattern(QStringLiteral("^extensions/[A-Za-z0-9_.-]+\\.lua$"));
    static const QRegularExpression libsPattern(QStringLiteral("^lua/[A-Za-z0-9_./-]+\\.lua$"));
    static const QRegularExpression langPattern(QStringLiteral("^lang/[A-Za-z0-9_./-]+\\.lua$"));
    static const QRegularExpression aiPattern(QStringLiteral("^lua/ai/[A-Za-z0-9_./-]+\\.lua$"));
    if (role == QLatin1String("script")) {
        if (!matches(path, scriptPattern))
            return QStringLiteral("script must be extensions/<name>.lua");
    } else if (role == QLatin1String("libs")) {
        if (!matches(path, libsPattern) || path.startsWith(QLatin1String("lua/ai/"))
            || coreFiles().contains(path)
            || path == QLatin1String("lua/lib/middleclass.lua"))
            return QStringLiteral("libs path is not extension library content");
    } else if (role == QLatin1String("lang")) {
        if (!matches(path, langPattern))
            return QStringLiteral("lang path must be under lang/");
    } else if (role == QLatin1String("ai")) {
        if (!matches(path, aiPattern) && path != QLatin1String("lua/lib/middleclass.lua"))
            return QStringLiteral("ai path is not server-only AI content");
    }
    return {};
}

bool validPackageId(const QString &id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
    return expression.match(id).hasMatch();
}

bool safeV3Path(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))
        || path.contains(QLatin1Char('%')) || path.contains(QLatin1Char('?'))
        || path.contains(QLatin1Char('#')))
        return false;
    for (const QChar character : path)
        if (character.unicode() < 0x20)
            return false;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::KeepEmptyParts))
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    return true;
}

bool validV3LuaPath(const QString &path)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_./-]+\\.lua$"));
    return safeV3Path(path) && expression.match(path).hasMatch();
}

bool validPackageAssetPath(const QString &path)
{
    return safeV3Path(path) && (path.startsWith(QLatin1String("image/"))
        || path.startsWith(QLatin1String("audio/")));
}

bool validatePackages(const QJsonArray &packages, QSet<QString> *ids, QString *error)
{
    QHash<QString, QStringList> dependenciesById;
    QHash<QString, QString> aliases;
    for (int index = 0; index < packages.size(); ++index) {
        const QJsonValue value = packages.at(index);
        if (!value.isObject()) {
            *error = QStringLiteral("package %1 must be an object").arg(index);
            return false;
        }
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString version = object.value(QStringLiteral("version")).toString();
        if (!validPackageId(id) || version.isEmpty() || ids->contains(id)) {
            *error = QStringLiteral("package %1 has an invalid id, version, or duplicate id").arg(index);
            return false;
        }
        ids->insert(id);
        if (!object.value(QStringLiteral("dependencies")).isArray()) {
            *error = QStringLiteral("package %1 dependencies must be an array").arg(index);
            return false;
        }
        QStringList dependencies;
        QSet<QString> seen;
        for (const QJsonValue &dependencyValue : object.value(QStringLiteral("dependencies")).toArray()) {
            if (!dependencyValue.isString()) {
                *error = QStringLiteral("package %1 has invalid dependencies").arg(index);
                return false;
            }
            const QString dependency = dependencyValue.toString();
            if (!validPackageId(dependency) || dependency == id || seen.contains(dependency)) {
                *error = QStringLiteral("package %1 has invalid dependencies").arg(index);
                return false;
            }
            seen.insert(dependency);
            dependencies.append(dependency);
        }
        dependenciesById.insert(id, dependencies);
        if (!object.value(QStringLiteral("assets")).isObject()) {
            *error = QStringLiteral("package %1 assets must be an object").arg(index);
            return false;
        }
        const QJsonObject assets = object.value(QStringLiteral("assets")).toObject();
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            const QString alias = it.key();
            const QString target = it.value().toString();
            if (!it.value().isString() || !validPackageAssetPath(alias)
                || !validPackageAssetPath(target)
                || (alias.startsWith(QLatin1String("image/")) != target.startsWith(QLatin1String("image/")))
                || aliases.contains(alias.toCaseFolded())) {
                *error = QStringLiteral("package %1 has invalid or colliding asset mapping: %2").arg(id, alias);
                return false;
            }
            aliases.insert(alias.toCaseFolded(), id);
        }
    }
    QSet<QString> emitted;
    for (int index = 0; index < packages.size(); ++index) {
        const QString id = packages.at(index).toObject().value(QStringLiteral("id")).toString();
        for (const QString &dependency : dependenciesById.value(id)) {
            if (!ids->contains(dependency) || !emitted.contains(dependency)) {
                *error = QStringLiteral("package order violates dependency: %1 -> %2").arg(id, dependency);
                return false;
            }
        }
        emitted.insert(id);
    }
    return true;
}

QString scriptName(const QString &script)
{
    const QString file = script.section(QLatin1Char('/'), -1);
    return file.left(file.size() - QStringLiteral(".lua").size());
}

QJsonArray canonicalEntries(const ContentManifest &manifest)
{
    QJsonArray entries;
    for (const auto &entry : manifest.entries) {
        // AI/lang/media are deliberately omitted: they are presentation or
        // server-owned policy and must not alter shared rules identity.
        QJsonArray dependencies = QJsonArray::fromStringList(entry.dependencies);
        QJsonArray libs = QJsonArray::fromStringList(entry.libs);
        entries.append(QJsonObject{{QStringLiteral("name"), entry.name},
            {QStringLiteral("script"), entry.script},
            {QStringLiteral("dependencies"), dependencies},
            {QStringLiteral("libs"), libs}});
    }
    return entries;
}
}

ContentManifest parseContentManifest(const QStringList &declared)
{
    ContentManifest manifest;
    QSet<QString> seen;
    for (int index = 0; index < declared.size(); ++index) {
        const auto fail = [&manifest, index](const QString &reason) {
            manifest.error = QStringLiteral("entry %1: %2").arg(index).arg(reason);
            return manifest;
        };
        const QStringList fields = declared.at(index).split(QLatin1Char(';'));
        ManifestEntry entry;
        entry.script = fields.value(0).trimmed();
        entry.name = scriptName(entry.script);
        QString reason = validatePath(QStringLiteral("script"), entry.script);
        if (!reason.isEmpty())
            return fail(reason);
        if (seen.contains(entry.script))
            return fail(QStringLiteral("duplicate path"));
        seen.insert(entry.script);

        QSet<QString> keys;
        for (int field = 1; field < fields.size(); ++field) {
            const QString text = fields.at(field).trimmed();
            const int equals = text.indexOf(QLatin1Char('='));
            const QString key = equals < 0 ? text : text.left(equals).trimmed();
            if (key != QLatin1String("libs") && key != QLatin1String("lang")
                && key != QLatin1String("ai"))
                return fail(QStringLiteral("unknown field key"));
            if (keys.contains(key))
                return fail(QStringLiteral("duplicate field key"));
            keys.insert(key);
            QStringList *target = key == QLatin1String("libs") ? &entry.libs
                : key == QLatin1String("lang") ? &entry.lang : &entry.ai;
            for (const QString &raw : text.mid(equals + 1).split(QLatin1Char(','))) {
                const QString path = raw.trimmed();
                reason = validatePath(key, path);
                if (!reason.isEmpty())
                    return fail(reason);
                if (seen.contains(path))
                    return fail(QStringLiteral("duplicate path"));
                seen.insert(path);
                target->append(path);
            }
            target->sort();
        }
        manifest.entries.append(entry);
    }
    return manifest;
}

ContentManifest parseRuntimeContent(const QJsonObject &descriptor)
{
    ContentManifest manifest;
    manifest.descriptorPresent = true;
    const int schema = descriptor.value(QStringLiteral("schema_version")).toInt(-1);
    const bool v3 = schema == 3
        && descriptor.value(QStringLiteral("profile")).toString() == QLatin1String("packages-v1");
    if ((!v3 && (schema != 2
        || descriptor.value(QStringLiteral("profile")).toString() != QLatin1String("declared-v2")))
        || !descriptor.value(QStringLiteral("extensions")).isArray()) {
        manifest.error = QStringLiteral("runtime-content.json has an invalid schema");
        return manifest;
    }
    QSet<QString> packageIds;
    if (v3) {
        if (!descriptor.value(QStringLiteral("packages")).isArray()) {
            manifest.error = QStringLiteral("runtime-content.json packages must be an array");
            return manifest;
        }
        manifest.packages = descriptor.value(QStringLiteral("packages")).toArray();
        if (!validatePackages(manifest.packages, &packageIds, &manifest.error))
            return manifest;
    }
    const QJsonArray extensions = descriptor.value(QStringLiteral("extensions")).toArray();
    QSet<QString> names;
    QSet<QString> foldedNames;
    QSet<QString> paths;
    QSet<QString> foldedPaths;
    for (int index = 0; index < extensions.size(); ++index) {
        const QJsonValue raw = extensions.at(index);
        if (!raw.isObject()) {
            manifest.error = QStringLiteral("extension %1 must be an object").arg(index);
            return manifest;
        }
        const QJsonObject object = raw.toObject();
        ManifestEntry entry;
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.script = object.value(QStringLiteral("script")).toString();
        if (entry.name.isEmpty() || names.contains(entry.name)
            || (v3 && foldedNames.contains(entry.name.toCaseFolded()))) {
            manifest.error = QStringLiteral("extension %1 has a duplicate or empty name").arg(index);
            return manifest;
        }
        const QString packageId = entry.script.startsWith(QLatin1String("packages/"))
            ? entry.script.section(QLatin1Char('/'), 1, 1) : QString();
        QString reason;
        if (v3 && entry.script.startsWith(QLatin1String("packages/"))) {
            const QString prefix = QStringLiteral("packages/%1/lua/").arg(packageId);
            if (!validPackageId(packageId) || !packageIds.contains(packageId)
                || !entry.script.startsWith(prefix) || !entry.script.endsWith(QLatin1String(".lua"))
                || entry.script.startsWith(prefix + QLatin1String("ai/"))
                || !validV3LuaPath(entry.script))
                reason = QStringLiteral("script must be under its declared package lua directory");
        } else {
            reason = validatePath(QStringLiteral("script"), entry.script);
            if (v3 && packageId.size())
                reason = QStringLiteral("package entry script must use packages/<id>/lua/");
        }
        if (!reason.isEmpty() || paths.contains(entry.script)
            || (v3 && foldedPaths.contains(entry.script.toCaseFolded()))) {
            manifest.error = QStringLiteral("extension %1 has an invalid or duplicate script").arg(index);
            return manifest;
        }
        names.insert(entry.name);
        foldedNames.insert(entry.name.toCaseFolded());
        paths.insert(entry.script);
        foldedPaths.insert(entry.script.toCaseFolded());
        const auto readPaths = [&](const QString &key, const QString &role,
                                   QStringList *target) -> bool {
            const QJsonValue value = object.value(key);
            if (value.isUndefined()) return true;
            if (!value.isArray()) return false;
            for (const auto &item : value.toArray()) {
                if (!item.isString()) return false;
                const QString path = item.toString();
                if (path.isEmpty() || paths.contains(path)
                    || (v3 && foldedPaths.contains(path.toCaseFolded()))) return false;
                QString pathError;
                if (v3 && path.startsWith(QLatin1String("packages/"))) {
                    const QString prefix = QStringLiteral("packages/%1/").arg(packageId);
                    const bool ownerOk = validPackageId(packageId) && packageIds.contains(packageId)
                        && path.startsWith(prefix) && validV3LuaPath(path);
                    const bool roleOk = role == QLatin1String("lang")
                        ? path.startsWith(QStringLiteral("packages/%1/translation/").arg(packageId))
                            && path.endsWith(QLatin1String(".lua"))
                        : role == QLatin1String("ai")
                        ? path.startsWith(prefix + QLatin1String("lua/ai/"))
                            && path.endsWith(QLatin1String(".lua"))
                        : path.startsWith(prefix + QLatin1String("lua/"))
                            && !path.startsWith(prefix + QLatin1String("lua/ai/"))
                            && path.endsWith(QLatin1String(".lua"));
                    if (!ownerOk || !roleOk) return false;
                } else if (v3 && packageId.size()) {
                    return false;
                } else {
                    pathError = validatePath(role, path);
                }
                if (!pathError.isEmpty()) return false;
                paths.insert(path);
                foldedPaths.insert(path.toCaseFolded());
                target->append(path);
            }
            target->sort();
            return true;
        };
        if (!readPaths(QStringLiteral("libs"), QStringLiteral("libs"), &entry.libs)
            || !readPaths(QStringLiteral("lang"), QStringLiteral("lang"), &entry.lang)
            || !readPaths(QStringLiteral("ai"), QStringLiteral("ai"), &entry.ai)) {
            manifest.error = QStringLiteral("extension %1 has invalid content paths").arg(index);
            return manifest;
        }
        const QJsonValue dependencies = object.value(QStringLiteral("dependencies"));
        if (!dependencies.isArray()) {
            manifest.error = QStringLiteral("extension %1 dependencies must be an array").arg(index);
            return manifest;
        }
        QSet<QString> seenDependencies;
        for (const auto &item : dependencies.toArray()) {
            const QString dependency = item.toString();
            if (dependency.isEmpty() || dependency == entry.name
                || seenDependencies.contains(dependency)) {
                manifest.error = QStringLiteral("extension %1 has invalid dependencies").arg(index);
                return manifest;
            }
            seenDependencies.insert(dependency);
            entry.dependencies.append(dependency);
        }
        manifest.entries.append(entry);
    }
    for (int index = 0; index < manifest.entries.size(); ++index) {
        for (const auto &dependency : manifest.entries.at(index).dependencies) {
            int dependencyIndex = -1;
            for (int candidate = 0; candidate < manifest.entries.size(); ++candidate)
                if (manifest.entries.at(candidate).name == dependency) dependencyIndex = candidate;
            if (dependencyIndex < 0 || dependencyIndex >= index) {
                manifest.error = QStringLiteral("extension order violates dependency: %1 -> %2")
                    .arg(manifest.entries.at(index).name, dependency);
                return manifest;
            }
        }
    }
    return manifest;
}

QJsonObject runtimeContentDescriptor(const ContentManifest &manifest)
{
    QJsonArray extensions;
    for (const auto &entry : manifest.entries) {
        extensions.append(QJsonObject{{QStringLiteral("name"), entry.name},
            {QStringLiteral("script"), entry.script},
            {QStringLiteral("dependencies"), QJsonArray::fromStringList(entry.dependencies)},
            {QStringLiteral("libs"), QJsonArray::fromStringList(entry.libs)},
            {QStringLiteral("lang"), QJsonArray::fromStringList(entry.lang)},
            {QStringLiteral("ai"), QJsonArray::fromStringList(entry.ai)}});
    }
    QJsonObject result{{QStringLiteral("schema_version"), manifest.packages.isEmpty() ? 2 : 3},
        {QStringLiteral("profile"), manifest.packages.isEmpty()
            ? QStringLiteral("declared-v2") : QStringLiteral("packages-v1")},
        {QStringLiteral("extensions"), extensions}};
    if (!manifest.packages.isEmpty())
        result.insert(QStringLiteral("packages"), manifest.packages);
    return result;
}

ContentManifest mergePackageContent(const ContentManifest &legacy,
                                    const QJsonArray &packageExtensions,
                                    const QJsonArray &packageDescriptors)
{
    if (!legacy.isValid()) return legacy;
    QJsonObject descriptor = runtimeContentDescriptor(legacy);
    QJsonArray entries = descriptor.value(QStringLiteral("extensions")).toArray();
    QHash<QString, int> slotByName;
    QSet<QString> mergedNames;
    for (int index = 0; index < entries.size(); ++index)
        slotByName.insert(entries.at(index).toObject().value(QStringLiteral("name")).toString(), index);
    for (const QJsonValue &value : packageExtensions) {
        if (!value.isObject()) {
            ContentManifest failed;
            failed.error = QStringLiteral("package extension must be an object");
            return failed;
        }
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            ContentManifest failed;
            failed.error = QStringLiteral("package extension has an empty name");
            return failed;
        }
        if (mergedNames.contains(name)) {
            ContentManifest failed;
            failed.error = QStringLiteral("duplicate package extension name: %1").arg(name);
            return failed;
        }
        mergedNames.insert(name);
        if (slotByName.contains(name))
            entries.replace(slotByName.value(name), value);
        else {
            slotByName.insert(name, entries.size());
            entries.append(value);
        }
    }
    descriptor.insert(QStringLiteral("schema_version"), 3);
    descriptor.insert(QStringLiteral("profile"), QStringLiteral("packages-v1"));
    descriptor.insert(QStringLiteral("extensions"), entries);
    descriptor.insert(QStringLiteral("packages"), packageDescriptors);
    return parseRuntimeContent(descriptor);
}

QByteArray runtimeContentCanonical(const ContentManifest &manifest)
{
    return QJsonDocument(canonicalEntries(manifest)).toJson(QJsonDocument::Compact);
}

QString runtimeContentDigest(const ContentManifest &manifest)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("qsan-runtime-content-v2\0") + runtimeContentCanonical(manifest),
        QCryptographicHash::Sha256).toHex());
}

QStringList manifestScripts(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.script;
    return result;
}

QStringList manifestHashedFiles(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.script << entry.libs;
    return result;
}

QStringList manifestDeliveredFiles(const ContentManifest &manifest)
{
    QStringList result = manifestHashedFiles(manifest);
    for (const auto &entry : manifest.entries)
        result << entry.lang;
    return result;
}

QStringList manifestServerOnlyFiles(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.ai;
    return result;
}
}
