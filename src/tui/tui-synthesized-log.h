#ifndef TUI_SYNTHESIZED_LOG_H
#define TUI_SYNTHESIZED_LOG_H

#include "protocol/protocol-message.h"

#include <QList>
#include <QString>

#include <functional>

class ClientGameState;

QString tuiResolveLogPlayerName(const ClientGameState &state, const QString &objectName);

void tuiAppendSynthesizedLogs(ClientGameState *state, QList<int> *renPile,
                              const QSanProtocol::ProtocolMessage &message,
                              const std::function<void(const QString &)> &writeOutput);

#endif
