#include "ai-strategy-round52-olsp.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QPointer>
#include <cmath>
#include <limits>
namespace {
struct Frame {
 QPointer<Room> room; QPointer<ServerPlayer> owner;
 QString skill; SkillInstanceRef activation, source;
 Frame *previous=nullptr; bool valid=false;
};
thread_local Frame *current=nullptr;
bool same(const SkillInstanceRef &a,const SkillInstanceRef &b) {
 return a.ownerObjectName==b.ownerObjectName && a.key.skillName==b.key.skillName
  && a.key.instanceID==b.key.instanceID;
}
bool live(Room &room,const ServerPlayer *p) {
 if(!current||!current->valid||current->room!=&room||current->owner!=p
  ||!p||p->getRoom()!=&room)return false;
 bool roster=false;for(auto *q:room.getAllPlayers(true))if(q==p)roster=true;
 return roster && p->hasSkillInstance(current->activation.key.skillName,current->activation.key.instanceID)
  && p->hasSkillInstance(current->source.key.skillName,current->source.key.instanceID);
}
}
struct AIRound52OlspQuestionScope::Data {Frame frame;};
AIRound52OlspQuestionScope::AIRound52OlspQuestionScope(Room &room,const SkillContext &ctx,const QString &skill):d(new Data) {
 auto &f=d->frame;f.previous=current;f.room=&room;f.owner=ctx.owner;
 f.skill=skill;f.activation=ctx.activationRef;f.source=ctx.sourceRef;
 f.valid=(skill=="ol2shanjia"||skill=="jiewan") && ctx.owner && ctx.owner->getRoom()==&room
  && f.activation.isValid() && f.source.isValid()
  && f.activation.ownerObjectName==ctx.owner->objectName()
  && f.source.ownerObjectName==ctx.owner->objectName()
  && f.activation.key.skillName==skill && f.source.key.skillName==skill;
 // Even an invalid nested scope shadows the outer inquiry.
 current=&f;
}
AIRound52OlspQuestionScope::~AIRound52OlspQuestionScope(){current=d->frame.previous;}
bool aiRound52OlspInstance(Room &room,const ServerPlayer *p,const QString &skill,int instanceID) {
 return live(room,p)&&current->skill==skill&&current->activation.key.instanceID==instanceID;
}
bool aiRound52OlspAction(Room &room,const ServerPlayer *p,const AiSkillActionContext &a) {
 return live(room,p)&&same(a.activationRef,current->activation)&&same(a.sourceRef,current->source);
}
bool aiRound52OlspQuestion(const AIRequest &r,const ServerPlayer *p) {
 if(!p||!p->getRoom()||!live(*p->getRoom(),p)||r.decisionId==0
  ||r.viewerObjectName!=p->objectName()||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  ||r.handlingMethod!=Card::MethodUse||r.pattern!="@@"+current->skill
  ||r.prompt!=(current->skill=="ol2shanjia"?"ol2shanjia1":"jiewan0"))return false;
 return !r.hasSkillActionContext||aiRound52OlspAction(*p->getRoom(),p,r.skillActionContext);
}
bool aiRound52OlspEvaluation(const AIRequest &r,const ServerPlayer *p,const QJsonObject &q) {
 if(!aiRound52OlspQuestion(r,p)||q.size()!=4
  ||q.value("kind")!="evaluate_skill_card"||!q.value("subcards").isArray())return false;
 const QString raw=q.value("skill").toString();const auto ids=q.value("subcards").toArray();
 if(current->skill=="ol2shanjia")return raw=="_ol2shanjia"&&q.value("name")=="slash"&&ids.isEmpty();
 if(raw!="jiewan"||q.value("name")!="snatch"||ids.size()!=1||!ids[0].isDouble())return false;
 const double value=ids[0].toDouble();
 if(!std::isfinite(value)||value<0||value>std::numeric_limits<int>::max()||value!=std::floor(value))return false;
 const int id=int(value);Room &room=*p->getRoom();
 if(room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand)return false;
 for(const Card *c:p->getHandcards())if(c&&c->getEffectiveId()==id)
  for(const auto &v:r.worldView.handCards)if(v.effectiveId==id&&v.cardId==c->getId()
   &&v.objectName==c->objectName()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&v.skillName==c->getSkillName(false)&&v.virtualCard==c->isVirtualCard()
   &&v.subcardIds==c->getSubcards()&&!v.legacyIdentityToken.isEmpty()
   &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return true;
 return false;
}
