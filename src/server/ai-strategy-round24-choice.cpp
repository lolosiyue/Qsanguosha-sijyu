#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round24-choice.h"
#include "ai-strategy-public-disclosure.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QJsonArray>
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
bool current(const AIRequest &request, const ServerPlayer *viewer)
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
    if(!current(request,viewer))return false;
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

void captureAIRound24ChoiceRegistry(Room &room)
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
void projectAIRound24ChoiceRegistry(AIWorldView &world,Room &room)
{
    auto guo=world.strategyFacts.value("guo").toObject();
    guo.insert("mobileniluan_callback",captured(room)?captured(room)->value:QJsonObject{{"known",false}});
    world.strategyFacts.insert("guo",guo);
}
void projectAIRound24ChoiceContext(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    const auto nativeHasChuming = [&] { return ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("chuming"); }()); };
    projectAIRound24ChoiceRegistry(request.worldView,room);
    if(!chumingSource(request,viewer,nativeHasChuming) || !((request.kind==AIRequest::Choice && request.choiceOptions.reason=="chuming")
        ||(request.kind==AIRequest::UseCard && request.pattern=="@@chuming!")))return;
    auto props=request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    props.insert("chumingSubcard",viewer->property("chumingSubcard").toString());
    request.worldView.strategyFacts.insert("owner_skill_properties",props);
}
bool aiRound24ChoiceConstruction(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
{
    if(!current(request,viewer) || spec.size()!=6)return false;
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
bool aiRound24ChumingPublicMaterials(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
{
    return spec.value("source_key")=="chuming" && aiRound24ChoiceConstruction(request,viewer,spec);
}
bool authorizeAIRound24ChumingPayment(const AIRequest &request,const ServerPlayer *viewer,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,const QJsonObject &spec,QList<int> *publicMaterials)
{
    if(!current(request,viewer) || request.kind!=AIRequest::UseCard || request.pattern!="@@chuming!"
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
