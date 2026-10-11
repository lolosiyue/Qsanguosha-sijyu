#include "ai-strategy-round24-feidiy.h"
#include "ai-strategy-construction-query.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool owner(const AIRequest &r,const ServerPlayer *p) {
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
bool response(const AIRequest&r) {
    return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)
        && (r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
}
int aiRound24FeiDIYExtraTarget(const AIRequest&r,const ServerPlayer*p,const QJsonObject&spec) {
    const QString key=spec.value("source_key").toString();
    // Fixed ai/feiDIY-ai.lua311/350: both print feihujue, but each caller must
    // own its actual source. aiAuditedConstructionQuery checks all six fields.
    if ((key!="feihujue"&&key!="feilangduo")||!r.pattern.isEmpty()||!aiAuditedConstructionQuery(r,p,spec))return 0;
    return 99;
}
bool aiRound24FeiDIYSource(const AIRequest&r,const ServerPlayer*p,const Card*c,const QString&key,QString*error) {
    auto reject=[&](const char*why){if(error)*error=QString::fromLatin1(why);return false;};
    if(!c||c->getId()>=0||c->objectName()!="duel"||c->getSkillName(false)!="feihujue"
        ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0||!c->getSubcards().isEmpty())return reject("FeiDIY extra99 requires the original exact Duel clone");
    const QJsonObject spec{{"source_key",key},{"name","duel"},{"skill","feihujue"},
        {"suit",int(Card::NoSuit)},{"number",0},{"subcards",QJsonArray{}}};
    if(aiRound24FeiDIYExtraTarget(r,p,spec)!=99)return reject("FeiDIY extra99 source owner or current request differs");
    return true;
}
QJsonObject aiRound24FeiDIYTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,
    const QString&key,const QJsonObject&query) {
    auto reject=[](const QString&s){return QJsonObject{{"error",s},{"key","feiDIY.extra99"}};};
    QString error;if(!p||p->getRoom()!=&room||!aiRound24FeiDIYSource(r,p,c,key,&error))return reject(error);
    if(query.size()!=2||!query.value("prefix").isArray()||!query.value("target").isString())return reject("malformed FeiDIY target query");
    QList<const Player*>prefix;QSet<QString>seen;const auto names=query.value("prefix").toArray();
    if(names.size()>room.getAlivePlayers().size())return reject("FeiDIY prefix exceeds live roster");
    for(const auto&name:names){
        if(!name.isString()||seen.contains(name.toString()))return reject("FeiDIY prefix is not a distinct roster list");
        auto*target=room.findPlayerByObjectName(name.toString());if(!target||!target->isAlive())return reject("FeiDIY prefix player absent");
        seen.insert(name.toString());prefix<<target;
    }
    auto*target=room.findPlayerByObjectName(query.value("target").toString());if(!target||!target->isAlive())return reject("FeiDIY target absent");
    const auto revision=room.roomRuntime()->stateRevision();
    AITianyiQueryScope extra(p,c,99,false);const bool accepted=c->targetFilter(prefix,target,p);
    if(revision!=room.roomRuntime()->stateRevision())return reject("FeiDIY readonly target query changed state");
    return {{"accepted",accepted}};
}
int aiRound24FeiDIYPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
    if(!owner(r,p)||!response(r))return -1;
    if(skill=="feifenhu"&&prototype=="#feifenhuCard"&&r.pattern=="@@feifenhu"&&p->hasSkill(skill))return 0;
    // Targets, not the skill owner, receive this actual native response ask
    // (extensions/feiDIY.lua1305). Source presence is NOT payment authority;
    // the existing issued activation / selection / factory checks still apply.
    if(skill=="feifengpo"&&prototype=="#feifengpoCard"&&r.pattern=="@@feifengpo")return 1;
    return -1;
}
void projectAIRound24FeiDIYContext(AIRequest&r,Room&room,const ServerPlayer*p,
    const std::function<QJsonObject(const Card*)>&publicCard){
    if(!owner(r,p)||p->getRoom()!=&room)return;
    const auto reason=r.choiceOptions.reason;const auto roster=room.getAllPlayers(true);
    QJsonObject rows=r.choiceOptions.context.value("feiDIY").toObject();bool touched=false;
    if(r.kind==AIRequest::SkillInvoke&&reason=="feiqianxun"){
        // Exact owner's Collateral target tag; never another player's private tag.
        auto*victim=p->getTag("collateralVictim").value<ServerPlayer*>();
        rows.insert("collateral_victim",victim&&roster.contains(victim)?QJsonValue(victim->objectName()):QJsonValue(false));touched=true;
    }
    if(r.kind==AIRequest::Choice&&reason=="feizhaohu"){
        QJsonObject moves;QJsonArray marked;
        // Only actor identity is projected. The underlying move's hidden IDs,
        // from_places, reason and private card face never cross this boundary.
        // Writer feiDIY.lua311–319 records the public last-hand loss actor.
        for(auto*player:roster){if(player->getMark("feizhaohup")<=0)continue;
            marked.append(player->objectName());const QVariant tag=player->getTag("feizhaohuData");
            if(!tag.isValid()||tag.isNull()){moves.insert(player->objectName(),false);continue;}
            if(!tag.canConvert<CardsMoveOneTimeStruct>())continue;
            const auto move=tag.value<CardsMoveOneTimeStruct>();
            if(!move.from)moves.insert(player->objectName(),QJsonObject{{"from",false}});
            else if(roster.contains(qobject_cast<ServerPlayer*>(move.from)))
                moves.insert(player->objectName(),QJsonObject{{"from",move.from->objectName()}});
        }
        rows.insert("feizhaohu_marked",marked);rows.insert("feizhaohu_moves",moves);touched=true;
    }
    if(response(r)&&r.pattern=="@@feifengpo"){
        const QVariant tag=room.getTag("feifengpo");
        if(!tag.isValid()||tag.isNull())rows.insert("feifengpo_use",false);
        else if(tag.canConvert<CardUseStruct>()){
            const auto use=tag.value<CardUseStruct>();
            if(!use.card)rows.insert("feifengpo_use",false);
            // Writer is exclusively TargetSpecified, after the use is publicly
            // declared. Require the actual recipient and public source shape;
            // no arbitrary Room tag or foreign hidden card is an input.
            else if(use.from&&roster.contains(use.from)&&use.from->hasSkill("feifengpo")
                &&use.to.contains(const_cast<ServerPlayer*>(p))
                &&(use.card->isKindOf("Slash")||use.card->isKindOf("Duel"))&&publicCard){
                QJsonArray targets;bool valid=true;for(auto*to:use.to){if(!to||!roster.contains(to)){valid=false;break;}targets.append(to->objectName());}
                const auto face=publicCard(use.card);if(valid&&!face.isEmpty())rows.insert("feifengpo_use",QJsonObject{{"from",use.from->objectName()},{"to",targets},{"card",face}});
            }
        }touched=true;
    }
    if(touched){r.choiceOptions.context.insert("feiDIY",rows);r.worldView.decisionContext.insert("feiDIY",rows);}
}
