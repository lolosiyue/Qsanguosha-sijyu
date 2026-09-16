#include "../src/core/package-store.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QTemporaryDir>

#include <zlib.h>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool writePackage(const QString &root, const QString &id, const QString &version,
                  const QStringList &dependencies = {})
{
    const QByteArray script("-- " + version.toUtf8() + "\n");
    if (!writeFile(QDir(root).filePath(QStringLiteral("lua/%1.lua").arg(id)), script)) return false;
    QJsonObject inventory{{QStringLiteral("path"), QStringLiteral("lua/%1.lua").arg(id)},
                          {QStringLiteral("role"), QStringLiteral("rules")},
                          {QStringLiteral("size"), script.size()},
                          {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex())}};
    QJsonObject extension{{QStringLiteral("name"), id},
                          {QStringLiteral("script"), QStringLiteral("lua/%1.lua").arg(id)},
                          {QStringLiteral("dependencies"), QJsonArray()}, {QStringLiteral("libs"), QJsonArray()},
                          {QStringLiteral("lang"), QJsonArray()}, {QStringLiteral("ai"), QJsonArray()}};
    QJsonObject manifest{{QStringLiteral("schema_version"), 1}, {QStringLiteral("engine_api"), 1},
                         {QStringLiteral("id"), id}, {QStringLiteral("version"), version},
                         {QStringLiteral("dependencies"), QJsonArray::fromStringList(dependencies)},
                         {QStringLiteral("extensions"), QJsonArray{extension}},
                         {QStringLiteral("assets"), QJsonObject()},
                         {QStringLiteral("files"), QJsonArray{inventory}}};
    return writeFile(QDir(root).filePath(QStringLiteral("manifest.json")), QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

QString versionOf(const QSanPackages::Catalog &catalog, const QString &id)
{
    for (const auto &package : catalog.packages) if (package.id == id) return package.version;
    return QString();
}

QByteArray makeStoredZip(const QMap<QString, QByteArray> &files)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    struct Record { QByteArray name; quint32 crc; quint32 size; quint32 offset; };
    QList<Record> records;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const QByteArray content = it.value();
        const quint32 crc = quint32(::crc32(0, reinterpret_cast<const Bytef *>(content.constData()), uInt(content.size())));
        const quint32 offset = quint32(bytes.size());
        out << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0)
            << quint16(0) << quint16(0) << crc << quint32(content.size()) << quint32(content.size())
            << quint16(name.size()) << quint16(0);
        out.writeRawData(name.constData(), name.size());
        out.writeRawData(content.constData(), content.size());
        records.append({name, crc, quint32(content.size()), offset});
    }
    const quint32 centralOffset = quint32(bytes.size());
    for (const Record &record : records) {
        out << quint32(0x02014b50) << quint16(0x0314) << quint16(20) << quint16(0) << quint16(0)
            << quint16(0) << quint16(0) << record.crc << record.size << record.size
            << quint16(record.name.size()) << quint16(0) << quint16(0) << quint16(0)
            << quint16(0) << quint32(0) << record.offset;
        out.writeRawData(record.name.constData(), record.name.size());
    }
    const quint32 centralSize = quint32(bytes.size()) - centralOffset;
    const quint16 count = quint16(records.size());
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << count << count
        << centralSize << centralOffset << quint16(0);
    return bytes;
}

QMap<QString, QByteArray> packageArchiveFiles(const QString &root, const QString &prefix = {})
{
    QMap<QString, QByteArray> files;
    const QString id = QFileInfo(root).fileName();
    for (const QString &relative : {QStringLiteral("manifest.json"), QStringLiteral("lua/%1.lua").arg(id)}) {
        QFile file(QDir(root).filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) return {};
        files.insert(prefix + relative, file.readAll());
    }
    return files;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    PackageStore noOp(QDir(temp.path()).filePath(QStringLiteral("untouched-runtime")),
                      QDir(temp.path()).filePath(QStringLiteral("untouched-user")));
    QString error;
    if (!noOp.prepareStartup(&error)
        || QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("untouched-runtime")))
        || QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("untouched-user")))) return 18;
    const QString runtime = QDir(temp.path()).filePath(QStringLiteral("runtime"));
    const QString data = QDir(temp.path()).filePath(QStringLiteral("user"));
    const QString initial = QDir(temp.path()).filePath(QStringLiteral("initial/demo"));
    const QString updated = QDir(temp.path()).filePath(QStringLiteral("updated/demo"));
    const QString dependent = QDir(temp.path()).filePath(QStringLiteral("dependent/client"));
    if (!writePackage(QDir(runtime).filePath(QStringLiteral("packages/demo")), QStringLiteral("demo"), QStringLiteral("1.0"))
        || !writePackage(initial, QStringLiteral("demo"), QStringLiteral("1.0"))
        || !writePackage(updated, QStringLiteral("demo"), QStringLiteral("2.0"))
        || !writePackage(dependent, QStringLiteral("client"), QStringLiteral("1.0"), {QStringLiteral("demo")})
        || !writeFile(QDir(data).filePath(QStringLiteral("packages/keep/user.txt")), "preserve")) return 2;

    PackageStore store(runtime, data);
    if (!store.stageDirectory(updated, &error)) return 3;
    if (versionOf(store.packages(), QStringLiteral("demo")) != QStringLiteral("1.0") || !store.hasPending()) return 4;
    if (!store.prepareStartup(&error)) return 5;
    if (versionOf(store.packages(), QStringLiteral("demo")) != QStringLiteral("2.0")) return 6;
    if (!QFileInfo::exists(QDir(data).filePath(QStringLiteral("packages/keep/user.txt")))) return 7;

    if (!store.stageDirectory(dependent, &error)) return 8;
    if (!store.prepareStartup(&error)) return 9;
    if (store.remove(QStringLiteral("demo"), &error) || error.isEmpty()) return 10;
    error.clear();

    if (!store.rollback(QStringLiteral("demo"), &error) || !store.prepareStartup(&error)) return 11;
    if (versionOf(store.packages(), QStringLiteral("demo")) != QStringLiteral("1.0")) return 12;

    const QString unsafe = QDir(temp.path()).filePath(QStringLiteral("unsafe/bad"));
    if (!writePackage(unsafe, QStringLiteral("bad"), QStringLiteral("1.0"))) return 13;
    QFile manifest(QDir(unsafe).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return 14;
    QJsonObject bad = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    QJsonArray files = bad.value(QStringLiteral("files")).toArray();
    QJsonObject record = files.first().toObject();
    record.insert(QStringLiteral("path"), QStringLiteral("../escape.lua"));
    files[0] = record;
    bad.insert(QStringLiteral("files"), files);
    if (!writeFile(QDir(unsafe).filePath(QStringLiteral("manifest.json")), QJsonDocument(bad).toJson(QJsonDocument::Compact))) return 15;
    if (store.stageDirectory(unsafe, &error) || error.isEmpty()) return 16;

    QBuffer invalidZip;
    invalidZip.setData("not a zip");
    invalidZip.open(QIODevice::ReadOnly);
    error.clear();
    if (store.stageArchive(invalidZip, &error) || error.isEmpty()) return 17;

    const QString zipRuntime = QDir(temp.path()).filePath(QStringLiteral("zip-runtime"));
    const QString zipData = QDir(temp.path()).filePath(QStringLiteral("zip-user"));
    const QString zipSource = QDir(temp.path()).filePath(QStringLiteral("zip-source/demo"));
    if (!writePackage(zipSource, QStringLiteral("demo"), QStringLiteral("3.0"))) return 24;
    PackageStore zipStore(zipRuntime, zipData);
    const QByteArray flatZipBytes = makeStoredZip(packageArchiveFiles(zipSource));
    QBuffer flatZip;
    flatZip.setData(flatZipBytes);
    flatZip.open(QIODevice::ReadOnly);
    error.clear();
    if (!zipStore.stageArchive(flatZip, &error) || !zipStore.prepareStartup(&error)
        || versionOf(zipStore.packages(), QStringLiteral("demo")) != QStringLiteral("3.0")) return 25;

    const QString nestedSource = QDir(temp.path()).filePath(QStringLiteral("nested-source/nested"));
    if (!writePackage(nestedSource, QStringLiteral("nested"), QStringLiteral("1.0"))) return 26;
    const QByteArray nestedZipBytes = makeStoredZip(packageArchiveFiles(nestedSource, QStringLiteral("nested/")));
    QBuffer nestedZip;
    nestedZip.setData(nestedZipBytes);
    nestedZip.open(QIODevice::ReadOnly);
    error.clear();
    if (!zipStore.stageArchive(nestedZip, &error) || !zipStore.prepareStartup(&error)
        || versionOf(zipStore.packages(), QStringLiteral("nested")) != QStringLiteral("1.0")) return 27;

    const QString interruptedRuntime = QDir(temp.path()).filePath(QStringLiteral("interrupted-runtime"));
    const QString interruptedData = QDir(temp.path()).filePath(QStringLiteral("interrupted-user"));
    if (!writePackage(QDir(interruptedRuntime).filePath(QStringLiteral("packages/demo")), QStringLiteral("demo"), QStringLiteral("1.0"))) return 19;
    PackageStore interrupted(interruptedRuntime, interruptedData);
    error.clear();
    if (!interrupted.stageDirectory(updated, &error) || !interrupted.stageDirectory(dependent, &error)) return 20;
    const QString txn = QDir(interruptedRuntime).filePath(QStringLiteral(".package-store-transaction"));
    const QString stagedNext = QDir(txn).filePath(QStringLiteral("next/packages/demo"));
    const QString stagedDependent = QDir(txn).filePath(QStringLiteral("next/packages/client"));
    if (!writePackage(stagedNext, QStringLiteral("demo"), QStringLiteral("2.0"))
        || !writePackage(stagedDependent, QStringLiteral("client"), QStringLiteral("1.0"), {QStringLiteral("demo")})) return 21;
    if (!QDir().mkpath(QDir(txn).filePath(QStringLiteral("old")))
        || !QDir().rename(QDir(interruptedRuntime).filePath(QStringLiteral("packages")), QDir(txn).filePath(QStringLiteral("old/packages")))) return 22;
    if (!interrupted.prepareStartup(&error) || versionOf(interrupted.packages(), QStringLiteral("demo")) != QStringLiteral("2.0")
        || versionOf(interrupted.packages(), QStringLiteral("client")) != QStringLiteral("1.0")) return 23;

    const QString activatedRuntime = QDir(temp.path()).filePath(QStringLiteral("activated-runtime"));
    const QString activatedData = QDir(temp.path()).filePath(QStringLiteral("activated-user"));
    const QString storeRoot = QDir(activatedData).filePath(QStringLiteral("package-store"));
    if (!writePackage(QDir(activatedRuntime).filePath(QStringLiteral("packages/demo")), QStringLiteral("demo"), QStringLiteral("2.0"))
        || !writePackage(QDir(storeRoot).filePath(QStringLiteral("pending/demo")), QStringLiteral("demo"), QStringLiteral("2.0"))
        || !writePackage(QDir(storeRoot).filePath(QStringLiteral("previous/demo")), QStringLiteral("demo"), QStringLiteral("1.0"))) return 28;
    const QJsonObject activatedJournal{{QStringLiteral("changes"), QJsonObject{{QStringLiteral("demo"), QStringLiteral("install")}}},
                                       {QStringLiteral("phase"), QStringLiteral("activated")}};
    if (!writeFile(QDir(storeRoot).filePath(QStringLiteral("journal.json")),
                   QJsonDocument(activatedJournal).toJson(QJsonDocument::Compact))) return 29;
    PackageStore activated(activatedRuntime, activatedData);
    error.clear();
    QString previousError;
    const QSanPackages::Package previous = QSanPackages::parsePackage(QDir(storeRoot).filePath(QStringLiteral("previous/demo")), &previousError, true);
    if (!previousError.isEmpty() || previous.version != QStringLiteral("1.0")
        || !activated.prepareStartup(&error)
        || versionOf(activated.packages(), QStringLiteral("demo")) != QStringLiteral("2.0")
        || !activated.pendingIds().isEmpty()
        || !QFileInfo::exists(QDir(storeRoot).filePath(QStringLiteral("last-applied.json")))) return 30;
    const auto retainedPrevious = QSanPackages::parsePackage(QDir(storeRoot).filePath(QStringLiteral("previous/demo")), &previousError, true);
    if (!previousError.isEmpty() || retainedPrevious.version != QStringLiteral("1.0")) return 31;

    const QString invalidRuntime = QDir(temp.path()).filePath(QStringLiteral("invalid-runtime"));
    const QString invalidData = QDir(temp.path()).filePath(QStringLiteral("invalid-user"));
    if (!writePackage(QDir(invalidRuntime).filePath(QStringLiteral("packages/demo")), QStringLiteral("demo"), QStringLiteral("1.0"))) return 32;
    PackageStore invalidPending(invalidRuntime, invalidData);
    error.clear();
    if (!invalidPending.stageDirectory(updated, &error)) return 33;
    const QString stagedManifestPath = QDir(invalidData).filePath(QStringLiteral("package-store/pending/demo/manifest.json"));
    QFile stagedManifest(stagedManifestPath);
    if (!stagedManifest.open(QIODevice::ReadOnly)) return 34;
    QJsonObject tampered = QJsonDocument::fromJson(stagedManifest.readAll()).object();
    stagedManifest.close();
    tampered.insert(QStringLiteral("dependencies"), QJsonArray{QStringLiteral("missing")});
    if (!writeFile(stagedManifestPath, QJsonDocument(tampered).toJson(QJsonDocument::Compact))) return 35;
    if (!invalidPending.prepareStartup(&error)
        || versionOf(invalidPending.packages(), QStringLiteral("demo")) != QStringLiteral("1.0")
        || !invalidPending.pendingIds().isEmpty()) return 36;
    bool reportedInvalidPending = false;
    for (const QVariant &row : invalidPending.overview())
        reportedInvalidPending |= row.toMap().value(QStringLiteral("storeError")).toBool();
    if (!reportedInvalidPending) return 37;

    const QString missingManifestRuntime = QDir(temp.path()).filePath(QStringLiteral("missing-manifest-runtime"));
    const QString missingManifestData = QDir(temp.path()).filePath(QStringLiteral("missing-manifest-user"));
    if (!writePackage(QDir(missingManifestRuntime).filePath(QStringLiteral("packages/demo")), QStringLiteral("demo"), QStringLiteral("1.0"))) return 38;
    PackageStore missingManifest(missingManifestRuntime, missingManifestData);
    error.clear();
    if (!missingManifest.stageDirectory(updated, &error)) return 39;
    const QString removedManifest = QDir(missingManifestData).filePath(QStringLiteral("package-store/pending/demo/manifest.json"));
    if (!QFile::remove(removedManifest)) return 40;
    if (!missingManifest.prepareStartup(&error)
        || versionOf(missingManifest.packages(), QStringLiteral("demo")) != QStringLiteral("1.0")
        || !missingManifest.pendingIds().isEmpty()) return 41;
    bool reportedMissingManifest = false;
    for (const QVariant &row : missingManifest.overview())
        reportedMissingManifest |= row.toMap().value(QStringLiteral("storeError")).toBool();
    if (!reportedMissingManifest) return 42;

    const QString firstInstallRuntime = QDir(temp.path()).filePath(QStringLiteral("first-install-runtime"));
    const QString firstInstallData = QDir(temp.path()).filePath(QStringLiteral("first-install-user"));
    PackageStore firstInstall(firstInstallRuntime, firstInstallData);
    error.clear();
    if (!firstInstall.stageDirectory(updated, &error)) return 43;
    const QString firstInstallTxn = QDir(firstInstallRuntime).filePath(QStringLiteral(".package-store-transaction"));
    const QString firstInstallNext = QDir(firstInstallTxn).filePath(QStringLiteral("next/packages"));
    const QString firstInstallOld = QDir(firstInstallTxn).filePath(QStringLiteral("old/packages"));
    if (!writePackage(QDir(firstInstallNext).filePath(QStringLiteral("demo")), QStringLiteral("demo"), QStringLiteral("2.0"))
        || !QDir().mkpath(firstInstallOld)
        || !QDir().rename(firstInstallNext, QDir(firstInstallRuntime).filePath(QStringLiteral("packages")))) return 44;
    const QJsonObject oldMovedJournal{{QStringLiteral("changes"), QJsonObject{{QStringLiteral("demo"), QStringLiteral("install")}}},
                                      {QStringLiteral("phase"), QStringLiteral("old_moved")}};
    const QString firstInstallJournal = QDir(firstInstallData).filePath(QStringLiteral("package-store/journal.json"));
    if (!writeFile(firstInstallJournal, QJsonDocument(oldMovedJournal).toJson(QJsonDocument::Compact))) return 45;
    if (!firstInstall.prepareStartup(&error)
        || versionOf(firstInstall.packages(), QStringLiteral("demo")) != QStringLiteral("2.0")
        || !firstInstall.beginBootAttempt(&error)
        || !firstInstall.recoverPrevious(&error)
        || !firstInstall.prepareStartup(&error)
        || !firstInstall.packages().isValid()
        || !firstInstall.packages().packages.isEmpty()) return 46;
    return 0;
}
