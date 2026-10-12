#include "ai-strategy/overseas.h"

// ---- overseas-choice-choice-2 ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/native-registry.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "ai-strategy/owner.h"
#include <QStringList>
#include <QHash>
#include <QPointer>
#include "card-lifetime-manager.h"
namespace {
bool current(const AIRequest &r, Room &room, const ServerPlayer *p) {
 return p && p->getRoom()==&room && p->isAlive() && r.viewerObjectName==p->objectName()
  && r.worldView.self.objectName==p->objectName()
  && aiRangeRequestCurrent(room,r,p);
}
bool disclosed(const AIRequest &r, Room &room, ServerPlayer *p, int id) {
 if(id<0)return false;
 for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
 for(const auto &c:r.worldView.self.equips)if(c.effectiveId==id)return true;
 for(const auto &pile:r.worldView.self.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 return readAIStrategyPublicDisclosure(room,p,id).value("known")==true;
}
}
bool aiNiwoResponseQuestion(const AIRequest &r, Room &room, const ServerPlayer *p) {
 return current(r,room,p) && room.getCurrent()==p && ([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ov_niwo"); }())
  && p->getPhase()==Player::Play && p->getHandcardNum()>0
  && r.kind==AIRequest::UseCard && r.pattern=="@@ov_niwo" && r.prompt=="ov_niwo0"
  && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE && r.handlingMethod==Card::MethodUse;
}
bool aiNiwoShelieChoicePlanningQuestion(const AIRequest &r, Room &room, const ServerPlayer *p) {
 if(aiNiwoResponseQuestion(r,room,p))return true;
 // Fixed ov_shelie asks before replacing Finish -> NotActive, after its mark
 // increments. The consumed suit tag has already been removed; do not read it.
 return current(r,room,p) && room.getCurrent()==p && ([&] {
        std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ov_shelie");
    }())
  && p->getPhase()==Player::Finish && p->getMark("ov_shelieUse")>0
  && r.kind==AIRequest::Choice && r.choiceOptions.question=="askForChoice"
  && r.choiceOptions.reason=="ov_shelie"
  && r.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
}
QJsonObject aiNiwoShelieChoicePlanningContext(const AIRequest &r, Room &room, const ServerPlayer *p) {
 if(!aiNiwoShelieChoicePlanningQuestion(r,room,p))return{};
 const QString source=r.kind==AIRequest::UseCard?"OverseasVersion.ov_niwo":"OverseasVersion.ov_shelie";
 return{{"known",true},{"readonly",true},{"source",source},{"viewer",r.viewerObjectName},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"kind","activate"},
  {"reason",int(CardUseStruct::CARD_USE_REASON_PLAY)},{"pattern",""},{"handling_method",int(Card::MethodUse)}};
}
std::unique_ptr<Card> createAILihuoHypothesis(const AIRequest &r, Room &room, ServerPlayer *p, const QVariant &data) {
 if(!current(r,room,p) || r.kind!=AIRequest::SkillInvoke || r.choiceOptions.question!="askForSkillInvoke"
  || r.choiceOptions.reason!="ov_lihuo" || !p->hasSkill("ov_lihuo") || !data.canConvert<CardUseStruct>())return{};
 const auto use=data.value<CardUseStruct>();
 if(!use.card || use.from!=p || !use.card->isKindOf("Slash") || use.card->isKindOf("NatureSlash"))return{};
 for(const auto *target:use.to)if(!target || !room.getAllPlayers(true).contains(target))return{};
 const QList<int> costs=use.card->isVirtualCard()?use.card->getSubcards():QList<int>{use.card->getEffectiveId()};
 for(int id:costs)if(!disclosed(r,room,p,id))return{};
 std::unique_ptr<Card> result(Sanguosha->cloneCard("fire_slash",Card::NoSuit,0));
 if(!result)return{};
 result->setSkillName("mobilelihuo");result->addSubcards(costs);
 return result;
}

namespace {
// The generic borrowed-card store deliberately accepts only physical cards.
// This source-only capture is reached after lihQuestion has verified the actual
// owner inquiry and disclosed ordered materials. It never exposes the original
// graph or transfers ownership: the recipe reads only this exact live Card's
// class/face/material scalars, all rechecked before each reached construction.
AIReadonlyCardLease observeLihuoIncoming(const Card *card) {
 if(!card)return{};
 if(!card->isVirtualCard())return AIOwnerReadonlyCardStore::observe(card);
 auto &manager=globalCardLifetimeManager();
 auto token=manager.observeCard(const_cast<Card *>(card));
 if(!token||!token->live)return{};
 auto pin=std::make_shared<CardLifetimeLease>(manager,token);
 if(!pin->isValid())return{};
 const QPointer<Card> watched(const_cast<Card *>(card));
 return [watched,token,pin]() -> const Card * {
  if(watched.isNull()||!token->live||!pin->isValid())return nullptr;
  return watched.data();
 };
}
struct LihuoInput {
 QPointer<ServerPlayer> viewer;
 quint64 decision=0,revision=0;
 AIReadonlyCardLease source;
 QString name,klass;int suit=0,number=0;
 QList<int> costs;QStringList targets;
};
class LihuoInputs final:public QObject {
public:explicit LihuoInputs(Room &room):QObject(&room){}
 QHash<QString,LihuoInput> values;
};
LihuoInputs *lihInputs(Room &room) {
 for(auto *child:room.children())if(auto *found=dynamic_cast<LihuoInputs *>(child))return found;
 return new LihuoInputs(room);
}
bool lihQuestion(const AIRequest &r,Room &room,ServerPlayer *p,const CardUseStruct &u) {
 if(!current(r,room,p)||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.question!="askForSkillInvoke"
  ||r.choiceOptions.reason!="ov_lihuo"||!p->hasSkill("ov_lihuo")||u.from!=p||!u.card
  ||!u.card->isKindOf("Slash")||u.card->isKindOf("NatureSlash"))return false;
 for(auto *target:u.to)if(!target||!room.getAllPlayers(true).contains(target))return false;
 const auto costs=u.card->isVirtualCard()?u.card->getSubcards():QList<int>{u.card->getEffectiveId()};
 for(int id:costs)if(!disclosed(r,room,p,id))return false;
 return true;
}
}
void projectAILihuoQuestion(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &data) {
 if(!p || r.kind!=AIRequest::SkillInvoke || r.choiceOptions.reason!="ov_lihuo")return;
 QJsonObject row{{"known",false},{"source_key","ov_lihuo_hypothesis"}};
 auto *state=lihInputs(room);state->values.remove(p->objectName());
 if(data.canConvert<CardUseStruct>()) {
  const auto u=data.value<CardUseStruct>();
  if(lihQuestion(r,room,p,u)) {
   LihuoInput input;input.viewer=p;input.decision=r.decisionId;input.revision=r.stateRevision;
   input.source=observeLihuoIncoming(u.card);
   input.name=u.card->objectName();input.klass=u.card->getClassName();input.suit=int(u.card->getSuit());input.number=u.card->getNumber();
   input.costs=u.card->isVirtualCard()?u.card->getSubcards():QList<int>{u.card->getEffectiveId()};
   for(auto *target:u.to)input.targets<<target->objectName();
   if(input.source&&input.source()){state->values.insert(p->objectName(),input);row.insert("known",true);}
  }
 }
 r.choiceOptions.context.insert("overseas_lihuo_hypothesis",row);
}
std::unique_ptr<Card> createAICapturedLihuoHypothesis(const AIRequest &r,Room &room,ServerPlayer *p,CardUseStruct *out,QString *error) {
 auto fail=[&](const char *message)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(message);return{};};
 if(!p||!out||!current(r,room,p))return fail("Lihuo current owner inquiry unavailable");
 const auto input=lihInputs(room)->values.value(p->objectName());
 if(input.viewer!=p||input.decision!=r.decisionId||input.revision!=r.stateRevision||!input.source)
  return fail("Lihuo incoming source capture is stale or absent");
 const Card *source=input.source();
 if(!source||source->objectName()!=input.name||source->getClassName()!=input.klass
  ||int(source->getSuit())!=input.suit||source->getNumber()!=input.number)
  return fail("Lihuo incoming source identity or face changed");
 const auto costs=source->isVirtualCard()?source->getSubcards():QList<int>{source->getEffectiveId()};
 if(costs!=input.costs)return fail("Lihuo source ordered materials changed");
 CardUseStruct u;u.card=source;u.from=p;
 for(const auto &name:input.targets){auto *target=room.findPlayerByObjectName(name,true);if(!target)return fail("Lihuo original target left roster");u.to<<target;}
 if(!lihQuestion(r,room,p,u))return fail("Lihuo source question or disclosure no longer valid");
 auto card=createAILihuoHypothesis(r,room,p,QVariant::fromValue(u));
 if(!card)return fail("Lihuo exact hypothetical native clone unavailable");
 u.card=card.get();*out=u;if(error)error->clear();return card;
}

void clearAILihuoChoiceState(Room &room) {
 for(auto *child:room.children())if(auto *state=dynamic_cast<LihuoInputs *>(child))state->values.clear();
}

// ---- overseas-choice-choice ----
#include "standard.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool valid(const AIRequest &r,const ServerPlayer *p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),r,p);}
}
bool aiXiafengChoicePlanningQuestion(const AIRequest &r,Room &room,const ServerPlayer *p){
 return valid(r,p)&&p->getRoom()==&room&&r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="ov_xiafeng"
  &&p->isAlive()&&p->hasSkill("ov_xiafeng")&&p->getPhase()==Player::Play&&p->getTag("ov_baonieNum").toInt()>0;
}
QJsonObject aiXiafengChoicePlanningContext(const AIRequest &r,Room &room,const ServerPlayer *p){
 if(!aiXiafengChoicePlanningQuestion(r,room,p))return{};
 return{{"known",true},{"readonly",true},{"source","OverseasVersion.ov_xiafeng"},{"viewer",r.viewerObjectName},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"kind","activate"},
  {"reason",int(CardUseStruct::CARD_USE_REASON_PLAY)},{"pattern",""},{"handling_method",int(Card::MethodUse)}};
}

int aiOverseasChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
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

bool aiOverseasChoiceConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
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
void projectAIOverseasChoiceContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&data,QJsonObject&context,const std::function<QJsonObject(const Card*)>&publicCard){
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
 if(r.kind==AIRequest::Activate&&([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,p,gameLua));
        return p->hasSkill("ov_yanshiYL"); }())){
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

// ---- overseas-registry-prefix ----
#include "ai-strategy/source-slots.h"
#include "ai-strategy/lua-seal.h"
#include "lua-runtime.h"
#include <QRegularExpression>
#include <cstring>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
bool overseasRegistryPrefixCurrent(Room&r,ServerPlayer*p,const AIRequest&q){return p&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
 &&r.roomRuntime()->lua().isCurrentThreadOwner()&&r.roomRuntime()->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.decisionId
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRangeRequestCurrent(r,q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool sourceScope(ServerPlayer*p,const AIRequest&q,const QString&reg,const QString&key){
 const bool play=q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 const auto reason=q.choiceOptions.reason;
 if((reg=="ai_use_priority"&&key=="ZunweiCard")||(reg=="ai_skill_choice"&&key=="zunwei")){
  // Ownership auth must not introduce a correction/effect call between the
  // source's adjacent priority and choice assignments.
  bool owns=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="zunwei"){owns=true;break;}return play&&owns;
 }
 if(reg=="ai_skill_playerchosen"&&key=="ov_yingjia")return q.kind==AIRequest::RespondCard&&q.prompt.startsWith("ov_yingjia0:");
 if(reg=="ai_skill_invoke"&&key=="ov_beini")return play&&p->hasSkill("ov_beini");
 if(reg!="ai_skill_choice")return false;
 if(key=="ov_xiafeng")return q.kind==AIRequest::SkillInvoke&&reason==key;
 if(key=="wangxing")return q.kind==AIRequest::SkillInvoke&&reason=="ov_xiawei";
 if(key=="ov_zenhui"||key=="ov_yizhu")return q.kind==AIRequest::PlayerChosen&&reason==key;
 if(key=="shifa")return (q.kind==AIRequest::RespondCard&&q.prompt.startsWith("ov_fengji0:"))
  ||(play&&(p->hasSkill("ov_sfzhouhu")||p->hasSkill("ov_sffengqi")||p->hasSkill("ov_xingzhui")));
 return false;
}
struct Write{Room*r;ServerPlayer*p;const AIRequest*q;QByteArray registry,key,text;QPointer<ServerPlayer>target;
 enum Kind{Nil,Boolean,Text,Player,Number}kind=Nil;bool boolean=false,reached=false;
 AIUsePriorityOperation*priority=nullptr;
 lua_State*L=nullptr;quint64 generation=0;const void*globals=nullptr,*space=nullptr,*table=nullptr;};
bool roots(lua_State*L,const Write&w){Stack st(L);AILuaInspect::globals(L);if(lua_topointer(L,-1)!=w.globals)return false;
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=w.space)return false;
 AILuaInspect::field(L,-1,w.registry.constData());auto*t=AILuaInspect::table(L,-1);return t&&!t->metatable&&lua_topointer(L,-1)==w.table;}
bool live(const Write&w){auto*rt=w.r->roomRuntime();return rt&&!rt->isClosing()&&rt->lua().rawState()==w.L&&rt->lua().generation()==w.generation&&overseasRegistryPrefixCurrent(*w.r,w.p,*w.q);}
int assign(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"named registry prefix stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));if(!w||!live(*w)||!roots(L,*w)||lua_topointer(L,1)!=w->table)return luaL_error(L,"named registry prefix context changed");
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));
 if(w->kind==Write::Nil)lua_pushnil(L);
 else if(w->kind==Write::Boolean)lua_pushboolean(L,w->boolean);
 else if(w->kind==Write::Number)lua_pushnumber(L,10);
 else if(w->kind==Write::Text)lua_pushlstring(L,w->text.constData(),size_t(w->text.size()));
 else if(!w->target||!pushAISlotPlayer(L,w->target))return luaL_error(L,"named registry prefix Player binding unavailable");
 if(!live(*w)||!roots(L,*w)||(w->kind==Write::Player&&(!w->target||w->target->getRoom()!=w->r)))return luaL_error(L,"named registry prefix changed during allocation");
 if(w->priority&&w->kind==Write::Number)w->priority->beforeWrite();
 lua_settable(L,1);w->reached=true;
 if(w->priority&&w->kind==Write::Number)w->priority->afterWrite();return 0;
}
}
QJsonObject writeAIOverseasPrefix(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto fail=[&](const char*why){return QJsonObject{{"known",false},{"error",QString::fromLatin1(why)}};};
 if(input.size()!=4||input.value("op")!="source_prefix"||!input.value("registry").isString()||!input.value("key").isString()||!input.value("value").isObject())return fail("named registry source envelope malformed");
 const QString reg=input.value("registry").toString(),key=input.value("key").toString();
 if(!overseasRegistryPrefixCurrent(r,p,q)||!sourceScope(p,q,reg,key)||!overseasRegistryPrefixCurrent(r,p,q))return fail("named registry source question unavailable");
 Write w;w.r=&r;w.p=p;w.q=&q;w.registry=reg.toUtf8();w.key=key.toUtf8();const auto v=input.value("value").toObject();const auto kind=v.value("kind").toString();
 if(reg=="ai_use_priority"){
  if(key!="ZunweiCard"||kind!="scalar"||v.size()!=2||!v.value("value").isDouble()||v.value("value").toDouble()!=10)return fail("Zunwei source priority must be exact ten");w.kind=Write::Number;
 }else if(reg=="ai_skill_playerchosen"){
  if(kind=="nil"&&v.size()==1)w.kind=Write::Nil;
  else if(kind=="player"&&v.size()==2&&v.value("name").isString()){
   const QString name=v.value("name").toString();bool publicName=q.worldView.self.objectName==name;
   for(const auto&player:q.worldView.players)if(player.objectName==name)publicName=true;
   if(!publicName)return fail("named registry target is outside public roster");
   w.target=r.findPlayerByObjectName(name,true);if(!w.target||w.target->getRoom()!=&r)return fail("named registry target is no longer in room");w.kind=Write::Player;
  }else return fail("Yingjia source value must be exact Player or nil");
 }else if(reg=="ai_skill_invoke"){
  if(kind!="scalar"||v.size()!=2||!v.value("value").isBool())return fail("Beini source value must be boolean");w.kind=Write::Boolean;w.boolean=v.value("value").toBool();
 }else{
  if(kind!="scalar"||v.size()!=2||!v.value("value").isString())return fail("named source choice value must be string");const QString value=v.value("value").toString();
  bool valid=false;
  if(key=="zunwei")valid=value=="recover"||value=="draw"||value=="equip";
  else if(key=="ov_zenhui")valid=value=="ov_zenhui1"||value=="ov_zenhui2";
  else if(key=="ov_yizhu")valid=value=="ov_yizhu2"||value=="ov_yizhu3";
  else if(key=="ov_xiafeng")valid=QRegularExpression("^[0-3](?:\\.0)?$").match(value).hasMatch();
  else if(key=="wangxing")valid=QRegularExpression("^[1-4](?:\\.0)?$").match(value).hasMatch();
  else if(key=="shifa")valid=value.size()<=32&&QRegularExpression("^shifa-?[0-9]+(?:\\.0)?$").match(value).hasMatch();
  if(!valid)return fail("named source choice string is outside fixed writer range");w.kind=Write::Text;w.text=value.toUtf8();
 }
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,512))return fail("named registry game VM unavailable");Stack st(L);
 w.L=L;w.generation=rt->lua().generation();AILuaInspect::globals(L);w.globals=lua_topointer(L,-1);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("named source namespace unavailable");w.space=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,w.registry.constData());auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return fail("named source registry is not plain");w.table=lua_topointer(L,-1);const int table=lua_absindex(L,-1);
 AIUsePriorityOperation priority(r,p,q,L,assign,&w,w.kind==Write::Number?AIUsePriorityOperation::LiteralTen:AIUsePriorityOperation::Preserve,key);
 w.priority=&priority;
 lua_pushcfunction(L,assign);lua_pushvalue(L,table);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,2,0,0);
 // Actual assignment is never rolled back. A subsequent reentry/root change
 // denies completion; neither equality nor an old cached value restores it.
 if(status!=LUA_OK||!w.reached||!live(w)||!roots(L,w))return fail("named registry source prefix failed or was superseded");
 return{{"known",true},{"written",true}};
}

// ---- guo-chuming ----
#include "lua.hpp"
#include <QScopeGuard>
#include <QObject>
#include <cmath>
#include <limits>

namespace {
class CapturedRegistry final : public QObject {
public:
    CapturedRegistry(Room &room, const QJsonObject &value) : QObject(&room), value(value)
    { setObjectName("isolated-round24-choice-registry"); }
    const QJsonObject value;
};
const CapturedRegistry *captured(const Room &room)
{
    for (QObject *child : room.children()) if (auto *row = dynamic_cast<CapturedRegistry *>(child)) return row;
    return nullptr;
}
bool rawField(lua_State *L, const char *key)
{
    if (!lua_istable(L, -1)) { lua_pushnil(L); lua_remove(L, -2); return false; }
    const int table = lua_absindex(L, -1);
    lua_pushstring(L,key); lua_rawget(L,table);
    bool known = true;
    if (lua_isnil(L,-1) && lua_getmetatable(L,table)) {
        lua_pushliteral(L,"__index");lua_rawget(L,-2);known=lua_isnil(L,-1);lua_pop(L,2);
    }
    lua_remove(L,table);return known;
}
bool guoChumingCurrent(const AIRequest &request, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() && request.viewerObjectName==viewer->objectName()
        && request.stateRevision==viewer->getRoom()->roomRuntime()->stateRevision();
}
bool idsFromJson(const QJsonValue &value, QList<int> &ids)
{
    if (!value.isArray() || value.toArray().size()>128) return false;
    for(const auto &v:value.toArray()) {
        const double n=v.toDouble(-1);
        if (!v.isDouble() || !std::isfinite(n) || n<0 || n!=std::floor(n)
            || n>std::numeric_limits<int>::max() || ids.contains(int(n))) return false;
        ids<<int(n);
    }
    return true;
}
bool idsFromProperty(const ServerPlayer *viewer, QList<int> &ids)
{
    const QString text=viewer->property("chumingSubcard").toString();
    if (text.isEmpty()) return true; // utilities.lua split('') is empty.
    for(const QString &part:text.split('+')) {
        bool ok=false;int id=part.toInt(&ok);
        if (!ok || id<0 || ids.contains(id)) return false;
        ids<<id;
    }
    return ids.size()<=128;
}
bool exactFace(const AICardView &known, const Card *live)
{
    return live && known.effectiveId==live->getEffectiveId() && known.objectName==live->objectName()
        && known.className==live->getClassName() && known.suit==int(live->getSuit()) && known.number==live->getNumber();
}
bool lawfulFace(const AIRequest &request, const ServerPlayer *viewer, int id)
{
    Room *room=viewer->getRoom();const Card *card=room->getCard(id);if(!card)return false;
    const auto place=room->getCardPlace(id);
    if(place==Player::DiscardPile)return true;
    // Table is also the native default mapping; only a genuine public witness
    // or current disclosed descriptor can authorize its face.
    if(place==Player::PlaceTable && readAIStrategyPublicDisclosure(*room,
            const_cast<ServerPlayer *>(viewer),id).value("known").toBool())return true;
    for(const auto &row:request.choiceOptions.cards)if(exactFace(row,card))return true;
    for(const auto &row:request.worldView.discardPile)if(exactFace(row,card))return true;
    for(const auto &row:request.worldView.handCards)if(exactFace(row,card))return true;
    for(const auto &row:request.worldView.self.equips)if(exactFace(row,card))return true;
    for(const auto &pile:request.worldView.self.piles)for(const auto &row:pile.cards)if(exactFace(row,card))return true;
    for(const auto &p:request.worldView.players)for(const auto &row:p.knownCards)if(exactFace(row,card))return true;
    return false;
}
bool chumingSource(const AIRequest &request,const ServerPlayer *viewer,
    const std::function<bool()> &query = {})
{
    if(!guoChumingCurrent(request,viewer))return false;
    for(const auto &instance:viewer->getSkillInstances()) {
        if(instance.key().skillName!="chuming")continue;
        const SkillInstanceRef ref(viewer->objectName(),instance.key());
        if(viewer->getRoom()->isAcceptedViewAsEffect(ref))return true;
    }
    // Direct owner source is legal as a readonly predicate; it does not create
    // the accepted effect state required for payment below.
    return query ? query() : viewer->hasSkill("chuming");
}
}

void captureAIGuoChoiceRegistry(Room &room)
{
    if(captured(room))return;
    LuaRuntime &runtime=room.roomRuntime()->lua();LuaRuntime::Binding binding(runtime,false);
    lua_State *L=runtime.state();QJsonObject row{{"known",false}};
    if(!L){new CapturedRegistry(room,row);return;}
    const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    lua_pushglobaltable(L);bool known=rawField(L,"sgs");
    known=rawField(L,"ai_skill_cardask")&&known;known=rawField(L,"@mobileniluan")&&known;
    const bool present=!lua_isnil(L,-1);row.insert("known",known);row.insert("present",present);
    bool matches=false;
    if(lua_isfunction(L,-1)) {
        lua_Debug info{};lua_pushvalue(L,-1);
        if(lua_getinfo(L,">S",&info)) {
            const QString source=QString::fromUtf8(info.source?info.source:"");
            row.insert("source",source);row.insert("line",info.linedefined);
            matches=source.endsWith("/tenyear_hc-ai.lua") && info.linedefined==4738
                && QString::fromLatin1(info.what?info.what:"")=="Lua";
        }
    }
    row.insert("source_matches",known && present && matches);
    new CapturedRegistry(room,row);
}
void projectAIGuoChoiceRegistry(AIWorldView &world,Room &room)
{
    auto guo=world.strategyFacts.value("guo").toObject();
    guo.insert("mobileniluan_callback",captured(room)?captured(room)->value:QJsonObject{{"known",false}});
    world.strategyFacts.insert("guo",guo);
}
void projectAIGuoChoiceContext(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    const auto nativeHasChuming = [&] { return ([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("chuming"); }()); };
    projectAIGuoChoiceRegistry(request.worldView,room);
    if(!chumingSource(request,viewer,nativeHasChuming) || !((request.kind==AIRequest::Choice && request.choiceOptions.reason=="chuming")
        ||(request.kind==AIRequest::UseCard && request.pattern=="@@chuming!")))return;
    auto props=request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    props.insert("chumingSubcard",viewer->property("chumingSubcard").toString());
    request.worldView.strategyFacts.insert("owner_skill_properties",props);
}
bool aiGuoChoiceConstruction(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
{
    if(!guoChumingCurrent(request,viewer) || spec.size()!=6)return false;
    QList<int> ids;if(!idsFromJson(spec.value("subcards"),ids))return false;
    const QString key=spec.value("source_key").toString();
    if(key=="jinpozhu") {
        if(request.kind!=AIRequest::Activate || request.reason!=CardUseStruct::CARD_USE_REASON_PLAY
            || !viewer->hasSkill(key) || spec.value("skill")!=key || spec.value("name")!="chuqibuyi"
            || ids.size()!=1 || (!viewer->handCards().contains(ids[0]) && !viewer->getHandPile().contains(ids[0]))
            || viewer->getRoom()->getCardOwner(ids[0])!=viewer || !lawfulFace(request,viewer,ids[0]))return false;
        const Card *material=viewer->getRoom()->getCard(ids[0]);
        return spec.value("suit")==int(material->getSuit()) && spec.value("number")==material->getNumber();
    }
    if(key!="chuming" || !chumingSource(request,viewer) || request.kind!=AIRequest::Choice
        || request.choiceOptions.reason!="chuming" || spec.value("skill")!="_chuming"
        || spec.value("suit")!=int(Card::NoSuit) || spec.value("number")!=0)return false;
    const QString name=spec.value("name").toString();
    if((name!="collateral" && name!="dismantlement") || !request.choiceOptions.choices.contains(name))return false;
    QList<int> property;if(!idsFromProperty(viewer,property) || property!=ids)return false;
    for(int id:ids)if(!lawfulFace(request,viewer,id))return false;
    return true;
}
bool aiChumingPublicMaterials(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
{
    return spec.value("source_key")=="chuming" && aiGuoChoiceConstruction(request,viewer,spec);
}
bool authorizeAIChumingPayment(const AIRequest &request,const ServerPlayer *viewer,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,const QJsonObject &spec,QList<int> *publicMaterials)
{
    if(!guoChumingCurrent(request,viewer) || request.kind!=AIRequest::UseCard || request.pattern!="@@chuming!"
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE || !activation.isValid() || !source.isValid()
        || activation.ownerObjectName!=viewer->objectName() || activation.key.skillName!="chuming"
        || !viewer->getRoom()->isAcceptedViewAsEffect(activation) || spec.value("skill")!="chuming")return false;
    bool issued=false;
    const auto scopes=request.conversionPolicy.value("scopes").toObject();
    for(auto it=scopes.begin();it!=scopes.end();++it) {
        const auto row=it.value().toObject();const auto identity=row.value("identity").toObject();
        if(row.value("skill_name")!="chuming" || row.value("pattern")!="@@chuming!"
            || row.value("reason").toInt()!=int(CardUseStruct::CARD_USE_REASON_RESPONSE_USE))continue;
        if(identity.value("activation_owner")==activation.ownerObjectName
            && identity.value("activation_skill")==activation.key.skillName
            && identity.value("activation_instance").toInt()==activation.key.instanceID
            && identity.value("source_owner")==source.ownerObjectName
            && identity.value("source_skill")==source.key.skillName
            && identity.value("source_instance").toInt()==source.key.instanceID) { issued=true;break; }
    }
    if(!issued)return false;
    const auto state=viewer->getSkillInstanceStateValue("chuming",activation.key.instanceID,"response").toMap();
    const QString name=state.value("name").toString();
    if((name!="collateral" && name!="dismantlement") || spec.value("name")!=name)return false;
    QList<int> proposed,expected;if(!idsFromJson(spec.value("subcards"),proposed))return false;
    for(const QVariant &v:state.value("ids").toList())expected<<v.toInt();
    if(expected.isEmpty() || expected!=proposed)return false;
    auto *target=viewer->getRoom()->findPlayerByObjectName(state.value("target").toString());
    if(!target || target->isDead())return false;
    for(int id:expected) {
        const Card *card=viewer->getRoom()->getCard(id);
        if(!card || viewer->getRoom()->getCardOwner(id) || card->hasFlag("using") || !lawfulFace(request,viewer,id))return false;
    }
    if(publicMaterials)*publicMaterials=expected;
    return true;
}
