#include "theme-pack.h"
#include "json.h"
#include "runtime-paths.h"
#include "settings.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <atomic>

namespace ThemePacks
{
namespace
{
const char *const kEnabledKey = "ThemePacks/Enabled";
const char *const kManifestName = "theme.json";

struct State
{
    bool loaded = false;
    QList<Pack> packs;
    QStringList enabled;
    QHash<QString, QString> slotOverrides;
    QHash<QString, QString> keyOverrides;
    QHash<QString, QString> fileOverrides;
    // Legacy folder prefix -> theme folders, highest priority first; longest prefix first.
    QList<QPair<QString, QStringList>> folderOverrides;
};

QReadWriteLock g_lock;
State g_state;
std::atomic<bool> g_active{false};
std::atomic<quint64> g_revision{0};

// Folder lookups stat the disk, so remember each answer until the next rebuild.
QMutex g_folderCacheMutex;
QHash<QString, QString> g_folderCache;

QList<Slot> loadSlotTable()
{
    QList<Slot> result;
    const QString path = QSanRuntimePaths::assetPath(QStringLiteral("skins/theme-slots.json"));
    const JsonDocument doc = JsonDocument::fromFilePath(path);
    if (!doc.isValid() || !doc.isObject()) {
        qWarning().noquote() << "Theme slots: cannot read" << path << doc.errorString();
        return result;
    }
    const QVariantList entries = doc.object().value(QStringLiteral("slots")).toList();
    for (const QVariant &entry : entries) {
        const JsonObject object = entry.toMap();
        Slot slot;
        slot.id = object.value(QStringLiteral("id")).toString();
        if (slot.id.isEmpty())
            continue;
        slot.label = object.value(QStringLiteral("label"), slot.id).toString();
        slot.group = object.value(QStringLiteral("group")).toString();
        slot.note = object.value(QStringLiteral("note")).toString();
        slot.directory = object.value(QStringLiteral("kind")).toString() == QLatin1String("directory");
        slot.defaultPath = object.value(QStringLiteral("default")).toString();
        slot.redirect = object.value(QStringLiteral("redirect")).toBool();
        slot.skinKeys = object.value(QStringLiteral("skinKeys")).toStringList();
        const QVariantList size = object.value(QStringLiteral("size")).toList();
        if (size.size() == 2) {
            slot.width = size.at(0).toInt();
            slot.height = size.at(1).toInt();
        }
        result << slot;
    }
    return result;
}

// "image/system/x.png", "./image\\system/x.png" and "<asset root>/image/system/x.png" all
// name the same legacy asset; return it relative and case-folded, keeping a trailing '/'.
QString normalizeLegacy(const QString &path)
{
    if (path.isEmpty())
        return QString();
    QString result = QDir::fromNativeSeparators(path);
    const bool folder = result.endsWith(QLatin1Char('/'));
    result = QDir::cleanPath(result);
    if (QDir::isAbsolutePath(result)) {
        const QString root = QSanRuntimePaths::assetRoot();
        if (root.isEmpty())
            return QString();
        const QString prefix = QDir::cleanPath(root) + QLatin1Char('/');
        if (!result.startsWith(prefix))
            return QString();
        result = result.mid(prefix.size());
    }
    if (result.startsWith(QLatin1String("./")))
        result = result.mid(2);
    if (folder)
        result += QLatin1Char('/');
    return result.toCaseFolded();
}

// A manifest path inside the pack folder, or empty when it escapes the folder or is missing.
QString containedPath(const QString &root, const QString &relative, bool folder)
{
    if (relative.isEmpty())
        return QString();
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(relative));
    if (QDir::isAbsolutePath(clean) || clean == QLatin1String("..") || clean.startsWith(QLatin1String("../")))
        return QString();
    const QFileInfo info(QDir(root).filePath(clean));
    if (!info.exists() || info.isDir() != folder)
        return QString();
    // Symlinks may point anywhere; only files really inside the pack count.
    const QString canonical = info.canonicalFilePath();
    if (!canonical.startsWith(root + QLatin1Char('/')))
        return QString();
    return folder ? canonical + QLatin1Char('/') : canonical;
}

void addFolderOverride(State &state, const QString &legacyFolder, const QString &themeFolder)
{
    QString prefix = normalizeLegacy(legacyFolder);
    if (prefix.isEmpty())
        return;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    for (auto &entry : state.folderOverrides) {
        if (entry.first == prefix) {
            // Packs are applied lowest priority first, so a later one goes in front.
            entry.second.prepend(themeFolder);
            return;
        }
    }
    state.folderOverrides.append(qMakePair(prefix, QStringList(themeFolder)));
}

void rebuildLocked(State &state)
{
    state.slotOverrides.clear();
    state.keyOverrides.clear();
    state.fileOverrides.clear();
    state.folderOverrides.clear();

    QHash<QString, const Pack *> byId;
    for (const Pack &pack : state.packs)
        byId.insert(pack.id, &pack);

    for (int i = state.enabled.size() - 1; i >= 0; --i) {
        const Pack *pack = byId.value(state.enabled.at(i));
        if (!pack)
            continue;
        for (auto it = pack->slotFiles.constBegin(); it != pack->slotFiles.constEnd(); ++it) {
            const Slot *slot = findSlot(it.key());
            if (!slot)
                continue;
            state.slotOverrides.insert(slot->id, it.value());
            if (slot->directory) {
                if (!slot->defaultPath.isEmpty())
                    addFolderOverride(state, slot->defaultPath, it.value());
                continue;
            }
            for (const QString &key : slot->skinKeys)
                state.keyOverrides.insert(key, it.value());
            if (slot->redirect && !slot->defaultPath.isEmpty())
                state.fileOverrides.insert(normalizeLegacy(slot->defaultPath), it.value());
        }
        for (auto it = pack->files.constBegin(); it != pack->files.constEnd(); ++it) {
            if (it.key().endsWith(QLatin1Char('/')))
                addFolderOverride(state, it.key(), it.value());
            else
                state.fileOverrides.insert(it.key(), it.value());
        }
    }
    std::sort(state.folderOverrides.begin(), state.folderOverrides.end(),
        [](const QPair<QString, QStringList> &a, const QPair<QString, QStringList> &b) {
            return a.first.size() > b.first.size();
        });

    {
        QMutexLocker locker(&g_folderCacheMutex);
        g_folderCache.clear();
    }
    g_active.store(!state.slotOverrides.isEmpty() || !state.keyOverrides.isEmpty()
        || !state.fileOverrides.isEmpty() || !state.folderOverrides.isEmpty());
    ++g_revision;
}

QList<Pack> scan()
{
    QList<Pack> packs;
    QSet<QString> seenIds, seenFolders;
    for (const QString &base : searchDirectories()) {
        const QDir dir(base);
        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            const QString folder = QFileInfo(dir.filePath(entry)).canonicalFilePath();
            if (folder.isEmpty() || seenFolders.contains(folder)
                || !QFileInfo::exists(QDir(folder).filePath(QLatin1String(kManifestName))))
                continue;
            seenFolders.insert(folder);
            QString error;
            Pack pack = parsePack(folder, &error);
            if (!error.isEmpty()) {
                qWarning().noquote() << "Theme pack" << folder << "skipped:" << error;
                continue;
            }
            // The user's copy is scanned first and wins over a bundled pack with the same id.
            if (seenIds.contains(pack.id))
                continue;
            seenIds.insert(pack.id);
            for (const QString &warning : pack.warnings)
                qWarning().noquote() << "Theme pack" << pack.id << ":" << warning;
            packs << pack;
        }
    }
    return packs;
}

void ensureLoaded()
{
    {
        QReadLocker locker(&g_lock);
        if (g_state.loaded)
            return;
    }
    reload();
}
} // namespace

const QList<Slot> &slotTable()
{
    static const QList<Slot> table = loadSlotTable();
    return table;
}

const Slot *findSlot(const QString &id)
{
    for (const Slot &slot : slotTable()) {
        if (slot.id == id)
            return &slot;
    }
    return nullptr;
}

QString userThemeDirectory()
{
    const QString path = QSanRuntimePaths::userDataPath(QStringLiteral("themes/.keep"));
    return QFileInfo(path).absolutePath();
}

QStringList searchDirectories()
{
    QStringList result;
    QSet<QString> seen;
    const QStringList candidates{userThemeDirectory(), QSanRuntimePaths::assetPath(QStringLiteral("themes"))};
    for (const QString &candidate : candidates) {
        const QString canonical = QFileInfo(candidate).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        result << canonical;
    }
    return result;
}

Pack parsePack(const QString &directory, QString *error)
{
    Pack pack;
    if (error)
        error->clear();
    pack.root = QFileInfo(directory).canonicalFilePath();
    const QString manifestPath = QDir(pack.root).filePath(QLatin1String(kManifestName));
    if (pack.root.isEmpty() || !QFileInfo::exists(manifestPath)) {
        if (error) *error = QStringLiteral("missing %1").arg(QLatin1String(kManifestName));
        return pack;
    }
    const JsonDocument doc = JsonDocument::fromFilePath(manifestPath);
    if (!doc.isValid() || !doc.isObject()) {
        if (error) *error = QStringLiteral("invalid %1: %2").arg(QLatin1String(kManifestName), doc.errorString());
        return pack;
    }
    const JsonObject manifest = doc.object();
    const int format = manifest.value(QStringLiteral("format"), 1).toInt();
    if (format > 1)
        pack.warnings << QStringLiteral("format %1 is newer than this game understands; unknown fields are ignored").arg(format);

    pack.id = manifest.value(QStringLiteral("id")).toString().trimmed();
    if (pack.id.isEmpty())
        pack.id = QFileInfo(pack.root).fileName();
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$"));
    if (!validId.match(pack.id).hasMatch()) {
        if (error) *error = QStringLiteral("id \"%1\" may only use letters, digits, '_', '.' and '-'").arg(pack.id);
        return pack;
    }
    pack.name = manifest.value(QStringLiteral("name"), pack.id).toString();
    pack.author = manifest.value(QStringLiteral("author")).toString();
    pack.version = manifest.value(QStringLiteral("version")).toString();
    pack.description = manifest.value(QStringLiteral("description")).toString();

    const QString preview = manifest.value(QStringLiteral("preview"), QStringLiteral("preview.png")).toString();
    pack.preview = containedPath(pack.root, preview, false);

    const JsonObject slotMap = manifest.value(QStringLiteral("slots")).toMap();
    for (auto it = slotMap.constBegin(); it != slotMap.constEnd(); ++it) {
        const Slot *slot = findSlot(it.key());
        if (!slot) {
            pack.warnings << QStringLiteral("unknown slot \"%1\"").arg(it.key());
            continue;
        }
        const QString target = containedPath(pack.root, it.value().toString(), slot->directory);
        if (target.isEmpty()) {
            pack.warnings << QStringLiteral("slot \"%1\": %2 \"%3\" is missing or outside the pack")
                .arg(it.key(), slot->directory ? QStringLiteral("folder") : QStringLiteral("file"), it.value().toString());
            continue;
        }
        pack.slotFiles.insert(slot->id, target);
    }

    const JsonObject fileMap = manifest.value(QStringLiteral("files")).toMap();
    for (auto it = fileMap.constBegin(); it != fileMap.constEnd(); ++it) {
        const QString legacy = normalizeLegacy(it.key());
        if (!legacy.startsWith(QLatin1String("image/"))) {
            pack.warnings << QStringLiteral("files: \"%1\" is not an image/ path").arg(it.key());
            continue;
        }
        const bool folder = legacy.endsWith(QLatin1Char('/'));
        const QString target = containedPath(pack.root, it.value().toString(), folder);
        if (target.isEmpty()) {
            pack.warnings << QStringLiteral("files: \"%1\" is missing or outside the pack").arg(it.value().toString());
            continue;
        }
        pack.files.insert(legacy, target);
    }
    return pack;
}

QList<Pack> installed()
{
    ensureLoaded();
    QReadLocker locker(&g_lock);
    return g_state.packs;
}

void reload()
{
    const QList<Pack> packs = scan();
    const QStringList saved = Config.value(QLatin1String(kEnabledKey)).toStringList();
    QWriteLocker locker(&g_lock);
    g_state.packs = packs;
    // Keep ids whose folder is gone, so putting the folder back restores the order.
    g_state.enabled = saved;
    g_state.enabled.removeDuplicates();
    g_state.loaded = true;
    rebuildLocked(g_state);
}

QStringList enabledIds()
{
    ensureLoaded();
    QReadLocker locker(&g_lock);
    QStringList result;
    for (const QString &id : g_state.enabled) {
        for (const Pack &pack : g_state.packs) {
            if (pack.id == id) {
                result << id;
                break;
            }
        }
    }
    return result;
}

bool setEnabledIds(const QStringList &ids)
{
    ensureLoaded();
    QStringList cleaned = ids;
    cleaned.removeDuplicates();
    cleaned.removeAll(QString());
    {
        QWriteLocker locker(&g_lock);
        if (g_state.enabled == cleaned)
            return false;
        g_state.enabled = cleaned;
        rebuildLocked(g_state);
    }
    Config.setValue(QLatin1String(kEnabledKey), cleaned);
    return true;
}

bool isActive()
{
    return g_active.load();
}

quint64 revision()
{
    return g_revision.load();
}

QString overrideForSlot(const QString &slotId)
{
    if (!g_active.load())
        return QString();
    QReadLocker locker(&g_lock);
    return g_state.slotOverrides.value(slotId);
}

QString overrideForKey(const QString &skinKey)
{
    if (!g_active.load())
        return QString();
    QReadLocker locker(&g_lock);
    return g_state.keyOverrides.value(skinKey);
}

QString overrideForFile(const QString &legacyPath)
{
    if (!g_active.load())
        return QString();
    const QString key = normalizeLegacy(legacyPath);
    if (!key.startsWith(QLatin1String("image/")) || key.endsWith(QLatin1Char('/')))
        return QString();
    QStringList folders;
    QString prefix;
    {
        QReadLocker locker(&g_lock);
        const auto exact = g_state.fileOverrides.constFind(key);
        if (exact != g_state.fileOverrides.constEnd())
            return exact.value();
        for (const auto &entry : g_state.folderOverrides) {
            if (key.startsWith(entry.first)) {
                prefix = entry.first;
                folders = entry.second;
                break;
            }
        }
    }
    if (folders.isEmpty())
        return QString();
    {
        QMutexLocker locker(&g_folderCacheMutex);
        const auto cached = g_folderCache.constFind(key);
        if (cached != g_folderCache.constEnd())
            return cached.value();
    }
    // Folder keys are case-folded; the rest of the path keeps the caller's spelling.
    const QString rest = QDir::cleanPath(QDir::fromNativeSeparators(legacyPath)).right(key.size() - prefix.size());
    QString result;
    for (const QString &folder : folders) {
        const QString candidate = folder + rest;
        if (QFileInfo(candidate).isFile()) {
            result = candidate;
            break;
        }
    }
    QMutexLocker locker(&g_folderCacheMutex);
    g_folderCache.insert(key, result);
    return result;
}

QString resolveDirectory(const QString &legacyDir, const QString &probe)
{
    if (!g_active.load())
        return legacyDir;
    QString folder = legacyDir;
    if (!folder.endsWith(QLatin1Char('/')))
        folder += QLatin1Char('/');
    const QString themed = overrideForFile(folder + probe);
    // An exact "files" entry may rename the frame; only a mirrored folder is a whole animation.
    if (themed.isEmpty() || !themed.endsWith(QLatin1Char('/') + probe))
        return legacyDir;
    return themed.left(themed.size() - probe.size());
}
}
