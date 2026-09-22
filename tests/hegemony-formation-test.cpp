#include "engine-bootstrap.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"
#include "standard-cards.h"

#include <QDebug>
#include <QScopeGuard>
#include <memory>

namespace {
#define HEG_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Formation V2 contract failed" << __LINE__ << #condition; \
    return false; \
} } while (false)

bool formationMetadata()
{
    const QMap<QString, QStringList> formationShared{
        {"heg_dengai", {"tuntian", "jixi"}}, {"heg_jiangwei", {"tiaoxin"}},
        {"heg_hetaihou", {"zhendu", "qiluan"}}};
    for (auto it = formationShared.cbegin(); it != formationShared.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        for (const QString &name : it.value()) {
            HEG_CHECK(general->hasSkill(name) && Sanguosha->getSkill(name));
            HEG_CHECK(!Sanguosha->getSkill("heg_" + name));
        }
    }
    for (const QString &name : {"heg_ziliang", "heg_huyuan", "heg_heyi", "heg_tianfu",
                               "heg_shengxi", "heg_shoucheng", "heg_niaoxiang", "heg_yicheng",
                               "heg_qianhuan", "heg_zhangwu", "heg_shouyue", "heg_jizhao"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    HEG_CHECK(dynamic_cast<const DistanceSkillV2 *>(Sanguosha->getSkill("heg_feiying")));
    const auto *shangyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_shangyi"));
    HEG_CHECK(shangyi && shangyi->getLimitScope() == Skill::Limit_Phase);
    for (const QString &name : {"HZiliangCard", "HHuyuanCard", "HTiaoxinCard", "HShangyiCard",
                               "HQianhuanCard", "HHeyiSummon", "HTianfuSummon", "HNiaoxiangSummon"}) {
        std::unique_ptr<SkillCard> removed(Sanguosha->cloneSkillCard(name));
        HEG_CHECK(!removed);
    }
    return true;
}

bool formationMode(bool hegemony)
{
    Config.EnableHegemony = hegemony;
    Config.EnableBasara = hegemony;
    Config.Enable2ndGeneral = hegemony;
    ServerInfo.EnableHegemony = hegemony;
    ServerInfo.EnableBasara = hegemony;
    ServerInfo.Enable2ndGeneral = hegemony;
    QVariantMap values = Config.valueOverrides();
    values.insert("EnableHegemony", hegemony);
    Config.setValueOverrides(values);
    Room room(nullptr, QStringLiteral("04p"));
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    {
        ServerPlayer owner(&room), right(&room), opposite(&room), left(&room);
        owner.setNext(&right);
        right.setNext(&opposite);
        opposite.setNext(&left);
        left.setNext(&owner);
        const auto *heyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_heyi"));
        const auto *shangyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_shangyi"));
        const auto *niaoxiang = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("heg_niaoxiang"));
        HEG_CHECK(heyi && shangyi && niaoxiang);
        ActiveSkillRequest request;
        request.initiator = &owner;
        request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        request.pattern = "@@heg_heyi";
        HEG_CHECK(heyi->canActivate(request) == !hegemony);
        HEG_CHECK(heyi->targetMode() == (hegemony ? ViewAsSkillV2::NoTarget : ViewAsSkillV2::SelectTargets));
        HEG_CHECK(heyi->targetsFeasible(request, {}) == hegemony);
        if (!hegemony) {
            // Both clockwise and wraparound groups are legal; gaps and omission of self are not.
            HEG_CHECK(heyi->targetsFeasible(request, {&owner, &right}));
            HEG_CHECK(heyi->targetsFeasible(request, {&left, &owner, &right}));
            HEG_CHECK(heyi->targetsFeasible(request, {&owner, &right, &opposite, &left}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&owner}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&right, &opposite}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&owner, &opposite}));
            HEG_CHECK(Sanguosha->getSkill("feiying") && Sanguosha->getSkill("kanpo"));
        }
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.pattern.clear();
        HEG_CHECK(shangyi->canActivate(request) == !hegemony); // Empty hand remains legal in identity mode.
        HEG_CHECK(shangyi->canSelectTarget(request, {}, &right));
        HEG_CHECK(!shangyi->canSelectTarget(request, {}, &owner));
        HEG_CHECK(niaoxiang->getFrequency(&owner) == (hegemony ? Skill::Compulsory : Skill::NotFrequent));
        if (!hegemony) {
            for (const QString &name : {"heg_heyi", "heg_tianfu", "heg_niaoxiang"})
                HEG_CHECK(!dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name))->canActivate(request));
        }
    }
    {
        ServerPlayer equipped(&room);
        const auto *array = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_niaoxiang"));
        HEG_CHECK(array);
        ActiveSkillRequest summonRequest;
        summonRequest.initiator = &equipped;
        summonRequest.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        HEG_CHECK(array->targetMode() == ViewAsSkillV2::NoTarget);
        HEG_CHECK(!array->canActivate(summonRequest)); // An unowned array cannot summon.
        std::unique_ptr<const Card> summon(array->createCard(summonRequest));
        HEG_CHECK(summon && summon->isKindOf("ActiveSkillCard"));
        HEG_CHECK(summon->getSkillName() == "heg_niaoxiang");

        // Response selectors reject unrelated cards and duplicate submissions.
        for (const QString &name : {"heg_ziliang", "heg_huyuan", "heg_qianhuan"}) {
            const auto *selection = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name));
            HEG_CHECK(selection && !selection->willThrowSelectedCards());
            ActiveSkillRequest request;
            request.initiator = &equipped;
            request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
            request.pattern = "@@" + name;
            HEG_CHECK(selection->canActivate(request));
            HEG_CHECK(!selection->cardSelectionFeasible(request));
            request.selectedCardIds = {0, 0};
            HEG_CHECK(!selection->cardSelectionFeasible(request) && !selection->createCard(request));
            request.selectedCardIds.clear();
            request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
            HEG_CHECK(!selection->canActivate(request));
        }
    }
    return true;
}
}

int runHegemonyFormationTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Formation bootstrap failed:" << error;
        return 1;
    }
    const bool hegemony = Config.EnableHegemony;
    const bool basara = Config.EnableBasara;
    const bool second = Config.Enable2ndGeneral;
    const bool ai = Config.EnableAI;
    const ServerInfoStruct serverInfo = ServerInfo;
    const QVariantMap previousOverrides = Config.valueOverrides();
    const auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.EnableBasara = basara;
        Config.Enable2ndGeneral = second;
        Config.EnableAI = ai;
        Config.setValueOverrides(previousOverrides);
        ServerInfo = serverInfo;
    });
    // Selection contracts do not require an AI runtime or a running game.
    Config.EnableAI = false;
    if (!formationMetadata()) return 2;
    if (!formationMode(true)) return 3;
    if (!formationMode(false)) return 4;
    qInfo() << "HEGEMONY_FORMATION_TEST_RESULT status=PASS";
    return 0;
}
