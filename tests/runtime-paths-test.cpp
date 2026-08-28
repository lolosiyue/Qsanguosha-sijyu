#include "asset-manifest.h"
#include "runtime-paths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace
{
bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    qCritical().noquote() << message;
    return false;
}

// Windows hands back the current directory in whatever form the OS stored it
// (drive-letter case, 8.3 short components under %TEMP%), so comparing the
// strings that went in against the strings that came out is not portable.
// Compare what the filesystem says the paths are instead.
QString normalized(const QString &path)
{
    if (path.isEmpty())
        return path;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
                               : canonical;
}

bool samePath(const QString &left, const QString &right)
{
    return !left.isEmpty() && normalized(left) == normalized(right);
}

bool writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&file) << content;
    return true;
}

// A directory only counts as an asset root when the two Lua files the engine
// bootstrap loads are there; anything less and the engine would exit(1) from
// its own constructor, long after the resolver claimed success.
bool makeAssetRoot(const QString &path)
{
    return writeFile(path + QStringLiteral("/lua/config.lua"), QStringLiteral("config = {}\n"))
        && writeFile(path + QStringLiteral("/lua/sanguosha.lua"), QStringLiteral("-- rules\n"));
}

QStringList argv(const QStringList &extra = QStringList())
{
    return QStringList{QStringLiteral("QSanguosha")} + extra;
}

class CurrentDirectoryGuard
{
public:
    CurrentDirectoryGuard() : m_original(QDir::currentPath()) {}
    ~CurrentDirectoryGuard() { QDir::setCurrent(m_original); }

private:
    QString m_original;
};

bool commandLineOverrideWins()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString chosen = directory.filePath(QStringLiteral("chosen"));
    const QString ignored = directory.filePath(QStringLiteral("ignored"));
    if (!makeAssetRoot(chosen) || !makeAssetRoot(ignored))
        return expect(false, "unable to build the fixture asset roots");
    QDir::setCurrent(ignored);

    QString error;
    const bool resolved =
        QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), chosen}), &error);
    return expect(resolved, "an explicit --asset-root was rejected")
        && expect(samePath(QSanRuntimePaths::assetRoot(), chosen),
                  "--asset-root did not win over the working directory")
        && expect(QSanRuntimePaths::resolution().assetRootSource
                      == QSanRuntimePaths::AssetRootSource::CommandLine,
                  "the resolution source was not reported as the command line")
        // Resolving must also make the legacy relative paths ("lua/config.lua",
        // "image/...") point at the resolved root rather than at whatever
        // directory the player happened to start the game from.
        && expect(samePath(QDir::currentPath(), QSanRuntimePaths::assetRoot()),
                  "resolve() did not move the working directory to the asset root");
}

// Silently falling back would turn "I passed --asset-root" into a bug nobody
// can find, so an explicit override that is not an asset root must be fatal
// even when a perfectly good root exists elsewhere.
bool explicitOverrideNeverFallsBack()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString usable = directory.filePath(QStringLiteral("usable"));
    const QString empty = directory.filePath(QStringLiteral("empty"));
    if (!makeAssetRoot(usable) || !QDir().mkpath(empty))
        return expect(false, "unable to build the fixture directories");
    QDir::setCurrent(usable);

    QString error;
    const bool resolved =
        QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), empty}), &error);
    return expect(!resolved, "a --asset-root without lua/config.lua was accepted")
        && expect(!error.isEmpty(), "the failure carried no explanation")
        && expect(samePath(QDir::currentPath(), usable),
                  "a failed resolution must not move the working directory");
}

bool missingOverrideDirectoryIsReported()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    QString error;
    const bool resolved = QSanRuntimePaths::resolve(
        argv({QStringLiteral("--asset-root"), directory.filePath(QStringLiteral("nope"))}),
        &error);
    return expect(!resolved, "a non-existent --asset-root was accepted")
        && expect(error.contains(QStringLiteral("does not exist")),
                  "the error did not say the directory is missing");
}

// An installed or portable layout must beat the working directory: a packaged
// binary started from inside some old source tree has to use its own data.
bool installedLayoutBeatsWorkingDirectory()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString elsewhere = directory.filePath(QStringLiteral("elsewhere"));
    if (!makeAssetRoot(elsewhere))
        return expect(false, "unable to build the fixture asset root");
    QDir::setCurrent(elsewhere);

    // The test binary is not in a bin/ directory, so this exercises the
    // priority list rather than a real install; the working directory is the
    // only candidate that can match here.
    QString error;
    const bool resolved = QSanRuntimePaths::resolve(argv(), &error);
    if (!resolved)
        return expect(false, qPrintable(error));
    const QSanRuntimePaths::Resolution &resolution = QSanRuntimePaths::resolution();
    const auto candidateIndex = [&resolution](const QString &source) {
        for (int index = 0; index < resolution.candidates.size(); ++index) {
            if (resolution.candidates.at(index).startsWith(source + QLatin1Char('=')))
                return index;
        }
        return -1;
    };
    const int workingIndex = candidateIndex(QStringLiteral("working-directory"));
    const int installedIndex = candidateIndex(QStringLiteral("installed-prefix"));
    return expect(samePath(resolution.assetRoot, elsewhere),
                  "the working directory was not used as the last-resort root")
        && expect(installedIndex >= 0, "the installed-prefix candidate was never tried")
        && expect(installedIndex < workingIndex,
                  "the installed layout must be tried before the working directory");
}

// A development tree keeps writing beside the game the way it always has; only
// a packaged root redirects to the user's own directory, because /usr/share and
// an AppImage's squashfs are read-only.
bool userDataFollowsWhetherTheRootIsPackaged()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString development = directory.filePath(QStringLiteral("dev"));
    if (!makeAssetRoot(development))
        return expect(false, "unable to build the fixture asset root");
    QDir::setCurrent(development);
    if (!QSanRuntimePaths::resolve(argv()))
        return expect(false, "the development tree did not resolve");
    const bool developmentWritesInPlace =
        expect(samePath(QSanRuntimePaths::userDataRoot(), QSanRuntimePaths::assetRoot()),
               "a development tree must keep writing beside the game");

    QSanRuntimePaths::resetForTesting();
    const QString packaged = directory.filePath(QStringLiteral("packaged"));
    if (!makeAssetRoot(packaged))
        return expect(false, "unable to build the packaged fixture root");
    if (!QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), packaged})))
        return expect(false, "the packaged root did not resolve");
    return developmentWritesInPlace
        && expect(!samePath(QSanRuntimePaths::userDataRoot(), QSanRuntimePaths::assetRoot()),
                  "a packaged root must not be used as the user data directory")
        && expect(!normalized(QSanRuntimePaths::recordDir())
                       .startsWith(normalized(QSanRuntimePaths::assetRoot())),
                  "replays must not be written into a packaged asset root")
        && expect(QDir(QSanRuntimePaths::recordDir()).exists(),
                  "the record directory was not created");
}

bool userDataRootIsOverridable()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    const QString userData = directory.filePath(QStringLiteral("userdata"));
    if (!makeAssetRoot(root))
        return expect(false, "unable to build the fixture asset root");
    qputenv("QSAN_USER_DATA_ROOT", userData.toUtf8());
    const bool resolved =
        QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), root}));
    const QString observed = QSanRuntimePaths::userDataRoot();
    qunsetenv("QSAN_USER_DATA_ROOT");
    return expect(resolved, "the fixture root did not resolve")
        && expect(samePath(observed, userData), "QSAN_USER_DATA_ROOT was ignored");
}

// A read that has no user copy has to fall back to the one shipped in the
// package, or custom scenarios would vanish the first time the game is
// installed rather than run from a source tree.
bool readablePathPrefersUserDataThenAssets()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    const QString userData = directory.filePath(QStringLiteral("userdata"));
    if (!makeAssetRoot(root)
        || !writeFile(root + QStringLiteral("/etc/customScenes/1.txt"), QStringLiteral("bundled")))
        return expect(false, "unable to build the fixture asset root");
    qputenv("QSAN_USER_DATA_ROOT", userData.toUtf8());
    const bool resolved =
        QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), root}));
    const QString relative = QStringLiteral("etc/customScenes/1.txt");
    const QString bundled = QSanRuntimePaths::readablePath(relative);
    const bool fellBackToBundle =
        expect(samePath(bundled, QDir(root).filePath(relative)),
               "a file that only exists in the package was not found there");

    if (!writeFile(QDir(userData).filePath(relative), QStringLiteral("mine")))
        return expect(false, "unable to write the user copy");
    const QString mine = QSanRuntimePaths::readablePath(relative);
    qunsetenv("QSAN_USER_DATA_ROOT");
    return expect(resolved, "the fixture root did not resolve")
        && fellBackToBundle
        && expect(samePath(mine, QDir(userData).filePath(relative)),
                  "the user's own copy did not win over the packaged one");
}

// A missing manifest is a fact about the tree, not an error: development trees
// have none, and reporting a failure there would train everyone to ignore it.
bool assetManifestReportsWhatIsMissing()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    if (!makeAssetRoot(root))
        return expect(false, "unable to build the fixture asset root");
    if (!QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), root})))
        return expect(false, "the fixture root did not resolve");

    const QSanAssetManifest::Report absent = QSanAssetManifest::inspect();
    const bool absentIsNotAnError =
        expect(!absent.manifestPresent, "a manifest was found where none was written")
        && expect(absent.error.isEmpty(), "a missing manifest was reported as an error")
        && expect(!QSanAssetManifest::diagnostics(absent).isEmpty(),
                  "a missing manifest produced no diagnostics");

    if (!writeFile(root + QStringLiteral("/assets-manifest.json"),
                   QStringLiteral("{\"schema_version\":1,\"game_version\":\"test\","
                                  "\"asset_pack_version\":\"core-1\","
                                  "\"required_paths\":[\"lua/config.lua\",\"lua/gone.lua\"],"
                                  "\"optional_paths\":[\"image\"]}\n")))
        return expect(false, "unable to write the fixture manifest");

    const QSanAssetManifest::Report report = QSanAssetManifest::inspect();
    return absentIsNotAnError
        && expect(report.manifestPresent, "the manifest was not picked up")
        && expect(report.assetPackVersion == QStringLiteral("core-1"),
                  "the asset pack version was not read")
        && expect(report.missingRequired() == QStringList{QStringLiteral("lua/gone.lua")},
                  "the missing required path was not reported")
        && expect(report.missingOptional() == QStringList{QStringLiteral("image")},
                  "the missing optional path was not reported")
        && expect(!report.complete(), "a package missing a required path claimed to be complete");
}

bool unsupportedManifestSchemaIsRejected()
{
    CurrentDirectoryGuard guard;
    QSanRuntimePaths::resetForTesting();
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    if (!makeAssetRoot(root)
        || !writeFile(root + QStringLiteral("/assets-manifest.json"),
                      QStringLiteral("{\"schema_version\":99}\n")))
        return expect(false, "unable to build the fixture asset root");
    if (!QSanRuntimePaths::resolve(argv({QStringLiteral("--asset-root"), root})))
        return expect(false, "the fixture root did not resolve");
    const QSanAssetManifest::Report report = QSanAssetManifest::inspect();
    return expect(!report.error.isEmpty(), "an unknown manifest schema was accepted")
        && expect(!report.complete(), "an unreadable manifest claimed completeness");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!commandLineOverrideWins())
        return 1;
    if (!explicitOverrideNeverFallsBack())
        return 2;
    if (!missingOverrideDirectoryIsReported())
        return 3;
    if (!installedLayoutBeatsWorkingDirectory())
        return 4;
    if (!userDataFollowsWhetherTheRootIsPackaged())
        return 5;
    if (!userDataRootIsOverridable())
        return 6;
    if (!readablePathPrefersUserDataThenAssets())
        return 7;
    if (!assetManifestReportsWhatIsMissing())
        return 8;
    if (!unsupportedManifestSchemaIsRejected())
        return 9;
    return 0;
}
