// Headless skill scenario host: builds a real Room from a JSON case, fires the
// case's Lua trigger once the board is set, and writes a JSON report of the
// resulting history facts and final board.  Driven by
// tools/autotest/skill_scenario_runner.py; see tools/autotest/README.md.
#include "engine-bootstrap.h"
#include "engine.h"
#include "general.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "miniscenarios.h"
#include "room.h"
#include "roomthread.h"
#include "runtime-paths.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

namespace {

const QString kScenarioName = QStringLiteral("skill_scenario");

QString cardSpec(const Card *card)
{
    return card ? QStringLiteral("%1/%2/%3").arg(card->objectName(), card->getSuitString())
                      .arg(card->getNumber())
                : QString();
}

// A spec is "name", "name/suit" or "name/suit/number"; number may also be A/J/Q/K.
bool matchesSpec(const Card *card, const QString &spec)
{
    const QStringList parts = spec.split(QLatin1Char('/'));
    if (!card || card->objectName() != parts.value(0)) return false;
    const QString suit = parts.value(1), number = parts.value(2);
    if (!suit.isEmpty() && card->getSuitString() != suit) return false;
    return number.isEmpty() || number == QString::number(card->getNumber())
        || number == card->getNumberString();
}

bool isCardId(const QJsonValue &value, int *id)
{
    if (value.isDouble()) { *id = value.toInt(); return true; }
    bool ok = false;
    *id = value.toString().toInt(&ok);
    return ok;
}

// Setup cards come from the room's deck catalogue; each spec takes a distinct card.
int resolveSetupCard(const QJsonValue &value, const QList<int> &pool, QSet<int> &used, QString *error)
{
    int id = -1;
    if (isCardId(value, &id)) {
        used.insert(id);
        return id;
    }
    const QString spec = value.toString();
    for (int candidate : pool) {
        if (!used.contains(candidate) && matchesSpec(Sanguosha->getEngineCard(candidate), spec)) {
            used.insert(candidate);
            return candidate;
        }
    }
    *error = QStringLiteral("no unused deck card matches '%1'").arg(spec);
    return -1;
}

// Answer cards prefer one a player already holds, then the draw pile.
int resolveRoomCard(Room *room, const QString &spec)
{
    int fallback = -1;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        if (!matchesSpec(Sanguosha->getEngineCard(id), spec)) continue;
        if (room->getCardOwner(id)) return id;
        if (fallback < 0 && room->getCardPlace(id) == Player::DrawPile) fallback = id;
    }
    return fallback;
}

Room *g_triggerRoom = nullptr;
bool g_hegemony = false;

int luaEnterPlayUse(lua_State *)
{
    RoomState &state = g_triggerRoom->roomRuntime()->state();
    state.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    state.setCurrentCardUsePattern(QString());
    return 0;
}

// __use_selected(card_id, from_name, {target_names}): a use as a client/AI selection
// submits it, so target revalidation and the hegemony TargetMod reveal plan run.
int luaUseSelected(lua_State *L)
{
    CardUseStruct use;
    use.card = Sanguosha->getCard(int(luaL_checkinteger(L, 1)));
    use.from = g_triggerRoom->findPlayerByObjectName(QString::fromUtf8(luaL_checkstring(L, 2)), true);
    luaL_checktype(L, 3, LUA_TTABLE);
    for (lua_Integer i = 1; lua_rawgeti(L, 3, i) == LUA_TSTRING; ++i) {
        use.to << g_triggerRoom->findPlayerByObjectName(QString::fromUtf8(lua_tostring(L, -1)), true);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    use.m_validateTargets = true;
    lua_pushboolean(L, use.card && use.from && g_triggerRoom->useCard(use, true));
    return 1;
}

QString luaPrelude(const QStringList &seatNames)
{
    QStringList seats;
    for (const QString &name : seatNames)
        seats << QStringLiteral("room:findPlayerByObjectName(\"%1\", true)").arg(name);
    return QStringLiteral(
        "room = R\n"
        "P = {%1}\n"
        "local function spec_match(c, spec)\n"
        "  local name, suit, number = spec:match('^([^/]+)/?([^/]*)/?([^/]*)$')\n"
        "  if not c or c:objectName() ~= name then return false end\n"
        "  if suit ~= '' and c:getSuitString() ~= suit then return false end\n"
        "  return number == '' or number == tostring(c:getNumber()) or number == c:getNumberString()\n"
        "end\n"
        "function card(spec)\n"
        "  if type(spec) == 'number' then return sgs.Sanguosha:getCard(spec) end\n"
        "  local fallback\n"
        "  for id = 0, sgs.Sanguosha:getCardCount() - 1 do\n"
        "    if spec_match(sgs.Sanguosha:getEngineCard(id), spec) then\n"
        "      if room:getCardOwner(id) then return sgs.Sanguosha:getCard(id) end\n"
        "      if not fallback and room:getCardPlace(id) == sgs.Player_DrawPile then fallback = id end\n"
        "    end\n"
        "  end\n"
        "  if fallback then return sgs.Sanguosha:getCard(fallback) end\n"
        "  error('no card matches ' .. spec, 2)\n"
        "end\n"
        // Level 3 blames the trigger line that called use() or activate().
        "local function use_card(c, from, targets)\n"
        "  if type(c) ~= 'userdata' then c = card(c) end\n"
        "  local u = sgs.CardUseStruct()\n"
        "  u.card = c\n"
        "  u.from = from\n"
        "  for _, t in ipairs(targets or {}) do u.to:append(t) end\n"
        "  if not room:useCard(u, true) then error('useCard rejected ' .. c:toString(), 3) end\n"
        "end\n"
        "function use(c, from, targets) use_card(c, from, targets) end\n"
        // Binds a skill card to the player's live skill instance, as an activation from the UI would.
        "function skillcard(class_name, skill, owner, subcards)\n"
        "  local c = sgs.Sanguosha:cloneSkillCard(class_name)\n"
        "  if not c then error('unknown skill card ' .. class_name, 2) end\n"
        "  for _, spec in ipairs(subcards or {}) do c:addSubcard(card(spec)) end\n"
        "  local ctx = room:getAiSkillActionContext(owner, skill)\n"
        "  if ctx:isValid() then\n"
        "    c:setActivationSkill(ctx:getActivationSkillName(), ctx:getActivationInstanceId())\n"
        "    c:setSourceSkill(ctx:getSourceSkillName(), ctx:getSourceInstanceID())\n"
        "  end\n"
        "  return c\n"
        "end\n"
        // Uses a card as a play-phase activation: the state PlayerDecisionService::activate sets.
        "local function enter_play(from)\n"
        "  room:setCurrent(from)\n"
        "  if from:getPhase() ~= sgs.Player_Play then\n"
        "    from:setPhase(sgs.Player_Play)\n"
        "    room:broadcastProperty(from, 'phase')\n"
        "  end\n"
        "  __enter_play_use()\n"
        "end\n"
        "function activate(c, from, targets)\n"
        "  enter_play(from)\n"
        "  use_card(c, from, targets)\n"
        "end\n"
        // Same, but submitted as a selection: targets and limits are revalidated.
        "function select_use(c, from, targets)\n"
        "  if type(c) ~= 'userdata' then c = card(c) end\n"
        "  enter_play(from)\n"
        "  local names = {}\n"
        "  for _, t in ipairs(targets or {}) do table.insert(names, t:objectName()) end\n"
        "  if not __use_selected(c:getEffectiveId(), from:objectName(), names) then\n"
        "    error('useCard rejected ' .. c:toString(), 2)\n"
        "  end\n"
        "end\n").arg(seats.join(QStringLiteral(", ")));
}

QVariantList cardSpecs(const QList<int> &ids)
{
    QVariantList specs;
    for (int id : ids) specs << cardSpec(Sanguosha->getEngineCard(id));
    return specs;
}

// Reads every page after the setup watermark; the room thread has stopped by now.
QVariantList factsAfter(Room *room, const QVariant &watermark, bool actualDamage)
{
    QVariantList items;
    QVariantMap filter{{QStringLiteral("limit"), 100}};
    if (watermark.isValid()) filter.insert(QStringLiteral("after"), watermark);
    for (;;) {
        const QVariantMap page = actualDamage ? room->queryActualDamage(filter) : room->queryHistoryFacts(filter);
        if (page.contains(QStringLiteral("error"))) {
            items << QVariantMap{{QStringLiteral("query_error"), page.value(QStringLiteral("error"))}};
            break;
        }
        items += page.value(QStringLiteral("items")).toList();
        if (!page.value(QStringLiteral("has_more")).toBool()) break;
        filter.insert(QStringLiteral("after"), page.value(QStringLiteral("next_after")));
        filter.insert(QStringLiteral("watermark"), page.value(QStringLiteral("watermark")));
    }
    return items;
}

// Runs on the room thread: once the worker stops, shutdown releases room tags and cards.
QVariantMap buildReport(Room *room, const QStringList &names, const QVariant &watermark, const QString &error)
{
    QVariantList seats;
    for (int index = 0; index < names.size(); ++index) {
        ServerPlayer *player = room->findPlayerByObjectName(names.at(index), true);
        if (!player) continue;
        QVariantMap marks, piles;
        for (const QString &mark : player->getMarkNames())
            if (player->getMark(mark) != 0) marks.insert(mark, player->getMark(mark));
        for (const QString &pile : player->getPileNames())
            piles.insert(pile, cardSpecs(player->getPile(pile)));
        QVariantList skills;
        for (const Skill *skill : player->getVisibleSkillList()) skills << skill->objectName();
        seats << QVariantMap{
            {"seat", index + 1}, {"name", player->objectName()}, {"general", player->getGeneralName()},
            {"general2", player->getGeneral2Name()}, {"role", player->getRole()}, {"hp", player->getHp()},
            {"maxhp", player->getMaxHp()}, {"alive", player->isAlive()}, {"chained", player->isChained()},
            {"faceup", player->faceUp()}, {"hand", cardSpecs(player->handCards())},
            {"equip", cardSpecs(player->getEquipsId())}, {"judge", cardSpecs(player->getJudgingAreaID())},
            {"marks", marks}, {"piles", piles}, {"skills", skills}};
    }
    return QVariantMap{
        {"error", error.isEmpty() ? QVariant() : QVariant(error)},
        {"seats", seats}, {"draw_pile_top", cardSpecs(room->getDrawPile().mid(0, 5))},
        {"facts", factsAfter(room, watermark, false)}, {"damage", factsAfter(room, watermark, true)}};
}

// Filled by the room thread before it finishes; read by main once workers stop.
QVariantMap g_report;

class SkillScenarioRule : public MiniSceneRule
{
public:
    SkillScenarioRule(Scenario *scenario, const QList<QMap<QString, QString>> &seats,
                      const QList<int> &pileTopFirst, const QJsonArray &script, const QString &trigger)
        : MiniSceneRule(scenario), m_script(script), m_trigger(trigger)
    {
        players = seats;
        QStringList ids;
        // setPile prepends each card in turn, so the last one listed ends on top.
        for (auto it = pileTopFirst.crbegin(); it != pileTopFirst.crend(); ++it)
            ids << QString::number(*it);
        if (!ids.isEmpty()) setPile(ids.join(QLatin1Char(',')));
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != GameReady || room->getTag(QStringLiteral("SkillScenarioStarted")).toBool())
            return MiniSceneRule::trigger(event, room, player, data);
        room->setTag(QStringLiteral("SkillScenarioStarted"), true);
        // The base setup walks getAllPlayers() in this same order.
        const QList<ServerPlayer *> seats = room->getAllPlayers();
        QStringList names;
        for (ServerPlayer *seat : seats) names << seat->objectName();
        MiniSceneRule::trigger(event, room, player, data);
        // Hegemony rules switch on only now: the hegemony start would deal random head/deputy pairs.
        if (g_hegemony) Config.EnableHegemony = ServerInfo.EnableHegemony = true;
        // Hegemony "hidden" seats conceal the head general the way the hegemony start does,
        // keeping the innate instances so a later reveal is presentation only.
        for (int index = 0; index < seats.size(); ++index) {
            ServerPlayer *seat = seats.at(index);
            if (!Config.EnableHegemony || players.at(index).value(QStringLiteral("hidden")) != QLatin1String("1")) continue;
            // MiniScene changes generals through the ordinary scenario lifecycle.
            // Restore head-source metadata before hiding, preserving IDs and state.
            const General *head = seat->getGeneral();
            const QStringList explicitSkills = players.at(index).value(QStringLiteral("acquireSkills")).split(',');
            for (SkillInstance instance : seat->getSkillInstances()) {
                if ((instance.source != SourceInnate && instance.source != SourceAcquired)
                    || instance.bindHead != 0 || !head || !head->hasSkill(instance.skillName)
                    || instance.parentRef.isValid() || instance.frozenSourceRef.isValid()
                    || explicitSkills.contains(instance.skillName)) continue;
                std::fprintf(stderr, "SKILL_SCENARIO hidden root: %s %s#%d source=%d -> innate head\n",
                    qPrintable(seat->objectName()), qPrintable(instance.skillName), instance.instanceID,
                    static_cast<int>(instance.source));
                if (instance.source == SourceAcquired)
                    seat->removeSkillInstance(instance.skillName, instance.instanceID);
                instance.source = SourceInnate;
                instance.bindHead = 1;
                seat->upsertSkillInstance(instance);
            }
            std::fflush(stderr);
            room->setTag(seat->objectName(), QStringList{seat->getGeneralName(), seat->getGeneral2Name()});
            room->safeSetPlayerProperty(seat, "actual_general1", seat->getGeneralName());
            room->setPlayerProperty(seat, "general_showed", true);
            seat->hideGeneral(true);
        }
        const QVariant watermark =
            room->queryHistoryFacts({{QStringLiteral("limit"), 1}}).value(QStringLiteral("watermark"));
        QString error = registerScript(room, seats);
        try {
            if (error.isEmpty()) error = runTrigger(room, names);
        } catch (TriggerEvent) {
            // A death inside the trigger can end the game; record the board it ended on.
            g_report = buildReport(room, names, watermark, error);
            throw;
        }
        g_report = buildReport(room, names, watermark, error);
        room->gameOver(QStringLiteral("."));
        return true;
    }

private:
    QString registerScript(Room *room, const QList<ServerPlayer *> &seats) const
    {
        for (const QJsonValue &value : m_script) {
            const QJsonObject entry = value.toObject();
            ServerPlayer *player = seats.value(entry.value(QStringLiteral("seat")).toInt() - 1);
            const QString type = entry.value(QStringLiteral("type")).toString();
            const QString key = entry.value(QStringLiteral("key")).toString();
            const QJsonValue answer = entry.value(QStringLiteral("answer"));
            static const QStringList types = {"skill_invoke", "choice", "player_chosen",
                                              "card_chosen", "card", "activate"};
            if (!player || !types.contains(type))
                return QStringLiteral("script entry needs a valid seat and one of %1: %2")
                    .arg(types.join(QStringLiteral(", ")))
                    .arg(QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
            QVariant resolved = answer.toVariant();
            if (type == QLatin1String("player_chosen")) {
                ServerPlayer *target = seats.value(answer.toInt() - 1);
                if (!target) return QStringLiteral("player_chosen answer must be a seat number");
                resolved = target->objectName();
            } else if (type == QLatin1String("card") || type == QLatin1String("card_chosen")) {
                const QString text = answer.toString();
                int id = -1;
                // Legacy card strings ("slash:wusheng[...]=12", "@XxxCard=1") pass through unchanged.
                if (!isCardId(answer, &id) && !text.contains(QRegularExpression(QStringLiteral("[:=@$]")))) {
                    id = resolveRoomCard(room, text);
                    if (id < 0) return QStringLiteral("no card matches '%1'").arg(text);
                }
                if (id >= 0) resolved = id;
            }
            room->registerTestOverride(player, type, key, resolved);
        }
        return QString();
    }

    QString runTrigger(Room *room, const QStringList &seatNames) const
    {
        lua_State *L = room->getLuaState();
        if (!L) return QStringLiteral("room has no Lua state");
        room->initializeLuaTestEnvironment(); // binds the R (room) global
        g_triggerRoom = room;
        lua_register(L, "__enter_play_use", luaEnterPlayUse);
        lua_register(L, "__use_selected", luaUseSelected);
        lua_getglobal(L, "debug");
        lua_getfield(L, -1, "traceback");
        lua_remove(L, -2);
        const int handler = lua_gettop(L);
        QString error;
        // Separate chunks keep trigger line numbers matching the case text.
        const QList<QPair<QByteArray, const char *>> chunks = {
            {luaPrelude(seatNames).toUtf8(), "=prelude"}, {m_trigger.toUtf8(), "=trigger"}};
        for (const auto &chunk : chunks) {
            if (luaL_loadbuffer(L, chunk.first.constData(), size_t(chunk.first.size()), chunk.second) != LUA_OK
                || LuaRuntime::protectedCall(L, 0, 0, handler) != LUA_OK) {
                error = QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);
                break;
            }
        }
        lua_remove(L, handler);
        return error;
    }

    QJsonArray m_script;
    QString m_trigger;
};

struct CaseSetup {
    QList<QMap<QString, QString>> seats;
    QList<int> pile;
    QJsonArray script;
    QString trigger;
};

bool parseCase(const QJsonObject &root, CaseSetup &setup, QString *error)
{
    static const QSet<QString> seatKeys = {
        "general", "general2", "role", "hp", "maxhp", "hujia", "draw", "kingdom", "chained",
        "turned", "starter", "skills", "marks", "hand", "equip", "judge", "hidden"};
    const QList<int> pool = Sanguosha->getRandomCards();
    QSet<int> used;
    const QJsonArray seats = root.value(QStringLiteral("seats")).toArray();
    if (seats.size() < 2) { *error = QStringLiteral("a case needs at least two seats"); return false; }
    bool hasStarter = false;
    for (int index = 0; index < seats.size(); ++index) {
        const QJsonObject seat = seats.at(index).toObject();
        for (const QString &key : seat.keys()) {
            if (!seatKeys.contains(key)) {
                *error = QStringLiteral("seat %1: unknown key '%2'").arg(index + 1).arg(key);
                return false;
            }
        }
        if (!Sanguosha->getGeneral(seat.value(QStringLiteral("general")).toString())) {
            *error = QStringLiteral("seat %1: unknown general '%2'")
                .arg(index + 1).arg(seat.value(QStringLiteral("general")).toString());
            return false;
        }
        QMap<QString, QString> fields;
        fields["general"] = seat.value(QStringLiteral("general")).toString();
        fields["role"] = seat.value(QStringLiteral("role")).toString(index == 0 ? "lord" : "rebel");
        fields["draw"] = QString::number(seat.value(QStringLiteral("draw")).toInt(0));
        for (const char *key : {"general2", "hp", "maxhp", "hujia"}) {
            const QJsonValue value = seat.value(QLatin1String(key));
            if (!value.isUndefined())
                fields[key] = value.isDouble() ? QString::number(value.toInt()) : value.toString();
        }
        if (seat.contains(QStringLiteral("kingdom"))) fields["nationality"] = seat.value(QStringLiteral("kingdom")).toString();
        if (seat.value(QStringLiteral("chained")).toBool()) fields["chained"] = "1";
        if (seat.value(QStringLiteral("turned")).toBool()) fields["turned"] = "1";
        if (seat.value(QStringLiteral("hidden")).toBool()) fields["hidden"] = "1";
        if (seat.value(QStringLiteral("starter")).toBool()) { fields["starter"] = "1"; hasStarter = true; }
        QStringList skills;
        for (const QJsonValue &skill : seat.value(QStringLiteral("skills")).toArray()) skills << skill.toString();
        if (!skills.isEmpty()) fields["acquireSkills"] = skills.join(QLatin1Char(','));
        QStringList marks;
        const QJsonObject markMap = seat.value(QStringLiteral("marks")).toObject();
        for (auto it = markMap.begin(); it != markMap.end(); ++it)
            marks << QStringLiteral("%1*%2").arg(it.key()).arg(it.value().toInt());
        if (!marks.isEmpty()) fields["marks"] = marks.join(QLatin1Char(','));
        for (const char *area : {"hand", "equip", "judge"}) {
            QStringList ids;
            for (const QJsonValue &value : seat.value(QLatin1String(area)).toArray()) {
                const int id = resolveSetupCard(value, pool, used, error);
                if (id < 0) { *error = QStringLiteral("seat %1 %2: %3").arg(index + 1).arg(area).arg(*error); return false; }
                ids << QString::number(id);
            }
            if (!ids.isEmpty()) fields[area] = ids.join(QLatin1Char(','));
        }
        setup.seats << fields;
    }
    if (!hasStarter) setup.seats.first()["starter"] = "1";
    for (const QJsonValue &value : root.value(QStringLiteral("pile")).toArray()) {
        const int id = resolveSetupCard(value, pool, used, error);
        if (id < 0) { *error = QStringLiteral("pile: %1").arg(*error); return false; }
        setup.pile << id;
    }
    setup.script = root.value(QStringLiteral("script")).toArray();
    setup.trigger = root.value(QStringLiteral("trigger")).toString();
    return true;
}

bool writeJson(const QString &path, const QVariantMap &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(QJsonDocument::fromVariant(value).toJson()) >= 0;
}

QString optionValue(const QStringList &args, const QString &name)
{
    const int at = args.indexOf(name);
    return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
}

// Prints a general's package, skills and their translated descriptions, the input
// an agent needs to write cases from the skill text.
int describeGeneral(const QString &name, const QString &reportPath)
{
    const General *general = Sanguosha->getGeneral(name);
    if (!general) {
        fprintf(stderr, "unknown general '%s'\n", qPrintable(name));
        return 2;
    }
    QVariantList skills;
    for (const Skill *skill : general->getSkillList())
        skills << QVariantMap{
            {"name", skill->objectName()}, {"translation", Sanguosha->translate(skill->objectName())},
            {"description", Sanguosha->translate(":" + skill->objectName())},
            {"visible", skill->isVisible()}, {"class", skill->metaObject()->className()}};
    const auto *package = qobject_cast<const Package *>(general->parent());
    const QVariantMap info{
        {"general", name}, {"translation", Sanguosha->translate(name)},
        {"package", package ? package->objectName() : QString()}, {"kingdom", general->getKingdom()},
        {"max_hp", general->getMaxHp()}, {"start_hp", general->getStartHp()}, {"male", general->isMale()},
        {"skills", skills}};
    return writeJson(reportPath, info) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString casePath = optionValue(args, QStringLiteral("--case"));
    const QString reportPath = optionValue(args, QStringLiteral("--report"));
    const int timeoutSeconds = optionValue(args, QStringLiteral("--timeout")).toInt();
    const QString describe = optionValue(args, QStringLiteral("--describe-general"));
    bool seedOk = false;
    const quint64 seed = optionValue(args, QStringLiteral("--seed")).toULongLong(&seedOk);
    if (reportPath.isEmpty() || (describe.isEmpty() && (casePath.isEmpty() || !seedOk))) {
        fprintf(stderr, "usage: qsanguosha_skill_scenario --case <case.json> --report <report.json> "
                        "--seed <n> [--timeout <seconds>]\n"
                        "       qsanguosha_skill_scenario --describe-general <name> --report <info.json>\n");
        return 64;
    }
    if (!describe.isEmpty()) {
        QString error;
        if (!QSanRuntimePaths::resolve(args, &error) || !EngineBootstrap::initialize(false, &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        const int result = describeGeneral(describe, reportPath);
        EngineBootstrap::shutdown();
        return result;
    }
    QFile caseFile(casePath);
    QJsonParseError parseError;
    const QJsonDocument caseJson = caseFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(caseFile.readAll(), &parseError) : QJsonDocument();
    if (!caseJson.isObject()) {
        fprintf(stderr, "cannot read case %s: %s\n", qPrintable(casePath), qPrintable(parseError.errorString()));
        return 64;
    }
    QString error;
    if (!QSanRuntimePaths::resolve(args, &error) || !EngineBootstrap::initialize(false, &error)) {
        fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    Config.EnableAI = true;
    Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false;
    g_hegemony = caseJson.object().value(QStringLiteral("hegemony")).toBool();
    Config.AIDelay = Config.OriginAIDelay = 0;
    Config.CountDownSeconds = 0;
    // Standalone cases have no client handshake to initialize lord-skill eligibility.
    ServerInfo.GameMode = kScenarioName;
    ServerInfo.EnableHegemony = Config.EnableHegemony;
    ServerInfo.Enable2ndGeneral = Config.Enable2ndGeneral;
    ServerInfo.EnableAI = Config.EnableAI;
    QVariantMap scenarioOverrides = Config.valueOverrides();
    scenarioOverrides.insert(QStringLiteral("WithoutLordskill"), false);
    Config.setValueOverrides(scenarioOverrides);

    CaseSetup setup;
    if (!parseCase(caseJson.object(), setup, &error)) {
        writeJson(reportPath, {{"error", error}});
        fprintf(stderr, "%s\n", qPrintable(error));
        EngineBootstrap::shutdown();
        return 2;
    }
    auto *scenario = new MiniScene(kScenarioName);
    TriggerSkill *miniRule = scenario->getRule();
    scenario->setRule(new SkillScenarioRule(scenario, setup.seats, setup.pile, setup.script, setup.trigger));
    delete miniRule;
    Sanguosha->addScenario(scenario);
    Config.GameMode = Sanguosha->getGameMode(kScenarioName);

    auto *room = new Room(nullptr, kScenarioName, GameSessionConfig(seed));
    if (!room->hasLuaRuntime()) {
        fprintf(stderr, "room has no Lua runtime\n");
        delete room;
        EngineBootstrap::shutdown();
        return 1;
    }
    // Non-empty and not "headless": no countdown or AI pacing, and startGame still installs SmartAI.
    room->setProperty("to_test", kScenarioName);
    room->setNoClock(true);
    for (int index = 0; index < setup.seats.size(); ++index) {
        ServerPlayer *player = room->addAIPlayer();
        if (index == 0) player->setOwner(true);
        room->signup(player, QStringLiteral("Seat%1").arg(index + 1), QString(), true);
    }

    bool finished = false;
    QString winner;
    QObject::connect(room, &Room::game_over, &app, [&](const QString &value) {
        finished = true;
        winner = value;
    });
    QTimer lifecycle;
    QObject::connect(&lifecycle, &QTimer::timeout, &app, [&] {
        if (!finished || !room->allGameThreadsStopped()) return;
        lifecycle.stop();
        QVariantMap report = g_report;
        const bool started = !report.isEmpty();
        if (!started) report.insert(QStringLiteral("error"), QStringLiteral("game ended before the scenario started"));
        report.insert(QStringLiteral("winner"), winner);
        const bool written = writeJson(reportPath, report);
        delete room;
        room = nullptr;
        app.exit(written && started ? 0 : 1);
    });
    if (timeoutSeconds > 0) {
        QTimer::singleShot(timeoutSeconds * 1000, &app, [&] {
            // A stuck room thread cannot be queried safely; report and leave without teardown.
            writeJson(reportPath, {{"error", QStringLiteral("timeout after %1 s").arg(timeoutSeconds)}});
            fflush(nullptr);
            std::_Exit(3);
        });
    }
    lifecycle.start(10);
    room->forceStart();
    const int result = app.exec();
    EngineBootstrap::shutdown();
    return result;
}
