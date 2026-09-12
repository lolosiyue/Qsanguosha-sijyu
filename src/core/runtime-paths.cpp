#include "runtime-paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include "android_assets.h"
#endif

#ifndef QSAN_BIN_TO_DATA_RELATIVE
// Location of share/qsanguosha/ relative to bin/, computed by CMake from GNUInstallDirs.
// Deliberately a relative path instead of CMAKE_INSTALL_FULL_DATADIR: no absolute path may
// be burned into the binary; the install tree must keep working after being moved or
// unpacked anywhere.
#define QSAN_BIN_TO_DATA_RELATIVE "../share/qsanguosha"
#endif

namespace
{
using QSanRuntimePaths::AssetRootSource;

QSanRuntimePaths::Resolution g_resolution;

// What qualifies a directory as the asset root: the engine bootstrap must be able to open
// these two files, otherwise it exits with exit(1) in its constructor. Using them as
// markers lets us fail before "wrong root chosen, then die quietly".
bool looksLikeAssetRoot(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QDir dir(path);
    return dir.exists()
        && QFileInfo::exists(dir.filePath(QStringLiteral("lua/config.lua")))
        && QFileInfo::exists(dir.filePath(QStringLiteral("lua/sanguosha.lua")));
}

QString cleanedAbsolutePath(const QString &path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// --asset-root <path> / --asset-root=<path>
QString commandLineOverride(const QStringList &arguments, const QString &name, bool *present)
{
    *present = false;
    const QString flag = QStringLiteral("--") + name;
    const QString flagEquals = flag + QLatin1Char('=');
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument.startsWith(flagEquals)) {
            *present = true;
            return argument.mid(flagEquals.size());
        }
        if (argument == flag) {
            *present = true;
            return i + 1 < arguments.size() ? arguments.at(i + 1) : QString();
        }
    }
    return QString();
}

void recordCandidate(const QString &source, const QString &path, const QString &verdict)
{
    g_resolution.candidates.append(
        QStringLiteral("%1=%2=%3").arg(source, path.isEmpty() ? QStringLiteral("<unset>") : path, verdict));
}

// An explicitly specified root (CLI/env) that fails validation is a hard error. Falling
// back silently would turn "I did pass --asset-root" into a bug that is nearly impossible
// to trace.
bool acceptExplicitRoot(const QString &raw, const QString &sourceLabel, AssetRootSource source,
                        bool *failed)
{
    *failed = false;
    if (raw.isEmpty())
        return false;
    const QString absolute = cleanedAbsolutePath(raw);
    if (!QDir(absolute).exists()) {
        // "Does not exist" and "exists but is not a directory" are two different typos (the
        // latter usually points at a file); being precise is what actually helps.
        const bool existsButNotADirectory = QFileInfo::exists(absolute);
        recordCandidate(sourceLabel, absolute,
                        existsButNotADirectory ? QStringLiteral("not-a-directory")
                                               : QStringLiteral("missing"));
        g_resolution.error = existsButNotADirectory
            ? QStringLiteral("%1 is not a directory: %2").arg(sourceLabel, absolute)
            : QStringLiteral("%1 points at a directory that does not exist: %2")
                  .arg(sourceLabel, absolute);
        *failed = true;
        return false;
    }
    if (!looksLikeAssetRoot(absolute)) {
        recordCandidate(sourceLabel, absolute, QStringLiteral("no-lua-config"));
        g_resolution.error =
            QStringLiteral("%1 is not a QSanguosha asset root (lua/config.lua and "
                           "lua/sanguosha.lua are missing): %2")
                .arg(sourceLabel, absolute);
        *failed = true;
        return false;
    }
    recordCandidate(sourceLabel, absolute, QStringLiteral("accepted"));
    g_resolution.assetRoot = absolute;
    g_resolution.assetRootSource = source;
    return true;
}

bool tryCandidate(const QString &path, const QString &sourceLabel, AssetRootSource source)
{
    if (path.isEmpty())
        return false;
    const QString absolute = cleanedAbsolutePath(path);
    if (!looksLikeAssetRoot(absolute)) {
        recordCandidate(sourceLabel, absolute,
                        QDir(absolute).exists() ? QStringLiteral("no-lua-config")
                                                : QStringLiteral("missing"));
        return false;
    }
    recordCandidate(sourceLabel, absolute, QStringLiteral("accepted"));
    g_resolution.assetRoot = absolute;
    g_resolution.assetRootSource = source;
    return true;
}

// An installed/packaged asset root must not be written to (AppImage is a read-only
// squashfs, /usr/share is usually not user-owned). The dev tree keeps the old behavior:
// record/AiData stay in the working directory, so the files developers are used to do not
// suddenly move away.
bool assetRootIsPackaged(AssetRootSource source)
{
    switch (source) {
    case AssetRootSource::CommandLine:
    case AssetRootSource::Environment:
    case AssetRootSource::InstalledPrefix:
    case AssetRootSource::PortableBundle:
    case AssetRootSource::AndroidApplicationData:
        return true;
    case AssetRootSource::WorkingDirectory:
    case AssetRootSource::ApplicationDir:
    case AssetRootSource::ApplicationParent:
    case AssetRootSource::None:
        break;
    }
    return false;
}

QString xdgUserDataRoot()
{
#ifdef Q_OS_ANDROID
    // 設定／紀錄與可編輯的 Lua runtime 分開，全部留在 app 私有空間。
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appData.isEmpty() ? QString() : QDir(appData).filePath(QStringLiteral("userdata"));
#else
    // Deliberately not based on QCoreApplication's organizationName/applicationName: those
    // two may differ between the GUI and the dedicated server, yet both must write into
    // the same user data directory.
    const QString generic =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (generic.isEmpty())
        return QDir::homePath() + QStringLiteral("/.local/share/QSanguosha");
    return generic + QStringLiteral("/QSanguosha");
#endif
}
}

namespace QSanRuntimePaths
{
bool resolve(const QStringList &arguments, QString *error)
{
    if (g_resolution.resolved) {
        if (error != nullptr)
            *error = g_resolution.error;
        return g_resolution.error.isEmpty();
    }

    g_resolution.applicationDir = QCoreApplication::instance() != nullptr
        ? QCoreApplication::applicationDirPath()
        : QString();

    bool present = false;
    bool failed = false;
    const QString cliRoot = commandLineOverride(arguments, QStringLiteral("asset-root"), &present);
    if (present && cliRoot.isEmpty()) {
        g_resolution.error = QStringLiteral("--asset-root requires a directory argument");
    } else if (present) {
        acceptExplicitRoot(cliRoot, QStringLiteral("--asset-root"), AssetRootSource::CommandLine,
                           &failed);
    }

    if (g_resolution.error.isEmpty() && g_resolution.assetRoot.isEmpty()) {
        const QString envRoot = qEnvironmentVariable("QSAN_ASSET_ROOT");
        if (!envRoot.isEmpty())
            acceptExplicitRoot(envRoot, QStringLiteral("QSAN_ASSET_ROOT"),
                               AssetRootSource::Environment, &failed);
    }

#ifdef Q_OS_ANDROID
    if (g_resolution.error.isEmpty() && g_resolution.assetRoot.isEmpty()) {
        // QApplication 已建立；每次啟動只補缺少的隨包檔，不覆蓋使用者擴展。
        // 釋出失敗不能退回另一份規則，也不能帶著部分 runtime 建立 Engine。
        const QString prepared = QCoreApplication::instance()
            ? QCoreApplication::instance()->property("androidRuntimeRoot").toString() : QString();
        const QString root = prepared.isEmpty() ? AndroidAssets::getWritableDataPath() : prepared;
        // The content store has already composed this immutable tree. Never patch
        // a live version in place: unchanged files may share hardlinked inodes.
        if (prepared.isEmpty() && !AndroidAssets::copyAssetsToWritableLocation(&g_resolution.error)) {
            recordCandidate(QStringLiteral("android-app-data"), root,
                            QStringLiteral("extraction-failed"));
        } else if (!tryCandidate(root, QStringLiteral("android-app-data"),
                                 AssetRootSource::AndroidApplicationData)) {
            g_resolution.error = QStringLiteral("The Android runtime is missing core Lua files: %1")
                                     .arg(root);
        }
    }
#endif

    if (g_resolution.error.isEmpty() && g_resolution.assetRoot.isEmpty()
        && !g_resolution.applicationDir.isEmpty()) {
        const QDir appDir(g_resolution.applicationDir);
        // Installed/portable layouts come first: a packaged binary must not use the assets
        // of some old source tree just because the user happened to launch it from inside
        // one.
        tryCandidate(appDir.filePath(QStringLiteral(QSAN_BIN_TO_DATA_RELATIVE)),
                     QStringLiteral("installed-prefix"), AssetRootSource::InstalledPrefix)
            || tryCandidate(appDir.filePath(QStringLiteral("share/qsanguosha")),
                            QStringLiteral("portable-bundle"), AssetRootSource::PortableBundle)
            || tryCandidate(QDir::currentPath(), QStringLiteral("working-directory"),
                            AssetRootSource::WorkingDirectory)
            // application-parent ranks above application-dir: a build output directory
            // (relwithdebinfo/) sometimes only has the lua/ copied over by deploy-server,
            // an incomplete tree; the real complete source tree is one level up. A true flat
            // deployment directory (exe next to lua/) has no lua/config.lua one level up,
            // so it falls through to application-dir as usual.
            || tryCandidate(appDir.filePath(QStringLiteral("..")),
                            QStringLiteral("application-parent"),
                            AssetRootSource::ApplicationParent)
            || tryCandidate(g_resolution.applicationDir, QStringLiteral("application-dir"),
                            AssetRootSource::ApplicationDir);
    }

    if (g_resolution.error.isEmpty() && g_resolution.assetRoot.isEmpty()) {
        g_resolution.error = QStringLiteral(
            "Unable to locate the QSanguosha data directory. Pass --asset-root <path> or set "
            "QSAN_ASSET_ROOT to a directory that contains lua/config.lua.");
    }

    const QString userOverride = qEnvironmentVariable("QSAN_USER_DATA_ROOT");
    if (!userOverride.isEmpty())
        g_resolution.userDataRoot = cleanedAbsolutePath(userOverride);
    else if (assetRootIsPackaged(g_resolution.assetRootSource))
        g_resolution.userDataRoot = cleanedAbsolutePath(xdgUserDataRoot());
    else if (!g_resolution.assetRoot.isEmpty())
        g_resolution.userDataRoot = g_resolution.assetRoot;
    else
        g_resolution.userDataRoot = cleanedAbsolutePath(xdgUserDataRoot());

    g_resolution.resolved = true;

    if (g_resolution.error.isEmpty()) {
        // Transitional bridge: the engine and skin bank still use relative paths like
        // "lua/..." and "image/...". Switch CWD once so they point at the resolved asset
        // root instead of the user's CWD.
        if (!QDir::setCurrent(g_resolution.assetRoot))
            g_resolution.error = QStringLiteral("Unable to enter the QSanguosha data directory: %1")
                                     .arg(g_resolution.assetRoot);
    }

    if (error != nullptr)
        *error = g_resolution.error;
    return g_resolution.error.isEmpty();
}

bool isResolved()
{
    return g_resolution.resolved;
}

const Resolution &resolution()
{
    return g_resolution;
}

QString applicationDir()
{
    return g_resolution.applicationDir;
}

QString assetRoot()
{
    return g_resolution.assetRoot;
}

QString userDataRoot()
{
    return g_resolution.userDataRoot;
}

QString assetPath(const QString &relative)
{
    const QString root = assetRoot();
    if (relative.isEmpty())
        return root;
    if (root.isEmpty())
        return relative;
    return QDir(root).filePath(relative);
}

QString userDataPath(const QString &relative)
{
    QString root = userDataRoot();
    if (root.isEmpty())
        root = QDir::currentPath();
    if (relative.isEmpty()) {
        QDir().mkpath(root);
        return root;
    }
    const QString full = QDir(root).filePath(relative);
    const QString parent = QFileInfo(full).absolutePath();
    if (!parent.isEmpty())
        QDir().mkpath(parent);
    return full;
}

QString recordDir()
{
    const QString path = userDataPath(QStringLiteral("record"));
    QDir().mkpath(path);
    return path;
}

QString readablePath(const QString &relative)
{
    if (relative.isEmpty())
        return QString();
    const QString root = userDataRoot();
    if (!root.isEmpty()) {
        const QString candidate = QDir(root).filePath(relative);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    const QString bundled = assetPath(relative);
    if (QFileInfo::exists(bundled))
        return bundled;
    return root.isEmpty() ? bundled : QDir(root).filePath(relative);
}

QString customSceneDir()
{
    const QString path = userDataPath(QStringLiteral("etc/customScenes"));
    QDir().mkpath(path);
    return path;
}

QString sourceName(AssetRootSource source)
{
    switch (source) {
    case AssetRootSource::CommandLine:
        return QStringLiteral("command-line");
    case AssetRootSource::Environment:
        return QStringLiteral("environment");
    case AssetRootSource::InstalledPrefix:
        return QStringLiteral("installed-prefix");
    case AssetRootSource::PortableBundle:
        return QStringLiteral("portable-bundle");
    case AssetRootSource::WorkingDirectory:
        return QStringLiteral("working-directory");
    case AssetRootSource::ApplicationDir:
        return QStringLiteral("application-dir");
    case AssetRootSource::ApplicationParent:
        return QStringLiteral("application-parent");
    case AssetRootSource::AndroidApplicationData:
        return QStringLiteral("android-app-data");
    case AssetRootSource::None:
        break;
    }
    return QStringLiteral("none");
}

QVariantMap describe()
{
    QVariantMap map;
    map.insert(QStringLiteral("resolved"), g_resolution.resolved);
    map.insert(QStringLiteral("application_dir"), g_resolution.applicationDir);
    map.insert(QStringLiteral("asset_root"), g_resolution.assetRoot);
    map.insert(QStringLiteral("asset_root_source"), sourceName(g_resolution.assetRootSource));
    map.insert(QStringLiteral("user_data_root"), g_resolution.userDataRoot);
    map.insert(QStringLiteral("packaged"), assetRootIsPackaged(g_resolution.assetRootSource));
    map.insert(QStringLiteral("candidates"), g_resolution.candidates);
    if (!g_resolution.error.isEmpty())
        map.insert(QStringLiteral("error"), g_resolution.error);
    return map;
}

void resetForTesting()
{
    g_resolution = Resolution();
}
}
