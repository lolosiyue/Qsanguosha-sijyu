#include "package-store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>
#include <limits>

namespace {
constexpr quint64 MiB = 1024ULL * 1024;
constexpr quint64 GiB = 1024ULL * MiB;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        qCritical().noquote() << "FAIL line" << __LINE__ << #condition << error; \
        return false; \
    } \
} while (false)

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool makeTinyPackage(const QString &root)
{
    const QByteArray payload("small package payload");
    const QString relative = QStringLiteral("image/test.png");
    if (!writeFile(QDir(root).filePath(relative), payload)) return false;

    const QJsonObject file{{QStringLiteral("path"), relative},
                           {QStringLiteral("role"), QStringLiteral("data")},
                           {QStringLiteral("size"), payload.size()},
                           {QStringLiteral("sha256"), QString::fromLatin1(
                                QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())}};
    const QJsonObject manifest{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("engine_api"), 1},
                               {QStringLiteral("id"), QStringLiteral("material-test")},
                               {QStringLiteral("version"), QStringLiteral("1.0.0")},
                               {QStringLiteral("dependencies"), QJsonArray{}},
                               {QStringLiteral("extensions"), QJsonArray{}},
                               {QStringLiteral("assets"), QJsonObject{{QStringLiteral("image/test.png"), relative}}},
                               {QStringLiteral("files"), QJsonArray{file}}};
    return writeFile(QDir(root).filePath(QStringLiteral("manifest.json")),
                     QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

bool writeLimits(const QString &userRoot, const QByteArray &json)
{
    return writeFile(QDir(userRoot).filePath(QStringLiteral("package-store/limits.json")), json);
}

QByteArray limitsJson(qint64 maximumMiB, qint64 reserveMiB, int schema = 1)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), schema},
                                     {QStringLiteral("max_installed_mib"), maximumMiB},
                                     {QStringLiteral("reserve_free_mib"), reserveMiB}})
        .toJson(QJsonDocument::Compact);
}

bool testQuotaBounds(QString &error)
{
    PackageStore::Limits limits;
    CHECK(PackageStore::fitsQuota({4 * GiB, 90000}, limits, &error));
    CHECK(PackageStore::fitsQuota({limits.maxInstalledBytes, limits.maxInstalledEntries}, limits, &error));
    CHECK(!PackageStore::fitsQuota({limits.maxInstalledBytes + 1, 1}, limits, &error));
    CHECK(!error.isEmpty());
    error.clear();
    CHECK(!PackageStore::fitsQuota({1, limits.maxInstalledEntries + 1}, limits, &error));
    CHECK(!error.isEmpty());
    return true;
}

bool testPlanAccounting(QString &error)
{
    PackageStore::SpacePlan plan;
    const PackageStore::TreeSize active{3 * GiB, 10};
    const PackageStore::TreeSize installs{1 * GiB, 20};
    const PackageStore::TreeSize backup{512 * MiB, 5};
    const PackageStore::TreeSize incoming{64 * MiB, 4};
    constexpr quint64 allocationUnit = 4096;
    const quint64 effective = active.bytes + installs.bytes + 30 * allocationUnit;
    const quint64 incomingAllocated = incoming.bytes + incoming.entries * allocationUnit;
    const quint64 backupAllocated = backup.bytes + backup.entries * allocationUnit;
    CHECK(PackageStore::planSpace(active, installs, backup, incoming, 2, allocationUnit, &plan, &error));
    CHECK(plan.stageUserBytes == effective + 2 * incomingAllocated);
    CHECK(plan.restartUserBytes == backupAllocated + incomingAllocated);
    CHECK(plan.restartRuntimeBytes == effective);

    PackageStore::SpacePlan overflowPlan;
    CHECK(!PackageStore::planSpace({std::numeric_limits<quint64>::max(), 1}, {1, 0}, {}, {},
                                   0, allocationUnit, &overflowPlan, &error));
    CHECK(!error.isEmpty());
    return true;
}

bool testAvailableSpaceBoundaries(QString &error)
{
    PackageStore::SpacePlan plan{100, 50, 70};
    PackageStore::Limits limits;
    limits.reserveBytes = 10;
    // One physical volume needs the larger of staging and restart peaks,
    // including the transient old+next runtime trees at restart.
    CHECK(PackageStore::fitsAvailableSpace(plan, 130, 130, true, limits, &error));
    CHECK(!PackageStore::fitsAvailableSpace(plan, 129, 200, true, limits, &error));
    CHECK(!PackageStore::fitsAvailableSpace(plan, 200, 129, true, limits, &error));

    // Separate filesystems charge the user-data and runtime phases separately.
    CHECK(PackageStore::fitsAvailableSpace(plan, 110, 80, false, limits, &error));
    CHECK(!PackageStore::fitsAvailableSpace(plan, 109, 100, false, limits, &error));
    CHECK(!PackageStore::fitsAvailableSpace(plan, 200, 79, false, limits, &error));
    return true;
}

bool testLimitsConfig(QString &error)
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString runtime = temp.filePath(QStringLiteral("runtime"));
    const QString user = temp.filePath(QStringLiteral("user"));
    const QString package = temp.filePath(QStringLiteral("tiny-package"));
    CHECK(QDir().mkpath(runtime));
    CHECK(makeTinyPackage(package));

    // Missing configuration keeps the documented safe defaults.
    PackageStore defaults(runtime, user);
    CHECK(defaults.limits().maxInstalledBytes == 8 * GiB);
    CHECK(defaults.limits().reserveBytes == 512 * MiB);

    // Both ends of each accepted configuration range are valid.
    CHECK(writeLimits(user, limitsJson(512, 512)));
    PackageStore minimum(runtime, user);
    CHECK(minimum.limits().maxInstalledBytes == 512 * MiB);
    CHECK(minimum.limits().reserveBytes == 512 * MiB);
    CHECK(writeLimits(user, limitsJson(32768, 4096)));
    PackageStore maximum(runtime, user);
    CHECK(maximum.limits().maxInstalledBytes == 32768 * MiB);
    CHECK(maximum.limits().reserveBytes == 4096 * MiB);

    const QList<QByteArray> invalidConfigs{
        QByteArrayLiteral("{"),
        limitsJson(8192, 512, 2),
        limitsJson(511, 512),
        limitsJson(32769, 512),
        limitsJson(8192, 511),
        limitsJson(8192, 4097)
    };
    for (const QByteArray &config : invalidConfigs) {
        CHECK(writeLimits(user, config));
        PackageStore invalid(runtime, user);
        CHECK(!invalid.stageDirectory(package, &error));
        CHECK(!error.isEmpty());
        CHECK(!invalid.hasPending());
        error.clear();
    }
    // Sparse payloads exercise real metadata rejection without allocating or
    // hashing a multi-GB test library. Preflight must reject before .incoming.
    CHECK(writeLimits(user, limitsJson(512, 512)));
    const QString active = QDir(runtime).filePath(QStringLiteral("packages/large"));
    CHECK(QDir().mkpath(active));
    for (int i = 0; i < 33; ++i) {
        QFile sparse(QDir(active).filePath(QString::number(i) + QStringLiteral(".bin")));
        CHECK(sparse.open(QIODevice::WriteOnly));
        CHECK(sparse.resize(16 * MiB));
    }
    PackageStore overQuota(runtime, user);
    CHECK(!overQuota.stageDirectory(package, &error));
    CHECK(error.contains(QStringLiteral("quota")));
    CHECK(!overQuota.hasPending());
    CHECK(!QFileInfo::exists(QDir(user).filePath(QStringLiteral("package-store/pending/material-test.incoming"))));
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!testQuotaBounds(error) || !testPlanAccounting(error)
        || !testAvailableSpaceBoundaries(error) || !testLimitsConfig(error))
        return 1;
    qInfo() << checks << "storage quota and preflight assertions passed";
    return 0;
}
