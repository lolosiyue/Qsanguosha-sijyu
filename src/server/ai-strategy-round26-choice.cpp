#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round26-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include "ai-strategy-public-disclosure.h"
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QHash>
namespace {
class FlagLedger final:public QObject {
public: explicit FlagLedger(Room&room):QObject(&room){setObjectName("isolated-round26-choice-flags");}
 QHash<QString,QJsonObject> rows;QSet<ServerPlayer*> connected;
};
FlagLedger*flagLedger(Room&room,bool create){
 auto*object=room.findChild<QObject*>("isolated-round26-choice-flags",Qt::FindDirectChildrenOnly);
 if(!object&&create)return new FlagLedger(room);
 return static_cast<FlagLedger*>(object);
}
bool witnessedFlag(Room&room,ServerPlayer*p,const QString&name){
 const auto*ledger=flagLedger(room,false);if(!ledger||!p)return false;
 const auto value=ledger->rows.value(p->objectName()).value(name);
 return value.isBool()&&value.toBool()&&p->hasFlag(name);
}
bool valid(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool owns(const ServerPlayer*p,const QString&s){return p->hasSkill(s)||(s=="jiwu_extarget"&&p->hasSkill("sr_jiwu"));}
}
void observeAIRound26ChoiceFlag(Room&room,ServerPlayer*p,const QString&flag){
 if(!p||p->getRoom()!=&room||room.findPlayerByObjectName(p->objectName(),true)!=p)return;
 const bool clear=flag.startsWith('-');const QString name=clear?flag.mid(1):flag;
 if(name!="dailao_target"&&name!="youdisource")return;
 auto*ledger=flagLedger(room,true);
 if(!ledger->connected.contains(p)){
  ledger->connected.insert(p);const QString player=p->objectName();
  // A direct/private mutation (including clear then restore) must never revive
  // an old public witness. This conservative invalidation is local to evidence.
  QObject::connect(p,&Player::gameplay_property_changed,ledger,[ledger,player]{ledger->rows.remove(player);});
  QObject::connect(p,&QObject::destroyed,ledger,[ledger,p,player]{ledger->connected.remove(p);ledger->rows.remove(player);});
 }
 auto row=ledger->rows.value(p->objectName());
 if(p->hasFlag(name)==!clear)row.insert(name,!clear);else row.remove(name);
 ledger->rows.insert(p->objectName(),row);
}
int aiRound26ChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!valid(r,p)||!owns(p,skill))return -1;
 // Exact source cardsview callbacks return these legacy SkillCards without
 // a declaration string. Native provider activation/cost validation still runs.
 if((r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)){
  const bool peach=r.pattern=="peach"||r.pattern=="peach+analeptic"||r.pattern=="analeptic+peach";
  if(skill=="sr_fangxin"&&prototype=="#sr_fangxincard"&&peach)return 1;
  if(skill=="sr_qingcheng"&&prototype=="#sr_qingchengcard"&&(r.pattern=="slash"||r.pattern=="jink"))return 0;
  if(skill=="sr_xiahou"&&prototype=="#sr_xiahoucard"&&(peach||r.pattern=="slash"||r.pattern=="jink"||r.pattern=="analeptic"))return 0;
 }
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
  if(skill=="sr_quanheng_slash"&&prototype=="#sr_quanheng_slashCard"&&r.pattern=="@@sr_quanheng_slash")return 1;
  if(skill=="sr_quanheng_ex_nihilo"&&prototype=="#sr_quanheng_ex_nihiloCard"&&r.pattern=="@@sr_quanheng_ex_nihilo")return 1;
  if(skill=="sr_shuixi"&&prototype=="#sr_shuixicard"&&r.pattern=="@@sr_shuixi")return 1;
  if(skill=="sr_fuzheng"&&prototype=="#sr_fuzhengcard"&&r.pattern=="@@sr_fuzheng")return 0;
  if(skill=="sr_youdi"&&prototype=="#sr_youdicard"&&r.pattern=="@@sr_youdi")return 1;
  if(skill=="jiwu_extarget"&&prototype=="#jiwu_extarget"&&r.pattern=="@@jiwu_extarget")return 0;
 }
 if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play)return -1;
 if(skill=="sr_chouxi"&&prototype=="#sr_chouxicard")return 1;
 if(skill=="sr_shouji"&&prototype=="#sr_shoujicard")return 1;
 if(skill=="sr_yaozhan"&&prototype=="#sr_yaozhancard")return 1;
 if(skill=="sr_wenjiu"&&prototype=="#sr_wenjiucard")return 1;
 if(skill=="sr_sanfen"&&prototype=="#sr_sanfencard")return 0;
 if(skill=="sr_quanheng"&&prototype=="#sr_quanhengCard")return 0;
 if(skill=="sr_xionglve"&&prototype=="#sr_xionglvecard")return 0;
 if(skill=="sr_dailao"&&prototype=="#sr_dailaocard")return 0;
 if(skill=="sr_weibao"&&prototype=="#sr_weibaocard")return 1;
 if(skill=="sr_choulve"&&prototype=="#sr_choulvecard")return 0;
 if(skill=="sr_jiexi"&&prototype=="#sr_jiexicard")return 1;
 if(skill=="sr_youxia"&&prototype=="#sr_youxiacard")return 0;
 if(skill=="sr_zhouyan"&&prototype=="#sr_zhouyancard")return 0;
 if(skill=="sr_zhaxiang"&&prototype=="#sr_zhaxiangcard")return 1;
 if(skill=="sr_fangxin"&&prototype=="#sr_fangxincard")return 1;
 if(skill=="sr_yinmeng"&&prototype=="#sr_yinmengcard")return 0;
 if(skill=="sr_juelie"&&prototype=="#sr_jueliecard")return 0;
 if(skill=="sr_zhishi"&&prototype=="#sr_zhishicard")return 0;
 if(skill=="sr_aozhan"&&prototype=="#sr_aozhanCard")return 0;
 if(skill=="sr_liuyun"&&prototype=="#sr_liuyuncard")return 1;
 if(skill=="sr_qingcheng"&&prototype=="#sr_qingchengcard")return 0;
 if(skill=="sr_xiahou"&&prototype=="#sr_xiahoucard")return 0;
 if(skill=="sr_lijian"&&prototype=="#sr_lijiancard")return 1;
 if(skill=="sr_manwu"&&prototype=="#sr_manwucard")return 0;
 if(skill=="sr_xingyi"&&prototype=="#sr_xingyicard")return 0;
 if(skill=="sr_jiwu"&&prototype=="#sr_jiwucard")return 0;
 return -1;
}
bool aiRound26ChoiceConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
 if(!valid(r,p)||s.size()!=6||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),skill=s.value("skill").toString(),name=s.value("name").toString();
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&p->getPhase()==Player::Play;
 const bool responding=r.kind==AIRequest::RespondCard;
 const auto costs=s.value("subcards").toArray();
 if((key=="sr_hemouvs"||key=="sr_sheji")&&skill==key&&p->hasSkill(key)&&play&&costs.size()==1){
  if(!costs[0].isDouble()||costs[0].toDouble()!=costs[0].toInt(-1))return false;
  const int id=costs[0].toInt(-1);Room*room=p->getRoom();const Card*material=id>=0?room->getCard(id):nullptr;
  if(!material||room->getCardOwner(id)!=p||s.value("suit")!=QJsonValue(int(material->getSuit()))||s.value("number")!=QJsonValue(material->getNumber()))return false;
  if(key=="sr_sheji")return name=="slash"&&material->isKindOf("EquipCard")&&(room->getCardPlace(id)==Player::PlaceHand||room->getCardPlace(id)==Player::PlaceEquip);
  const int mark=p->getMark("hemousuit");const QStringList names{"duel","collateral","snatch","fire_attack"};
  return room->getCardPlace(id)==Player::PlaceHand&&mark>=1&&mark<=4&&int(material->getSuit())==mark-1&&name==names[mark-1];
 }
 if(!costs.isEmpty()||s.value("number")!=QJsonValue(0))return false;
 if(key=="sr_fangxin"&&p->hasSkill(key)&&(play||responding)&&skill.isEmpty())
  return (name=="indulgence"&&s.value("suit")==QJsonValue(int(Card::Diamond)))||(name=="supply_shortage"&&s.value("suit")==QJsonValue(int(Card::Club)));
 if(s.value("suit")!=QJsonValue(int(Card::NoSuit))||name!="slash")return false;
 if(key=="#sr_yaozhancard"&&p->hasSkill("sr_yaozhan")&&play)return skill=="_sr_yaozhan";
 if(key=="sr_jiuzhu"&&p->hasSkill(key)&&r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==key)return skill=="_sr_jiuzhu";
 if(key=="@@sr_quanheng_slash"&&p->hasSkill("sr_quanheng_slash")&&r.kind==AIRequest::UseCard&&r.pattern==key)return skill=="sr_quanheng";
 return false;
}
void projectAIRound26ChoiceContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&,QJsonObject&context,const std::function<QJsonObject(const Card*)>&publicCard){
 if(!valid(r,p)||p->getRoom()!=&room||!publicCard)return;
 auto row=context.value("round26_choice").toObject();row.remove("flag_witness");const QString reason=r.choiceOptions.reason;
 const auto roster=[&](const Player*v){return v&&room.findPlayerByObjectName(v->objectName(),true)==v;};
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 if((play&&([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,p,gameLua));
        return (p->hasSkill("sr_xionglve")||p->hasSkill("sr_qingcheng")||p->hasSkill("sr_xiahou")||p->hasSkill("sr_jiwu")); }()))
  ||(r.kind==AIRequest::Choice&&(reason=="sr_xionglvebasic"||reason=="sr_xiahou"))
  ||(r.choiceOptions.question=="askForAG"&&reason=="sr_xionglvecard"))row.insert("slash_available",Slash::IsAvailable(p,nullptr));
 // srkill.lua178..184 broadcasts the two face-up moves before this exact cardask.
 // A table location/tag alone is not visibility proof; retain only exact public-disclosure records.
 if(r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForCard"&&r.prompt.section(':',0,0)=="@srchouxi-discard"){
  const auto raw=room.getTag("agcards").toString();QJsonArray ids;QJsonObject cards;bool known=true;
  for(const QString&part:raw.split('+',Qt::SkipEmptyParts)){
   bool ok=false;const int id=part.toInt(&ok);const auto proof=ok&&id>=0?readAIStrategyPublicDisclosure(room,p,id):QJsonObject();
   if(!proof.value("known").toBool()){known=false;break;}
   ids.append(part);cards.insert(QString::number(id),proof.value("card"));
  }
  if(known){row.insert("agcards",ids);row.insert("ag_faces",cards);}
 }
 // These named payloads are exposed only to the actual prompted actor.
 if(r.kind==AIRequest::Discard&&reason=="sr_fuzheng1")row.insert("fuzheng_num",room.getTag("fuzheng_num").toInt());
 if(r.kind==AIRequest::PlayerChosen&&reason=="sr_zhaxiang"&&p->hasSkill(reason))row.insert("zhaxiang",room.getTag("zhaxiang").toString());
 if(r.kind==AIRequest::PlayerChosen&&reason=="sr_huiqu"&&p->hasSkill(reason)){
  auto*target=room.getTag("huiquTarget").value<ServerPlayer*>();
  if(!target||roster(target))row.insert("huiqu_target",target?QJsonValue(target->objectName()):QJsonValue(false));
 }
 if((r.kind==AIRequest::Choice&&reason=="sr_dailao")||(r.kind==AIRequest::UseCard&&r.pattern=="@@sr_youdi")){
  const QString flag=reason=="sr_dailao"?"dailao_target":"youdisource";
  row.remove(flag);
  // Static setters establish the scope, not disclosure. Require the live
  // first flagged target to match an actual public notification witness.
  for(auto*other:room.getOtherPlayers(p))if(other->hasFlag(flag)){
   if(witnessedFlag(room,other,flag))row.insert(flag,other->objectName());
   break;
  }
 }
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@jiwu_extarget"&&p->hasSkill("sr_jiwu")){
  const auto raw=room.getTag("jiwudata");if(raw.canConvert<CardUseStruct>()){
   const auto use=raw.value<CardUseStruct>();bool known=use.from==p&&use.card&&use.card->isKindOf("Slash");
   QJsonArray targets;for(auto*to:use.to){known=known&&roster(to);if(to)targets.append(to->objectName());}
   if(known)row.insert("jiwu_use",QJsonObject{{"from",p->objectName()},{"to",targets},{"card",publicCard(use.card)}});
  }
 }
 if(r.kind==AIRequest::Choice&&reason=="sr_xionglvebasic"&&p->hasSkill("sr_xionglve"))
  row.insert("flag_witness",QJsonObject{{"source_key",reason},{"player",p->objectName()},{"name","xionglveused"},{"value",true}});
 if(r.kind==AIRequest::Choice&&reason=="sr_dailao"&&p->hasSkill("sr_dailao")&&row.value("dailao_target").isString())
  row.insert("flag_witness",QJsonObject{{"source_key",reason},{"player",row.value("dailao_target")},{"name","dailao_target"},{"value",false}});
 if(!row.isEmpty())context.insert("round26_choice",row);
}

// Preflight occurs before any pending effect is committed. Commit revalidates the
// same finite proof, then performs the single source-ordered public flag write.
bool stageAIRound26ChoiceFlags(Room&room,const AIRequest&r,const AIResult&result,bool commit,QString*error){
 QJsonObject effect;int count=0;
 for(const auto&raw:result.strategyEffects)if(raw.isObject()&&raw.toObject().value("kind")=="srkill_choice_flag"){
  effect=raw.toObject();++count;
 }
 if(!count)return true;
 const auto fail=[&](const QString&why){if(error)*error=why;return false;};
 auto*p=room.findPlayerByObjectName(r.viewerObjectName,true);
 if(count!=1||!valid(r,p)||p->getRoom()!=&room||r.kind!=AIRequest::Choice||!result.handled
  ||result.kind!=AIResult::Answer||!result.errorCode.isEmpty()||result.decisionId!=r.decisionId
  ||result.stateRevision!=r.stateRevision)return fail("SRkill choice flag request/result mismatch");
 const QStringList required{"kind","source_key","player","name","value"};
 for(auto it=effect.begin();it!=effect.end();++it)
  if(!required.contains(it.key())&&!(it.key()=="after_delta"&&it.value()==QJsonValue(0)))return fail("SRkill choice flag shape");
 if(!effect.value("source_key").isString()||!effect.value("player").isString()
  ||!effect.value("name").isString()||!effect.value("value").isBool())return fail("SRkill choice flag types");
 const auto row=r.choiceOptions.context.value("round26_choice").toObject();
 const auto witness=row.value("flag_witness").toObject();
 for(const QString&key:QStringList{"source_key","player","name","value"})
  if(!witness.contains(key)||witness.value(key)!=effect.value(key))return fail("SRkill choice flag missing witness");
 const QString source=effect.value("source_key").toString(),name=effect.value("name").toString();
 auto*target=room.findPlayerByObjectName(effect.value("player").toString(),true);
 if(!target||!target->isAlive()||r.choiceOptions.reason!=source
  ||!r.choiceOptions.choices.contains(result.action.userString))return fail("SRkill choice flag actor/answer");
 if(source=="sr_xionglvebasic"){
  if(target!=p||!p->hasSkill("sr_xionglve")||name!="xionglveused"||!effect.value("value").toBool()
   ||result.action.userString!="cancel")return fail("SRkill Xionglve flag contract");
 }else if(source=="sr_dailao"){
  if(!p->hasSkill("sr_dailao")||target==p||name!="dailao_target"||effect.value("value").toBool()
   ||!witnessedFlag(room,target,name)||row.value("dailao_target")!=QJsonValue(target->objectName())
   ||(result.action.userString!="srdraw"&&result.action.userString!="srdiscard"))return fail("SRkill Dailao flag contract");
  ServerPlayer*first=nullptr;for(auto*other:room.getOtherPlayers(p))if(other->hasFlag(name)){first=other;break;}
  if(first!=target)return fail("SRkill Dailao original target order");
 }else return fail("SRkill choice flag source");
 if(commit)room.setPlayerFlag(target,effect.value("value").toBool()?name:"-"+name);
 return true;
}
