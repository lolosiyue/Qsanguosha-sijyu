#include "websocket-gateway.h"

#include "socket.h"

namespace {

QSanWebSocketServerFactory g_webSocketServerFactory = nullptr;

} // namespace

void qsanSetWebSocketServerFactory(QSanWebSocketServerFactory factory)
{
    g_webSocketServerFactory = factory;
}

ServerSocket *qsanCreateWebSocketServer()
{
    return g_webSocketServerFactory ? g_webSocketServerFactory() : nullptr;
}
