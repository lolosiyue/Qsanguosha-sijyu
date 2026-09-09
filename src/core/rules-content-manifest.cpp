#include "rules-content-manifest.h"

#include <QRegularExpression>
#include <QSet>

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
