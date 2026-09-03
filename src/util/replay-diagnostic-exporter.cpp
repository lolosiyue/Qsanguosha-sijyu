#include "replay-diagnostic-exporter.h"

#include "game-snapshot.h"
#include "recorder.h"
#include "version.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>

#include <cstring>
#include <limits>

namespace
{
constexpr quint16 ZipVersion = 20;
constexpr quint16 ZipUtf8DataDescriptorFlags = 0x0808;
constexpr quint32 ZipLocalHeaderSignature = 0x04034b50;
constexpr quint32 ZipDataDescriptorSignature = 0x08074b50;
constexpr quint32 ZipCentralHeaderSignature = 0x02014b50;
constexpr quint32 ZipEndSignature = 0x06054b50;

struct PayloadRecord
{
    QString path;
    quint32 size = 0;
    QByteArray sha256;
};

struct CentralRecord
{
    QByteArray name;
    quint32 crc32 = 0;
    quint32 size = 0;
    quint32 localOffset = 0;
    quint16 dosTime = 0;
    quint16 dosDate = 0;
};

bool writeAll(QIODevice *device, const char *data, qint64 size)
{
    qint64 written = 0;
    while (written < size) {
        const qint64 count = device->write(data + written, size - written);
        if (count <= 0)
            return false;
        written += count;
    }
    return true;
}

bool writeBytes(QIODevice *device, const QByteArray &bytes)
{
    return writeAll(device, bytes.constData(), bytes.size());
}

bool writeLe16(QIODevice *device, quint16 value)
{
    char bytes[2] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    return writeAll(device, bytes, 2);
}

bool writeLe32(QIODevice *device, quint32 value)
{
    char bytes[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    return writeAll(device, bytes, 4);
}

quint32 updateCrc32(quint32 crc, const char *data, qsizetype size)
{
    for (qsizetype i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}

void toDosDateTime(const QDateTime &dateTime, quint16 *dosDate, quint16 *dosTime)
{
    const QDateTime local = dateTime.toLocalTime();
    const QDate date = local.date();
    const QTime time = local.time();
    const int year = qBound(1980, date.year(), 2107);
    *dosDate = static_cast<quint16>(((year - 1980) << 9)
        | (date.month() << 5) | date.day());
    *dosTime = static_cast<quint16>((time.hour() << 11)
        | (time.minute() << 5) | (time.second() / 2));
}

class StoreZipWriter
{
public:
    explicit StoreZipWriter(const QString &path)
        : m_file(path)
    {
    }

    bool open(QString *error)
    {
        if (m_file.open(QIODevice::WriteOnly))
            return true;
        if (error)
            *error = m_file.errorString();
        return false;
    }

    bool addFile(const QString &archivePath, const QString &sourcePath,
                 PayloadRecord *payload, QString *error)
    {
        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Unable to read %1: %2")
                    .arg(QFileInfo(sourcePath).fileName(), source.errorString());
            return false;
        }
        return addDevice(archivePath, &source, QFileInfo(source).lastModified(),
                         payload, error);
    }

    bool addBytes(const QString &archivePath, const QByteArray &bytes,
                  PayloadRecord *payload, QString *error)
    {
        class ByteArrayDevice final : public QIODevice
        {
        public:
            explicit ByteArrayDevice(const QByteArray &data) : m_data(data) {}
            bool isSequential() const override { return true; }
        protected:
            qint64 readData(char *data, qint64 maxSize) override
            {
                const qint64 remaining = m_data.size() - m_offset;
                const qint64 count = qMin(maxSize, remaining);
                if (count <= 0)
                    return 0;
                memcpy(data, m_data.constData() + m_offset,
                       static_cast<size_t>(count));
                m_offset += count;
                return count;
            }
            qint64 writeData(const char *, qint64) override { return -1; }
        private:
            const QByteArray &m_data;
            qint64 m_offset = 0;
        } device(bytes);
        device.open(QIODevice::ReadOnly);
        return addDevice(archivePath, &device, QDateTime::currentDateTime(),
                         payload, error);
    }

    bool finish(QString *error)
    {
        if (m_records.size() > std::numeric_limits<quint16>::max()) {
            if (error)
                *error = QStringLiteral("ZIP32 entry limit exceeded");
            return false;
        }

        const qint64 centralOffset64 = m_file.pos();
        if (centralOffset64 < 0
            || centralOffset64 > std::numeric_limits<quint32>::max()) {
            if (error)
                *error = QStringLiteral("ZIP32 archive offset limit exceeded");
            return false;
        }

        for (const CentralRecord &record : m_records) {
            if (!writeLe32(&m_file, ZipCentralHeaderSignature)
                || !writeLe16(&m_file, ZipVersion)
                || !writeLe16(&m_file, ZipVersion)
                || !writeLe16(&m_file, ZipUtf8DataDescriptorFlags)
                || !writeLe16(&m_file, 0)
                || !writeLe16(&m_file, record.dosTime)
                || !writeLe16(&m_file, record.dosDate)
                || !writeLe32(&m_file, record.crc32)
                || !writeLe32(&m_file, record.size)
                || !writeLe32(&m_file, record.size)
                || !writeLe16(&m_file, static_cast<quint16>(record.name.size()))
                || !writeLe16(&m_file, 0)
                || !writeLe16(&m_file, 0)
                || !writeLe16(&m_file, 0)
                || !writeLe16(&m_file, 0)
                || !writeLe32(&m_file, 0)
                || !writeLe32(&m_file, record.localOffset)
                || !writeBytes(&m_file, record.name)) {
                if (error)
                    *error = m_file.errorString();
                return false;
            }
        }

        const qint64 centralEnd64 = m_file.pos();
        const qint64 centralSize64 = centralEnd64 - centralOffset64;
        if (centralEnd64 < 0 || centralSize64 < 0
            || centralEnd64 > std::numeric_limits<quint32>::max()
            || centralSize64 > std::numeric_limits<quint32>::max()) {
            if (error)
                *error = QStringLiteral("ZIP32 central directory limit exceeded");
            return false;
        }

        const quint16 count = static_cast<quint16>(m_records.size());
        if (!writeLe32(&m_file, ZipEndSignature)
            || !writeLe16(&m_file, 0)
            || !writeLe16(&m_file, 0)
            || !writeLe16(&m_file, count)
            || !writeLe16(&m_file, count)
            || !writeLe32(&m_file, static_cast<quint32>(centralSize64))
            || !writeLe32(&m_file, static_cast<quint32>(centralOffset64))
            || !writeLe16(&m_file, 0)) {
            if (error)
                *error = m_file.errorString();
            return false;
        }

        if (m_file.commit())
            return true;
        if (error)
            *error = m_file.errorString();
        return false;
    }

private:
    bool addDevice(const QString &archivePath, QIODevice *source,
                   const QDateTime &modified, PayloadRecord *payload,
                   QString *error)
    {
        const QByteArray name = archivePath.toUtf8();
        const qint64 localOffset64 = m_file.pos();
        if (name.isEmpty() || name.size() > std::numeric_limits<quint16>::max()
            || localOffset64 < 0
            || localOffset64 > std::numeric_limits<quint32>::max()) {
            if (error)
                *error = QStringLiteral("ZIP32 path or offset limit exceeded");
            return false;
        }

        CentralRecord record;
        record.name = name;
        record.localOffset = static_cast<quint32>(localOffset64);
        toDosDateTime(modified.isValid() ? modified : QDateTime::currentDateTime(),
                      &record.dosDate, &record.dosTime);

        if (!writeLe32(&m_file, ZipLocalHeaderSignature)
            || !writeLe16(&m_file, ZipVersion)
            || !writeLe16(&m_file, ZipUtf8DataDescriptorFlags)
            || !writeLe16(&m_file, 0)
            || !writeLe16(&m_file, record.dosTime)
            || !writeLe16(&m_file, record.dosDate)
            || !writeLe32(&m_file, 0)
            || !writeLe32(&m_file, 0)
            || !writeLe32(&m_file, 0)
            || !writeLe16(&m_file, static_cast<quint16>(name.size()))
            || !writeLe16(&m_file, 0)
            || !writeBytes(&m_file, name)) {
            if (error)
                *error = m_file.errorString();
            return false;
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        quint32 crc = 0xffffffffU;
        quint64 size = 0;
        char buffer[64 * 1024];
        while (true) {
            const qint64 count = source->read(buffer, sizeof(buffer));
            if (count < 0) {
                if (error)
                    *error = source->errorString();
                return false;
            }
            if (count == 0)
                break;
            size += static_cast<quint64>(count);
            if (size > std::numeric_limits<quint32>::max()) {
                if (error)
                    *error = QStringLiteral("ZIP32 file size limit exceeded: %1")
                        .arg(archivePath);
                return false;
            }
            crc = updateCrc32(crc, buffer, static_cast<qsizetype>(count));
            hash.addData(buffer, count);
            if (!writeAll(&m_file, buffer, count)) {
                if (error)
                    *error = m_file.errorString();
                return false;
            }
        }

        record.crc32 = ~crc;
        record.size = static_cast<quint32>(size);
        if (!writeLe32(&m_file, ZipDataDescriptorSignature)
            || !writeLe32(&m_file, record.crc32)
            || !writeLe32(&m_file, record.size)
            || !writeLe32(&m_file, record.size)) {
            if (error)
                *error = m_file.errorString();
            return false;
        }

        m_records.append(record);
        if (payload) {
            payload->path = archivePath;
            payload->size = record.size;
            payload->sha256 = hash.result().toHex();
        }
        return true;
    }

    QSaveFile m_file;
    QList<CentralRecord> m_records;
};

QString compilerId()
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC");
#elif defined(__clang__)
    return QStringLiteral("Clang");
#elif defined(__GNUC__)
    return QStringLiteral("GCC");
#else
    return QStringLiteral("unknown");
#endif
}

QString compilerVersion()
{
#if defined(_MSC_FULL_VER)
    return QString::number(_MSC_FULL_VER);
#elif defined(__clang_version__)
    return QString::fromLatin1(__clang_version__);
#elif defined(__VERSION__)
    return QString::fromLatin1(__VERSION__);
#else
    return QStringLiteral("unknown");
#endif
}

QByteArray jsonBytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QJsonObject bundleManifest(const ReplayDiagnosticExportRequest &request,
                           const QList<PayloadRecord> &payloads,
                           const QMap<QString, QString> &omitted)
{
    QJsonArray files;
    for (const PayloadRecord &payload : payloads) {
        files.append(QJsonObject{
            {QStringLiteral("path"), payload.path},
            {QStringLiteral("size"), static_cast<qint64>(payload.size)},
            {QStringLiteral("sha256"), QString::fromLatin1(payload.sha256)}
        });
    }

    QJsonArray omittedFiles;
    for (auto it = omitted.constBegin(); it != omitted.constEnd(); ++it) {
        omittedFiles.append(QJsonObject{
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("reason"), it.value()}
        });
    }

    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("qsanguosha-bug-bundle-v1")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("createdAtUtc"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("sourceReplay"), QFileInfo(request.replayPath).fileName()},
        {QStringLiteral("files"), files},
        {QStringLiteral("omitted"), omittedFiles},
        {QStringLiteral("privacyWarnings"), QJsonArray{
            QStringLiteral("Replay snapshots may contain the original local replayPath."),
            QStringLiteral("Replay and state-now data may contain player names, chat text, room metadata, or connection metadata.")
        }}
    };
}

bool pathsReferToSameFile(const QString &leftPath, const QString &rightPath)
{
    const QFileInfo left(leftPath);
    const QFileInfo right(rightPath);
    QString leftKey = left.exists() ? left.canonicalFilePath()
                                    : left.absoluteFilePath();
    QString rightKey = right.exists() ? right.canonicalFilePath()
                                      : right.absoluteFilePath();
    leftKey = QDir::cleanPath(leftKey);
    rightKey = QDir::cleanPath(rightKey);
#ifdef Q_OS_WIN
    return leftKey.compare(rightKey, Qt::CaseInsensitive) == 0;
#else
    return leftKey == rightKey;
#endif
}

bool validateBundleSources(const ReplayDiagnosticExportRequest &request,
                           QString *error)
{
    const QFileInfo replayInfo(request.replayPath);
    if (!replayInfo.exists() || !replayInfo.isFile()
        || replayInfo.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("Replay source must be a regular .txt file");
        return false;
    }

    const QString snapshotDir = GameSnapshot::getSnapshotDir(request.replayPath);
    const QFileInfo snapshotDirInfo(snapshotDir);
    const QString expectedManifest = QDir(snapshotDir).filePath(
        QStringLiteral("manifest.json"));
    const QFileInfo manifestInfo(request.manifestPath);
    if (!snapshotDirInfo.exists() || !snapshotDirInfo.isDir()
        || !manifestInfo.exists() || !manifestInfo.isFile()
        || !pathsReferToSameFile(request.manifestPath, expectedManifest)) {
        if (error)
            *error = QStringLiteral("Replay manifest must be the sidecar manifest.json");
        return false;
    }

    static const QRegularExpression snapshotNamePattern(
        QStringLiteral("^turn_[0-9]+_turn\\.json$"));
    for (const QString &snapshotPath : request.snapshotPaths) {
        const QFileInfo snapshotInfo(snapshotPath);
        const QString resolvedSnapshotDir = QFileInfo(
            snapshotInfo.canonicalFilePath()).absolutePath();
        if (!snapshotInfo.exists() || !snapshotInfo.isFile()
            || !snapshotNamePattern.match(snapshotInfo.fileName()).hasMatch()
            || !pathsReferToSameFile(resolvedSnapshotDir, snapshotDir)) {
            if (error)
                *error = QStringLiteral("Snapshot source is outside the Replay sidecar or has an invalid filename");
            return false;
        }
    }
    return true;
}
}

QJsonObject ReplayDiagnosticExporter::createDiagnostics(const Replayer &replayer)
{
    QJsonObject compiler{
        {QStringLiteral("id"), compilerId()},
        {QStringLiteral("version"), compilerVersion()}
    };
    QJsonObject qt{
        {QStringLiteral("compileVersion"), QStringLiteral(QT_VERSION_STR)},
        {QStringLiteral("runtimeVersion"), QString::fromLatin1(qVersion())}
    };
    QJsonObject platform{
        {QStringLiteral("product"), QSysInfo::prettyProductName()},
        {QStringLiteral("kernelType"), QSysInfo::kernelType()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("buildCpuArchitecture"), QSysInfo::buildCpuArchitecture()},
        {QStringLiteral("currentCpuArchitecture"), QSysInfo::currentCpuArchitecture()}
    };

    qint64 durationMs = 0;
    if (!replayer.events().isEmpty())
        durationMs = replayer.events().constLast().elapsedMs;
    QJsonObject replay{
        {QStringLiteral("sourceFile"), QFileInfo(replayer.getPath()).fileName()},
        {QStringLiteral("formatVersion"), static_cast<int>(replayer.formatVersion())},
        {QStringLiteral("protocolVersion"), static_cast<int>(replayer.messageProtocolVersion())},
        {QStringLiteral("eventCount"), replayer.events().size()},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("takeoverSnapshotCount"), replayer.getTakeoverSnapshotPaths().size()}
    };

    return QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("createdAtUtc"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("gameVersion"), QString::fromLatin1(QSanVersion::Number)},
#if defined(QT_DEBUG)
        {QStringLiteral("buildMode"), QStringLiteral("debug")},
#else
        {QStringLiteral("buildMode"), QStringLiteral("release")},
#endif
        {QStringLiteral("compiler"), compiler},
        {QStringLiteral("qt"), qt},
        {QStringLiteral("platform"), platform},
        {QStringLiteral("replay"), replay}
    };
}

ReplayDiagnosticExportResult ReplayDiagnosticExporter::exportBundle(
    const QString &outputPath, const ReplayDiagnosticExportRequest &request)
{
    ReplayDiagnosticExportResult result;
    if (outputPath.isEmpty() || request.replayPath.isEmpty()
        || request.manifestPath.isEmpty() || request.snapshotPaths.isEmpty()) {
        result.error = QStringLiteral("Replay bundle source is incomplete");
        return result;
    }

    // This only constrains package inputs to the already verified Replay
    // sidecar. Schema and content hashes remain Replayer's responsibility.
    if (!validateBundleSources(request, &result.error))
        return result;

    QStringList sourcePaths{request.replayPath, request.manifestPath};
    sourcePaths.append(request.snapshotPaths);
    for (const QString &sourcePath : sourcePaths) {
        if (pathsReferToSameFile(outputPath, sourcePath)) {
            result.error = QStringLiteral("Bundle output must not overwrite a source file");
            return result;
        }
    }

    StoreZipWriter writer(outputPath);
    if (!writer.open(&result.error))
        return result;

    QList<PayloadRecord> payloads;
    auto addFile = [&](const QString &archivePath, const QString &sourcePath) {
        PayloadRecord payload;
        if (!writer.addFile(archivePath, sourcePath, &payload, &result.error))
            return false;
        payloads.append(payload);
        result.includedFiles.append(archivePath);
        return true;
    };
    auto addJson = [&](const QString &archivePath, const QJsonObject &object) {
        PayloadRecord payload;
        if (!writer.addBytes(archivePath, jsonBytes(object), &payload, &result.error))
            return false;
        payloads.append(payload);
        result.includedFiles.append(archivePath);
        return true;
    };

    if (!addFile(QStringLiteral("replay.txt"), request.replayPath)
        || !addFile(QStringLiteral("replay.snapshots/manifest.json"),
                    request.manifestPath))
        return result;

    QSet<QString> snapshotNames;
    for (const QString &snapshotPath : request.snapshotPaths) {
        const QString name = QFileInfo(snapshotPath).fileName();
        if (name.isEmpty() || snapshotNames.contains(name)) {
            result.error = QStringLiteral("Duplicate or invalid snapshot filename");
            return result;
        }
        snapshotNames.insert(name);
        if (!addFile(QStringLiteral("replay.snapshots/") + name, snapshotPath))
            return result;
    }

    if (request.includeStateNow) {
        if (!addJson(QStringLiteral("state-now.json"), request.stateNow))
            return result;
    } else {
        result.omittedFiles.insert(QStringLiteral("state-now.json"),
            request.stateNowOmission.isEmpty()
                ? QStringLiteral("state capture unavailable")
                : request.stateNowOmission);
    }

    if (request.includeDiagnostics) {
        if (!addJson(QStringLiteral("diagnostics.json"), request.diagnostics))
            return result;
    } else {
        result.omittedFiles.insert(QStringLiteral("diagnostics.json"),
            request.diagnosticsOmission.isEmpty()
                ? QStringLiteral("diagnostics unavailable")
                : request.diagnosticsOmission);
    }

    PayloadRecord bundlePayload;
    if (!writer.addBytes(QStringLiteral("bundle.json"),
                         jsonBytes(bundleManifest(request, payloads,
                                                  result.omittedFiles)),
                         &bundlePayload, &result.error))
        return result;
    result.includedFiles.prepend(QStringLiteral("bundle.json"));

    if (!writer.finish(&result.error))
        return result;
    result.success = true;
    return result;
}
