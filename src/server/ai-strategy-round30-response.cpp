#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round30-response.h"
#include "ai-strategy-round45-choice.h"
#include "ai-strategy-source-prototypes.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

AIRound30ResponsePolicy::AIRound30ResponsePolicy() = default;
AIRound30ResponsePolicy::~AIRound30ResponsePolicy() = default;
AIRound30ResponsePolicy::AIRound30ResponsePolicy(AIRound30ResponsePolicy &&) noexcept = default;
AIRound30ResponsePolicy &AIRound30ResponsePolicy::operator=(AIRound30ResponsePolicy &&) noexcept = default;
namespace {
bool integer(const QJsonValue &v, int &n) {
    const double x=v.toDouble();
    if(!v.isDouble() || !std::isfinite(x) || x!=std::floor(x)
        || x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max())return false;
    n=int(x);return true;
}
bool disclosed(const AIRequest &r, Room &room, ServerPlayer *p, int id) {
    if(id<0 || room.getCardOwner(id)!=p)return false;
    bool found=false;
    const auto scan=[&](const QList<AICardView> &cards) {
        for(const auto &c:cards) if(!c.virtualCard && c.effectiveId==id)found=true;
    };
    scan(r.worldView.handCards);scan(r.worldView.self.knownCards);
    scan(r.worldView.self.equips);scan(r.worldView.self.judgingArea);
    for(const auto &pile:r.worldView.self.piles)scan(pile.cards);
    return found;
}
bool prototypeAllowed(const QString &skill,const QString &name) {
    static const QMap<QString,QString> response{
        {"jijiang","JijiangCard"},{"nosjiefan","NosJiefanCard"},{"nosrenxin","NosRenxinCard"},
        {"jinxianwan","JinXianwanCard"},{"qinwang","QinwangCard"},{"zhenshan","ZhenshanCard"},
        {"huomo","HuomoCard"},{"chunlao","ChunlaoWineCard"},{"olchunlao","OLChunlaoCard"},
        {"ejisi","#ejisiCard"},{"inovation_fengzhu","InovationFengzhuCard"},
        {"guhuo","GuhuoCard"},{"aocai","AocaiCard"},{"heg_aocai","HAocaiCard"},
        {"ov_yingji","#ov_yingjiCard"},{"ov_mouli","#ov_mouliCard"},{"ov_fushu","#ov_fushuCard"},
        {"sr_fangxin","#sr_fangxincard"},{"sr_qingcheng","#sr_qingchengcard"},
        {"sr_xiahou","#sr_xiahoucard"},{"tongyiTQ","#tongyiTQ"}};
    return response.value(skill)==name || aiAuditedSourcePrototype(skill,name)>=0;
}
QJsonArray idsJson(const QList<int> &ids) { QJsonArray a;for(int id:ids)a.append(id);return a; }
}
AIRound30ResponsePolicy buildAIRound30ResponsePolicy(const AIRequest &r,Room &room,ServerPlayer *p,
    const QJsonObject &scope,const QString &registry,const QString &hookKey,
    const QJsonValue &proposal,bool attachRequest) {
    AIRound30ResponsePolicy out;
    const auto unknown=[&](const char *why) {out.error=QString::fromLatin1(why);return std::move(out);};
    const auto absent=[&]() {out.status=AIRound30ResponsePolicy::Absent;return std::move(out);};
    static const QSet<QString> registries{"ai_guhuo_card","ai_cardsview_valuable","ai_cardsview","ai_view_as"};
    const QString skill=scope.value("skill_name").toString();
    const QJsonObject identity=scope.value("identity").toObject();
    if(!p || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || !aiRound44RangeRequestCurrent(room,r,p) || !registries.contains(registry)
        || skill.isEmpty() || hookKey.isEmpty() || !scope.value("class_name").isString()
        || scope.value("legacy_hook_keys").toObject().value(registry)!=hookKey
        || (hookKey!=skill && (!attachRequest || hookKey!=identity.value("source_skill").toString()))
        || attachRequest!=scope.value("has_action").toBool()
        || (attachRequest && (registry=="ai_guhuo_card" || registry=="ai_view_as")))
        return unknown("readonly response source is not the exact issued callback scope");
    if(identity.value("activation_owner")!=p->objectName()
        || !identity.value("source_owner").isString() || identity.value("source_owner").toString().isEmpty())
        return unknown("readonly response activation or source owner is unknown");
    // A legitimate copied skill can retain another owner's source reference.
    // Only the coordinator's issued native scope validates that relationship;
    // its presence never permits reading that source owner's material faces.
    for(const QString &field:{QStringLiteral("activation_instance"),QStringLiteral("source_instance")}) {
        int n=0;if(!integer(identity.value(field),n)||n<=0)return unknown("readonly response instance is unknown");
    }
    QString wire,selector,name,rawSkill,userString;
    QList<int> ids;int suit=int(Card::NoSuit),number=0;bool typed=false;
    if(proposal.isObject()) {
        typed=true;const auto spec=proposal.toObject();
        static const QSet<QString> fields{"name","skill","suit","number","subcards","legacy_class","user_string"};
        for(auto it=spec.begin();it!=spec.end();++it)if(!fields.contains(it.key()))return unknown("unrepresented response specification field");
        if(!spec.value("skill").isString() || !spec.value("subcards").isArray())return unknown("response specification shape is unknown");
        rawSkill=spec.value("skill").toString();
        if(spec.contains("user_string")&&!spec.value("user_string").isString())return unknown("response declaration is not a string");
        userString=spec.value("user_string").toString();
        if(spec.contains("legacy_class")) {
            if(!spec.value("legacy_class").isString() || spec.contains("name") || spec.contains("suit") || spec.contains("number"))
                return unknown("legacy response specification mixes product identities");
            selector=spec.value("legacy_class").toString();
        } else {
            if(!spec.value("name").isString() || !integer(spec.value("suit"),suit)
                || suit < -1 || suit>6 || !integer(spec.value("number"),number) || number < -1 || number>15)
                return unknown("response specification face is unknown");
            name=spec.value("name").toString();
            if(spec.contains("user_string"))return unknown("ordinary response specification has no user-string grammar");
        }
        const auto costs=spec.value("subcards").toArray();
        if(costs.size()>128)return unknown("response source material bound exceeded");
        for(const auto &v:costs){int id=-1;if(!integer(v,id))return unknown("response material ID is not an integer");ids.append(id);}
    } else if(proposal.isString()) {
        wire=proposal.toString();
        if(wire.size()>8192)return unknown("response source wire bound exceeded");
        QString text=wire.section("->",0,0),costText;
        if(text.startsWith('@')) {
            static const QRegularExpression plain("^@(\\w+)=([^:]+)(:.+)?$"),extended("^@(\\w*)\\[(\\w+):(.+)\\]=([^:]+)(:.+)?$");
            auto m=plain.match(text);
            if(m.hasMatch()){selector=m.captured(1);costText=m.captured(2);}
            else {m=extended.match(text);if(!m.hasMatch())return absent();selector=m.captured(1);costText=m.captured(4);}
        } else if(text.startsWith('#')) {
            while(text.startsWith("##"))text.remove(0,1);
            static const QRegularExpression plain("#(\\w+):(.*):(.*)"),extended("#(\\w*)\\[(\\w+):(.+)\\]:(.*):(.*)");
            auto m=plain.match(text);
            if(m.hasMatch()){selector="#"+m.captured(1);costText=m.captured(2);}
            else {m=extended.match(text);if(!m.hasMatch())return absent();selector="#"+m.captured(1);costText=m.captured(4);}
        } else if(text.startsWith('$')) return unknown("DummyCard response wire is not an audited callback product");
        else if(wire.contains('=')) {
            static const QRegularExpression ordinary("^(\\w+):(\\w*)\\[(\\w+):(.+)\\]=(.+)$");
            const auto m=ordinary.match(text);if(!m.hasMatch())return absent();
            name=m.captured(1);rawSkill=m.captured(2);costText=m.captured(5);
        } else {
            bool ok=false;int id=text.toInt(&ok);if(!ok || text==".")return absent();
            if(!disclosed(r,room,p,id))return unknown("physical response string is not an owner-disclosed card");
            if(attachRequest){
                QString error;out.physicalCard=attachAIRound45DanlvePhysical(room,r,p,scope,registry,hookKey,id,&error);
                if(!out.physicalCard){out.error=error;return std::move(out);}
                out.bindingSpec={{"physical_id",id},{"physical_request_attached",true}};
                out.status=AIRound30ResponsePolicy::Parsed;return out;
            }
            out.physicalCard=Sanguosha->getCard(id);
            if(!out.physicalCard)return absent();
            out.bindingSpec={{"physical_id",id}};out.status=AIRound30ResponsePolicy::Parsed;return out;
        }
        if(costText!=".") {
            const auto tokens=costText.split('+');if(tokens.size()>128)return unknown("response source material bound exceeded");
            // ListS2I uses QString::toInt; preserve its zero coercion, but only
            // when that resulting physical ID is already disclosed to owner.
            for(const auto &token:tokens){const int id=token.toInt();ids.append(id);}
        }
    } else return unknown("response parser requires a serialized card or typed source descriptor");
    if(!selector.isEmpty() && !prototypeAllowed(skill,selector) && !prototypeAllowed(hookKey,selector))
        return unknown("response SkillCard prototype is outside the audited source catalog");
    if(typed) {
        if(!selector.isEmpty()) {
            if(selector.startsWith('#'))out.card.reset(LuaSkillCard::Parse(selector+":.:"));
            else {
                auto *card=Sanguosha->cloneSkillCard(selector);out.card.reset(card);
                if(card && card->getSkillName().isEmpty()){QString derived=selector;card->setSkillName(derived.remove("Card").toLower());}
            }
            if(auto *card=dynamic_cast<SkillCard*>(out.card.get()))card->setUserString(userString);
        } else {
            out.card.reset(Sanguosha->cloneCard(name,Card::Suit(suit),number));
            if(out.card && out.card->getTypeId()==Card::TypeSkill)return unknown("ordinary response name resolves to an unaudited SkillCard");
            if(out.card)out.card->setSkillName(rawSkill);
        }
        if(out.card)out.card->addSubcards(ids);
    } else {
        // Validation above makes parsing readonly: no hidden material lookup,
        // no arbitrary Lua constructor, no target list or callback execution.
        const Card *parsed=Card::Parse(wire);
        if(parsed)out.card.reset(const_cast<Card*>(parsed));
    }
    if(!out.card)return absent();
    // Construction and ID storage above never inspect a material face. A
    // missing registered product remains a source parse miss even when its
    // unused material IDs were not disclosed. Validate before effective face.
    for(int id:ids)if(!disclosed(r,room,p,id))return unknown("response material is not disclosed to its owner");
    if(out.card->getSubcards()!=ids)return unknown("native response parser material order changed");
    if(selector.isEmpty() && out.card->getTypeId()==Card::TypeSkill)return unknown("ordinary response parser returned an unaudited SkillCard");
    if(attachRequest) {
        out.card->setSkillName(identity.value("activation_skill").toString());
        out.card->setActivationSkill(identity.value("activation_skill").toString(),identity.value("activation_instance").toInt());
        out.card->setSourceSkill(identity.value("source_skill").toString(),identity.value("source_instance").toInt());
    }
    if(selector.isEmpty()) out.bindingSpec={{"name",out.card->objectName()},{"skill",out.card->getSkillName(false)},
        {"suit",int(out.card->getSuit())},{"number",out.card->getNumber()},{"subcards",idsJson(ids)}};
    else {
        out.bindingSpec={{"skill",skill},{"legacy_class",selector},{"subcards",idsJson(ids)}};
        const auto *card=dynamic_cast<const SkillCard*>(out.card.get());
        if(!card)return unknown("audited response prototype is not a SkillCard");
        if(!card->getUserString().isEmpty())out.bindingSpec.insert("user_string",card->getUserString());
    }
    out.status=AIRound30ResponsePolicy::Parsed;return out;
}
