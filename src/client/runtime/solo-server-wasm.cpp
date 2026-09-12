#include "solo-server-host.h"

#include <emscripten/emscripten.h>
#include <exception>

namespace {
SoloServerHost host(QStringLiteral("/assets"), QStringLiteral("/work"), QStringLiteral("/userdata"));

int invoke(int (SoloServerHost::*operation)())
{
    // Never unwind C++ exceptions across the exported JavaScript ABI.
    try {
        return (host.*operation)();
    } catch (const std::exception &error) {
        return host.reportFailure(QString::fromUtf8(error.what()));
    } catch (...) {
        return host.reportFailure(QStringLiteral("solo_native_exception"));
    }
}
}

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_solo_initialize() { return invoke(&SoloServerHost::initialize); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_solo_start() { return invoke(&SoloServerHost::start); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_solo_frame() { return invoke(&SoloServerHost::frame); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_solo_pump() { return invoke(&SoloServerHost::pump); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_solo_stop() { return invoke(&SoloServerHost::stop); }
