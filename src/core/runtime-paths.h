#ifndef QSAN_RUNTIME_PATHS_H
#define QSAN_RUNTIME_PATHS_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Runtime layout resolution (Linux GUI M3).
//
// The game historically assumed `CWD == repository root`: the engine loaded
// "lua/config.lua" directly and the skin bank opened "image/..." directly. After
// installing/packaging that assumption no longer holds, so every "where are the assets"
// and "where is writable data" decision is centralized in this module instead of call
// sites gluing paths with QDir::currentPath().
//
// The resolved asset root is setCurrent()ed once at startup (see the resolve() comment),
// so legacy relative paths keep working — but their target changes from "wherever the
// user happened to launch the game" to "a real, verified asset directory".
namespace QSanRuntimePaths
{
// Which source decides the asset root; the Android private directory ranks after CLI/env.
enum class AssetRootSource {
    None,
    CommandLine,        // --asset-root <path>
    Environment,        // QSAN_ASSET_ROOT
    InstalledPrefix,    // <appDir>/../share/qsanguosha（GNUInstallDirs 安裝樹）
    PortableBundle,     // <appDir>/share/qsanguosha（可攜／AppImage 版面）
    WorkingDirectory,   // 目前工作目錄（開發樹的既有行為）
    ApplicationDir,     // binary 隔籬（Windows deploy 版面）
    ApplicationParent,  // <appDir>/.. (build output directory outside the source tree)
    AndroidApplicationData, // <AppDataLocation>/runtime（APK 逐檔補齊的可寫副本）
};

struct Resolution
{
    bool resolved = false;
    QString assetRoot;
    AssetRootSource assetRootSource = AssetRootSource::None;
    QString userDataRoot;
    QString applicationDir;
    // Every candidate tried, as "source=path=verdict"; used by missing-asset diagnostics and
    // the smoke report.
    QStringList candidates;
    // An explicitly specified (CLI/env) root that failed validation: treat as an error,
    // never fall back silently.
    QString error;
};

// Resolves once and memoizes the result. arguments is the full argv (including argv[0]) or
// QCoreApplication::arguments(); repeated calls reuse the first result.
//
// On success it does QDir::setCurrent(assetRoot()): the M3 transitional bridge for legacy
// relative-path call sites, making "launch from any CWD" work immediately. New code should
// use assetPath()/userDataPath() and stop relying on CWD.
bool resolve(const QStringList &arguments, QString *error = nullptr);

bool isResolved();
const Resolution &resolution();

QString applicationDir();
QString assetRoot();
QString userDataRoot();
// asset root 係咪打包版面（--asset-root／QSAN_ASSET_ROOT／安裝樹／可攜包）。
// 未 resolve 或者開發樹就係 false。
bool isPackaged();

// Assets under assetRoot; on Android this is a writable copy. With relative empty, returns
// assetRoot().
QString assetPath(const QString &relative);
// Writable files under userDataRoot; the parent directory is created as a side effect.
QString userDataPath(const QString &relative);
// 對局記錄／replay 的目錄（會建立）。
QString recordDir();
// Content the user may customize but that also ships a bundled version (custom scenarios
// and the like): user data first, falling back to the asset tree when not found. The
// returned path is not guaranteed to exist.
QString readablePath(const QString &relative);
// 自訂劇本目錄（可寫，會建立）。
QString customSceneDir();

QString sourceName(AssetRootSource source);
// JSON-able description for the smoke report/diagnostics; carries no sensitive data beyond
// the home directory.
QVariantMap describe();

// For tests: clears the memoized result.
void resetForTesting();
}

#endif
