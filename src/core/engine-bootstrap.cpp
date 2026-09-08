#include "engine-bootstrap.h"

#include "engine.h"
#include "rules-bundle-identity.h"

namespace EngineBootstrap
{
bool initialize(bool manualMode, QString *error)
{
    if (Sanguosha != nullptr)
        return true;

    QSanRulesIdentity::beginBootstrap();
    Sanguosha = new Engine(manualMode);
    if (Sanguosha == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("Unable to allocate engine");
        return false;
    }
    QSanRulesIdentity::finishBootstrap(manualMode);
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
    QSanRulesIdentity::clear();
    Engine *engine = Sanguosha;
    Sanguosha = nullptr;
    delete engine;
}
}
