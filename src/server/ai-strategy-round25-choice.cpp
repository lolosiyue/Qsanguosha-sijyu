#include "ai-strategy-round25-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include <QJsonArray>

namespace {
bool scope(const AIRequest &r,const ServerPlayer *p) {
 return p && p->getRoom() && r.viewerObjectName==p->objectName()
     && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
}
int aiRound25ChoicePrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
 if (!scope(r,p) || !p->hasSkill(skill)) return -1;
 if (r.kind==AIRequest::UseCard && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
  if (skill=="cangji" && prototype=="CangjiCard" && r.pattern=="@@cangji") return 1;
  if (skill=="dangqian" && prototype=="#dangqianCard" && r.pattern=="@@dangqian") return 0;
 }
 if (r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY) return -1;
 if ((skill=="xiechan" && prototype=="XiechanCard") || (skill=="mouzhu" && prototype=="MouzhuCard")
     || (skill=="zfduanhe" && prototype=="#zfduanhe") || (skill=="bu_s2_benxi" && prototype=="#bu_s2_benxi")
     || (skill=="kejieguizhashi" && prototype=="#kejieguizhashiCard")) return 0;
 if ((skill=="puji" && prototype=="PujiCard") || (skill=="luajuao" && prototype=="#luajuaocard")
     || (skill=="lyduji" && prototype=="#lyduji") || (skill=="lyxiance" && prototype=="#lyxiance")
     || (skill=="LuaFangxian" && prototype=="#LuaFangxianCard")) return 1;
 for (const QString &name : {QStringLiteral("keguishouye"),QStringLiteral("keguijiehuo"),
                            QStringLiteral("kejieguishouye"),QStringLiteral("kejieguijiehuo")})
  if (skill==name && prototype=="#"+name+"Card") return 1;
 if ((skill=="keguixianjiVS" && prototype=="#keguixianjiCard")
     || (skill=="kejieguixianjiVS" && prototype=="#kejieguixianjiCard")) return 1;
 return -1;
}

bool aiRound25ChoiceConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s) {
 if (!scope(r,p) || s.size()!=6 || !s.value("subcards").isArray()) return false;
 const QString key=s.value("source_key").toString(), skill=s.value("skill").toString();
 const auto costs=s.value("subcards").toArray();
 if (key=="krskitgjiamian" && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
     && p->hasSkill(key) && skill==key && s.value("name")==QJsonValue("slash") && costs.size()==1
     && costs[0].isDouble() && costs[0].toDouble()==costs[0].toInt(-1)) {
  const int id=costs[0].toInt(-1);Room *room=p->getRoom();const Card *c=id>=0?room->getCard(id):nullptr;
  return c && room->getCardOwner(id)==p && room->getCardPlace(id)==Player::PlaceHand && c->isKindOf("Jink")
      && s.value("suit")==QJsonValue(int(c->getSuit())) && s.value("number")==QJsonValue(c->getNumber());
 }
 if (key=="spwushen" && r.kind==AIRequest::PlayerChosen && r.choiceOptions.reason=="spwushen"
     && p->hasSkill("spwushen") && s.value("name")==QJsonValue("fire_slash")
     && s.value("suit")==QJsonValue(int(Card::Heart)) && s.value("number")==QJsonValue(0)
     && costs.isEmpty() && (skill.isEmpty() || skill=="spwushen")) return true;
 const bool niluanQuestion = (r.kind==AIRequest::UseCard && r.pattern=="@@niluan"
     && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
     || (r.kind==AIRequest::RespondCard && r.choiceOptions.question=="askForCard");
 if (key!="@niluan-slash" || !niluanQuestion
     || r.prompt.section(':',0,0)!="@niluan-slash" || !p->hasSkill("niluan")
     || s.value("name")!=QJsonValue("slash") || s.value("suit")!=QJsonValue(int(Card::SuitToBeDecided))
     || s.value("number")!=QJsonValue(-1) || (!skill.isEmpty() && skill!="niluan") || costs.size()>1) return false;
 if (costs.isEmpty()) return true;
 if (!costs[0].isDouble() || costs[0].toDouble()!=costs[0].toInt(-1)) return false;
 const int id=costs[0].toInt(-1);Room *room=p->getRoom();
 return id>=0 && room->getCardOwner(id)==p
     && (room->getCardPlace(id)==Player::PlaceHand || room->getCardPlace(id)==Player::PlaceEquip);
}

void projectAIRound25ChoiceContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &data,QJsonObject &context,
                                  const std::function<QJsonObject(const Card *)> &publicCard) {
 if (!scope(r,p) || p->getRoom()!=&room || !publicCard) return;
 const QString reason=r.choiceOptions.reason;
 auto row=context.value("round25_choice").toObject();
 const auto roster=[&](const Player *v){return v && room.findPlayerByObjectName(v->objectName(),true)==v;};
 if (r.kind==AIRequest::PlayerChosen && reason=="LuaGaobi" && p->hasSkill(reason))
  row.insert("slash_available",Slash::IsAvailable(p,nullptr));
 if (r.kind==AIRequest::SkillInvoke && reason=="keguiqinwang" && p->hasSkill(reason)
     && data.canConvert<ServerPlayer *>()) {
  const auto tag=room.getTag("CurrentDamageStruct");
  if (tag.canConvert<DamageStruct>()) {
   const auto d=tag.value<DamageStruct>();
   if (roster(d.to) && d.to==data.value<ServerPlayer *>() && (!d.from || roster(d.from))) {
    QJsonObject damage{{"to",d.to->objectName()},{"from",d.from?QJsonValue(d.from->objectName()):QJsonValue(false)},
      {"amount",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer}};
    if (d.card) damage.insert("card",publicCard(d.card));row.insert("current_damage",damage);
   }
  }
 }
 if (reason=="kejieguisheji" && p->hasSkill(reason)
     && (r.kind==AIRequest::SkillInvoke || r.choiceOptions.question=="askForPindian")) {
  const auto tag=room.getTag("CurrentUseStruct");
  if (tag.canConvert<CardUseStruct>()) {
   const auto use=tag.value<CardUseStruct>();bool valid=roster(use.from) && use.card && use.card->isKindOf("Slash");
   for (auto *to:use.to) valid=valid && roster(to);
   if (r.kind==AIRequest::SkillInvoke) valid=valid && data.canConvert<ServerPlayer *>() && use.to.contains(data.value<ServerPlayer *>());
   if (valid) {
    QJsonArray tos;for(auto *to:use.to)tos.append(to->objectName());
    row.insert("current_use",QJsonObject{{"from",use.from->objectName()},{"to",tos},{"card",publicCard(use.card)}});
    QJsonValue target(false);for(auto *to:room.getOtherPlayers(p))if(to->hasFlag("kejieguishejiPindianTarget")){target=to->objectName();break;}
    row.insert("sheji_pindian_target",target);
   }
  }
 }
 if (!row.isEmpty()) context.insert("round25_choice",row);
}
