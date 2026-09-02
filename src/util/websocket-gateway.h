#ifndef WEBSOCKET_GATEWAY_H
#define WEBSOCKET_GATEWAY_H

#include "build-features.h"

class ServerSocket;

using QSanWebSocketServerFactory = ServerSocket *(*)();

void qsanSetWebSocketServerFactory(QSanWebSocketServerFactory factory);
ServerSocket *qsanCreateWebSocketServer();

#if QSAN_ENABLE_WEBSOCKETS
// Defined in qsanguosha_websocket. Call before constructing Server.
void qsanLinkWebSocketGateway();
#endif

#endif
