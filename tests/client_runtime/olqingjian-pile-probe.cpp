// E2 native probe: GET_CARD must fill Player::getPile("olqingjian") so
// OLQingjianVS can accept a legal @@olqingjian! selection.
#include "card.h"
#include "client-game-state.h"
#include "client-game-state-reducer.h"
#include "client-rules-session.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "interaction-model.h"
#include "player.h"
#include "protocol.h"
#include "runtime-paths.h"
#include "skill.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

QByteArray encode(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
          "cannot write probe output");
}

QJsonObject parse(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    check(error.error == QJsonParseError::NoError && document.isObject(), "invalid JSON");
    return document.object();
}

QJsonObject player(const QString &name, int seat, const QStringList &skills)
{
    return {{"object_name", name}, {"seat", seat}, {"general", QString()},
        {"role", "loyalist"}, {"alive", true}, {"hp", 4}, {"max_hp", 4},
        {"phase", "not_active"}, {"skills", QJsonArray::fromStringList(skills)}};
}

QJsonObject olqingjianRequest(const QJsonObject &state,
                              const QJsonArray &cardIds, const QJsonArray &targets)
{
    return {{"schema_version", 1}, {"generation", 0}, {"revision", 0},
        {"request_id", "18446744073709551615"},
        {"command", static_cast<int>(QSanProtocol::S_COMMAND_RESPONSE_CARD)},
        {"payload", QJsonObject{{"pattern", "@@olqingjian!"},
            {"handling_method", static_cast<int>(Card::MethodNone)}}},
        {"state", state},
        {"selection", QJsonObject{{"card_ids", cardIds}, {"targets", targets},
            {"skill_name", "olqingjian"}, {"skill_instance_id", 0}, {"user_string", ""}}}};
}

bool jsonContainsInt(const QJsonArray &values, int id)
{
    for (const QJsonValue &entry : values) {
        if (entry.toInt() == id)
            return true;
    }
    return false;
}

void applyMove(ClientGameState *state, int command, const QString &fromPlayer,
               const QString &toPlayer, int fromPlace, int toPlace,
               const QString &fromPile, const QString &toPile, const QList<int> &ids)
{
    QVariantMap payload{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("moves"), QVariantList{QVariantMap{
            {QStringLiteral("from_player"), fromPlayer},
            {QStringLiteral("to_player"), toPlayer},
            {QStringLiteral("from_place"), fromPlace},
            {QStringLiteral("to_place"), toPlace},
            {QStringLiteral("from_pile"), fromPile},
            {QStringLiteral("to_pile"), toPile},
            {QStringLiteral("card_ids"), [&ids] {
                QVariantList values;
                for (int id : ids)
                    values.append(id);
                return values;
            }()},
            {QStringLiteral("open"), true}}}}};
    const ClientStateReduction result =
        ClientGameStateReducer::applyNotification(state, command, payload);
    check(result.success, "card movement notification rejected");
}

QJsonObject snapshotState(const ClientGameState &state)
{
    QJsonObject json = state.toJson();
    json.insert(QStringLiteral("player_names"),
                QJsonArray::fromStringList(state.playerNames()));
    return json;
}

QJsonArray jsonInts(const QVariantList &values)
{
    QJsonArray array;
    for (const QVariant &value : values)
        array.append(value.toInt());
    return array;
}

}

int main(int argc, char **argv)
{
    try {
        check(argc == 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--asset-root")
                  && QString::fromLocal8Bit(argv[3]) == QLatin1String("--output"),
              "expected --asset-root DIR --output FILE");
        const QString assets = QDir(QString::fromLocal8Bit(argv[2])).absolutePath();
        const QString output = QDir::current().absoluteFilePath(QString::fromLocal8Bit(argv[4]));
        QTemporaryDir isolated(QDir::temp().filePath(QStringLiteral("olqingjian-pile-XXXXXX")));
        check(isolated.isValid(), "cannot create isolated work directory");
        isolated.setAutoRemove(true);
        const QString user = isolated.filePath(QStringLiteral("userdata"));
        check(QDir().mkpath(user), "cannot create isolated userdata directory");
        // The launcher stages builtin-only assets before this process starts.
        // Exercise the production Engine + ClientRulesSession without writing
        // extension placeholders into the caller's asset directory.
        int argcHolder = 1;
        char name[] = "qsanguosha_olqingjian_pile_probe";
        char *argvHolder[] = {name, nullptr};
        QCoreApplication application(argcHolder, argvHolder);
        QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_client"));
        qputenv("QSAN_ASSET_ROOT", assets.toUtf8());
        qputenv("QSAN_USER_DATA_ROOT", user.toUtf8());
        QString error;
        check(QSanRuntimePaths::resolve(application.arguments(), &error),
              qPrintable(QStringLiteral("runtime path resolve failed: %1").arg(error)));
        check(EngineBootstrap::initialize(false, &error) && EngineBootstrap::hasLuaState(),
              qPrintable(QStringLiteral("engine initialization failed: %1").arg(error)));
        check(Sanguosha != nullptr && Sanguosha->getViewAsSkill(QStringLiteral("olqingjian")) != nullptr,
              "olqingjian ViewAs skill missing from production registry");

        int slash = -1;
        const int count = Sanguosha->getCardCount();
        for (int id = 0; id < count; ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (slash < 0 && card != nullptr && card->objectName() == QLatin1String("slash"))
                slash = id;
        }
        check(slash >= 0, "builtin registry must supply Slash");

        ClientGameState state;
        state.setCardIdSpace(count);
        state.setSetup(QVariantMap{{QStringLiteral("game_mode"), QStringLiteral("02p")},
                                   {QStringLiteral("mode"), QStringLiteral("02p")}});
        state.setSelfName(QStringLiteral("sgs1"));
        state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2")});
        auto installPlayer = [&state](const QString &name, int seat, const QStringList &skills) {
            const QJsonObject person = player(name, seat, skills);
            for (auto it = person.constBegin(); it != person.constEnd(); ++it) {
                if (it.key() == QLatin1String("skills"))
                    state.setPlayerValue(name, it.key(), skills);
                else
                    state.setPlayerValue(name, it.key(), it.value().toVariant());
            }
        };
        installPlayer(QStringLiteral("sgs1"), 1, {QStringLiteral("olqingjian")});
        installPlayer(QStringLiteral("sgs2"), 2, {});

        // Old buggy projection: card owner/place/pile updated, player piles empty.
        state.setCardValue(slash, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(slash, QStringLiteral("place"), static_cast<int>(Player::PlaceSpecial));
        state.setCardValue(slash, QStringLiteral("pile"), QStringLiteral("olqingjian"));
        state.setCardValue(slash, QStringLiteral("open"), true);

        QJsonArray cases;
        const auto evaluate = [&](const char *label, const QJsonArray &cardIds,
                                  const QJsonArray &targets, bool expectConfirm,
                                  const char *expectReason) {
            InteractionResponse canonical;
            const QJsonObject result = ClientRulesSession().evaluate(
                olqingjianRequest(snapshotState(state), cardIds, targets), &canonical);
            const QString reason = result.value(QStringLiteral("reason")).toString();
            const bool confirm = result.value(QStringLiteral("can_confirm")).toBool();
            const bool known = result.value(QStringLiteral("known")).toBool();
            const QByteArray detail = QByteArray(label) + " known="
                + (known ? "true" : "false") + " confirm=" + (confirm ? "true" : "false")
                + " reason=" + reason.toUtf8();
            check(known, detail.constData());
            check(confirm == expectConfirm, detail.constData());
            check((canonical.kind != InteractionResponseKind::None) == expectConfirm,
                  "only a complete selection may return a canonical response");
            check(result.value(QStringLiteral("wire")).isNull(),
                  "a rules preview must not reserve or encode a transport reply");
            if (expectReason != nullptr)
                check(reason == QString::fromUtf8(expectReason), detail.constData());
            cases.append(QJsonObject{{QStringLiteral("label"), label},
                {QStringLiteral("can_confirm"), confirm},
                {QStringLiteral("reason"), reason},
                {QStringLiteral("selectable_cards"), result.value(QStringLiteral("selectable_cards"))},
                {QStringLiteral("piles"), jsonInts(state.playerValue(QStringLiteral("sgs1"),
                    QStringLiteral("piles")).toMap().value(QStringLiteral("olqingjian")).toList())}});
            return result;
        };

        evaluate("card_fields_without_player_pile", QJsonArray{slash}, QJsonArray{"sgs2"},
                 false, "subcard_rejected");
        check(state.playerValue(QStringLiteral("sgs1"), QStringLiteral("piles")).toMap()
                  .value(QStringLiteral("olqingjian")).toList().isEmpty(),
              "control case must start with an empty named pile");

        applyMove(&state, QSanProtocol::S_COMMAND_GET_CARD, QString(), QStringLiteral("sgs1"),
                  static_cast<int>(Player::PlaceUnknown), static_cast<int>(Player::PlaceSpecial),
                  QString(), QStringLiteral("olqingjian"), {slash});
        check(state.playerValue(QStringLiteral("sgs1"), QStringLiteral("piles")).toMap()
                  .value(QStringLiteral("olqingjian")).toList() == QVariantList{slash},
              "GET_CARD must insert the visible olqingjian pile id");

        const QJsonObject offered = evaluate("get_card_sync_offers_pile", QJsonArray{}, QJsonArray{},
                                             false, "incomplete_card_selection");
        check(jsonContainsInt(offered.value(QStringLiteral("selectable_cards")).toArray(), slash),
              "olqingjian pile card was not offered as a selectable candidate");

        const QJsonObject ok = evaluate("get_card_sync_allows_distribute", QJsonArray{slash},
                                        QJsonArray{"sgs2"}, true, "");
        check(ok.value(QStringLiteral("can_confirm")).toBool(), "legal distribution was rejected");

        applyMove(&state, QSanProtocol::S_COMMAND_LOSE_CARD, QStringLiteral("sgs1"), QString(),
                  static_cast<int>(Player::PlaceSpecial), static_cast<int>(Player::PlaceTable),
                  QStringLiteral("olqingjian"), QString(), {slash});
        check(state.playerValue(QStringLiteral("sgs1"), QStringLiteral("piles")).toMap()
                  .value(QStringLiteral("olqingjian")).toList().isEmpty(),
              "LOSE_CARD must drop the named pile id");
        evaluate("lose_card_clears_distribute_pool", QJsonArray{slash}, QJsonArray{"sgs2"},
                 false, "subcard_rejected");

        EngineBootstrap::shutdown();
        writeFile(output, encode({{"schema_version", 1}, {"status", "PASS"},
            {"card_id", slash}, {"card_count", count}, {"cases", cases}}));
        std::fprintf(stderr, "[AUTOTEST] OLQINGJIAN_PILE_SYNC status=PASS card_id=%d\n", slash);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[AUTOTEST] OLQINGJIAN_PILE_SYNC status=FAIL detail=%s\n", error.what());
        return 1;
    }
}
