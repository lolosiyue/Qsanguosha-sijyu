#include "theme-pack.h"
#include "json.h"
#include "runtime-paths.h"
#include "settings.h"

#include <QCoreApplication>
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

struct FileLayer
{
    QHash<QString, QString> exact;
    QList<QPair<QString, QString>> folders;
};

struct State
{
    bool loaded = false;
    QList<Pack> packs;
    QStringList enabled;
    QHash<QString, QString> slotOverrides;
    QHash<QString, QString> keyOverrides;
    QHash<QString, QColor> colorOverrides;
    // Highest pack first; within each pack manifest files precede slot redirects.
    QList<FileLayer> fileLayers;
    // Pushed by the running room: packs stacked over the enabled ones (lowest first), then
    // the room's own overrides above everything. Never saved.
    QStringList runtimePacks;
    Pack runtime;
};

QReadWriteLock g_lock;
State g_state;
std::atomic<bool> g_active{false};
std::atomic<quint64> g_revision{0};

// Folder lookups stat the disk, so remember each answer until the next rebuild.
QMutex g_folderCacheMutex;
QHash<QString, QString> g_folderCache;

const JsonObject &registry()
{
    static const JsonObject object = [] {
        const QString path = QSanRuntimePaths::assetPath(QStringLiteral("skins/theme-slots.json"));
        const JsonDocument doc = JsonDocument::fromFilePath(path);
        if (!doc.isValid() || !doc.isObject()) {
            qWarning().noquote() << "Theme slots: cannot read" << path << doc.errorString();
            return JsonObject();
        }
        return doc.object();
    }();
    return object;
}

QList<Slot> loadSlotTable()
{
    QList<Slot> result;
    const QVariantList entries = registry().value(QStringLiteral("slots")).toList();
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

QList<ColorSlot> loadColorTable()
{
    QList<ColorSlot> result;
    const QVariantList entries = registry().value(QStringLiteral("colors")).toList();
    for (const QVariant &entry : entries) {
        const JsonObject object = entry.toMap();
        ColorSlot slot;
        slot.id = object.value(QStringLiteral("id")).toString();
        if (slot.id.isEmpty())
            continue;
        slot.label = object.value(QStringLiteral("label"), slot.id).toString();
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

void addFolderOverride(FileLayer &layer, const QString &legacyFolder, const QString &themeFolder)
{
    QString prefix = normalizeLegacy(legacyFolder);
    if (prefix.isEmpty())
        return;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    layer.folders.append(qMakePair(prefix, themeFolder));
}

void prependLayer(State &state, FileLayer layer)
{
    if (layer.exact.isEmpty() && layer.folders.isEmpty())
        return;
    std::sort(layer.folders.begin(), layer.folders.end(),
        [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
            return a.first.size() > b.first.size();
        });
    state.fileLayers.prepend(layer);
}

// Layers one pack over everything applied before it.
void applyPackLocked(State &state, const Pack *pack)
{
    FileLayer slotLayer, fileLayer;
    for (auto it = pack->slotFiles.constBegin(); it != pack->slotFiles.constEnd(); ++it) {
        const Slot *slot = findSlot(it.key());
        if (!slot)
            continue;
        state.slotOverrides.insert(slot->id, it.value());
        if (slot->directory) {
            if (!slot->defaultPath.isEmpty())
                addFolderOverride(slotLayer, slot->defaultPath, it.value());
            continue;
        }
        for (const QString &key : slot->skinKeys)
            state.keyOverrides.insert(key, it.value());
        if (slot->redirect && !slot->defaultPath.isEmpty())
            slotLayer.exact.insert(normalizeLegacy(slot->defaultPath), it.value());
    }
    for (auto it = pack->colors.constBegin(); it != pack->colors.constEnd(); ++it)
        state.colorOverrides.insert(it.key(), it.value());
    for (auto it = pack->files.constBegin(); it != pack->files.constEnd(); ++it) {
        if (it.key().endsWith(QLatin1Char('/')))
            addFolderOverride(fileLayer, it.key(), it.value());
        else
            fileLayer.exact.insert(it.key(), it.value());
    }
    prependLayer(state, slotLayer);
    prependLayer(state, fileLayer);
}

const Pack *findPackLocked(const State &state, const QString &id)
{
    for (const Pack &pack : state.packs) {
        if (pack.id == id)
            return &pack;
    }
    return nullptr;
}

void rebuildLocked(State &state)
{
    state.slotOverrides.clear();
    state.keyOverrides.clear();
    state.colorOverrides.clear();
    state.fileLayers.clear();

    for (int i = state.enabled.size() - 1; i >= 0; --i) {
        if (const Pack *pack = findPackLocked(state, state.enabled.at(i)))
            applyPackLocked(state, pack);
    }
    for (const QString &id : state.runtimePacks) {
        if (const Pack *pack = findPackLocked(state, id))
            applyPackLocked(state, pack);
    }
    applyPackLocked(state, &state.runtime);

    {
        QMutexLocker locker(&g_folderCacheMutex);
        g_folderCache.clear();
    }
    g_active.store(!state.slotOverrides.isEmpty() || !state.keyOverrides.isEmpty()
        || !state.colorOverrides.isEmpty() || !state.fileLayers.isEmpty());
    ++g_revision;
}

QString resolveFile(const QString &legacyPath, bool foldersOnly)
{
    const QString key = normalizeLegacy(legacyPath);
    if (!key.startsWith(QLatin1String("image/")) || key.endsWith(QLatin1Char('/')))
        return QString();
    QList<FileLayer> layers;
    QString cacheKey;
    {
        QReadLocker locker(&g_lock);
        layers = g_state.fileLayers;
        // A lookup racing with rebuild cannot repopulate the new revision's cache.
        cacheKey = QString::number(g_revision.load()) + (foldersOnly ? QLatin1String(":dir:") : QLatin1String(":file:"))
            + legacyPath;
    }
    {
        QMutexLocker locker(&g_folderCacheMutex);
        const auto cached = g_folderCache.constFind(cacheKey);
        if (cached != g_folderCache.constEnd())
            return cached.value();
    }
    const QString callerPath = QDir::cleanPath(QDir::fromNativeSeparators(legacyPath));
    QString result;
    for (const FileLayer &layer : layers) {
        if (!foldersOnly) {
            const QString exact = layer.exact.value(key);
            if (!exact.isEmpty() && QFileInfo(exact).isFile()) {
                result = exact;
                break;
            }
        }
        for (const auto &entry : layer.folders) {
            if (!key.startsWith(entry.first))
                continue;
            // Prefixes are case-folded; preserve the caller's file spelling.
            const QString candidate = entry.second + callerPath.right(key.size() - entry.first.size());
            if (QFileInfo(candidate).isFile()) {
                result = candidate;
                break;
            }
        }
        if (!result.isEmpty())
            break;
    }
    QMutexLocker locker(&g_folderCacheMutex);
    g_folderCache.insert(cacheKey, result);
    return result;
}

// An asset-relative file or folder a room names at runtime; empty when it escapes the
// asset root or is missing.
QString runtimeAsset(const QString &value, bool folder, QString *error)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(value));
    if (QDir::isAbsolutePath(clean) || clean == QLatin1String("..") || clean.startsWith(QLatin1String("../"))) {
        if (error) *error = QStringLiteral("\"%1\" must be a path inside the game folder").arg(value);
        return QString();
    }
    const QFileInfo info(QSanRuntimePaths::assetPath(clean));
    if (!info.exists() || info.isDir() != folder) {
        if (error) *error = (folder ? QStringLiteral("folder \"%1\" not found") : QStringLiteral("file \"%1\" not found")).arg(value);
        return QString();
    }
    return folder ? info.absoluteFilePath() + QLatin1Char('/') : info.absoluteFilePath();
}

// "theme:<id>" names an installed pack; returns it, or null with *error set.
const Pack *runtimePackRef(const State &state, const QString &value, QString *error)
{
    const QString id = value.mid(6).trimmed();
    const Pack *pack = findPackLocked(state, id);
    if (!pack && error)
        *error = QStringLiteral("theme pack \"%1\" is not installed").arg(id);
    return pack;
}

bool isThemeRef(const QString &value)
{
    return value.startsWith(QLatin1String("theme:"));
}

// Applies one runtime request to state.runtime / state.runtimePacks; false when rejected.
bool applyRuntimeLocked(State &state, const QString &kind, const QString &id, const QString &value, QString *error)
{
    if (kind == QLatin1String("reset")) {
        state.runtimePacks.clear();
        state.runtime = Pack();
        return true;
    }
    if (id.isEmpty()) {
        if (error) *error = QStringLiteral("%1: missing id").arg(kind);
        return false;
    }
    if (kind == QLatin1String("pack")) {
        if (value.isEmpty()) {
            state.runtimePacks.removeAll(id);
            return true;
        }
        if (!findPackLocked(state, id)) {
            if (error) *error = QStringLiteral("theme pack \"%1\" is not installed").arg(id);
            return false;
        }
        state.runtimePacks.removeAll(id);
        state.runtimePacks.append(id);
        return true;
    }
    if (kind == QLatin1String("slot")) {
        const Slot *slot = findSlot(id);
        if (!slot) {
            if (error) *error = QStringLiteral("unknown slot \"%1\"").arg(id);
            return false;
        }
        if (value.isEmpty()) {
            state.runtime.slotFiles.remove(id);
            return true;
        }
        QString target;
        if (isThemeRef(value)) {
            const Pack *pack = runtimePackRef(state, value, error);
            if (!pack)
                return false;
            target = pack->slotFiles.value(id);
            if (target.isEmpty()) {
                if (error) *error = QStringLiteral("theme pack \"%1\" has no slot \"%2\"").arg(pack->id, id);
                return false;
            }
        } else {
            target = runtimeAsset(value, slot->directory, error);
            if (target.isEmpty())
                return false;
        }
        state.runtime.slotFiles.insert(id, target);
        return true;
    }
    if (kind == QLatin1String("color")) {
        const bool known = std::any_of(colorTable().cbegin(), colorTable().cend(),
            [&](const ColorSlot &slot) { return slot.id == id; });
        if (!known) {
            if (error) *error = QStringLiteral("unknown color \"%1\"").arg(id);
            return false;
        }
        if (value.isEmpty()) {
            state.runtime.colors.remove(id);
            return true;
        }
        QColor color;
        if (isThemeRef(value)) {
            const Pack *pack = runtimePackRef(state, value, error);
            if (!pack)
                return false;
            color = pack->colors.value(id);
        } else {
            color = QColor(value);
        }
        if (!color.isValid()) {
            if (error) *error = QStringLiteral("color \"%1\": \"%2\" is not a valid color").arg(id, value);
            return false;
        }
        state.runtime.colors.insert(id, color);
        return true;
    }
    if (kind == QLatin1String("file")) {
        const QString legacy = normalizeLegacy(id);
        if (!legacy.startsWith(QLatin1String("image/"))) {
            if (error) *error = QStringLiteral("file \"%1\" is not an image/ path").arg(id);
            return false;
        }
        if (value.isEmpty()) {
            state.runtime.files.remove(legacy);
            return true;
        }
        const QString target = runtimeAsset(value, legacy.endsWith(QLatin1Char('/')), error);
        if (target.isEmpty())
            return false;
        state.runtime.files.insert(legacy, target);
        return true;
    }
    if (error) *error = QStringLiteral("unknown kind \"%1\"").arg(kind);
    return false;
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

const QList<ColorSlot> &colorTable()
{
    static const QList<ColorSlot> table = loadColorTable();
    return table;
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
        pack.warnings << QCoreApplication::translate("ThemePacks", "format %1 is newer than this game understands; unknown fields are ignored").arg(format);

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
            pack.warnings << QCoreApplication::translate("ThemePacks", "unknown slot \"%1\"").arg(it.key());
            continue;
        }
        const QString target = containedPath(pack.root, it.value().toString(), slot->directory);
        if (target.isEmpty()) {
            pack.warnings << (slot->directory
                ? QCoreApplication::translate("ThemePacks", "slot \"%1\": folder \"%2\" is missing or outside the pack")
                : QCoreApplication::translate("ThemePacks", "slot \"%1\": file \"%2\" is missing or outside the pack"))
                .arg(it.key(), it.value().toString());
            continue;
        }
        pack.slotFiles.insert(slot->id, target);
    }

    const JsonObject colorMap = manifest.value(QStringLiteral("colors")).toMap();
    for (auto it = colorMap.constBegin(); it != colorMap.constEnd(); ++it) {
        const bool known = std::any_of(colorTable().cbegin(), colorTable().cend(),
            [&](const ColorSlot &slot) { return slot.id == it.key(); });
        if (!known) {
            pack.warnings << QCoreApplication::translate("ThemePacks", "unknown color \"%1\"").arg(it.key());
            continue;
        }
        const QColor color(it.value().toString());
        if (!color.isValid()) {
            pack.warnings << QCoreApplication::translate("ThemePacks", "color \"%1\": \"%2\" is not a valid color")
                .arg(it.key(), it.value().toString());
            continue;
        }
        pack.colors.insert(it.key(), color);
    }

    const JsonObject fileMap = manifest.value(QStringLiteral("files")).toMap();
    for (auto it = fileMap.constBegin(); it != fileMap.constEnd(); ++it) {
        const QString legacy = normalizeLegacy(it.key());
        if (!legacy.startsWith(QLatin1String("image/"))) {
            pack.warnings << QCoreApplication::translate("ThemePacks", "files: \"%1\" is not an image/ path").arg(it.key());
            continue;
        }
        const bool folder = legacy.endsWith(QLatin1Char('/'));
        const QString target = containedPath(pack.root, it.value().toString(), folder);
        if (target.isEmpty()) {
            pack.warnings << QCoreApplication::translate("ThemePacks", "files: \"%1\" is missing or outside the pack").arg(it.value().toString());
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

QColor color(const QString &id, const QColor &fallback)
{
    if (!g_active.load())
        return fallback;
    QReadLocker locker(&g_lock);
    return g_state.colorOverrides.value(id, fallback);
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
    return resolveFile(legacyPath, false);
}

QString resolveDirectory(const QString &legacyDir, const QString &probe)
{
    if (!g_active.load())
        return legacyDir;
    QString folder = legacyDir;
    if (!folder.endsWith(QLatin1Char('/')))
        folder += QLatin1Char('/');
    // Exact frame overrides never select an animation: take its whole mirrored folder.
    const QString themed = resolveFile(folder + probe, true);
    if (themed.isEmpty())
        return legacyDir;
    return themed.left(themed.size() - probe.size());
}

bool setRuntimeOverride(const QString &kind, const QString &id, const QString &value, QString *error)
{
    if (error)
        error->clear();
    ensureLoaded();
    QWriteLocker locker(&g_lock);
    const QStringList packsBefore = g_state.runtimePacks;
    const QMap<QString, QString> slotsBefore = g_state.runtime.slotFiles, filesBefore = g_state.runtime.files;
    const QMap<QString, QColor> colorsBefore = g_state.runtime.colors;
    if (!applyRuntimeLocked(g_state, kind, id.trimmed(), value.trimmed(), error))
        return false;
    if (g_state.runtimePacks == packsBefore && g_state.runtime.slotFiles == slotsBefore
        && g_state.runtime.files == filesBefore && g_state.runtime.colors == colorsBefore)
        return false;
    rebuildLocked(g_state);
    return true;
}

void clearRuntime()
{
    QWriteLocker locker(&g_lock);
    if (g_state.runtimePacks.isEmpty() && g_state.runtime.slotFiles.isEmpty()
        && g_state.runtime.files.isEmpty() && g_state.runtime.colors.isEmpty())
        return;
    g_state.runtimePacks.clear();
    g_state.runtime = Pack();
    rebuildLocked(g_state);
}

bool hasRuntimeOverrides()
{
    QReadLocker locker(&g_lock);
    return !g_state.runtimePacks.isEmpty() || !g_state.runtime.slotFiles.isEmpty()
        || !g_state.runtime.files.isEmpty() || !g_state.runtime.colors.isEmpty();
}
}
