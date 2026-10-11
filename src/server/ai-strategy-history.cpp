#include "ai-strategy-history.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QSaveFile>
#include <cmath>
#include <limits>
#include <mutex>

namespace {
constexpr qint64 MaximumBytes = 8 * 1024 * 1024;
constexpr qint64 MaximumArchiveBytes = 32 * 1024 * 1024;
constexpr int MaximumKeys = 4096;
const QStringList Statistics = {QStringLiteral("drawData"), QStringLiteral("convertData"),
    QStringLiteral("damageData"), QStringLiteral("throwData")};
const QStringList Mappings = {QStringLiteral("aiResponse"), QStringLiteral("card_damage_nature")};

bool fail(QString *error, const char *reason)
{
    if (error) *error = QString::fromLatin1(reason);
    return false;
}
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool nameValid(const QString &name)
{
    return !name.isEmpty() && name.size() <= 256 && !name.contains(QChar::Null);
}
QString checkedDirectory(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.exists() || !info.isDir() || info.isSymLink()) {
        fail(error, "AI_HISTORY_INVALID_DIRECTORY"); return {};
    }
    const QString canonical = info.canonicalFilePath();
    // Refuse symlink traversal as well as a symlink final component. Configuration
    // can pass the resolved location explicitly instead.
    if (canonical.isEmpty() || QDir::cleanPath(info.absoluteFilePath()) != canonical) {
        fail(error, "AI_HISTORY_DIRECTORY_ALIAS"); return {};
    }
    return canonical;
}
bool readFile(const QString &path, QByteArray *bytes, bool *exists, QString *error,
              qint64 maximum = MaximumBytes)
{
    const QFileInfo info(path);
    *exists = info.exists();
    if (info.isSymLink() || (*exists && !info.isFile()))
        return fail(error, "AI_HISTORY_UNSAFE_FILE");
    if (!*exists) { bytes->clear(); return true; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "AI_HISTORY_READ_FAILED");
    *bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, "AI_HISTORY_READ_FAILED");
    if (bytes->size() > maximum) return fail(error, "AI_HISTORY_TOO_LARGE");
    return true;
}

QJsonObject emptyData()
{
    QJsonObject result;
    for (const QString &key : Statistics + Mappings) result.insert(key, QJsonObject{});
    return result;
}

// There is deliberately no recursive/free-form JSON carrier in the format.
// allowlists == nullptr is only used to validate an on-disk partition or a
// quarantined legacy archive, never to authorize a policy write/projection.
bool normalizeData(const QJsonObject &source, QJsonObject *result, bool legacy,
                   const QSet<QString> *skills, const QSet<QString> *cards,
                   QString *error, bool allowEmptySampleObject = false)
{
    *result = emptyData();
    if (source.size() > Statistics.size() + Mappings.size())
        return fail(error, "AI_HISTORY_UNKNOWN_CATEGORY");
    for (auto category = source.constBegin(); category != source.constEnd(); ++category) {
        const bool statistic = Statistics.contains(category.key());
        if (!statistic && !Mappings.contains(category.key()))
            return fail(error, "AI_HISTORY_UNKNOWN_CATEGORY");
        if (!category.value().isObject()) return fail(error, "AI_HISTORY_INVALID_CATEGORY");
        const QJsonObject input = category.value().toObject();
        if (input.size() > MaximumKeys) return fail(error, "AI_HISTORY_TOO_MANY_KEYS");
        QJsonObject output;
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) {
            if (!nameValid(row.key())) return fail(error, "AI_HISTORY_INVALID_KEY");
            if ((statistic && skills && !skills->contains(row.key()))
                || (!statistic && cards && !cards->contains(row.key())))
                return fail(error, "AI_HISTORY_UNDISCLOSED_KEY");
            if (statistic) {
                // The bounded Lua bridge cannot distinguish an empty array
                // from an empty object. This exception is only for policy
                // input at this exact schema position; disk format stays strict.
                if (!row.value().isArray() && !(allowEmptySampleObject
                    && row.value().isObject() && row.value().toObject().isEmpty()))
                    return fail(error, "AI_HISTORY_INVALID_SAMPLES");
                QJsonArray numbers;
                const QJsonArray samples = row.value().toArray();
                if (samples.size() > 99) return fail(error, "AI_HISTORY_TOO_MANY_SAMPLES");
                for (const QJsonValue &sample : samples) {
                    bool converted = false;
                    const double value = sample.isDouble() ? sample.toDouble()
                        : legacy && sample.isString() ? sample.toString().toDouble(&converted) : 0;
                    if ((!sample.isDouble() && !converted) || !std::isfinite(value))
                        return fail(error, "AI_HISTORY_NONNUMERIC_SAMPLE");
                    numbers.append(value);
                }
                output.insert(row.key(), numbers);
            } else if (category.key() == QLatin1String("aiResponse")) {
                if (!row.value().isString() || !nameValid(row.value().toString()))
                    return fail(error, "AI_HISTORY_INVALID_RESPONSE");
                if (cards && !cards->contains(row.value().toString()))
                    return fail(error, "AI_HISTORY_UNDISCLOSED_RESPONSE");
                output.insert(row.key(), row.value());
            } else {
                const double nature = row.value().toDouble(-1);
                // DamageStruct::Nature is a stable serialized enum: Normal,
                // Fire, Thunder, Ice, God, Poison. Never accept arbitrary JSON here.
                if (!row.value().isDouble() || nature != std::floor(nature) || nature < 0 || nature > 5)
                    return fail(error, "AI_HISTORY_INVALID_NATURE");
                output.insert(row.key(), row.value());
            }
        }
        result->insert(category.key(), output);
    }
    return true;
}

QJsonObject projected(const QJsonObject &source, const QSet<QString> &skills,
                      const QSet<QString> &cards)
{
    QJsonObject result = emptyData();
    for (const QString &category : Statistics + Mappings) {
        QJsonObject rows;
        const QJsonObject input = source.value(category).toObject();
        const QSet<QString> &allowed = Statistics.contains(category) ? skills : cards;
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) {
            if (!allowed.contains(row.key())) continue;
            if (category == QLatin1String("aiResponse") && !cards.contains(row.value().toString())) continue;
            rows.insert(row.key(), row.value());
        }
        result.insert(category, rows);
    }
    return result;
}

struct Loaded {
    QJsonObject data = emptyData();
    QString revision = QStringLiteral("absent");
    quint64 generation = 0;
};
bool load(const QString &path, const QString &scope, Loaded *loaded, QString *error)
{
    QByteArray bytes; bool exists;
    if (!readFile(path, &bytes, &exists, error)) return false;
    if (!exists) return true;
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, "AI_HISTORY_CORRUPT_JSON");
    const QJsonObject envelope = doc.object();
    if (envelope.size() != 5 || envelope.value("format") != QLatin1String("qsanguosha.isolated-ai-history")
        || envelope.value("version") != 1 || envelope.value("scope") != scope
        || !envelope.value("generation").isString() || !envelope.value("data").isObject())
        return fail(error, "AI_HISTORY_INVALID_ENVELOPE");
    bool ok = false;
    loaded->generation = envelope.value("generation").toString().toULongLong(&ok);
    if (!ok || loaded->generation == 0 || QString::number(loaded->generation) != envelope.value("generation").toString())
        return fail(error, "AI_HISTORY_INVALID_GENERATION");
    if (!normalizeData(envelope.value("data").toObject(), &loaded->data, false, nullptr, nullptr, error)) return false;
    loaded->revision = digest(bytes);
    return true;
}

std::shared_ptr<std::mutex> mutexFor(const QString &path)
{
    static QMutex mapMutex;
    static QHash<QString, std::weak_ptr<std::mutex>> mutexes;
    QMutexLocker locker(&mapMutex);
    // Do not accumulate one permanent entry for every retired Room identity.
    for (auto it = mutexes.begin(); it != mutexes.end();) {
        if (it.value().expired()) it = mutexes.erase(it); else ++it;
    }
    auto result = mutexes.value(path).lock();
    if (!result) { result = std::make_shared<std::mutex>(); mutexes.insert(path, result); }
    return result;
}
}

struct AIStrategyHistoryWrite::Impl {
    std::shared_ptr<std::mutex> mutex;
    std::unique_lock<std::mutex> threadLock;
    std::unique_ptr<QLockFile> processLock;
    std::unique_ptr<QSaveFile> file;
    QString revision;
    bool committed = false;

    bool lock(const QString &path, QString *error)
    {
        mutex = mutexFor(path);
        threadLock = std::unique_lock<std::mutex>(*mutex, std::try_to_lock);
        if (!threadLock.owns_lock()) return fail(error, "AI_HISTORY_BUSY");
        const QFileInfo lockInfo(path + QStringLiteral(".lock"));
        if (lockInfo.isSymLink()) return fail(error, "AI_HISTORY_UNSAFE_LOCK");
        processLock = std::make_unique<QLockFile>(lockInfo.absoluteFilePath());
        processLock->setStaleLockTime(30000);
        if (!processLock->tryLock(0)) return fail(error, "AI_HISTORY_BUSY");
        return true;
    }
    bool stage(const QString &path, const QByteArray &bytes, QString *error,
               qint64 maximum = MaximumBytes)
    {
        if (bytes.size() > maximum) return fail(error, "AI_HISTORY_TOO_LARGE");
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            return fail(error, "AI_HISTORY_UNSAFE_FILE");
        file = std::make_unique<QSaveFile>(path);
        file->setDirectWriteFallback(false);
        if (!file->open(QIODevice::WriteOnly)
            || !file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file->write(bytes) != bytes.size()) return fail(error, "AI_HISTORY_STAGE_FAILED");
        revision = digest(bytes);
        return true;
    }
};

AIStrategyHistoryWrite::AIStrategyHistoryWrite(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
AIStrategyHistoryWrite::~AIStrategyHistoryWrite() = default;
QString AIStrategyHistoryWrite::revision() const { return m_impl->revision; }
bool AIStrategyHistoryWrite::commit(QString *error)
{
    if (error) error->clear();
    if (m_impl->committed) return fail(error, "AI_HISTORY_ALREADY_COMMITTED");
    if (!m_impl->file || !m_impl->file->commit()) return fail(error, "AI_HISTORY_COMMIT_FAILED");
    m_impl->committed = true;
    m_impl->file.reset();
    m_impl->processLock.reset();
    if (m_impl->threadLock.owns_lock()) m_impl->threadLock.unlock();
    return true;
}

AIStrategyHistoryStore::AIStrategyHistoryStore(const QString &rootDirectory, const QString &trustedScope)
{
    m_root = checkedDirectory(rootDirectory, &m_error);
    if (!nameValid(trustedScope)) { fail(&m_error, "AI_HISTORY_INVALID_SCOPE"); return; }
    m_scopeHash = digest(QByteArrayLiteral("qsanguosha-isolated-history-v1:") + trustedScope.toUtf8());
}
AIStrategyHistorySnapshot AIStrategyHistoryStore::read(const QSet<QString> &skills, const QSet<QString> &cards) const
{
    AIStrategyHistorySnapshot result;
    if (!m_error.isEmpty()) { result.error = m_error; return result; }
    Loaded loaded;
    if (!load(QDir(m_root).filePath(m_scopeHash + QStringLiteral(".json")), m_scopeHash, &loaded, &result.error)) return result;
    result.known = true;
    result.data = projected(loaded.data, skills, cards);
    result.revision = loaded.revision;
    return result;
}
std::unique_ptr<AIStrategyHistoryWrite> AIStrategyHistoryStore::prepare(
    const QString &expectedRevision, const QJsonObject &replacement,
    const QSet<QString> &skills, const QSet<QString> &cards, QString *error) const
{
    if (error) error->clear();
    if (!m_error.isEmpty()) { if (error) *error = m_error; return {}; }
    QJsonObject valid;
    if (!normalizeData(replacement, &valid, false, &skills, &cards, error, true)) return {};
    const QString path = QDir(m_root).filePath(m_scopeHash + QStringLiteral(".json"));
    auto state = std::make_unique<AIStrategyHistoryWrite::Impl>();
    if (!state->lock(path, error)) return {};
    Loaded loaded;
    if (!load(path, m_scopeHash, &loaded, error)) return {};
    if (loaded.revision != expectedRevision) { fail(error, "AI_HISTORY_STALE"); return {}; }
    if (loaded.generation == std::numeric_limits<quint64>::max()) { fail(error, "AI_HISTORY_GENERATION_OVERFLOW"); return {}; }
    QJsonObject merged = loaded.data;
    for (const QString &category : Statistics + Mappings) {
        QJsonObject rows = merged.value(category).toObject();
        // Only erase keys the viewer actually received, including both sides
        // of aiResponse. Hidden older entries remain opaque and recoverable.
        const QJsonObject visible = projected(loaded.data, skills, cards).value(category).toObject();
        for (auto row = visible.constBegin(); row != visible.constEnd(); ++row) rows.remove(row.key());
        const QJsonObject input = valid.value(category).toObject();
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) rows.insert(row.key(), row.value());
        merged.insert(category, rows);
    }
    QJsonObject revalidated;
    if (!normalizeData(merged, &revalidated, false, nullptr, nullptr, error)) return {};
    const QJsonObject envelope{{"format", "qsanguosha.isolated-ai-history"}, {"version", 1},
        {"scope", m_scopeHash}, {"generation", QString::number(loaded.generation + 1)}, {"data", revalidated}};
    if (!state->stage(path, QJsonDocument(envelope).toJson(QJsonDocument::Compact), error)) return {};
    return std::unique_ptr<AIStrategyHistoryWrite>(new AIStrategyHistoryWrite(std::move(state)));
}

namespace {
struct LegacyFiles {
    QString root;
    QMap<QString, QByteArray> files;
    QString error;
};
LegacyFiles legacyFiles(const QString &directory, const QString &modernAiDataPath)
{
    LegacyFiles result;
    result.root = checkedDirectory(directory, &result.error);
    if (result.root.isEmpty()) return result;
    qint64 total = 0;
    for (const QString &name : QStringList{QStringLiteral("AiData")} + Statistics) {
        QByteArray bytes; bool exists;
        QString path = QDir(result.root).filePath(name);
        if (name == QLatin1String("AiData") && !modernAiDataPath.isEmpty()) {
            const QFileInfo modern(modernAiDataPath);
            if (!modern.isAbsolute() || modern.fileName() != QLatin1String("AiData")
                || checkedDirectory(modern.absolutePath(), &result.error).isEmpty()) {
                if (result.error.isEmpty()) fail(&result.error, "AI_HISTORY_INVALID_LEGACY_PATH");
                return result;
            }
            path = modern.absoluteFilePath();
        }
        if (!readFile(path, &bytes, &exists, &result.error)) return result;
        if (!exists) continue;
        total += bytes.size();
        // Bound total input independently of the recovery envelope, which also
        // holds base64 originals and parsed samples. Never truncate input.
        if (total > MaximumBytes) { fail(&result.error, "AI_HISTORY_LEGACY_TOO_LARGE"); return result; }
        result.files.insert(name, bytes);
    }
    return result;
}
AIStrategyLegacyHistory inspectLegacy(const LegacyFiles &files)
{
    AIStrategyLegacyHistory result;
    result.sourceFiles = files.files.keys();
    if (!files.error.isEmpty()) { result.error = files.error; return result; }
    QJsonObject raw;
    const QByteArray json = files.files.value(QStringLiteral("AiData"));
    bool useOldFiles = json.isEmpty();
    if (!json.isEmpty()) {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(json, &parse);
        if (parse.error != QJsonParseError::NoError || doc.isNull()) {
            result.warnings.append(QStringLiteral("AI_HISTORY_LEGACY_CORRUPT_JSON"));
            useOldFiles = true; // Engine::getAiData exposes empty string here.
        } else if (!doc.isObject()) {
            fail(&result.error, "AI_HISTORY_LEGACY_INVALID_OBJECT"); return result;
        } else {
            raw = doc.object();
        }
    }
    if (useOldFiles) {
        for (const QString &category : Statistics) {
            QJsonObject rows;
            const QByteArray bytes = files.files.value(category);
            const QString text = QString::fromUtf8(bytes);
            if (text.toUtf8() != bytes) { fail(&result.error, "AI_HISTORY_LEGACY_ENCODING"); return result; }
            for (const QString &line : text.split('\n')) {
                if (line.isEmpty()) continue;
                const QStringList columns = line.split(':');
                if (columns.size() < 2) continue; // donor ignores lines without colon
                QJsonArray samples;
                for (const QString &sample : columns.at(1).split(','))
                    if (!sample.isEmpty()) samples.append(sample);
                rows.insert(columns.at(0), samples); // donor last definition wins
            }
            raw.insert(category, rows);
        }
    }
    result.valid = normalizeData(raw, &result.data, true, nullptr, nullptr, &result.error);
    return result;
}
}
AIStrategyLegacyHistory inspectAIStrategyLegacyHistory(const QString &legacyDirectory,
                                                      const QString &modernAiDataPath)
{
    return inspectLegacy(legacyFiles(legacyDirectory, modernAiDataPath));
}
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
    const QString &archiveDirectory, const QString &legacyDirectory, QString *error,
    const QString &modernAiDataPath)
{
    if (error) error->clear();
    const QString root = checkedDirectory(archiveDirectory, error);
    if (root.isEmpty()) return {};
    const LegacyFiles files = legacyFiles(legacyDirectory, modernAiDataPath);
    if (!files.error.isEmpty()) { if (error) *error = files.error; return {}; }
    if (files.files.isEmpty()) { fail(error, "AI_HISTORY_NO_LEGACY_FILES"); return {}; }
    const AIStrategyLegacyHistory parsed = inspectLegacy(files);
    QJsonObject originals;
    for (auto it = files.files.constBegin(); it != files.files.constEnd(); ++it)
        originals.insert(it.key(), QJsonObject{{"encoding", "base64"},
            {"bytes", QString::fromLatin1(it.value().toBase64())}, {"sha256", digest(it.value())}});
    const QJsonObject archive{{"format", "qsanguosha.isolated-ai-history-quarantine"}, {"version", 1},
        {"originals", originals}, {"parsed_valid", parsed.valid}, {"parsed", parsed.data},
        {"parse_error", parsed.error}, {"warnings", QJsonArray::fromStringList(parsed.warnings)},
        {"provenance", "legacy-unpartitioned-unattributed"}};
    const QByteArray bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    const QString path = QDir(root).filePath(QStringLiteral("legacy-") + digest(bytes) + QStringLiteral(".json"));
    auto state = std::make_unique<AIStrategyHistoryWrite::Impl>();
    if (!state->lock(path, error)) return {};
    QByteArray existing; bool exists;
    if (!readFile(path, &existing, &exists, error, MaximumArchiveBytes)) return {};
    if (exists && existing != bytes) { fail(error, "AI_HISTORY_ARCHIVE_CONFLICT"); return {}; }
    if (!state->stage(path, bytes, error, MaximumArchiveBytes)) return {};
    return std::unique_ptr<AIStrategyHistoryWrite>(new AIStrategyHistoryWrite(std::move(state)));
}
