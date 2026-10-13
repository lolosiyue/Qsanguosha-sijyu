#include "ai-strategy-round37-choice.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-card-identity.h"
#include "ai-runtime.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "game-rng.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include "player-armor-effect-domain.h"
#include <QHash>
#include <QObject>
#include <QUuid>
#include <cmath>
namespace {
struct Capture {QString viewer,target,flags,scope,identity;QJsonObject source;quint64 decision=0,revision=0;QList<int> ids;int selected=-1;};
struct Mark {QString identity,target;bool value=false;};
struct Owner {bool baseline=false;QHash<QString,QHash<int,Mark>> marks;};
class Ledger final:public AIRoomLedger {public:explicit Ledger(Room&r):AIRoomLedger(r){}bool captured=false,entered=false,initial=false;QString active;
 QHash<QString,Owner> owners;QHash<QString,Capture> scopes,samples,receipts;};
Ledger &ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return*l;return *new Ledger(r);}
QString nonce(const char*prefix){return QString::fromLatin1(prefix)+QUuid::createUuid().toString(QUuid::WithoutBraces);}
QJsonObject fail(const char*why){return{{"kind","original_sd_choice"},{"known",false},{"reason",QString::fromLatin1(why)}};}
QJsonObject ok(){return{{"kind","original_sd_choice"},{"known",true}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()
 &&q.stateRevision==r.roomRuntime()->stateRevision()&&aiRound35OriginalQuestion(q);}
bool reason(const QString&s){return s=="snatch"||s=="dismantlement";}
bool zones(const QString&s){if(s.isEmpty())return false;for(auto c:s)if(c!='h'&&c!='e'&&c!='j')return false;return true;}
QList<int> full(ServerPlayer*p,const QString&flags){QList<int> ids;for(auto*c:p->getCards(flags)){if(!c||c->getEffectiveId()<0)return{};ids<<c->getEffectiveId();}return ids;}
bool stamp(const Capture&c,const AIRequest&q){return c.viewer==q.viewerObjectName&&c.decision==q.decisionId&&c.revision==q.stateRevision;}
QString identity(Room&r,ServerPlayer*p,int id){return aiLegacyCardIdentity(r,p,Sanguosha->getCard(id));}
bool disclosed(const AIRequest&q,ServerPlayer*p,ServerPlayer*t,int id){
 if(p==t){for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)return true;}
 const AIPlayerView*v=p==t?&q.worldView.self:nullptr;for(const auto&x:q.worldView.players)if(x.objectName==t->objectName())v=&x;
 if(!v)return false;for(const auto*cards:{&v->knownCards,&v->equips,&v->judgingArea})for(const auto&c:*cards)if(c.effectiveId==id)return true;return false;
}
bool sourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s,const QString&name){
 // Actual owner physical or retained readonly object only. No arbitrary clone,
 // hidden material lookup, declaration, payment or native flag modification.
 const Card*c=nullptr;
 if(s.size()==1&&s.value("recipe_key").isString())c=r.roomRuntime()->ai().readonlyCard(q,p,s.value("recipe_key").toString());
 else if(s.size()==1&&s.value("id").isDouble()){
  const double n=s.value("id").toDouble();if(!std::isfinite(n)||std::floor(n)!=n||n<0||n>Sanguosha->getCardCount())return false;
  int id=int(n);for(auto*x:p->getCards("he"))if(x&&x->getEffectiveId()==id)c=x;
 }
 return c&&c->objectName()==name;
}
Owner &owner(Ledger&l,const QString&name){if(!l.owners.contains(name)){Owner o;o.baseline=l.captured&&l.initial;l.owners.insert(name,o);}return l.owners[name];}
Capture capture(const AIRequest&q,ServerPlayer*t,const QString&flags,const QString&scope){Capture c;c.viewer=q.viewerObjectName;c.target=t->objectName();c.flags=flags;c.scope=scope;c.decision=q.decisionId;c.revision=q.stateRevision;c.ids=full(t,flags);return c;}
}
void captureAIRound37SD(Room&r){auto&l=ledger(r);if(l.captured)return;l.captured=true;l.initial=!l.entered;
 // Bootstrap is public configured initial state, captured before any policy.
 // Never expose or reconstruct a preexisting private flag map.
 if(l.initial)for(int id=0;id<Sanguosha->getCardCount();++id){const Card*c=Sanguosha->getCard(id);if(c&&(c->hasFlag("AIGlobal_SDCardChosen_snatch")||c->hasFlag("AIGlobal_SDCardChosen_dismantlement"))){l.initial=false;break;}}
}
void beginAIRound37SD(Room&r,const QString&viewer){auto&l=ledger(r);l.entered=true;
 if(viewer.isEmpty()||(!l.active.isEmpty()&&l.active!=viewer)){l.initial=false;for(auto i=l.owners.begin();i!=l.owners.end();++i){i->baseline=false;i->marks.clear();}}
 l.active=viewer;l.scopes.clear();l.samples.clear();l.receipts.clear();
}
void invalidateAIRound37SD(Room&r){auto&l=ledger(r);l.entered=true;l.initial=false;l.scopes.clear();l.samples.clear();l.receipts.clear();for(auto i=l.owners.begin();i!=l.owners.end();++i){i->baseline=false;i->marks.clear();}}
void invalidateAIRound37SDCard(Room&r,int id){auto&l=ledger(r);for(auto i=l.owners.begin();i!=l.owners.end();++i)for(auto j=i->marks.begin();j!=i->marks.end();++j){
 // Movement does not clear source flags. Lose knowledge, do not invent false.
 if(j->contains(id)){Mark unknown;j->insert(id,unknown);}}
 for(auto i=l.samples.begin();i!=l.samples.end();)if(i->ids.contains(id))i=l.samples.erase(i);else++i;
 for(auto i=l.receipts.begin();i!=l.receipts.end();)if(i->ids.contains(id))i=l.receipts.erase(i);else++i;
}
QJsonObject queryAIRound37SD(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(!current(r,q,p))return fail("Original SD request/viewer/revision mismatch");auto&l=ledger(r);if(l.active!=p->objectName())return fail("Original SD policy-entry witness missing");
 const QString op=s.value("op").toString();
 if(op=="enter"&&s.size()==3&&s.value("source").isObject()){
  const QString name=s.value("reason").toString();if(!reason(name)
   ||!sourceCard(r,q,p,s.value("source").toObject(),name))return fail("Original SD planning source is not an exact owner/retained object");
  const QString key=nonce("original-sd-scope:");Capture c;c.viewer=p->objectName();c.decision=q.decisionId;c.revision=q.stateRevision;c.flags=name;c.source=s.value("source").toObject();l.scopes.insert(key,c);auto out=ok();out.insert("scope",key);return out;
 }
 if(op=="leave"&&s.size()==2){l.scopes.remove(s.value("scope").toString());return ok();}
 if(op=="public_armor_absence"&&s.size()==3){
  // Exact literal names reached by fixed Original common/standard-card families.
  // bazhen stays a literal armor-name query, never a private skill query.
  const auto name=s.value("name").toString();
  if(name!="SilverLion"&&name!="EightDiagram"&&name!="bazhen"&&name!="Vine"&&name!="IronArmor"&&name!="RenwangShield"&&name!="PeaceSpell"&&name!="Breastplate")return fail("Original public armor name outside finite source domain");
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original public armor target missing");
  const AIPlayerView*v=t==p?&q.worldView.self:nullptr;
  for(const auto&row:q.worldView.players)if(row.objectName==t->objectName())v=&row;
  if(!v||!v->equips.isEmpty()||!t->getEquips().isEmpty())return fail("Original public armor equipment domain is not empty");
  if(isPlayerArmorEffectNamePossible(name))return fail("Original public armor virtual effect is not proved");
  auto out=ok();out.insert("value",false);return out;
 }
 if(op=="silver_lion"){
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original SilverLion target missing");
  if(s.contains("scope")){
   const auto sc=l.scopes.constFind(s.value("scope").toString());
   if(s.size()!=3||sc==l.scopes.cend()||!stamp(*sc,q)||!sourceCard(r,q,p,sc->source,sc->flags))return fail("Original SilverLion call-tree scope missing");
  }else{
   if(s.size()!=2||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"
    ||q.choiceOptions.context.value("who")!=QJsonValue(t->objectName()))return fail("Original SilverLion inquiry mismatch");
  }
  // Empty public equipment makes the entire realEquip/matching loop empty.
  // No foreign skill, property, IgnoreArea mark or nullification rule is read.
  const AIPlayerView*v=t==p?&q.worldView.self:nullptr;
  for(const auto&row:q.worldView.players)if(row.objectName==t->objectName())v=&row;
  if(!v||!v->equips.isEmpty()||!t->getEquips().isEmpty())return fail("Original SilverLion equipped effect is not proved");
  if(isPlayerArmorEffectNamePossible(QStringLiteral("SilverLion")))return fail("Original SilverLion virtual effect is not proved");
  auto out=ok();out.insert("value",false);return out;
 }
 if(op=="read"&&s.size()==4){
  const QString name=s.value("reason").toString(),target=s.value("target").toString(),flags=s.value("flags").toString();
  auto*t=r.findPlayerByObjectName(target,true);const auto&ctx=q.choiceOptions.context;
  if(!reason(name)||!t||!zones(flags)||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"
   ||q.choiceOptions.reason!=name||ctx.value("who")!=QJsonValue(target)||ctx.value("flags")!=QJsonValue(flags))return fail("Original SD reader question mismatch");
  auto&o=owner(l,p->objectName());auto&marks=o.marks[name];const auto ids=full(t,flags);
  for(int id:ids){auto i=marks.find(id);if(i==marks.end()){if(!o.baseline)return fail("Original SD earlier flag is not observed");continue;}
   if(i->identity!=identity(r,p,id)||i->target!=target)return fail("Original SD card lifetime/location continuity unknown");
   if(!i->value)continue;
   // Clear at the reached source read, not after accepted decision. Prefix
   // survives later errors/native rejection just as source Card::setFlags.
   i->value=false;Capture c=capture(q,t,flags,{});c.selected=id;c.identity=i->identity;
   const QString token=nonce("isolated-original-sd-selected:");l.receipts.insert(token,c);
   auto out=ok();out.insert("selected",true);out.insert("receipt",QJsonObject{{"token",token},{"index",1},{"avoid_wounded_enemy_silver_lion",false}});return out;
  }
  auto out=ok();out.insert("selected",false);return out;
 }
 const QString scope=s.value("scope").toString();auto sc=l.scopes.constFind(scope);if(sc==l.scopes.cend()||!stamp(*sc,q)||!sourceCard(r,q,p,sc->source,sc->flags))return fail("Original SD active call-tree scope missing");
 // Insert after the existing sc/stamp check. This is an observation-only
 // query under the same sealed Snatch/Dismantlement source call tree.
 if(op=="can_get"&&s.size()==4){
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original canGet target missing");
  const auto key=s.value("key");
  if(key.isString()){
   if(!p->isAlive()||!t->isAlive()){auto out=ok();out.insert("value",false);return out;}
   const QString flags=key.toString();if(!zones(flags))return fail("Original canGet zone string invalid");
   // Player::canGet(target,flags) visits h,e,j in that order. A foreign
   // hand predicate can reveal hidden class/suit existence: authenticate ALL
   // selected hand faces before evaluating its native existential predicate.
   for(const auto zone:{QStringLiteral("h"),QStringLiteral("e"),QStringLiteral("j")}){
    if(!flags.contains(zone))continue;
    const auto ids=full(t,zone);
    for(int id:ids)if(!disclosed(q,p,t,id))return fail("Original canGet selected zone is not fully disclosed");
    for(int id:ids)if(p->canGet(t,id)){auto out=ok();out.insert("value",true);return out;}
   }
   auto out=ok();out.insert("value",false);return out;
  }
  if(key.isDouble()){
   const double raw=key.toDouble();if(!std::isfinite(raw)||std::floor(raw)!=raw||raw<0||raw>=Sanguosha->getCardCount())return fail("Original canGet material malformed");
   const int id=int(raw);if(!full(t,"hej").contains(id)||!disclosed(q,p,t,id))return fail("Original canGet material not a current disclosed target card");
   auto out=ok();out.insert("value",p->canGet(t,id));return out;
  }
  return fail("Original canGet key is neither exact ID nor zones");
 }
 if(op=="domain"&&s.size()==4){auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);const auto flags=s.value("flags").toString();if(!t||!zones(flags))return fail("Original SD target/zones invalid");
  Capture c=capture(q,t,flags,scope);const QString token=nonce("original-sd-domain:");l.samples.insert(token,c);auto out=ok();out.insert("token",token);out.insert("count",c.ids.size());return out;}
 if(op=="select"&&s.size()==5){auto it=l.samples.find(s.value("token").toString());const double ordinal=s.value("ordinal").toDouble(-1);if(it==l.samples.end()||!stamp(*it,q)||it->scope!=scope||!s.value("avoid").isBool()
  ||!std::isfinite(ordinal)||std::floor(ordinal)!=ordinal||ordinal<0||ordinal>=it->ids.size())return fail("Original SD source sample ordinal invalid");auto*t=r.findPlayerByObjectName(it->target,true);if(!t||full(t,it->flags)!=it->ids)return fail("Original SD sampled domain changed");int index=int(ordinal);
  if(s.value("avoid").toBool()){if(!t->isWounded()||!t->hasArmorEffect("SilverLion"))return fail("Original SD SilverLion prerequisites changed");if(Sanguosha->getCard(it->ids[index])==t->getArmor()){if(index+1<it->ids.size())++index;else if(index>0)--index;}}
  it->selected=it->ids[index];it->identity=identity(r,p,it->selected);auto out=ok();out.insert("handle",s.value("token"));return out;}
 if(op=="native_hand"&&s.size()==3){auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);if(!t)return fail("Original SD hand target missing");Capture c=capture(q,t,"h",scope);
  // Player::getRandomHandCardId preserves its own gameplay RNG stream/order.
  {GameRng::Binding gameplay(r.roomRuntime()->rng());c.selected=t->getRandomHandCardId();}auto out=ok();if(c.selected<0){out.insert("empty",true);return out;}c.identity=identity(r,p,c.selected);const auto key=nonce("original-sd-native-hand:");l.samples.insert(key,c);out.insert("handle",key);return out;}
 if(op=="write"&&s.size()==5){const auto name=s.value("reason").toString();auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);if(!t||name!=sc->flags)return fail("Original SD writer source/target mismatch");int id=-1;QString ident;
  const auto value=s.value("selection");if(value.isString()){auto i=l.samples.constFind(value.toString());if(i==l.samples.cend()||!stamp(*i,q)||i->scope!=scope||i->target!=t->objectName()||i->selected<0||full(t,i->flags)!=i->ids)return fail("Original SD selected handle expired");id=i->selected;ident=i->identity;}
  else if(value.isDouble()){double n=value.toDouble();if(!std::isfinite(n)||std::floor(n)!=n||n<0||n>=Sanguosha->getCardCount())return fail("Original SD disclosed selection invalid");id=int(n);if(!disclosed(q,p,t,id)||!full(t,"hej").contains(id))return fail("Original SD selected ID was not disclosed");ident=identity(r,p,id);}
  else return fail("Original SD selection shape invalid");if(ident!=identity(r,p,id))return fail("Original SD selected identity changed");
  owner(l,p->objectName()).marks[name].insert(id,Mark{ident,t->objectName(),true});return ok();}
 return fail("Original SD operation shape invalid");
}
bool resolveAIRound37SD(Room&r,ServerPlayer*p,const AIRequest&q,AIResult&result){
 const auto payload=result.action.randomCardChoice;const QString token=payload.value("token").toString();if(!token.startsWith("isolated-original-sd-selected:"))return false;
 if(!current(r,q,p)||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"||payload.size()!=3||payload.value("index")!=QJsonValue(1)||payload.value("avoid_wounded_enemy_silver_lion")!=QJsonValue(false))return false;
 auto&l=ledger(r);auto i=l.receipts.find(token);if(i==l.receipts.end()||!stamp(*i,q)||i->selected<0)return false;auto*t=r.findPlayerByObjectName(i->target,true);
 if(!t||q.choiceOptions.context.value("who")!=QJsonValue(i->target)||q.choiceOptions.context.value("flags")!=QJsonValue(i->flags)||full(t,i->flags)!=i->ids||identity(r,p,i->selected)!=i->identity)return false;
 result.action.selectedCardIds<<i->selected;result.action.randomCardChoiceResolved=true;l.receipts.erase(i);return true;
}
