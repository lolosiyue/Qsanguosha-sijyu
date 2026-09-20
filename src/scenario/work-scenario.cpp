#include "work-scenario.h"
#include "engine.h"
#include "general.h"
#include "package-catalog.h"
#include "room.h"
#include "rules-content-manifest.h"
#include "runtime-paths.h"
#include "serverplayer.h"
#include "standard.h"
#include "version.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace {
QString runtimeText(const char *source) { return QCoreApplication::translate("ScenarioWorkRuntime", source); }
const ScenarioWork::SceneDefinition *findScene(const ScenarioWork::WorkLaunch &launch, QString *entryId)
{
    for (const auto &entry : launch.work.entries) {
        if (entry.id != launch.entryId)
            continue;
        for (const auto &scene : launch.work.scenes) {
            if (scene.id == entry.sceneId && scene.revision == entry.sceneRevision) {
                *entryId = entry.id;
                return &scene;
            }
        }
    }
    return nullptr;
}

bool mergeCarry(const ScenarioWork::WorkLaunch &launch, ScenarioWork::SceneDefinition *scene, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    ScenarioWork::LegacySceneDocument document;
    if (!ScenarioWork::parseLegacyScene(scene->setup, &document, error))
        return false;
    if (scene->playerSeat < 0 || scene->playerSeat >= document.players.size())
        return fail(
            runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Work player seat is outside scene seats")));
    for (const QString &option : document.extraOptions)
        if (option.section(':', 0, 0) == QStringLiteral("randomRoles"))
            return fail(
                runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Work seats cannot use randomRoles")));
    const auto &policy = launch.work.carry;
    const auto &values = launch.carry.values;
    QStringList errors;
    if (!ScenarioWork::validateCarryState(launch.carry, &errors))
        return fail(errors.join("; "));
    QSet<QString> allowed;
    if (policy.hp)
        allowed << "hp";
    if (policy.maxhp)
        allowed << "maxhp";
    if (policy.hujia)
        allowed << "hujia";
    if (policy.generals)
        allowed << "general" << "general2";
    if (policy.hand)
        allowed << "hand";
    if (policy.equip)
        allowed << "equip";
    if (!policy.marks.isEmpty())
        allowed << "marks";
    if (!policy.skills.isEmpty())
        allowed << "acquiredSkills";
    for (const QString &key : values.keys())
        if (!allowed.contains(key))
            return fail(runtimeText(
                QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Carry key is not allowed by policy: %1"))
                    .arg(key));
    auto &human = document.players[scene->playerSeat];
    for (const QString &key : { QStringLiteral("hp"), QStringLiteral("maxhp"), QStringLiteral("hujia") }) {
        if (!values.contains(key))
            continue;
        const auto value = values.value(key);
        const int number = value.toInt(-1);
        if (!value.isDouble() || value.toDouble() != number || number < (key == "hujia" ? 0 : 1)
            || number > 9999)
            return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid carry %1")).arg(key));
        human[key] = QString::number(number);
        if (key == "maxhp")
            human.remove("hpadj");
    }
    for (const QString &key : { QStringLiteral("general"), QStringLiteral("general2") }) {
        if (!values.contains(key))
            continue;
        const QString name = values.value(key).toString();
        if (!values.value(key).isString() || (name.isEmpty() && key == "general")
            || (!name.isEmpty() && !Sanguosha->getGeneral(name)))
            return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Unknown carried general: %1"))
                    .arg(name));
        if (key == "general2" && !name.isEmpty() && !launch.work.rules.value("secondGeneral").toBool())
            return fail(runtimeText(QT_TRANSLATE_NOOP(
                "ScenarioWorkRuntime", "Second-general carry requires secondGeneral rules")));
        human[key] = name;
    }
    // Overlay only named marks/skills; next scene's independent values remain.
    if (values.contains("marks")) {
        QMap<QString, int> marks;
        for (const QString &text : human.value("marks").split(',', Qt::SkipEmptyParts))
            marks[text.section('*', 0, 0)] = text.section('*', 1, 1).toInt();
        QSet<QString> seen;
        for (const auto &value : values.value("marks").toArray()) {
            const auto mark = value.toObject();
            const QString name = mark.value("name").toString();
            const auto number = mark.value("value");
            if (!policy.marks.contains(name) || seen.contains(name) || !number.isDouble()
                || number.toDouble() != number.toInt() || number.toInt() < 0)
                return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid carried mark: %1"))
                        .arg(name));
            seen << name;
            marks[name] = number.toInt();
        }
        QStringList list;
        for (auto it = marks.cbegin(); it != marks.cend(); ++it)
            list << it.key() + '*' + QString::number(it.value());
        human["marks"] = list.join(',');
    }
    if (values.contains("acquiredSkills")) {
        QStringList skills = human.value("acquireSkills").split(',', Qt::SkipEmptyParts);
        for (const QString &name : policy.skills)
            skills.removeAll(name);
        for (const auto &value : values.value("acquiredSkills").toArray()) {
            const QString name = value.toString();
            if (!value.isString() || !policy.skills.contains(name) || !Sanguosha->getSkill(name))
                return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid carried skill: %1"))
                        .arg(name));
            if (!skills.contains(name))
                skills << name;
        }
        human["acquireSkills"] = skills.join(',');
    }
    QSet<int> carried;
    for (const QString &key : { QStringLiteral("hand"), QStringLiteral("equip") }) {
        if (!values.contains(key))
            continue;
        QStringList ids;
        QMap<int, int> slotCounts;
        QMap<int, int> capacities;
        for (int i = 0; i < 5; ++i)
            capacities[i] = 1;
        for (const QString &area : human.value("equipArea").split(',', Qt::SkipEmptyParts))
            capacities[area.section('*', 0, 0).toInt()] = area.section('*', 1, 1).toInt();
        for (const auto &value : values.value(key).toArray()) {
            const int id = value.toInt(-1);
            const Card *card = id >= 0 && id < Sanguosha->getCardCount() ? Sanguosha->getCard(id) : nullptr;
            if (!value.isDouble() || value.toDouble() != id || !card || carried.contains(id))
                return fail(runtimeText(
                    QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid or repeated carried card")));
            if (key == "equip") {
                const auto *equip = qobject_cast<const EquipCard *>(card);
                if (!equip)
                    return fail(runtimeText(QT_TRANSLATE_NOOP(
                        "ScenarioWorkRuntime", "Carried equipment exceeds next scene slots")));
                for (int slot : equip->getOccupyLocations())
                    if (++slotCounts[slot] > capacities.value(slot))
                        return fail(runtimeText(QT_TRANSLATE_NOOP(
                            "ScenarioWorkRuntime", "Carried equipment exceeds next scene slots")));
            }
            carried << id;
            ids << QString::number(id);
        }
        human[key] = ids.join(',');
        if (key == "hand")
            human["draw"] = "0";
    }
    // A physical card cannot also be explicitly owned by another seat/zone.
    for (int seat = 0; seat < document.players.size(); ++seat) {
        const auto &row = document.players[seat];
        for (const QString &zone :
            { QStringLiteral("hand"), QStringLiteral("equip"), QStringLiteral("judge") }) {
            if (seat == scene->playerSeat && values.contains(zone))
                continue;
            for (const QString &text : row.value(zone).split(',', Qt::SkipEmptyParts)) {
                bool ok = false;
                const int id = text.toInt(&ok);
                if (ok && carried.contains(id))
                    return fail(runtimeText(QT_TRANSLATE_NOOP(
                        "ScenarioWorkRuntime", "Carried card conflicts with next scene ownership")));
            }
        }
    }
    for (int id : carried)
        document.fixedPile.removeAll(id);
    // Carry must form another valid scene, not bypass the model's shape gate.
    if (!ScenarioWork::validateLegacySceneShape(document, &errors))
        return fail(errors.join("; "));
    QSet<int> ownedCards;
    for (auto &row : document.players) {
        // The legacy loader treats nonempty flags as true; preserve explicit
        // false values by removing them after the typed work validation.
        for (const QString &key :
            { QStringLiteral("turned"), QStringLiteral("chained"), QStringLiteral("starter") })
            if (row.value(key) == "false")
                row.remove(key);
        for (const QString &key :
            { QStringLiteral("general"), QStringLiteral("general2"), QStringLiteral("general3") }) {
            const QString name = row.value(key);
            if (key == "general2" && !name.isEmpty() && !launch.work.rules.value("secondGeneral").toBool())
                return fail(runtimeText(QT_TRANSLATE_NOOP(
                    "ScenarioWorkRuntime", "Scene second general requires secondGeneral rules")));
            if (!name.isEmpty() && name != "select" && !Sanguosha->getGeneral(name))
                return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Unknown scene general: %1"))
                        .arg(name));
        }
        for (const QString &name : row.value("acquireSkills").split(',', Qt::SkipEmptyParts))
            if (!Sanguosha->getSkill(name))
                return fail(runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Unknown scene skill: %1"))
                        .arg(name));
        int maximum = row.value("maxhp").toInt();
        bool maximumKnown = !row.value("maxhp").isEmpty();
        if (row.value("maxhp").isEmpty()) {
            const auto *primary = Sanguosha->getGeneral(row.value("general"));
            QString secondaryName = row.value("general2");
            if (secondaryName == row.value("general"))
                secondaryName = row.value("general3");
            const auto *secondary = Sanguosha->getGeneral(secondaryName);
            if (primary && (secondaryName.isEmpty() || secondary)) {
                maximum = QSanWorks::sceneGeneralHealth(
                    primary, secondary, row.value("role") == "lord" && document.players.size() > 4, true);
                maximumKnown = true;
            }
        }
        maximum += row.value("hpadj").toInt();
        if ((maximumKnown && (maximum <= 0 || maximum > 999))
            || (!maximumKnown && (!row.value("hp").isEmpty() || row.value("hpadj").toInt() != 0))
            || (!row.value("hp").isEmpty() && row.value("hp").toInt() > maximum))
            return fail(runtimeText(QT_TRANSLATE_NOOP(
                "ScenarioWorkRuntime", "Scene health exceeds or cannot resolve maximum health")));
        QMap<int, int> capacities, slotCounts;
        for (int slot = 0; slot < 5; ++slot)
            capacities[slot] = 1;
        for (const QString &area : row.value("equipArea").split(',', Qt::SkipEmptyParts))
            capacities[area.section('*', 0, 0).toInt()] = area.section('*', 1, 1).toInt();
        for (const QString &zone :
            { QStringLiteral("hand"), QStringLiteral("equip"), QStringLiteral("judge") }) {
            for (const QString &text : row.value(zone).split(',', Qt::SkipEmptyParts)) {
                bool ok = false;
                const int id = text.toInt(&ok);
                // Published works use exact physical IDs, never installEquip's
                // permissive name fallback, so identity is fingerprint-bound.
                const Card *card
                    = ok && id >= 0 && id < Sanguosha->getCardCount() ? Sanguosha->getCard(id) : nullptr;
                if (!card || ownedCards.contains(id)
                    || (zone == "equip" && !qobject_cast<const EquipCard *>(card))
                    || (zone == "judge" && !card->isKindOf("DelayedTrick")))
                    return fail(runtimeText(
                        QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid or repeated scene card: %1"))
                            .arg(text));
                ownedCards << id;
                if (zone == "equip") {
                    const auto *equip = qobject_cast<const EquipCard *>(card);
                    for (int slot : equip->getOccupyLocations())
                        if (++slotCounts[slot] > capacities.value(slot))
                            return fail(runtimeText(QT_TRANSLATE_NOOP(
                                "ScenarioWorkRuntime", "Scene equipment exceeds available slots")));
                }
            }
        }
    }
    for (int id : document.fixedPile)
        if (id < 0 || id >= Sanguosha->getCardCount() || !Sanguosha->getCard(id))
            return fail(
                runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Invalid scene draw-pile card")));
    scene->setup = ScenarioWork::serializeLegacyScene(document);
    return true;
}
}

namespace QSanWorks {
int sceneGeneralHealth(const General *primary, const General *secondary, bool welfare, bool maximum)
{
    if (!primary)
        return 0;
    int value = maximum ? primary->getMaxHp() : primary->getStartHp();
    // Work mini-identity always uses legacy mini's minimum dual-general rule,
    // independent of the user's normal-game Config.MaxHpScheme.
    if (secondary)
        value = qMax(1, qMin(value, maximum ? secondary->getMaxHp() : secondary->getStartHp()));
    return value + (welfare ? 1 : 0);
}
QJsonObject currentCompatibility()
{
    QJsonObject result { { "engineVersion", QString::fromLatin1(QSanVersion::Number) } };
    if (!Sanguosha)
        return result;
    const auto &manifest = Sanguosha->rulesContentManifest();
    result.insert("manifest", QSanRules::runtimeContentDigest(manifest));
    QStringList extensionNames;
    for (const auto &entry : manifest.entries)
        extensionNames << entry.name;
    extensionNames.removeDuplicates();
    extensionNames.sort();
    result.insert("extensions", QJsonArray::fromStringList(extensionNames));
    QCryptographicHash content(QCryptographicHash::Sha256);
    bool readable = manifest.isValid();
    QStringList files = QSanRules::manifestHashedFiles(manifest);
    files << "lua/config.lua" << "lua/sanguosha.lua";
    files.removeDuplicates();
    files.sort();
    for (const QString &name : files) {
        const QString path = name.startsWith("package://")
            ? QSanPackages::resolve(QSanRuntimePaths::assetRoot(), name)
            : QSanRuntimePaths::assetPath(name);
        QFile file(path);
        content.addData(name.toUtf8());
        content.addData(QByteArray(1, '\0'));
        if (file.open(QIODevice::ReadOnly))
            content.addData(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
        else {
            readable = false;
            content.addData(QByteArrayLiteral("MISSING"));
        }
    }
    result.insert("content", QString::fromLatin1(content.result().toHex()));
    result.insert("contentReadable", readable);
    QJsonArray cards;
    for (int i = 0; i < Sanguosha->getCardCount(); ++i) {
        const Card *card = Sanguosha->getCard(i);
        if (card)
            cards.append(
                QJsonObject { { "id", i }, { "name", card->objectName() }, { "package", card->getPackage() },
                    { "suit", int(card->getSuit()) }, { "number", card->getNumber() } });
    }
    result.insert("cards",
        QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(cards).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
                .toHex()));
    return result;
}

bool validateWorkForRuntime(const ScenarioWork::WorkDefinition &work, QString *error)
{
    QStringList errors;
    if (!ScenarioWork::validateWork(work, &errors)) {
        if (error)
            *error = errors.join("; ");
        return false;
    }
    if (work.revision.isEmpty() || work.revision != ScenarioWork::computeRevision(work)) {
        if (error)
            *error = runtimeText(
                QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Work revision does not match its contents"));
        return false;
    }
    for (auto it = work.rules.constBegin(); it != work.rules.constEnd(); ++it) {
        if ((it.key() != "secondGeneral" && it.key() != "fixedSeats") || !it.value().isBool()
            || (it.key() == "fixedSeats" && !it.value().toBool())) {
            if (error)
                *error = runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Unsupported work rule: %1"))
                             .arg(it.key());
            return false;
        }
    }
    const auto compatibility = currentCompatibility();
    if (!Sanguosha || !compatibility.value("contentReadable").toBool() || work.compatibility.isEmpty()
        || work.compatibility != compatibility) {
        if (error)
            *error = runtimeText(QT_TRANSLATE_NOOP(
                "ScenarioWorkRuntime", "Work compatibility fingerprint does not match this engine"));
        return false;
    }
    for (const auto &scene : work.scenes) {
        ScenarioWork::WorkLaunch launch;
        launch.work = work;
        auto copy = scene;
        if (!mergeCarry(launch, &copy, error))
            return false;
    }
    return true;
}

WorkScenario::WorkScenario(const QSharedPointer<const ScenarioWork::WorkLaunch> &launch)
    : MiniScene(QStringLiteral("_work_%1").arg(launch ? launch->work.id : QStringLiteral("invalid")))
    , m_launch(launch)
{
    if (!launch) {
        m_error = runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Missing work launch"));
        return;
    }
    if (!validateWorkForRuntime(launch->work, &m_error))
        return;
    const auto *scene = findScene(*launch, &m_entryId);
    if (!scene) {
        m_error = runtimeText(
            QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Work entry does not resolve to its scene revision"));
        return;
    }
    m_playerSeat = scene->playerSeat;
    for (const auto &entry : launch->work.entries)
        if (entry.id == m_entryId && !entry.goals.isEmpty()) {
            m_goal = entry.goals.first();
            m_hasGoal = true;
        }
    if (!m_hasGoal && !scene->goals.isEmpty()) {
        m_goal = scene->goals.first();
        m_hasGoal = true;
    }
    // Legacy/default scenes settle on the bound human's ordinary victory.
    if (!m_hasGoal) {
        m_goal = ScenarioWork::GoalDefinition { };
        m_hasGoal = true;
    }
    auto setup = *scene;
    if (!mergeCarry(*launch, &setup, &m_error))
        return;
    auto *mini = dynamic_cast<MiniSceneRule *>(getRule());
    if (!mini) {
        m_error = runtimeText(QT_TRANSLATE_NOOP("ScenarioWorkRuntime", "Work scene rule is unavailable"));
        return;
    }
    mini->loadSetting(setup, &m_error);
}

void WorkScenario::bindPlayers(Room *room) const
{
    const auto seats = room->getPlayers();
    for (int i = 0; i < seats.size(); ++i)
        seats[i]->setTag("WorkSeat", i);
}
QList<ServerPlayer *> WorkScenario::players(Room *room) const
{
    QList<ServerPlayer *> seats;
    for (int i = 0; i < room->getPlayers().size(); ++i)
        seats.append(nullptr);
    for (auto *player : room->getPlayers()) {
        const auto value = player->getTag("WorkSeat");
        const int seat = value.toInt();
        if (value.isValid() && seat >= 0 && seat < seats.size())
            seats[seat] = player;
    }
    return seats;
}
ScenarioWork::GoalEvaluation WorkScenario::evaluate(Room *room) const
{
    QList<ScenarioWork::SeatState> state;
    for (auto *player : players(room)) {
        ScenarioWork::SeatState seat;
        if (player) {
            seat.alive = player->isAlive();
            seat.hp = player->getHp();
            for (const QString &mark : player->getMarkNames())
                seat.marks[mark] = player->getMark(mark);
        }
        state << seat;
    }
    return ScenarioWork::evaluateGoals(m_goal, state, room->getTag("WorkCompletedHumanTurns").toInt());
}
ScenarioWork::CarryState WorkScenario::captureCarry(Room *room) const
{
    ScenarioWork::CarryState state;
    const auto *human = players(room).value(m_playerSeat);
    if (!human || !human->isAlive())
        return state;
    const auto &policy = m_launch->work.carry;
    auto &out = state.values;
    if (policy.hp)
        out["hp"] = human->getHp();
    if (policy.maxhp)
        out["maxhp"] = human->getMaxHp();
    if (policy.hujia)
        out["hujia"] = human->getMark("@HuJia");
    if (policy.generals) {
        out["general"] = human->getGeneralName();
        out["general2"] = human->getGeneral2Name();
    }
    if (policy.hand) {
        QJsonArray cards;
        for (const auto *card : human->getHandcards())
            cards.append(card->getEffectiveId());
        out["hand"] = cards;
    }
    if (policy.equip) {
        QJsonArray cards;
        for (const auto *card : human->getEquips())
            cards.append(card->getEffectiveId());
        out["equip"] = cards;
    }
    if (!policy.marks.isEmpty()) {
        QJsonArray marks;
        for (const auto &name : policy.marks)
            marks.append(QJsonObject { { "name", name }, { "value", human->getMark(name) } });
        out["marks"] = marks;
    }
    if (!policy.skills.isEmpty()) {
        QJsonArray skills;
        for (const auto &name : policy.skills)
            if (human->getAcquiredSkills().contains(name))
                skills.append(name);
        out["acquiredSkills"] = skills;
    }
    return state;
}
}
