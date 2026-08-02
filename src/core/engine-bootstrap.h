#ifndef QSAN_ENGINE_BOOTSTRAP_H
#define QSAN_ENGINE_BOOTSTRAP_H

#include <QString>

namespace EngineBootstrap
{
    bool initialize(bool manualMode = false, QString *error = nullptr);
    bool isInitialized();
    bool hasLuaState();
    void shutdown();
}

#endif
