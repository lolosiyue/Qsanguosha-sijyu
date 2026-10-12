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
