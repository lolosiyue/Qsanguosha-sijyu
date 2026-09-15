#include "tui-synthesized-log.h"

#include "client-game-state.h"
#include "client-move-log.h"
#include "engine.h"
#include "protocol.h"
#include "tui-log-text.h"
#include "client-log-formatter.h"

using namespace QSanProtocol;

QString tuiResolveLogPlayerName(const ClientGameState &state, const QString &objectName)
{
    return clientLogPlayerName(state.player(objectName), objectName);
}

void tuiAppendSynthesizedLogs(ClientGameState *state, QList<int> *renPile,
                              const ProtocolMessage &message,
                              const std::function<void(const QString &)> &writeOutput)
{
    const auto records = synthesizeClientMessageLogs(state, renPile, message);

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
