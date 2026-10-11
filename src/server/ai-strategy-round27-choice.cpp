#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round27-choice.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "engine.h"
#include "standard.h"
#include "ai-strategy-public-disclosure.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool valid(const AIRequest &r,const ServerPlayer *p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(*p->getRoom(),r,p);}
}
bool aiRound27ChoicePlanningQuestion(const AIRequest &r,Room &room,const ServerPlayer *p){
 return valid(r,p)&&p->getRoom()==&room&&r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="ov_xiafeng"
  &&p->isAlive()&&p->hasSkill("ov_xiafeng")&&p->getPhase()==Player::Play&&p->getTag("ov_baonieNum").toInt()>0;
}
QJsonObject aiRound27ChoicePlanningContext(const AIRequest &r,Room &room,const ServerPlayer *p){
 if(!aiRound27ChoicePlanningQuestion(r,room,p))return{};
 return{{"known",true},{"readonly",true},{"source","OverseasVersion.ov_xiafeng"},{"viewer",r.viewerObjectName},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"kind","activate"},
  {"reason",int(CardUseStruct::CARD_USE_REASON_PLAY)},{"pattern",""},{"handling_method",int(Card::MethodUse)}};
}

int aiRound27ChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!valid(r,p)||!p->isAlive()||(!p->hasSkill(skill)&&!p->hasLordSkill(skill)&&!(skill=="ov_yuejian"&&r.pattern=="@@ov_wanwei!"&&p->hasSkill("ov_wanwei"))&&!(skill=="ov_chengxi"&&r.pattern=="@@ov_xinghan"&&p->hasSkill("ov_xinghan"))&&!(skill=="ov_fenxian"&&r.pattern=="@@ov_fuzhu"&&p->hasSkill("ov_fuzhu"))))return -1;
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&p->getPhase()==Player::Play;
 if(play){
  if(skill=="ov_lingbao"&&prototype=="#ov_lingbaoCard")return 1;
  if(skill=="ov_beini"&&prototype=="#ov_beiniCard")return 0;
  if(skill=="ov_weipo"&&prototype=="#ov_weipoCard")return 0;
  if(skill=="ov_weipobf"&&prototype=="#ov_weipobfCard")return 1;
  if(skill=="ov_mouzhu"&&prototype=="#ov_mouzhuCard")return 0;
  if(skill=="ov_daoji"&&prototype=="#ov_daojiCard")return 1;
  if(skill=="ov_yujuevs"&&prototype=="#ov_yujuevsCard")return 1;
  if(skill=="ov_sfzhouhu"&&prototype=="#ov_sfzhouhuCard")return 1;
  if(skill=="ov_sffengqi"&&prototype=="#ov_sffengqiCard")return 1;
  if(skill=="ov_gongqi"&&prototype=="#ov_gongqiCard")return 1;
  if(skill=="ov_jiefan"&&prototype=="#ov_jiefanCard")return 0;
  if(skill=="ov_fuzuan"&&prototype=="#ov_fuzuanCard")return 0;
  if(skill=="ov_zhuidu"&&prototype=="#ov_zhuiduCard")return 0;
  if(skill=="ov_luannianvs"&&prototype=="#ov_luannianCard")return 1;
  if(skill=="ov_bingde"&&prototype=="#ov_bingdeCard")return 1;
  if(skill=="ov_xiongsi"&&prototype=="#ov_xiongsiCard")return 0;
  if(skill=="ov_xiongxi"&&prototype=="#ov_xiongxiCard")return 1;
  if(skill=="ov_juxiangvs"&&prototype=="#ov_juxiangCard")return 1;
  if(skill=="ov_shijunvs"&&prototype=="#ov_shijunCard")return 0;
  if(skill=="ov_xingzhui"&&prototype=="#ov_xingzhuiCard")return 0;
  if(skill=="ov_luanlve"&&prototype=="#ov_luanlveCard")return 1;
  if(skill=="ov_jichou"&&prototype=="#ov_jichouCard")return 0;
  if(skill=="ov_mutao"&&prototype=="#ov_mutaoCard")return 0;
  if(skill=="ov_kujian"&&prototype=="#ov_kujianCard")return 1;
  if(skill=="ov_boming"&&prototype=="#ov_bomingCard")return 1;
  if(skill=="ov_jinglve"&&prototype=="#ov_jinglveCard")return 0;
  if(skill=="ov_fuman"&&prototype=="#ov_fumanCard")return 1;
  if(skill=="ov_mouli"&&prototype=="#ov_mouliCard")return 2;
  if(skill=="ov_yuanhu"&&prototype=="#ov_yuanhuCard")return 1;
  if(skill=="ov_qingce"&&prototype=="#ov_qingceCard")return 1;
  if(skill=="ov_luanchou"&&prototype=="#ov_luanchouCard")return 0;
  if(skill=="ov_gongxin"&&prototype=="#ov_gongxinCard")return 0;
  if(skill=="ov_kaizengvs"&&prototype=="#ov_kaizengCard")return 0;
  if(skill=="ov_xiechang"&&prototype=="#ov_xiechangCard")return 0;
  if(skill=="ov_yuejian"&&prototype=="#ov_yuejianCard")return 1;
  if(skill=="ov_muyue"&&prototype=="#ov_muyueCard")return 1;
  if(skill=="ov_fubi"&&prototype=="#ov_fubiCard")return 1;
  if(skill=="ov_quanqian"&&prototype=="#ov_quanqianCard")return 1;
  if(skill=="ov_chengxi"&&prototype=="#ov_chengxiCard")return 0;
  if(skill=="ov_danlie"&&prototype=="#ov_danlieCard")return 0;
  if(skill=="ov_huajing"&&prototype=="#ov_huajingCard")return 1;
  if(skill=="ov_jieqiu"&&prototype=="#ov_jieqiuCard")return 0;
  if(skill=="ov_enchou"&&prototype=="#ov_enchouCard")return 0;
  if(skill=="ov_yanshiYL"&&prototype=="#ov_yanshiYLCard")return 0;
  if(skill=="ov_huanji"&&prototype=="#ov_huanjiCard")return 0;
  if(skill=="ov_qinghan"&&prototype=="#ov_qinghanCard")return 1;
  if(skill=="ov_xianyuan"&&prototype=="#ov_xianyuanCard")return 0;
  if(skill=="ov_lifeng"&&prototype=="#ov_lifengCard")return 1;
  if(skill=="ov_guihan"&&prototype=="#ov_guihanCard")return 0;
  if(skill=="ov_renxian"&&prototype=="#ov_renxianCard")return 0;
  if(skill=="ov_liaoluanvs"&&prototype=="#ov_liaoluanCard")return 0;
  if(skill=="ov_fenxian"&&prototype=="#ov_fenxianCard")return 0;
  if(skill=="ov_shiyi"&&prototype=="#ov_shiyiCard")return 0;
  if(skill=="ov_miyong"&&prototype=="#ov_miyongCard")return 1;
  if(skill=="ov_hanhong"&&prototype=="#ov_hanhongCard")return 2;
  if(skill=="ov_qianxiong"&&prototype=="#ov_qianxiongCard")return 0;
  if(skill=="ov_fushu"&&prototype=="#ov_fushuCard")return 1;
 }
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
  if(r.pattern=="@@ov_zhenjun"&&skill=="ov_zhenjun"&&prototype=="#ov_zhenjunCard")return 1;
  if(r.pattern=="@@ov_yanhuo"&&skill=="ov_yanhuo"&&prototype=="#ov_yanhuoCard")return 0;
  if(r.pattern=="@@ov_fuzuan"&&skill=="ov_fuzuan"&&prototype=="#ov_fuzuanCard")return 0;
  if(r.pattern=="@@ov_kaiji"&&skill=="ov_kaiji"&&prototype=="#ov_kaijiCard")return 0;
  if(r.pattern=="@@ov_fenghan"&&skill=="ov_fenghan"&&prototype=="#ov_fenghanCard")return 0;
  if(r.pattern=="@@ov_xiongzheng"&&skill=="ov_xiongzheng"&&prototype=="#ov_xiongzhengCard")return 0;
  if(r.pattern=="@@ov_polu"&&skill=="ov_polu"&&prototype=="#ov_poluCard")return 0;
  if(r.pattern=="@@ov_dingzhen"&&skill=="ov_dingzhen"&&prototype=="#ov_dingzhenCard")return 0;
  if(r.pattern=="@@ov_chaofeng"&&skill=="ov_chaofeng"&&prototype=="#ov_chaofengCard")return 0;
  if(r.pattern=="@@ov_zhenhu!"&&skill=="ov_zhenhu"&&prototype=="#ov_zhenhuCard")return 0;
  if(r.pattern=="@@ov_xuanhuo"&&skill=="ov_xuanhuo"&&prototype=="#ov_xuanhuoCard")return 1;
  if(r.pattern=="@@ov_yuanhu"&&skill=="ov_yuanhu"&&prototype=="#ov_yuanhuCard")return 1;
  if(r.pattern=="@@ov_hongju"&&skill=="ov_hongju"&&prototype=="#ov_hongjuCard")return 1;
  if(r.pattern=="@@ov_xingluan!"&&skill=="ov_xingluan"&&prototype=="#ov_xingluanCard")return 1;
  if(r.pattern=="@@ov_zaoli!"&&skill=="ov_zaoli"&&prototype=="#ov_zaoliCard")return 1;
  if(r.pattern=="@@ov_liexi"&&skill=="ov_liexi"&&prototype=="#ov_liexiCard")return 1;
  if(r.pattern=="@@ov_wanwei!"&&skill=="ov_yuejian"&&prototype=="#ov_yuejianCard")return 1;
  if(r.pattern=="@@ov_xingwu"&&skill=="ov_xingwu"&&prototype=="#ov_xingwuCard")return 1;
  if(r.pattern=="@@ov_lijian"&&skill=="ov_lijian"&&prototype=="#ov_lijianCard")return 1;
  if(r.pattern=="@@ov_xinghan"&&skill=="ov_chengxi"&&prototype=="#ov_chengxiCard")return 0;
  if(r.pattern=="@@ov_niwo"&&skill=="ov_niwo"&&prototype=="#ov_niwoCard")return 1;
  if(r.pattern=="@@ov_fuzhu"&&skill=="ov_fenxian"&&prototype=="#ov_fenxianCard")return 0;
  if(r.pattern=="@@ov_xiumu!"&&skill=="ov_xiumu"&&prototype=="#ov_xiumuCard")return 1;
 }
 if((r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)){
  if(skill=="ov_yingji"&&prototype=="#ov_yingjiCard")return 2;
  if(skill=="ov_mouli"&&prototype=="#ov_mouliCard")return 2;
  if(skill=="ov_fushu"&&prototype=="#ov_fushuCard"&&(r.pattern=="peach"||r.pattern=="peach+analeptic"||r.pattern=="analeptic+peach"))return 1;
 }
 return -1;
}

bool aiRound27ChoiceConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
 if(!valid(r,p)||!p->isAlive()||!s.value("subcards").isArray()||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0)return false;
 const QString key=s.value("source_key").toString(),raw=s.value("skill").toString(),name=s.value("name").toString();
 const auto costs=s.value("subcards").toArray();const bool recast=s.contains("can_recast");
 if(s.size()!=6+int(recast)|| (recast&&(key!="ov_cairu"||!s.value("can_recast").isBool()||s.value("can_recast").toBool())))return false;
 // Source construction is readonly: intermediate zero/one-cost clones are
 // deliberate hypotheses. Selected payment remains the ordinary sealed binder.
 QString ownerSkill;QStringList rawSkills,names;int maxCosts=0;bool play=false,response=false,named=false;
 if(key=="ov_sidai"){ownerSkill="ov_sidai";rawSkills={"","ov_sidai"};names={"slash"};maxCosts=256;play=true;}
 if(key=="@@ov_xiongzheng"){ownerSkill="ov_xiongzheng";rawSkills={"","_ov_xiongzheng"};names={"slash"};maxCosts=0;named=true;}
 if(key=="ov_xiongsi"){ownerSkill="ov_xiongsi";rawSkills={"","ov_xiongsi"};names={"slash"};maxCosts=0;play=true;}
 if(key=="ov_zhuitingvs"){ownerSkill="ov_zhuitingvs";rawSkills={"","ov_zhuiting"};names={};maxCosts=1;response=true;}
 if(key=="@@ov_qingkou"){ownerSkill="ov_qingkou";rawSkills={"","ov_qingkou"};names={"duel"};maxCosts=0;named=true;}
 if(key=="ov_luanlve"){ownerSkill="ov_luanlve";rawSkills={"","ov_luanlve"};names={"snatch"};maxCosts=256;play=true;}
 if(key=="ov_jichou"){ownerSkill="ov_jichou";rawSkills={"","ov_jichou"};names={};maxCosts=0;play=true;}
 if(key=="ov_jilun"){ownerSkill="ov_jilun";rawSkills={"","_ov_jilun"};names={};maxCosts=0;named=true;}
 if(key=="@@ov_fenwu"){ownerSkill="ov_fenwu";rawSkills={"","ov_fenwu"};names={"slash"};maxCosts=0;named=true;}
 if(key=="ov_jiange"){ownerSkill="ov_jiange";rawSkills={"","ov_jiange"};names={"slash"};maxCosts=1;play=true;response=true;}
 if(key=="ov_chaofeng"){ownerSkill="ov_chaofeng";rawSkills={"","ov_chaofeng"};names={};maxCosts=1;play=true;response=true;}
 if(key=="ov_lihuo"){ownerSkill="ov_lihuo";rawSkills={"","mobilelihuo"};names={"fire_slash"};maxCosts=256;play=true;named=true;}
 if(key=="ov_chue"){ownerSkill="ov_chue";rawSkills={"","_ov_chue"};names={"slash"};maxCosts=0;named=true;}
 if(key=="@@ov_chue!"){ownerSkill="ov_chue";rawSkills={"","_ov_chue"};names={"slash"};maxCosts=0;named=true;}
 if(key=="ov_yanshiYL"){ownerSkill="ov_yanshiYL";rawSkills={"","ov_yanshiYL"};names={};maxCosts=1;play=true;}
 if(key=="ov_juexing"){ownerSkill="ov_juexing";rawSkills={"","ov_juexing"};names={"duel"};maxCosts=0;play=true;}
 if(key=="ov_beiding"){ownerSkill="ov_beiding";rawSkills={"","ov_beiding"};names={};maxCosts=0;named=true;}
 if(key=="@@ov_beiding"){ownerSkill="ov_beiding";rawSkills={"","_ov_beiding"};names={};maxCosts=0;named=true;}
 if(key=="ov_huanji"){ownerSkill="ov_huanji";rawSkills={"","ov_beiding"};names={};maxCosts=0;named=true;}
 if(key=="ov_qinghan"){ownerSkill="ov_qinghan";rawSkills={"","ov_qinghan"};names={};maxCosts=0;named=true;}
 if(key=="ov_qiji"){ownerSkill="ov_qiji";rawSkills={"","ov_liyuan"};names={"slash"};maxCosts=0;named=true;}
 if(key=="ov_chenxun"){ownerSkill="ov_chenxun";rawSkills={"","ov_chenxun"};names={"duel"};maxCosts=0;named=true;}
 if(key=="ov_liyuan"){ownerSkill="ov_liyuan";rawSkills={"","ov_liyuan"};names={"slash"};maxCosts=1;play=true;}
 if(key=="ov_cairu"){ownerSkill="ov_cairu";rawSkills={"","ov_cairu"};names={"fire_attack","iron_chain","ex_nihilo"};maxCosts=2;play=true;}
 if(key=="ov_xijun"){ownerSkill="ov_xijun";rawSkills={"","ov_xijun"};names={"slash","duel","Slash","Duel"};maxCosts=1;play=true;response=true;}
 if(key=="@@ov_xijun"){ownerSkill="ov_xijun";rawSkills={"","ov_xijun"};names={"slash","duel"};maxCosts=1;named=true;}
 if(key=="ov_bimeng"){ownerSkill="ov_bimeng";rawSkills={"","ov_bimeng"};names={};maxCosts=256;play=true;}
 if(key=="ov_qinyi"){ownerSkill="ov_qinyi";rawSkills={"","ov_qinyi"};names={};maxCosts=0;named=true;}
 if(ownerSkill.isEmpty()||(!p->hasSkill(ownerSkill)&&!p->hasLordSkill(ownerSkill))||!rawSkills.contains(raw)||costs.size()>maxCosts)return false;
 const bool playQuestion=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&p->getPhase()==Player::Play;
 const bool responseQuestion=(r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
 const bool namedQuestion=key.startsWith("@@") ? r.kind==AIRequest::UseCard&&r.pattern==key : r.choiceOptions.reason==key;
 if(!(play&&playQuestion)&&!(response&&responseQuestion)&&!(named&&namedQuestion))return false;
 if(names.isEmpty()){
  if(!r.worldView.strategyFacts.value("pattern_names").toArray().contains(name)&&!r.worldView.strategyFacts.value("pattern_class_names").toObject().contains(name))return false;
 }else if(!names.contains(name))return false;
 QSet<int> seen;for(const auto&v:costs){
  const int id=v.toInt(-1);if(!v.isDouble()||v.toDouble()!=id||id<0||seen.contains(id))return false;seen.insert(id);
  if(p->getRoom()->getCardOwner(id)!=p)return false;
  bool disclosed=false;for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)disclosed=true;
  for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
  if(p->getHandPile().contains(id))for(const auto&pile:r.worldView.self.piles)for(const auto&c:pile.cards)if(c.effectiveId==id)disclosed=true;
  if(!disclosed)return false;
 }
 return true;
}
void projectAIRound27ChoiceContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&data,QJsonObject&context,const std::function<QJsonObject(const Card*)>&publicCard){
 if(!valid(r,p)||p->getRoom()!=&room||!publicCard)return;
 QJsonObject row=context.value("round27_choice").toObject(),tags,properties;
 const QString reason=r.choiceOptions.reason;
 const auto roster=[&](const Player*v){return v&&room.findPlayerByObjectName(v->objectName(),true)==v;};
 const auto addPlayer=[&](const QString&key){auto*v=p->getTag(key).value<ServerPlayer*>();if(!v||roster(v))tags.insert(key,v?QJsonValue(v->objectName()):QJsonValue(false));};
 const auto addUse=[&](const QString&key){
  const QVariant value=p->getTag(key);if(!value.isValid()){tags.insert(key,false);return;}if(!value.canConvert<CardUseStruct>())return;
  const auto use=value.value<CardUseStruct>();if(!use.card||!roster(use.from))return;QJsonArray tos;for(auto*to:use.to){if(!roster(to))return;tos.append(to->objectName());}
  tags.insert(key,QJsonObject{{"from",use.from->objectName()},{"to",tos},{"card",publicCard(use.card)}});
 };
 const auto addIds=[&](const QString&key){QJsonArray ids;for(int id:p->getTag(key).value<QList<int>>()){
  bool disclosed=false;for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)disclosed=true;for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
  for(const auto&pile:r.worldView.self.piles)for(const auto&c:pile.cards)if(c.effectiveId==id)disclosed=true;
  if(!disclosed&&!readAIStrategyPublicDisclosure(room,p,id).value("known").toBool())return;ids.append(id);
 }tags.insert(key,ids);};
 if(r.kind==AIRequest::SkillInvoke&&reason=="ov_shepan")addUse("ov_shepan");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_xiongzheng")addPlayer("ov_xiongzheng");
 if(r.kind==AIRequest::SkillInvoke&&reason=="ov_xiafeng")tags.insert("ov_baonieNum",p->getTag("ov_baonieNum").toInt());
 if(r.kind==AIRequest::Discard&&reason=="ov_chongwang")addPlayer("ov_chongwang");
 if(r.kind==AIRequest::SkillInvoke&&reason=="ov_jiekuang")addUse("ov_jiekuang");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_fenwu")tags.insert("ov_fenwu",p->getTag("ov_fenwu").toString());
 if(r.kind==AIRequest::Discard&&reason=="wangxing")tags.insert("wangxing_ai",p->getTag("wangxing_ai").toString());
 if(r.kind==AIRequest::PlayerChosen&&reason=="ov_lvren")addUse("ov_lvren");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_xingluan!")addIds("ov_xingluanForAI");
 if(r.kind==AIRequest::PlayerChosen&&reason=="ov_yizhu")addUse("ov_yizhuData");
 if(r.kind==AIRequest::Discard&&reason=="ov_suizheng")addPlayer("ov_suizheng");
 if(r.kind==AIRequest::Choice&&reason=="ov_gongge")addUse("ov_gongge");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_lijian")addIds("ov_lijianForAI");
 if(r.kind==AIRequest::RespondCard&&r.prompt.section(':',0,0)=="ov_yanshiYLuse")properties.insert("ov_yanshiYLuse",p->property("ov_yanshiYLuse").toString());
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_beiding")properties.insert("ov_beidingUse",p->property("ov_beidingUse").toString());
 if(r.choiceOptions.question=="askForAG"&&reason=="ov_guimou")addPlayer("ov_zongquanTo");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_fuzhu")addIds("ov_fuzhuForAI");
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@ov_qianxiong")addIds("ov_qianxiongForAI");
 if(r.kind==AIRequest::PlayerChosen&&reason=="ov_zenhui")addUse("ov_zenhuiData");
 if(r.kind==AIRequest::PlayerChosen&&reason=="ov_qirang")addUse("ov_qirangData");
 if(r.kind==AIRequest::Activate&&p->hasSkill("ov_yanshiYL")){
  QJsonArray values;for(const auto&name:Sanguosha->getZhinangCards())values.append(name);row.insert("zhinang_cards",values);
 }

 // Native source call sites pass these values to this prompted owner.
 if(r.kind==AIRequest::SkillInvoke&&QStringList{"ov_fupan","ov_enyuan","ov_yuzhang","ov_xinshou","ov_qiji2"}.contains(reason)
  &&data.metaType().id()==QMetaType::QString)row.insert("data",QJsonObject{{"kind","String"},{"value",data.toString()}});
 // OverseasVersion.lua11727 fills AG to the actor before this prompt; the AI
 // uses only length. Export the count, never the otherwise unnecessary IDs.
 if(r.kind==AIRequest::SkillInvoke&&reason=="ov_qiaosi"&&data.canConvert<QList<int>>())
  row.insert("data",QJsonObject{{"kind","IntListCount"},{"count",data.value<QList<int>>().size()}});
 // QObject child order is the source Player::getSiblings order; Room's
 // current-player-rotated order is deliberately not substituted.
 if(r.kind==AIRequest::Activate||reason.startsWith("ov_")||r.pattern.startsWith("@@ov_")||r.prompt.startsWith("ov_")){
  QJsonArray siblings;for(const auto*other:p->getSiblings())if(roster(other))siblings.append(other->objectName());row.insert("sibling_names",siblings);
 }
 if(r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForCard"
  &&QStringList{"ov_huzhong0","ov_ronggui0"}.contains(r.prompt.section(':',0,0))&&data.canConvert<CardUseStruct>()){
  const auto use=data.value<CardUseStruct>();bool safe=use.card&&roster(use.from);for(auto*to:use.to)safe=safe&&roster(to);
  if(safe){QJsonArray names;for(auto*to:room.getCardTargets(p,use.card,use.to))names.append(to->objectName());row.insert("card_target_names",names);}
 }
 if(!tags.isEmpty())row.insert("owner_tags",tags);if(!properties.isEmpty())row.insert("owner_properties",properties);
 if(!row.isEmpty())context.insert("round27_choice",row);
}
