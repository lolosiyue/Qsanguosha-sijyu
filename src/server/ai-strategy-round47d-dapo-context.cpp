#include "ai-strategy-round47d-dapo-context.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>
namespace {
bool role(const QString &s)
{ return s=="lord" || s=="loyalist" || s=="rebel" || s=="renegade"; }
const AIPlayerView *view(const AIWorldView &world, const QString &name)
{
    if (world.self.objectName==name) return &world.self;
    for (const auto &p:world.players) if(p.objectName==name) return &p;
    return nullptr;
}
}
void projectAIRound47DapoEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    world.strategyFacts.remove("dongmanbao_dapo_role_witness");
    if (!viewer || viewer->getRoom()!=&room || !eventPlayer || eventPlayer->getRoom()!=&room
        || world.self.objectName!=viewer->objectName() || event.triggerEvent!=Dying
        || !data.canConvert<DyingStruct>()) return;
    const auto dying=data.value<DyingStruct>();
    if (!dying.who || dying.who->getRoom()!=&room || !view(world,dying.who->objectName())) return;
    const auto *target=view(world,dying.who->objectName());
    const bool targetKnown=(dying.who==viewer || target->roleVisible) && role(target->role);
    QJsonArray sources;
    for (ServerPlayer *source:room.getOtherPlayers(eventPlayer)) {
        const auto *row=view(world,source->objectName());
        if (!row || row->hp!=1) continue;
        bool hasSkill=false;
        for(const auto &skill:row->skills)
            if(skill.skillName=="SE_Dapo" && !skill.invalid) {hasSkill=true;break;}
        if(!hasSkill) continue; // No hidden native hasSkill/role query.
        const bool known=(source==viewer || row->roleVisible) && role(row->role);
        QJsonObject proof{{"player",source->objectName()},{"role_known",known}};
        if(known) proof.insert("role",row->role);
        sources.append(proof);
    }
    world.strategyFacts.insert("dongmanbao_dapo_role_witness",QJsonObject{
        {"actor",eventPlayer->objectName()},{"player",dying.who->objectName()},
        {"target_role_known",targetKnown},{"sources",sources}});
}
