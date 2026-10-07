#include "package-store.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <atomic>

#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x << error; return 1; } } while (false)
static QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString error;
    CHECK(argc == 2);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString runtime = temp.filePath(QStringLiteral("runtime")), user = temp.filePath(QStringLiteral("user"));
    CHECK(QDir().mkpath(runtime));
    PackageStore store(runtime, user);
    auto stage = [&](const QString &name, const QString &id, const QString &version, std::atomic_bool *cancel = nullptr) {
        QFile f(QDir(QString::fromLocal8Bit(argv[1])).filePath(name));
        return f.open(QIODevice::ReadOnly) && store.stageArchive(f, &error, cancel, id, version);
    };
    const QString active = QDir(runtime).filePath(QStringLiteral("packages/material-test/image/test.png"));
    CHECK(stage(QStringLiteral("v1-wrapped.zip"), QStringLiteral("material-test"), QStringLiteral("1.0.0")));
    CHECK(!QFile::exists(active));
    CHECK(store.hasPending());
    CHECK(!stage(QStringLiteral("v2.zip"), QStringLiteral("material-test"), QStringLiteral("2.0.0")));
    CHECK(read(QDir(user).filePath(QStringLiteral("package-store/pending/material-test/image/test.png"))) == QByteArrayLiteral("old asset"));
    CHECK(store.prepareStartup(&error));
    CHECK(read(active) == QByteArrayLiteral("old asset"));
    CHECK(store.markBootSuccessful(&error));

    // A catalog/archive mismatch must not change active content or the pending set.
    CHECK(!stage(QStringLiteral("v2.zip"), QStringLiteral("other-id"), QStringLiteral("2.0.0")));
    CHECK(!stage(QStringLiteral("v2.zip"), QStringLiteral("material-test"), QStringLiteral("9.0.0")));
    CHECK(!store.hasPending());
    CHECK(read(active) == QByteArrayLiteral("old asset"));
    for (const QString &archive : {QStringLiteral("bad-hash.zip"), QStringLiteral("traversal.zip"),
                                  QStringLiteral("symlink.zip"), QStringLiteral("compression-bomb.zip")}) {
        CHECK(!stage(archive, QStringLiteral("material-test"), QStringLiteral("2.0.0")));
        CHECK(!store.hasPending());
        CHECK(read(active) == QByteArrayLiteral("old asset"));
    }
    CHECK(!QFile::exists(temp.filePath(QStringLiteral("escaped.txt"))));
    const QString largeRoot = temp.filePath(QStringLiteral("oversized-manifest"));
    CHECK(QDir().mkpath(largeRoot));
    QFile largeManifest(QDir(largeRoot).filePath(QStringLiteral("manifest.json")));
    CHECK(largeManifest.open(QIODevice::WriteOnly));
    CHECK(largeManifest.write(QByteArray(16 * 1024 * 1024 + 1, ' ')) == 16 * 1024 * 1024 + 1);
    largeManifest.close();
    CHECK(QSanPackages::parsePackage(largeRoot, &error).id.isEmpty());
    CHECK(error.contains(QStringLiteral("size limit")));
    std::atomic_bool cancel{true};
    CHECK(!stage(QStringLiteral("v2.zip"), QStringLiteral("material-test"), QStringLiteral("2.0.0"), &cancel));
    CHECK(!store.hasPending());
    CHECK(stage(QStringLiteral("v2.zip"), QStringLiteral("material-test"), QStringLiteral("2.0.0")));
    CHECK(read(active) == QByteArrayLiteral("old asset"));
    // A limits/configuration failure at restart keeps the old working tree and
    // pending journal for retry instead of discarding the selected update.
    const QString limitsPath = QDir(user).filePath(QStringLiteral("package-store/limits.json"));
    QFile limitsFile(limitsPath);
    CHECK(limitsFile.open(QIODevice::WriteOnly));
    CHECK(limitsFile.write("{invalid") == 8); limitsFile.close();
    PackageStore blocked(runtime, user);
    CHECK(blocked.prepareStartup(&error));
    CHECK(blocked.hasPending());
    CHECK(read(active) == QByteArrayLiteral("old asset"));
    CHECK(QFile::remove(limitsPath));
    CHECK(store.prepareStartup(&error));
    CHECK(read(active) == QByteArrayLiteral("new asset"));
    CHECK(store.beginBootAttempt(&error));
    CHECK(store.hasUnsuccessfulBootAttempt());
    CHECK(store.recoverPrevious(&error));
    CHECK(store.hasPending());
    CHECK(read(active) == QByteArrayLiteral("new asset"));
    CHECK(store.prepareStartup(&error));
    CHECK(read(active) == QByteArrayLiteral("old asset"));
    CHECK(store.markBootSuccessful(&error));
    qInfo() << "Restart staging, identity/hash/ZIP rejection, cancellation and boot rollback passed";
    return 0;
}
