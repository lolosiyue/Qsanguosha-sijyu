#include "ai-strategy-round49-names.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
namespace {
bool live(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
bool response(const AIRequest&q){return q.kind==AIRequest::UseCard&&(q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);}
bool jil(const AIRequest&q,ServerPlayer*p){return response(q)&&q.pattern=="@@jilie"&&q.prompt=="jilie0:"+p->property("jilieCn").toString();}
}
void projectAIRound49OwnerNames(AIRequest&q,Room&r,ServerPlayer*p){
 const QStringList all={"SkillDescriptionRecord_fengying","SkillDescriptionRecord_fuping","choulve_damage_card","jilieCn","jingong_tricks","tenyearjingong_tricks"};
 auto props=q.worldView.strategyFacts.value("owner_skill_properties").toObject();for(const auto&k:all)props.remove(k);
 const auto publish=[&]{if(props.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();if(!live(q,r,p))return;
 QStringList keys;
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY)keys={"SkillDescriptionRecord_fengying","SkillDescriptionRecord_fuping","jingong_tricks","tenyearjingong_tricks"};
 if(q.kind==AIRequest::RespondCard)keys<<"SkillDescriptionRecord_fuping";
 if((q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen")||(response(q)&&q.pattern=="@@choulve!"))keys<<"choulve_damage_card";
 if(jil(q,p))keys<<"jilieCn";
 for(const auto&k:keys)props.insert(k,p->property(k.toUtf8().constData()).toString());publish();
}
bool aiRound49JilieConstruction(const AIRequest&q,Room&r,ServerPlayer*p,const QJsonObject&s){
 if(!live(q,r,p)||!jil(q,p)||s.size()!=6||s.value("source_key")!="@@jilie"||s.value("skill")!="_jilie"||!s.value("suit").isDouble()||s.value("suit").toDouble()!=6||!s.value("number").isDouble()||s.value("number").toDouble()!=0||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString n=p->property("jilieCn").toString();
 return !n.isEmpty()&&s.value("name").isString()&&s.value("name").toString()==n&&q.worldView.strategyFacts.value("owner_skill_properties").toObject().value("jilieCn").toString()==n;
}
