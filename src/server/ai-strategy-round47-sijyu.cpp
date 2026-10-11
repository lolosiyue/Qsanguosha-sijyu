#include "ai-strategy-round47-sijyu.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "serverplayer.h"
#include "room.h"
int aiRound47SijyuPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &name)
{
 // Fixed sijyu.lua1003: owner PhaseChanging invokes its V2 zero-card response
 // through askForUseCard(MethodNone). No general Lua SkillCard alias admission.
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  ||r.pattern!="@@sijyu_xuanmeng"||r.prompt!="@sijyu_xuanmeng"
  ||r.handlingMethod!=Card::MethodNone||skill!="sijyu_xuanmeng"||name!="#sijyu_xuanmeng"
  ||!p->hasSkill(skill))return -1;
 return aiRound44RangeRequestCurrent(*p->getRoom(),r,p)?0:-1;
}
