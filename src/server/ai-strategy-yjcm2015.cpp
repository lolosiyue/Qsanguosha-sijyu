#include "ai-strategy-yjcm2015.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
namespace {
QJsonObject disclosedCard(const AICardView &c)
{
    // Reuse only the identity the world producer already disclosed. In
    // particular, do not reread a foreign live wrapper's flags or root chain.
    QJsonObject row{{"id",c.cardId},{"effective_id",c.effectiveId},
        {"name",c.objectName},{"class_name",c.className},
        {"name_length",c.nameLength},{"legacy_string_key",c.legacyStringKey},{"legacy_sort_key",c.legacySortKey},
        {"kind_of",QJsonArray::fromStringList(c.kindOfNames)},
        {"type_id",c.typeId},{"suit",c.suit},{"number",c.number},
        {"red",c.red},{"black",c.black},{"skill_name",c.skillName},
        {"virtual_card",false},{"subcards",QJsonArray()},
        {"damage_card",c.damageCard},{"target_fixed",c.targetFixed},
        {"will_throw",c.willThrow},{"can_recast",c.canRecast},
        {"handling_method",c.handlingMethod},{"known_flags",c.knownFlags},
        {"flags_complete",c.flagsComplete}};
    row.insert("skill_names_complete",c.skillNamesComplete);
    if(c.skillNamesComplete)row.insert("skill_names",QJsonArray::fromStringList(c.skillNames));
    if(c.flagsComplete)row.insert("flags_complete_scope","owner_non_visibility");
    if(c.weaponRange>=0)row.insert("weapon_range",c.weaponRange);
    return row;
}
}
void projectAIYjcm2015(AIWorldView &world, Room &room, const ServerPlayer *viewer,
                      const std::function<QJsonObject(const Card *)> &publicDecisionCard)
{
    if(!viewer || world.self.objectName!=viewer->objectName())return;
    const QVariant idTag=viewer->getTag("TaoxiId");
    bool idValid=false;const int id=idTag.toInt(&idValid);
    QJsonObject facts{{"taoxi_id",id},{"taoxi_id_present",idTag.isValid()},
        {"taoxi_here",viewer->getTag("TaoxiHere").toBool()},
        {"taoxi_use",false},{"taoxi_borrowed",false}};
    const QVariant useTag=viewer->getTag("taoxi_carduse");
    if(useTag.canConvert<CardUseStruct>()){
        const CardUseStruct use=useTag.value<CardUseStruct>();
        if(use.card){
            QJsonArray targets;for(const auto *p:use.to)if(p)targets.append(p->objectName());
            facts.insert("taoxi_use",QJsonObject{{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(false)},
                {"to",targets},{"card",publicDecisionCard(use.card)}});
        }
    }
    // Source TaoxiMove does not transfer the underlying card. showCard occurs
    // first, so lawful world disclosure + these live tags are the witness.
    const ServerPlayer *owner=idValid&&id>=0?room.getCardOwner(id):nullptr;
    if(viewer->hasFlag("TaoxiRecord")&&facts.value("taoxi_here").toBool()
        &&owner&&owner!=viewer&&room.getCardPlace(id)==Player::PlaceHand
        &&owner->handCards().contains(id)){
        for(const auto &player:world.players){
            if(player.objectName!=owner->objectName())continue;
            for(const auto &card:player.knownCards){
                if(card.effectiveId!=id||card.virtualCard)continue;
                facts.insert("taoxi_borrowed",QJsonObject{{"card_id",id},{"owner",owner->objectName()},
                    {"place","hand"},{"card",disclosedCard(card)}});
                break;
            }
            break;
        }
    }
    // Range rules can involve third-party lords (zhaofu/jinchengwu). Refuse
    // this exact matrix unless the whole participating public skill roster is
    // complete, rather than querying a hidden general's contribution.
    QList<AIPlayerView> rangePlayers=world.players;rangePlayers.prepend(world.self);
    bool rangeKnown=true;for(const auto &p:rangePlayers)if(!p.skillsComplete)rangeKnown=false;
    QJsonObject ranges;
    if(rangeKnown){
        for(const auto &fromView:rangePlayers){
            if(!fromView.alive)continue;
            const auto *from=room.findPlayerByObjectName(fromView.objectName,true);
            if(!from){rangeKnown=false;break;}
            QSet<int> offsets{0,1};
            for(const auto &equip:fromView.equips)if(equip.weaponRange>=1)offsets.insert(equip.weaponRange-1);
            QJsonObject targets;
            for(const auto &toView:rangePlayers){
                if(!toView.alive)continue;
                const auto *to=room.findPlayerByObjectName(toView.objectName,true);
                if(!to){rangeKnown=false;break;}
                QJsonObject values;
                for(int offset:offsets)values.insert(QString::number(offset),from->inMyAttackRange(to,offset));
                targets.insert(toView.objectName,values);
            }
            if(!rangeKnown)break;
            ranges.insert(fromView.objectName,targets);
        }
    }
    facts.insert("attack_range_known",rangeKnown);
    facts.insert("in_attack_range",rangeKnown?ranges:QJsonObject());
    world.strategyFacts.insert("yjcm2015",facts);
}
