#include "tui-synthesized-log.h"

#include "client-game-state.h"
#include "client-move-log.h"
#include "engine.h"
#include "protocol.h"
#include "tui-log-text.h"

using namespace QSanProtocol;

QString tuiResolveLogPlayerName(const ClientGameState &state, const QString &objectName)
{
    const QVariantMap player = state.player(objectName);
    QString general = player.value(QStringLiteral("general")).toString();
    if (general.isEmpty())
        general = player.value(QStringLiteral("avatar")).toString();
    if (general.isEmpty())
        return objectName;
    auto translate = [](const QString &name) {
        if (Sanguosha == nullptr)
            return name;
        const QString translated = Sanguosha->translate(name);
        return translated.isEmpty() ? name : translated;
    };
    QString name = translate(general);
    const QString deputy = player.value(QStringLiteral("deputy_general")).toString();
    if (!deputy.isEmpty())
        name += QLatin1Char('/') + translate(deputy);
    return name;
}

void tuiAppendSynthesizedLogs(ClientGameState *state, QList<int> *renPile,
                              const ProtocolMessage &message,
                              const std::function<void(const QString &)> &writeOutput)
{
    if (state == nullptr || message.type != ProtocolMessageType::Notification)
        return;

    if (message.command == S_COMMAND_GAME_START
        || (message.command == S_COMMAND_STATE_SYNC
            && message.payload.toMap().value(QStringLiteral("phase")).toString()
                == QLatin1String("begin"))) {
        if (renPile != nullptr)
            renPile->clear();
    }

    QList<ClientLogRecord> records;
    if (message.command == S_COMMAND_GET_CARD || message.command == S_COMMAND_LOSE_CARD) {
        records = synthesizeCardMovementLogs(message.command, message.payload.toMap(),
                                             renPile);
    } else if (message.command == S_COMMAND_CHANGE_HP) {
        const QVariantMap payload = message.payload.toMap();
        const QString who = payload.value(QStringLiteral("player_name")).toString();
        records = synthesizeHpChangeLogs(
            payload,
            state->playerValue(who, QStringLiteral("hp")).toInt(),
            state->playerValue(who, QStringLiteral("max_hp")).toInt());
    } else if (message.command == S_COMMAND_CHANGE_MAXHP) {
        const QVariantMap payload = message.payload.toMap();
        const QString who = payload.value(QStringLiteral("player_name")).toString();
        records = synthesizeMaxHpChangeLogs(
            who,
            state->playerValue(who, QStringLiteral("hp")).toInt(),
            state->playerValue(who, QStringLiteral("max_hp")).toInt());
    } else {
        return;
    }

    const TuiPlayerNameResolver names = [state](const QString &objectName) {
        return tuiResolveLogPlayerName(*state, objectName);
    };
    for (const ClientLogRecord &record : records) {
        const QVariantMap map = record.toSkillLogMap();
        const QString line = tuiSkillLogText(map, names);
        if (line.isEmpty())
            continue;
        state->appendPresentationEvent(S_COMMAND_LOG_SKILL, line, map);
        if (writeOutput)
            writeOutput(line);
    }
}
