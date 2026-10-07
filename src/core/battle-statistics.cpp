#include "battle-statistics.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThreadPool>
#include <QRunnable>
#include <QUuid>
#include <QDebug>
#include <QSet>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QFile>
#include <QLockFile>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <limits>

namespace BattleStatistics {
namespace {
QMutex fenceMutex;
QMap<QString,QMap<QString,quint64>> pendingFences;
QMap<QString,quint64> projectionRevisions;
QString normalizedPath(const QString &path) {
    return QDir::cleanPath(QFileInfo(path.isEmpty()?defaultDatabasePath():path).absoluteFilePath());
}
void registerFence(const QString &path,const QString &root,quint64 generation) {
    QMutexLocker lock(&fenceMutex);const QString key=normalizedPath(path);auto &roots=pendingFences[key];
    if(!roots.contains(root)||roots.value(root)<generation) {roots[root]=generation;++projectionRevisions[key];}
}
bool belowPendingFence(const QString &path,const QString &root,quint64 generation) {
    QMutexLocker lock(&fenceMutex);const auto roots=pendingFences.value(normalizedPath(path));
    return roots.contains(root)&&generation<roots.value(root);
}
void durableFence(const QString &path,const QString &root,quint64 generation) {
    QMutexLocker lock(&fenceMutex);const QString key=normalizedPath(path);
    auto pathIt=pendingFences.find(key);if(pathIt==pendingFences.end())return;
    auto it=pathIt->find(root);if(it!=pathIt->end()&&it.value()<=generation)pathIt->erase(it);
    if(pathIt->isEmpty())pendingFences.erase(pathIt);
}
struct TimelineJournal {
    QString root,branch,owner;
    quint64 generation=0;
    bool active=false,dirty=false;
    QString publication;
};
QMap<QString,QMap<QString,TimelineJournal>> knownJournals;
QMap<QString,QSet<QString>> armedRoots,uncertainHashes,blockedRoots;
QSet<QString> scannedPaths;
QString processOwner() { static const QString owner=QUuid::createUuid().toString(QUuid::WithoutBraces);return owner; }
QString digest(const QString &value) { return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(),QCryptographicHash::Sha256).toHex()); }
QString journalDirectory(const QString &path) { return normalizedPath(path)+QStringLiteral(".timeline"); }
QString journalFile(const QString &path,const QString &root) { return QDir(journalDirectory(path)).filePath(digest(root)+QStringLiteral(".json")); }
bool validMatch(const Match &match,QString *error) {
    if(match.rootMatchId.isEmpty()||match.rootMatchId.size()>256||match.branchId.isEmpty()||match.branchId.size()>256
        ||match.generation>quint64(std::numeric_limits<qint64>::max())) {
        if(error)*error="Invalid root match ID, branch or generation";return false;
    }
    return true;
}
void uncertain(const QString &path,const QString &root) {
    QMutexLocker lock(&fenceMutex);const QString key=normalizedPath(path);
    if(!uncertainHashes[key].contains(digest(root))) {uncertainHashes[key].insert(digest(root));++projectionRevisions[key];}
}
void cacheJournal(const QString &path,const TimelineJournal &journal) {
    QMutexLocker lock(&fenceMutex);const QString key=normalizedPath(path);
    knownJournals[key][journal.root]=journal;uncertainHashes[key].remove(digest(journal.root));
}
bool locallyArmed(const QString &path,const QString &root) {
    QMutexLocker lock(&fenceMutex);return armedRoots.value(normalizedPath(path)).contains(root);
}
bool blocked(const QString &path,const QString &root) {
    QMutexLocker lock(&fenceMutex);return blockedRoots.value(normalizedPath(path)).contains(root);
}
bool readJournalFile(const QString &path,const QString &fileName,TimelineJournal *journal,QString *error) {
    QFile file(fileName);QJsonParseError parse;
    if(QFileInfo(fileName).isSymLink()||!file.open(QIODevice::ReadOnly)||file.size()>16384) {
        if(error)*error="Missing or unreadable timeline journal";return false;
    }
    const QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&parse);
    const QJsonObject object=doc.object();
    const QString root=object.value("root").toString(),branch=object.value("branch").toString(),
        text=object.value("generation").toString(),owner=object.value("owner").toString(),publication=object.value("publication").toString();
    bool numberValid=false;const quint64 generation=text.toULongLong(&numberValid);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||object.size()!=9
        ||object.value("version").toDouble()!=1||!object.value("version").isDouble()
        ||!object.value("root").isString()||root.isEmpty()||root.size()>256
        ||!object.value("branch").isString()||branch.isEmpty()||branch.size()>256
        ||!object.value("generation").isString()||!numberValid||QString::number(generation)!=text
        ||generation>quint64(std::numeric_limits<qint64>::max())
        ||!object.value("owner").isString()||QUuid(owner).isNull()||QUuid(owner).toString(QUuid::WithoutBraces)!=owner
        ||!object.value("publication").isString()||QUuid(publication).isNull()||QUuid(publication).toString(QUuid::WithoutBraces)!=publication
        ||!object.value("database").isString()||object.value("database").toString()!=digest(normalizedPath(path))
        ||!object.value("active").isBool()||!object.value("dirty").isBool()
        ||QDir::cleanPath(fileName)!=journalFile(path,root)) {
        if(error)*error="Corrupt or mismatched timeline journal";return false;
    }
    *journal={root,branch,owner,generation,object.value("active").toBool(),object.value("dirty").toBool(),publication};
    cacheJournal(path,*journal);return true;
}
bool readJournal(const QString &path,const QString &root,TimelineJournal *journal,QString *error) {
    if(!readJournalFile(path,journalFile(path,root),journal,error)||journal->root!=root) {uncertain(path,root);return false;}
    return true;
}
bool writeJournal(const QString &path,const TimelineJournal &journal,QString *error) {
    QSaveFile file(journalFile(path,journal.root));file.setDirectWriteFallback(false);
    const QJsonObject object{{"version",1},{"root",journal.root},{"branch",journal.branch},
        {"generation",QString::number(journal.generation)},{"owner",journal.owner},
        {"database",digest(normalizedPath(path))},{"active",journal.active},{"dirty",journal.dirty},{"publication",journal.publication}};
    const QByteArray bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) {
        if(error)*error="Cannot atomically persist timeline journal: "+file.errorString();return false;
    }
    cacheJournal(path,journal);return true;
}
void scanJournals(const QString &path) {
    const QString key=normalizedPath(path);
    { QMutexLocker lock(&fenceMutex);if(scannedPaths.contains(key))return;scannedPaths.insert(key); }
    QDir directory(journalDirectory(path));
    for(const QString &name:directory.entryList(QStringList{"*.json"},QDir::Files)) {
        TimelineJournal journal;QString error;
        if(!readJournalFile(path,directory.filePath(name),&journal,&error)) {
            QMutexLocker lock(&fenceMutex);uncertainHashes[key].insert(QFileInfo(name).completeBaseName());
        }
    }
}
bool foreignActive(const QString &path,const TimelineJournal &journal) {
    return journal.active&&(journal.owner!=processOwner()||!locallyArmed(path,journal.root));
}
QByteArray json(const QVariant &v) { return QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact); }
QVariant decoded(const QVariant &v) { return QJsonDocument::fromJson(v.toByteArray()).toVariant(); }
void add(QVariantMap &m, const QString &k, double v) { m[k] = m.value(k).toDouble() + v; }
double slope(const QList<double> &values,const QList<double> &positions) {
    if(values.size()<2)return 0;
    double center=0;for(double position:positions)center+=position;center/=positions.size();
    double numerator=0,denominator=0;
    for(int i=0;i<values.size();++i) { const double x=positions.at(i)-center; numerator+=x*values.at(i);denominator+=x*x; }
    return numerator/denominator;
}
bool growing(double hand,double maxhp,double damage) {
    return ((hand>=.25 || maxhp>=.2) && damage>=0)
        || (damage>=.5 && hand>=0 && maxhp>=0);
}
bool cardClass(const QVariantMap &d, const char *name) {
    const auto c = d.value("card").toMap();
    return c.value("classes").toList().contains(QString::fromLatin1(name))
        || c.value("class_name", c.value("class")).toString() == QLatin1String(name);
}
QString datasetFor(const Match &m, const QVariantMap &p) {
    if (!m.metadata.value("excluded_reason").toString().isEmpty() || !m.metadata.value("terminal").toBool()) return "excluded";
    if (m.metadata.value("mixed_control").toBool() || p.value("identity_changed").toBool()) return "mixed";
    bool allRobot = true;
    for (const auto &v : m.metadata.value("participants").toList()) {
        if (v.toMap().value("identity_changed").toBool()) return "mixed";
        const QString c = v.toMap().value("control").toString();
        QString firstSource;
        for(const auto &entry:v.toMap().value("control_history").toList()) {
            QString source=entry.toString();if(source=="online")source="human";
            if(source!="human"&&source!="robot")return "mixed";
            if(!firstSource.isEmpty()&&firstSource!=source)return "mixed";
            firstSource=source;
        }
        if(!firstSource.isEmpty()&&firstSource!=c)return "mixed";
        if (c != "robot") allRobot = false;
        if (c != "human" && c != "robot") return "mixed";
    }
    return allRobot ? "ai_self_play" : p.value("control") == "human" ? "human" : "human_ai_opponent";
}
struct Database {
    QString name = QUuid::createUuid().toString();
    QSqlDatabase db;
    Database(const QString &path,bool readOnly=false,int timeout=100) : db(QSqlDatabase::addDatabase("QSQLITE", name)) {
        db.setDatabaseName(path); db.setConnectOptions(QString("QSQLITE_BUSY_TIMEOUT=%1").arg(timeout)+(readOnly?";QSQLITE_OPEN_READONLY":""));
    }
    ~Database() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
};
bool schema(QSqlDatabase &db, QString *error) {
    const QStringList statements{
        "CREATE TABLE IF NOT EXISTS battle_matches (root TEXT PRIMARY KEY, generation INTEGER NOT NULL, branch TEXT NOT NULL, terminal INTEGER NOT NULL, schema_version INTEGER NOT NULL, analysis_version INTEGER NOT NULL, metadata BLOB NOT NULL, history BLOB NOT NULL)",
        "CREATE TABLE IF NOT EXISTS battle_participants (root TEXT NOT NULL, player TEXT NOT NULL, dataset TEXT NOT NULL, environment TEXT NOT NULL, general TEXT NOT NULL, general2 TEXT NOT NULL, summary BLOB NOT NULL, PRIMARY KEY(root, player))",
        "CREATE INDEX IF NOT EXISTS battle_dataset ON battle_participants(dataset,environment,general,general2)",
        "CREATE TABLE IF NOT EXISTS battle_turns (root TEXT NOT NULL, player TEXT NOT NULL, turn TEXT NOT NULL, summary BLOB NOT NULL, PRIMARY KEY(root,player,turn))"};
    for (const auto &sql : statements) { QSqlQuery q(db); if (!q.exec(sql)) { if(error)*error=q.lastError().text(); return false; } }
    return true;
}
class WriterPool : public QThreadPool {
public:
    WriterPool() {
        setMaxThreadCount(1);
        if (QCoreApplication::instance())
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                QCoreApplication::instance(), [this]() { waitForDone(); }, Qt::DirectConnection);
    }
    ~WriterPool() { waitForDone(); }
};
WriterPool &pool() { static WriterPool value; return value; }
class WriteJob : public QRunnable {
public:
    Match match; bool invalidation;QString path;
    WriteJob(const Match &m, bool i,const QString &p=QString()) : match(m), invalidation(i),path(p.isEmpty()?defaultDatabasePath():p) {}
    void run() override {
        QString error;
        if (!save(match, path, &error, invalidation))
            qWarning().noquote() << "Battle statistics save failed:" << error;
    }
};
}

QVariantList analyze(const Match &match)
{
    const QVariantMap history = match.history.serialize();
    const QVariantList events = history.value("events").toList(), facts = history.value("facts").toList();
    QMap<QString, QVariantMap> turns;
    bool hasExtraTurns = false;
    for (const auto &v : events) {
        const auto e = v.toMap();
        if (e.value("kind") == "turn") {
            const auto d = e.value("data").toMap();
            const bool extra = d.value("extra_turn").toBool(); hasExtraTurns |= extra;
            const bool completed = e.value("outcome") == "completed" && e.value("status") == "finished";
            turns[e.value("id").toString()] = {{"turn", e.value("id")}, {"player", d.value("player")},
                {"extra", extra}, {"truncated", !completed}, {"damage", 0.0}, {"slash_uses", 0.0}, {"card_uses", 0.0}};
        }
    }
    // Actual damage is committed once at the first mutation. Armor and HP
    // components may be appended later, including during terminal unwinding.
    QMap<QString, QVariantMap> damageParts;
    for (const auto &v : facts) {
        const auto f=v.toMap(); if (f.value("kind") != "damage_component") continue;
        const auto d=f.value("data").toMap();
        add(damageParts[f.value("event_id").toString()], d.value("component").toString(), qMax(0,d.value("amount").toInt()));
    }
    const auto coverageKinds = history.value("coverage").toMap().value("facts").toList();
    bool complete = history.value("complete").toBool()
        && history.value("coverage").toMap().value("events").toList().contains(QString("turn"));
    for (const QString &kind : {QString("actual_damage"),QString("damage_component"),QString("use_card"),QString("actual_recover")})
        complete &= coverageKinds.contains(kind);
    for(const auto &v:facts) {
        const auto f=v.toMap();const auto card=f.value("data").toMap().value("card").toMap();
        if((f.value("kind")=="use_card" || (f.value("kind")=="actual_damage"&&!card.isEmpty()))
            && !card.contains("classes"))complete=false;
    }
    QVariantList result;
    for (const auto &participant : match.metadata.value("participants").toList()) {
        const auto p=participant.toMap(); const QString player=p.value("player").toString();
        QVariantMap metrics;
        for (const QString &key : QStringList{"damage","hp_damage","armor_damage","received_damage","recovery","support_recovery","card_uses","slash_uses","slash_damage","aoe_damage","aoe_uses","completed_turns","extra_turns","truncated_turns"}) metrics[key]=0.0;
        auto playerTurns = turns;
        bool resourceKnown=false;
        bool resourceValid=complete && coverageKinds.contains(QString("player_state"))
            && coverageKinds.contains(QString("turn_hp_snapshot"));
        int hand=0,maxhp=0;
        QSet<QString> seen; // sequence identity is scoped to this effective generation only.
        for (const auto &v : facts) {
            const auto f=v.toMap(), d=f.value("data").toMap();
            const QString sequence=f.value("sequence").toString();
            if (!sequence.isEmpty() && seen.contains(sequence)) continue;
            if (!sequence.isEmpty()) seen.insert(sequence);
            const QString kind=f.value("kind").toString(), from=d.value("from").toString(), to=d.value("to").toString();
            auto turn=playerTurns.find(f.value("turn_id").toString());
            const bool ownTurn=turn!=playerTurns.end() && turn->value("player").toString()==player;
            if(kind=="player_state" && d.value("player").toString()==player) {
                const bool fields=d.contains("hand_count_after")&&d.contains("maxhp_after");
                if(d.value("boundary")=="baseline") {
                    if(resourceKnown)resourceValid=false;
                    resourceKnown=fields;
                } else if(!resourceKnown || !fields || !d.contains("hand_count_before") || !d.contains("maxhp_before")
                    || d.value("hand_count_before").toInt()!=hand || d.value("maxhp_before").toInt()!=maxhp)resourceValid=false;
                hand=d.value("hand_count_after").toInt();maxhp=d.value("maxhp_after").toInt();
            }
            if(kind=="turn_hp_snapshot" && ownTurn && d.value("player").toString()==player) {
                const QString boundary=d.value("boundary").toString();
                if(boundary=="start" || boundary=="end") {
                    (*turn)["resource_"+boundary]=resourceKnown&&resourceValid;
                    (*turn)["hand_"+boundary]=hand;(*turn)["maxhp_"+boundary]=maxhp;
                    (*turn)["alive_"+boundary]=d.value("alive").toBool();
                }
            }
            if(kind=="actual_damage") {
                const auto parts=damageParts.value(f.value("event_id").toString());
                const double hp=parts.isEmpty()?d.value("hp_loss").toDouble():parts.value("hp").toDouble();
                const double armor=parts.isEmpty()?d.value("absorbed").toDouble():parts.value("armor").toDouble();
                const double amount=qMax(0.0,hp)+qMax(0.0,armor);
                if(from==player) {
                    add(metrics,"damage",amount); add(metrics,"hp_damage",hp); add(metrics,"armor_damage",armor);
                    if(cardClass(d,"Slash")) { add(metrics,"slash_damage",amount); if(ownTurn)add(*turn,"slash_damage",amount); }
                    if(cardClass(d,"AOE")) { add(metrics,"aoe_damage",amount); if(ownTurn)add(*turn,"aoe_damage",amount); }
                    if(ownTurn) add(*turn,"damage",amount);
                }
                if(to==player) add(metrics,"received_damage",amount);
            } else if(kind=="use_card" && from==player && !cardClass(d,"SkillCard")) {
                add(metrics,"card_uses",1); if(ownTurn)add(*turn,"card_uses",1);
                if(cardClass(d,"Slash")) { add(metrics,"slash_uses",1); if(ownTurn)add(*turn,"slash_uses",1); }
                if(cardClass(d,"AOE")) { add(metrics,"aoe_uses",1); if(ownTurn)add(*turn,"aoe_uses",1); }
            } else if(kind=="actual_recover" && from==player) {
                const double amount=qMax(0.0,d.value("amount").toDouble());
                add(metrics,"recovery",amount); if(to!=player) { add(metrics,"support_recovery",amount); if(ownTurn)add(*turn,"support_recovery",amount); }
            }
        }
        QVariantList turnRows;
        for (auto it=playerTurns.cbegin();it!=playerTurns.cend();++it) {
            if(it->value("player").toString()!=player)continue;
            turnRows << *it;
            if(it->value("extra").toBool())add(metrics,"extra_turns",1);
            else if(it->value("truncated").toBool())add(metrics,"truncated_turns",1);
            else add(metrics,"completed_turns",1);
        }
        std::sort(turnRows.begin(),turnRows.end(),[](const QVariant &a,const QVariant &b){return a.toMap().value("turn").toLongLong()<b.toMap().value("turn").toLongLong();});
        QList<double> hands,maxhps,outputs,cyclePositions;
        int cycleOrdinal=0;
        for(const auto &tv:turnRows) {
            const auto t=tv.toMap();if(t.value("extra").toBool())continue;
            const int ordinal=cycleOrdinal++;
            if(t.value("truncated").toBool())continue;
            if(!t.value("resource_start").toBool()||!t.value("resource_end").toBool()) {resourceValid=false;continue;}
            // Late-cycle observations are conditional on being alive at both
            // boundaries. This is descriptive growth, not a survival advantage.
            if(!t.value("alive_start").toBool()||!t.value("alive_end").toBool())continue;
            hands<<t.value("hand_end").toDouble();maxhps<<t.value("maxhp_end").toDouble();outputs<<t.value("damage").toDouble();cyclePositions<<ordinal;
        }
        const bool developmentCovered=resourceKnown&&resourceValid&&hands.size()>=3;
        const QVariantMap development{{"covered",developmentCovered},{"cycles",hands.size()},{"survivor_only",true},
            {"hand_slope",developmentCovered?QVariant(slope(hands,cyclePositions)):QVariant()},
            {"maxhp_slope",developmentCovered?QVariant(slope(maxhps,cyclePositions)):QVariant()},
            {"damage_slope",developmentCovered?QVariant(slope(outputs,cyclePositions)):QVariant()}};
        const QVariantMap environment{{"mode",match.metadata.value("mode")},{"player_count",match.metadata.value("player_count")},
            {"role",p.value("role")},{"rules_version",match.metadata.value("rules_version")},
            {"engine_version",match.metadata.value("engine_version")},{"hegemony",match.metadata.value("hegemony")},
            {"second_general_enabled",match.metadata.value("second_general_enabled")},{"extra_turns_present",hasExtraTurns},
            {"analysis_version",AnalysisVersion}};
        const QString dataset=datasetFor(match,p);
        QVariantMap row{{"player",player},{"general",p.value("general")},{"general2",p.value("general2")},
            {"dataset",dataset},{"environment",QString::fromUtf8(json(environment))},{"environment_details",environment},
            {"metrics",metrics},{"turns",turnRows},{"coverage",complete},{"development",development},{"control_history",p.value("control_history")}};
        result << row;
    }
    return result;
}

QString defaultDatabasePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("battle-statistics.sqlite");
}
static bool persistMatch(const Match &match,const QString &requestedPath,QString *error,bool invalidation,bool recovery)
{
    if(error)error->clear();
    const QString path=normalizedPath(requestedPath);
    if(!validMatch(match,error))return false;
    if(invalidation)registerFence(path,match.rootMatchId,match.generation);
    else if(belowPendingFence(path,match.rootMatchId,match.generation)) {
        if(recovery) {if(error)*error="Recovery is older than pending generation";return false;}
        return true;
    }
    if(!recovery&&blocked(path,match.rootMatchId)) {if(error)*error="Match was not safely armed; publication disabled";return false;}
    if(!QDir().mkpath(journalDirectory(path))) {if(error)*error="Cannot create timeline journal directory";return false;}
    QLockFile journalLock(journalFile(path,match.rootMatchId)+".lock");
    if(!journalLock.tryLock(0)) {if(error)*error="Timeline journal is busy; no waiting on game thread";return false;}
    TimelineJournal journal;
    const bool markerExists=QFileInfo::exists(journalFile(path,match.rootMatchId));
    if(markerExists&&!readJournal(path,match.rootMatchId,&journal,error))return false;
    if(recovery&&(!markerExists||!match.metadata.value("terminal").toBool()||!match.history.isComplete()
        ||match.metadata.value("participants").toList().isEmpty())) {
        if(error)*error="Recovery requires a valid journal and complete authoritative terminal Match";return false;
    }
    if(markerExists) {
        if(match.generation<journal.generation) {
            if(recovery) {if(error)*error="Recovery generation is older than durable journal";return false;}
            return true;
        }
        if(match.generation==journal.generation&&match.branchId!=journal.branch) {if(error)*error="Timeline branch conflicts at same generation";return false;}
        if(!recovery&&foreignActive(path,journal)) {if(error)*error="Uncertain active match from another lifecycle requires authoritative recovery";return false;}
    }
    auto prepareJournal=[&]() {
        if(recovery) {
            journal.active=true;journal.owner=processOwner();
            journal.publication=QUuid::createUuid().toString(QUuid::WithoutBraces);
            QMutexLocker lock(&fenceMutex);armedRoots[path].insert(match.rootMatchId);
        }
        journal.generation=match.generation;journal.branch=match.branchId;journal.dirty=true;
        return writeJournal(path,journal,error);
    };
    // Existing roots fence the independent file BEFORE even opening SQLite.
    // A read-only/unavailable database must not leave a clean stale journal.
    if(markerExists&&!prepareJournal())return false;
    Database connection(path); auto &db=connection.db;
    if(!db.open()){if(error)*error=db.lastError().text();return false;}
    if(!schema(db,error))return false;
    // A missing marker is not permission to bless a pre-existing DB row. The
    // synchronous helper only bootstraps roots that have never been published.
    if(!markerExists) {
        if(locallyArmed(path,match.rootMatchId)) {uncertain(path,match.rootMatchId);if(error)*error="Active match lost its pre-armed journal";return false;}
        QSqlQuery check(db);check.prepare("SELECT 1 FROM battle_matches WHERE root=?");check.addBindValue(match.rootMatchId);
        if(!check.exec()) {if(error)*error=check.lastError().text();return false;}
        if(check.next()) {uncertain(path,match.rootMatchId);if(error)*error="Existing match has no timeline journal; refusing implicit repair";return false;}
        journal={match.rootMatchId,match.branchId,processOwner(),match.generation,false,true,QUuid::createUuid().toString(QUuid::WithoutBraces)};
    }
    if(!markerExists&&!prepareJournal())return false;
    QSqlQuery begin(db); if(!begin.exec("BEGIN IMMEDIATE")){if(error)*error=begin.lastError().text();return false;}
    auto fail=[&](const QString &message){db.rollback();if(error)*error=message;return false;};
    QSqlQuery existing(db); existing.prepare("SELECT generation,terminal,branch,metadata FROM battle_matches WHERE root=?"); existing.addBindValue(match.rootMatchId);
    if(!existing.exec())return fail(existing.lastError().text());
    if(existing.next()) {
        const quint64 stored=existing.value(0).toULongLong();
        if(stored>match.generation) return fail("Database generation is newer than supplied Match; journal remains uncertain");
        if(stored==match.generation&&existing.value(2).toString()!=match.branchId)
            return fail("Database branch conflicts with supplied timeline");
        if(!recovery&&stored==match.generation&&decoded(existing.value(3)).toMap().value("statistics_publication").toString()!=journal.publication)
            return fail("Database publication does not match journal; authoritative recovery required");
        // Equal-generation invalidation cannot erase its already saved terminal.
        if(!recovery && stored==match.generation && (existing.value(1).toBool() || invalidation)) {
            db.rollback();
            if(stored>=journal.generation) {
                journal.dirty=false;
                if(!writeJournal(path,journal,error))return false;
                durableFence(path,match.rootMatchId,stored);
            }
            return true;
        }
    }
    existing.finish();
    for(const QString &table:QStringList{"battle_participants","battle_turns"}) {
        QSqlQuery q(db);q.prepare("DELETE FROM "+table+" WHERE root=?");q.addBindValue(match.rootMatchId);
        if(!q.exec())return fail(q.lastError().text());
    }
    QSqlQuery insert(db);insert.prepare("INSERT OR REPLACE INTO battle_matches VALUES(?,?,?,?,?,?,?,?)");
    insert.addBindValue(match.rootMatchId);insert.addBindValue(qlonglong(match.generation));insert.addBindValue(match.branchId.isNull()?QString(""):match.branchId);
    insert.addBindValue(!invalidation && match.metadata.value("terminal").toBool());insert.addBindValue(SchemaVersion);insert.addBindValue(AnalysisVersion);
    QVariantMap persistedMetadata=match.metadata;persistedMetadata["statistics_publication"]=journal.publication;
    insert.addBindValue(json(persistedMetadata));insert.addBindValue(invalidation?QByteArray("{}"):json(match.history.serialize()));
    if(!insert.exec())return fail(insert.lastError().text());
    if(!invalidation) for(const auto &v:analyze(match)) {
        const auto row=v.toMap();QSqlQuery q(db);q.prepare("INSERT INTO battle_participants VALUES(?,?,?,?,?,?,?)");
        for(const auto &value:QVariantList{match.rootMatchId,row.value("player"),row.value("dataset"),row.value("environment"),row.value("general").toString(),row.value("general2").toString(),json(row)})
            q.addBindValue(value.isNull() || (value.userType() == QMetaType::QString && value.toString().isNull())
                ? QVariant(QStringLiteral("")) : value);
        if(!q.exec())return fail(q.lastError().text());
        for(const auto &turn:row.value("turns").toList()) {
            QSqlQuery t(db);t.prepare("INSERT INTO battle_turns VALUES(?,?,?,?)");t.addBindValue(match.rootMatchId);t.addBindValue(row.value("player"));t.addBindValue(turn.toMap().value("turn"));t.addBindValue(json(turn));
            if(!t.exec())return fail(t.lastError().text());
        }
    }
    if(!db.commit())return fail(db.lastError().text());
    { QMutexLocker lock(&fenceMutex);++projectionRevisions[path]; }
    journal.dirty=false;
    if(recovery)journal.active=false;
    if(!writeJournal(path,journal,error))return false;
    if(recovery) {QMutexLocker lock(&fenceMutex);armedRoots[path].remove(match.rootMatchId);blockedRoots[path].remove(match.rootMatchId);}
    durableFence(path,match.rootMatchId,match.generation);return true;
}
bool save(const Match &match,const QString &path,QString *error,bool invalidation)
{
    return persistMatch(match,path,error,invalidation,false);
}
bool recoverMatch(const Match &match,const QString &path,QString *error)
{
    return persistMatch(match,path,error,false,true);
}

bool armMatch(const Match &match,const QString &requestedPath,QString *error)
{
    if(error)error->clear();if(!validMatch(match,error))return false;
    const QString path=normalizedPath(requestedPath);const bool alreadyArmed=locallyArmed(path,match.rootMatchId);
    auto fail=[&](const QString &message) {
        if(error)*error=message;
        if(!alreadyArmed) {QMutexLocker lock(&fenceMutex);blockedRoots[path].insert(match.rootMatchId);}
        uncertain(path,match.rootMatchId);return false;
    };
    if(blocked(path,match.rootMatchId))return fail("Previous arm failed; this root cannot publish");
    if(!QDir().mkpath(journalDirectory(path)))return fail("Cannot create pre-armed timeline directory");
    QLockFile journalLock(journalFile(path,match.rootMatchId)+".lock");
    if(!journalLock.tryLock(0))return fail("Timeline journal busy while arming match");
    TimelineJournal journal;
    if(QFileInfo::exists(journalFile(path,match.rootMatchId))) {
        QString detail;if(!readJournal(path,match.rootMatchId,&journal,&detail))return fail(detail);
        if(foreignActive(path,journal))return fail("Uncertain foreign active match requires authoritative recovery");
        if(journal.dirty||journal.generation!=match.generation||journal.branch!=match.branchId)
            return fail("Arming cannot change an existing timeline generation or repair dirty state");
        if(alreadyArmed&&journal.active)return true;
    } else {
        if(QFileInfo::exists(path)) {
            Database connection(path,true,0);auto &db=connection.db;
            if(!db.open())return fail(db.lastError().text());
            QSqlQuery table(db);
            if(!table.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='battle_matches'"))return fail(table.lastError().text());
            if(table.next()) {
                QSqlQuery existing(db);existing.prepare("SELECT 1 FROM battle_matches WHERE root=?");existing.addBindValue(match.rootMatchId);
                if(!existing.exec())return fail(existing.lastError().text());
                if(existing.next())return fail("Existing match has no timeline journal; refusing implicit repair");
            }
        }
        journal={match.rootMatchId,match.branchId,processOwner(),match.generation,true,false,QUuid::createUuid().toString(QUuid::WithoutBraces)};
    }
    journal.active=true;journal.owner=processOwner();
    QString detail;if(!writeJournal(path,journal,&detail))return fail(detail);
    {QMutexLocker lock(&fenceMutex);armedRoots[path].insert(match.rootMatchId);++projectionRevisions[path];}
    return true;
}
bool notifyTimelineRestore(const Match &match,const QString &requestedPath,QString *error)
{
    if(error)error->clear();if(!validMatch(match,error))return false;
    const QString path=normalizedPath(requestedPath);
    if(!locallyArmed(path,match.rootMatchId)||blocked(path,match.rootMatchId)) {
        if(error)*error="Restore notification requires a successfully pre-armed active match";return false;
    }
    {
        QMutexLocker lock(&fenceMutex);const auto journal=knownJournals.value(path).value(match.rootMatchId);
        if(match.generation<journal.generation||(match.generation==journal.generation&&match.branchId!=journal.branch)) {
            if(error)*error="Restore notification conflicts with the known timeline";return false;
        }
        const bool retry=pendingFences.value(path).contains(match.rootMatchId)||journal.dirty;
        if(match.generation==journal.generation&&!retry) {
            if(error)*error="A new restore must advance generation; clean same-generation notifications are rejected";return false;
        }
    }
    registerFence(path,match.rootMatchId,match.generation);
    const bool durable=[&]() {
        QLockFile journalLock(journalFile(path,match.rootMatchId)+".lock");
        if(!journalLock.tryLock(0)) {if(error)*error="Timeline journal busy; active marker retains uncertainty";return false;}
        TimelineJournal journal;if(!readJournal(path,match.rootMatchId,&journal,error))return false;
        if(!journal.active||journal.owner!=processOwner()||match.generation<journal.generation
            ||(match.generation==journal.generation&&match.branchId!=journal.branch)) {
            if(error)*error="Restore notification does not own the active timeline";return false;
        }
        journal.generation=match.generation;journal.branch=match.branchId;journal.dirty=true;
        return writeJournal(path,journal,error);
    }();
    // Every SQLite operation, including lock contention, stays off the caller.
    pool().start(new WriteJob(match,true,path));
    return durable;
}
static bool sealMatch(const QString &root,quint64 generation,const QString &path,QString *error)
{
    if(!locallyArmed(path,root))return true;
    QLockFile journalLock(journalFile(path,root)+".lock");
    if(!journalLock.tryLock(0)) {if(error)*error="Timeline journal busy during lifecycle seal";return false;}
    TimelineJournal journal;if(!readJournal(path,root,&journal,error))return false;
    if(journal.owner!=processOwner()||!journal.active||journal.generation!=generation||journal.dirty
        ||belowPendingFence(path,root,generation)||blocked(path,root)) {
        if(error)*error="Uncertain timeline retained active at lifecycle close";return false;
    }
    if(QFileInfo::exists(path)) {
        Database connection(path,true,0);auto &db=connection.db;
        if(!db.open()) {if(error)*error=db.lastError().text();return false;}
        QSqlQuery table(db);if(!table.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='battle_matches'")) {if(error)*error=table.lastError().text();return false;}
        if(table.next()) {
            QSqlQuery q(db);q.prepare("SELECT generation,branch,metadata FROM battle_matches WHERE root=?");q.addBindValue(root);
            if(!q.exec()) {if(error)*error=q.lastError().text();return false;}
            if(q.next()&&(q.value(0).toULongLong()!=generation||q.value(1).toString()!=journal.branch||decoded(q.value(2)).toMap().value("statistics_publication").toString()!=journal.publication)) {if(error)*error="Database does not match the journal; active marker retained";return false;}
        }
    }
    journal.active=false;
    if(!writeJournal(path,journal,error))return false;
    {QMutexLocker lock(&fenceMutex);armedRoots[normalizedPath(path)].remove(root);++projectionRevisions[normalizedPath(path)];}
    return true;
}
class CloseJob : public QRunnable {
public:
    QString root,path;quint64 generation;
    CloseJob(const QString &r,quint64 g,const QString &p):root(r),path(normalizedPath(p)),generation(g){}
    void run() override {QString error;if(!sealMatch(root,generation,path,&error))qWarning().noquote()<<"Battle statistics lifecycle remains uncertain:"<<error;}
};
void closeMatch(const QString &rootMatchId,quint64 generation,const QString &path)
{
    pool().start(new CloseJob(rootMatchId,generation,path));
}

void submit(const Match &match,bool invalidation) { pool().start(new WriteJob(match,invalidation)); }
void waitForPendingWrites() { pool().waitForDone(); }
quint64 projectionRevision(const QString &path) {
    QMutexLocker lock(&fenceMutex);return projectionRevisions.value(normalizedPath(path));
}
int pendingTimelineInvalidations(const QString &path) {
    QMutexLocker lock(&fenceMutex);const QString key=normalizedPath(path);
    QSet<QString> hashes=uncertainHashes.value(key);
    const auto fences=pendingFences.value(key);
    for(auto it=fences.cbegin();it!=fences.cend();++it)hashes.insert(digest(it.key()));
    const auto journals=knownJournals.value(key);
    for(const auto &journal:journals)
        if(journal.dirty||(journal.active&&(journal.owner!=processOwner()||!armedRoots.value(key).contains(journal.root))))hashes.insert(digest(journal.root));
    return hashes.size();
}

QVariantList readSummaries(const QString &path,const QString &dataset,QString *error)
{
    if(error)error->clear(); const QString actual=path.isEmpty()?defaultDatabasePath():path;
    scanJournals(actual);
    const quint64 readRevision=projectionRevision(actual);
    if(!QFileInfo::exists(actual))return {};
    Database connection(actual,true,0);auto &db=connection.db;
    if(!db.open()){if(error)*error=db.lastError().text();return {};}
    QSqlQuery q(db); q.prepare("SELECT p.root,m.generation,m.branch,m.metadata,p.summary FROM battle_participants p JOIN battle_matches m ON m.root=p.root"+(dataset.isEmpty()?QString():QString(" WHERE p.dataset=?")));
    if(!dataset.isEmpty())q.addBindValue(dataset);
    if(!q.exec()){if(error)*error=q.lastError().text();return {};}
    QMap<QString,QVariantList> groups;
    QMap<QString,TimelineJournal> acceptedRoots;
    while(q.next()) {
        const QString root=q.value(0).toString();TimelineJournal journal;QString journalError;
        if(!readJournal(actual,root,&journal,&journalError)||foreignActive(actual,journal)||blocked(actual,root)) {
            if(error)*error="Uncertain match quarantined: "+(journalError.isEmpty()?QString("active lifecycle was not safely closed"):journalError);
            continue;
        }
        if(journal.dirty||belowPendingFence(actual,root,q.value(1).toULongLong()))continue;
        if(q.value(1).toULongLong()!=journal.generation||q.value(2).toString()!=journal.branch
            ||decoded(q.value(3)).toMap().value("statistics_publication").toString()!=journal.publication) {
            uncertain(actual,root);if(error)*error="Uncertain match quarantined: database and timeline journal disagree";continue;
        }
        acceptedRoots[root]=journal;
        auto row=decoded(q.value(4)).toMap();row["root"]=q.value(0);
        const QString key=QString::fromUtf8(json(QVariantList{row.value("dataset"),row.value("environment"),row.value("general"),row.value("general2")}));groups[key]<<row;
    }
    q.finish();
    // Validate each accepted root again after the SQL snapshot has been read.
    // A concurrent external writer changing its marker rejects every seat of
    // that root, never just the later participants encountered by the query.
    QSet<QString> revokedRoots;
    for(auto it=acceptedRoots.cbegin();it!=acceptedRoots.cend();++it) {
        TimelineJournal journal;QString detail;
        if(!readJournal(actual,it.key(),&journal,&detail)||foreignActive(actual,journal)||journal.dirty
            ||journal.generation!=it.value().generation||journal.branch!=it.value().branch||journal.publication!=it.value().publication
            ||belowPendingFence(actual,it.key(),it.value().generation)) {
            revokedRoots.insert(it.key());
            if(error)*error=detail.isEmpty()?QString("Timeline changed during reading; affected match quarantined"):detail;
        }
    }
    QVariantList result;
    for(auto rows:groups) {
        for(auto it=rows.begin();it!=rows.end();) {
            if(revokedRoots.contains(it->toMap().value("root").toString()))it=rows.erase(it);else ++it;
        }
        if(rows.isEmpty())continue;
        QVariantMap summary=rows.first().toMap(), totals; bool complete=true;
        QSet<QString> roots;QList<double> damageDistribution,turnDamage;QVariantMap normal,extra,truncated;
        QMap<QString,QList<double>> damageByRoot;
        QMap<QString,QVariantList> developmentByRoot;
        for(const auto &v:rows) {
            const auto row=v.toMap(), metrics=row.value("metrics").toMap();roots.insert(row.value("root").toString());
            complete &= row.value("coverage").toBool();
            for(auto it=metrics.cbegin();it!=metrics.cend();++it)add(totals,it.key(),it.value().toDouble());
            damageByRoot[row.value("root").toString()] << metrics.value("damage").toDouble();
            const auto development=row.value("development").toMap();
            if(development.value("covered").toBool())developmentByRoot[row.value("root").toString()]<<development;
            for(const auto &tv:row.value("turns").toList()) {
                const auto t=tv.toMap();
                if(t.value("extra").toBool()||t.value("truncated").toBool()) {
                    auto &bucket=t.value("truncated").toBool()?truncated:extra;
                    for(const QString &key:QStringList{"damage","slash_damage","slash_uses","card_uses","aoe_damage","aoe_uses","support_recovery"})add(bucket,key,t.value(key).toDouble());
                    continue;
                }
                turnDamage << t.value("damage").toDouble();
                for(const QString &key:QStringList{"damage","slash_damage","slash_uses","card_uses","aoe_damage","aoe_uses","support_recovery"})add(normal,key,t.value(key).toDouble());
            }
        }
        // One sample per match: duplicate same-general seats share a match mean,
        // and never inflate either the game count or the variance sample size.
        for(const auto &values:damageByRoot) { double sum=0;for(double n:values)sum+=n;damageDistribution<<sum/values.size(); }
        const int games=roots.size();double mean=0,variance=0;for(double n:damageDistribution)mean+=n;
        mean/=qMax(1,damageDistribution.size());for(double n:damageDistribution)variance+=(n-mean)*(n-mean);
        if(damageDistribution.size()>1)variance/=damageDistribution.size()-1;
        QVariantList labels; const bool eligible=summary.value("dataset")=="human"||summary.value("dataset")=="ai_self_play";
        const bool sufficient=complete&&games>=MinimumGames&&turnDamage.size()>=10;
        if(sufficient&&eligible) {
            if(normal.value("card_uses").toDouble()>0 && normal.value("slash_uses").toDouble()/normal.value("card_uses").toDouble()>=.45 && normal.value("slash_uses").toDouble()/turnDamage.size()>=.8 && normal.value("damage").toDouble()>0 && normal.value("slash_damage").toDouble()/normal.value("damage").toDouble()>=.5)labels<<QString::fromUtf8("菜刀");
            std::sort(turnDamage.begin(),turnDamage.end());int bursts=0;for(double n:turnDamage)if(n>=3)++bursts;
            if(double(bursts)/turnDamage.size()>=.2 && turnDamage.at((turnDamage.size()-1)*3/4)>=2)labels<<QString::fromUtf8("爆发");
            if(normal.value("damage").toDouble()>0 && normal.value("aoe_damage").toDouble()/normal.value("damage").toDouble()>=.4 && normal.value("aoe_uses").toDouble()/turnDamage.size()>=.3)labels<<QString("AOE");
            if(normal.value("support_recovery").toDouble()/turnDamage.size()>=.5)labels<<QString::fromUtf8("辅助");
        }
        double handSlope=0,maxhpSlope=0,damageSlope=0;int growthGames=0,growthCycles=0;
        for(const auto &observations:developmentByRoot) {
            double h=0,m=0,d=0;
            for(const auto &v:observations) {
                const auto observation=v.toMap();h+=observation.value("hand_slope").toDouble();m+=observation.value("maxhp_slope").toDouble();d+=observation.value("damage_slope").toDouble();growthCycles+=observation.value("cycles").toInt();
            }
            h/=observations.size();m/=observations.size();d/=observations.size();
            handSlope+=h;maxhpSlope+=m;damageSlope+=d;if(growing(h,m,d))++growthGames;
        }
        const int developmentGames=developmentByRoot.size();
        if(developmentGames) {handSlope/=developmentGames;maxhpSlope/=developmentGames;damageSlope/=developmentGames;}
        const double positiveFraction=developmentGames?double(growthGames)/developmentGames:0;
        const bool developmentSufficient=developmentGames>=MinimumGames;
        if(eligible&&sufficient&&developmentSufficient&&positiveFraction>=.6&&growing(handSlope,maxhpSlope,damageSlope))labels<<QString::fromUtf8("发育");
        summary["development"]=QVariantMap{{"games",developmentGames},{"cycles",growthCycles},{"survivor_only",true},
            {"status",!eligible?"isolated":!developmentGames?"unknown":!developmentSufficient?"provisional":"observed"},
            {"hand_slope",developmentGames?QVariant(handSlope):QVariant()},
            {"maxhp_slope",developmentGames?QVariant(maxhpSlope):QVariant()},
            {"damage_slope",developmentGames?QVariant(damageSlope):QVariant()},
            {"positive_fraction",developmentGames?QVariant(positiveFraction):QVariant()}};
        summary.remove("player");summary.remove("root");summary.remove("turns");summary.remove("control_history");
        summary["games"]=games;summary["observations"]=rows.size();summary["metrics"]=totals;summary["coverage"]=complete;summary["labels"]=labels;
        summary["status"]=!eligible?"isolated":!complete?"unknown":!sufficient?"provisional":"observed";
        std::sort(damageDistribution.begin(),damageDistribution.end());
        summary["damage_max"]=!complete||damageDistribution.isEmpty()?QVariant():QVariant(damageDistribution.last());
        summary["damage_p75"]=!complete||damageDistribution.isEmpty()?QVariant():QVariant(damageDistribution.at((damageDistribution.size()-1)*3/4));
        summary["damage_mean"]=complete?QVariant(mean):QVariant();summary["damage_variance"]=complete&&damageDistribution.size()>1?QVariant(variance):QVariant();
        summary["normal_turn_metrics"]=normal;
        summary["extra_turn_metrics"]=extra;summary["truncated_turn_metrics"]=truncated;
        std::sort(turnDamage.begin(),turnDamage.end());
        int burstCount=0;for(double n:turnDamage)if(n>=3)++burstCount;
        summary["burst_frequency"]=!complete||turnDamage.isEmpty()?QVariant():QVariant(double(burstCount)/turnDamage.size());
        summary["turn_damage_p75"]=!complete||turnDamage.isEmpty()?QVariant():QVariant(turnDamage.at((turnDamage.size()-1)*3/4));
        summary["analysis_version"]=AnalysisVersion;
        summary["unsupported_labels"]=QStringList{QString::fromUtf8("控制"),QString::fromUtf8("坦克"),QString::fromUtf8("卖血"),QString::fromUtf8("过牌"),QString::fromUtf8("速战速决"),QString::fromUtf8("拉长战线")};
        result<<summary;
    }
    if(projectionRevision(actual)!=readRevision) {
        if(error)*error="Statistics changed during reading; refresh required";
        return {};
    }
    return result;
}
}
