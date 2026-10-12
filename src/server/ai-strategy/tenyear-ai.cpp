#include "ai-strategy/tenyear-ai.h"

// ---- tenyear-hc-bools ----
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
void projectAIZunweiBools(AIRequest&q,Room&r,ServerPlayer*p){
 auto row=q.worldView.strategyFacts.value("owner_skill_properties").toObject();
 const QStringList keys={"zunwei_draw","zunwei_recover","zunwei_equip"};for(const auto&k:keys)row.remove(k);
 const auto publish=[&]{if(row.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",row);};publish();
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return;
 // Native ZunweiCard effect27090–27111 is the current public owner writer.
 // Absent QVariant converts false exactly; no history or global choice grant.
 for(const auto&k:keys)row.insert(k,p->property(k.toUtf8().constData()).toBool());
 if(q.stateRevision!=r.roomRuntime()->stateRevision())return;publish();
}

// ---- tenyear-hc-current ----
#include "card.h"
void projectAIQuxiShawuFields(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("QuxiDeathPlayer");
 auto facts=r.worldView.strategyFacts.value("tenyear_hc").toObject();facts.remove("ShawuTarget");
 const auto publish=[&]{
  if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
  if(facts.isEmpty())r.worldView.strategyFacts.remove("tenyear_hc");else r.worldView.strategyFacts.insert("tenyear_hc",facts);
 };publish();
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(p)
  ||r.kind!=AIRequest::UseCard||(r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE))return;
 if(r.pattern=="@@quxi2"&&r.prompt=="@quxi2"&&r.handlingMethod==Card::MethodNone){
  // ol.cpp11718–20 public property write -> question -> clear. Empty stays empty.
  // The AI's getAliveSiblings excludes the actual dead target; do not repair it.
  props.insert("QuxiDeathPlayer",p->property("QuxiDeathPlayer").toString());
 }else if(r.pattern=="@@shawu"&&r.prompt.startsWith("@shawu:")){
  // tenyear2.cpp28792–94 exact owner tag -> prompted target -> clear.
  const QVariant tag=p->getTag("ShawuTarget");ServerPlayer*target=tag.value<ServerPlayer*>();
  if(!target){if(!tag.isValid()||tag.isNull())facts.insert("ShawuTarget",false);}
  else if(room.getPlayers().contains(target)&&target->getRoom()==&room
   &&r.prompt=="@shawu:"+target->objectName())facts.insert("ShawuTarget",target->objectName());
 }
 publish();
}

// ---- tenyear-hc-extra-planning ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/owner.h"
#include "engine.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool integer(const QJsonValue&v,int&n){const double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d!=std::floor(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max())return false;n=int(d);return true;}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool question(const AIRequest&r,AIRequest::DecisionKind kind,const QString&why,const QString&q){return r.kind==kind&&r.choiceOptions.reason==why&&r.choiceOptions.question==q;}
bool response(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern==pattern;}
bool slashResponse(const AIRequest&r){return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern.contains("slash",Qt::CaseInsensitive);}
bool instance(const ServerPlayer*p,const QString&name){for(const auto&i:p->getSkillInstances())if(i.skillName==name)return true;return false;}
bool same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
const Card*knownWrapper(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(same(v,c,id,room,p))return c;
 for(const auto&v:r.worldView.discardPile)if(same(v,c,id,room,p))return c;
 auto players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&row:players){
  for(const auto&v:row.knownCards)if(same(v,c,id,room,p))return c;
  for(const auto&v:row.equips)if(same(v,c,id,room,p))return c;
  for(const auto&v:row.judgingArea)if(same(v,c,id,room,p))return c;
  for(const auto&pile:row.piles)if(row.objectName==p->objectName()||pile.open)for(const auto&v:pile.cards)if(same(v,c,id,room,p))return c;
 }return nullptr;
}
const Card*material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zones){
 if(zones.contains('p')){
  const auto complete=r.worldView.strategyFacts.value("round45_hand_pile_ids");
  if(complete.isArray()&&complete.toArray().contains(id)&&p->getHandPile().contains(id))
   if(const auto*c=knownWrapper(r,room,p,id))return c;
 }
 if(room.getCardOwner(id)!=p)return nullptr;
 if(zones.contains('h')&&room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))
  for(const auto&v:r.worldView.handCards)if(same(v,Sanguosha->getCard(id),id,room,p))return Sanguosha->getCard(id);
 if(zones.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)for(const auto&v:r.worldView.self.equips)if(same(v,c,id,room,p))return c;
 if(zones=="jbbing"&&p->getPile("jbbing").contains(id))
  for(const auto&pile:r.worldView.self.piles)if(pile.name=="jbbing")for(const auto&v:pile.cards)if(same(v,Sanguosha->getCard(id),id,room,p))return Sanguosha->getCard(id);
 return nullptr;
}
bool publicName(const AIRequest&r,const QString&name){return !name.isEmpty()&&r.worldView.strategyFacts.value("pattern_names").toArray().contains(name);}
}
void projectAICuixinContext(AIRequest&r,const QVariant&data){
 r.choiceOptions.context.remove("round49_cuixin_name");
 if(!question(r,AIRequest::SkillInvoke,"cuixin","askForSkillInvoke")||data.metaType().id()!=QMetaType::QString)return;
 const auto parts=data.toString().split(':');
 if(parts.size()==2&&(parts[0]=="shangjia"||parts[0]=="xiajia"))r.choiceOptions.context.insert("round49_cuixin_name",parts[1]);
}
bool aiTenyearHcConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()||!aiRangeRequestCurrent(room,r,p))return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("subcards").isArray())return false;
 const auto key=s.value("source_key").toString(),raw=s.value("skill").toString(),name=s.value("name").toString();
 int suit,number;if(!integer(s.value("suit"),suit)||!integer(s.value("number"),number))return false;
 const auto costs=s.value("subcards").toArray();if(costs.size()>1)return false;
 int id=-1;if(!costs.isEmpty()&&(!integer(costs.first(),id)||id<0))return false;
 const bool defaultFace=suit==int(Card::NoSuit)&&number==0;
 // Every exact response/question below authenticates its own current callback;
 // recipients such as Zhenge need not own the source skill. No added hasSkill,
 // mark, availability, invalidity, jilei or using gate before source evaluation.
 if(key=="juanxia"||key=="tenyearjuanxia")return defaultFace&&costs.isEmpty()&&raw=="_"+key
  &&question(r,AIRequest::Choice,key,"askForChoice")&&r.choiceOptions.choices.contains(name)&&publicName(r,name);
 if(key=="@@jueman!")return defaultFace&&costs.isEmpty()&&raw=="_jueman"&&response(r,key)
  &&r.prompt.split(':').value(1)==name&&p->property("JuemanCardName").toString()==name&&publicName(r,name);
 // Fixed native producer asks @@libang!, whereas the donor registers @@libang.
 // Do not alias these questions or repair the later source string-call error.
 if(key=="@@libang")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="_libang"&&response(r,key);
 if(key=="@@zhenge!")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="_zhenge"&&response(r,key);
 if(key=="@@pingxiang"||key=="PingxiangCard")return defaultFace&&costs.isEmpty()&&name=="fire_slash"&&raw=="_pingxiang"
  &&(key=="@@pingxiang"?response(r,key):play(r)&&instance(p,"pingxiang"));
 if(key=="#xunshi")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="xunshi"&&question(r,AIRequest::PlayersChosen,key,"askForPlayersChosen");
 if(key=="cuixin")return defaultFace&&costs.isEmpty()&&raw=="cuixin"&&question(r,AIRequest::SkillInvoke,key,"askForSkillInvoke")
  &&r.choiceOptions.context.value("round49_cuixin_name")==QJsonValue(name)&&publicName(r,name);
 if(key=="SecondZhanyiViewAsBasicCard"){
  if(!play(r)||!instance(p,"secondzhanyi")||!costs.isEmpty()||(raw!=""&&raw!="secondzhanyi")||!publicName(r,name))return false;
  // The source clone reads the issued SkillCard's derived face, with no costs
  // copied into its hypothetical Basic clone. Its native prototype has one own
  // Basic material: preserve that face union, not arbitrary suit/number input.
  for(const auto&v:r.worldView.handCards){const auto*c=material(r,room,p,v.effectiveId,"hp");if(c&&c->isKindOf("BasicCard")&&suit==int(c->getSuit())&&number==c->getNumber())return true;}
  const auto hp=r.worldView.strategyFacts.value("round45_hand_pile_ids");if(hp.isArray())for(const auto&v:hp.toArray()){int n;if(integer(v,n)){const auto*c=material(r,room,p,n,"p");if(c&&c->isKindOf("BasicCard")&&suit==int(c->getSuit())&&number==c->getNumber())return true;}}
  return false;
 }
 if(!defaultFace)return false;
 QString skill,zones="hep";bool addFirst=false;enum Filter{Any,Equip,WeaponArmor,Black,Red}filter=Any;
 if(key=="@@saowei"){
  if(!response(r,key)||name!="slash"||raw!="saowei")return false;zones="h";
 }else{
  if(key=="tenyearjinggong"||key=="kanpodz"){
   skill=key;addFirst=true;zones=key=="kanpodz"?"hp":"hep";filter=key=="tenyearjinggong"?Equip:Any;
   if(name!="slash"||(!play(r)&&!slashResponse(r)))return false;
  }else if(key=="qiaoli"){skill=key;filter=Equip;if(!play(r)||name!="duel")return false;
  }else if(key=="tenyearlingyin"){skill=key;zones="he";filter=WeaponArmor;if(!play(r)||name!="duel")return false;
  }else if(key=="jibing"){skill=key;zones="jbbing";if(!play(r)||name!="slash")return false;
  }else if(key=="spniluan"||key=="SpNiluanCard"){skill="spniluan";filter=Black;if(!play(r)||name!="slash")return false;
  }else if(key=="WeiwuCard"){skill="weiwu";filter=Red;if(!play(r)||name!="snatch")return false;
  }else return false;
  if(!instance(p,skill)||(raw!=skill&&!(addFirst&&raw.isEmpty()&&costs.size()==1)))return false;
 }
 if(costs.isEmpty())return !addFirst; // setSkillName before addSubcard.
 const Card*c=material(r,room,p,id,zones);if(!c)return false;
 if(filter==Equip&&c->getTypeId()!=Card::TypeEquip)return false;
 if(filter==WeaponArmor&&!c->isKindOf("Weapon")&&!c->isKindOf("Armor"))return false;
 if(filter==Black&&!c->isBlack())return false;
 if(filter==Red&&!c->isRed())return false;
 return true;
}

// ---- tenyear-hc-globals ----
#include "ai-strategy/lua-seal.h"
#include "lua-runtime.h"
#include <QPointer>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool tenyearHcGlobalsCurrent(Room&r,ServerPlayer*p,const AIRequest&q){auto*rt=r.roomRuntime();return rt&&!rt->isClosing()&&rt->lua().isCurrentThreadOwner()
 &&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running&&p&&p->getRoom()==&r&&q.decisionId&&q.kind==AIRequest::Activate
 &&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
struct Write{Room*r;ServerPlayer*p;const AIRequest*q;QString key;QByteArray bytes;QPointer<ServerPlayer>target;
 bool zero=false,playerValue=false,reached=false;lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr;};
bool live(const Write&w){auto*rt=w.r->roomRuntime();return tenyearHcGlobalsCurrent(*w.r,w.p,*w.q)&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation;}
bool roots(lua_State*L,const Write&w){Stack st(L);AILuaInspect::globals(L);if(lua_topointer(L,-1)!=w.globals)return false;
 AILuaInspect::field(L,-1,"sgs");return lua_istable(L,-1)&&lua_topointer(L,-1)==w.space
  &&matchAINativeNamespaceWrite(*w.r,L,"sgs",w.key);}
int assign(lua_State*L){if(!lua_checkstack(L,2048))return luaL_error(L,"source global assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));if(!w||!live(*w)||!roots(L,*w)||lua_topointer(L,1)!=w->space)return luaL_error(L,"source global assignment context changed");
 lua_pushlstring(L,w->bytes.constData(),size_t(w->bytes.size()));
 if(w->zero)lua_pushinteger(L,0);else if(!w->playerValue)lua_pushnil(L);else if(!w->target||!pushAISlotPlayer(L,w->target))return luaL_error(L,"source global Player binding unavailable");
 if(!live(*w)||!roots(L,*w)||(w->playerValue&&(!w->target||w->target->getRoom()!=w->r)))return luaL_error(L,"source global assignment changed during allocation");
 // Ordinary source syntax is preserved: the exact captured SWIG namespace
 // __newindex and missing .set entry are proved; no rawset shortcut is used.
 lua_settable(L,1);w->reached=true;return 0;
}
}
QJsonObject writeAITenyearHCGlobal(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&v){
 auto fail=[](){return QJsonObject{{"known",false}};};
 if(v.size()!=3||v.value("op")!="source_global"||!v.value("key").isString()||!v.value("value").isObject()||!tenyearHcGlobalsCurrent(r,p,q))return fail();
 const QString key=v.value("key").toString();const QString skill=key=="xiaowu_n"?"xiaowu":key=="yujue_target"?"yujue":key=="secondyujue_target"?"secondyujue":QString();
 if(skill.isEmpty()||!p->hasSkill(skill)||!tenyearHcGlobalsCurrent(r,p,q))return fail();
 Write w;w.r=&r;w.p=p;w.q=&q;w.key=key;w.bytes=key.toUtf8();const auto value=v.value("value").toObject();
 if(key=="xiaowu_n"){
  if(value.size()!=2||value.value("kind")!="scalar"||!value.value("value").isDouble()||value.value("value").toDouble()!=0)return fail();w.zero=true;
 }else if(value.size()==1&&value.value("kind")=="nil"){
  // Reached source getYujueTarget returning nil clears the actual global.
 }else if(value.size()==2&&value.value("kind")=="player"&&value.value("name").isString()){
  const QString name=value.value("name").toString();bool disclosed=q.worldView.self.objectName==name;for(const auto&row:q.worldView.players)if(row.objectName==name)disclosed=true;
  if(!disclosed)return fail();w.playerValue=true;w.target=r.findPlayerByObjectName(name,true);if(!w.target||w.target->getRoom()!=&r)return fail();
 }else return fail();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,2048))return fail();Stack st(L);
 w.L=L;w.generation=rt->lua().generation();AILuaInspect::globals(L);w.globals=lua_topointer(L,-1);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail();w.space=lua_topointer(L,-1);const int space=lua_absindex(L,-1);
 if(!roots(L,w))return fail();lua_pushcfunction(L,assign);lua_pushvalue(L,space);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 if(status!=LUA_OK||!w.reached||!live(w)||!roots(L,w))return fail();return{{"known",true},{"written",true}};
}

// ---- tenyear-hc-gongjian ----
void projectAIGongjian(AIRequest&q,Room&r,ServerPlayer*p){
 auto f=q.worldView.strategyFacts.value("tenyear_hc").toObject();f.remove("gongjianData");f.remove("gongjian_slash_targets");
 const auto publish=[&]{if(f.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",f);};publish();
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="gongjian"||q.choiceOptions.question!="askForSkillInvoke")return;
 // Fixed Gongjian::trigger stores TargetSpecified data in this owner's tag
 // immediately before this question. The AI reads ONLY to:list, never card/from.
 // Preserve QVariant::value's empty CardUseStruct and resulting source nil error.
 const auto use=p->getTag("gongjianData").value<CardUseStruct>();QJsonArray to;
 for(auto*t:use.to){if(!t||!r.getPlayers().contains(t)||t->getRoom()!=&r)return;to.append(t->objectName());}
 f.insert("gongjianData",QJsonObject{{"to",to}});
 // Only fixed native writer is the public CardFinished target-name list.
 // This is current public room metadata, not a retained private policy slot.
 f.insert("gongjian_slash_targets",QJsonArray::fromStringList(r.getTag("gongjian_slash_targets").toStringList()));publish();
}

// ---- tenyear-hc-names ----
namespace {
bool tenyearHcNamesLive(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
bool tenyearHcNamesResponse(const AIRequest&q){return q.kind==AIRequest::UseCard&&(q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);}
bool jil(const AIRequest&q,ServerPlayer*p){return tenyearHcNamesResponse(q)&&q.pattern=="@@jilie"&&q.prompt=="jilie0:"+p->property("jilieCn").toString();}
}
void projectAIRecordedOwnerNames(AIRequest&q,Room&r,ServerPlayer*p){
 const QStringList all={"SkillDescriptionRecord_fengying","SkillDescriptionRecord_fuping","choulve_damage_card","jilieCn","jingong_tricks","tenyearjingong_tricks"};
 auto props=q.worldView.strategyFacts.value("owner_skill_properties").toObject();for(const auto&k:all)props.remove(k);
 const auto publish=[&]{if(props.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();if(!tenyearHcNamesLive(q,r,p))return;
 QStringList keys;
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY)keys={"SkillDescriptionRecord_fengying","SkillDescriptionRecord_fuping","jingong_tricks","tenyearjingong_tricks"};
 if(q.kind==AIRequest::RespondCard)keys<<"SkillDescriptionRecord_fuping";
 if((q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen")||(tenyearHcNamesResponse(q)&&q.pattern=="@@choulve!"))keys<<"choulve_damage_card";
 if(jil(q,p))keys<<"jilieCn";
 for(const auto&k:keys)props.insert(k,p->property(k.toUtf8().constData()).toString());publish();
}
bool aiJilieConstruction(const AIRequest&q,Room&r,ServerPlayer*p,const QJsonObject&s){
 if(!tenyearHcNamesLive(q,r,p)||!jil(q,p)||s.size()!=6||s.value("source_key")!="@@jilie"||s.value("skill")!="_jilie"||!s.value("suit").isDouble()||s.value("suit").toDouble()!=6||!s.value("number").isDouble()||s.value("number").toDouble()!=0||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString n=p->property("jilieCn").toString();
 return !n.isEmpty()&&s.value("name").isString()&&s.value("name").toString()==n&&q.worldView.strategyFacts.value("owner_skill_properties").toObject().value("jilieCn").toString()==n;
}

// ---- tenyear-hc-record-constructors ----
bool aiRecordedNameConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||!aiRangeRequestCurrent(r,q,p))return false;
 const QSet<QString> fields{"source_key","skill","name","suit","number","subcards"};if(s.size()!=fields.size())return false;for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("skill").isString()||!s.value("name").isString()||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),raw=s.value("skill").toString(),name=s.value("name").toString();const auto costs=s.value("subcards").toArray();QString property;
 if(key=="choulve"||key=="@@choulve!"){
  if(!((key=="choulve"&&q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen")||(key=="@@choulve!"&&q.kind==AIRequest::UseCard&&q.pattern==key&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)))return false;
  return costs.isEmpty()&&(raw.isEmpty()||raw=="_choulve")&&name==p->property("choulve_damage_card").toString();
 }
 if(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty())return false;
 if(key=="fengying"||key=="fuping")property="SkillDescriptionRecord_"+key;
 else if(key=="jingong"||key=="tenyearjingong")property=key+"_tricks";else return false;
 if(!p->property(property.toUtf8().constData()).toString().split('+').contains(name)||(raw!=key&&!raw.isEmpty())||costs.size()>1)return false;
 if(costs.isEmpty())return key=="fengying"||raw.isEmpty();
 const auto v=costs.first();const double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d!=std::floor(d)||d>std::numeric_limits<int>::max())return false;
 const int id=int(d);if(r.getCardOwner(id)!=p)return false;const Card*c=r.getCard(id);if(!c)return false;const AICardView*face=nullptr;
 if(r.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){for(const auto&x:q.worldView.handCards)if(x.effectiveId==id){face=&x;break;}}
 else if(key!="fengying"&&r.getCardPlace(id)==Player::PlaceEquip){for(const auto&x:q.worldView.self.equips)if(x.effectiveId==id){face=&x;break;}}
 else if((key=="jingong"||key=="tenyearjingong")&&p->getPile("wooden_ox").contains(id)){for(const auto&pile:q.worldView.self.piles)if(pile.name=="wooden_ox")for(const auto&x:pile.cards)if(x.effectiveId==id){face=&x;break;}}
 if(!face||face->objectName!=c->objectName()||face->className!=c->getClassName()||face->suit!=int(c->getSuit())||face->number!=c->getNumber()||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=aiLegacyCardIdentity(r,const_cast<ServerPlayer*>(p),c))return false;
 if(key=="fengying")return raw==key&&c->getNumber()<=p->getMark("&dgrlzjiao");
 if(key=="fuping")return c->getTypeId()!=Card::TypeBasic;
 return c->isKindOf("Slash")||c->isKindOf("EquipCard");
}

// ---- notified-context ----
namespace {
bool notifiedContextLive(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
QJsonArray ids(const QVariant&v){QJsonArray a;if(v.userType()==qMetaTypeId<QList<int>>()){for(int id:v.value<QList<int>>())a.append(id);}else for(const auto&x:v.toList())a.append(x.toInt());return a;}
}
void projectAINotifiedPileContexts(AIRequest&q,Room&r,ServerPlayer*p){
 auto facts=q.worldView.strategyFacts.value("tenyear_hc").toObject();
 const QStringList all={"sushouForAI","xingzuoForAI","guanxuhandForAI","guanxudrawpileForAI","guanxuForAI","zhengyueForAI","yuanrongForAI"};
 for(const auto&k:all)facts.remove(k);
 q.worldView.strategyFacts.remove("round49_notified_faces");
 auto props=q.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("mythyuqi_AI");
 const auto publish=[&]{if(facts.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",facts);if(props.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();
 if(!notifiedContextLive(q,r,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)return;
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

// ---- notified-costs ----
#include "card-lifetime-manager.h"
#include "skill.h"
#include "tenyear.h"
#include <QRegularExpression>
#include <QWeakPointer>
#include <typeinfo>
namespace {
struct Frame {Room*room;ServerPlayer*viewer;ServerPlayer*other;QList<int>offered;QString prompt;int depth;};
thread_local QList<Frame> frames;
struct Receipt {QWeakPointer<AIConversionLedger>ledger;quint64 decision,revision;QString viewer,wire;AICardSpec spec;SkillInstanceRef activation,source;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){}QList<Receipt>rows;};
Ledger*ledger(Room&r,bool make=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return make?new Ledger(r):nullptr;}
bool notifiedCostsLive(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->getPlayers().contains(p)&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
AICardSpec specOf(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool notifiedCostsSame(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
QJsonObject json(const AICardSpec&s){QJsonArray ids;for(int id:s.subcardIds)ids<<id;return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",ids},{"conversion_id",s.conversionId}};}
QList<int>tagIds(const QVariant&v){if(v.userType()==qMetaTypeId<QList<int>>())return v.value<QList<int>>();QList<int>ids;for(auto x:v.toList())ids<<x.toInt();return ids;}
QJsonArray array(const QList<int>&ids){QJsonArray a;for(int id:ids)a<<id;return a;}
bool disclosed(const AIRequest&q,ServerPlayer*p,ServerPlayer*owner,int id){
 // Find independently issued current face FIRST. Notification printed faces do
 // not enter this proof; never compare a hidden wrapper to the printed catalog.
 const AICardView*known=nullptr;
 if(owner==p){for(const auto&v:q.worldView.handCards)if(v.effectiveId==id){known=&v;break;}}
 else for(const auto&v:q.worldView.players)if(v.objectName==owner->objectName())for(const auto&c:v.knownCards)if(c.effectiveId==id){known=&c;break;}
 if(!known||!known->nativeIdentity||known->legacyIdentityToken.isEmpty())return false;
 Room&r=*p->getRoom();const Card*c=r.getCard(id);
 return c&&r.getCardOwner(id)==owner&&r.getCardPlace(id)==Player::PlaceHand
  &&globalCardLifetimeManager().liveToken(c)==known->nativeIdentity
  &&aiLegacyCardIdentity(r,p,c)==known->legacyIdentityToken
  &&c->getEffectiveId()==id&&!c->isVirtualCard()&&known->objectName==c->objectName()
  &&known->className==c->getClassName()&&known->suit==int(c->getSuit())&&known->number==c->getNumber()
  &&known->cardId==c->getId()&&known->virtualCard==c->isVirtualCard()&&known->subcardIds==c->getSubcards()
  &&known->skillName==c->getSkillName(false)&&known->sourceSkillName==c->getSourceSkillName()
  &&known->kindOfNames==c->getKindOfNames()&&known->typeId==int(c->getTypeId())
  &&known->red==c->isRed()&&known->black==c->isBlack()&&known->canRecast==c->canRecast()
  &&known->willThrow==c->willThrow()&&known->targetFixed==c->targetFixed()
  &&known->handlingMethod==int(c->getHandlingMethod())&&known->damageCard==c->isDamageCard()
  &&known->singleTarget==c->isSingleTargetCard()&&known->legacyStringKey==c->toString();
}
bool prepare(const AIRequest&q,ServerPlayer*p,const QString&wire,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&why){
 const auto fail=[&](const QString&s){why=s;return false;};
 if(!notifiedCostsLive(q,p)||frames.isEmpty())return fail("Sushou source question is not current");
 Room&r=*p->getRoom();const Frame f=frames.last();
 if(f.room!=&r||f.viewer!=p||!f.other||!r.getPlayers().contains(f.other)||r.getCurrent()!=f.other||f.other==p
  ||r.roomRuntime()->lua().protectedCallDepth()!=f.depth||q.kind!=AIRequest::UseCard
  ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse
  ||q.pattern!="@@sushou"||q.prompt!=f.prompt)return fail("Sushou exact compiled inquiry scope differs");
 const auto w=readAIStrategyNotifiedPile(r,p,"#sushou");const auto seq=tagIds(p->getTag("sushouForAI"));
 if(seq.isEmpty()||seq!=f.offered||w.value("ordered_ids").toArray()!=array(seq)
  ||q.worldView.strategyFacts.value("tenyear_hc").toObject().value("sushouForAI").toArray()!=array(seq))return fail("Sushou recipient notification changed");
 // Full source loads every offered card before valuation/selection.
 for(int id:seq){const auto loc=w.value("locations").toObject().value(QString::number(id)).toObject();
  if(loc.value("owner").toString()!=f.other->objectName()||loc.value("place").toInt(-1)!=int(Player::PlaceHand)
   ||!disclosed(q,p,f.other,id))return fail("Sushou source needs independently disclosed current foreign hand face");}
 static const QRegularExpression syntax("^@SushouCard=([0-9]+)(\\+[0-9]+)+$");
 if(!syntax.match(wire).hasMatch())return fail("Sushou source raw wire malformed");
 QList<int>costs;for(const auto&s:wire.mid(QStringLiteral("@SushouCard=").size()).split('+')){bool ok=false;int id=s.toInt(&ok);if(!ok||id<0||QString::number(id)!=s)return fail("Sushou source integer wire malformed");costs<<id;}
 if(costs.size()%2||costs.size()>p->getLostHp()*2)return fail("Sushou source pair count differs");
 QMap<int,int>offeredCounts;for(int id:seq)++offeredCounts[id];QSet<int>own;
 for(int i=0;i<costs.size();i+=2){int other=costs[i],self=costs[i+1];
  if(offeredCounts.value(other)<=0||own.contains(self)||!disclosed(q,p,p,self))return fail("Sushou ordered foreign/own source cost pair differs");
  --offeredCounts[other];own.insert(self);
 }
 // Card::Parse is the fixed legacy AI ABI; no GUI fake-pile viewFilter/viewAs.
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Sushou source raw parser rejected wire");
 CardUseStruct candidate(parsed,p); // manager event-payload lease; Parse scheduled deleteLater.
 if(typeid(*parsed)!=typeid(SushouCard)||parsed->getSubcards()!=costs||parsed->getHandlingMethod()!=Card::MethodNone||parsed->willThrow()||!parsed->targetFixed())return fail("Sushou native raw product differs");
 // Fixed ordinary V1 early return is inquiry-free. Attached alternatives would
 // ask a provider-choice question; refuse that different native continuation.
 bool ordinary=false;int attached=0;
 for(const auto&i:p->getSkillInstances())if(i.skillName=="sushou"){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(i.skillName,i.instanceID,"legacy_activation_lifecycle").toBool())++attached;else ordinary=true;}
 if(!ordinary&&attached)return fail("Sushou attached-provider reconstruction is outside the fixed ordinary inquiry");
 if(!admit||!admit(candidate)||!notifiedCostsLive(q,p)||readAIStrategyNotifiedPile(r,p,"#sushou")!=w||tagIds(p->getTag("sushouForAI"))!=seq)return fail("Sushou native reconstruction rejected or changed source context");
 if(candidate.card!=parsed||candidate.card->getSubcards()!=costs)return fail("Sushou resolver replaced original raw product");
 use=candidate;return true;
}
}
const Card*askAISushou(Room&r,ServerPlayer*p,ServerPlayer*other,const QList<int>&ids,const QString&prompt){
 frames<<Frame{&r,p,other,ids,prompt,r.roomRuntime()->lua().protectedCallDepth()};
 struct Pop{~Pop(){frames.removeLast();}}pop;
 return r.askForUseCard(p,"@@sushou",prompt);
}
QJsonObject issueAISushouNotifiedCosts(const AIRequest&q,ServerPlayer*p,const QString&wire,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString why;if(!prepare(q,p,wire,admit,use,why))return{{"error",why},{"key","sushou.notified_costs"}};
 if(!q.conversionLedger)return{{"error","Sushou active receipt ledger missing"}};
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;
 Receipt proof{q.conversionLedger,q.decisionId,q.stateRevision,p->objectName(),wire,specOf(use.card,id),use.activationRef,use.sourceRef};
 AICardConversionView marker;marker.conversionId=id;marker.policyClass="@r51_sushou_raw_costs";marker.name=proof.spec.name;marker.available=false;
 q.conversionLedger->tickets<<marker;auto*l=ledger(*p->getRoom(),true);
 for(auto it=l->rows.begin();it!=l->rows.end();)if(it->ledger.isNull())it=l->rows.erase(it);else ++it;
 l->rows<<proof;return{{"card_spec",json(proof.spec)},{"targets",QJsonArray()}};
}
bool hasAISushouNotifiedCosts(const AIRequest&q,int id){if(q.conversionLedger)for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@r51_sushou_raw_costs")return true;return false;}
bool applyAISushouNotifiedCosts(const AIRequest&q,ServerPlayer*p,const AICardSpec&s,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!notifiedCostsLive(q,p)||!targets.isEmpty()||!hasAISushouNotifiedCosts(q,s.conversionId))return false;auto*l=ledger(*p->getRoom());if(!l)return false;
 Receipt proof;bool found=false;for(const auto&v:l->rows)if(v.ledger.toStrongRef()==q.conversionLedger&&v.decision==q.decisionId&&v.revision==q.stateRevision&&v.viewer==p->objectName()&&notifiedCostsSame(v.spec,s)){proof=v;found=true;break;}
 if(!found)return false;CardUseStruct rebuilt;QString why;
 if(!prepare(q,p,proof.wire,admit,rebuilt,why)||!notifiedCostsSame(specOf(rebuilt.card,s.conversionId),s)||rebuilt.activationRef!=proof.activation||rebuilt.sourceRef!=proof.source)return false;
 use=rebuilt;return true;
}

// ---- notified ----
namespace {
bool notifiedLive(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
QJsonArray ordered(ServerPlayer*p,const QString&s){QJsonArray a;for(const auto&v:p->getTag(s+"ForAI").toList())a.append(v.toInt());return a;}
}
QString aiNotifiedPileSkill(const AIRequest&q){
 if(q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse)return {};
 if(q.pattern=="@@chuiti"&&q.prompt=="@chuiti")return "chuiti";
 if(q.pattern=="@@boxuan"&&q.prompt=="boxuan1")return "boxuan";
 if(q.pattern=="@@tanluan"&&q.prompt=="tanluan0")return "tanluan";
 return {};
}
void projectAINotifiedPile(AIRequest&q,Room&r,ServerPlayer*p){
 auto facts=q.worldView.strategyFacts.value("tenyear_hc").toObject();facts.remove("chuitiForAI");facts.remove("boxuanForAI");facts.remove("tanluanForAI");
 if(facts.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",facts);
 const QString s=aiNotifiedPileSkill(q);if(s.isEmpty()||!notifiedLive(q,r,p))return;
 const auto witness=readAIStrategyNotifiedPile(r,p,"#"+s);
 if(!q.notifiedPileBound){q.notifiedPileBound=true;q.notifiedPileWitness=witness;}
 if(q.notifiedPileWitness!=witness)return;
 const auto ids=ordered(p,s);const auto cards=witness.value("cards").toObject();
 if(!ids.isEmpty()&&(!witness.value("ordered_ids").isArray()||witness.value("ordered_ids").toArray()!=ids))return;
 for(const auto&v:ids)if(!cards.contains(QString::number(v.toInt())))return;
 // Empty QVariant list is a known source empty loop, not a fabricated offering.
 facts.insert(s+"ForAI",ids);q.worldView.strategyFacts.insert("tenyear_hc",facts);
 for(const auto&v:ids)if(witness.value("current_card_ids").toArray().contains(v)){const Card*c=r.getCard(v.toInt());if(c&&!q.notifiedPileIdentities.contains(v.toInt()))q.notifiedPileIdentities.insert(v.toInt(),aiLegacyCardIdentity(r,p,c));}
}
bool authorizeAINotifiedPileEvaluation(const AIRequest&q,Room&r,ServerPlayer*p,int id,QJsonObject*out){
 const QString s=aiNotifiedPileSkill(q);if(s.isEmpty()||id<0||!notifiedLive(q,r,p)||!q.notifiedPileBound)return false;
 const auto w=readAIStrategyNotifiedPile(r,p,"#"+s);
 if(w.isEmpty()||w!=q.notifiedPileWitness||!w.value("current_card_ids").toArray().contains(id))return false;
 const auto ids=ordered(p,s);if(!w.value("ordered_ids").isArray()||w.value("ordered_ids").toArray()!=ids||!ids.contains(id)||ids!=q.worldView.strategyFacts.value("tenyear_hc").toObject().value(s+"ForAI").toArray())return false;
 const auto f=w.value("cards").toObject().value(QString::number(id)).toObject();const Card*c=r.getCard(id);
 if(!c||q.notifiedPileIdentities.value(id).isEmpty()||q.notifiedPileIdentities.value(id)!=aiLegacyCardIdentity(r,p,c)||c->isVirtualCard()||c->getEffectiveId()!=id||f.isEmpty()||f.value("name").toString()!=c->objectName()||f.value("class_name").toString()!=c->getClassName()||f.value("suit").toInt()!=int(c->getSuit())||f.value("number").toInt()!=c->getNumber())return false;
 if(out)*out=f;return true;
}
bool authorizeAINotifiedPilePhysical(const AIRequest&q,Room&r,ServerPlayer*p,const SkillInstanceRef&a,const SkillInstanceRef&source,int id){
 if(!authorizeAINotifiedPileEvaluation(q,r,p,id))return false;const QString s=aiNotifiedPileSkill(q);
 if(!a.isValid()||!source.isValid()||a.ownerObjectName!=p->objectName()||a.key.skillName!=s||!p->hasSkillInstance(s,a.key.instanceID)||p->isSkillInvalid(s,a.key.instanceID))return false;
 auto root=r.resolveSkillInstanceRootRef(a);if(!root.isValid())root=a;if(root!=source)return false;
 bool issued=q.hasSkillActionContext&&q.skillActionContext.activationRef==a&&q.skillActionContext.sourceRef==source;
 for(const auto&v:q.skillActions)if(v.activationRef==a&&v.sourceRef==source)issued=true;
 const auto*v=Sanguosha->getViewAsSkill(s);if(!issued||!v||dynamic_cast<const ViewAsSkillV2*>(v)||v->getExpandPile()!="#"+s||!v->isEnabledAtResponse(p,q.pattern))return false;
 const Card*c=r.getCard(id);if(!c)return false;
 if(s=="tanluan"&&(Sanguosha->getCurrentCardUsePattern()!=q.pattern||Sanguosha->getCurrentCardUseReason()!=q.reason))return false;
 // Source submits c:toString(), i.e. original physical ID. The legacy Room
 // path does NOT replay the client-only #pile viewFilter. Do not invent a
 // server pile or convert this to ActiveSkillCard. V1 viewAs returns c itself.
 const Card*product=v->viewAs(QList<const Card*>{c});const bool same=product==c;
 if(product&&product!=c&&product->isVirtualCard())const_cast<Card*>(product)->deleteLater();
 return same&&Sanguosha->getViewAsSkill(s)==v&&r.getCard(id)==c&&notifiedLive(q,r,p)&&authorizeAINotifiedPileEvaluation(q,r,p,id);
}

// ---- jijiao ----
#include "ai-strategy/native-registry.h"

namespace {
bool scope(const AIRequest &r, const ServerPlayer *p, const std::function<bool()> &query = {}) {
    return p && p->getRoom() && Sanguosha && Sanguosha->currentRoom()==p->getRoom()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && r.pattern.isEmpty() && r.viewerObjectName==p->objectName()
        && r.worldView.self.objectName==p->objectName()
        && p->getRoom()->getCurrent()==p && p->getPhase()==Player::Play
        && p->isAlive() && (query ? query() : p->hasSkill("jijiao"))
        && aiRangeRequestCurrent(*p->getRoom(),r,p);
}
bool exactDiscard(const AICardView &v, const Card *c) {
    // Only called after CURRENT PUBLIC pile membership and mapping agree.
    return c && v.nativeIdentity && globalCardLifetimeManager().isLive(v.nativeIdentity)
        && v.nativeIdentity->address==c && v.cardId==c->getId()
        && v.effectiveId==c->getEffectiveId() && v.objectName==c->objectName()
        && v.className==c->getClassName() && v.suit==int(c->getSuit())
        && v.number==c->getNumber() && v.skillName==c->getSkillName(false)
        && v.subcardIds==c->getSubcards() && v.virtualCard==c->isVirtualCard();
}
}
void projectAIJijiao(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto nativeHasJijiao = [&] { return ([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,p,gameLua));
        return p->hasSkill("jijiao"); }()); };
    auto facts=r.worldView.strategyFacts.value("tenyear_hc").toObject();
    facts.remove("JijiaoRecord"); facts.remove("jijiao_discard");
    r.worldView.strategyFacts.insert("tenyear_hc",facts);
    if(!scope(r,p,nativeHasJijiao) || p->getRoom()!=&room) return;
    // Exact SWIG QVariant::toIntList (qvariant.i145): append toInt()
    // without an ok filter. Native effect separately uses ListV2I, which
    // skips failed conversions; normal fixed writers store integers only.
    // Never sort/dedup or substitute the Room-global JijiaoRecord.
    QList<int> ids;
    const QVariant own=p->getTag("JijiaoRecord");
    if(own.canConvert<QVariantList>())
        for(const QVariant &v:own.toList()) ids.append(v.toInt());
    const auto discard=room.getDiscardPile();
    QJsonArray record; QJsonObject membership;
    for(int id:ids) {
        record.append(id);
        const QString key=QString::number(id);
        if(membership.contains(key)) continue;
        bool inSnapshot=false, exact=false;
        for(const auto &v:r.worldView.discardPile) if(v.effectiveId==id) {
            inSnapshot=true;
            if(discard.contains(id) && room.getCardPlace(id)==Player::DiscardPile)
                exact=exactDiscard(v,Sanguosha->getCard(id));
            break;
        }
        if(!discard.contains(id) && !inSnapshot) membership.insert(key,false);
        else if(exact) membership.insert(key,true);
        // Inconsistent/stale public pile, mapping, wrapper or face: omit the
        // membership row (typed unknown). Do not reveal any non-discard location.
    }
    if(!scope(r,p,nativeHasJijiao)) return;
    facts.insert("JijiaoRecord",record); facts.insert("jijiao_discard",membership);
    r.worldView.strategyFacts.insert("tenyear_hc",facts);
}
int aiJijiaoPrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &name) {
    if(skill!="jijiao" || name!="JijiaoCard" || !scope(r,p)
        || p->getMark("@jijiaoMark")<=0) return -1;
    // Obtained discard IDs are EFFECT cards, never selected subcard costs.
    return 0;
}

// ---- liehou ----
void projectAILiehou(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    // Remove a copied older inquiry's answer before checking any new scope.
    QJsonObject family = request.worldView.strategyFacts.value("tenyear_hc").toObject();
    family.remove("LiehouTarget");
    const auto publish = [&] {
        if (family.isEmpty()) request.worldView.strategyFacts.remove("tenyear_hc");
        else request.worldView.strategyFacts.insert("tenyear_hc", family);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || !room.getPlayers().contains(viewer) || !request.decisionId
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::Yiji || request.choiceOptions.reason != "liehou") return;
    // The real V2 receive frame has already transferred the demanded cards.
    // Its inquiry offers the responder's hand, not a donor's hidden cards.
    if (request.choiceOptions.cardIds.isEmpty()) return;
    QSet<int> ids;
    for (int id : request.choiceOptions.cardIds) {
        if (id < 0 || ids.contains(id) || !viewer->handCards().contains(id)
            || room.getCardOwner(id) != viewer || room.getCardPlace(id) != Player::PlaceHand) return;
        ids.insert(id);
    }
    QSet<QString> targets;
    for (const QString &name : request.choiceOptions.playerNames) {
        if (targets.contains(name) || !room.findPlayerByObjectName(name, true)) return;
        targets.insert(name);
    }
    // Exactly QVariant::toPlayer in swig/qvariant.i:70, including null for
    // missing/wrong-type values. Never dereference an unvalidated tag pointer.
    ServerPlayer *donor = viewer->getTag("LiehouTarget").value<ServerPlayer *>();
    if (!donor) family.insert("LiehouTarget", false);
    else {
        if (!room.getPlayers().contains(donor)) return;
        family.insert("LiehouTarget", donor->objectName());
    }
    publish();
}

// ---- diou ----
#include "ai-strategy/inovation-ai.h"
#include "ai-runtime.h"
#include "wrapped-card.h"
#include "lua.hpp"
#include <QHash>
#include <QUuid>
#include <memory>
#include <cstring>
namespace {
struct DiouStack{lua_State*L;int n;explicit DiouStack(lua_State*s):L(s),n(lua_gettop(s)){}~DiouStack(){lua_settop(L,n);}};
struct Row {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class DiouLedger final:public QObject {public:explicit DiouLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,Row>>rows;};
DiouLedger*diouLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<DiouLedger*>(c))return l;return create?new DiouLedger(r):nullptr;}
bool diouCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool diouScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::RespondCard&&q.handlingMethod==Card::MethodNone
  &&q.prompt=="diou0"&&q.choiceOptions.question=="askForCard";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@diou"&&q.handlingMethod==Card::MethodUse;
}
const Card*equippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool shown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&equippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void field(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){DiouStack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&env(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L){
 DiouStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 field(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;
 field(L,-1,"@@diou");
 if(!code(r,L,-1,"lua/ai/tenyear_hc-ai.lua",8555,8561))return false;
 field(L,sgs,"list");if(!code(r,L,-1,"lua/utilities.lua",38,41))return false;
 field(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=code(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=code(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){field(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 field(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){field(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct DiouWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;};
int diouAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<DiouWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!diouCurrent(*w->q,w->owner)||w->lease()!=w->card
  ||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool target(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct ReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int readTargets(lua_State*L){
 auto*w=static_cast<ReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!diouCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!target(r,L,-1,p))return fail("saved native target identity or method changed");
  w->out->append(p);lua_pop(L,1);
 }
 if(!diouCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIDiouSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIDiouSlots(Room&r){if(auto*l=diouLedger(r))delete l;}
QJsonObject queryAIDiouSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("diouUse");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","diou.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!diouCurrent(q,p)||!diouScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");DiouStack stack(L);
 if(!self(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  DiouWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!shown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!shown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,diouAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!diouCurrent(q,p)||w.lease()!=w.card)return fail("saved-plan assignment changed request or custody");
  {auto*l=diouLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=Row{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!sourceReadBindings(r,L))return fail("saved consumer serializer/iterator bindings changed");
 if(!lookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!lookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!plain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!target(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  ReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,readTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 // Only after the complete ordered target loop does the source read card.
 if(!lookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||lua_isboolean(L,cardIndex)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,cardIndex)))return fail("saved card primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source card)").arg(lua_isnil(L,cardIndex)?"nil":"boolean")}};
 }
 auto*l=diouLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!shown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!shown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!diouCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

bool aiDiouConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!diouCurrent(q,const_cast<ServerPlayer*>(p))||!diouScope(q,const_cast<ServerPlayer*>(p),true))return false;
 if(s.size()!=6||s.value("source_key")!="diou0"||s.value("skill")!="_diou"
  ||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0||!s.value("name").isString()
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // The fixed source derives the name from current own handcards, then creates
 // a new default-face clone. No arbitrary printed face, ID or payment grant.
 for(const Card*c:p->getHandcards())if(c&&c->objectName()==s.value("name").toString()
  &&(c->isKindOf("BasicCard")||c->isNDTrick())&&shown(q,const_cast<ServerPlayer*>(p),c->getEffectiveId(),c))return true;
 return false;
}

namespace {
struct DiouReceipt {QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;QPointer<const Card>source;quint64 decision=0,revision=0;QString wire;QStringList targets;AICardSpec spec;SkillInstanceRef activation,root;};
class DiouReceipts final:public QObject{public:explicit DiouReceipts(Room&r):QObject(&r){}QList<DiouReceipt>rows;};
DiouReceipts*diouReceipts(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<DiouReceipts*>(c))return l;return create?new DiouReceipts(r):nullptr;}
AICardSpec diouSpec(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool diouSame(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
bool prepareDiou(const AIRequest&q,ServerPlayer*p,const QString&wire,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,AIReadonlyCardLease&retained,QString&error){
 auto fail=[&](const QString&s){error=s;return false;};
 if(!diouCurrent(q,p)||!diouScope(q,p,false)||!admit)return fail("Diou selected response request unavailable");
 const auto actual=queryAIDiouSlot(q,p,"read",{});if(actual.value("status")!="plan")return fail(actual.value("error").toString("Diou actual saved source is not a card plan"));
 if(actual.value("wire")!=wire||actual.value("targets").toArray()!=QJsonArray::fromStringList(targets))return fail("Diou selected wire or ordered targets differs from actual source slot");
 auto*l=diouLedger(*p->getRoom());if(!l)return fail("Diou saved source custody unavailable");retained=l->rows.value(p).value("diouUse").lease;const Card*c=retained?retained():nullptr;
 if(!c||!c->isVirtualCard()||c->getSkillName(false)!="_diou"||!c->getSubcards().isEmpty()||c->toString()!=wire||p->property("diouCard").toString()!=c->objectName())return fail("Diou selected source requires the actual current zero-cost declaration");
 const auto*vs=Sanguosha->getViewAsSkill("diou");if(!dynamic_cast<const ZeroCardViewAsSkill*>(vs)||dynamic_cast<const ViewAsSkillV2*>(vs))return fail("Diou current native provider is not the fixed V1 zero-card source");
 // Ordinary V1 raw submissions return before GUI viewAs. A solely attached
 // lifecycle can inquire/rebuild; keep that different provider outside here.
 bool ordinary=false,attached=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="diou"){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(i.skillName,i.instanceID,"legacy_activation_lifecycle").toBool())attached=true;else ordinary=true;
 }if(attached&&!ordinary)return fail("Diou attached source requires separate native continuation");
 // Parse schedules deleteLater before returning: defer its drain until the
 // native CardUseStruct has acquired its payload lease (including error paths).
 CardLifetimeScope parsedLifetime(globalCardLifetimeManager());
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Diou source serialization has no native parsed card");
 use=CardUseStruct(parsed,p);for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Diou selected target left roster");use.to<<t;}
 if(parsed->getActivationSkillName()!="_diou"||parsed->getActivationSkillInstanceId()!=0||!parsed->isVirtualCard()||parsed->getSkillName(false)!="_diou"||!parsed->getSubcards().isEmpty())return fail("Diou native parsed source attribution changed");
 if(!admit(use))return fail("Diou original raw native resolver rejected selected source");
 if(!diouCurrent(q,p)||retained()!=c||c->toString()!=wire)return fail("Diou selected source changed during reconstruction");
 const auto after=queryAIDiouSlot(q,p,"read",{});if(after.value("status")!="plan"||after.value("wire")!=wire||after.value("targets").toArray()!=QJsonArray::fromStringList(targets))return fail("Diou actual source slot changed during reconstruction");return true;
}
}
QJsonObject issueAIDiouReceipt(const AIRequest&q,ServerPlayer*p,const QJsonObject&payload,const std::function<bool(CardUseStruct&)>&admit){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","diou.selected_source"}};};
 if(payload.size()!=2||!payload.value("wire").isString()||!payload.value("targets").isArray())return fail("Diou selected source envelope malformed");QStringList targets;for(const auto&v:payload.value("targets").toArray()){if(!v.isString()||v.toString().isEmpty())return fail("Diou selected target malformed");targets<<v.toString();}
 const QString wire=payload.value("wire").toString();CardUseStruct use;AIReadonlyCardLease retained;QString error;
 if(!prepareDiou(q,p,wire,targets,admit,use,retained,error))return fail(error);
 if(!q.conversionLedger)return fail("Diou current conversion ledger unavailable");
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;DiouReceipt receipt;receipt.ledger=q.conversionLedger;receipt.owner=p;receipt.source=retained();receipt.decision=q.decisionId;receipt.revision=q.stateRevision;receipt.wire=wire;receipt.targets=targets;receipt.spec=diouSpec(use.card,id);receipt.activation=use.activationRef;receipt.root=use.sourceRef;
 q.conversionLedger->observedProducts.insert(id,std::move(retained));AICardConversionView marker;marker.conversionId=id;marker.policyClass="@diou_raw_source";marker.name=receipt.spec.name;marker.available=false;q.conversionLedger->tickets<<marker;p->getRoom()->roomRuntime()->ai().trackObservationLedger(q.conversionLedger);
 auto*l=diouReceipts(*p->getRoom(),true);for(auto i=l->rows.begin();i!=l->rows.end();)if(i->ledger.isNull())i=l->rows.erase(i);else ++i;l->rows<<receipt;
 const auto&s=receipt.spec;QJsonObject spec{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",QJsonArray()},{"conversion_id",s.conversionId}};
 return{{"card_spec",spec},{"targets",QJsonArray::fromStringList(targets)}};
}
bool hasAIDiouReceipt(const AIRequest&q,int id){if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@diou_raw_source")return true;return false;}
bool applyAIDiouReceipt(const AIRequest&q,ServerPlayer*p,const AICardSpec&spec,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&out){
 if(!diouCurrent(q,p)||!hasAIDiouReceipt(q,spec.conversionId))return false;auto*l=diouReceipts(*p->getRoom());if(!l)return false;
 for(const auto&entry:l->rows)if(entry.owner==p&&entry.ledger.toStrongRef()==q.conversionLedger&&entry.decision==q.decisionId&&entry.revision==q.stateRevision&&diouSame(entry.spec,spec)&&entry.targets==targets){
  const auto copy=entry;const auto source=q.conversionLedger->observedProducts.value(spec.conversionId);if(!source||!copy.source||source()!=copy.source.data())return false;
  CardUseStruct use;AIReadonlyCardLease retained;QString error;if(!prepareDiou(q,p,copy.wire,targets,admit,use,retained,error)||retained()!=source()||!diouSame(diouSpec(use.card,spec.conversionId),spec)||use.activationRef!=copy.activation||use.sourceRef!=copy.root)return false;use.m_validateTargets=true;out=use;return true;
 }return false;
}

// ---- gengdu ----
namespace {
struct GengduStack{lua_State*L;int n;explicit GengduStack(lua_State*s):L(s),n(lua_gettop(s)){}~GengduStack(){lua_settop(L,n);}};
struct GengduRow {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class GengduLedger final:public QObject {public:explicit GengduLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,GengduRow>>rows;};
GengduLedger*gengduLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<GengduLedger*>(c))return l;return create?new GengduLedger(r):nullptr;}
bool gengduCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool gengduScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="gengdu"
  &&q.choiceOptions.question=="askForAG";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@gengdu"&&q.handlingMethod==Card::MethodUse;
}
const Card*gengduEquippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool gengduShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&gengduEquippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void gengduField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool gengduSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool gengduAbsentNewIndex(lua_State*L,int t){GengduStack st(L);if(!lua_getmetatable(L,t))return true;gengduField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool gengduPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool gengduEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool gengduCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&gengduEnv(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool gengduLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));gengduField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  gengduField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool gengduSourceReadBindings(Room&r,lua_State*L){
 GengduStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 gengduField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 gengduField(L,sgs,"ai_skill_use");if(!gengduPlain(L,-1))return false;
 gengduField(L,-1,"@@gengdu");
 if(!gengduCode(r,L,-1,"lua/ai/tenyear_hc-ai.lua",9353,9362))return false;
 gengduField(L,sgs,"list");if(!gengduCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 gengduField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=gengduCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=gengduCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){gengduField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 gengduField(L,globals,"table");if(!gengduPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){gengduField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct GengduWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;QString cardState;};
int gengduAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<GengduWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(w->cardState=="card"){if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");}
 else if(w->cardState=="false")lua_pushboolean(L,0);else lua_pushnil(L);
 lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!gengduCurrent(*w->q,w->owner)||(w->cardState=="card"&&w->lease()!=w->card)
  ||!gengduSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 gengduField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!gengduAbsentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool gengduTarget(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct GengduReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int gengduReadTargets(lua_State*L){
 auto*w=static_cast<GengduReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!gengduCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!gengduTarget(r,L,-1,p))return fail("saved native target identity or method changed");
  w->out->append(p);lua_pop(L,1);
 }
 if(!gengduCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIGengduSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIGengduSlots(Room&r){if(auto*l=gengduLedger(r))delete l;}
QJsonObject queryAIGengduSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("gengduUse");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","gengdu.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!gengduCurrent(q,p)||!gengduScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");GengduStack stack(L);
 if(!gengduSelf(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  GengduWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=4||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject())return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.cardState=payload.value("card_state").toString();
   if(w.cardState!="card"&&w.cardState!="nil"&&w.cardState!="false")return fail("saved source card state malformed");
   if(w.cardState=="card"){
    w.lease=std::move(lease);w.card=w.lease?w.lease():nullptr;if(!w.card)return fail("saved source card custody expired");
    if(!w.card->isVirtualCard()&&!gengduShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
    for(int id:w.card->getSubcards())if(!gengduShown(q,p,id))return fail("saved source material not currently disclosed");
   }else if(lease||!payload.value("selector").toObject().isEmpty())return fail("absent source card cannot carry an authority selector");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,gengduAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!gengduCurrent(q,p)||(w.cardState=="card"&&w.lease()!=w.card))return fail("saved-plan assignment changed request or custody");
  {auto*l=gengduLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=GengduRow{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!gengduLookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(!gengduSourceReadBindings(r,L))return fail("saved consumer serializer/iterator bindings changed");
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!gengduLookup(L,saved,"card"))return fail("saved card requires unproved getter");
 const int initialCard=lua_absindex(L,-1);
 if(lua_isnil(L,initialCard)||(lua_isboolean(L,initialCard)&&!lua_toboolean(L,initialCard)))return{{"status","absent"}};
 if(!lua_istable(L,saved)||!gengduLookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!gengduPlain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!gengduTarget(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  GengduReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,gengduReadTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 // Only after the complete ordered target loop does the source read card.
 if(!gengduLookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||lua_isboolean(L,cardIndex)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,cardIndex)))return fail("saved card primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source card)").arg(lua_isnil(L,cardIndex)?"nil":"boolean")}};
 }
 auto*l=gengduLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!gengduShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!gengduShown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!gengduCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

bool aiGengduConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!gengduCurrent(q,const_cast<ServerPlayer*>(p))||!gengduScope(q,const_cast<ServerPlayer*>(p),true))return false;
 if(s.size()!=6||s.value("source_key")!="gengdu"||s.value("skill")!="gengdu"
  ||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0||!s.value("name").isString()
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()>1)return false;
 const auto materials=s.value("subcards").toArray();
 if(!materials.isEmpty()){
  const auto v=materials.first();const double n=v.toDouble(-1);
  if(!v.isDouble()||n<0||n>2147483647||n!=std::floor(n))return false;
  const int id=int(n);const Card*source=nullptr;
  for(const Card*c:p->getCards("he"))if(c&&c->getEffectiveId()==id)source=c;
  if(!source||r.getCardOwner(id)!=p||!source->isRed()
   ||!gengduShown(q,const_cast<ServerPlayer*>(p),id,source))return false;
  // addSubcard stores an ID. Its live wrapped aggregate printing must also be
  // owner-observable; getEquips supplies the public inner identity separately.
  if(r.getCardPlace(id)==Player::PlaceEquip&&!gengduEquippedMaterial(q,const_cast<ServerPlayer*>(p),id))return false;
 }
 for(const auto&v:q.choiceOptions.cards)if(q.choiceOptions.cardIds.contains(v.effectiveId)){
  const Card*c=r.getCard(v.effectiveId);
  if(c&&c->objectName()==s.value("name").toString()&&gengduShown(q,const_cast<ServerPlayer*>(p),v.effectiveId,c))return true;
 }
 return false;
}

// ---- zhuning ----
namespace {
struct ZhuningStack{lua_State*L;int n;explicit ZhuningStack(lua_State*s):L(s),n(lua_gettop(s)){}~ZhuningStack(){lua_settop(L,n);}};
struct ZhuningRow {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class ZhuningLedger final:public QObject {public:explicit ZhuningLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,ZhuningRow>>rows;};
ZhuningLedger*zhuningLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<ZhuningLedger*>(c))return l;return create?new ZhuningLedger(r):nullptr;}
bool zhuningCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool zhuningScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="zhuning"
  &&q.choiceOptions.question=="askForAG";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@zhuning"&&q.handlingMethod==Card::MethodUse;
}
const Card*zhuningEquippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool zhuningShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&zhuningEquippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void zhuningField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool zhuningSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool zhuningAbsentNewIndex(lua_State*L,int t){ZhuningStack st(L);if(!lua_getmetatable(L,t))return true;zhuningField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool zhuningPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool zhuningEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool zhuningCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&zhuningEnv(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool zhuningLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));zhuningField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  zhuningField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool zhuningSourceReadBindings(Room&r,lua_State*L){
 ZhuningStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 zhuningField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 zhuningField(L,sgs,"ai_skill_use");if(!zhuningPlain(L,-1))return false;
 zhuningField(L,-1,"@@zhuning");
 if(!zhuningCode(r,L,-1,"lua/ai/tenyear_hc-ai.lua",7973,7981))return false;
 zhuningField(L,sgs,"list");if(!zhuningCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 zhuningField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=zhuningCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=zhuningCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){zhuningField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 zhuningField(L,globals,"table");if(!zhuningPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){zhuningField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct ZhuningWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;QString cardState;};
int zhuningAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<ZhuningWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(w->cardState=="card"){if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");}
 else if(w->cardState=="false")lua_pushboolean(L,0);else lua_pushnil(L);
 lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!zhuningCurrent(*w->q,w->owner)||(w->cardState=="card"&&w->lease()!=w->card)
  ||!zhuningSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 zhuningField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!zhuningAbsentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool zhuningTarget(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct ZhuningReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int zhuningReadTargets(lua_State*L){
 auto*w=static_cast<ZhuningReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!zhuningCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!zhuningTarget(r,L,-1,p))return fail("saved native target identity or method changed");
  w->out->append(p);lua_pop(L,1);
 }
 if(!zhuningCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIZhuningSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIZhuningSlots(Room&r){if(auto*l=zhuningLedger(r))delete l;}
QJsonObject queryAIZhuningSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("zhuning_d");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","zhuning.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!zhuningCurrent(q,p)||!zhuningScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");ZhuningStack stack(L);
 if(!zhuningSelf(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  ZhuningWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=4||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject())return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.cardState=payload.value("card_state").toString();
   if(w.cardState!="card"&&w.cardState!="nil"&&w.cardState!="false")return fail("saved source card state malformed");
   if(w.cardState=="card"){
    w.lease=std::move(lease);w.card=w.lease?w.lease():nullptr;if(!w.card)return fail("saved source card custody expired");
    if(!w.card->isVirtualCard()&&!zhuningShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
    for(int id:w.card->getSubcards())if(!zhuningShown(q,p,id))return fail("saved source material not currently disclosed");
   }else if(lease||!payload.value("selector").toObject().isEmpty())return fail("absent source card cannot carry an authority selector");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,zhuningAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!zhuningCurrent(q,p)||(w.cardState=="card"&&w.lease()!=w.card))return fail("saved-plan assignment changed request or custody");
  {auto*l=zhuningLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=ZhuningRow{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!zhuningLookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||(lua_isboolean(L,saved)&&!lua_toboolean(L,saved)))return{{"status","absent"}};
 if(!zhuningSourceReadBindings(r,L))return fail("saved consumer serializer/iterator bindings changed");
 if(lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!zhuningLookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!zhuningPlain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!zhuningTarget(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  ZhuningReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,zhuningReadTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 // Only after the complete ordered target loop does the source read card.
 if(!zhuningLookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||lua_isboolean(L,cardIndex)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,cardIndex)))return fail("saved card primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source card)").arg(lua_isnil(L,cardIndex)?"nil":"boolean")}};
 }
 auto*l=zhuningLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!zhuningShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!zhuningShown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!zhuningCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

bool aiZhuningConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!zhuningCurrent(q,const_cast<ServerPlayer*>(p))||!zhuningScope(q,const_cast<ServerPlayer*>(p),true))return false;
 if(s.size()!=6||s.value("source_key")!="zhuning"||s.value("skill")!="_zhuning"
  ||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0||!s.value("name").isString()
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // The current AG question discloses the offered wrapped face. Read only
 // that exact current object/name; its future native factory uses EngineCard
 // and independently rejects any source/native printing discrepancy.
 for(const auto&v:q.choiceOptions.cards)if(q.choiceOptions.cardIds.contains(v.effectiveId)){
  const Card*c=r.getCard(v.effectiveId);
  if(c&&c->objectName()==s.value("name").toString()&&zhuningShown(q,const_cast<ServerPlayer*>(p),v.effectiveId,c))return true;
 }
 return false;
}

// ---- lianzhan ----
namespace {
// Exact native lexical producer; never exported to Lua and no generic C-entry
// privilege. The source issuer also requires its original Prepared bootstrap.
struct BootstrapInstall {
 Room*room;lua_State*L;quint64 generation;int depth;BootstrapInstall*previous;bool accepted=false;
};
thread_local BootstrapInstall*bootstrapInstall=nullptr;
int installCanonicalLianzhanBinding(lua_State*state){
 // Provenance is optional instrumentation. Its refusal must neither skip the
 // original installer nor add an authored bootstrap failure.
 if(!lua_checkstack(state,4096))
  return luaL_error(state,"Lianzhan canonical CardUseStruct binding changed");
 auto*f=bootstrapInstall;const bool sourceReceipt=f&&f->accepted;bool began=false;
 if(sourceReceipt){
  try{began=beginAILianzhanFinalizerTransition(*f->room,state);}catch(...){}
  if(!began)retireAIBootstrapSourceBoundary(*f->room);
 }
 const bool installed=installAILianzhanBinding(state);
 if(!installed){
  if(sourceReceipt)retireAIBootstrapSourceBoundary(*f->room);
  return luaL_error(state,"Lianzhan canonical CardUseStruct binding changed");
 }
 if(sourceReceipt&&began){
  bool completed=false;
  try{completed=completeAILianzhanFinalizerTransition(*f->room,state);}catch(...){}
  if(!completed)retireAIBootstrapSourceBoundary(*f->room);
 }
 return 0;
}
struct BootstrapInstallScope {
 BootstrapInstall frame;
 BootstrapInstallScope(Room&r,lua_State*L):frame{&r,L,r.roomRuntime()->lua().generation(),r.roomRuntime()->lua().protectedCallDepth(),bootstrapInstall}{bootstrapInstall=&frame;}
 ~BootstrapInstallScope(){bootstrapInstall=frame.previous;}
};
bool lianzhanCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==p->getRoom()&&aiRangeRequestCurrent(*p->getRoom(),q,p);}
bool lianzhanScope(const AIRequest&q,bool write){return q.choiceOptions.reason=="lianzhan"&&(write?(q.kind==AIRequest::SkillInvoke&&q.choiceOptions.question=="askForSkillInvoke"):(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"));}
QString name(Room&r,const ServerPlayer*p){for(auto*x:r.getAllPlayers(true))if(x==p)return x->objectName();return{};}
QJsonArray names(Room&r,const QList<ServerPlayer*>&ps,bool&ok){QJsonArray out;ok=true;for(auto*p:ps){auto n=name(r,p);if(n.isEmpty()){ok=false;return{};}out.append(n);}return out;}
QJsonObject face(const Card*c){return{{"name",c->objectName()},{"class",c->getClassName()},{"skill",c->getSkillName(false)},{"suit",int(c->getSuit())},{"number",c->getNumber()},{"virtual",c->isVirtualCard()},{"id",c->getEffectiveId()}};}
// Borrow exactly the actual native source, including virtual damage cards.
// This lease never owns/deletes the Card and never mints a readonly recipe.
struct BorrowedCard {
 QPointer<const Card> object;std::shared_ptr<const CardLifetimeToken> token;CardLifetimeLease lease;
 explicit BorrowedCard(const Card*c):object(c),token(globalCardLifetimeManager().liveToken(c)),lease(globalCardLifetimeManager(),token){}
 const Card*live()const{return !object.isNull()&&token&&token->live&&lease.isValid()&&globalCardLifetimeManager().liveToken(object.data())==token?object.data():nullptr;}
};
using Borrow=std::shared_ptr<BorrowedCard>;
struct LianzhanRow{QPointer<ServerPlayer>owner;Borrow card;QJsonObject observedFace;};
struct Capture {QPointer<ServerPlayer>owner;QWeakPointer<AIConversionLedger>ledger;quint64 decision=0,revision=0;std::shared_ptr<const CardUseStruct>use;Borrow card;};
class LianzhanLedger final:public QObject{public:explicit LianzhanLedger(Room&r):QObject(&r){}quint64 generation=0;QHash<const ServerPlayer*,LianzhanRow>rows;QHash<quint64,Capture>captures;};
LianzhanLedger*lianzhanLedger(Room&r,bool create=false){for(auto*x:r.children())if(auto*l=dynamic_cast<LianzhanLedger*>(x))return l;return create?new LianzhanLedger(r):nullptr;}
bool lianzhanField(lua_State*L,int index,const char*key){QSet<const void*>seen;lua_pushvalue(L,index);for(int depth=0;depth<16;++depth){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AILuaInspect::field(L,self,"lianzhanUse");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);if(exists||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
bool source(Room&r,lua_State*L,bool write){const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};AILuaInspect::globals(L);const int env=lua_absindex(L,-1);AILuaInspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;AILuaInspect::field(L,-1,write?"ai_skill_invoke":"ai_skill_playerchosen");if(!lua_istable(L,-1))return false;AILuaInspect::field(L,-1,"lianzhan");const int f=lua_absindex(L,-1);if(!matchAIPinnedFunction(r,L,f,"lua/ai/tenyear_hc-ai.lua",write?8972:8978,write?8976:8985))return false;for(int i=1;;++i){const char*k=lua_getupvalue(L,f,i);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;}return true;}
struct Call{const AIRequest*q;ServerPlayer*p;bool write;Borrow lease;std::shared_ptr<const CardUseStruct>sourceUse;QJsonObject observedFace,result;QString error;std::function<QJsonObject(const Card*)>profile;};
int run(lua_State*L){auto*w=static_cast<Call*>(lua_touserdata(L,1));auto fail=[&](const char*s){w->error=QString::fromLatin1(s);return luaL_error(L,"%s",s);};if(!lua_checkstack(L,4096))return fail("Lianzhan source slot stack unavailable");Room&r=*w->p->getRoom();int value=0;
 if(w->write){if(!pushAILianzhanUse(L,*w->sourceUse))return fail("Lianzhan source CardUse binding unavailable");value=lua_absindex(L,-1);}
 if(!lianzhanCurrent(*w->q,w->p)||!pushAIActualAISelf(r,w->p,L))return fail("Lianzhan actual Self unavailable");const int self=lua_absindex(L,-1);
 if(!source(r,L,w->write)||(!w->write&&(!matchAINativeClassMethod(r,L,"SPlayerList","length")||!matchAINativeClassMethod(r,L,"SPlayerList","at"))))return fail("Lianzhan actual source provider changed");
 if(w->write){
  if(!assignment(L,self)||!lianzhanCurrent(*w->q,w->p)||w->lease->live()!=w->sourceUse->card)return fail("Lianzhan assignment dispatch or source custody changed");
  lua_pushvalue(L,value);lua_setfield(L,self,"lianzhanUse");w->result={{"written",true}};
 }else{
  if(!lianzhanField(L,self,"lianzhanUse"))return fail("Lianzhan saved field requires unproved getter");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)||lua_isboolean(L,value)){if(AILuaInspect::primitiveMetatable(L,lua_type(L,value)))return fail("Lianzhan primitive metatable changed");w->result={{"source_error","source lianzhanUse is not a CardUse value"}};return 0;}
  const Card*savedCard=nullptr;ServerPlayer*savedFrom=nullptr;QList<ServerPlayer*>savedTo;if(!readAILianzhanUse(L,value,savedCard,savedFrom,savedTo))return fail("Lianzhan current slot is not its genuine source CardUse value");
  // Native copy retains exactly the original Card. Whole from/card/to is read
  // from the current actual userdata; no stale isolated plan is rehydrated.
  auto*l=lianzhanLedger(r);if(!l||l->generation!=r.roomRuntime()->lua().generation())return fail("Lianzhan source Card custody unavailable");const auto row=l->rows.value(w->p);w->lease=row.card;
  const Card*c=w->lease?w->lease->live():nullptr;if(row.owner!=w->p||!c||(savedCard&&savedCard!=c)||(savedCard&&face(c)!=row.observedFace))return fail("Lianzhan source Card identity or disclosed face changed");
  const QString from=savedFrom?name(r,savedFrom):QString();if(savedFrom&&from.isEmpty())return fail("Lianzhan source actor left current roster");bool ok=false;const auto to=names(r,savedTo,ok);if(!ok)return fail("Lianzhan source target left current roster");
  // Only the already observed public face is exported. No private card flags,
  // material IDs, target legality, receipt, or evaluation clone is manufactured.
  QJsonObject card;if(savedCard){card=w->profile(c);if(card.isEmpty())return fail("Lianzhan public source face unavailable");card.remove("known_flags");card.remove("flags_complete");card.remove("flags_complete_scope");card.remove("subcards");card.insert("virtual_card",c->isVirtualCard());card.insert("legacy_identity_token",aiLegacyCardIdentity(r,w->p,c));card.insert("evaluation_only",true);}
  w->result={{"status","use"},{"from",savedFrom?QJsonValue(from):QJsonValue(false)},{"card",savedCard?QJsonValue(card):QJsonValue(false)},{"to",to}};
 }
 if(!lianzhanCurrent(*w->q,w->p)||!w->lease||!w->lease->live()||!pushAIActualAISelf(r,w->p,L)||!lua_rawequal(L,self,-1))return fail("Lianzhan operation changed source request or Self");return 0;
}
}
void projectAILianzhan(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
 // Projection precedes lexical request aliases and canonical execution. Bind
 // the complete native value to the immutable decision + weak ledger, not a
 // temporary stack address; query still requires R44's exact current request.
 auto*l=lianzhanLedger(r,true);for(auto i=l->captures.begin();i!=l->captures.end();)if(i->ledger.isNull())i=l->captures.erase(i);else ++i;
 l->captures.remove(q.decisionId);
 if(!p||p->getRoom()!=&r||!q.decisionId||q.conversionLedger.isNull()||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||!lianzhanScope(q,true)||data.metaType().id()!=qMetaTypeId<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();bool ok=false;names(r,use.to,ok);
 if(use.from!=p||!use.card||!use.card->isDamageCard()||use.to.size()!=1||!ok)return;
 auto copy=std::make_shared<CardUseStruct>(use);auto borrowed=std::make_shared<BorrowedCard>(use.card);if(borrowed->live()!=use.card)return;
 Capture cap;cap.owner=p;cap.ledger=q.conversionLedger;cap.decision=q.decisionId;cap.revision=q.stateRevision;cap.use=std::move(copy);cap.card=std::move(borrowed);l->captures.insert(q.decisionId,std::move(cap));
}
bool registerAILianzhanSources(Room&r,lua_State*L,QString*why){
 if(!registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),why))return false;
 BootstrapInstallScope lexicalInstaller(r,L);
 const int top=lua_gettop(L);lua_pushcfunction(L,installCanonicalLianzhanBinding);
 const bool ok=LuaRuntime::protectedCall(L,0,0,0)==LUA_OK;if(!ok&&why)*why=QStringLiteral("Lianzhan canonical native binding bootstrap failed");lua_settop(L,top);return ok;
}
bool matchAILianzhanBootstrapEntry(Room&r,lua_State*L,int arguments)noexcept{
 auto*f=bootstrapInstall;auto*rt=r.roomRuntime();
 if(!f||f->room!=&r||f->L!=L||!rt||rt->isClosing()||rt->lua().rawState()!=L
  ||rt->lua().generation()!=f->generation||rt->lua().protectedCallDepth()!=f->depth
  ||arguments!=0||lua_gettop(L)<1||LuaRuntime::current()!=&rt->lua())return false;
 // Borrow exact pending value; no stack growth, upvalue push or Lua call.
 const auto*v=AILuaInspect::value(L,-1);
 const bool ok=v&&ttislcf(v)&&fvalue(v)==installCanonicalLianzhanBinding;
 if(ok)f->accepted=true;return ok;
}
void clearAILianzhanSlots(Room&r){if(auto*l=lianzhanLedger(r))delete l;}
QJsonObject queryAILianzhanSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,const std::function<QJsonObject(const Card*)>&profile){auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","lianzhan.saved_use"}};};if((op!="write"&&op!="read")||!lianzhanCurrent(q,p)||!lianzhanScope(q,op=="write"))return fail("Lianzhan exact current source question unavailable");Call w;w.q=&q;w.p=p;w.write=op=="write";w.profile=profile;Room&r=*p->getRoom();
 if(w.write){auto*l=lianzhanLedger(r);if(!l)return fail("Lianzhan current native source capture unavailable");const auto capture=l->captures.value(q.decisionId);if(capture.owner!=p||capture.decision!=q.decisionId||capture.revision!=q.stateRevision||capture.ledger.toStrongRef()!=q.conversionLedger||!capture.use||!capture.card||capture.card->live()!=capture.use->card)return fail("Lianzhan current native source capture changed");w.sourceUse=capture.use;w.lease=capture.card;if(payload.size()!=3||!payload.value("from").isString()||!payload.value("card_identity").isString()||!payload.value("to").isArray())return fail("Lianzhan source use envelope malformed");const auto&use=*w.sourceUse;bool ok=false;const auto to=names(r,use.to,ok);if(!ok||use.from!=p||!w.lease||w.lease->live()!=use.card||payload.value("from")!=p->objectName()||payload.value("to").toArray()!=to||payload.value("card_identity").toString()!=aiLegacyCardIdentity(r,p,use.card))return fail("Lianzhan DTO differs from exact native question source");w.observedFace=face(use.card);}
 else if(!payload.isEmpty()||!profile)return fail("Lianzhan read envelope malformed");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return fail("Lianzhan game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail(w.error.isEmpty()?QStringLiteral("Lianzhan protected operation failed"):w.error);
 if(w.write){auto*l=lianzhanLedger(r,true);if(l->generation&&l->generation!=vm.generation())return fail("Lianzhan game generation changed");l->generation=vm.generation();l->rows[p]={p,std::move(w.lease),w.observedFace};}return w.result;
}

bool inAILianzhanBootstrapInstaller(Room&r,lua_State*L)noexcept{
 auto*f=bootstrapInstall;auto*rt=r.roomRuntime();
 if(!f||!f->accepted||f->room!=&r||f->L!=L||!rt||rt->isClosing()||rt->lua().rawState()!=L
  ||rt->lua().generation()!=f->generation||rt->lua().protectedCallDepth()!=f->depth+1
  ||LuaRuntime::current()!=&rt->lua()||!L->ci||isLua(L->ci))return false;
 const auto*v=s2v(L->ci->func.p);
 return ttislcf(v)&&fvalue(v)==installCanonicalLianzhanBinding;
}

// ---- mingfa ----
namespace {
struct MingfaRow{QPointer<ServerPlayer>owner;AIReadonlyCardLease card;};
class MingfaLedger final:public QObject{public:explicit MingfaLedger(Room&r):QObject(&r){}quint64 generation=0;QHash<const ServerPlayer*,MingfaRow>rows;};
MingfaLedger*mingfaLedger(Room&r,bool create=false){for(auto*x:r.children())if(auto*l=dynamic_cast<MingfaLedger*>(x))return l;return create?new MingfaLedger(r):nullptr;}
bool mingfaCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==p->getRoom()&&aiRangeRequestCurrent(*p->getRoom(),q,p);}
bool mingfaScope(const AIRequest&q,bool write){return q.choiceOptions.reason=="tenyearmingfa"&&(write?(q.kind==AIRequest::SkillInvoke&&q.choiceOptions.question=="askForSkillInvoke"):(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"));}
bool mingfaField(lua_State*L,int index,const char*key){QSet<const void*>seen;lua_pushvalue(L,index);for(int depth=0;depth<16;++depth){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool mingfaAssignment(lua_State*L,int self){AILuaInspect::field(L,self,"tenyearmingfa_c");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);if(exists||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
bool reader(Room&r,lua_State*L){const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};AILuaInspect::globals(L);const int env=lua_absindex(L,-1);AILuaInspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;AILuaInspect::field(L,-1,"ai_skill_playerchosen");if(!lua_istable(L,-1))return false;AILuaInspect::field(L,-1,"tenyearmingfa");const int f=lua_absindex(L,-1);if(!matchAIPinnedFunction(r,L,f,"lua/ai/tenyear_hc-ai.lua",1843,1871))return false;for(int i=1;;++i){const char*k=lua_getupvalue(L,f,i);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;}return true;}
struct MingfaCall{const AIRequest*q;ServerPlayer*p;QString op,method;ServerPlayer*target=nullptr;AIReadonlyCardLease lease;const Card*card=nullptr;QJsonObject result;QString error;};
int mingfaRun(lua_State*L){auto*w=static_cast<MingfaCall*>(lua_touserdata(L,1));auto fail=[&](const char*s){w->error=QString::fromLatin1(s);return luaL_error(L,"%s",s);};if(!lua_checkstack(L,4096))return fail("Mingfa source slot stack unavailable");Room&r=*w->p->getRoom();int value=0;
 if(w->op=="write"){if(!pushAISlotCard(L,w->card))return fail("Mingfa source Card binding unavailable");value=lua_absindex(L,-1);}
 if(!mingfaCurrent(*w->q,w->p)||!pushAIActualAISelf(r,w->p,L))return fail("Mingfa actual Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="write"){
  if(!mingfaAssignment(L,self)||!mingfaCurrent(*w->q,w->p)||w->lease()!=w->card)return fail("Mingfa assignment dispatch or custody changed");lua_pushvalue(L,value);lua_setfield(L,self,"tenyearmingfa_c");
 }else{
  if(!mingfaField(L,self,"tenyearmingfa_c"))return fail("Mingfa Card field requires unproved getter");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)||lua_isboolean(L,value)){if(AILuaInspect::primitiveMetatable(L,lua_type(L,value)))return fail("Mingfa primitive metatable changed");w->result={{"source_error","source tenyearmingfa_c is not a Card"}};return 0;}
  auto*l=mingfaLedger(r);if(!l||l->generation!=r.roomRuntime()->lua().generation())return fail("Mingfa retained Card unavailable");const auto row=l->rows.value(w->p);w->lease=row.card;w->card=w->lease?w->lease():nullptr;
  if(row.owner!=w->p||!w->card||!w->card->isVirtualCard()||!w->card->getSubcards().isEmpty()||!matchAISlotCard(L,value,w->card)||!reader(r,L))return fail("Mingfa current source Card identity/provider changed");
  // The fixed reader reaches only these two operations. No eager availability,
  // other-target scan, clone substitution, descriptor receipt or cached result.
  bool valueBool=false;
  if(w->method=="target_fixed"){
   if(!matchAINativeObjectMethods(r,L,value,"Card",{"targetFixed"}))return fail("Mingfa targetFixed dispatch changed");valueBool=w->card->targetFixed();
  }else{
   if(!mingfaField(L,self,"player")||!matchAISlotPlayer(L,-1,w->p)||!matchAINativeObjectMethods(r,L,-1,"ServerPlayer",{"canUse"}))return fail("Mingfa source owner canUse dispatch changed");
   valueBool=w->p->canUse(w->card,w->target,true);
  }
  w->result={{"known",true},{"value",valueBool}};
 }
 if(!mingfaCurrent(*w->q,w->p)||w->lease()!=w->card||!pushAIActualAISelf(r,w->p,L)||!lua_rawequal(L,self,-1))return fail("Mingfa query changed request or retained identity");
 if(w->op=="write")w->result={{"written",true}};return 0;
}
}
void projectAIMingfa(AIRequest&q,const QVariant&data){q.choiceOptions.context.remove("round53_mingfa_name");if(!mingfaScope(q,true)||data.metaType().id()!=QMetaType::QString)return;const auto pieces=data.toString().split(':');if(pieces.size()==2&&pieces.first()=="tenyearmingfa")q.choiceOptions.context.insert("round53_mingfa_name",pieces.last());}
bool aiMingfaConstruction(const AIRequest&q,Room&,const ServerPlayer*p,const QJsonObject&s){return mingfaCurrent(q,const_cast<ServerPlayer*>(p))&&mingfaScope(q,true)&&s.size()==6&&s.value("source_key")=="tenyearmingfa"&&s.value("skill")=="tenyearmingfa"&&s.value("suit")==int(Card::NoSuit)&&s.value("number")==0&&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty()&&q.choiceOptions.context.value("round53_mingfa_name").isString()&&!q.choiceOptions.context.value("round53_mingfa_name").toString().isEmpty()&&s.value("name")==q.choiceOptions.context.value("round53_mingfa_name");}
bool registerAIMingfaSources(Room&r,lua_State*L,QString*why){return registerAIPinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),why);}
void clearAIMingfaSlots(Room&r){if(auto*l=mingfaLedger(r))delete l;}
QJsonObject queryAIMingfaSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","tenyearmingfa.saved_card"}};};if((op!="write"&&op!="query")||!mingfaCurrent(q,p)||!mingfaScope(q,op=="write"))return fail("Mingfa exact current source question unavailable");MingfaCall w;w.q=&q;w.p=p;w.op=op;
 if(op=="write"){if(payload.size()!=1||!payload.value("selector").isObject()||!lease)return fail("Mingfa source selector malformed");w.lease=std::move(lease);w.card=w.lease();if(!w.card||!w.card->isVirtualCard()||!w.card->getSubcards().isEmpty())return fail("Mingfa fixed zero-material source Card unavailable");}
 else{w.method=payload.value("method").toString();if(w.method=="target_fixed"){if(payload.size()!=1)return fail("Mingfa targetFixed has extra arguments");}else if(w.method=="can_use"){if(payload.size()!=2||!payload.value("target").isString())return fail("Mingfa target malformed");const auto name=payload.value("target").toString();if(!q.choiceOptions.playerNames.contains(name))return fail("Mingfa target was not offered");for(auto*t:p->getRoom()->getAllPlayers(true))if(t->objectName()==name)w.target=t;if(!w.target)return fail("Mingfa target left roster");}else return fail("Mingfa source predicate unavailable");}
 Room&r=*p->getRoom();auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return fail("Mingfa game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};lua_pushcfunction(L,mingfaRun);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail(w.error.isEmpty()?QStringLiteral("Mingfa protected operation failed"):w.error);if(op=="write"){auto*l=mingfaLedger(r,true);if(l->generation&&l->generation!=vm.generation())return fail("Mingfa game generation changed");l->generation=vm.generation();l->rows[p]={p,std::move(w.lease)};}return w.result;
}

// ---- peiqi ----
// Must follow Qt headers: the private Lua implementation defines cast macros.
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool peiqiCurrent(Room&r,const AIRequest&q,ServerPlayer*p,const AIEventView*e){
 return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&aiRangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r
  &&(e?(q.decisionId==0&&e->revision==q.stateRevision):q.decisionId>0);
}
bool peiqiScope(const AIRequest&q,ServerPlayer*p,const AIEventView*e,const QString&op,const QString&key){
 // canDamageHp(to) invokes peiqi even when the observer does not own that skill.
 // This finite scratch table carries no Card face or payment permission.
 if(op=="reset")return key.isEmpty();
 if(key!="from"&&key!="to"&&key!="cid")return false;
 if(op=="write")return true;
 if(op!="read")return false;
 if(e){const auto a=e->details.value("prompt_list").toArray();return e->triggerEvent==int(ChoiceMade)&&a.size()>=2&&a.at(0)==QJsonValue("cardChosen")
  &&(a.at(1)==QJsonValue("peiqi")||a.at(1)==QJsonValue("s4_longhun")||a.at(1)==QJsonValue("s4_yuezhe"))&&(key=="cid"||key=="to");}
 if(key=="from"&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getRoom()->getCurrent()==p&&p->getPhase()==Player::Play&&p->hasSkill("dressing"))return true; // dressing use_func reads the record just written by invoke
 if(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"&&q.choiceOptions.reason=="dressing")return key=="to";
 if(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen")return(q.choiceOptions.reason=="peiqi_from"&&key=="from")||(q.choiceOptions.reason=="peiqi_to"&&key=="to");
 return q.kind==AIRequest::CardChosen&&q.choiceOptions.question=="askForCardChosen"&&(q.choiceOptions.reason=="peiqi"||q.choiceOptions.reason=="dressing")&&key=="cid";
}
bool peiqiField(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AILuaInspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool peiqiAssignment(lua_State*L,int self,const char*key){AILuaInspect::field(L,self,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AILuaInspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
ServerPlayer*roster(Room&r,const AIRequest&q,const QString&name){
 bool projected=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)projected=projected||v.objectName==name;
 if(!projected)return nullptr;for(auto*p:r.getAllPlayers(true))if(p->objectName()==name)return p;return nullptr;
}
ServerPlayer*actualPlayer(Room&r,lua_State*L,int at){for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,at,p))return p;return nullptr;}

// Actual-Self storage alone is not disclosure of a legacy-mutated physical ID.
// The fixed writer takes cid from a public e/j card. Require that same lawful
// domain at each numeric export; no hidden-zone face or historical tag lookup.
bool publicCid(Room&r,const AIRequest&q,double n){
 if(!std::isfinite(n)||n<0||n>2147483647||n!=std::floor(n))return false;
 const int id=int(n);auto views=q.worldView.players;views.prepend(q.worldView.self);
 for(const auto&v:views){
  auto*holder=roster(r,q,v.objectName);if(!holder)continue;
  for(int zone=0;zone<2;++zone){
   const auto&rows=zone?v.judgingArea:v.equips;
   for(const auto&c:rows){
    if(c.effectiveId!=id||!c.nativeIdentity||!globalCardLifetimeManager().isLive(c.nativeIdentity))continue;
    const auto place=zone?Player::PlaceDelayedTrick:Player::PlaceEquip;
    if(r.getCardOwner(id)!=holder||r.getCardPlace(id)!=place)continue;
    const auto actual=zone?holder->getJudgingArea():holder->getEquips();
    for(const auto*card:actual)if(card&&card->getEffectiveId()==id&&c.nativeIdentity->address==card)return true;
   }
  }
 }
 return false;
}
struct PeiqiCall{Room*r;const AIRequest*q;ServerPlayer*p;const AIEventView*e;QString op,key;ServerPlayer*target=nullptr;int id=0;QJsonObject result;const char*error=nullptr;};
int peiqiFail(lua_State*L,PeiqiCall*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool now(PeiqiCall*w){return peiqiCurrent(*w->r,*w->q,w->p,w->e);}
int peiqiRun(lua_State*L){
 auto*w=static_cast<PeiqiCall*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return peiqiFail(L,w,"Peiqi source stack unavailable");
 int fresh=0;if(w->op=="reset"){lua_newtable(L);fresh=lua_absindex(L,-1);}
 else if(w->op=="write"){if(w->key=="cid")lua_pushinteger(L,w->id);else if(!pushAISlotPlayer(L,w->target))return peiqiFail(L,w,"Peiqi Player wrapper unavailable");fresh=lua_absindex(L,-1);}
 if(!now(w)||!pushAIActualAISelf(*w->r,w->p,L))return peiqiFail(L,w,"Peiqi current gameplay Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="reset"){
  if(!peiqiAssignment(L,self,"peiqiData")||!now(w))return peiqiFail(L,w,"Peiqi reset dispatch changed");
  lua_pushvalue(L,fresh);lua_setfield(L,self,"peiqiData");
 }else{
  if(!peiqiField(L,self,"peiqiData"))return peiqiFail(L,w,"Peiqi Self getter changed");const int record=lua_absindex(L,-1);
  if(!lua_istable(L,record)){
   const int t=lua_type(L,record);
   if(t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER){w->result={{"known",true},{"ordinary_error",QString("attempt to index a %1 value (field 'peiqiData')").arg(QString::fromLatin1(lua_typename(L,t)))}};return 0;}
   return peiqiFail(L,w,"Peiqi record is not a source plain table");
  }
  if(lua_getmetatable(L,record))return peiqiFail(L,w,"Peiqi record metatable is outside the fixed source domain");const auto key=w->key.toUtf8();
  if(w->op=="write"){
   if(!now(w))return peiqiFail(L,w,"Peiqi record writer became stale");
   if(w->key=="cid"&&!publicCid(*w->r,*w->q,w->id))return peiqiFail(L,w,"Peiqi cid is not currently projected public e/j");lua_pushvalue(L,fresh);lua_setfield(L,record,key.constData());
   AILuaInspect::field(L,record,key.constData());if(!lua_rawequal(L,fresh,-1))return peiqiFail(L,w,"Peiqi record write changed");lua_pop(L,1);
  }else{
   AILuaInspect::field(L,record,key.constData());const int v=lua_absindex(L,-1),t=lua_type(L,v);
   if(t==LUA_TNIL)w->result={{"known",true},{"type","nil"}};
   else if(t==LUA_TBOOLEAN)w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,v))}};
   else if(t==LUA_TNUMBER){const double n=lua_tonumber(L,v);if(!std::isfinite(n))return peiqiFail(L,w,"Peiqi nonfinite field");if(w->key=="cid"&&!publicCid(*w->r,*w->q,n))return peiqiFail(L,w,"Peiqi cid is not currently projected public e/j");w->result={{"known",true},{"type","number"},{"value",n}};}
   else if(t==LUA_TSTRING){size_t n;const char*c=lua_tolstring(L,v,&n);if(n>4096)return peiqiFail(L,w,"Peiqi string field exceeds finite domain");w->result={{"known",true},{"type","string"},{"value",QString::fromUtf8(c,int(n))}};}
   else{auto*p=actualPlayer(*w->r,L,v);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAINativeObjectMethods(*w->r,L,v,"ServerPlayer",{"objectName"}))return peiqiFail(L,w,"Peiqi field is not a projected native Player");w->result={{"known",true},{"type","player"},{"value",p->objectName()}};}
  }
  // Do not substitute another table or reconstruct an aggregate after each field write.
  if(!peiqiField(L,self,"peiqiData")||!lua_rawequal(L,record,-1))return peiqiFail(L,w,"Peiqi actual record changed during operation");
 }
 if(!now(w)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return peiqiFail(L,w,"Peiqi receiver changed after operation");
 if(w->op=="reset"){AILuaInspect::field(L,self,"peiqiData");if(!lua_rawequal(L,fresh,-1))return peiqiFail(L,w,"Peiqi reset table changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject queryAIPeiqiRecord(Room&r,const AIRequest&q,ServerPlayer*p,const AIEventView*e,const QString&op,const QString&key,const QJsonObject&input){
 if(!peiqiCurrent(r,q,p,e)||!peiqiScope(q,p,e,op,key))return unknown("Peiqi operation requires its actual current owner policy/source reader");
 PeiqiCall w{&r,&q,p,e,op,key};
 if(op=="write"){
  if(input.size()!=1)return unknown("Peiqi field assignment shape invalid");
  if(key=="cid"){const auto v=input.value("value");const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n>2147483647||n!=std::floor(n))return unknown("Peiqi cid is not a source integer");w.id=int(n);if(!publicCid(r,q,n))return unknown("Peiqi cid is not currently projected public e/j");}
  else{if(!input.value("value").isString())return unknown("Peiqi target name malformed");w.target=roster(r,q,input.value("value").toString());if(!w.target)return unknown("Peiqi target is not projected roster");}
 }else if(!input.isEmpty())return unknown("Peiqi read/reset has extra arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("Peiqi game VM unavailable");
 const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 lua_pushcfunction(L,peiqiRun);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected Peiqi source operation failed");return w.result;
}

// ---- tychende ----
namespace {
struct TychendeStack{lua_State*L;int n;explicit TychendeStack(lua_State*s):L(s),n(lua_gettop(s)){}~TychendeStack(){lua_settop(L,n);}};
struct TychendeRow {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class TychendeLedger final:public QObject {public:explicit TychendeLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,TychendeRow>>rows;};
TychendeLedger*tychendeLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<TychendeLedger*>(c))return l;return create?new TychendeLedger(r):nullptr;}
bool tychendeCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool tychendeScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="tychende";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@tychende"&&q.handlingMethod==Card::MethodUse;
}
const Card*tychendeEquippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool tychendeShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&tychendeEquippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void tychendeField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool tychendeSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool tychendeAbsentNewIndex(lua_State*L,int t){TychendeStack st(L);if(!lua_getmetatable(L,t))return true;tychendeField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool tychendePlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool tychendeEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool tychendeCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&tychendeEnv(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool tychendeLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));tychendeField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  tychendeField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool tychendeSourceReadBindings(Room&r,lua_State*L){
 TychendeStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 tychendeField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 tychendeField(L,sgs,"ai_skill_use");if(!tychendePlain(L,-1))return false;
 tychendeField(L,-1,"@@tychende");
 if(!tychendeCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",345,354))return false;
 tychendeField(L,sgs,"list");if(!tychendeCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 tychendeField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=tychendeCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=tychendeCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){tychendeField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 tychendeField(L,globals,"table");if(!tychendePlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){tychendeField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct TychendeWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;};
int tychendeAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<TychendeWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!tychendeCurrent(*w->q,w->owner)||w->lease()!=w->card
  ||!tychendeSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 tychendeField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!tychendeAbsentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool tychendeTarget(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct TychendeReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int tychendeReadTargets(lua_State*L){
 auto*w=static_cast<TychendeReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!tychendeCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!tychendeTarget(r,L,-1,p))return fail("saved native target identity or method changed");
  bool projected=w->q->worldView.self.objectName==p->objectName();for(const auto&v:w->q->worldView.players)if(v.objectName==p->objectName())projected=true;
  if(!projected)return fail("saved target absent from projected roster");w->out->append(p);lua_pop(L,1);
 }
 if(!tychendeCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAITychendeSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/kearlqxf-ai.lua",QByteArray("8195d9f79bfd68fb42d7a06b631dae90839c51352de2f13a19768ddfd6db8ae5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAITychendeSlots(Room&r){if(auto*l=tychendeLedger(r))delete l;}
QJsonObject queryAITychendeSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("tychendeUse");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","tychende.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!tychendeCurrent(q,p)||!tychendeScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");TychendeStack stack(L);
 if(!tychendeSelf(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  {TychendeStack check(L);AILuaInspect::globals(L);tychendeField(L,-1,"sgs");tychendeField(L,-1,"ai_skill_askforag");
   if(!tychendePlain(L,-1))return fail("Tychende choice registry is not plain");tychendeField(L,-1,"tychende");
   if(!tychendeCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",326,343))return fail("Tychende full source producer binding changed");}

  TychendeWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!tychendeShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!tychendeShown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,tychendeAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!tychendeCurrent(q,p)||w.lease()!=w.card)return fail("saved-plan assignment changed request or custody");
  {auto*l=tychendeLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=TychendeRow{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 {TychendeStack check(L);AILuaInspect::globals(L);tychendeField(L,-1,"sgs");tychendeField(L,-1,"ai_skill_use");if(!tychendePlain(L,-1))return fail("response registry changed");tychendeField(L,-1,"@@tychende");if(!tychendeCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",345,354))return fail("source reader changed");}
 if(!tychendeLookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!tychendeLookup(L,saved,"card"))return fail("saved card getter unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||(lua_isboolean(L,cardIndex)&&!lua_toboolean(L,cardIndex)))return{{"status","pass"}};
 if(!tychendeSourceReadBindings(r,L))return fail("source list/serializer bindings changed");
 if(!lua_istable(L,saved)||!tychendeLookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!tychendePlain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!tychendeTarget(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  TychendeReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,tychendeReadTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 auto*l=tychendeLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!tychendeShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!tychendeShown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");bool projected=q.worldView.self.objectName==t->objectName();for(const auto&v:q.worldView.players)if(v.objectName==t->objectName())projected=true;if(!projected)return fail("saved target absent from projected roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!tychendeCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 QJsonArray costs;for(int id:c->getSubcards())costs.append(id);
 QJsonObject spec{{"name",c->objectName()},{"skill",c->getSkillName(false)},{"suit",int(c->getSuit())},{"number",c->getNumber()},{"subcards",costs}};
 return{{"status","plan"},{"wire",wire},{"class_name",c->getClassName()},{"physical_id",c->isVirtualCard()?-1:c->getEffectiveId()},{"spec",spec},{"targets",names},{"evaluation_only",true}};
}

bool aiTychendeConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 auto*owner=const_cast<ServerPlayer*>(p);
 if(!tychendeCurrent(q,owner)||!tychendeScope(q,owner,true)||s.size()!=6||s.value("source_key")!="tychende"
  ||s.value("skill")!="_tychende"||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0
  ||!s.value("name").isString()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString name=s.value("name").toString();
 // Source dummyCard uses the CURRENT face of one actual AG offer. Printed-only
 // IDs cannot mint a current wrapped face; authenticate disclosure first.
 for(int id:q.choiceOptions.cardIds){
  const Card*c=Sanguosha->getCard(id);
  if(c&&tychendeShown(q,owner,id,c)&&c->objectName()==name)return tychendeCurrent(q,owner);
 }
 return false;
}

// ---- tyfenwu ----
namespace {
struct TyfenwuStack{lua_State*L;int n;explicit TyfenwuStack(lua_State*s):L(s),n(lua_gettop(s)){}~TyfenwuStack(){lua_settop(L,n);}};
struct TyfenwuRow {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class TyfenwuLedger final:public QObject {public:explicit TyfenwuLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,TyfenwuRow>>rows;};
TyfenwuLedger*tyfenwuLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<TyfenwuLedger*>(c))return l;return create?new TyfenwuLedger(r):nullptr;}
bool tyfenwuCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool tyfenwuScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::Choice&&q.choiceOptions.reason=="tyfenwu";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@tyfenwu"&&q.handlingMethod==Card::MethodUse;
}
const Card*tyfenwuEquippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool tyfenwuShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&tyfenwuEquippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void tyfenwuField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool tyfenwuSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool tyfenwuAbsentNewIndex(lua_State*L,int t){TyfenwuStack st(L);if(!lua_getmetatable(L,t))return true;tyfenwuField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool tyfenwuPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool tyfenwuEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool tyfenwuCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&tyfenwuEnv(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool tyfenwuLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));tyfenwuField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  tyfenwuField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool tyfenwuSourceReadBindings(Room&r,lua_State*L){
 TyfenwuStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 tyfenwuField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 tyfenwuField(L,sgs,"ai_skill_use");if(!tyfenwuPlain(L,-1))return false;
 tyfenwuField(L,-1,"@@tyfenwu");
 if(!tyfenwuCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",545,554))return false;
 tyfenwuField(L,sgs,"list");if(!tyfenwuCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 tyfenwuField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=tyfenwuCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=tyfenwuCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){tyfenwuField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 tyfenwuField(L,globals,"table");if(!tyfenwuPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){tyfenwuField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct TyfenwuWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;};
int tyfenwuAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<TyfenwuWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!tyfenwuCurrent(*w->q,w->owner)||w->lease()!=w->card
  ||!tyfenwuSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 tyfenwuField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!tyfenwuAbsentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool tyfenwuTarget(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct TyfenwuReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int tyfenwuReadTargets(lua_State*L){
 auto*w=static_cast<TyfenwuReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!tyfenwuCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!tyfenwuTarget(r,L,-1,p))return fail("saved native target identity or method changed");
  bool projected=w->q->worldView.self.objectName==p->objectName();for(const auto&v:w->q->worldView.players)if(v.objectName==p->objectName())projected=true;
  if(!projected)return fail("saved target absent from projected roster");w->out->append(p);lua_pop(L,1);
 }
 if(!tyfenwuCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAITyfenwuSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/kearlqxf-ai.lua",QByteArray("8195d9f79bfd68fb42d7a06b631dae90839c51352de2f13a19768ddfd6db8ae5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAITyfenwuSlots(Room&r){if(auto*l=tyfenwuLedger(r))delete l;}
QJsonObject queryAITyfenwuSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("tyfenwuUse");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","tyfenwu.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!tyfenwuCurrent(q,p)||!tyfenwuScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");TyfenwuStack stack(L);
 if(!tyfenwuSelf(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  {TyfenwuStack check(L);AILuaInspect::globals(L);tyfenwuField(L,-1,"sgs");tyfenwuField(L,-1,"ai_skill_choice");
   if(!tyfenwuPlain(L,-1))return fail("Tyfenwu choice registry is not plain");tyfenwuField(L,-1,"tyfenwu");
   if(!tyfenwuCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",527,543))return fail("Tyfenwu full source producer binding changed");}

  TyfenwuWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!tyfenwuShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!tyfenwuShown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,tyfenwuAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!tyfenwuCurrent(q,p)||w.lease()!=w.card)return fail("saved-plan assignment changed request or custody");
  {auto*l=tyfenwuLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=TyfenwuRow{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 {TyfenwuStack check(L);AILuaInspect::globals(L);tyfenwuField(L,-1,"sgs");tyfenwuField(L,-1,"ai_skill_use");
  if(!tyfenwuPlain(L,-1))return fail("Tyfenwu response registry is not plain");tyfenwuField(L,-1,"@@tyfenwu");
  if(!tyfenwuCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",545,554))return fail("Tyfenwu full source reader binding changed");}
 if(!tyfenwuLookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!tyfenwuLookup(L,saved,"card"))return fail("saved card getter unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||(lua_isboolean(L,cardIndex)&&!lua_toboolean(L,cardIndex)))return{{"status","pass"}};
 if(!tyfenwuSourceReadBindings(r,L))return fail("saved consumer serializer/iterator bindings changed");
 if(!lua_istable(L,saved)||!tyfenwuLookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!tyfenwuPlain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!tyfenwuTarget(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  TyfenwuReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,tyfenwuReadTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 auto*l=tyfenwuLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!tyfenwuShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!tyfenwuShown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");bool projected=q.worldView.self.objectName==t->objectName();for(const auto&v:q.worldView.players)if(v.objectName==t->objectName())projected=true;if(!projected)return fail("saved target absent from projected roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!tyfenwuCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 QJsonArray costs;for(int id:c->getSubcards())costs.append(id);
 QJsonObject spec{{"name",c->objectName()},{"skill",c->getSkillName(false)},{"suit",int(c->getSuit())},{"number",c->getNumber()},{"subcards",costs}};
 return{{"status","plan"},{"wire",wire},{"class_name",c->getClassName()},{"physical_id",c->isVirtualCard()?-1:c->getEffectiveId()},{"spec",spec},{"targets",names},{"evaluation_only",true}};
}

bool aiTyfenwuConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 auto*owner=const_cast<ServerPlayer*>(p);
 if(!tyfenwuCurrent(q,owner)||!tyfenwuScope(q,owner,true)||s.size()!=6||s.value("source_key")!="tyfenwu"
  ||s.value("skill")!="_tyfenwu"||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0
  ||!s.value("name").isString()||!s.value("subcards").isArray())return false;
 const auto name=s.value("name").toString();const auto ids=s.value("subcards").toArray();
 if(name=="cancel"||!q.choiceOptions.choices.contains(name)||ids.size()!=1||!ids.first().isDouble())return false;
 const double id=ids.first().toDouble(-1);if(id<0||id!=std::floor(id)||id>2147483647||int(id)!=p->getMark("tyfenwuId"))return false;
 bool own=false;for(const Card*c:p->getHandcards())if(c->getEffectiveId()==int(id)&&tyfenwuShown(q,owner,int(id),c)){own=true;break;}
 return own&&tyfenwuCurrent(q,owner);
}

bool aiTyfenwuRawProduct(const AIRequest&q,ServerPlayer*p,const QString&activation,const Card*c){
 if(activation!="tyfenwu"||!tyfenwuCurrent(q,p)||!tyfenwuScope(q,p,false)||!c||!c->isVirtualCard()
  ||c->getSkillName(false)!="_tyfenwu"||c->getSubcards().size()!=1)return false;
 const int id=c->getSubcards().first();
 if(id!=p->getMark("tyfenwuId")||!p->handCards().contains(id)||!tyfenwuShown(q,p,id))return false;
 const QString declaration=p->property("tyfenwuDc").toString();
 return declaration==c->objectName()&&q.prompt=="tyfenwu0:"+declaration&&tyfenwuCurrent(q,p);
}

// ---- tyqingkou ----
namespace {
struct TyqingkouStack{lua_State*L;int n;explicit TyqingkouStack(lua_State*s):L(s),n(lua_gettop(s)){}~TyqingkouStack(){lua_settop(L,n);}};
struct TyqingkouRow {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class TyqingkouLedger final:public QObject {public:explicit TyqingkouLedger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,TyqingkouRow>>rows;};
TyqingkouLedger*tyqingkouLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<TyqingkouLedger*>(c))return l;return create?new TyqingkouLedger(r):nullptr;}
bool tyqingkouCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool tyqingkouScope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::Choice&&q.choiceOptions.reason=="tyqingkou";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@tyqingkou"&&q.handlingMethod==Card::MethodUse;
}
const Card*tyqingkouEquippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool tyqingkouShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&tyqingkouEquippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void tyqingkouField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool tyqingkouSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool tyqingkouAbsentNewIndex(lua_State*L,int t){TyqingkouStack st(L);if(!lua_getmetatable(L,t))return true;tyqingkouField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool tyqingkouPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool tyqingkouEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool tyqingkouCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&tyqingkouEnv(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool tyqingkouLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));tyqingkouField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  tyqingkouField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool tyqingkouSourceReadBindings(Room&r,lua_State*L){
 TyqingkouStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 tyqingkouField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 tyqingkouField(L,sgs,"ai_skill_use");if(!tyqingkouPlain(L,-1))return false;
 tyqingkouField(L,-1,"@@tyqingkou");
 if(!tyqingkouCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",579,588))return false;
 tyqingkouField(L,sgs,"list");if(!tyqingkouCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 tyqingkouField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=tyqingkouCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=tyqingkouCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){tyqingkouField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 tyqingkouField(L,globals,"table");if(!tyqingkouPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){tyqingkouField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct TyqingkouWrite {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;};
int tyqingkouAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<TyqingkouWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!tyqingkouCurrent(*w->q,w->owner)||w->lease()!=w->card
  ||!tyqingkouSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 tyqingkouField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!tyqingkouAbsentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool tyqingkouTarget(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
struct TyqingkouReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int tyqingkouReadTargets(lua_State*L){
 auto*w=static_cast<TyqingkouReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!tyqingkouCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;if(!tyqingkouTarget(r,L,-1,p))return fail("saved native target identity or method changed");
  bool projected=w->q->worldView.self.objectName==p->objectName();for(const auto&v:w->q->worldView.players)if(v.objectName==p->objectName())projected=true;
  if(!projected)return fail("saved target absent from projected roster");w->out->append(p);lua_pop(L,1);
 }
 if(!tyqingkouCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAITyqingkouSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/kearlqxf-ai.lua",QByteArray("8195d9f79bfd68fb42d7a06b631dae90839c51352de2f13a19768ddfd6db8ae5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAITyqingkouSlots(Room&r){if(auto*l=tyqingkouLedger(r))delete l;}
QJsonObject queryAITyqingkouSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("tyqingkouUse");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","tyqingkou.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!tyqingkouCurrent(q,p)||!tyqingkouScope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");TyqingkouStack stack(L);
 if(!tyqingkouSelf(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  {TyqingkouStack check(L);AILuaInspect::globals(L);tyqingkouField(L,-1,"sgs");tyqingkouField(L,-1,"ai_skill_choice");
   if(!tyqingkouPlain(L,-1))return fail("Tyqingkou choice registry is not plain");tyqingkouField(L,-1,"tyqingkou");
   if(!tyqingkouCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",560,577))return fail("Tyqingkou full source producer binding changed");}

  TyqingkouWrite w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!tyqingkouShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!tyqingkouShown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,tyqingkouAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!tyqingkouCurrent(q,p)||w.lease()!=w.card)return fail("saved-plan assignment changed request or custody");
  {auto*l=tyqingkouLedger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=TyqingkouRow{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 {TyqingkouStack check(L);AILuaInspect::globals(L);tyqingkouField(L,-1,"sgs");tyqingkouField(L,-1,"ai_skill_use");if(!tyqingkouPlain(L,-1))return fail("response registry changed");tyqingkouField(L,-1,"@@tyqingkou");if(!tyqingkouCode(r,L,-1,"lua/ai/kearlqxf-ai.lua",579,588))return fail("source reader changed");}
 if(!tyqingkouLookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!tyqingkouLookup(L,saved,"card"))return fail("saved card getter unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||(lua_isboolean(L,cardIndex)&&!lua_toboolean(L,cardIndex)))return{{"status","pass"}};
 if(!tyqingkouSourceReadBindings(r,L))return fail("source list/serializer bindings changed");
 if(!lua_istable(L,saved)||!tyqingkouLookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!tyqingkouPlain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!tyqingkouTarget(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  TyqingkouReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,tyqingkouReadTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 auto*l=tyqingkouLedger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!tyqingkouShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!tyqingkouShown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");bool projected=q.worldView.self.objectName==t->objectName();for(const auto&v:q.worldView.players)if(v.objectName==t->objectName())projected=true;if(!projected)return fail("saved target absent from projected roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!tyqingkouCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 QJsonArray costs;for(int id:c->getSubcards())costs.append(id);
 QJsonObject spec{{"name",c->objectName()},{"skill",c->getSkillName(false)},{"suit",int(c->getSuit())},{"number",c->getNumber()},{"subcards",costs}};
 return{{"status","plan"},{"wire",wire},{"class_name",c->getClassName()},{"physical_id",c->isVirtualCard()?-1:c->getEffectiveId()},{"spec",spec},{"targets",names},{"evaluation_only",true}};
}

bool aiTyqingkouConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 auto*owner=const_cast<ServerPlayer*>(p);
 if(!tyqingkouCurrent(q,owner)||!tyqingkouScope(q,owner,true)||s.size()!=6||s.value("source_key")!="tyqingkou"
  ||s.value("skill")!="_tyqingkou"||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0
  ||!s.value("name").isString()||!s.value("subcards").isArray())return false;
 const auto name=s.value("name").toString();const auto ids=s.value("subcards").toArray();
 if(name=="cancel"||!q.choiceOptions.choices.contains(name)||ids.size()!=1||!ids.first().isDouble())return false;
 const double id=ids.first().toDouble(-1);if(id<0||id!=std::floor(id)||id>2147483647||int(id)!=p->getMark("tyqingkouId"))return false;
 bool own=false;for(const Card*c:p->getHandcards())if(c->getEffectiveId()==int(id)&&tyqingkouShown(q,owner,int(id),c)){own=true;break;}
 return own&&tyqingkouCurrent(q,owner);
}

// ---- ol-qifu-tenyear-xd-choice ----
int aiJiqiaoPrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &prototype)
{
    if (skill != "jiqiao" || prototype != "JiqiaoCard" || !p || !p->getRoom()
        || r.decisionId == 0 || r.viewerObjectName != p->objectName()
        || r.worldView.self.objectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::UseCard || r.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || r.pattern != "@@jiqiao" || r.handlingMethod != Card::MethodNone)
        return -1;
    QList<AiSkillActionContext> actions = r.skillActions;
    if (r.hasSkillActionContext) actions.prepend(r.skillActionContext);
    for (const auto &action : actions) {
        const auto &activation = action.activationRef;
        if (activation.ownerObjectName != p->objectName() || activation.key.skillName != skill
            || !action.isValid() || !action.activationQuotaAvailable || !action.sourceQuotaAvailable
            || !p->hasSkillInstance(skill, activation.key.instanceID)
            || p->isSkillInvalid(skill, activation.key.instanceID))
            continue;
        // Native Jiqiao::cost establishes this on its genuine BorrowedSkillScope.
        // Merely having the skill outside that selection window grants nothing.
        const QVariant state = p->getSkillInstanceStateValue(skill, activation.key.instanceID,
                                                            "select_equipment");
        if (state.type() == QVariant::Bool && state.toBool()) return 1;
    }
    return -1;
}

// ---- ol-qifu-tenyear-xd-response ----
namespace {
bool olQifuTenyearXdResponseInteger(const QJsonValue &value,int &out)
{
    const double n=value.toDouble();
    if(!value.isDouble()||!std::isfinite(n)||std::floor(n)!=n
        ||n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max())return false;
    out=int(n);return true;
}
bool olQifuTenyearXdResponseDisclosed(const AIRequest &r,int id)
{
    for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
    for(const auto &c:r.worldView.self.knownCards)if(c.effectiveId==id)return true;
    for(const auto &c:r.worldView.self.equips)if(c.effectiveId==id)return true;
    return false;
}
}
bool aiOlQifuResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
        ||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return false;
    const QString source=spec.value("source_key").toString();
    if((source!="cihuai"&&source!="chixin")||!p->hasSkill(source)
        ||spec.value("name")!=QStringLiteral("slash")||!spec.value("subcards").isArray())return false;
    int suit=-1,number=-1;
    if(!olQifuTenyearXdResponseInteger(spec.value("suit"),suit)||!olQifuTenyearXdResponseInteger(spec.value("number"),number))return false;
    const QJsonArray costs=spec.value("subcards").toArray();
    // The source fill is unconditional. Active-instance/hand-count admission
    // belongs to the actual provider at final selection, not enumeration.
    if(source=="cihuai")return spec.value("skill")==QStringLiteral("_cihuai")
        &&costs.isEmpty()&&suit==Card::NoSuit&&number==0;
    if(spec.value("skill")!=source||costs.size()!=1)return false;
    int id=-1;if(!olQifuTenyearXdResponseInteger(costs.first(),id)||id<0||!olQifuTenyearXdResponseDisclosed(r,id))return false;
    const Room *room=p->getRoom();
    if(room->getCardOwner(id)!=p||(room->getCardPlace(id)!=Player::PlaceHand
        &&room->getCardPlace(id)!=Player::PlaceEquip))return false;
    const Card *card=Sanguosha->getCard(id);
    // Source fill does not check using: preserve hypothetical sorting, and let
    // the native final payment gate reject a selected unavailable material.
    return card&&!card->isVirtualCard()&&card->getSuit()==Card::Diamond
        &&suit==int(card->getSuit())&&number==card->getNumber();
}

bool aiTenyearXdResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
        ||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision())return false;
    const QString skill=spec.value("source_key").toString(),name=spec.value("name").toString();
    const bool turn=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
    const bool prompted=skill=="zhifeng"&&r.kind==AIRequest::UseCard
        &&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@zhifeng"&&r.prompt=="zhifeng0";
    if((skill!="peiniang"&&skill!="zhifeng")||(!turn&&!prompted)||(turn&&!p->hasSkill(skill))
        ||spec.value("skill")!=skill||!spec.value("subcards").isArray())return false;
    int suit=-1,number=-1,id=-1;const auto costs=spec.value("subcards").toArray();
    if(!olQifuTenyearXdResponseInteger(spec.value("suit"),suit)||!olQifuTenyearXdResponseInteger(spec.value("number"),number)
        ||suit!=Card::NoSuit||number!=0||costs.size()!=1||!olQifuTenyearXdResponseInteger(costs.first(),id)||id<0||!olQifuTenyearXdResponseDisclosed(r,id))return false;
    const auto *room=p->getRoom();const auto place=room->getCardPlace(id);
    if(room->getCardOwner(id)!=p||(place!=Player::PlaceHand&&place!=Player::PlaceEquip))return false;
    const Card *card=Sanguosha->getCard(id);if(!card||card->isVirtualCard())return false;
    if(skill=="peiniang")return name=="analeptic"&&card->getSuitString()==p->property("yitongSuit").toString();
    // The fixed source counts he and uses n<1. The native V1 counts h and
    // uses n<0: preserve this real mismatch until selected native validation.
    const int sourceDifference=p->getCards("he").size()-p->getHp();
    return sourceDifference>0 ? name=="analeptic"&&card->isBlack() : name=="slash"&&card->isRed();
}

// ---- zhifeng-godlongnu ----
#include "room-state.h"
#include "roomthread.h"
#include <atomic>
namespace {
thread_local QHash<Room *, QList<const AIZhifengQuestion *>> questions;
std::atomic<quint64> nextQuestion{0};
}
struct AIZhifengQuestion::Data {
    Room *room = nullptr;
    QPointer<ServerPlayer> viewer;
    quint64 serial = 0, frame = 0;
    bool valid = false;
};
struct AIZhifengAccess {
    static const AIZhifengQuestion::Data *current(Room &room) {
        const auto it=questions.constFind(&room);
        return it==questions.cend() || it->isEmpty() ? nullptr : it->last()->d.get();
    }
    static bool valid(const AIRequest &r, Room &room, ServerPlayer *p, quint64 serial) {
        const auto *d=current(room); quint64 frame=0;
        return d && d->valid && p && d->viewer==p && serial && d->serial==serial
            && r.zhifengQuestionSerial==serial && r.viewerObjectName==p->objectName()
            && r.stateRevision==room.roomRuntime()->stateRevision()
            && r.kind==AIRequest::UseCard && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && r.pattern=="@@zhifeng" && r.prompt=="zhifeng0" && r.handlingMethod==Card::MethodUse
            && p->isAlive() && room.getCurrent()==p && p->hasFlag("CurrentPlayer")
            && room.roomState()->getCurrentCardUseReason()==r.reason
            && room.roomState()->getCurrentCardUsePattern()==r.pattern
            && room.getThread() && room.getThread()->currentThWeijingQuestion(p,frame) && frame==d->frame;
    }
};
AIZhifengQuestion::AIZhifengQuestion(Room &room, ServerPlayer *p,
    const QString &pattern, const QString &prompt, int method) : d(new Data) {
    d->room=&room; d->viewer=p; d->serial=++nextQuestion;
    questions[&room].append(this);
    d->valid=p && p->isAlive() && pattern=="@@zhifeng" && prompt=="zhifeng0"
        && method==int(Card::MethodUse) && room.getCurrent()==p && p->hasFlag("CurrentPlayer")
        && room.getThread() && room.getThread()->currentThWeijingQuestion(p,d->frame);
}
AIZhifengQuestion::~AIZhifengQuestion() {
    auto it=questions.find(d->room);if(it==questions.end())return;
    it->removeAll(this);if(it->isEmpty())questions.erase(it);
}
void projectAIZhifengQuestion(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto *d=AIZhifengAccess::current(room);if(!d||!d->valid)return;
    r.zhifengQuestionSerial=d->serial;
    if(!AIZhifengAccess::valid(r,room,p,d->serial)){r.zhifengQuestionSerial=0;return;}
    const QJsonObject row{{"known",true},{"kind","zhifeng_native_question"}};
    r.choiceOptions.context.insert("zhifeng_question",row);
    r.worldView.decisionContext.insert("zhifeng_question",row);
}
Card *createAIZhifengProduct(const AIRequest &r, Room &room, ServerPlayer *p,
    quint64 serial, const QList<int> &costs, QString *error) {
    const auto fail=[&](const char *message)->Card*{if(error)*error=QString::fromLatin1(message);return nullptr;};
    if(!AIZhifengAccess::valid(r,room,p,serial))return fail("ThWeijing recipient question is not active");
    // Fixed @@ source chooses exactly one he material, even where the V1 rule
    // could accept more. A question is not permission to submit another recipe.
    if(costs.size()!=1 || costs.first()<0)return fail("Zhifeng source requires one selected material");
    const int id=costs.first();
    if(room.getCardOwner(id)!=p || (room.getCardPlace(id)!=Player::PlaceHand && room.getCardPlace(id)!=Player::PlaceEquip))
        return fail("Zhifeng material is not the current recipient's hand or equipment");
    const Card *material=Sanguosha->getCard(id);
    const ViewAsSkill *vs=Sanguosha->getViewAsSkill("zhifeng");
    if(!material || material->hasFlag("using") || !vs || dynamic_cast<const ViewAsSkillV2*>(vs)
        || !vs->isEnabledAtResponse(p,"@@zhifeng"))return fail("Zhifeng native producer or material changed");
    // Caller supplies the normal native Self/room-state query scope; no fake
    // skill instance, activation ref, ownership, or pattern is installed here.
    if(!vs->viewFilter({},material))return fail("Zhifeng native material prefix rejected");
    Card *card=const_cast<Card*>(vs->viewAs({material}));
    if(!card)return fail("Zhifeng native producer declined");
    if(!card->isVirtualCard() || card->getSkillName(false)!="zhifeng" || card->getSubcards()!=costs
        || (card->objectName()!="slash" && card->objectName()!="analeptic" && card->objectName()!="duel")
        || p->isLocked(card) || p->isCardLimited(card,Card::MethodUse)
        || !AIZhifengAccess::valid(r,room,p,serial)) {
        card->deleteLater();return fail("Zhifeng native product or current question is invalid");
    }
    if(error)error->clear();return card;
}
Card *rebuildAIZhifengProduct(const AIRequest &r, Room &room, ServerPlayer *p,
    const AICardConversionView &t) {
    if(!t.zhifengQuestion || t.producerVersion!=1 || t.policyClass!="@zhifeng_question"
        || t.producerReason!=r.reason || t.producerPattern!=r.pattern || t.costCount!=1
        || !t.declaredName.isEmpty() || t.activationRef.isValid() || t.sourceRef.isValid())return nullptr;
    Card *card=createAIZhifengProduct(r,room,p,t.zhifengQuestionSerial,t.subcardIds);
    if(!card)return nullptr;
    if(card->objectName()!=t.name || card->getClassName()!=t.className || int(card->getSuit())!=t.suit
        || card->getNumber()!=t.number || card->getSkillName(false)!=t.skillName || card->toString()!=t.legacyStringKey){
        card->deleteLater();return nullptr;
    }
    return card;
}


bool aiGodlongnuConstruction(const AIRequest &r, const ServerPlayer *p, const QJsonObject &spec)
{
    if (!p || !p->getRoom() || r.viewerObjectName != p->objectName()
        || r.worldView.self.objectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::Activate || r.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || !p->hasSkill("godlongnu") || spec.value("source_key") != QStringLiteral("godlongnu")
        || spec.value("skill") != QStringLiteral("godlongnu") || !spec.value("subcards").isArray()) return false;
    const auto exactInt=[](const QJsonValue &v,int &out) {
        const double n=v.toDouble();
        if(!v.isDouble() || !std::isfinite(n) || std::floor(n)!=n
            || n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max()) return false;
        out=int(n);return true;
    };
    int id=-1,suit=-2,number=-2;const auto costs=spec.value("subcards").toArray();
    if(costs.size()!=1 || !exactInt(costs.first(),id) || id<0
        || !exactInt(spec.value("suit"),suit) || !exactInt(spec.value("number"),number)) return false;
    bool disclosed=false;
    for(const auto &c:r.worldView.handCards) if(c.effectiveId==id) disclosed=true;
    if(!disclosed || p->getRoom()->getCardOwner(id)!=p
        || p->getRoom()->getCardPlace(id)!=Player::PlaceHand) return false;
    const Card *material=Sanguosha->getCard(id);
    if(!material || material->isVirtualCard()) return false;
    const QString name=spec.value("name").toString();
    const bool all=p->getMark("&godlongnuall-PlayClear")>0;
    // Source tests these independent OR predicates. Native can_select_card has
    // mark precedence; selection (not enumeration) retains that distinction.
    const bool sourceBranch=(name=="fire_slash" && material->isRed()
        && (all || p->getMark("&godlongnu+:+fire_slash-PlayClear")>0))
        || (name=="thunder_slash" && material->isBlack()
        && (all || p->getMark("&godlongnu+:+thunder_slash-PlayClear")>0));
    if(!sourceBranch) return false;
    // Availability clone uses ToBeDecided/-1. Final CardParse carries the
    // single material's effective face (including JOKER inheritance).
    return (suit==int(Card::SuitToBeDecided) && number==-1)
        || (suit==int(material->getSuit()) && number==material->getNumber());
}

// ---- nyarz-huanshi-kanyu ----
#include <QObject>
namespace {
struct SavedJudge { QString token, who, pattern; bool good=false; };
class SavedJudges final : public QObject {
public:
    explicit SavedJudges(Room &room) : QObject(&room) { setObjectName("isolated-round24-saved-judges"); }
    QHash<QString,SavedJudge> rows;
};
SavedJudges *store(Room &room) {
    for (QObject *child:room.children())
        if (child->objectName()=="isolated-round24-saved-judges")
            if (auto *ledger=dynamic_cast<SavedJudges *>(child)) return ledger;
    return new SavedJudges(room);
}
std::unique_ptr<Card> disclosedClone(const AICardView &face) {
    if (face.effectiveId<0) return {};
    std::unique_ptr<Card> card(Sanguosha->cloneCard(face.objectName,Card::Suit(face.suit),face.number));
    if (!card || card->getClassName()!=face.className) return {};
    card->setId(face.effectiveId); card->setSkillName(face.skillName);
    return card;
}
bool publicPredicate(const QString &pattern) {
    if (pattern.contains('$')) return false;
    for (const QString &part:pattern.split('#')) {
        const QStringList factors=part.split('|');
        if (factors.size()>3 && factors[3]!=".") return false;
    }
    return true;
}
bool knownNoFilters(const AIRequest &request,const ServerPlayer *who) {
    const AIPlayerView *view=nullptr;
    if (who->objectName()==request.worldView.self.objectName) view=&request.worldView.self;
    else for (const auto &p:request.worldView.players) if (p.objectName==who->objectName()) {view=&p;break;}
    if (!view || !view->skillsComplete) return false;
    for (const auto &s:view->skills) if (!s.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(s.skillName))) return false;
    // Weidi's appended lord filters must not be inferred absent from another roster.
    for (const auto &s:view->skills) if (!s.invalid && s.skillName=="weidi") return false;
    return true;
}
}
void projectAIStrategyHuanshiKanyuResponse(AIRequest &request,Room &room,ServerPlayer *viewer,
                                     const QVariant &data,QJsonObject &context) {
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()) return;
    const QString reason=request.choiceOptions.reason;
    if (reason=="nyarz_huanshi" && (request.kind==AIRequest::Discard || request.kind==AIRequest::AmazingGrace)) {
        const QVariant raw=room.getTag("nyarz_huanshi");
        const JudgeStruct *pointer=raw.value<JudgeStruct *>();
        const JudgeStruct judge=pointer ? *pointer : raw.value<JudgeStruct>();
        if (!raw.isValid() || !judge.who) return;
        // The fixed writer asks the skill owner to exchange HE, then the judge
        // recipient to select from the faces it just publicly showed.
        if (request.kind==AIRequest::Discard && !viewer->hasSkill("nyarz_huanshi")) return;
        if (request.kind==AIRequest::AmazingGrace && judge.who!=viewer) return;
        QList<AICardView> faces=request.kind==AIRequest::AmazingGrace ? request.choiceOptions.cards : request.worldView.handCards;
        if (request.kind==AIRequest::Discard) faces.append(request.worldView.self.equips);
        QJsonObject matches;
        for (const auto &face:faces) {
            if (request.kind==AIRequest::AmazingGrace && !request.choiceOptions.cardIds.contains(face.effectiveId)) continue;
            auto card=disclosedClone(face);if (!card) continue;
            // Exactly the source's nil receiver. In particular place and '$'
            // availability clauses are skipped by ExpPattern with nullptr.
            matches.insert(QString::number(face.effectiveId),Sanguosha->matchExpPattern(judge.pattern,nullptr,card.get()));
        }
        auto second=context.value("NyarzSecond").toObject();
        second.insert("judge_nil_matches",QJsonObject{{judge.pattern,matches}});
        context.insert("NyarzSecond",second);
    }
    if (reason!="kanyu") return;
    if (request.kind==AIRequest::SkillInvoke) {
        auto *ledger=store(room);
        const JudgeStruct *judge=data.value<JudgeStruct *>();
        if (!judge || !judge->who) {ledger->rows.remove(viewer->objectName());context.insert("round24_kanyu",QJsonObject{{"present",false}});return;}
        SavedJudge row{QUuid::createUuid().toString(QUuid::WithoutBraces),judge->who->objectName(),judge->pattern,judge->good};
        ledger->rows.insert(viewer->objectName(),row);
        context.insert("round24_kanyu",QJsonObject{{"present",true},{"token",row.token},{"who",row.who},{"pattern",row.pattern},{"predicate_good",row.good}});
    } else if (request.kind==AIRequest::AmazingGrace) {
        auto *ledger=store(room);
        if (!ledger->rows.contains(viewer->objectName())) return;
        const SavedJudge row=ledger->rows.value(viewer->objectName());
        ServerPlayer *who=nullptr;for (auto *p:room.getAllPlayers(true)) if (p->objectName()==row.who) {who=p;break;}
        if (!who) return;
        JudgeStruct judge;judge.who=who;judge.pattern=row.pattern;judge.good=row.good;
        QJsonObject outcomes;
        for (const auto &face:request.choiceOptions.cards) {
            if (!request.choiceOptions.cardIds.contains(face.effectiveId)) continue;
            if (who==viewer) {
                const auto filtered=projectAIOwnerFilteredCard(room,viewer,face.effectiveId,&judge,int(Player::PlaceJudge),&face);
                if (filtered.value("complete").toBool() && filtered.value("judge_outcome").isBool()) outcomes.insert(QString::number(face.effectiveId),filtered.value("judge_outcome"));
            } else if (knownNoFilters(request,who) && publicPredicate(row.pattern)) {
                auto card=disclosedClone(face);if (card) outcomes.insert(QString::number(face.effectiveId),judge.isGood(card.get()));
            }
        }
        context.insert("round24_kanyu",QJsonObject{{"present",true},{"token",row.token},{"who",row.who},{"pattern",row.pattern},{"predicate_good",row.good},{"outcomes",outcomes}});
    }
}

void clearAIStrategyKanyuResponse(Room &room) {
    for (QObject *child:room.children())
        if (child->objectName()=="isolated-round24-saved-judges")
            if (auto *ledger=dynamic_cast<SavedJudges *>(child)) ledger->rows.clear();
}
