#include "engine-bootstrap.h"

#include "engine.h"

namespace EngineBootstrap
{
bool initialize(bool manualMode, QString *error)
{
    if (Sanguosha != nullptr)
        return true;

    Sanguosha = new Engine(manualMode);
    if (Sanguosha == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("Unable to allocate engine");
        return false;
    }
    return true;
}

bool isInitialized()
{
    return Sanguosha != nullptr;
}

bool hasLuaState()
{
    return Sanguosha != nullptr && Sanguosha->getLuaState() != nullptr;
}

void shutdown()
{
    Engine *engine = Sanguosha;
    Sanguosha = nullptr;
    delete engine;
}
}
