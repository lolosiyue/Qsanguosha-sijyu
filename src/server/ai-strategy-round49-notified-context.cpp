#include "ai-strategy-round49-notified-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-card-identity.h"
#include <QJsonArray>
namespace {
bool live(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
QJsonArray ids(const QVariant&v){QJsonArray a;if(v.userType()==qMetaTypeId<QList<int>>()){for(int id:v.value<QList<int>>())a.append(id);}else for(const auto&x:v.toList())a.append(x.toInt());return a;}
}
void projectAIRound49NotifiedContexts(AIRequest&q,Room&r,ServerPlayer*p){
 auto facts=q.worldView.strategyFacts.value("tenyear_hc").toObject();
 const QStringList all={"sushouForAI","xingzuoForAI","guanxuhandForAI","guanxudrawpileForAI","guanxuForAI","zhengyueForAI","yuanrongForAI"};
 for(const auto&k:all)facts.remove(k);
 q.worldView.strategyFacts.remove("round49_notified_faces");
 auto props=q.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("mythyuqi_AI");
 const auto publish=[&]{if(facts.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",facts);if(props.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();
 if(!live(q,r,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)return;
 QStringList piles;bool myth=false;
 if(q.pattern=="@@sushou"&&q.prompt.startsWith("@sushou:")&&q.handlingMethod==Card::MethodUse)piles<<"sushou";
 if(q.pattern=="@@xingzuo"&&q.prompt=="@xingzuo"&&q.handlingMethod==Card::MethodNone)piles<<"xingzuo";
 if(q.pattern=="@@guanxu1"&&q.prompt=="@guanxu1"&&q.handlingMethod==Card::MethodNone)piles<<"guanxuhand"<<"guanxudrawpile";
 if(q.pattern=="@@guanxu2"&&q.prompt=="@guanxu2"&&q.handlingMethod==Card::MethodDiscard)piles<<"guanxu";
 if(q.pattern=="@@zhengyue"&&(q.prompt.startsWith("zhengyue0:")||q.prompt.startsWith("zhengyue1:"))&&q.handlingMethod==Card::MethodUse)piles<<"zhengyue";
 if(q.pattern=="@@yuanrong!"&&(q.prompt.startsWith("yuanrong0:")||q.prompt.startsWith("yuanrong2:"))&&q.handlingMethod==Card::MethodUse)piles<<"yuanrong";
 if(q.pattern=="@@mythyuqi"&&(q.prompt.startsWith("#mythyuqi1:")||q.prompt.startsWith("#mythyuqi2:"))&&q.handlingMethod==Card::MethodNone){piles<<"mythyuqi";myth=true;}
 QJsonObject faces;
 for(const auto&n:piles){
  const auto sequence=ids(myth?p->property("mythyuqi_AI"):p->getTag(n+"ForAI"));
  const auto w=readAIStrategyNotifiedPile(r,p,"#"+n);
  // An actual empty owner value remains the source's empty list, even after
  // clear. Nonempty reads require the same recipient's exact live notification.
  if(!sequence.isEmpty()&&(!w.value("ordered_ids").isArray()||w.value("ordered_ids").toArray()!=sequence))continue;
  if(myth)props.insert("mythyuqi_AI",sequence);else facts.insert(n+"ForAI",sequence);
  const auto current=w.value("current_card_ids").toArray();const auto cards=w.value("cards").toObject();
  for(const auto&v:sequence){const QString k=QString::number(v.toInt());if(current.contains(v)&&cards.value(k).isObject()){auto face=cards.value(k).toObject();const Card*c=r.getCard(v.toInt());if(!c)continue;face.insert("legacy_identity_token",aiLegacyCardIdentity(r,p,c));faces.insert(k,face);}}
 }
 publish();if(!faces.isEmpty())q.worldView.strategyFacts.insert("round49_notified_faces",QJsonObject{{"pattern",q.pattern},{"cards",faces}});
}
