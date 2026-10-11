#include "ai-strategy-round44-routes.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai-strategy-round27-choice.h"
#include "ai-strategy-round28-choice.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "lua.hpp"
#include <QMap>
#include <QSet>
namespace {
struct Origin {QString file;int line;};
const QMap<QString,Origin>&origins(){static const QMap<QString,Origin> values{
        {"kejieshengzhuihun",{"@lua/ai/isolated/keshengbao-ai.lua",423}},
        {"shenghua",{"@lua/ai/isolated/legends-ai.lua",1230}},
        {"#sfofl_shuangren",{"@lua/ai/isolated/sfoflCard-ai.lua",2610}},
        {"sfofl_sitong",{"@lua/ai/isolated/sfoflCard-ai.lua",3476}},
        {"sfofl_jiaozheng",{"@lua/ai/isolated/sfoflCard-ai.lua",3568}},
        {"sfofl_qingjin",{"@lua/ai/isolated/sfoflCard-ai.lua",3610}},
        {"#sfofl_zainei",{"@lua/ai/isolated/sfoflCard-ai.lua",4111}},
        {"#sfofl_sixi",{"@lua/ai/isolated/sfoflCard-ai.lua",5786}},
        {"BearingDownBorder",{"@lua/ai/isolated/sfoflCard-ai.lua",6581}},
        {"@sfofl_bearing_down_border",{"@lua/ai/isolated/sfoflCard-ai.lua",6605}},
        {"sfofl_jubing",{"@lua/ai/isolated/sfoflCard-ai.lua",6623}},
        {"@@sfofl_qifeng",{"@lua/ai/isolated/sfoflCard-ai.lua",6720}},
        {"sfofl_juedou",{"@lua/ai/isolated/sfoflCard-ai.lua",7054}},
        {"mouwusheng",{"@lua/ai/isolated/newgenerals-ai.lua",4871}},
        {"@mouxianchou-slash",{"@lua/ai/isolated/newgenerals-ai.lua",5312}},
        {"shijun",{"@lua/ai/isolated/meizl-ai.lua",5839}},
        {"@@luazhuixi",{"@lua/ai/isolated/leo-ai.lua",97}},
        {"@@luayonglue",{"@lua/ai/isolated/leo-ai.lua",97}},
        {"@@f_yonghunSlash",{"@lua/ai/isolated/GeniusPackageGod-ai.lua",564}},
        {"@PlusZhongyong",{"@lua/ai/isolated/biaofeng-ai.lua",2329}},
        {"SixQiaoxie",{"@lua/ai/isolated/biaofeng-ai.lua",5246}},
        {"GodShijun",{"@lua/ai/isolated/goddess-ai.lua",142}},
    };return values;}
bool owned(const ServerPlayer *p,const QString &skill) {
    if(!p->hasSkill(skill))return false;
    for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill)return true;
    return false;
}
}
bool aiRound44RangeRoute(Room &room,const AIRequest &r,ServerPlayer *p,
 const QString &flag,const QString &source,lua_State *L,int closure) {
    if(!p||!p->isAlive()||!aiRound44RangeRequestCurrent(room,r,p)
        ||!aiRound44RangeSource(flag,source)||!L||!lua_isfunction(L,closure))return false;
    const auto it=origins().constFind(source);if(it==origins().cend())return false;
    lua_pushvalue(L,closure);lua_Debug info{};
    if(!lua_getinfo(L,">S",&info)||!info.what||QByteArray(info.what)!="Lua"
        ||!info.source||QString::fromUtf8(info.source)!=it->file||info.linedefined!=it->line)return false;
    // Bind the package-created closure to the exact serialized active request,
    // not a stale closure retained in isolated VM state from a prior question.
    bool sameRequest=false;closure=lua_absindex(L,closure);
    for(int index=1;;++index) {
        const char *name=lua_getupvalue(L,closure,index);if(!name)break;
        if(QByteArray(name)=="self"&&lua_istable(L,-1)) {
            lua_pushliteral(L,"request");lua_rawget(L,-2);
            sameRequest=lua_istable(L,-1)&&aiRound44RangeLuaRequestCurrent(room,r,lua_topointer(L,-1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
        if(sameRequest)break;
    }
    if(!sameRequest)return false;
    // The executed adapter must be the trusted facade prototype and must close
    // over the very same package closure, not an arbitrary callback paired with
    // an otherwise authentic fourth argument. Sandbox loaders/debug mutation
    // are removed before any configured script is loaded.
    if(!lua_isfunction(L,3))return false;
    lua_pushvalue(L,3);lua_Debug adapter{};
    if(!lua_getinfo(L,">S",&adapter)||!adapter.source
        ||QByteArray(adapter.source)!="@lua/ai/isolated-facades.lua"
        ||adapter.linedefined!=5451)return false;
    bool sameClosure=false,sameSelf=false;
    for(int index=1;;++index){
        const char *name=lua_getupvalue(L,3,index);if(!name)break;
        if(QByteArray(name)=="closure")sameClosure=lua_rawequal(L,-1,closure);
        if(QByteArray(name)=="self"&&lua_istable(L,-1)){
            lua_pushliteral(L,"request");lua_rawget(L,-2);
            sameSelf=lua_istable(L,-1)&&aiRound44RangeLuaRequestCurrent(room,r,lua_topointer(L,-1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
    }
    if(!sameClosure||!sameSelf)return false;
    return aiRound44RangeQuestion(r,p,source);
}
bool aiRound44RangeQuestion(const AIRequest &r,const ServerPlayer *p,const QString &source) {
    if(!p||!p->isAlive()||!p->getRoom())return false;
    Room &room=*p->getRoom();
    const bool nested=r.worldView.strategyFacts.contains("readonly_play_context")
        &&(aiRound23OlClanPlanningQuestion(r,room,p)||aiRound27ChoicePlanningQuestion(r,room,p)
            ||aiRound28ChoicePlanningQuestion(r,room,p));
    const bool play=(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY)||nested;
    static const QMap<QString,QString> playSkills{{"kejieshengzhuihun","kejieshengzhuihun"},
        {"shenghua","shenghua"},{"#sfofl_shuangren","sfofl_shuangren"},{"#sfofl_zainei","sfofl_zainei"},
        {"#sfofl_sixi","sfofl_sixi"},{"shijun","shijun"},{"GodShijun","GodShijun"}};
    if(playSkills.contains(source))return play&&owned(p,playSkills.value(source));
    // A printed-card planner is admitted by its actual immutable body, not by
    // inventing a skill named BearingDownBorder or granting a payment ticket.
    if(source=="BearingDownBorder")return play;
    static const QSet<QString> invoke{"sfofl_sitong","sfofl_qingjin","sfofl_jubing","sfofl_juedou"};
    if(invoke.contains(source))return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==source&&owned(p,source);
    // SixQiaoxie asks the current turn player, NOT the skill owner (biaofeng12865).
    if(source=="SixQiaoxie")return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==source;
    if(source=="mouwusheng")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason==source&&owned(p,source);
    // Native recipient inquiries deliberately require no recipient skill.
    if(source=="sfofl_jiaozheng")return r.kind==AIRequest::PlayersChosen&&r.choiceOptions.reason==source;
    if(source=="@mouxianchou-slash")return r.kind==AIRequest::RespondCard&&r.pattern==".."&&r.prompt.split(':').first()==source;
    if(source=="@sfofl_bearing_down_border")return r.kind==AIRequest::RespondCard&&r.pattern=="."&&r.prompt.split(':').first()==source;
    if(source=="@@f_yonghunSlash")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"f_yonghun");
    if(source=="@@sfofl_qifeng")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"sfofl_qifeng");
    if(source=="@@luazhuixi")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"luazhuixi");
    if(source=="@@luayonglue")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"luayonglue");
    if(source=="@PlusZhongyong")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"PlusZhongyong");
    return false;
}
