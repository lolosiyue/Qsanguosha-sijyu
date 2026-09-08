#include "client-rules-host.h"
#include "protocol/rules-bundle-identity.h"

#include <emscripten/emscripten.h>

namespace {
ClientRulesHost host(QStringLiteral("/assets"), QStringLiteral("/work"),
                     QStringLiteral("/userdata"));
}

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_bridge_schema()
{
    return QSanRules::BridgeSchema;
}

// Keep the production ABI unchanged. Native tests compile the same host;
// there is no test implementation of initialization, evaluation or shutdown.
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_initialize() { return host.initialize(); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_evaluate() { return host.evaluate(); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_shutdown() { return host.shutdown(); }
