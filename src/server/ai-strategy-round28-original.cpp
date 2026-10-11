#include "ai-strategy-round28-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "lua.hpp"
#include <QJsonObject>
#include <memory>
void projectAIRound28ExtraWorld(AIWorldView &world,Room &room,ServerPlayer *viewer) {
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()||world.self.objectName!=viewer->objectName())return;
    QJsonObject globals;
    // Existence only, never execute the legacy policy/helper. Fixed temp-only
    // providers must not be invented when the runtime did not load them.
    {
        LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);
        lua_State *L=room.roomRuntime()->lua().state();
        for(const char *name:{"GetQueueMembers","IsBigKingdomPlayer"}) {
            lua_getglobal(L,name);globals.insert(QString::fromLatin1(name),!lua_isnil(L,-1));lua_pop(L,1);
        }
    }
    QJsonObject prototypes;
    // Finite literal Card_Parse source spellings. Existence is public engine
    // metadata, not authority: missing Lua prototypes stay missing and are never
    // aliased to modern native H* products. Every temporary clone is destroyed.
    for (const char *name : {
        "#bf_nos_tiaodu",
        "#bf_qice",
        "#bf_xiongsuan",
        "#bf_yongjin",
        "#gd_zezhu",
        "#guandu_shicai",
        "#guandu_yuanlue",
        "#heg_chengliu",
        "#heg_duanxie",
        "#heg_duwu",
        "#heg_fk_bushi",
        "#heg_guikuang",
        "#heg_huaiyi",
        "#heg_huyuan",
        "#heg_jianglue",
        "#heg_jiantong",
        "#heg_jiaping_Attach",
        "#heg_jinfa",
        "#heg_jinghe",
        "#heg_keshou",
        "#heg_kurou",
        "#heg_lord_huibian",
        "#heg_lord_jiahe_Attach",
        "#heg_lord_lianzi",
        "#heg_lord_wendao",
        "#heg_mobile_paiyi",
        "#heg_mouduan",
        "#heg_nos_huaiyi",
        "#heg_ov_hongyuan",
        "#heg_ov_jiansu",
        "#heg_paiyi",
        "#heg_pojing",
        "#heg_qiangxi",
        "#heg_qingyin",
        "#heg_quanjin",
        "#heg_ruilve",
        "#heg_sanchen",
        "#heg_shensu",
        "#heg_shunfu",
        "#heg_six_swords",
        "#heg_tenyear_boyan",
        "#heg_tenyear_fenglue",
        "#heg_tenyear_mingfa",
        "#heg_tenyear_weimeng",
        "#heg_tianxiang",
        "#heg_transfer",
        "#heg_weidi",
        "#heg_xianqu",
        "#heg_xiejian",
        "#heg_xuanhuo",
        "#heg_yanxi",
        "#heg_yinyangyu",
        "#heg_zaoyun",
        "#heg_zhengbi",
        "#heg_zhuangrong",
        "#twyj_huzhu",
        "#twyj_qijia",
        "#twyj_rangyi",
        "#twyj_zhuchen"
    }) {
        const QString key=QString::fromLatin1(name);
        std::unique_ptr<LuaSkillCard> card(LuaSkillCard::Parse(key+":.:"));
        prototypes.insert(key,bool(card));
    }
    for (const char *name : {"ShensuCard","TenyearRendeCard","ZhihengCard"}) {
        std::unique_ptr<Card> card(Sanguosha->cloneSkillCard(QString::fromLatin1(name)));
        prototypes.insert(QString::fromLatin1(name),bool(card));
    }
    QJsonObject cloneNames;
    {
        // The literal source constructor key must resolve on its own. A modern
        // HThreatenEmperor template is not a replacement for a missing Lua card.
        std::unique_ptr<Card> card(Sanguosha->cloneCard("heg_threaten_emperor",Card::NoSuit,0));
        QJsonObject row{{"present",bool(card)}};
        if(card){row.insert("name",card->objectName());row.insert("class_name",card->getClassName());
            row.insert("suit",int(card->getSuit()));row.insert("number",card->getNumber());}
        cloneNames.insert("heg_threaten_emperor",row);
    }
    auto facts=world.strategyFacts.value("extra28").toObject();
    facts.insert("source_globals",globals);facts.insert("legacy_prototypes",prototypes);facts.insert("clone_names",cloneNames);
    world.strategyFacts.insert("extra28",facts);
}
