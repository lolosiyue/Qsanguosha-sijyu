#include "headless-replay-session.h"

#include "engine.h"
#include "record-buffer.h"
#include "room.h"
#include "server.h"
#include "serverplayer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
bool saveIndex(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot save replay index %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
}

HeadlessReplaySession::HeadlessReplaySession(Room &room, const QString &batchDirectory, int gameNumber)
    : m_room(room), m_batchDirectory(batchDirectory),
      m_directory(QDir(batchDirectory).filePath(QStringLiteral("game-%1").arg(gameNumber, 6, 10, QLatin1Char('0')))),
      m_startedAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)), m_gameNumber(gameNumber)
{
    m_clock.start();
}

bool HeadlessReplaySession::initialize(QString *error)
{
    if (QFileInfo::exists(m_directory) || !QDir().mkpath(m_directory)) {
        if (error) *error = QStringLiteral("Replay game directory already exists or cannot be created: %1").arg(m_directory);
        return false;
    }
    return writeIndex(QStringLiteral("running"), QString(), QString(), error);
}

bool HeadlessReplaySession::addPlayer(ServerPlayer *player, bool duringGame, QString *error)
{
    for (const Perspective &existing : m_perspectives)
        if (existing.player == player) return true;
    if (!player || player->objectName().isEmpty()) {
        if (error) *error = QStringLiteral("Replay perspective requires a named player");
        return false;
    }
    Perspective entry;
    entry.player = player;
    entry.playerName = player->objectName();
    entry.screenName = player->screenName();
    entry.file = QStringLiteral("seat-%1.txt").arg(m_perspectives.size() + 1, 3, 10, QLatin1Char('0'));
    entry.availableFromMs = duringGame ? m_clock.elapsed() : 0;
    player->startRecord();
    player->recordBuffer->beginTimeline(m_clock, entry.availableFromMs);
    if (!player->recordBuffer->openTextStream(QDir(m_directory).filePath(entry.file), error)) return false;
    m_perspectives.append(entry);
    // Reuse the same typed SETUP sent to a real client before its identity/roster.
    // Online clients already received SETUP during signup. Seed only their new
    // recorder, without reopening the client's room or replaying the handshake.
    if (!Server::sendSetup(player, error, player->isOnline() ? player->recordBuffer : nullptr)) return false;
    return writeIndex(QStringLiteral("running"), QString(), QString(), error);
}

void HeadlessReplaySession::completeBootstrap(ServerPlayer *player)
{
    for (const Perspective &entry : m_perspectives)
        if (entry.player && (!player || entry.player == player))
            entry.player->recordBuffer->completeBootstrap();
}

void HeadlessReplaySession::setCardCatalog(const QVariant &catalog)
{
    m_cardCatalog = catalog;
}

QVariant HeadlessReplaySession::cardCatalog() const
{
    return m_cardCatalog;
}

QString HeadlessReplaySession::directory() const
{
    return m_directory;
}

bool HeadlessReplaySession::finish(const QString &status, const QString &winner,
                                 const QString &detail, QString *error)
{
    if (m_finished) return true;
    QString failure = detail;
    bool saved = true;
    for (const Perspective &entry : m_perspectives) {
        if (!entry.player || !entry.player->saveRecord(QDir(m_directory).filePath(entry.file))) {
            saved = false;
            const QString cause = entry.player && entry.player->recordBuffer
                ? entry.player->recordBuffer->errorString() : QStringLiteral("recorded player no longer exists");
            failure += QStringLiteral("\n%1: %2").arg(entry.playerName, cause);
        }
    }
    QString indexError;
    if (!writeIndex(saved ? status : QStringLiteral("failed"), winner, failure.trimmed(), &indexError)) {
        saved = false;
        failure += QLatin1Char('\n') + indexError;
    }
    m_finished = true;
    if (!saved && error) *error = failure.trimmed();
    return saved;
}

bool HeadlessReplaySession::writeIndex(const QString &status, const QString &winner,
                                     const QString &detail, QString *error)
{
    QJsonArray perspectives;
    int roundCount = 0;
    for (const Perspective &entry : m_perspectives) {
        QJsonObject seat{
            {QStringLiteral("player_name"), entry.playerName},
            {QStringLiteral("screen_name"), entry.screenName},
            {QStringLiteral("file"), entry.file},
            {QStringLiteral("available_from_ms"), QString::number(entry.availableFromMs)}
        };
        if (entry.player) {
            seat.insert(QStringLiteral("general"), entry.player->getGeneralName());
            seat.insert(QStringLiteral("general2"), entry.player->getGeneral2Name());
            seat.insert(QStringLiteral("role"), entry.player->getRole());
            seat.insert(QStringLiteral("alive"), entry.player->isAlive());
            seat.insert(QStringLiteral("seat"), entry.player->getSeat());
            if (entry.player->recordBuffer) {
                seat.insert(QStringLiteral("event_count"), entry.player->recordBuffer->getRecords().size());
                roundCount = qMax(roundCount, entry.player->recordBuffer->roundCount());
            }
        }
        if (status != QLatin1String("running")) {
            QFile file(QDir(m_directory).filePath(entry.file));
            if (file.open(QIODevice::ReadOnly)) {
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (hash.addData(&file)) seat.insert(QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex()));
                seat.insert(QStringLiteral("bytes"), QString::number(file.size()));
            }
        }
        perspectives.append(seat);
    }
    QJsonObject game{
        {QStringLiteral("format"), QStringLiteral("qsanguosha-headless-game")},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("game_number"), m_gameNumber},
        {QStringLiteral("seed"), QString::number(m_room.getGameSeed())},
        {QStringLiteral("mode"), m_room.getMode()},
        {QStringLiteral("status"), status},
        {QStringLiteral("winner"), winner},
        {QStringLiteral("started_at_utc"), m_startedAt},
        {QStringLiteral("duration_ms"), QString::number(m_clock.elapsed())},
        {QStringLiteral("game_version"), Sanguosha->getVersion()},
        {QStringLiteral("round_count"), roundCount},
        {QStringLiteral("perspectives"), perspectives}
    };
    if (!detail.isEmpty()) game.insert(QStringLiteral("detail"), detail);
    if (!saveIndex(QDir(m_directory).filePath(QStringLiteral("index.json")), game, error)) return false;

    const QString indexPath = QDir(m_batchDirectory).filePath(QStringLiteral("index.json"));
    QJsonObject batch;
    QFile existing(indexPath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            if (error) *error = existing.errorString();
            return false;
        }
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()
            || document.object().value(QStringLiteral("format")).toString() != QLatin1String("qsanguosha-headless-batch")) {
            if (error) *error = QStringLiteral("Existing batch replay index is invalid");
            return false;
        }
        batch = document.object();
        existing.close();
    } else {
        batch.insert(QStringLiteral("format"), QStringLiteral("qsanguosha-headless-batch"));
        batch.insert(QStringLiteral("schema_version"), 1);
    }
    const QString relativeDirectory = QDir(m_batchDirectory).relativeFilePath(m_directory);
    game.insert(QStringLiteral("index"), relativeDirectory + QStringLiteral("/index.json"));
    if (!m_perspectives.isEmpty())
        game.insert(QStringLiteral("replay"), relativeDirectory + QLatin1Char('/') + m_perspectives.first().file);
    // Paths inside each game remain relative to that game's index.
    QJsonArray games = batch.value(QStringLiteral("games")).toArray();
    bool replaced = false;
    for (int i = 0; i < games.size(); ++i) {
        if (games[i].toObject().value(QStringLiteral("game_number")).toInt() == m_gameNumber) {
            games[i] = game;
            replaced = true;
            break;
        }
    }
    if (!replaced) games.append(game);
    batch.insert(QStringLiteral("games"), games);
    return saveIndex(indexPath, batch, error);
}
