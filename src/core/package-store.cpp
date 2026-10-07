#include "package-store.h"

#include "android-content-store.h"
#include "android-zip-reader.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <QBuffer>

#include <algorithm>
#include <limits>
#include <cmath>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,127}$"));
    return pattern.match(id).hasMatch();
}

bool safeRelative(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':')
        || path.contains(QChar::Null) || path != path.normalized(QString::NormalizationForm_C))
        return false;
    for (const QString &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
        for (QChar c : part) if (c.unicode() < 32) return false;
    }
    return true;
}

bool ensureDirectory(const QString &path, QString *error)
{
    const QFileInfo info(path);
    QString cursor = info.absoluteFilePath();
    while (!cursor.isEmpty()) {
        const QFileInfo ancestor(cursor);
        if (ancestor.isSymLink()) return fail(error, QStringLiteral("package store path crosses a symbolic link: %1").arg(cursor));
        const QString parent = ancestor.absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    if (info.exists()) return info.isDir() || fail(error, QStringLiteral("package store path is not a directory: %1").arg(path));
    return QDir().mkpath(path) || fail(error, QStringLiteral("cannot create package store directory: %1").arg(path));
}

bool regularFile(const QFileInfo &info)
{
    return info.isFile() && !info.isSymLink();
}

QString storageTr(const char *text) { return QCoreApplication::translate("PackageStore", text); }
bool addSize(quint64 a, quint64 b, quint64 *sum, QString *error)
{
    if (b > std::numeric_limits<quint64>::max() - a)
        return fail(error, storageTr("Package storage budget overflow."));
    *sum = a + b;
    return true;
}
bool addTree(PackageStore::TreeSize *total, const PackageStore::TreeSize &part, QString *error)
{
    return addSize(total->bytes, part.bytes, &total->bytes, error)
        && addSize(total->entries, part.entries, &total->entries, error);
}
bool safeAncestors(QString path)
{
    path = QFileInfo(path).absoluteFilePath();
    while (!path.isEmpty()) {
        const QFileInfo info(path);
        if (info.isSymLink()) return false;
        const QString parent = info.absolutePath();
        if (parent == path) break;
        path = parent;
    }
    return true;
}
// Logical bytes, including sparse-file lengths; allocation padding is budgeted
// separately. This pass reads metadata only, never multi-GB payloads into RAM.
bool treeSize(const QString &root, quint64 maxBytes, quint64 maxEntries,
              PackageStore::TreeSize *size, QString *error)
{
    *size = {};
    const QFileInfo rootInfo(root);
    if (!safeAncestors(root)) return fail(error, storageTr("Unsafe package storage path."));
    if (!rootInfo.exists()) return true;
    if (!rootInfo.isDir()) return fail(error, storageTr("Unsafe package storage path."));
    QHash<QString, QString> folded;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(root).relativeFilePath(path).replace('\\', '/');
        const QString key = relative.toCaseFolded();
        if (!safeRelative(relative) || info.isSymLink() || (!info.isDir() && !regularFile(info))
            || (folded.contains(key) && folded.value(key) != relative))
            return fail(error, storageTr("Unsafe package storage path."));
        folded.insert(key, relative);
        if (++size->entries > maxEntries) return fail(error, storageTr("Installed package entry quota exceeded."));
        if (info.isDir()) continue;
        if (info.size() < 0 || quint64(info.size()) > 128ULL * 1024 * 1024
            || quint64(info.size()) > maxBytes || size->bytes > maxBytes - quint64(info.size()))
            return fail(error, storageTr("Installed package byte quota exceeded."));
        size->bytes += quint64(info.size());
    }
    return true;
}
QStorageInfo storageAt(QString path)
{
    path = QFileInfo(path).absoluteFilePath();
    while (!QFileInfo::exists(path)) {
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) break;
        path = parent;
    }
    QStorageInfo info(path);
    info.refresh();
    return info;
}
bool usableStorage(const QStorageInfo &info)
{
    return info.isValid() && info.isReady() && !info.isReadOnly() && info.bytesAvailable() >= 0;
}
bool sharedStorage(const QStorageInfo &a, const QStorageInfo &b)
{
    // Coalesce bind mounts/subvolumes conservatively; use the smaller available
    // value when user quotas report different values on the same backing device.
    return a == b || (!a.device().isEmpty() && a.device() == b.device());
}

bool copyTree(const QString &source, const QString &destination, QString *error,
              quint64 maxBytes = 512ULL * 1024 * 1024,
              quint64 maxEntries = 200000)
{
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isDir() || sourceInfo.isSymLink())
        return fail(error, QStringLiteral("package source is not a real directory: %1").arg(source));
    if (!ensureDirectory(destination, error)) return false;
    quint64 bytes = 0;
    quint64 entries = 0;
    QHash<QString, QString> foldedPaths;
    QDirIterator iterator(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString from = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir(source).relativeFilePath(from).replace('\\', '/');
        if (!safeRelative(relative) || info.isSymLink())
            return fail(error, QStringLiteral("package source contains an unsafe path: %1").arg(relative));
        if (++entries > maxEntries) return fail(error, QStringLiteral("package entry count limit exceeded"));
        const QString folded = relative.toCaseFolded();
        if (foldedPaths.contains(folded) && foldedPaths.value(folded) != relative)
            return fail(error, QStringLiteral("package contains a case-insensitive path collision: %1").arg(relative));
        foldedPaths.insert(folded, relative);
        const QString to = QDir(destination).filePath(relative);
        if (info.isDir()) {
            if (!ensureDirectory(to, error)) return false;
            continue;
        }
        if (!regularFile(info) || !ensureDirectory(QFileInfo(to).absolutePath(), error))
            return fail(error, QStringLiteral("package source contains a non-regular file: %1").arg(relative));
        if (info.size() < 0 || quint64(info.size()) > 128ULL * 1024 * 1024
            || quint64(info.size()) > maxBytes || bytes > maxBytes - quint64(info.size()))
            return fail(error, QStringLiteral("package size limit exceeded"));
        bytes += quint64(info.size());
        QFile input(from);
        QSaveFile output(to);
        output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            return fail(error, QStringLiteral("cannot copy package file: %1").arg(relative));
        quint64 copiedBytes = 0;
        while (!input.atEnd()) {
            const QByteArray block = input.read(256 * 1024);
            if (block.isEmpty() && input.error() != QFileDevice::NoError)
                return fail(error, QStringLiteral("cannot read package file: %1").arg(relative));
            if (quint64(block.size()) > quint64(info.size()) - copiedBytes)
                return fail(error, storageTr("Package file changed during copy."));
            copiedBytes += quint64(block.size());
            if (output.write(block) != block.size())
                return fail(error, QStringLiteral("cannot write package file: %1").arg(relative));
        }
        if (copiedBytes != quint64(info.size())) return fail(error, storageTr("Package file changed during copy."));
        if (!output.commit()) return fail(error, QStringLiteral("cannot publish package file: %1").arg(relative));
    }
    return true;
}

bool removeTree(const QString &path, QString *error)
{
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return true;
    if (info.isSymLink()) return fail(error, QStringLiteral("refusing to remove symbolic link: %1").arg(path));
    if (!info.isDir()) return QFile::remove(path) || fail(error, QStringLiteral("cannot remove file: %1").arg(path));
    QDir dir(path);
    const QFileInfoList children = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &child : children)
        if (!removeTree(child.absoluteFilePath(), error)) return false;
    return QDir().rmdir(path) || fail(error, QStringLiteral("cannot remove directory: %1").arg(path));
}

QVariantMap readObject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists()) return {};
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("cannot read package store journal")); return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(error, QStringLiteral("package store journal is invalid")); return {};
    }
    return doc.object().toVariantMap();
}

QString transactionRoot(const QString &runtimeRoot)
{
    return QDir(runtimeRoot).filePath(QStringLiteral(".package-store-transaction"));
}

bool isInside(const QString &parent, const QString &child)
{
    const QString p = QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(parent).absoluteFilePath())) + QLatin1Char('/');
    const QString c = QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(child).absoluteFilePath()));
#ifdef Q_OS_WIN
    return c.startsWith(p, Qt::CaseInsensitive);
#else
    return c.startsWith(p, Qt::CaseSensitive);
#endif
}
}

PackageStore::PackageStore(const QString &runtimeRoot, const QString &userDataRoot)
    : m_runtimeRoot(runtimeRoot.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(runtimeRoot).absoluteFilePath())),
      m_userDataRoot(userDataRoot.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(userDataRoot).absoluteFilePath()))
{
    const QString path = QDir(userStoreRoot()).filePath(QStringLiteral("limits.json"));
    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) return;
    auto invalid = [&] { m_limitsError = storageTr("Invalid package storage limits configuration."); };
    QFile file(path);
    if (!safeAncestors(path) || !regularFile(QFileInfo(path)) || file.size() > 4096
        || !file.open(QIODevice::ReadOnly)) { invalid(); return; }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.read(4097), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) { invalid(); return; }
    const auto object = doc.object();
    auto integer = [&](const char *key, double low, double high, quint64 *value) {
        const auto v = object.value(QLatin1String(key)); const double n = v.toDouble(-1);
        if (!v.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < low || n > high) return false;
        *value = quint64(n); return true;
    };
    quint64 schema, maximum, reserve;
    if (!integer("schema_version", 1, 1, &schema) || !integer("max_installed_mib", 512, 32768, &maximum)
        || !integer("reserve_free_mib", 512, 4096, &reserve)) { invalid(); return; }
    m_limits.maxInstalledBytes = maximum * 1024 * 1024;
    m_limits.reserveBytes = reserve * 1024 * 1024;
}

QString PackageStore::packagesRoot() const { return QDir(m_runtimeRoot).filePath(QStringLiteral("packages")); }
QString PackageStore::userStoreRoot() const { return QDir(m_userDataRoot).filePath(QStringLiteral("package-store")); }

bool PackageStore::fitsQuota(const TreeSize &size, const Limits &limits, QString *error)
{
    if (size.bytes > limits.maxInstalledBytes) return fail(error, storageTr("Installed package byte quota exceeded."));
    if (size.entries > limits.maxInstalledEntries) return fail(error, storageTr("Installed package entry quota exceeded."));
    return true;
}

bool PackageStore::planSpace(const TreeSize &active, const TreeSize &installs,
                             const TreeSize &backup, const TreeSize &incoming,
                             quint64 copies, quint64 unit, SpacePlan *plan, QString *error)
{
    if (!plan || !unit || copies > 2) return fail(error, storageTr("Invalid package storage budget."));
    auto allocated = [&](const TreeSize &tree, quint64 *bytes) {
        if (tree.entries > std::numeric_limits<quint64>::max() / unit)
            return fail(error, storageTr("Package storage budget overflow."));
        return addSize(tree.bytes, tree.entries * unit, bytes, error);
    };
    quint64 a, i, b, n, validation, staging, restart;
    if (!allocated(active, &a) || !allocated(installs, &i) || !allocated(backup, &b)
        || !allocated(incoming, &n) || !addSize(a, i, &validation, error)) return false;
    if (copies && n > std::numeric_limits<quint64>::max() / copies)
        return fail(error, storageTr("Package storage budget overflow."));
    if (!addSize(validation, copies * n, &staging, error)
        || !addSize(b, copies ? n : 0, &restart, error)) return false;
    *plan = {staging, restart, validation};
    return true;
}

bool PackageStore::fitsAvailableSpace(const SpacePlan &plan, quint64 user, quint64 runtime,
                                      bool same, const Limits &limits, QString *error)
{
    quint64 userNeed, runtimeNeed;
    if (same) {
        quint64 restart;
        if (!addSize(plan.restartUserBytes, plan.restartRuntimeBytes, &restart, error)
            || !addSize(std::max(plan.stageUserBytes, restart), limits.reserveBytes, &userNeed, error)) return false;
        if (std::min(user, runtime) < userNeed)
            return fail(error, storageTr("Insufficient free space for package staging and rollback."));
    } else {
        if (!addSize(std::max(plan.stageUserBytes, plan.restartUserBytes), limits.reserveBytes, &userNeed, error)
            || !addSize(plan.restartRuntimeBytes, limits.reserveBytes, &runtimeNeed, error)) return false;
        if (user < userNeed || runtime < runtimeNeed)
            return fail(error, storageTr("Insufficient free space for package staging and rollback."));
    }
    return true;
}

bool PackageStore::checkStorage(const SpacePlan &plan, QString *error) const
{
    const auto user = storageAt(userStoreRoot()), runtime = storageAt(m_runtimeRoot);
    if (!usableStorage(user) || !usableStorage(runtime))
        return fail(error, storageTr("Package storage is unavailable or read-only."));
    // Allow bounded journal/QSaveFile metadata and workspace root directories
    // beyond the payload tree's per-entry allocation allowance.
    constexpr quint64 metadata = 4ULL * 1024 * 1024;
    SpacePlan padded = plan;
    if (!addSize(padded.stageUserBytes, metadata, &padded.stageUserBytes, error)
        || !addSize(padded.restartUserBytes, metadata, &padded.restartUserBytes, error)
        || !addSize(padded.restartRuntimeBytes, metadata, &padded.restartRuntimeBytes, error)) return false;
    return fitsAvailableSpace(padded, quint64(user.bytesAvailable()), quint64(runtime.bytesAvailable()),
                             sharedStorage(user, runtime), m_limits, error);
}

bool PackageStore::checkUserSpace(quint64 bytes, QString *error) const
{
    if (!m_limitsError.isEmpty()) return fail(error, m_limitsError);
    const auto user = storageAt(userStoreRoot());
    quint64 need;
    if (!addSize(bytes, m_limits.reserveBytes, &need, error)) return false;
    if (!usableStorage(user)) return fail(error, storageTr("Package storage is unavailable or read-only."));
    return quint64(user.bytesAvailable()) >= need
        || fail(error, storageTr("Insufficient free space for package staging and rollback."));
}

bool PackageStore::preflight(const QVariantMap &changes, const QVariantMap &sources,
                             const QString &incomingId, const TreeSize &incoming,
                             quint64 copies, QString *error) const
{
    if (!m_limitsError.isEmpty()) return fail(error, m_limitsError);
    TreeSize active, candidate, installs, backup;
    if (!treeSize(packagesRoot(), m_limits.maxInstalledBytes, m_limits.maxInstalledEntries, &active, error)) return false;
    candidate = active;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const QString id = it.key(), operation = it.value().toString();
        if (!validId(id) || id == QStringLiteral("core")
            || (operation != QStringLiteral("install") && operation != QStringLiteral("remove")))
            return fail(error, storageTr("Invalid package storage budget."));
        const QString oldPath = QDir(packagesRoot()).filePath(id);
        TreeSize old;
        if (!treeSize(oldPath, m_limits.maxInstalledBytes, m_limits.maxInstalledEntries, &old, error)) return false;
        if (QFileInfo(oldPath).exists()) ++old.entries;
        if (old.bytes > candidate.bytes || old.entries > candidate.entries)
            return fail(error, storageTr("Invalid package storage budget."));
        candidate.bytes -= old.bytes; candidate.entries -= old.entries;
        if (!addTree(&backup, old, error)) return false;
        if (operation == QStringLiteral("remove")) continue;
        TreeSize replacement;
        if (id == incomingId) replacement = incoming;
        else {
            const QString path = sources.contains(id) ? sources.value(id).toString()
                : QDir(userStoreRoot()).filePath(QStringLiteral("pending/") + id);
            if (!QFileInfo(path).isDir() || !treeSize(path, 512ULL * 1024 * 1024, 50000, &replacement, error))
                return fail(error, storageTr("Unsafe package storage path."));
        }
        ++replacement.entries; // package root in the aggregate tree
        if (!addTree(&candidate, replacement, error) || !addTree(&installs, replacement, error)) return false;
    }
    if (!fitsQuota(candidate, m_limits, error)) return false;
    const auto user = storageAt(userStoreRoot()), runtime = storageAt(m_runtimeRoot);
    quint64 unit = 4096;
    for (const auto &storage : {user, runtime}) {
        const qint64 reported = storage.blockSize();
        unit = std::max(unit, reported > 0 ? quint64(reported) : 65536ULL);
    }
    if (unit > 1024 * 1024) return fail(error, storageTr("Invalid package storage budget."));
    SpacePlan plan;
    return planSpace(active, installs, backup, incoming, copies, unit, &plan, error) && checkStorage(plan, error);
}

bool PackageStore::validateEffectiveSet(const QVariantMap &changes, const QVariantMap &overrideSources,
                                        QString *error) const
{
    if (!preflight(changes, overrideSources, QString(), {}, 0, error)) return false;
    QTemporaryDir validation(QDir(userStoreRoot()).filePath(QStringLiteral("validation-XXXXXX")));
    if (!validation.isValid()) return fail(error, QStringLiteral("cannot create package validation workspace"));
    const QString candidatePackages = QDir(validation.path()).filePath(QStringLiteral("packages"));
    if (QFileInfo(packagesRoot()).exists()) {
        if (!copyTree(packagesRoot(), candidatePackages, error, m_limits.maxInstalledBytes, m_limits.maxInstalledEntries)) return false;
    } else if (!ensureDirectory(candidatePackages, error)) return false;

    const QDir staged(QDir(userStoreRoot()).filePath(QStringLiteral("pending")));
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const QString id = it.key();
        if (!validId(id) || id == QStringLiteral("core"))
            return fail(error, QStringLiteral("pending package set contains an invalid or immutable id: %1").arg(id));
        const QString target = QDir(candidatePackages).filePath(id);
        if (it.value().toString() == QStringLiteral("remove")) {
            if (!removeTree(target, error)) return false;
            continue;
        }
        if (it.value().toString() != QStringLiteral("install"))
            return fail(error, QStringLiteral("unknown pending package operation"));
        const QString source = overrideSources.contains(id) ? overrideSources.value(id).toString() : staged.filePath(id);
        QString parseError;
        const QSanPackages::Package package = QSanPackages::parsePackage(source, &parseError, true);
        if (!parseError.isEmpty() || package.id != id)
            return fail(error, parseError.isEmpty() ? QStringLiteral("staged package id mismatch: %1").arg(id) : parseError);
        if (!removeTree(target, error) || !copyTree(package.root, target, error, 512ULL * 1024 * 1024, 50000)) return false;
    }
    TreeSize finalSize;
    if (!treeSize(candidatePackages, m_limits.maxInstalledBytes, m_limits.maxInstalledEntries, &finalSize, error)) return false;
    const QSanPackages::Catalog candidate = QSanPackages::loadCatalog(validation.path(), true);
    return candidate.isValid() || fail(error, QStringLiteral("pending package set is invalid: %1").arg(candidate.error));
}

bool PackageStore::writeJournal(const QVariantMap &journal, QString *error) const
{
    if (!ensureDirectory(userStoreRoot(), error)) return false;
    QSaveFile file(QDir(userStoreRoot()).filePath(QStringLiteral("journal.json")));
    file.setDirectWriteFallback(false);
    const QByteArray bytes = QJsonDocument::fromVariant(journal).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("cannot atomically write package store journal"));
    return true;
}

QVariantMap PackageStore::readJournal(QString *error) const
{
    if (!configured()) return {};
    return readObject(QDir(userStoreRoot()).filePath(QStringLiteral("journal.json")), error);
}

bool PackageStore::stageValidatedPackage(const QString &source, QString *error)
{
    QString parseError;
    QSanPackages::Package package = QSanPackages::parsePackage(source, error ? error : &parseError, true);
    if ((error && !error->isEmpty()) || !parseError.isEmpty()) return fail(error, error ? *error : parseError);
    const QString id = package.id;
    if (id == QStringLiteral("core"))
        return fail(error, QStringLiteral("the bundled core package is immutable"));
    QVariantMap before = readJournal(error);
    if (error && !error->isEmpty()) return false;
    QVariantMap planned = before.value(QStringLiteral("changes")).toMap();
    planned.insert(id, QStringLiteral("install"));
    TreeSize incoming;
    if (!treeSize(source, 512ULL * 1024 * 1024, 50000, &incoming, error)
        || !preflight(planned, {}, id, incoming, 1, error)) return false;
    const QString stagedRoot = QDir(userStoreRoot()).filePath(QStringLiteral("pending"));
    if (!ensureDirectory(stagedRoot, error)) return false;
    const QString destination = QDir(stagedRoot).filePath(id);
    const QString temporary = QDir(stagedRoot).filePath(id + QStringLiteral(".incoming"));
    if (!removeTree(temporary, error) || !copyTree(package.root, temporary, error, 512ULL * 1024 * 1024, 50000)) {
        removeTree(temporary, nullptr); return false;
    }
    QString verifyError;
    const QSanPackages::Package copied = QSanPackages::parsePackage(temporary, &verifyError, true);
    if (!verifyError.isEmpty()) { removeTree(temporary, nullptr); return fail(error, verifyError); }
    QVariantMap journal = readJournal(error);
    if (error && !error->isEmpty()) { removeTree(temporary, nullptr); return false; }
    QVariantMap changes = journal.value(QStringLiteral("changes")).toMap();
    changes.insert(id, QStringLiteral("install"));
    if (!validateEffectiveSet(changes, QVariantMap{{id, temporary}}, error)) { removeTree(temporary, nullptr); return false; }
    if (!removeTree(destination, error) || !QDir().rename(temporary, destination))
        return fail(error, QStringLiteral("cannot publish staged package %1").arg(id));
    journal.insert(QStringLiteral("changes"), changes);
    journal.remove(QStringLiteral("phase"));
    journal.remove(QStringLiteral("lastError"));
    return writeJournal(journal, error);
}

bool PackageStore::stageDirectory(const QString &packageDirectory, QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!ensureDirectory(userStoreRoot(), error) || !lock.tryLock(1000))
        return fail(error, QStringLiteral("package store is busy or unavailable"));
    return stageValidatedPackage(packageDirectory, error);
}

bool PackageStore::stageArchive(QIODevice &source, QString *error, std::atomic_bool *cancellation,
                                const QString &expectedId, const QString &expectedVersion)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    if (!ensureDirectory(userStoreRoot(), error)) return false;
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(1000)) return fail(error, QStringLiteral("package store is busy"));
    if (!expectedId.isEmpty()) {
        const auto journal = readJournal(error);
        if (error && !error->isEmpty()) return false;
        if (journal.value(QStringLiteral("changes")).toMap().contains(expectedId))
            return fail(error, QCoreApplication::translate("PackageStore", "This package already has a pending change."));
    }
    const QString scratch = QDir(userStoreRoot()).filePath(QStringLiteral("staging"));
    if (!ensureDirectory(scratch, error)) return false;
    AndroidContentStore::ImportLimits limits;
    limits.maxArchiveBytes = 256ULL * 1024 * 1024;
    limits.maxExpandedBytes = 512ULL * 1024 * 1024;
    limits.maxEntryBytes = 128ULL * 1024 * 1024;
    limits.maxEntries = 50000;
    limits.maxCompressionRatio = 200;
    AndroidZipReader reader(scratch);
    std::atomic_bool localCancel{false};
    std::atomic_bool *cancel = cancellation ? cancellation : &localCancel;
    if (!checkUserSpace(limits.maxArchiveBytes, error) || !reader.open(source, limits, cancel, error)) return false;
    // Bound manifest buffering; metadata preflight precedes payload extraction.
    const AndroidZipReader::Entry *manifest = nullptr;
    for (const auto &entry : reader.entries()) {
        if (entry.path == QStringLiteral("manifest.json") || entry.path.endsWith(QStringLiteral("/manifest.json"))) {
            if (manifest) return fail(error, storageTr("Invalid package storage budget."));
            manifest = &entry;
        }
    }
    if (!manifest || manifest->directory || manifest->uncompressedSize > 16ULL * 1024 * 1024)
        return fail(error, storageTr("Invalid package storage budget."));
    QBuffer manifestData; manifestData.open(QIODevice::ReadWrite);
    if (!reader.extract(*manifest, manifestData, cancel, AndroidContentStore::Progress(), error)) return false;
    const auto metadata = QJsonDocument::fromJson(manifestData.data()).object();
    const QString incomingId = metadata.value(QStringLiteral("id")).toString();
    TreeSize incoming;
    QSet<QString> directories;
    const QString prefix = manifest->path.left(manifest->path.size() - QStringLiteral("manifest.json").size());
    for (const auto &entry : reader.entries()) {
        QString relative = entry.path;
        if (entry.directory && relative.endsWith('/')) relative.chop(1);
        if (!safeRelative(relative)) return fail(error, storageTr("Unsafe package storage path."));
        if (entry.directory && !prefix.isEmpty() && relative == prefix.left(prefix.size() - 1)) continue;
        if (!relative.startsWith(prefix)) return fail(error, storageTr("Unsafe package storage path."));
        QString path = relative.mid(prefix.size());
        if (path.isEmpty()) continue;
        if (entry.directory) directories.insert(path);
        else { ++incoming.entries; if (!addSize(incoming.bytes, entry.uncompressedSize, &incoming.bytes, error)) return false; }
        while (path.contains('/')) {
            path = path.left(path.lastIndexOf('/')); directories.insert(path);
            if (quint64(directories.size()) > limits.maxEntries) return fail(error, storageTr("Invalid package storage budget."));
        }
    }
    incoming.entries += quint64(directories.size());
    if (incoming.bytes > limits.maxExpandedBytes || incoming.entries > limits.maxEntries)
        return fail(error, storageTr("Invalid package storage budget."));
    if ((!expectedId.isEmpty() && incomingId != expectedId)
        || (!expectedVersion.isEmpty() && metadata.value(QStringLiteral("version")).toString() != expectedVersion))
        return fail(error, QCoreApplication::translate("PackageStore", "Downloaded package identity does not match the catalog."));
    QVariantMap planned = readJournal(error).value(QStringLiteral("changes")).toMap();
    if (error && !error->isEmpty()) return false;
    planned.insert(incomingId, QStringLiteral("install"));
    if (!preflight(planned, {}, incomingId, incoming, 2, error)) return false;
    QTemporaryDir temp(QDir(scratch).filePath(QStringLiteral("package-XXXXXX")));
    if (!temp.isValid()) return fail(error, QStringLiteral("cannot create package extraction directory"));
    for (const AndroidZipReader::Entry &entry : reader.entries()) {
        if (*cancel) return fail(error, QCoreApplication::translate("PackageStore", "Package extraction cancelled."));
        QString relative = entry.path;
        if (entry.directory && relative.endsWith('/')) relative.chop(1);
        if (!safeRelative(relative)) return fail(error, QStringLiteral("unsafe package archive path: %1").arg(entry.path));
        const QString output = QDir(temp.path()).filePath(relative);
        if (entry.directory) {
            if (!ensureDirectory(output, error)) return false;
            continue;
        }
        if (!ensureDirectory(QFileInfo(output).absolutePath(), error)) return false;
        QSaveFile file(output);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) return fail(error, QStringLiteral("cannot stage archive member: %1").arg(entry.path));
        if (!reader.extract(entry, file, cancel, AndroidContentStore::Progress(), error) || !file.commit())
            return fail(error, QStringLiteral("cannot extract archive member: %1").arg(entry.path));
    }
    QString candidate = temp.path();
    if (!QFileInfo(QDir(candidate).filePath(QStringLiteral("manifest.json"))).isFile()) {
        const QStringList directories = QDir(candidate).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (directories.size() == 1 && QFileInfo(QDir(candidate).filePath(directories.first() + QStringLiteral("/manifest.json"))).isFile())
            candidate = QDir(candidate).filePath(directories.first());
    }
    if (*cancel) return fail(error, QCoreApplication::translate("PackageStore", "Package extraction cancelled."));
    // A catalog must not be able to stage a different package/version than the
    // player selected. Check identity before touching the pending journal.
    if (!expectedId.isEmpty() || !expectedVersion.isEmpty()) {
        QString identityError;
        const auto package = QSanPackages::parsePackage(candidate, &identityError, true);
        if (!identityError.isEmpty()) return fail(error, identityError);
        if (package.id != expectedId || package.version != expectedVersion)
            return fail(error, QCoreApplication::translate("PackageStore", "Downloaded package identity does not match the catalog."));
    }
    if (*cancel) return fail(error, QCoreApplication::translate("PackageStore", "Package extraction cancelled."));
    // Publication is an existing atomic PackageStore operation. Once entered,
    // finish it rather than interrupting its journal/tree transaction.
    return stageValidatedPackage(candidate, error);
}

QSanPackages::Catalog PackageStore::packages() const
{
    return configured() ? QSanPackages::loadCatalog(m_runtimeRoot, true) : QSanPackages::Catalog();
}

QStringList PackageStore::pendingIds() const
{
    if (!configured()) return {};
    QString ignored;
    return readJournal(&ignored).value(QStringLiteral("changes")).toMap().keys();
}

bool PackageStore::hasPending() const { return !pendingIds().isEmpty(); }

bool PackageStore::checkRemoval(const QString &id, QString *error) const
{
    if (!validId(id) || id == QStringLiteral("core")) return fail(error, QStringLiteral("invalid or immutable package id"));
    const QSanPackages::Catalog active = packages();
    if (!active.isValid()) return fail(error, active.error);
    QString ignored;
    const QVariantMap changes = readJournal(&ignored).value(QStringLiteral("changes")).toMap();
    if (!ignored.isEmpty()) return fail(error, ignored);
    const bool installed = std::any_of(active.packages.cbegin(), active.packages.cend(),
                                       [&](const QSanPackages::Package &package) { return package.id == id; });
    return installed || changes.value(id).toString() == QStringLiteral("install")
        ? true : fail(error, QStringLiteral("unknown package: %1").arg(id));
}

bool PackageStore::remove(const QString &id, QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    if (!ensureDirectory(userStoreRoot(), error)) return false;
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(1000)) return fail(error, QStringLiteral("package store is busy"));
    if (!checkRemoval(id, error)) return false;
    QVariantMap journal = readJournal(error);
    if (error && !error->isEmpty()) return false;
    QVariantMap changes = journal.value(QStringLiteral("changes")).toMap();
    changes.insert(id, QStringLiteral("remove"));
    if (!validateEffectiveSet(changes, {}, error)) return false;
    journal.insert(QStringLiteral("changes"), changes);
    journal.remove(QStringLiteral("phase"));
    journal.remove(QStringLiteral("lastError"));
    return writeJournal(journal, error);
}

bool PackageStore::rollback(const QString &id, QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    if (!validId(id) || id == QStringLiteral("core")) return fail(error, QStringLiteral("invalid or immutable package id"));
    const QString previous = QDir(userStoreRoot()).filePath(QStringLiteral("previous/") + id);
    if (!QFileInfo(previous).isDir()) return fail(error, QStringLiteral("no previous version is available for %1").arg(id));
    if (!ensureDirectory(userStoreRoot(), error)) return false;
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(1000)) return fail(error, QStringLiteral("package store is busy"));
    return stageValidatedPackage(previous, error);
}

QVariantList PackageStore::overview() const
{
    QVariantList result;
    const QSanPackages::Catalog active = packages();
    QHash<QString, QString> current;
    for (const auto &package : active.packages) current.insert(package.id, package.version);
    QString ignored;
    const QVariantMap changes = readJournal(&ignored).value(QStringLiteral("changes")).toMap();
    const QString pendingRoot = QDir(userStoreRoot()).filePath(QStringLiteral("pending"));
    QSet<QString> ids;
    for (auto it = current.cbegin(); it != current.cend(); ++it) ids.insert(it.key());
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) ids.insert(it.key());
    QStringList sorted = ids.values();
    std::sort(sorted.begin(), sorted.end());
    for (const QString &id : sorted) {
        QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("version"), current.value(id)},
                        {QStringLiteral("pending"), changes.contains(id)},
                        {QStringLiteral("pendingRemoval"), changes.value(id).toString() == QStringLiteral("remove")},
                        {QStringLiteral("previousAvailable"), QFileInfo(QDir(userStoreRoot()).filePath(QStringLiteral("previous/") + id)).isDir()}};
        if (changes.value(id).toString() == QStringLiteral("install")) {
            QString parseError;
            const auto staged = QSanPackages::parsePackage(QDir(pendingRoot).filePath(id), &parseError, false);
            if (parseError.isEmpty()) row.insert(QStringLiteral("pendingVersion"), staged.version);
        }
        result.append(row);
    }
    const QString lastError = readJournal(&ignored).value(QStringLiteral("lastError")).toString();
    if (!lastError.isEmpty())
        result.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("Package store")},
                                  {QStringLiteral("version"), lastError},
                                  {QStringLiteral("storeError"), true}});
    return result;
}

bool PackageStore::prepareStartup(QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    const QString packages = packagesRoot();
    const QString txn = transactionRoot(m_runtimeRoot);
    if (!isInside(m_runtimeRoot, txn) || QFileInfo(txn).isSymLink()) return fail(error, QStringLiteral("unsafe transaction directory"));
    const QString next = QDir(txn).filePath(QStringLiteral("next/packages"));
    const QString old = QDir(txn).filePath(QStringLiteral("old/packages"));
    QVariantMap journal = readJournal(error);
    if (error && !error->isEmpty()) return false;
    const QVariantMap changes = journal.value(QStringLiteral("changes")).toMap();
    // A normal installation with no package-store state must remain a pure
    // read path; in particular it must work from a read-only asset directory.
    if (changes.isEmpty() && !QFileInfo(txn).exists()) return true;
    if (!ensureDirectory(m_runtimeRoot, error) || !ensureDirectory(userStoreRoot(), error)) return false;
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(1000)) return fail(error, QStringLiteral("package store is busy"));

    // The active tree was already published. Do not replay its changes even
    // if the previous process stopped before clearing the journal.
    if (journal.value(QStringLiteral("phase")).toString() == QStringLiteral("activated")) {
        const QSanPackages::Catalog active = QSanPackages::loadCatalog(m_runtimeRoot, true);
        if (!active.isValid()) return fail(error, QStringLiteral("activated package set failed validation: %1").arg(active.error));
        if (!changes.isEmpty()) {
            QSaveFile attemptFile(QDir(userStoreRoot()).filePath(QStringLiteral("last-applied.json")));
            attemptFile.setDirectWriteFallback(false);
            const QByteArray attemptBytes = QJsonDocument::fromVariant(
                QVariantMap{{QStringLiteral("ids"), changes.keys()}}).toJson(QJsonDocument::Compact);
            if (!attemptFile.open(QIODevice::WriteOnly) || attemptFile.write(attemptBytes) != attemptBytes.size()
                || !attemptFile.commit()) return fail(error, QStringLiteral("cannot record recovered package activation"));
        }
        journal.insert(QStringLiteral("changes"), QVariantMap());
        journal.remove(QStringLiteral("phase"));
        if (!writeJournal(journal, error)) return false;
        if (QFileInfo(old).exists() && !removeTree(old, error)) return false;
        if (QFileInfo(txn).exists() && !removeTree(txn, error)) return false;
        return true;
    }

    // Resolve a process interruption using filesystem state plus the durable
    // phase marker. When old exists, finish activation if a complete next tree
    // exists; otherwise restore old, so the engine never sees a mixed catalog.
    if (QFileInfo(old).isDir()) {
        if (QFileInfo(next).isDir()) {
            const QSanPackages::Catalog recovered = QSanPackages::loadCatalog(QFileInfo(next).absolutePath(), true);
            if (!recovered.isValid()) {
                if (QFileInfo(packages).exists() && !removeTree(packages, error)) return false;
                if (!QDir().rename(old, packages))
                    return fail(error, QStringLiteral("interrupted package set is invalid and the previous set cannot be restored"));
                journal.insert(QStringLiteral("changes"), QVariantMap());
                journal.remove(QStringLiteral("phase"));
                journal.insert(QStringLiteral("lastError"), recovered.error);
                if (!writeJournal(journal, error) || !removeTree(txn, error)) return false;
                return true;
            }
            if (QFileInfo(packages).exists() && !removeTree(packages, error)) return false;
            if (!QDir().rename(next, packages)) return fail(error, QStringLiteral("cannot finish interrupted package activation"));
        } else if (!QFileInfo(packages).exists() && !QDir().rename(old, packages)) {
            return fail(error, QStringLiteral("cannot restore the previous package tree"));
        }
        if (!journal.value(QStringLiteral("changes")).toMap().isEmpty()) {
            QSaveFile attemptFile(QDir(userStoreRoot()).filePath(QStringLiteral("last-applied.json")));
            attemptFile.setDirectWriteFallback(false);
            const QByteArray attemptBytes = QJsonDocument::fromVariant(
                QVariantMap{{QStringLiteral("ids"), journal.value(QStringLiteral("changes")).toMap().keys()}})
                                                    .toJson(QJsonDocument::Compact);
            if (!attemptFile.open(QIODevice::WriteOnly) || attemptFile.write(attemptBytes) != attemptBytes.size()
                || !attemptFile.commit()) return fail(error, QStringLiteral("cannot record recovered package activation"));
        }
        journal.remove(QStringLiteral("phase"));
        if (!journal.value(QStringLiteral("changes")).toMap().isEmpty()) {
            journal.insert(QStringLiteral("changes"), QVariantMap());
            if (!writeJournal(journal, error)) return false;
        }
        if (QFileInfo(old).exists() && !removeTree(old, error)) return false;
        if (QFileInfo(txn).exists() && !removeTree(txn, error)) return false;
        return true;
    }

    if (changes.isEmpty()) return true;
    QString pendingValidationError;
    if (!preflight(changes, {}, QString(), {}, 0, &pendingValidationError)) {
        const auto active = QSanPackages::loadCatalog(m_runtimeRoot, true);
        if (!active.isValid()) return fail(error, active.error);
        journal.insert(QStringLiteral("lastError"), pendingValidationError);
        journal.remove(QStringLiteral("phase"));
        return writeJournal(journal, error);
    }
    if (!validateEffectiveSet(changes, {}, &pendingValidationError)) {
        const QSanPackages::Catalog active = QSanPackages::loadCatalog(m_runtimeRoot, true);
        if (!active.isValid())
            return fail(error, QStringLiteral("active package set failed validation: %1").arg(active.error));
        journal.remove(QStringLiteral("phase"));
        journal.insert(QStringLiteral("lastError"), pendingValidationError);
        if (!writeJournal(journal, error)) return false;
        if (QFileInfo(txn).exists() && !removeTree(txn, error)) return false;
        return true;
    }
    if (QFileInfo(txn).exists() && !removeTree(txn, error)) return false;
    auto buildNext = [&]() -> bool {
        if (!ensureDirectory(QFileInfo(next).absolutePath(), error) || !ensureDirectory(next, error)) return false;
        if (QFileInfo(packages).exists() && !copyTree(packages, next, error, m_limits.maxInstalledBytes, m_limits.maxInstalledEntries)) return false;
        if (!QFileInfo(packages).exists() && !ensureDirectory(next, error)) return false;

        const QDir staged(QDir(userStoreRoot()).filePath(QStringLiteral("pending")));
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            const QString id = it.key();
            if (!validId(id) || id == QStringLiteral("core")) return fail(error, QStringLiteral("journal contains an invalid package id"));
            const QString nextPath = QDir(next).filePath(id);
            if (it.value().toString() == QStringLiteral("install")) {
                QString packageError;
                const auto package = QSanPackages::parsePackage(staged.filePath(id), &packageError, true);
                if (!packageError.isEmpty() || package.id != id) return fail(error, packageError.isEmpty() ? QStringLiteral("staged package id mismatch") : packageError);
                if (!removeTree(nextPath, error) || !copyTree(package.root, nextPath, error, 512ULL * 1024 * 1024, 50000)) return false;
            } else if (it.value().toString() == QStringLiteral("remove")) {
                if (!removeTree(nextPath, error)) return false;
            } else return fail(error, QStringLiteral("unknown pending package operation"));
        }
        TreeSize finalSize;
        if (!treeSize(next, m_limits.maxInstalledBytes, m_limits.maxInstalledEntries, &finalSize, error)) return false;
        const QString parent = QFileInfo(next).absolutePath();
        const QSanPackages::Catalog candidate = QSanPackages::loadCatalog(parent, true);
        if (!candidate.isValid()) return fail(error, candidate.error);

        const QDir previousRoot(QDir(userStoreRoot()).filePath(QStringLiteral("previous")));
        if (!ensureDirectory(previousRoot.absolutePath(), error)) return false;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            const QString id = it.key();
            const QString activePath = QDir(packages).filePath(id);
            const QString previousPath = previousRoot.filePath(id);
            if (QFileInfo(activePath).isDir()) {
                if (!removeTree(previousPath, error) || !copyTree(activePath, previousPath, error, 512ULL * 1024 * 1024, 50000)) return false;
            } else if (!removeTree(previousPath, error)) return false;
        }

        return true;
    };
    if (!buildNext()) {
        const QString buildError = error ? *error : storageTr("Invalid package storage budget.");
        const auto active = QSanPackages::loadCatalog(m_runtimeRoot, true);
        if (!active.isValid()) return fail(error, active.error);
        if (!removeTree(txn, error)) return false;
        journal.insert(QStringLiteral("lastError"), buildError);
        journal.remove(QStringLiteral("phase"));
        if (error) error->clear();
        return writeJournal(journal, error);
    }

    // Keep old and next beside the active directory so both renames stay on
    // runtimeRoot's filesystem; persistent per-package rollback copies live in
    // userDataRoot/package-store/previous.
    journal.insert(QStringLiteral("phase"), QStringLiteral("ready"));
    if (!writeJournal(journal, error)) return false;
    if (!ensureDirectory(QFileInfo(old).absolutePath(), error)) return false;
    // Even a first install needs an old-tree sentinel. If the process stops
    // after next -> packages but before journaling "activated", recovery can
    // then recognize the in-flight activation instead of replaying it.
    if (!QFileInfo(packages).exists() && !ensureDirectory(packages, error)) return false;
    if (!QDir().rename(packages, old))
        return fail(error, QStringLiteral("cannot move active package tree; runtime directory must be writable"));
    journal.insert(QStringLiteral("phase"), QStringLiteral("old_moved"));
    if (!writeJournal(journal, error)) {
        if (!QFileInfo(packages).exists()) QDir().rename(old, packages);
        return false;
    }
    if (!QDir().rename(next, packages)) {
        if (!QFileInfo(packages).exists()) QDir().rename(old, packages);
        return fail(error, QStringLiteral("cannot activate pending packages"));
    }
    journal.insert(QStringLiteral("phase"), QStringLiteral("activated"));
    if (!writeJournal(journal, error)) return false;
    journal.insert(QStringLiteral("changes"), QVariantMap());
    journal.remove(QStringLiteral("phase"));
    QSaveFile attemptFile(QDir(userStoreRoot()).filePath(QStringLiteral("last-applied.json")));
    attemptFile.setDirectWriteFallback(false);
    const QByteArray attemptBytes = QJsonDocument::fromVariant(
        QVariantMap{{QStringLiteral("ids"), changes.keys()}}).toJson(QJsonDocument::Compact);
    if (!attemptFile.open(QIODevice::WriteOnly) || attemptFile.write(attemptBytes) != attemptBytes.size()
        || !attemptFile.commit()) return fail(error, QStringLiteral("cannot record package activation attempt"));
    if (!writeJournal(journal, error)) return false;
    if (!removeTree(old, error) || !removeTree(txn, error)) return false;
    return true;
}

bool PackageStore::beginBootAttempt(QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    const QVariantMap changes = readObject(QDir(userStoreRoot()).filePath(QStringLiteral("last-applied.json")), error);
    if (error && !error->isEmpty()) return false;
    if (changes.isEmpty()) return true;
    QSaveFile file(QDir(userStoreRoot()).filePath(QStringLiteral("boot-attempt.json")));
    file.setDirectWriteFallback(false);
    const QByteArray bytes = QJsonDocument::fromVariant(changes).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("cannot record package boot attempt"));
    QFile::remove(QDir(userStoreRoot()).filePath(QStringLiteral("last-applied.json")));
    return true;
}

bool PackageStore::markBootSuccessful(QString *error)
{
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    const QString path = QDir(userStoreRoot()).filePath(QStringLiteral("boot-attempt.json"));
    if (!QFileInfo::exists(path)) return true;
    return QFile::remove(path) || fail(error, QStringLiteral("cannot clear package boot marker"));
}

bool PackageStore::hasUnsuccessfulBootAttempt() const
{
    return configured() && QFileInfo::exists(QDir(userStoreRoot()).filePath(QStringLiteral("boot-attempt.json")));
}

bool PackageStore::recoverPrevious(QString *error)
{
    if (error) error->clear();
    if (!configured()) return fail(error, QStringLiteral("runtime and user data roots are required"));
    if (!ensureDirectory(userStoreRoot(), error)) return false;
    QLockFile lock(QDir(userStoreRoot()).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(1000)) return fail(error, QStringLiteral("package store is busy"));
    QVariantMap marker = readObject(QDir(userStoreRoot()).filePath(QStringLiteral("boot-attempt.json")), error);
    if (error && !error->isEmpty()) return false;
    const QStringList ids = marker.value(QStringLiteral("ids")).toStringList();
    if (ids.isEmpty()) return fail(error, QStringLiteral("no failed package boot attempt is recorded"));
    QVariantMap recoveryChanges = readJournal(error).value(QStringLiteral("changes")).toMap();
    if (error && !error->isEmpty()) return false;
    QVariantMap recoverySources;
    TreeSize recoverySize;
    for (const QString &id : ids) {
        if (!validId(id) || id == QStringLiteral("core")) return fail(error, storageTr("Invalid package storage budget."));
        const QString previous = QDir(userStoreRoot()).filePath(QStringLiteral("previous/") + id);
        if (QFileInfo(previous).isDir()) {
            TreeSize size;
            if (!treeSize(previous, 512ULL * 1024 * 1024, 50000, &size, error)) return false;
            ++size.entries;
            if (!addTree(&recoverySize, size, error)) return false;
            recoveryChanges.insert(id, QStringLiteral("install")); recoverySources.insert(id, previous);
        } else recoveryChanges.insert(id, QStringLiteral("remove"));
    }
    if (!fitsQuota(recoverySize, m_limits, error)
        || !preflight(recoveryChanges, recoverySources, QString(), recoverySize, 2, error)) return false;
    QTemporaryDir recovery(QDir(userStoreRoot()).filePath(QStringLiteral("recovery-XXXXXX")));
    if (!recovery.isValid()) return fail(error, QStringLiteral("cannot create rollback workspace"));
    if (!ensureDirectory(QDir(userStoreRoot()).filePath(QStringLiteral("pending")), error)) return false;
    QVariantMap journal = readJournal(error);
    if (error && !error->isEmpty()) return false;
    QVariantMap changes = journal.value(QStringLiteral("changes")).toMap();
    QVariantMap overrideSources;
    for (const QString &id : ids) {
        if (!validId(id) || id == QStringLiteral("core"))
            return fail(error, QStringLiteral("boot marker contains an invalid or immutable package id"));
        const QString previous = QDir(userStoreRoot()).filePath(QStringLiteral("previous/") + id);
        if (QFileInfo(previous).isDir()) {
            const QString staged = QDir(recovery.path()).filePath(id);
            if (!copyTree(previous, staged, error, 512ULL * 1024 * 1024, 50000)) return false;
            QString parseError;
            const QSanPackages::Package package = QSanPackages::parsePackage(staged, &parseError, true);
            if (!parseError.isEmpty() || package.id != id)
                return fail(error, parseError.isEmpty() ? QStringLiteral("previous package id mismatch: %1").arg(id) : parseError);
            overrideSources.insert(id, staged);
            changes.insert(id, QStringLiteral("install"));
        } else {
            changes.insert(id, QStringLiteral("remove"));
        }
    }
    if (!validateEffectiveSet(changes, overrideSources, error)) return false;

    const QString pendingRoot = QDir(userStoreRoot()).filePath(QStringLiteral("pending"));
    for (auto it = overrideSources.cbegin(); it != overrideSources.cend(); ++it) {
        const QString destination = QDir(pendingRoot).filePath(it.key());
        const QString temporary = QDir(pendingRoot).filePath(it.key() + QStringLiteral(".rollback-incoming"));
        if (!removeTree(temporary, error) || !copyTree(it.value().toString(), temporary, error, 512ULL * 1024 * 1024, 50000)
            || !removeTree(destination, error) || !QDir().rename(temporary, destination))
            return fail(error, QStringLiteral("cannot stage previous package %1").arg(it.key()));
    }
    journal.insert(QStringLiteral("changes"), changes);
    journal.remove(QStringLiteral("phase"));
    journal.remove(QStringLiteral("lastError"));
    if (!writeJournal(journal, error)) return false;
    for (const QString &id : ids)
        if (changes.value(id).toString() == QStringLiteral("remove")
            && !removeTree(QDir(pendingRoot).filePath(id), error)) return false;
    return QFile::remove(QDir(userStoreRoot()).filePath(QStringLiteral("boot-attempt.json")))
        || fail(error, QStringLiteral("cannot clear package boot marker"));
}
