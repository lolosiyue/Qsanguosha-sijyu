#include "battle-statistics.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QtMath>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s", __FILE__, __LINE__, #x); } while (false)

namespace {

using BattleStatistics::Match;

struct FixtureOptions {
    int slashHp = 2;
    int slashArmor = 1;
    bool complete = true;
    QString p1Control = QStringLiteral("human");
    QString p2Control = QStringLiteral("robot");
    QString p2Role = QStringLiteral("rebel");
    bool mixedControl = false;
    bool p1IdentityChanged = false;
    QString excludedReason;
    QString p1General = QStringLiteral("hero");
    QString p2General = QStringLiteral("rival");
    QString mode = QStringLiteral("02p");
    QString role = QStringLiteral("lord");
    bool addSecondP2Turn = false;
};

static void addCardUse(ResolutionHistoryService &history, const QString &from,
                       const QVariantList &classes)
{
    const qint64 event = history.beginEvent(QStringLiteral("use_card"), {}, false);
    CHECK(event > 0);
    CHECK(history.appendFact(event, QStringLiteral("use_card"),
        {{QStringLiteral("from"), from},
         {QStringLiteral("card"), QVariantMap{{QStringLiteral("classes"), classes}}}}) > 0);
    history.finishEvent(event);
}

static void addDamage(ResolutionHistoryService &history, const QString &from,
                      const QString &to, int hp, int armor,
                      const QVariantList &cardClasses)
{
    const qint64 event = history.beginEvent(QStringLiteral("damage"));
    CHECK(event > 0);
    CHECK(history.appendFact(event, QStringLiteral("actual_damage"),
        {{QStringLiteral("from"), from}, {QStringLiteral("to"), to},
         {QStringLiteral("hp_loss"), hp}, {QStringLiteral("absorbed"), armor},
         {QStringLiteral("card"), QVariantMap{{QStringLiteral("classes"), cardClasses}}}}) > 0);
    if (hp > 0)
        CHECK(history.appendFact(event, QStringLiteral("damage_component"),
            {{QStringLiteral("component"), QStringLiteral("hp")}, {QStringLiteral("amount"), hp}}) > 0);
    if (armor > 0)
        CHECK(history.appendFact(event, QStringLiteral("damage_component"),
            {{QStringLiteral("component"), QStringLiteral("armor")}, {QStringLiteral("amount"), armor}}) > 0);
    history.finishEvent(event);
}

static void addRecovery(ResolutionHistoryService &history, const QString &from,
                       const QString &to, int amount)
{
    const qint64 event = history.beginEvent(QStringLiteral("recover"));
    CHECK(event > 0);
    CHECK(history.appendFact(event, QStringLiteral("actual_recover"),
        {{QStringLiteral("from"), from}, {QStringLiteral("to"), to},
         {QStringLiteral("amount"), amount}}) > 0);
    history.finishEvent(event);
}

static void addTurn(ResolutionHistoryService &history, const QString &player,
                    bool extra, const std::function<void()> &actions = {},
                    const QString &outcome = QStringLiteral("completed"))
{
    const qint64 turn = history.beginEvent(QStringLiteral("turn"),
        {{QStringLiteral("player"), player}, {QStringLiteral("extra_turn"), extra}});
    CHECK(turn > 0);
    if (actions) actions();
    history.finishEvent(turn, outcome);
}

static Match makeMatch(const QString &root, quint64 generation,
                       const FixtureOptions &options = {})
{
    Match match;
    match.rootMatchId = root;
    match.generation = generation;
    match.branchId = QStringLiteral("branch-%1").arg(generation);

    QVariantMap p1{{QStringLiteral("player"), QStringLiteral("p1")},
                   {QStringLiteral("general"), options.p1General},
                   {QStringLiteral("general2"), QString()},
                   {QStringLiteral("role"), options.role},
                   {QStringLiteral("control"), options.p1Control},
                   {QStringLiteral("control_history"), QVariantList{options.p1Control}},
                   {QStringLiteral("identity_changed"), options.p1IdentityChanged}};
    if (options.p1IdentityChanged)
        p1[QStringLiteral("control_history")] = QVariantList{QStringLiteral("human"), QStringLiteral("external")};
    const QVariantMap p2{{QStringLiteral("player"), QStringLiteral("p2")},
                         {QStringLiteral("general"), options.p2General},
                         {QStringLiteral("general2"), QString()},
                         {QStringLiteral("role"), options.p2Role},
                         {QStringLiteral("control"), options.p2Control},
                         {QStringLiteral("control_history"), QVariantList{options.p2Control}},
                         {QStringLiteral("identity_changed"), false}};
    match.metadata = {{QStringLiteral("mode"), options.mode},
                      {QStringLiteral("player_count"), 2},
                      {QStringLiteral("rules_version"), QStringLiteral("rules-test-v1")},
                      {QStringLiteral("terminal"), true},
                      {QStringLiteral("winner"), QStringLiteral("p1")},
                      {QStringLiteral("mixed_control"), options.mixedControl},
                      {QStringLiteral("participants"), QVariantList{p1, p2}}};
    if (!options.excludedReason.isEmpty())
        match.metadata.insert(QStringLiteral("excluded_reason"), options.excludedReason);

    if (!options.complete) {
        // The default snapshot is intentionally incomplete. It exercises the
        // persisted unknown-coverage state without fabricating zero metrics.
        return match;
    }

    ResolutionHistoryService history;
    history.beginRound();
    addTurn(history, QStringLiteral("p1"), false, [&] {
        addCardUse(history, QStringLiteral("p1"), QVariantList{QStringLiteral("Slash")});
        addDamage(history, QStringLiteral("p1"), QStringLiteral("p2"),
                  options.slashHp, options.slashArmor, QVariantList{QStringLiteral("Slash")});
    });
    // A second ordinary turn gives summaries enough turn observations when
    // five matches share an environment. Healing another seat is support.
    addTurn(history, QStringLiteral("p1"), false, [&] {
        addRecovery(history, QStringLiteral("p1"), QStringLiteral("p2"), 1);
    });
    addTurn(history, QStringLiteral("p2"), false, [&] {
        addCardUse(history, QStringLiteral("p2"), QVariantList{QStringLiteral("AOE")});
        addDamage(history, QStringLiteral("p2"), QStringLiteral("p1"), 1, 0,
                  QVariantList{QStringLiteral("AOE")});
    });
    if (options.addSecondP2Turn)
        addTurn(history, QStringLiteral("p2"), false);
    history.endRound();
    match.history = history.snapshot();
    CHECK(match.history.isComplete());
    return match;
}

static void addPlayerState(ResolutionHistoryService &history, const QString &player,
                           const QString &boundary, int hpBefore, int hpAfter,
                           int maxHpBefore, int maxHpAfter,
                           int handBefore, int handAfter)
{
    const QVariantMap data{{QStringLiteral("player"), player},
                           {QStringLiteral("boundary"), boundary},
                           {QStringLiteral("hp_before"), hpBefore},
                           {QStringLiteral("hp_after"), hpAfter},
                           {QStringLiteral("maxhp_before"), maxHpBefore},
                           {QStringLiteral("maxhp_after"), maxHpAfter},
                           {QStringLiteral("hand_count_before"), handBefore},
                           {QStringLiteral("hand_count_after"), handAfter}};
    const qint64 event = history.beginEvent(QStringLiteral("player_state"), data);
    CHECK(event > 0);
    CHECK(history.appendFact(event, QStringLiteral("player_state"), data) > 0);
    history.finishEvent(event);
}

static void addTurnSnapshot(ResolutionHistoryService &history, qint64 turn,
                            const QString &player, const QString &boundary,
                            int hp, int maxHp, int hand, bool alive = true)
{
    CHECK(history.appendFact(turn, QStringLiteral("turn_hp_snapshot"),
        {{QStringLiteral("player"), player}, {QStringLiteral("boundary"), boundary},
         {QStringLiteral("hp"), hp}, {QStringLiteral("maxhp"), maxHp},
         {QStringLiteral("hand_count"), hand}, {QStringLiteral("alive"), alive}}) > 0);
}

static Match makeDevelopmentMatch(const QString &root, quint64 generation,
                                  const QList<int> &hands, const QList<int> &maxHps,
                                  const QList<int> &turnDamage,
                                  int deadEndCycle = -1, int missingEndCycle = -1,
                                  const QString &general = QStringLiteral("growth-hero"))
{
    CHECK(hands.size() == maxHps.size() && hands.size() == turnDamage.size());
    FixtureOptions options;
    options.p1General = general;
    Match match = makeMatch(root, generation, options);
    ResolutionHistoryService history;
    history.beginRound();
    addPlayerState(history, QStringLiteral("p1"), QStringLiteral("baseline"), 4, 4, 3, 3, 0, 0);
    addPlayerState(history, QStringLiteral("p2"), QStringLiteral("baseline"), 4, 4, 3, 3, 0, 0);
    int hand = 0;
    int maxHp = 3;
    for (int i = 0; i < hands.size(); ++i) {
        const qint64 turn = history.beginEvent(QStringLiteral("turn"),
            {{QStringLiteral("player"), QStringLiteral("p1")}, {QStringLiteral("extra_turn"), false}});
        CHECK(turn > 0);
        addTurnSnapshot(history, turn, QStringLiteral("p1"), QStringLiteral("start"), 4, maxHp, hand);
        if (turnDamage.at(i) > 0)
            addDamage(history, QStringLiteral("p1"), QStringLiteral("p2"),
                      turnDamage.at(i), 0, {});
        const int nextHand = hands.at(i), nextMaxHp = maxHps.at(i);
        addPlayerState(history, QStringLiteral("p1"), QStringLiteral("commit"),
                       4, 4, maxHp, nextMaxHp, hand, nextHand);
        if (i != missingEndCycle)
            addTurnSnapshot(history, turn, QStringLiteral("p1"), QStringLiteral("end"),
                            4, nextMaxHp, nextHand, i != deadEndCycle);
        history.finishEvent(turn);
        hand = nextHand;
        maxHp = nextMaxHp;
    }
    history.endRound();
    match.history = history.snapshot();
    CHECK(match.history.isComplete());
    return match;
}

static QVariantMap rowFor(const QVariantList &rows, const QString &player)
{
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("player")).toString() == player)
            return row;
    }
    return {};
}

static QVariantMap summaryFor(const QString &path, const QString &dataset,
                              const QString &general, const QString &mode = QString())
{
    QString error;
    const QVariantList summaries = BattleStatistics::readSummaries(path, dataset, &error);
    CHECK(error.isEmpty());
    for (const QVariant &value : summaries) {
        const QVariantMap summary = value.toMap();
        const QString summaryMode = summary.value(QStringLiteral("environment_details")).toMap()
            .value(QStringLiteral("mode")).toString();
        if (summary.value(QStringLiteral("general")).toString() == general
            && (mode.isEmpty() || summaryMode == mode))
            return summary;
    }
    return {};
}

static QVariantMap matchMetadata(const QString &path, const QString &root)
{
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap metadata;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        CHECK(db.open());
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT metadata FROM battle_matches WHERE root=?"));
        query.addBindValue(root);
        CHECK(query.exec() && query.next());
        metadata = QJsonDocument::fromJson(query.value(0).toByteArray()).toVariant().toMap();
        CHECK(!metadata.isEmpty());
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return metadata;
}

static void replaceMatchMetadata(const QString &path, const QString &root,
                                const QVariantMap &metadata)
{
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        CHECK(db.open());
        QSqlQuery query(db);
        query.prepare(QStringLiteral("UPDATE battle_matches SET metadata=? WHERE root=?"));
        query.addBindValue(QJsonDocument::fromVariant(metadata).toJson(QJsonDocument::Compact));
        query.addBindValue(root);
        CHECK(query.exec() && query.numRowsAffected() == 1);
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

static void saveMatch(const Match &match, const QString &path, bool invalidation = false)
{
    QString error;
    if (!BattleStatistics::save(match, path, &error, invalidation))
        qFatal("BattleStatistics::save failed: %s", qPrintable(error));
    CHECK(error.isEmpty());
}

static bool runStatisticsChild(const QStringList &arguments, QString *failure = nullptr)
{
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments(arguments);
    child.start();
    if (!child.waitForStarted(5000)) {
        if (failure) *failure = child.errorString();
        return false;
    }
    if (!child.waitForFinished(15000)) {
        child.kill();
        child.waitForFinished();
        if (failure) *failure = QStringLiteral("child process timed out");
        return false;
    }
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
        if (failure) *failure = QString::fromUtf8(child.readAllStandardError()
            + child.readAllStandardOutput());
        return false;
    }
    return true;
}

static int runStatisticsChildMode(const QStringList &arguments)
{
    if (arguments.size() < 3 || !arguments.at(1).startsWith(QStringLiteral("--statistics-")))
        return -1;
    const QString mode = arguments.at(1), path = arguments.at(2);
    if (mode == QStringLiteral("--statistics-dirty-crash")
        || mode == QStringLiteral("--statistics-sidecar-crash")) {
        if (arguments.size() != 4) return 2;
        const QString root = arguments.at(3);
        FixtureOptions oldValues; oldValues.slashHp = 3; oldValues.slashArmor = 0;
        const Match oldMatch = makeMatch(root, 0, oldValues);
        QString error;
        if (!BattleStatistics::armMatch(oldMatch, path, &error))
            qFatal("child armMatch failed: %s", qPrintable(error));
        if (!BattleStatistics::save(oldMatch, path, &error))
            qFatal("child initial save failed: %s", qPrintable(error));
        const QFileDevice::Permissions readOnly = QFileDevice::ReadOwner
            | QFileDevice::ReadGroup | QFileDevice::ReadOther;
        if (!QFile::setPermissions(path, readOnly))
            qFatal("child could not make database read-only");
        if (mode == QStringLiteral("--statistics-sidecar-crash")) {
            const QString timelineDirectory = path + QStringLiteral(".timeline");
            const QFileDevice::Permissions readExecute = QFileDevice::ReadOwner | QFileDevice::ExeOwner
                | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                | QFileDevice::ReadOther | QFileDevice::ExeOther;
            if (!QFile::setPermissions(timelineDirectory, readExecute))
                qFatal("child could not make timeline directory read-only");
        }
        FixtureOptions restoredValues; restoredValues.slashHp = 1; restoredValues.slashArmor = 0;
        const Match restored = makeMatch(root, 1, restoredValues);
        const bool notified = BattleStatistics::notifyTimelineRestore(restored, path, &error);
        if (mode == QStringLiteral("--statistics-dirty-crash")) {
            if (!notified) qFatal("child restore notification failed: %s", qPrintable(error));
        } else if (notified || error.isEmpty()) {
            qFatal("read-only sidecar unexpectedly accepted restore notification");
        }
        // Simulate process death before lifecycle close. This also discards
        // the in-process registry and any queued SQLite job.
        std::_Exit(0);
    }
    if (mode == QStringLiteral("--statistics-read-hidden")) {
        QString error;
        const auto rows = BattleStatistics::readSummaries(path, QStringLiteral("human"), &error);
        if (!rows.isEmpty() || error.isEmpty()
            || BattleStatistics::pendingTimelineInvalidations(path) <= 0)
            qFatal("fresh child did not quarantine uncertain rows; rows=%d error=%s",
                   rows.size(), qPrintable(error));
        return 0;
    }
    if (mode == QStringLiteral("--statistics-read-current")) {
        if (arguments.size() != 4) return 2;
        bool ok = false;
        const double expectedDamage = arguments.at(3).toDouble(&ok);
        if (!ok) return 2;
        QString error;
        const auto summaries = BattleStatistics::readSummaries(path, QStringLiteral("human"), &error);
        const auto summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
        const double actual = summary.value(QStringLiteral("metrics")).toMap()
            .value(QStringLiteral("damage")).toDouble();
        if (!error.isEmpty() || summaries.size() != 1
            || summary.value(QStringLiteral("games")).toInt() != 1
            || actual != expectedDamage
            || BattleStatistics::pendingTimelineInvalidations(path) != 0)
            qFatal("fresh child saw wrong current generation; rows=%d games=%d damage=%g error=%s",
                   summaries.size(), summary.value(QStringLiteral("games")).toInt(), actual,
                   qPrintable(error));
        return 0;
    }
    return -1;
}

static bool createDirectoryWithoutSymlinks(const QString &absolutePath, QString *error)
{
    QString current = QDir::rootPath();
    const QStringList components = absolutePath.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        current = QDir(current).filePath(component);
        const QFileInfo info(current);
        if (info.isSymLink()) {
            if (error) *error = QStringLiteral("Refusing symlinked fixture path component: %1").arg(current);
            return false;
        }
        if (info.exists()) {
            if (!info.isDir()) {
                if (error) *error = QStringLiteral("Fixture path component is not a directory: %1").arg(current);
                return false;
            }
            continue;
        }
        if (!QDir().mkdir(current)) {
            if (error) *error = QStringLiteral("Could not create fixture directory: %1").arg(current);
            return false;
        }
        const QFileInfo created(current);
        if (!created.isDir() || created.isSymLink()) {
            if (error) *error = QStringLiteral("Fixture directory changed during creation: %1").arg(current);
            return false;
        }
    }
    return true;
}

static bool saveSyntheticMatch(Match match, const QString &databasePath, QString *error)
{
    match.metadata.insert(QStringLiteral("synthetic"), true);
    if (!BattleStatistics::save(match, databasePath, error))
        return false;
    return error->isEmpty();
}

static bool generateStatisticsUiFixture(const QString &requestedRoot, QString *error)
{
    if (!QDir::isAbsolutePath(requestedRoot) || QDir::cleanPath(requestedRoot) != requestedRoot) {
        if (error) *error = QStringLiteral("Fixture root must be a clean absolute path");
        return false;
    }
    const QFileInfo requestedInfo(requestedRoot);
    if (requestedInfo.exists() || requestedInfo.isSymLink()) {
        if (error) *error = QStringLiteral("Fixture root must be a new, nonexistent directory");
        return false;
    }
    if (!createDirectoryWithoutSymlinks(requestedRoot, error))
        return false;

    const QString dataHome = QDir(requestedRoot).filePath(QStringLiteral("data"));
    const QString appData = QDir(dataHome).filePath(QStringLiteral("QSanguosha"));
    const QString configHome = QDir(requestedRoot).filePath(QStringLiteral("config"));
    const QString cacheHome = QDir(requestedRoot).filePath(QStringLiteral("cache"));
    for (const QString &directory : {dataHome, appData, configHome, cacheHome}) {
        if (!createDirectoryWithoutSymlinks(directory, error))
            return false;
    }
    const QString databasePath = QDir(appData).filePath(QStringLiteral("battle-statistics.sqlite"));

    for (int i = 1; i <= 5; ++i) {
        FixtureOptions options;
        options.p1General = QStringLiteral("SYNTHETIC-HumanHero");
        options.p2General = QStringLiteral("SYNTHETIC-AIOpponent");
        options.mode = QStringLiteral("SYNTHETIC-02p");
        options.addSecondP2Turn = true;
        Match match = makeMatch(QStringLiteral("SYNTHETIC-human-ai-%1").arg(i), 1, options);
        if (!saveSyntheticMatch(match, databasePath, error))
            return false;
    }
    for (int i = 1; i <= 5; ++i) {
        FixtureOptions options;
        options.p1Control = QStringLiteral("robot");
        options.p2Control = QStringLiteral("robot");
        options.p1General = QStringLiteral("SYNTHETIC-AISelfPlay");
        options.p2General = QStringLiteral("SYNTHETIC-AISelfPlay");
        options.mode = QStringLiteral("SYNTHETIC-02p");
        options.addSecondP2Turn = true;
        Match match = makeMatch(QStringLiteral("SYNTHETIC-ai-selfplay-%1").arg(i), 1, options);
        if (!saveSyntheticMatch(match, databasePath, error))
            return false;
    }

    // Leave one root in an active restored timeline. A fresh GUI process sees
    // its foreign active journal and can exercise the pending-data warning.
    FixtureOptions pendingOptions;
    pendingOptions.p1General = QStringLiteral("SYNTHETIC-PendingHero");
    pendingOptions.p2General = QStringLiteral("SYNTHETIC-PendingOpponent");
    pendingOptions.mode = QStringLiteral("SYNTHETIC-02p");
    const QString pendingRoot = QStringLiteral("SYNTHETIC-pending-restore");
    const Match oldTerminal = makeMatch(pendingRoot, 40, pendingOptions);
    if (!saveSyntheticMatch(oldTerminal, databasePath, error))
        return false;
    if (!BattleStatistics::armMatch(oldTerminal, databasePath, error))
        return false;
    pendingOptions.complete = false;
    Match restored = makeMatch(pendingRoot, 41, pendingOptions);
    restored.metadata.insert(QStringLiteral("synthetic"), true);
    restored.metadata.insert(QStringLiteral("terminal"), false);
    if (!BattleStatistics::notifyTimelineRestore(restored, databasePath, error))
        return false;
    BattleStatistics::waitForPendingWrites();

    qInfo().noquote() << "Synthetic battle statistics fixture created at" << requestedRoot;
    qInfo().noquote() << "Database:" << databasePath;
    qInfo().noquote() << "Pending restore journal left active for a fresh GUI process.";
    return true;
}

static int runStatisticsUiFixtureMode(const QStringList &arguments)
{
    if (arguments.size() < 2 || arguments.at(1) != QStringLiteral("--statistics-ui-fixture"))
        return -1;
#if !defined(Q_OS_LINUX)
    qCritical().noquote() << "The synthetic statistics UI fixture is supported only on Linux.";
    return 2;
#endif
    if (arguments.size() != 3) {
        qCritical().noquote() << "Usage: qsanguosha_battle_statistics_tests --statistics-ui-fixture <new-absolute-directory>";
        return 2;
    }
    QString error;
    if (!generateStatisticsUiFixture(arguments.at(2), &error)) {
        qCritical().noquote() << "Could not create synthetic statistics fixture:" << error;
        return 2;
    }
    return 0;
}

static void numericProjectionAndControlIsolation()
{
    const Match match = makeMatch(QStringLiteral("numeric"), 1);
    const QVariantList rows = BattleStatistics::analyze(match);
    CHECK(rows.size() == 2);
    const QVariantMap human = rowFor(rows, QStringLiteral("p1"));
    const QVariantMap opponent = rowFor(rows, QStringLiteral("p2"));
    CHECK(human.value(QStringLiteral("dataset")) == QStringLiteral("human"));
    CHECK(opponent.value(QStringLiteral("dataset")) == QStringLiteral("human_ai_opponent"));
    const QVariantMap humanMetrics = human.value(QStringLiteral("metrics")).toMap();
    CHECK(humanMetrics.value(QStringLiteral("damage")).toDouble() == 3.0);
    CHECK(humanMetrics.value(QStringLiteral("hp_damage")).toDouble() == 2.0);
    CHECK(humanMetrics.value(QStringLiteral("armor_damage")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("received_damage")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("recovery")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("support_recovery")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("card_uses")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("slash_uses")).toDouble() == 1.0);
    CHECK(humanMetrics.value(QStringLiteral("slash_damage")).toDouble() == 3.0);
    CHECK(humanMetrics.value(QStringLiteral("completed_turns")).toDouble() == 2.0);
    const QVariantMap aiMetrics = opponent.value(QStringLiteral("metrics")).toMap();
    CHECK(aiMetrics.value(QStringLiteral("damage")).toDouble() == 1.0);
    CHECK(aiMetrics.value(QStringLiteral("aoe_damage")).toDouble() == 1.0);
    CHECK(aiMetrics.value(QStringLiteral("aoe_uses")).toDouble() == 1.0);

    FixtureOptions allRobot;
    allRobot.p1Control = QStringLiteral("robot");
    allRobot.p2Control = QStringLiteral("robot");
    const auto selfPlayRows = BattleStatistics::analyze(makeMatch(QStringLiteral("selfplay"), 1, allRobot));
    CHECK(rowFor(selfPlayRows, QStringLiteral("p1")).value(QStringLiteral("dataset")) == QStringLiteral("ai_self_play"));
    CHECK(rowFor(selfPlayRows, QStringLiteral("p2")).value(QStringLiteral("dataset")) == QStringLiteral("ai_self_play"));
}

static void terminalUnwindKeepsCommittedDamage()
{
    Match match = makeMatch(QStringLiteral("terminal-unwind"), 1);
    ResolutionHistoryService history;
    history.beginRound();
    try {
        ResolutionHistoryEventGuard turn(history, QStringLiteral("turn"),
            {{QStringLiteral("player"), QStringLiteral("p1")}, {QStringLiteral("extra_turn"), false}});
        ResolutionHistoryEventGuard damage(history, QStringLiteral("damage"));
        CHECK(turn.id() > 0 && damage.id() > 0);
        // Armor commits first; the damage fact precedes the final HP component,
        // as it can during terminal rule unwinding. The checkpoint is taken
        // only after both committed components and both guards have closed.
        CHECK(history.appendFact(damage.id(), QStringLiteral("damage_component"),
            {{QStringLiteral("component"), QStringLiteral("armor")}, {QStringLiteral("amount"), 1}}) > 0);
        CHECK(history.appendFact(damage.id(), QStringLiteral("actual_damage"),
            {{QStringLiteral("from"), QStringLiteral("p1")}, {QStringLiteral("to"), QStringLiteral("p2")},
             {QStringLiteral("hp_loss"), 0}, {QStringLiteral("absorbed"), 1},
             {QStringLiteral("card"), QVariantMap{{QStringLiteral("classes"), QVariantList{QStringLiteral("Slash")}}}}}) > 0);
        CHECK(history.appendFact(damage.id(), QStringLiteral("damage_component"),
            {{QStringLiteral("component"), QStringLiteral("hp")}, {QStringLiteral("amount"), 2}}) > 0);
        throw std::runtime_error("terminal resolution");
    } catch (const std::runtime_error &) {
    }
    history.endRound();
    match.history = history.snapshot();
    CHECK(match.history.isComplete());
    const QVariantList rows = BattleStatistics::analyze(match);
    const QVariantMap human = rowFor(rows, QStringLiteral("p1"));
    const QVariantMap metrics = human.value(QStringLiteral("metrics")).toMap();
    CHECK(metrics.value(QStringLiteral("damage")).toDouble() == 3.0);
    CHECK(metrics.value(QStringLiteral("hp_damage")).toDouble() == 2.0);
    CHECK(metrics.value(QStringLiteral("armor_damage")).toDouble() == 1.0);
    CHECK(metrics.value(QStringLiteral("slash_damage")).toDouble() == 3.0);
    CHECK(metrics.value(QStringLiteral("completed_turns")).toDouble() == 0.0);
    CHECK(metrics.value(QStringLiteral("truncated_turns")).toDouble() == 1.0);
    const QVariantMap turn = human.value(QStringLiteral("turns")).toList().first().toMap();
    CHECK(turn.value(QStringLiteral("truncated")).toBool());
}

static void survivorConditionalDevelopmentTrend()
{
    const Match match = makeDevelopmentMatch(QStringLiteral("one-growth"), 1,
        {1, 2, 3}, {3, 4, 5}, {0, 1, 2});
    const QVariantMap row = rowFor(BattleStatistics::analyze(match), QStringLiteral("p1"));
    const QVariantMap development = row.value(QStringLiteral("development")).toMap();
    CHECK(development.value(QStringLiteral("covered")).toBool());
    CHECK(development.value(QStringLiteral("cycles")).toInt() == 3);
    CHECK(qAbs(development.value(QStringLiteral("hand_slope")).toDouble() - 1.0) < 0.000001);
    CHECK(qAbs(development.value(QStringLiteral("maxhp_slope")).toDouble() - 1.0) < 0.000001);
    CHECK(qAbs(development.value(QStringLiteral("damage_slope")).toDouble() - 1.0) < 0.000001);
    CHECK(development.value(QStringLiteral("survivor_only")).toBool());

    const Match survivorOnly = makeDevelopmentMatch(QStringLiteral("survivor-only"), 1,
        {1, 2, 3}, {3, 4, 5}, {0, 1, 2}, 2);
    const QVariantMap survivor = rowFor(BattleStatistics::analyze(survivorOnly), QStringLiteral("p1"))
        .value(QStringLiteral("development")).toMap();
    CHECK(!survivor.value(QStringLiteral("covered")).toBool());
    CHECK(survivor.value(QStringLiteral("cycles")).toInt() == 2);
    CHECK(survivor.value(QStringLiteral("survivor_only")).toBool());

    const Match missingBoundary = makeDevelopmentMatch(QStringLiteral("missing-boundary"), 1,
        {1, 2, 3}, {3, 4, 5}, {0, 1, 2}, -1, 2);
    const QVariantMap incomplete = rowFor(BattleStatistics::analyze(missingBoundary), QStringLiteral("p1"))
        .value(QStringLiteral("development")).toMap();
    CHECK(!incomplete.value(QStringLiteral("covered")).toBool());
    CHECK(incomplete.value(QStringLiteral("cycles")).toInt() == 2);

    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("development.sqlite"));
    for (int i = 0; i < 5; ++i)
        saveMatch(makeDevelopmentMatch(QStringLiteral("growth-%1").arg(i), 1,
            {1, 2, 3}, {3, 3, 3}, {0, 1, 2}), path);
    for (int i = 0; i < 5; ++i)
        saveMatch(makeDevelopmentMatch(QStringLiteral("flat-%1").arg(i), 1,
            {2, 2, 2}, {3, 3, 3}, {0, 0, 0}, -1, -1, QStringLiteral("flat-hero")), path);
    auto growth = summaryFor(path, QStringLiteral("human"), QStringLiteral("growth-hero"));
    const QVariantMap growthTrend = growth.value(QStringLiteral("development")).toMap();
    CHECK(growthTrend.value(QStringLiteral("status")) == QStringLiteral("observed"));
    CHECK(growthTrend.value(QStringLiteral("games")).toInt() == 5);
    CHECK(growthTrend.value(QStringLiteral("cycles")).toInt() == 15);
    CHECK(qAbs(growthTrend.value(QStringLiteral("positive_fraction")).toDouble() - 1.0) < 0.000001);
    CHECK(growth.value(QStringLiteral("labels")).toList().contains(QString::fromUtf8("发育")));

    const auto flat = summaryFor(path, QStringLiteral("human"), QStringLiteral("flat-hero"));
    const QVariantMap flatTrend = flat.value(QStringLiteral("development")).toMap();
    CHECK(flatTrend.value(QStringLiteral("status")) == QStringLiteral("observed"));
    CHECK(qAbs(flatTrend.value(QStringLiteral("positive_fraction")).toDouble()) < 0.000001);
    CHECK(!flat.value(QStringLiteral("labels")).toList().contains(QString::fromUtf8("发育")));

    QTemporaryDir provisionalTemp;
    CHECK(provisionalTemp.isValid());
    const QString provisionalPath = provisionalTemp.filePath(QStringLiteral("development-provisional.sqlite"));
    for (int i = 0; i < 3; ++i)
        saveMatch(makeDevelopmentMatch(QStringLiteral("growth-small-%1").arg(i), 1,
            {1, 2, 3}, {3, 3, 3}, {0, 1, 2}), provisionalPath);
    const auto provisional = summaryFor(provisionalPath, QStringLiteral("human"), QStringLiteral("growth-hero"));
    CHECK(provisional.value(QStringLiteral("development")).toMap().value(QStringLiteral("status"))
          == QStringLiteral("provisional"));
    CHECK(!provisional.value(QStringLiteral("labels")).toList().contains(QString::fromUtf8("发育")));

    QTemporaryDir unknownTemp;
    CHECK(unknownTemp.isValid());
    const QString unknownPath = unknownTemp.filePath(QStringLiteral("development-unknown.sqlite"));
    saveMatch(missingBoundary, unknownPath);
    const auto unknown = summaryFor(unknownPath, QStringLiteral("human"), QStringLiteral("growth-hero"));
    CHECK(unknown.value(QStringLiteral("development")).toMap().value(QStringLiteral("status"))
          == QStringLiteral("unknown"));
    CHECK(!unknown.value(QStringLiteral("labels")).toList().contains(QString::fromUtf8("发育")));
}

static void exclusionsAndMixedTrust()
{
    for (const QString &reason : {QStringLiteral("scenario"), QStringLiteral("replay")}) {
        FixtureOptions options;
        options.excludedReason = reason;
        const auto rows = BattleStatistics::analyze(makeMatch(QStringLiteral("excluded-") + reason, 1, options));
        CHECK(rows.size() == 2);
        CHECK(rowFor(rows, QStringLiteral("p1")).value(QStringLiteral("dataset")) == QStringLiteral("excluded"));
        CHECK(rowFor(rows, QStringLiteral("p2")).value(QStringLiteral("dataset")) == QStringLiteral("excluded"));
    }

    FixtureOptions mixed;
    mixed.mixedControl = true;
    auto mixedRows = BattleStatistics::analyze(makeMatch(QStringLiteral("mixed-control"), 1, mixed));
    CHECK(rowFor(mixedRows, QStringLiteral("p1")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));
    CHECK(rowFor(mixedRows, QStringLiteral("p2")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));

    FixtureOptions external;
    external.p1Control = QStringLiteral("external");
    auto externalRows = BattleStatistics::analyze(makeMatch(QStringLiteral("external-control"), 1, external));
    CHECK(rowFor(externalRows, QStringLiteral("p1")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));
    CHECK(rowFor(externalRows, QStringLiteral("p2")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));

    FixtureOptions takeover;
    takeover.p1IdentityChanged = true;
    auto takeoverRows = BattleStatistics::analyze(makeMatch(QStringLiteral("takeover"), 1, takeover));
    CHECK(rowFor(takeoverRows, QStringLiteral("p1")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));
    CHECK(rowFor(takeoverRows, QStringLiteral("p2")).value(QStringLiteral("dataset")) == QStringLiteral("mixed"));
}

static void deduplicationAndGenerationFencing()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("generation.sqlite"));
    FixtureOptions high;
    high.slashHp = 3;
    high.slashArmor = 0;
    const Match first = makeMatch(QStringLiteral("same-root"), 10, high);
    saveMatch(first, path);
    saveMatch(first, path); // A repeated terminal frame must stay one observation.
    auto summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(!summary.isEmpty());
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 3.0);
    CHECK(summary.value(QStringLiteral("damage_max")).toDouble() == 3.0);

    FixtureOptions replacement;
    replacement.slashHp = 1;
    replacement.slashArmor = 0;
    saveMatch(makeMatch(QStringLiteral("same-root"), 11, replacement), path);
    summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 1.0);
    CHECK(summary.value(QStringLiteral("damage_max")).toDouble() == 1.0);

    FixtureOptions stale;
    stale.slashHp = 9;
    stale.slashArmor = 0;
    saveMatch(makeMatch(QStringLiteral("same-root"), 9, stale), path);
    summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 1.0);
    CHECK(summary.value(QStringLiteral("damage_max")).toDouble() == 1.0);

    // A higher-generation invalidation removes the old contribution and keeps
    // its fence. The matching final frame may then restore the match once.
    saveMatch(makeMatch(QStringLiteral("same-root"), 12, replacement), path, true);
    QString error;
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("human"), &error).isEmpty());
    CHECK(error.isEmpty());
    saveMatch(makeMatch(QStringLiteral("same-root"), 11, high), path);
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("human"), &error).isEmpty());
    CHECK(error.isEmpty());
    saveMatch(makeMatch(QStringLiteral("same-root"), 12, replacement), path);
    summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 1.0);

    QTemporaryDir duplicateSeatsTemp;
    CHECK(duplicateSeatsTemp.isValid());
    const QString duplicateSeatsPath = duplicateSeatsTemp.filePath(QStringLiteral("same-general.sqlite"));
    FixtureOptions duplicateSeats;
    duplicateSeats.p2Control = QStringLiteral("human");
    duplicateSeats.p2General = QStringLiteral("hero");
    duplicateSeats.p2Role = duplicateSeats.role;
    const Match twoSeats = makeMatch(QStringLiteral("two-same-general-seats"), 1, duplicateSeats);
    saveMatch(twoSeats, duplicateSeatsPath);
    saveMatch(twoSeats, duplicateSeatsPath);
    summary = summaryFor(duplicateSeatsPath, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("observations")).toInt() == 2);
    CHECK(summary.value(QStringLiteral("damage_mean")).toDouble() == 2.0);
    CHECK(summary.value(QStringLiteral("damage_max")).toDouble() == 2.0);
}

static void provisionalUnknownAndEnvironmentVariance()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("summaries.sqlite"));
    for (int i = 0; i < 3; ++i)
        saveMatch(makeMatch(QStringLiteral("small-%1").arg(i), 1), path);
    auto provisional = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(provisional.value(QStringLiteral("games")).toInt() == 3);
    CHECK(provisional.value(QStringLiteral("status")) == QStringLiteral("provisional"));
    CHECK(provisional.value(QStringLiteral("labels")).toList().isEmpty());

    FixtureOptions noHistory;
    noHistory.complete = false;
    noHistory.p1General = QStringLiteral("unknown-hero");
    saveMatch(makeMatch(QStringLiteral("unknown"), 1, noHistory), path);
    auto unknown = summaryFor(path, QStringLiteral("human"), QStringLiteral("unknown-hero"));
    CHECK(unknown.value(QStringLiteral("status")) == QStringLiteral("unknown"));
    CHECK(!unknown.value(QStringLiteral("coverage")).toBool());
    CHECK(unknown.value(QStringLiteral("labels")).toList().isEmpty());
    for (const QString &key : {QStringLiteral("damage_mean"), QStringLiteral("damage_variance"),
                               QStringLiteral("damage_max"), QStringLiteral("damage_p75"),
                               QStringLiteral("burst_frequency"), QStringLiteral("turn_damage_p75")}) {
        const QVariant value = unknown.value(key);
        CHECK(!value.isValid() || value.isNull());
    }
    CHECK(unknown.value(QStringLiteral("damage_mean")).isNull());
    CHECK(unknown.value(QStringLiteral("damage_variance")).isNull());
    CHECK(unknown.value(QStringLiteral("damage_max")).isNull());

    QTemporaryDir varianceTemp;
    CHECK(varianceTemp.isValid());
    const QString variancePath = varianceTemp.filePath(QStringLiteral("variance.sqlite"));
    for (int i = 1; i <= 5; ++i) {
        FixtureOptions options;
        options.slashHp = i;
        options.slashArmor = 0;
        saveMatch(makeMatch(QStringLiteral("variance-%1").arg(i), 1, options), variancePath);
    }
    FixtureOptions otherMode;
    otherMode.mode = QStringLiteral("04p");
    otherMode.slashHp = 20;
    otherMode.slashArmor = 0;
    saveMatch(makeMatch(QStringLiteral("variance-other-mode"), 1, otherMode), variancePath);
    QString error;
    CHECK(BattleStatistics::readSummaries(variancePath, QStringLiteral("human"), &error).size() == 2);
    CHECK(error.isEmpty());
    const auto observed = summaryFor(variancePath, QStringLiteral("human"), QStringLiteral("hero"), QStringLiteral("02p"));
    CHECK(observed.value(QStringLiteral("games")).toInt() == 5);
    CHECK(observed.value(QStringLiteral("status")) == QStringLiteral("observed"));
    CHECK(observed.value(QStringLiteral("damage_mean")).toDouble() == 3.0);
    CHECK(qAbs(observed.value(QStringLiteral("damage_variance")).toDouble() - 2.5) < 0.000001);
    CHECK(observed.value(QStringLiteral("damage_max")).toDouble() == 5.0);
    CHECK(observed.value(QStringLiteral("damage_p75")).toDouble() == 4.0);
    const auto separateEnvironment = summaryFor(variancePath, QStringLiteral("human"), QStringLiteral("hero"), QStringLiteral("04p"));
    CHECK(separateEnvironment.value(QStringLiteral("games")).toInt() == 1);
    CHECK(separateEnvironment.value(QStringLiteral("damage_mean")).toDouble() == 20.0);
}

static void failedInvalidationKeepsTheVisibilityFence()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("invalidation.sqlite"));
    FixtureOptions oldValues;
    oldValues.slashHp = 3;
    oldValues.slashArmor = 0;
    const QString root = QStringLiteral("invalidation-root");
    saveMatch(makeMatch(root, 20, oldValues), path);
    const quint64 afterInitialSave = BattleStatistics::projectionRevision(path);
    CHECK(afterInitialSave > 0);
    CHECK(summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"))
        .value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 3.0);

    QFileInfo fileInfo(path);
    const QFileDevice::Permissions originalPermissions = fileInfo.permissions();
    const QFileDevice::Permissions readOnly = QFileDevice::ReadOwner
        | QFileDevice::ReadGroup | QFileDevice::ReadOther;
    CHECK(QFile::setPermissions(path, readOnly));
    FixtureOptions newValues;
    newValues.slashHp = 1;
    newValues.slashArmor = 0;
    const Match invalidate = makeMatch(root, 21, newValues);
    QString error;
    CHECK(!BattleStatistics::save(invalidate, path, &error, true));
    CHECK(!error.isEmpty());
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 1);
    const quint64 afterFailedInvalidation = BattleStatistics::projectionRevision(path);
    CHECK(afterFailedInvalidation == afterInitialSave + 1);
    CHECK(QFile::setPermissions(path, originalPermissions | QFileDevice::WriteOwner));

    // The database is readable again, so an empty result proves the pending
    // generation fence filtered the stale persisted row rather than masking a
    // read failure caused by the temporary permission change.
    const QVariantList hidden = BattleStatistics::readSummaries(path, QStringLiteral("human"), &error);
    CHECK(error.isEmpty());
    CHECK(hidden.isEmpty());
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 1);

    // A late older terminal frame cannot republish the row while invalidation
    // is pending, even after the underlying file has become writable again.
    saveMatch(makeMatch(root, 20, oldValues), path);
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("human"), &error).isEmpty());
    CHECK(error.isEmpty());
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 1);
    CHECK(BattleStatistics::projectionRevision(path) == afterFailedInvalidation);

    saveMatch(invalidate, path, true);
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 0);
    const quint64 afterRetry = BattleStatistics::projectionRevision(path);
    CHECK(afterRetry == afterFailedInvalidation + 1);
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("human"), &error).isEmpty());
    CHECK(error.isEmpty());

    Match invalidArgument;
    invalidArgument.generation = 22;
    CHECK(!BattleStatistics::save(invalidArgument, path, &error, true));
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 0);
    CHECK(BattleStatistics::projectionRevision(path) == afterRetry);

    FixtureOptions finalValues;
    finalValues.slashHp = 5;
    finalValues.slashArmor = 0;
    saveMatch(makeMatch(root, 22, finalValues), path);
    const auto replacement = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(replacement.value(QStringLiteral("games")).toInt() == 1);
    CHECK(replacement.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 5.0);
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 0);
    CHECK(BattleStatistics::projectionRevision(path) == afterRetry + 1);
}

static QString timelineJournalPath(const QString &databasePath, const QString &root)
{
    const QByteArray hash = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(databasePath + QStringLiteral(".timeline")).filePath(QString::fromLatin1(hash) + QStringLiteral(".json"));
}

static void requireStatisticsChild(const QStringList &arguments)
{
    QString failure;
    if (!runStatisticsChild(arguments, &failure))
        qFatal("Statistics child failed (%s): %s", qPrintable(arguments.value(1)), qPrintable(failure));
}

static void durableRestoreFenceSurvivesProcessExit()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("restart.sqlite"));
    const QString root = QStringLiteral("restart-root");
    FixtureOptions oldValues; oldValues.slashHp = 3; oldValues.slashArmor = 0;
    saveMatch(makeMatch(root, 0, oldValues), path);
    const QFileDevice::Permissions originalDatabasePermissions = QFileInfo(path).permissions();

    // The child arms the old terminal root, makes only the SQLite file
    // read-only, persists generation 1's restore journal, queues its DB write,
    // then exits without C++ cleanup or draining the worker pool.
    requireStatisticsChild({QStringLiteral("--statistics-dirty-crash"), path, root});
    requireStatisticsChild({QStringLiteral("--statistics-read-hidden"), path});
    CHECK(QFile::setPermissions(path, originalDatabasePermissions | QFileDevice::WriteOwner));

    // The new process has the authoritative restored timeline payload, so the
    // explicit recovery seam can replace the stale DB projection and seal its
    // marker. A later process must see only the replacement metrics.
    FixtureOptions currentValues; currentValues.slashHp = 5; currentValues.slashArmor = 0;
    const Match current = makeMatch(root, 1, currentValues);
    QString error;
    CHECK(BattleStatistics::recoverMatch(current, path, &error));
    CHECK(error.isEmpty());
    CHECK(BattleStatistics::pendingTimelineInvalidations(path) == 0);
    requireStatisticsChild({QStringLiteral("--statistics-read-current"), path, QStringLiteral("5")});
}

static void sameGenerationRecoveryRotatesPublicationToken()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("same-generation-recovery.sqlite"));
    const QString root = QStringLiteral("same-generation-recovery-root");
    FixtureOptions oldValues; oldValues.slashHp = 3; oldValues.slashArmor = 0;
    saveMatch(makeMatch(root, 4, oldValues), path);
    const QVariantMap oldMetadata = matchMetadata(path, root);
    const QString oldPublication = oldMetadata.value(QStringLiteral("statistics_publication")).toString();
    CHECK(!oldPublication.isEmpty());

    FixtureOptions authoritativeValues; authoritativeValues.slashHp = 5; authoritativeValues.slashArmor = 0;
    const Match authoritative = makeMatch(root, 4, authoritativeValues);
    QString error;
    CHECK(BattleStatistics::recoverMatch(authoritative, path, &error));
    CHECK(error.isEmpty());
    const QVariantMap authoritativeMetadata = matchMetadata(path, root);
    const QString currentPublication = authoritativeMetadata.value(QStringLiteral("statistics_publication")).toString();
    CHECK(!currentPublication.isEmpty() && currentPublication != oldPublication);
    auto summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 5.0);

    // Reinstalling the prior DB token models a stale metadata snapshot racing
    // with the sidecar publication. Readers must reject the whole root even
    // though generation and branch still match.
    QVariantMap staleMetadata = authoritativeMetadata;
    staleMetadata[QStringLiteral("statistics_publication")] = oldPublication;
    replaceMatchMetadata(path, root, staleMetadata);
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("human"), &error).isEmpty());
    CHECK(!error.isEmpty());

    replaceMatchMetadata(path, root, authoritativeMetadata);
    summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 5.0);
}

static void activeMarkerSurvivesFailedSidecarWrite()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("unwritable-timeline.sqlite"));
    const QString root = QStringLiteral("unwritable-sidecar-root");
    FixtureOptions oldValues; oldValues.slashHp = 3; oldValues.slashArmor = 0;
    saveMatch(makeMatch(root, 0, oldValues), path);
    const QFileDevice::Permissions originalDatabasePermissions = QFileInfo(path).permissions();
    const QString timelineDirectory = path + QStringLiteral(".timeline");
    const QFileDevice::Permissions originalDirectoryPermissions = QFileInfo(timelineDirectory).permissions();

    // Both durable write paths are unavailable during restore. notify returns
    // false, but the already-persisted active marker survives the crash and
    // forces a fresh reader to quarantine the old terminal aggregate.
    requireStatisticsChild({QStringLiteral("--statistics-sidecar-crash"), path, root});
    CHECK(QFile::setPermissions(path, originalDatabasePermissions | QFileDevice::WriteOwner));
    CHECK(QFile::setPermissions(timelineDirectory, originalDirectoryPermissions
        | QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther));
    requireStatisticsChild({QStringLiteral("--statistics-read-hidden"), path});
}

static void corruptMissingAndUnwritableJournalsFailClosed()
{
    QTemporaryDir corruptTemp;
    CHECK(corruptTemp.isValid());
    const QString corruptPath = corruptTemp.filePath(QStringLiteral("corrupt.sqlite"));
    const QString corruptRoot = QStringLiteral("corrupt-root");
    FixtureOptions values; values.slashHp = 3; values.slashArmor = 0;
    const Match original = makeMatch(corruptRoot, 0, values);
    saveMatch(original, corruptPath);
    QFile corruptJournal(timelineJournalPath(corruptPath, corruptRoot));
    CHECK(corruptJournal.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(corruptJournal.write(QByteArrayLiteral("not-json")) == 8);
    corruptJournal.close();
    requireStatisticsChild({QStringLiteral("--statistics-read-hidden"), corruptPath});
    QString error;
    CHECK(!BattleStatistics::recoverMatch(original, corruptPath, &error));
    CHECK(!error.isEmpty());

    QTemporaryDir missingTemp;
    CHECK(missingTemp.isValid());
    const QString missingPath = missingTemp.filePath(QStringLiteral("missing.sqlite"));
    const QString missingRoot = QStringLiteral("missing-root");
    const Match missingOriginal = makeMatch(missingRoot, 0, values);
    saveMatch(missingOriginal, missingPath);
    CHECK(QFile::remove(timelineJournalPath(missingPath, missingRoot)));
    CHECK(!BattleStatistics::armMatch(missingOriginal, missingPath, &error));
    CHECK(!error.isEmpty());
    CHECK(!BattleStatistics::save(missingOriginal, missingPath, &error));
    CHECK(!error.isEmpty());
    requireStatisticsChild({QStringLiteral("--statistics-read-hidden"), missingPath});

    QTemporaryDir unwritableTemp;
    CHECK(unwritableTemp.isValid());
    const QString unwritablePath = unwritableTemp.filePath(QStringLiteral("arm-failure.sqlite"));
    saveMatch(makeMatch(QStringLiteral("kept-root"), 0, values), unwritablePath);
    const QString timelineDirectory = unwritablePath + QStringLiteral(".timeline");
    const QFileDevice::Permissions originalDirectoryPermissions = QFileInfo(timelineDirectory).permissions();
    const QFileDevice::Permissions readExecute = QFileDevice::ReadOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    CHECK(QFile::setPermissions(timelineDirectory, readExecute));
    const Match unarmed = makeMatch(QStringLiteral("never-armed-root"), 0, values);
    CHECK(!BattleStatistics::armMatch(unarmed, unwritablePath, &error));
    CHECK(!error.isEmpty());
    CHECK(!BattleStatistics::save(unarmed, unwritablePath, &error));
    CHECK(!error.isEmpty());
    CHECK(QFile::setPermissions(timelineDirectory, originalDirectoryPermissions
        | QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther));
    const auto oldOnly = summaryFor(unwritablePath, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(oldOnly.value(QStringLiteral("games")).toInt() == 1);
    CHECK(oldOnly.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 3.0);
    requireStatisticsChild({QStringLiteral("--statistics-read-current"), unwritablePath, QStringLiteral("3")});
}

static void restoreNotificationDoesNotWaitForSqliteLock()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("notification-lock.sqlite"));
    const QString root = QStringLiteral("notification-lock-root");
    FixtureOptions oldValues; oldValues.slashHp = 3; oldValues.slashArmor = 0;
    const Match oldMatch = makeMatch(root, 0, oldValues);
    saveMatch(oldMatch, path);
    QString error;
    CHECK(BattleStatistics::armMatch(oldMatch, path, &error));
    CHECK(error.isEmpty());
    BattleStatistics::waitForPendingWrites();

    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    blocker.setDatabaseName(path);
    CHECK(blocker.open());
    QSqlQuery lock(blocker);
    CHECK(lock.exec(QStringLiteral("BEGIN IMMEDIATE")));

    FixtureOptions nextValues; nextValues.slashHp = 1; nextValues.slashArmor = 0;
    const Match restored = makeMatch(root, 1, nextValues);
    std::promise<QPair<bool, QString>> notification;
    auto ready = notification.get_future();
    std::thread notifier([&] {
        QString notifyError;
        const bool result = BattleStatistics::notifyTimelineRestore(restored, path, &notifyError);
        notification.set_value(qMakePair(result, notifyError));
    });
    const bool returnedUnderLock = ready.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
    blocker.rollback();
    notifier.join();
    const auto result = ready.get();
    lock.finish();
    blocker.close();
    blocker = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    CHECK(returnedUnderLock);
    CHECK(result.first);
    CHECK(result.second.isEmpty());

    BattleStatistics::waitForPendingWrites();
    saveMatch(restored, path);
    // A clean duplicate is not a new restore. Only the Room's exact pending
    // receipt may acknowledge that its background writer already caught up.
    CHECK(!BattleStatistics::notifyTimelineRestore(restored, path, &error));
    CHECK(BattleStatistics::notifyTimelineRestore(restored, path, &error, true));
    BattleStatistics::waitForPendingWrites();
    BattleStatistics::closeMatch(root, 1, path);
    BattleStatistics::waitForPendingWrites();
    const auto summary = summaryFor(path, QStringLiteral("human"), QStringLiteral("hero"));
    CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
    CHECK(summary.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("damage")).toDouble() == 1.0);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int childResult = runStatisticsChildMode(app.arguments());
    if (childResult >= 0)
        return childResult;
    const int fixtureResult = runStatisticsUiFixtureMode(app.arguments());
    if (fixtureResult >= 0)
        return fixtureResult;
    numericProjectionAndControlIsolation();
    terminalUnwindKeepsCommittedDamage();
    exclusionsAndMixedTrust();
    deduplicationAndGenerationFencing();
    provisionalUnknownAndEnvironmentVariance();
    survivorConditionalDevelopmentTrend();
    failedInvalidationKeepsTheVisibilityFence();
    durableRestoreFenceSurvivesProcessExit();
    sameGenerationRecoveryRotatesPublicationToken();
    activeMarkerSurvivesFailedSidecarWrite();
    corruptMissingAndUnwritableJournalsFailClosed();
    restoreNotificationDoesNotWaitForSqliteLock();
    qInfo() << "PASS battle statistics: numeric projection, trust partitions, generation fencing, durable restart recovery, and sample summaries";
    return 0;
}
