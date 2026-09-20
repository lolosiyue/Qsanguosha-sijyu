#include "scenario-work.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QUuid>
#include <cmath>
#include <limits>

namespace ScenarioWork {
namespace {
    constexpr qint64 MaxBytes = 8 * 1024 * 1024;
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    constexpr Qt::DateFormat SnapshotDateFormat = Qt::ISODateWithMs;
#else
    constexpr Qt::DateFormat SnapshotDateFormat = Qt::ISODate;
#endif

    QString kindName(WorkKind k)
    {
        return k == WorkKind::Scene ? QStringLiteral("scene") : QStringLiteral("stage");
    }
    QString selectionName(SelectionPolicy p)
    {
        return p == SelectionPolicy::Sequential ? QStringLiteral("sequential") : QStringLiteral("free");
    }
    QString goalModeName(GoalMode m)
    {
        return m == GoalMode::Objective ? QStringLiteral("objective") : QStringLiteral("settlement");
    }
    QString typeName(PredicateType t)
    {
        return QStringList { QStringLiteral("alive"), QStringLiteral("dead"), QStringLiteral("hp"),
            QStringLiteral("mark"), QStringLiteral("turns") }
            .value(static_cast<int>(t));
    }
    QString opName(PredicateOp op)
    {
        return QStringList { QStringLiteral("lt"), QStringLiteral("le"), QStringLiteral("eq"),
            QStringLiteral("ge"), QStringLiteral("gt") }
            .value(static_cast<int>(op));
    }

    bool enumValue(const QString &s, const QStringList &values, int *out)
    {
        const int i = values.indexOf(s);
        if (i < 0)
            return false;
        *out = i;
        return true;
    }
    QString safeId(const QString &id)
    {
        static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
        static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$"),
            QRegularExpression::CaseInsensitiveOption);
        return pattern.match(id).hasMatch() && !reserved.match(id).hasMatch() ? id : QString();
    }

    bool fail(QString *error, const char *message)
    {
        if (error)
            *error = QCoreApplication::translate("ScenarioWork", message);
        return false;
    }
    bool integer(const QJsonValue &v, int lo = std::numeric_limits<int>::min(),
        int hi = std::numeric_limits<int>::max())
    {
        return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble())
            && v.toDouble() >= lo && v.toDouble() <= hi;
    }
    bool keysOnly(const QJsonObject &o, const QStringList &keys)
    {
        for (const auto &key : o.keys())
            if (!keys.contains(key))
                return false;
        return true;
    }
    bool strings(const QJsonObject &o, const QStringList &keys)
    {
        for (const auto &key : keys)
            if (!o.value(key).isString())
                return false;
        return true;
    }
    bool stringArray(const QJsonValue &v, QStringList *out)
    {
        if (!v.isArray())
            return false;
        QSet<QString> seen;
        for (const auto &x : v.toArray()) {
            if (!x.isString() || x.toString().isEmpty() || seen.contains(x.toString()))
                return false;
            seen.insert(x.toString());
            out->append(x.toString());
        }
        return true;
    }
    bool hashId(const QString &id)
    {
        return QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(id).hasMatch();
    }

    QJsonObject predicateToJson(const GoalPredicate &p)
    {
        QJsonObject o { { QStringLiteral("type"), typeName(p.type) }, { QStringLiteral("seat"), p.seat },
            { QStringLiteral("op"), opName(p.op) }, { QStringLiteral("threshold"), p.threshold } };
        if (!p.mark.isEmpty())
            o.insert(QStringLiteral("mark"), p.mark);
        return o;
    }
    QJsonObject groupToJson(const GoalGroup &g)
    {
        QJsonArray a;
        for (const auto &p : g.predicates)
            a.append(predicateToJson(p));
        return { { QStringLiteral("all"), g.all }, { QStringLiteral("predicates"), a } };
    }
    QJsonObject goalToJson(const GoalDefinition &g)
    {
        return { { QStringLiteral("mode"), goalModeName(g.mode) },
            { QStringLiteral("success"), groupToJson(g.success) },
            { QStringLiteral("failure"), groupToJson(g.failure) } };
    }
    QJsonObject sceneToJson(const SceneDefinition &s)
    {
        QJsonArray goals;
        for (const auto &g : s.goals)
            goals.append(goalToJson(g));
        return { { QStringLiteral("id"), s.id }, { QStringLiteral("revision"), s.revision },
            { QStringLiteral("title"), s.title }, { QStringLiteral("author"), s.author },
            { QStringLiteral("intro"), s.intro }, { QStringLiteral("opening"), s.opening },
            { QStringLiteral("ending"), s.ending }, { QStringLiteral("setup"), s.setup },
            { QStringLiteral("playerSeat"), s.playerSeat }, { QStringLiteral("goals"), goals } };
    }
    QJsonObject entryToJson(const StageEntry &e)
    {
        QJsonArray goals;
        for (const auto &g : e.goals)
            goals.append(goalToJson(g));
        return { { QStringLiteral("id"), e.id }, { QStringLiteral("sceneId"), e.sceneId },
            { QStringLiteral("sceneRevision"), e.sceneRevision }, { QStringLiteral("title"), e.title },
            { QStringLiteral("intro"), e.intro }, { QStringLiteral("goals"), goals } };
    }

    bool parsePredicate(const QJsonValue &v, GoalPredicate *p, QString *err)
    {
        if (!v.isObject()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "predicate must be an object");
            return false;
        }
        const auto o = v.toObject();
        if (!keysOnly(o, { "type", "seat", "op", "threshold", "mark" })
            || (o.contains("mark") && !o.value("mark").isString()))
            return fail(err, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid predicate fields."));
        const QStringList types { QStringLiteral("alive"), QStringLiteral("dead"), QStringLiteral("hp"),
            QStringLiteral("mark"), QStringLiteral("turns") };
        const QStringList ops { QStringLiteral("lt"), QStringLiteral("le"), QStringLiteral("eq"),
            QStringLiteral("ge"), QStringLiteral("gt") };
        int x;
        if (!o.value(QStringLiteral("type")).isString()
            || !enumValue(o.value(QStringLiteral("type")).toString(), types, &x)) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "unknown predicate type");
            return false;
        }
        p->type = static_cast<PredicateType>(x);
        if (!integer(o.value(QStringLiteral("seat")))) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "predicate seat must be integer");
            return false;
        }
        p->seat = o.value(QStringLiteral("seat")).toInt(-1);
        if (!o.value(QStringLiteral("op")).isString()
            || !enumValue(o.value(QStringLiteral("op")).toString(), ops, &x)) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "unknown predicate operator");
            return false;
        }
        p->op = static_cast<PredicateOp>(x);
        if (!integer(o.value(QStringLiteral("threshold")))) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "predicate threshold must be integer");
            return false;
        }
        p->threshold = o.value(QStringLiteral("threshold")).toInt();
        p->mark = o.value(QStringLiteral("mark")).toString();
        return true;
    }
    bool parseGroup(const QJsonValue &v, GoalGroup *g, QString *err)
    {
        if (!v.isObject()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "goal group must be object");
            return false;
        }
        const auto o = v.toObject();
        if (!keysOnly(o, { "all", "predicates" }))
            return fail(err, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown goal group field."));
        if (!o.value(QStringLiteral("all")).isBool() || !o.value(QStringLiteral("predicates")).isArray()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "invalid goal group");
            return false;
        }
        g->all = o.value(QStringLiteral("all")).toBool();
        for (const auto &x : o.value(QStringLiteral("predicates")).toArray()) {
            GoalPredicate p;
            if (!parsePredicate(x, &p, err))
                return false;
            g->predicates.append(p);
        }
        return true;
    }
    bool parseGoal(const QJsonValue &v, GoalDefinition *g, QString *err)
    {
        if (!v.isObject()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "goal must be object");
            return false;
        }
        const auto o = v.toObject();
        if (!keysOnly(o, { "mode", "success", "failure" }))
            return fail(err, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown goal field."));
        const QStringList modes { QStringLiteral("objective"), QStringLiteral("settlement") };
        int x;
        if (!o.value(QStringLiteral("mode")).isString()
            || !enumValue(o.value(QStringLiteral("mode")).toString(), modes, &x)) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "unknown goal mode");
            return false;
        }
        g->mode = static_cast<GoalMode>(x);
        return parseGroup(o.value(QStringLiteral("success")), &g->success, err)
            && parseGroup(o.value(QStringLiteral("failure")), &g->failure, err);
    }
    bool parseGoals(const QJsonValue &v, QList<GoalDefinition> *out, QString *err)
    {
        if (!v.isArray()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "goals must be array");
            return false;
        }
        for (const auto &x : v.toArray()) {
            GoalDefinition g;
            if (!parseGoal(x, &g, err))
                return false;
            out->append(g);
        }
        return true;
    }
    bool parseScene(const QJsonValue &v, SceneDefinition *s, QString *err)
    {
        if (!v.isObject()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "scene must be object");
            return false;
        }
        const auto o = v.toObject();
        if (!keysOnly(o,
                { "id", "revision", "title", "author", "intro", "opening", "ending", "setup", "playerSeat",
                    "goals" })
            || !strings(o, { "id", "revision", "title", "author", "intro", "opening", "ending", "setup" })
            || !integer(o.value("playerSeat"), 0, 9))
            return fail(err, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene fields."));
        if (!o.value(QStringLiteral("id")).isString() || !o.value(QStringLiteral("setup")).isString()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "scene id/setup missing");
            return false;
        }
        s->id = o.value(QStringLiteral("id")).toString();
        s->revision = o.value(QStringLiteral("revision")).toString();
        s->title = o.value(QStringLiteral("title")).toString();
        s->author = o.value(QStringLiteral("author")).toString();
        s->intro = o.value(QStringLiteral("intro")).toString();
        s->opening = o.value(QStringLiteral("opening")).toString();
        s->ending = o.value(QStringLiteral("ending")).toString();
        s->setup = o.value(QStringLiteral("setup")).toString();
        s->playerSeat = o.value(QStringLiteral("playerSeat")).toInt(0);
        return parseGoals(o.value(QStringLiteral("goals")), &s->goals, err);
    }
    bool parseEntry(const QJsonValue &v, StageEntry *e, QString *err)
    {
        if (!v.isObject()) {
            if (err)
                *err = QCoreApplication::translate("ScenarioWork", "entry must be object");
            return false;
        }
        const auto o = v.toObject();
        if (!keysOnly(o, { "id", "sceneId", "sceneRevision", "title", "intro", "goals" })
            || !strings(o, { "id", "sceneId", "sceneRevision", "title", "intro" }))
            return fail(err, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid entry fields."));
        e->id = o.value(QStringLiteral("id")).toString();
        e->sceneId = o.value(QStringLiteral("sceneId")).toString();
        e->sceneRevision = o.value(QStringLiteral("sceneRevision")).toString();
        e->title = o.value(QStringLiteral("title")).toString();
        e->intro = o.value(QStringLiteral("intro")).toString();
        return parseGoals(o.value(QStringLiteral("goals")), &e->goals, err);
    }

    bool compare(int a, PredicateOp op, int b)
    {
        switch (op) {
        case PredicateOp::Lt:
            return a < b;
        case PredicateOp::Le:
            return a <= b;
        case PredicateOp::Eq:
            return a == b;
        case PredicateOp::Ge:
            return a >= b;
        case PredicateOp::Gt:
            return a > b;
        }
        return false;
    }
    bool groupSatisfied(const GoalGroup &g, const QList<SeatState> &seats, int turns)
    {
        if (g.predicates.isEmpty())
            return false;
        for (const auto &p : g.predicates) {
            bool v = false;
            if (p.seat < 0 || p.seat >= seats.size())
                v = false;
            else {
                const auto &s = seats.at(p.seat);
                switch (p.type) {
                case PredicateType::Alive:
                    v = s.alive;
                    break;
                case PredicateType::Dead:
                    v = !s.alive;
                    break;
                case PredicateType::Hp:
                    v = compare(s.hp, p.op, p.threshold);
                    break;
                case PredicateType::Mark:
                    v = compare(s.marks.value(p.mark), p.op, p.threshold);
                    break;
                case PredicateType::Turns:
                    v = compare(turns, p.op, p.threshold);
                    break;
                }
            }
            if (g.all && !v)
                return false;
            if (!g.all && v)
                return true;
        }
        return g.all;
    }

    QJsonObject canonical(const QJsonObject &input)
    {
        QJsonObject out;
        QStringList keys = input.keys();
        keys.sort();
        for (const auto &k : keys) {
            const auto v = input.value(k);
            if (v.isObject())
                out.insert(k, canonical(v.toObject()));
            else if (v.isArray()) {
                QJsonArray a;
                for (const auto &e : v.toArray())
                    a.append(e.isObject() ? QJsonValue(canonical(e.toObject())) : e);
                out.insert(k, a);
            } else
                out.insert(k, v);
        }
        return out;
    }
    // Library identities never become arbitrary paths. Reject symlinks/junctions at
    // every existing component, including the target, before creating or opening
    // it.
    QString libraryPath(const QString &root, const QString &area, const QString &id, const QString &revision)
    {
        if (root.isEmpty() || safeId(id).isEmpty() || !hashId(revision))
            return { };
        QDir base(root);
        const QString path
            = base.absoluteFilePath(area + "/" + id + "/" + revision + QStringLiteral(".json"));
        QFileInfo cursor(path);
        for (;;) {
            if (cursor.isSymLink())
                return { };
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
            if (cursor.isJunction())
                return { };
#endif
            const QString parent = cursor.absolutePath();
            if (parent == cursor.absoluteFilePath())
                break;
            cursor = QFileInfo(parent);
        }
        return QDir::cleanPath(path);
    }
    bool atomicJson(const QString &path, const QJsonObject &object, QString *error)
    {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
        if (path.isEmpty() || bytes.size() > MaxBytes)
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unsafe path or document too large."));
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Cannot create library directory."));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Cannot save library document."));
        return true;
    }
    bool readJson(const QString &path, QJsonObject *object, QString *error)
    {
        if (path.isEmpty())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unsafe library path."));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > MaxBytes)
            return fail(
                error, QT_TRANSLATE_NOOP("ScenarioWork", "Cannot read document or document too large."));
        // Bound the read even if a concurrently changed file grows after size().
        const auto bytes = file.read(MaxBytes + 1);
        if (bytes.size() > MaxBytes)
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Document too large."));
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid JSON document."));
        *object = doc.object();
        return true;
    }
    bool progressShape(const WorkProgress &p, QString *error)
    {
        if (safeId(p.workId).isEmpty() || !hashId(p.revision))
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid progress identity."));
        for (const auto &names : { p.completedEntryIds, p.processedRunIds }) {
            QSet<QString> ids;
            for (const auto &id : names) {
                if (safeId(id).isEmpty() || ids.contains(id))
                    return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid progress identifiers."));
                ids.insert(id);
            }
        }
        if (!p.continuationEntryId.isEmpty() && safeId(p.continuationEntryId).isEmpty())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid continuation identity."));
        QSet<QString> snapshots;
        for (const auto &snapshot : p.snapshots) {
            if (safeId(snapshot.id).isEmpty() || safeId(snapshot.entryId).isEmpty()
                || safeId(snapshot.sourceEntryId).isEmpty() || !snapshot.createdAt.isValid()
                || snapshots.contains(snapshot.id) || !p.completedEntryIds.contains(snapshot.sourceEntryId)
                || !validateCarryState(snapshot.carry))
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid progress snapshot."));
            snapshots.insert(snapshot.id);
        }
        return true;
    }
    bool hasLegacyEnding(const LegacySceneDocument &document)
    {
        for (const auto &player : document.players)
            for (const QString key : { "endedByPile", "singleTurn", "beforeNext" })
                if (player.contains(key))
                    return true;
        for (const auto &option : document.extraOptions)
            if (option.startsWith("beforeStartRound:") || option.startsWith("afterRound:"))
                return true;
        return false;
    }
    bool objective(const QList<GoalDefinition> &goals)
    {
        for (const auto &goal : goals)
            if (goal.mode == GoalMode::Objective)
                return true;
        return false;
    }
    QJsonObject progressJson(const WorkProgress &p)
    {
        QJsonArray c;
        for (const auto &x : p.completedEntryIds)
            c.append(x);
        QJsonArray ss;
        for (const auto &s : p.snapshots)
            ss.append(QJsonObject { { QStringLiteral("id"), s.id },
                { QStringLiteral("sourceEntryId"), s.sourceEntryId },
                { QStringLiteral("entryId"), s.entryId },
                { QStringLiteral("createdAt"), s.createdAt.toUTC().toString(SnapshotDateFormat) },
                { QStringLiteral("carry"), s.carry.values } });
        QJsonArray r;
        for (const auto &x : p.processedRunIds)
            r.append(x);
        return { { QStringLiteral("workId"), p.workId }, { QStringLiteral("revision"), p.revision },
            { QStringLiteral("completedEntryIds"), c },
            { QStringLiteral("continuationEntryId"), p.continuationEntryId },
            { QStringLiteral("snapshots"), ss }, { QStringLiteral("processedRunIds"), r } };
    }
} // namespace

bool CarryPolicy::enabled() const
{
    return hp || maxhp || hujia || generals || hand || equip || !marks.isEmpty() || !skills.isEmpty();
}

GoalEvaluation evaluateGoals(const GoalDefinition &g, const QList<SeatState> &s, int t)
{
    GoalEvaluation r;
    r.success
        = g.success.predicates.isEmpty() ? (g.mode == GoalMode::Settlement) : groupSatisfied(g.success, s, t);
    r.failure = !g.failure.predicates.isEmpty() && groupSatisfied(g.failure, s, t);
    if (r.failure)
        r.success = false;
    return r;
}

bool validateCarryState(const CarryState &state, QStringList *errors)
{
    QStringList e;
    auto add = [&](const char *message) { e.append(QCoreApplication::translate("ScenarioWork", message)); };
    const auto &o = state.values;
    if (!keysOnly(
            o, { "hp", "maxhp", "hujia", "general", "general2", "hand", "equip", "marks", "acquiredSkills" }))
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Unsupported carry field."));
    for (const QString key : { "hp", "maxhp", "hujia" })
        if (o.contains(key) && !integer(o.value(key), key == "hujia" ? 0 : 1, 999))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid carry health value."));
    if (o.contains("hp") && o.contains("maxhp") && o.value("hp").toDouble() > o.value("maxhp").toDouble())
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Carry health exceeds maximum health."));
    for (const QString key : { "general", "general2" })
        if (o.contains(key)
            && (!o.value(key).isString() || (key == "general" && o.value(key).toString().isEmpty())))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid carry general."));
    QSet<int> cards;
    for (const QString key : { "hand", "equip" }) {
        if (!o.contains(key))
            continue;
        if (!o.value(key).isArray()) {
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Carry cards must be an array."));
            continue;
        }
        for (const auto &v : o.value(key).toArray()) {
            if (!integer(v, 0) || cards.contains(v.toInt()))
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid or duplicate carry card."));
            cards.insert(v.toInt());
        }
    }
    if (o.contains("acquiredSkills")) {
        QStringList names;
        if (!stringArray(o.value("acquiredSkills"), &names))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid acquired skills."));
    }
    if (o.contains("marks")) {
        if (!o.value("marks").isArray())
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Carry marks must be an array."));
        QSet<QString> names;
        for (const auto &v : o.value("marks").toArray()) {
            const auto mark = v.toObject();
            const auto name = mark.value("name").toString();
            if (!v.isObject() || !keysOnly(mark, { "name", "value" }) || name.isEmpty()
                || names.contains(name) || !integer(mark.value("value"), 0))
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid carry mark."));
            names.insert(name);
        }
    }
    if (errors)
        *errors = e;
    return e.isEmpty();
}

WorkDefinition defaultWork()
{
    WorkDefinition w;
    w.id = QUuid::createUuid().toString().mid(1, 36);
    w.title = QCoreApplication::translate("ScenarioWork", "Untitled work");
    w.scenes.append(draftScene());
    w.scenes[0].revision = computeSceneRevision(w.scenes[0]);
    w.entries.append({ QStringLiteral("entry-1"), w.scenes.first().id, w.scenes.first().revision,
        w.scenes.first().title, QString(), { } });
    return w;
}
SceneDefinition draftScene()
{
    SceneDefinition s;
    s.id = QUuid::createUuid().toString().mid(1, 36);
    s.title = QCoreApplication::translate("ScenarioWork", "Untitled scene");
    s.setup = QStringLiteral("general:select role:lord starter:true\ngeneral:select role:rebel\n");
    s.playerSeat = 0;
    return s;
}

QJsonObject workToJson(const WorkDefinition &w)
{
    QJsonArray a, b;
    for (const auto &s : w.scenes)
        a.append(sceneToJson(s));
    for (const auto &e : w.entries)
        b.append(entryToJson(e));
    QJsonArray marks;
    for (const auto &m : w.carry.marks)
        marks.append(m);
    QJsonArray skills;
    for (const auto &m : w.carry.skills)
        skills.append(m);
    return { { QStringLiteral("schemaVersion"), w.schemaVersion },
        { QStringLiteral("kind"), kindName(w.kind) }, { QStringLiteral("id"), w.id },
        { QStringLiteral("revision"), w.revision }, { QStringLiteral("title"), w.title },
        { QStringLiteral("author"), w.author }, { QStringLiteral("intro"), w.intro },
        { QStringLiteral("rule"), w.rule }, { QStringLiteral("compatibility"), w.compatibility },
        { QStringLiteral("rules"), w.rules }, { QStringLiteral("selection"), selectionName(w.selection) },
        { QStringLiteral("scenes"), a }, { QStringLiteral("entries"), b },
        { QStringLiteral("carry"),
            QJsonObject { { QStringLiteral("hp"), w.carry.hp }, { QStringLiteral("maxhp"), w.carry.maxhp },
                { QStringLiteral("hujia"), w.carry.hujia }, { QStringLiteral("generals"), w.carry.generals },
                { QStringLiteral("hand"), w.carry.hand }, { QStringLiteral("equip"), w.carry.equip },
                { QStringLiteral("marks"), marks }, { QStringLiteral("skills"), skills } } } };
}

bool workFromJson(const QJsonObject &j, WorkDefinition *work, QString *error)
{
    // Stage every parse so a rejected import cannot mutate the editor document.
    if (!work
        || !keysOnly(j,
            { "schemaVersion", "kind", "id", "revision", "title", "author", "intro", "rule", "compatibility",
                "rules", "selection", "scenes", "entries", "carry" })
        || !integer(j.value("schemaVersion"), 1, 1)
        || !strings(j, { "kind", "id", "revision", "title", "author", "intro", "rule", "selection" })
        || !j.value("compatibility").isObject() || !j.value("rules").isObject()
        || !j.value("scenes").isArray() || !j.value("entries").isArray() || !j.value("carry").isObject())
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid work document fields."));
    WorkDefinition parsed;
    int value;
    if (!enumValue(j.value("kind").toString(), { "scene", "stage" }, &value))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown work kind."));
    parsed.kind = static_cast<WorkKind>(value);
    if (!enumValue(j.value("selection").toString(), { "sequential", "free" }, &value))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown selection policy."));
    parsed.selection = static_cast<SelectionPolicy>(value);
    parsed.id = j.value("id").toString();
    parsed.revision = j.value("revision").toString();
    parsed.title = j.value("title").toString();
    parsed.author = j.value("author").toString();
    parsed.intro = j.value("intro").toString();
    parsed.rule = j.value("rule").toString();
    parsed.compatibility = j.value("compatibility").toObject();
    parsed.rules = j.value("rules").toObject();
    for (const auto &v : j.value("scenes").toArray()) {
        SceneDefinition scene;
        if (!parseScene(v, &scene, error))
            return false;
        parsed.scenes.append(scene);
    }
    for (const auto &v : j.value("entries").toArray()) {
        StageEntry entry;
        if (!parseEntry(v, &entry, error))
            return false;
        parsed.entries.append(entry);
    }
    const auto carry = j.value("carry").toObject();
    if (!keysOnly(carry, { "hp", "maxhp", "hujia", "generals", "hand", "equip", "marks", "skills" }))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown carry policy field."));
    for (const QString key : { "hp", "maxhp", "hujia", "generals", "hand", "equip" })
        if (!carry.value(key).isBool())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Carry switches must be boolean."));
    parsed.carry.hp = carry.value("hp").toBool();
    parsed.carry.maxhp = carry.value("maxhp").toBool();
    parsed.carry.hujia = carry.value("hujia").toBool();
    parsed.carry.generals = carry.value("generals").toBool();
    parsed.carry.hand = carry.value("hand").toBool();
    parsed.carry.equip = carry.value("equip").toBool();
    if (!stringArray(carry.value("marks"), &parsed.carry.marks)
        || !stringArray(carry.value("skills"), &parsed.carry.skills))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid carry policy names."));
    QStringList errors;
    if (!validateWork(parsed, &errors)) {
        if (error)
            *error = errors.join(QStringLiteral("; "));
        return false;
    }
    *work = parsed;
    return true;
}

bool validateWork(const WorkDefinition &w, QStringList *errors)
{
    QStringList e;
    auto add = [&](const char *message) { e.append(QCoreApplication::translate("ScenarioWork", message)); };
    if (w.schemaVersion != 1 || safeId(w.id).isEmpty() || w.rule != QStringLiteral("mini_identity"))
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid work identity or rule."));
    if (w.kind != WorkKind::Scene && w.kind != WorkKind::Stage)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Unknown work kind."));
    if (w.selection != SelectionPolicy::Sequential && w.selection != SelectionPolicy::Free)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Unknown selection policy."));
    if (!keysOnly(w.rules, { "secondGeneral", "fixedSeats" }))
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Unsupported rule field."));
    if (w.rules.contains("secondGeneral") && !w.rules.value("secondGeneral").isBool())
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Second general must be boolean."));
    if (w.rules.contains("fixedSeats")
        && (!w.rules.value("fixedSeats").isBool() || !w.rules.value("fixedSeats").toBool()))
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Fixed seats must be enabled."));
    if (!w.compatibility.isEmpty()) {
        const auto &c = w.compatibility;
        QStringList extensions;
        if (!stringArray(c.value("extensions"), &extensions))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid extension manifest."));
        QStringList sortedExtensions = extensions;
        sortedExtensions.sort();
        if (extensions != sortedExtensions)
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Extension manifest must be sorted."));
        if (!keysOnly(c, { "engineVersion", "manifest", "content", "contentReadable", "cards", "extensions" })
            || !strings(c, { "engineVersion", "manifest", "content", "cards" })
            || c.value("engineVersion").toString().isEmpty() || c.value("manifest").toString().isEmpty()
            || !hashId(c.value("content").toString()) || !hashId(c.value("cards").toString())
            || !c.value("contentReadable").isBool())
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid compatibility fingerprint."));
    }
    if (!w.legacyFields.isEmpty())
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Unknown export data is not allowed."));
    if (w.scenes.isEmpty() || w.entries.isEmpty() || (w.kind == WorkKind::Scene && w.entries.size() != 1))
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene or entry count."));
    auto checkGoals = [&](const QList<GoalDefinition> &goals, int seats) {
        if (goals.size() > 1)
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Only one goal definition is allowed."));
        for (const auto &g : goals) {
            if (g.mode != GoalMode::Objective && g.mode != GoalMode::Settlement)
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Unknown goal mode."));
            if (g.mode == GoalMode::Objective && g.success.predicates.isEmpty())
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Objective requires success predicates."));
            for (const auto &group : { g.success, g.failure })
                for (const auto &p : group.predicates) {
                    if (p.seat < 0 || p.seat >= seats || static_cast<int>(p.type) < 0
                        || static_cast<int>(p.type) > 4 || static_cast<int>(p.op) < 0
                        || static_cast<int>(p.op) > 4 || (p.type == PredicateType::Mark && p.mark.isEmpty()))
                        add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid goal predicate."));
                }
        }
    };
    QMap<QString, int> sceneSeats;
    QSet<QString> sceneIds;
    QSet<QString> legacyEndingScenes;
    for (const auto &scene : w.scenes) {
        sceneIds.insert(scene.id);
        const QString key = scene.id + ":" + scene.revision;
        if (safeId(scene.id).isEmpty() || !hashId(scene.revision)
            || scene.revision != computeSceneRevision(scene) || sceneSeats.contains(key))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene identity or revision."));
        LegacySceneDocument legacy;
        QString error;
        if (!parseLegacyScene(scene.setup, &legacy, &error))
            e.append(error);
        sceneSeats.insert(key, legacy.players.size());
        if (hasLegacyEnding(legacy)) {
            legacyEndingScenes.insert(key);
            if (objective(scene.goals))
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Objective goals conflict with legacy endings."));
        }
        if (scene.playerSeat < 0 || scene.playerSeat >= legacy.players.size())
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Player seat is outside the scene."));
        checkGoals(scene.goals, legacy.players.size());
    }
    if (w.kind == WorkKind::Scene && sceneIds.size() != 1)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene work must contain one scene identity."));
    QSet<QString> entries;
    for (const auto &entry : w.entries) {
        if (safeId(entry.id).isEmpty() || entries.contains(entry.id))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid or duplicate entry identity."));
        entries.insert(entry.id);
        const QString key = entry.sceneId + ":" + entry.sceneRevision;
        if (!sceneSeats.contains(key))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Entry must reference an exact scene revision."));
        checkGoals(entry.goals, sceneSeats.value(key));
        if (legacyEndingScenes.contains(key) && objective(entry.goals))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Objective goals conflict with legacy endings."));
    }
    for (const auto &names : { w.carry.marks, w.carry.skills }) {
        QSet<QString> seen;
        for (const auto &name : names) {
            if (name.isEmpty() || seen.contains(name))
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid carry policy names."));
            seen.insert(name);
        }
    }
    if (errors)
        *errors = e;
    return e.isEmpty();
}

QString computeSceneRevision(const SceneDefinition &s)
{
    SceneDefinition copy = s;
    copy.revision.clear();
    const QByteArray b = QJsonDocument(canonical(sceneToJson(copy))).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QString computeRevision(const WorkDefinition &w)
{
    WorkDefinition c = w;
    c.revision.clear();
    const QByteArray b = QJsonDocument(canonical(workToJson(c))).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QString workFilePath(const QString &root, const WorkDefinition &work)
{
    return libraryPath(root, QStringLiteral("works"), work.id, work.revision);
}

bool readWork(const QString &path, WorkDefinition *work, QString *error)
{
    QJsonObject json;
    WorkDefinition parsed;
    if (!work || !readJson(path, &json, error) || !workFromJson(json, &parsed, error))
        return false;
    if (!hashId(parsed.revision) || parsed.revision != computeRevision(parsed))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Work revision does not match content."));
    *work = parsed;
    return true;
}

bool writeWork(const QString &root, const WorkDefinition &work, QString *error)
{
    QStringList errors;
    if (!validateWork(work, &errors)) {
        if (error)
            *error = errors.join(QStringLiteral("; "));
        return false;
    }
    WorkDefinition copy = work;
    const auto expected = computeRevision(copy);
    if (copy.revision.isEmpty())
        copy.revision = expected;
    if (copy.revision != expected)
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Work revision does not match content."));
    return atomicJson(workFilePath(root, copy), workToJson(copy), error);
}

QList<WorkFileInfo> listWorks(const QString &root, QString *error)
{
    QList<WorkFileInfo> result;
    QDir base(QDir(root).filePath(QStringLiteral("works")));
    for (const auto &id : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (safeId(id).isEmpty())
            continue;
        QDir directory(base.filePath(id));
        for (const auto &file :
            directory.entryList({ QStringLiteral("*.json") }, QDir::Files | QDir::NoSymLinks)) {
            const auto revision = QFileInfo(file).completeBaseName();
            const auto path = libraryPath(root, QStringLiteral("works"), id, revision);
            WorkDefinition work;
            QString ignored;
            if (!path.isEmpty() && readWork(path, &work, &ignored) && work.id == id
                && work.revision == revision)
                result.append({ work.id, work.revision, work.title, work.kind, path });
        }
    }
    if (error)
        error->clear();
    return result;
}

bool loadProgress(const QString &root, const WorkDefinition &work, WorkProgress *progress, QString *error)
{
    if (!progress)
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Missing progress output."));
    const auto path = libraryPath(root, QStringLiteral("progress"), work.id, work.revision);
    if (path.isEmpty())
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unsafe progress path."));
    WorkProgress parsed;
    parsed.workId = work.id;
    parsed.revision = work.revision;
    if (!QFileInfo::exists(path)) {
        *progress = parsed;
        return true;
    }
    QJsonObject o;
    if (!readJson(path, &o, error))
        return false;
    if (!keysOnly(o,
            { "workId", "revision", "completedEntryIds", "continuationEntryId", "snapshots",
                "processedRunIds" })
        || !strings(o, { "workId", "revision", "continuationEntryId" }) || !o.value("snapshots").isArray()
        || o.value("workId").toString() != work.id || o.value("revision").toString() != work.revision
        || !stringArray(o.value("completedEntryIds"), &parsed.completedEntryIds)
        || !stringArray(o.value("processedRunIds"), &parsed.processedRunIds))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid progress document."));
    parsed.continuationEntryId = o.value("continuationEntryId").toString();
    QMap<QString, int> entries;
    for (int i = 0; i < work.entries.size(); ++i)
        entries.insert(work.entries[i].id, i);
    for (const auto &id : parsed.completedEntryIds)
        if (!entries.contains(id))
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown completed entry."));
    if (!parsed.continuationEntryId.isEmpty() && !entries.contains(parsed.continuationEntryId))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Unknown continuation entry."));
    for (const auto &v : o.value("snapshots").toArray()) {
        const auto q = v.toObject();
        if (!v.isObject() || !keysOnly(q, { "id", "sourceEntryId", "entryId", "createdAt", "carry" })
            || !strings(q, { "id", "sourceEntryId", "entryId", "createdAt" }) || !q.value("carry").isObject())
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid snapshot fields."));
        ProgressSnapshot snapshot;
        snapshot.id = q.value("id").toString();
        snapshot.sourceEntryId = q.value("sourceEntryId").toString();
        snapshot.entryId = q.value("entryId").toString();
        snapshot.createdAt = QDateTime::fromString(q.value("createdAt").toString(), SnapshotDateFormat);
        snapshot.carry.values = q.value("carry").toObject();
        if (!entries.contains(snapshot.sourceEntryId) || !entries.contains(snapshot.entryId)
            || entries.value(snapshot.entryId) != entries.value(snapshot.sourceEntryId) + 1)
            return fail(error,
                QT_TRANSLATE_NOOP("ScenarioWork", "Snapshot target does not follow its source entry."));
        parsed.snapshots.append(snapshot);
    }
    if (!progressShape(parsed, error))
        return false;
    *progress = parsed;
    return true;
}

bool canPlayEntry(const WorkDefinition &work, const WorkProgress &progress, const QString &entryId)
{
    if (progress.workId != work.id || progress.revision != work.revision)
        return false;
    int index = -1;
    for (int i = 0; i < work.entries.size(); ++i)
        if (work.entries.at(i).id == entryId) {
            index = i;
            break;
        }
    if (index < 0)
        return false;
    if (work.selection == SelectionPolicy::Free)
        return true;
    for (int i = 0; i < index; ++i)
        if (!progress.completedEntryIds.contains(work.entries.at(i).id))
            return false;
    return true;
}
bool saveProgress(const QString &root, const WorkProgress &progress, QString *error)
{
    if (!progressShape(progress, error))
        return false;
    const auto path = libraryPath(root, QStringLiteral("progress"), progress.workId, progress.revision);
    const auto updated = progressJson(progress);
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QJsonObject existing;
        if (!readJson(path, &existing, error))
            return false;
        if (existing.value("workId") != updated.value("workId")
            || existing.value("revision") != updated.value("revision"))
            return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Progress identity cannot change."));
        // Previous results and snapshots are append-only; retries cannot replace
        // the state from which an already saved target entry was launched.
        for (const QString key : { "completedEntryIds", "processedRunIds", "snapshots" }) {
            if (!existing.value(key).isArray())
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid existing progress."));
            const auto before = existing.value(key).toArray(), after = updated.value(key).toArray();
            if (after.size() < before.size())
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Saved progress cannot be removed."));
            for (int i = 0; i < before.size(); ++i)
                if (before[i] != after[i])
                    return fail(
                        error, QT_TRANSLATE_NOOP("ScenarioWork", "Saved progress cannot be changed."));
        }
    }
    return atomicJson(path, updated, error);
}

bool recordResult(const QString &root, const WorkDefinition &work, const StageRunResult &result,
    WorkProgress *progress, QString *error)
{
    if (!hashId(work.revision) || computeRevision(work) != work.revision || safeId(result.runId).isEmpty()
        || result.workId != work.id || result.revision != work.revision || result.entryId.isEmpty()
        || result.runId.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("ScenarioWork", "result identity mismatch");
        return false;
    }
    int entryIndex = -1;
    for (int i = 0; i < work.entries.size(); ++i) {
        if (work.entries.at(i).id == result.entryId) {
            entryIndex = i;
            break;
        }
    }
    if (entryIndex < 0) {
        if (error)
            *error = QCoreApplication::translate("ScenarioWork", "result entry does not exist");
        return false;
    }
    WorkProgress local;
    if (!loadProgress(root, work, &local, error))
        return false;
    if (progress)
        *progress = local;
    // Trial, failed, and aborted runs never alter durable progress.
    if (result.trial || !result.success || result.aborted)
        return true;
    if (local.processedRunIds.contains(result.runId))
        return true;
    if (!canPlayEntry(work, local, result.entryId))
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Entry is not available."));
    QStringList carryErrors;
    if (!validateCarryState(result.carry, &carryErrors)) {
        if (error)
            *error = carryErrors.join(QStringLiteral("; "));
        return false;
    }
    local.processedRunIds << result.runId;
    if (!local.completedEntryIds.contains(result.entryId))
        local.completedEntryIds << result.entryId;
    if (entryIndex + 1 < work.entries.size()) {
        local.continuationEntryId = work.entries.at(entryIndex + 1).id;
        local.snapshots << ProgressSnapshot { QUuid::createUuid().toString().mid(1, 36),
            result.entryId, work.entries.at(entryIndex + 1).id, QDateTime::currentDateTimeUtc(),
            result.carry };
    } else {
        local.continuationEntryId.clear();
    }
    if (!saveProgress(root, local, error))
        return false;
    if (progress)
        *progress = local;
    return true;
}

bool parseLegacyScene(const QString &text, LegacySceneDocument *document, QString *error)
{
    if (!document || text.toUtf8().size() > MaxBytes)
        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene document."));
    LegacySceneDocument parsed;
    bool pile = false;
    for (const auto &raw : text.split(QRegularExpression(QStringLiteral("[\\r\\n]")))) {
        const auto line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith(QStringLiteral("extraOptions:"))) {
            parsed.extraOptions += line.mid(13).split(' ', Qt::SkipEmptyParts);
            continue;
        }
        if (line.startsWith(QStringLiteral("setPile:"))) {
            if (pile)
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Duplicate fixed pile."));
            pile = true;
            const auto value = line.mid(8);
            if (!value.isEmpty())
                for (const auto &token : value.split(',', Qt::KeepEmptyParts)) {
                    bool ok = false;
                    const int id = token.toInt(&ok);
                    if (!ok || id < 0)
                        return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid fixed pile card."));
                    parsed.fixedPile.append(id);
                }
            continue;
        }
        QMap<QString, QString> fields;
        const auto tokens
            = line.contains('|') ? line.split('|', Qt::SkipEmptyParts) : line.split(' ', Qt::SkipEmptyParts);
        for (const auto &token : tokens) {
            const int colon = token.indexOf(':');
            if (colon <= 0)
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene field."));
            const auto key = token.left(colon), value = token.mid(colon + 1);
            if (fields.contains(key) || key.contains(QRegularExpression(QStringLiteral("[\\s,|*]")))
                || value.contains(QRegularExpression(QStringLiteral("[\\s:|]"))))
                return fail(error, QT_TRANSLATE_NOOP("ScenarioWork", "Invalid or repeated scene field."));
            fields.insert(key, value);
        }
        parsed.players.append(fields);
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            parsed.fields.insert(it.key(), it.value());
    }
    QStringList errors;
    if (!validateLegacySceneShape(parsed, &errors)) {
        if (error)
            *error = errors.join(QStringLiteral("; "));
        return false;
    }
    *document = parsed;
    return true;
}

QString serializeLegacyScene(const LegacySceneDocument &d)
{
    QStringList out;
    if (!d.extraOptions.isEmpty())
        out << QStringLiteral("extraOptions:") + d.extraOptions.join(' ');
    if (!d.fixedPile.isEmpty()) {
        QStringList p;
        for (int id : d.fixedPile)
            p << QString::number(id);
        out << QStringLiteral("setPile:") + p.join(',');
    }
    for (const auto &p : d.players) {
        QStringList f;
        for (auto it = p.cbegin(); it != p.cend(); ++it)
            f << it.key() + ':' + it.value();
        out << f.join(' ');
    }
    return out.join('\n') + "\n";
}
bool validateLegacySceneShape(const LegacySceneDocument &d, QStringList *errors)
{
    QStringList e;
    auto add = [&](const char *message) { e.append(QCoreApplication::translate("ScenarioWork", message)); };
    if (d.players.size() < 2 || d.players.size() > 10)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene needs 2 to 10 players."));
    int starters = 0, lords = 0;
    QSet<QString> camps;
    QSet<int> cards;
    auto takeCard = [&](const QString &token) {
        bool ok = false;
        const int id = token.toInt(&ok);
        if (!ok || id < 0 || cards.contains(id))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid or duplicate physical card."));
        else
            cards.insert(id);
    };
    for (int id : d.fixedPile)
        takeCard(QString::number(id));
    for (const auto &player : d.players) {
        if (player.value("general").isEmpty())
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Player general is required."));
        if (player.contains("starter")) {
            if (player.value("starter") == "true")
                ++starters;
            else if (player.value("starter") != "false")
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Starter must be boolean."));
        }
        const auto role = player.value("role");
        if (!QStringList { "lord", "loyalist", "rebel", "renegade" }.contains(role))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Unsupported player role."));
        if (role == "lord")
            ++lords;
        camps.insert(role == "lord" || role == "loyalist" ? QStringLiteral("loyal") : role);
        for (const QString key : { "draw", "hpadj" }) {
            if (!player.contains(key))
                continue;
            bool ok = false;
            const int value = player.value(key).toInt(&ok);
            if (!ok || value < (key == "draw" ? 0 : -999) || value > 999)
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid draw count or health adjustment."));
        }
        for (const QString key : { "turned", "chained" }) {
            if (player.contains(key) && player.value(key) != "true" && player.value(key) != "false")
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene flags must be boolean."));
        }
        for (const QString key : { "hp", "maxhp", "hujia" }) {
            if (!player.contains(key))
                continue;
            bool ok = false;
            const int n = player.value(key).toInt(&ok);
            if (!ok || n < (key == "hujia" ? 0 : 1) || n > 999)
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene health value."));
        }
        if (player.contains("hp") && player.contains("maxhp")
            && player.value("hp").toInt() > player.value("maxhp").toInt())
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene health exceeds maximum health."));
        for (const QString key : { "card", "hand", "equip", "judge" }) {
            if (!player.contains(key) || player.value(key).isEmpty())
                continue;
            for (const auto &token : player.value(key).split(',', Qt::KeepEmptyParts))
                takeCard(token);
        }
        for (const QString key : { "marks", "equipArea" }) {
            if (!player.contains(key) || player.value(key).isEmpty())
                continue;
            QSet<QString> names;
            for (const auto &token : player.value(key).split(',', Qt::KeepEmptyParts)) {
                const auto parts = token.split('*', Qt::KeepEmptyParts);
                if (parts.size() != 2 || parts[0].isEmpty()) {
                    add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene mark or equipment capacity."));
                    continue;
                }
                bool ok = false;
                const int count = parts[1].toInt(&ok);
                if (!ok || count < 0 || names.contains(parts[0]))
                    add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid scene mark or equipment capacity."));
                names.insert(parts[0]);
                if (key == "equipArea") {
                    const int slot = parts[0].toInt(&ok);
                    if (!ok || slot < 0 || slot > 4 || count > 99)
                        add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid equipment capacity."));
                }
            }
        }
    }
    if (starters != 1)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene needs exactly one starter."));
    if (lords > 1 || camps.size() < 2)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Scene requires opposing camps and at most one lord."));
    int endings = 0;
    for (const QString key : { "endedByPile", "singleTurn", "beforeNext" })
        if (d.fields.contains(key))
            ++endings;
    QSet<QString> optionKeys;
    for (const auto &option : d.extraOptions) {
        const int colon = option.indexOf(':');
        const auto key = colon < 0 ? option : option.left(colon);
        if (optionKeys.contains(key))
            add(QT_TRANSLATE_NOOP("ScenarioWork", "Repeated legacy option."));
        optionKeys.insert(key);
        if (key == "beforeStartRound" || key == "afterRound") {
            ++endings;
            bool ok = false;
            const int count = option.mid(colon + 1).toInt(&ok);
            if (colon < 0 || !ok || count < 1 || count > 999)
                add(QT_TRANSLATE_NOOP("ScenarioWork", "Invalid legacy round count."));
        }
    }
    if (endings > 1)
        add(QT_TRANSLATE_NOOP("ScenarioWork", "Conflicting legacy endings."));
    if (errors)
        *errors = e;
    return e.isEmpty();
}

} // namespace ScenarioWork
