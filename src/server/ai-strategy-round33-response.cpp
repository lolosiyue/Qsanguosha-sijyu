#include "ai-strategy-round33-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <memory>

bool isAIRound33TongyiSource(const AIRequest &r,const QString &skill,const QString &policyClass)
{
    if (skill!="tongyiTQ") return false;
    const bool jink=r.kind==AIRequest::RespondCard && r.pattern=="jink!"
        && (policyClass=="Jink" || policyClass=="MeleeSlashJink")
        && ((r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE && r.handlingMethod==Card::MethodResponse)
            || (r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE && r.handlingMethod==Card::MethodUse));
    // Fixed native ChanshuangCard::use asks this exact compulsory question.
    const bool slash=r.kind==AIRequest::UseCard && r.pattern=="Slash!"
        && (policyClass=="Slash" || policyClass=="FireSlash" || policyClass=="ThunderSlash"
            || policyClass=="IceSlash" || policyClass=="GodSlash" || policyClass=="MeleeSlashJink")
        && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE && r.handlingMethod==Card::MethodUse;
    return jink || slash;
}

Card *buildAIRound33TongyiSource(const AIRequest &r,Room &room,ServerPlayer *p,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,
    const QString &policyClass,const QString &declaration,const QList<int> &costs)
{
    if(!p || !isAIRound33TongyiSource(r,activation.key.skillName,policyClass)
        || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision() || !costs.isEmpty()
        || !activation.isValid() || !source.isValid()
        || activation.ownerObjectName!=p->objectName()
        || !p->hasSkillInstance(activation.key.skillName,activation.key.instanceID)
        || p->isSkillInvalid(activation.key.skillName,activation.key.instanceID)
        || room.resolveSkillInstanceRootRef(activation)!=source) return nullptr;
    const auto classRow=r.conversionPolicy.value("by_class").toObject().value(policyClass).toObject();
    if(classRow.value("card_name")!=declaration)return nullptr;
    // Full generic source loops each matching patterns() class; MeleeSlashJink
    // is its own class and explicitly matches both families. Do not relabel it.
    // Fixed constructors: standard-cards22/307, maneuvering28/36, yingbian26,
    // godlailailai2283, meleemode200/212; extensions yongjian109, nybeauty267.
    const bool fixedPair=(policyClass=="Jink" && declaration=="jink")
        || (policyClass=="MeleeSlashJink" && declaration=="__melee_slash_jink")
        || (policyClass=="Slash" && (declaration=="slash" || declaration=="yj_stabs_slash" || declaration=="_stabs_slash"))
        || (policyClass=="FireSlash" && declaration=="fire_slash")
        || (policyClass=="ThunderSlash" && declaration=="thunder_slash")
        || (policyClass=="IceSlash" && declaration=="ice_slash")
        || (policyClass=="GodSlash" && declaration=="_god_slash");
    if(!fixedPair)return nullptr;
    std::unique_ptr<Card> declared(Sanguosha->cloneCard(declaration,Card::NoSuit,0));
    if(!declared || declared->getClassName()!=policyClass
        || !declared->isKindOf(r.pattern=="jink!" ? "Jink" : "Slash"))return nullptr;
    const auto *viewAs=Sanguosha->getViewAsSkill("tongyiTQ");
    if(!viewAs || dynamic_cast<const ViewAsSkillV2 *>(viewAs)
        || !viewAs->isEnabledAtResponse(p,declaration))return nullptr;
    // AIgeneral-ai.lua225 returns this exact literal using patterns(class).
    // LuaAI source parsing and Room's final V1 path never call GUI viewAs.
    std::unique_ptr<const Card> parsed(Card::Parse("#tongyiTQ:.:"+declaration));
    const auto *sc=parsed ? dynamic_cast<const SkillCard *>(parsed.get()) : nullptr;
    if(!sc || !parsed->isVirtualCard() || parsed->objectName()!="tongyiTQ"
        || parsed->subcardsLength()!=0 || sc->getUserString()!=declaration
        || parsed->getSuit()!=Card::NoSuit || parsed->getNumber()!=0)return nullptr;
    Card *card=const_cast<Card *>(parsed.release());
    card->setActivationSkill(activation.key.skillName,activation.key.instanceID);
    card->setSourceSkill(source.key.skillName,source.key.instanceID);
    return card;
}

#include <QJsonArray>
#include <cmath>
#include <climits>
AIRound33CardsViewObject::AIRound33CardsViewObject()=default;
AIRound33CardsViewObject::~AIRound33CardsViewObject()=default;
AIRound33CardsViewObject::AIRound33CardsViewObject(AIRound33CardsViewObject &&) noexcept=default;
AIRound33CardsViewObject &AIRound33CardsViewObject::operator=(AIRound33CardsViewObject &&) noexcept=default;
AIRound33CardsViewObject buildAIRound33CardsViewObject(const AIRequest &r,Room &room,ServerPlayer *p,
    const QJsonObject &scope,const QString &registry,const QString &hook,
    const QJsonObject &proposal,bool attachRequest,const Card *ownedSource)
{
    AIRound33CardsViewObject out;
    const bool creating=proposal.contains("source_object_create"), attaching=proposal.contains("source_object_key");
    if(!creating&&!attaching)return out;
    out.handled=true;
    const auto fail=[&](const char *why){out.error=QString::fromLatin1(why);return std::move(out);};
    const auto identity=scope.value("identity").toObject();
    const SkillInstanceRef activation(identity.value("activation_owner").toString(),
        SkillInstanceKey(identity.value("activation_skill").toString(),identity.value("activation_instance").toInt()));
    const SkillInstanceRef source(identity.value("source_owner").toString(),
        SkillInstanceKey(identity.value("source_skill").toString(),identity.value("source_instance").toInt()));
    if(proposal.size()!=1 || creating==attaching || !attachRequest || !scope.value("has_action").toBool()
        || registry!="ai_cardsview" || hook!="sxzhitu" || scope.value("skill_name")!="sxzhitu"
        || scope.value("legacy_hook_keys").toObject().value(registry)!=hook
        || !p || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision() || !activation.isValid() || !source.isValid()
        || activation.ownerObjectName!=p->objectName() || activation.key.skillName!="sxzhitu"
        || !p->hasSkillInstance(activation.key.skillName,activation.key.instanceID)
        || p->isSkillInvalid(activation.key.skillName,activation.key.instanceID)
        || room.resolveSkillInstanceRootRef(activation)!=source)
        return fail("Card-object attachment has no exact current sxzhitu callback scope");
    const QString className=scope.value("class_name").toString();
    const Card *card=ownedSource;
    QList<int> ids;
    if(creating) {
        if(!proposal.value("source_object_create").isArray())return fail("sxzhitu object materials are not an array");
        const auto values=proposal.value("source_object_create").toArray();
        if(values.size()!=2)return fail("sxzhitu source requires two ordered materials");
        for(const auto &v:values){const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>INT_MAX)return fail("sxzhitu material is not an ID");ids<<int(n);}
        if(ids[0]==ids[1])return fail("sxzhitu source skips the same material object");
        int sum=0;
        for(int id:ids) {
            bool disclosed=false;
            for(const auto &c:r.worldView.handCards)if(!c.virtualCard&&c.effectiveId==id)disclosed=true;
            for(const auto &c:r.worldView.self.equips)if(!c.virtualCard&&c.effectiveId==id)disclosed=true;
            if(!disclosed||room.getCardOwner(id)!=p
                ||(room.getCardPlace(id)!=Player::PlaceHand&&room.getCardPlace(id)!=Player::PlaceEquip))
                return fail("sxzhitu material is not current disclosed owner HE");
            const Card *c=Sanguosha->getCard(id);if(!c)return fail("sxzhitu disclosed material expired");sum+=c->getNumber();
        }
        if(sum!=13)return fail("sxzhitu source material numbers do not sum to thirteen");
        // Source dummyCard(class_name,"sxzhitu") followed by ordered addSubcard.
        out.created.reset(Sanguosha->cloneCard(className,Card::NoSuit,0));
        if(!out.created){out.absent=true;return out;}
        if(out.created->getTypeId()==Card::TypeSkill)return fail("sxzhitu ordinary clone resolved to a SkillCard");
        out.created->setSkillName("sxzhitu");out.created->addSubcards(ids);card=out.created.get();
    } else {
        if(!proposal.value("source_object_key").isString()||!card||card->parent()
            ||!card->isVirtualCard()||card->getTypeId()==Card::TypeSkill
            ||card->getClassName()!=className||card->getSubcards().size()!=2)
            return fail("sxzhitu source object custody or class expired");
        // The dispatcher checked this exact recipe's current request and scope
        // source key. Only that detached object may receive the source setters.
        Card *mutableCard=const_cast<Card*>(card);
        mutableCard->setSkillName(activation.key.skillName);
        mutableCard->setActivationSkill(activation.key.skillName,activation.key.instanceID);
        mutableCard->setSourceSkill(source.key.skillName,source.key.instanceID);
        out.attached=card;
    }
    QJsonArray costs;for(int id:card->getSubcards())costs.append(id);
    out.bindingSpec={{"name",card->objectName()},{"skill",card->getSkillName(false)},
        {"suit",int(card->getSuit())},{"number",card->getNumber()},{"subcards",costs}};
    return out;
}

bool aiRound33TongyiConstruction(const AIRequest &r,Room &room,ServerPlayer *p,const QJsonObject &spec)
{
    if(spec.size()!=6 || spec.value("source_key")!="tongyiTQ" || spec.value("skill")!="tongyiAIWJ"
        || !spec.value("name").isString() || spec.value("suit")!=int(Card::NoSuit)
        || spec.value("number")!=0 || !spec.value("subcards").isArray()
        || !spec.value("subcards").toArray().isEmpty())return false;
    // This constructor query originates in the isolated VM, but the audited
    // V1 enablement/prototype callbacks belong to the room's gameplay VM.
    // Binding outlives every temporary Card below; no Lua userdata crosses it.
    EngineRuntimeContextScope engineContext(*Sanguosha,&room);
    LuaRuntime::Binding gameBinding(room.roomRuntime()->lua(),false);
    const QString name=spec.value("name").toString();
    const auto scopes=r.conversionPolicy.value("scopes").toObject();
    for(const auto &value:scopes) {
        const auto scope=value.toObject();const QString cls=scope.value("class_name").toString();
        if(scope.value("skill_name")!="tongyiTQ" || scope.value("card_name")!=name
            || scope.value("version")!=1 || !isAIRound33TongyiSource(r,"tongyiTQ",cls))continue;
        const auto identity=scope.value("identity").toObject();
        const SkillInstanceRef activation(identity.value("activation_owner").toString(),
            SkillInstanceKey(identity.value("activation_skill").toString(),identity.value("activation_instance").toInt()));
        const SkillInstanceRef source(identity.value("source_owner").toString(),
            SkillInstanceKey(identity.value("source_skill").toString(),identity.value("source_instance").toInt()));
        // Reuse precisely the source product proof, not a generic clone/name grant.
        // This temporary prototype has no receipt and no gameplay effect.
        std::unique_ptr<Card> proof(buildAIRound33TongyiSource(r,room,p,activation,source,cls,name,{}));
        if(proof && r.stateRevision==room.roomRuntime()->stateRevision())return true;
    }
    return false;
}
