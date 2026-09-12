#include "rules-content-manifest.h"

#include <QRegularExpression>
#include <QSet>
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
    if (descriptor.value(QStringLiteral("schema_version")).toInt(-1) != 2
        || descriptor.value(QStringLiteral("profile")).toString() != QLatin1String("declared-v2")
        || !descriptor.value(QStringLiteral("extensions")).isArray()) {
        manifest.error = QStringLiteral("runtime-content.json has an invalid schema");
        return manifest;
    }
    const QJsonArray extensions = descriptor.value(QStringLiteral("extensions")).toArray();
    QSet<QString> names;
    QSet<QString> paths;
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
        if (entry.name.isEmpty() || names.contains(entry.name)) {
            manifest.error = QStringLiteral("extension %1 has a duplicate or empty name").arg(index);
            return manifest;
        }
        QString reason = validatePath(QStringLiteral("script"), entry.script);
        if (!reason.isEmpty() || paths.contains(entry.script)) {
            manifest.error = QStringLiteral("extension %1 has an invalid or duplicate script").arg(index);
            return manifest;
        }
        names.insert(entry.name);
        paths.insert(entry.script);
        const auto readPaths = [&](const QString &key, const QString &role,
                                   QStringList *target) -> bool {
            const QJsonValue value = object.value(key);
            if (value.isUndefined()) return true;
            if (!value.isArray()) return false;
            for (const auto &item : value.toArray()) {
                const QString path = item.toString();
                if (path.isEmpty() || paths.contains(path)) return false;
                const QString pathError = validatePath(role, path);
                if (!pathError.isEmpty()) return false;
                paths.insert(path);
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
    for (const auto &entry : manifest.entries)
        extensions.append(QJsonObject{{QStringLiteral("name"), entry.name},
            {QStringLiteral("script"), entry.script},
            {QStringLiteral("dependencies"), QJsonArray::fromStringList(entry.dependencies)},
            {QStringLiteral("libs"), QJsonArray::fromStringList(entry.libs)},
            {QStringLiteral("lang"), QJsonArray::fromStringList(entry.lang)},
            {QStringLiteral("ai"), QJsonArray::fromStringList(entry.ai)}});
    return {{QStringLiteral("schema_version"), 2},
            {QStringLiteral("profile"), QStringLiteral("declared-v2")},
            {QStringLiteral("extensions"), extensions}};
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
