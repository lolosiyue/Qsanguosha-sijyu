#include "runtime-paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef QSAN_BIN_TO_DATA_RELATIVE
// bin/ 相對 share/qsanguosha/ 的位置，由 CMake 依 GNUInstallDirs 算出。
// 刻意用相對路徑而唔係 CMAKE_INSTALL_FULL_DATADIR：binary 入面唔應該
// 燒死絕對路徑，安裝樹要可以整個搬走／解壓到任何地方都行得到。
#define QSAN_BIN_TO_DATA_RELATIVE "../share/qsanguosha"
#endif

namespace
{
using QSanRuntimePaths::AssetRootSource;

QSanRuntimePaths::Resolution g_resolution;

// 一個目錄夠唔夠資格做 asset root：engine bootstrap 一定要開到呢兩個檔，
// 開唔到就會喺 constructor 入面 exit(1)。用佢哋做 marker，等我哋喺
// 「揀錯 root 然後靜靜死」之前就發現問題。
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

// 明確指定嘅 root（CLI／env）唔合格就係硬錯誤。靜靜落 fallback 會令
// 「我明明指咗 --asset-root」變成一個查極都查唔到嘅 bug。
bool acceptExplicitRoot(const QString &raw, const QString &sourceLabel, AssetRootSource source,
                        bool *failed)
{
    *failed = false;
    if (raw.isEmpty())
        return false;
    const QString absolute = cleanedAbsolutePath(raw);
    if (!QDir(absolute).exists()) {
        recordCandidate(sourceLabel, absolute, QStringLiteral("missing"));
        g_resolution.error = QStringLiteral("%1 points at a directory that does not exist: %2")
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

// 安裝／打包出嚟嘅 asset root 唔應該被寫入（AppImage 係唯讀 squashfs，
// /usr/share 通常唔屬於使用者）。開發樹就維持舊行為，record／AiData 照舊
// 寫喺工作目錄，唔會突然搬走開發者慣用嘅檔案。
bool assetRootIsPackaged(AssetRootSource source)
{
    switch (source) {
    case AssetRootSource::CommandLine:
    case AssetRootSource::Environment:
    case AssetRootSource::InstalledPrefix:
    case AssetRootSource::PortableBundle:
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
    // 刻意唔靠 QCoreApplication 嘅 organizationName／applicationName：
    // 呢兩個值喺 GUI 同 dedicated server 之間唔一定一樣，但兩者要寫入同一個
    // 使用者資料目錄。
    const QString generic =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (generic.isEmpty())
        return QDir::homePath() + QStringLiteral("/.local/share/QSanguosha");
    return generic + QStringLiteral("/QSanguosha");
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

    if (g_resolution.error.isEmpty() && g_resolution.assetRoot.isEmpty()
        && !g_resolution.applicationDir.isEmpty()) {
        const QDir appDir(g_resolution.applicationDir);
        // 安裝／可攜版面行先：一個打包好嘅 binary 唔應該因為使用者
        // 碰巧喺一個舊 source tree 入面開佢，就走去用嗰邊嘅資產。
        tryCandidate(appDir.filePath(QStringLiteral(QSAN_BIN_TO_DATA_RELATIVE)),
                     QStringLiteral("installed-prefix"), AssetRootSource::InstalledPrefix)
            || tryCandidate(appDir.filePath(QStringLiteral("share/qsanguosha")),
                            QStringLiteral("portable-bundle"), AssetRootSource::PortableBundle)
            || tryCandidate(QDir::currentPath(), QStringLiteral("working-directory"),
                            AssetRootSource::WorkingDirectory)
            // application-parent 行先過 application-dir:build 輸出目錄
            // (relwithdebinfo/)有時只得 deploy-server 抄過去嘅 lua/,係一份
            // 唔完整嘅樹;佢上一層先至係真正齊料嘅 source tree。真正嘅
            // 平面部署目錄(exe 同 lua/ 同一層)上一層唔會有 lua/config.lua,
            // 所以照樣落返 application-dir。
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
        // 過渡橋樑：engine／skin bank 仍然用 "lua/..."、"image/..." 相對路徑。
        // 一次過改 CWD 令佢哋指向解析出嚟嘅 asset root，而唔係使用者嘅 CWD。
        QDir::setCurrent(g_resolution.assetRoot);
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
