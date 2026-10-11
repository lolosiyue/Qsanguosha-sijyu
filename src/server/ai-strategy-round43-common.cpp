#include "ai-strategy-round43-common.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
namespace {
bool ownPlay(const AIRequest &r,const ServerPlayer *p)
{
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && p->hasSkill("jizhanshiz");
}
}
int aiRound43InovationPrototype(const AIRequest &r,const ServerPlayer *p,
                              const QString &skill,const QString &prototype)
{
    // Fixed AI570 constructs the zero-cost historical SkillCard. This is only
    // the source prototype declaration; real V2 BaonuEffects, phase history,
    // target filters and final selected admission remain the native pipeline's.
    return ownPlay(r,p) && skill=="jizhanshiz" && prototype=="JizhanCard" ? 0 : -1;
}
bool emitAIRound43InovationWarning(Room &room,const AIRequest &r,const ServerPlayer *p,
                                 const QString &source,const QString &message)
{
    if(!ownPlay(r,p) || p->getRoom()!=&room || source!="inovation.jizhanshiz"
        || message!="here???") return false;
    // Exact reached source writeToConsole sink, after use.card assignment and
    // before use.to replacement. No game state or private value is exported.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}
