#include "settings.h"
#include "engine.h"
#include "scenerule.h"
#include "room.h"

SceneRule::SceneRule(QObject *parent) : GameRule(parent)
{
    events << GameReady;
}

int SceneRule::getPriority(TriggerEvent) const
{
    return -2;
}

bool SceneRule::trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
{
    if (!player && triggerEvent == GameReady) {
        foreach (QString extension, Sanguosha->getExtensions()) {
            if (Config.BanPackages.contains(extension) || Config.value("ForbidPackages").toStringList().contains(extension)) continue;

            QString skill = QString("#%1").arg(extension);
            if (extension.startsWith("scene") && Sanguosha->getSkill(skill)) {
                foreach(ServerPlayer *p, room->getPlayers())
                    room->acquireSkill(p, skill);
            }
        }
    }

    return GameRule::trigger(triggerEvent, room, player, data);
}

// R56 exact source-owned native type registrations. No callback executes.
#include "ai-strategy-round56-native-types.h"
void registerAIRound56NativeTypes_src_scenario_scenerule_cpp(AIRound56NativeTypes&types){
 types.emplace(std::type_index(typeid(SceneRule)), 2u);
 types.recordActivateSource<SceneRule>(2929u); AI_R56_REQUIRE_DECLARATION(SceneRule, getLimitScope, Skill); AI_R56_REQUIRE_DECLARATION(SceneRule, getMaxUsageLimit, Skill);
}
