#include "ai-strategy/native-registry.h"

// ---- native-types ----
#include "skill.h"
#include "lua-wrapper.h"

// R56 exact source-owned native type registrations. No callback executes.
void registerAINativeTypes_src_server_ai_strategy_native_types_cpp(AINativeTypes&types){
 types.emplace(std::type_index(typeid(LuaTriggerSkill)), 2u);
 types.recordActivateSource<LuaTriggerSkill>(2936u); AI_NATIVE_REQUIRE_DECLARATION(LuaTriggerSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaTriggerSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaTriggerSkillV2)), 2u);
 types.recordActivateSource<LuaTriggerSkillV2>(2937u); AI_NATIVE_REQUIRE_DECLARATION(LuaTriggerSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaTriggerSkillV2, getLimitScope, LuaTriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaTriggerSkillV2, getMaxUsageLimit, LuaTriggerSkillV2);
 types.emplace(std::type_index(typeid(LuaEquipSkillV2)), 2u);
 types.recordActivateSource<LuaEquipSkillV2>(2938u); AI_NATIVE_REQUIRE_DECLARATION(LuaEquipSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaEquipSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaEquipSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaScenarioRule)), 2u);
 types.recordActivateSource<LuaScenarioRule>(2939u); AI_NATIVE_REQUIRE_DECLARATION(LuaScenarioRule, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaScenarioRule, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaProhibitSkill)), 3u);
 types.recordActivateSource<LuaProhibitSkill>(2940u); AI_NATIVE_REQUIRE_DECLARATION(LuaProhibitSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaProhibitSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaProhibitPindianSkill)), 2u);
 types.recordActivateSource<LuaProhibitPindianSkill>(2941u); AI_NATIVE_REQUIRE_DECLARATION(LuaProhibitPindianSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaProhibitPindianSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaViewAsSkill)), 2u);
 types.recordActivateSource<LuaViewAsSkill>(2942u); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(LuaViewAsSkillV2)), 2u);
 types.recordActivateSource<LuaViewAsSkillV2>(2943u); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, getBaseAmount, ViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, getLimitScope, LuaViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, getMaxUsageLimit, LuaViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, hasIndependentAIConversion, ViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, targetMode, LuaViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsSkillV2, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(LuaFilterSkill)), 2u);
 types.recordActivateSource<LuaFilterSkill>(2944u); AI_NATIVE_REQUIRE_DECLARATION(LuaFilterSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaFilterSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaFilterSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(LuaDistanceSkill)), 3u);
 types.recordActivateSource<LuaDistanceSkill>(2945u); AI_NATIVE_REQUIRE_DECLARATION(LuaDistanceSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaDistanceSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaDistanceSkillV2)), 3u);
 types.recordActivateSource<LuaDistanceSkillV2>(2946u); AI_NATIVE_REQUIRE_DECLARATION(LuaDistanceSkillV2, getBaseAmount, DistanceSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaDistanceSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaDistanceSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaMaxCardsSkill)), 3u);
 types.recordActivateSource<LuaMaxCardsSkill>(2947u); AI_NATIVE_REQUIRE_DECLARATION(LuaMaxCardsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaMaxCardsSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaMaxCardsSkillV2)), 3u);
 types.recordActivateSource<LuaMaxCardsSkillV2>(2948u); AI_NATIVE_REQUIRE_DECLARATION(LuaMaxCardsSkillV2, getBaseAmount, MaxCardsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaMaxCardsSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaMaxCardsSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaTargetModSkill)), 3u);
 types.recordActivateSource<LuaTargetModSkill>(2949u); AI_NATIVE_REQUIRE_DECLARATION(LuaTargetModSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaTargetModSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaTargetModSkillV2)), 3u);
 types.recordActivateSource<LuaTargetModSkillV2>(2950u); AI_NATIVE_REQUIRE_DECLARATION(LuaTargetModSkillV2, getBaseAmount, TargetModSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaTargetModSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaTargetModSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaInvaliditySkill)), 3u);
 types.recordActivateSource<LuaInvaliditySkill>(2951u); AI_NATIVE_REQUIRE_DECLARATION(LuaInvaliditySkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaInvaliditySkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaAttackRangeSkill)), 3u);
 types.recordActivateSource<LuaAttackRangeSkill>(2952u); AI_NATIVE_REQUIRE_DECLARATION(LuaAttackRangeSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaAttackRangeSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaAttackRangeSkillV2)), 3u);
 types.recordActivateSource<LuaAttackRangeSkillV2>(2953u); AI_NATIVE_REQUIRE_DECLARATION(LuaAttackRangeSkillV2, getBaseAmount, AttackRangeSkillV2); AI_NATIVE_REQUIRE_DECLARATION(LuaAttackRangeSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaAttackRangeSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaViewAsEquipSkill)), 3u);
 types.recordActivateSource<LuaViewAsEquipSkill>(2954u); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsEquipSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaViewAsEquipSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaCardLimitSkill)), 3u);
 types.recordActivateSource<LuaCardLimitSkill>(2955u); AI_NATIVE_REQUIRE_DECLARATION(LuaCardLimitSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaCardLimitSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaPreSelectionMetaSkill)), 2u);
 types.recordActivateSource<LuaPreSelectionMetaSkill>(2956u); AI_NATIVE_REQUIRE_DECLARATION(LuaPreSelectionMetaSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaPreSelectionMetaSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaAnytimeSkill)), 2u);
 types.recordActivateSource<LuaAnytimeSkill>(2957u); AI_NATIVE_REQUIRE_DECLARATION(LuaAnytimeSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaAnytimeSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(LuaBattleArraySkill)), 2u);
 types.recordActivateSource<LuaBattleArraySkill>(2958u); AI_NATIVE_REQUIRE_DECLARATION(LuaBattleArraySkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(LuaBattleArraySkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(Skill)), 2u);
 types.recordActivateSource<Skill>(2959u); AI_NATIVE_REQUIRE_DECLARATION(Skill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(Skill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ViewAsSkill)), 2u);
 types.recordActivateSource<ViewAsSkill>(2960u); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(ViewAsSkillV2)), 2u);
 types.recordActivateSource<ViewAsSkillV2>(2961u); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, getBaseAmount, ViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, hasIndependentAIConversion, ViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, targetMode, ViewAsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(ViewAsSkillV2, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(ZeroCardViewAsSkill)), 2u);
 types.recordActivateSource<ZeroCardViewAsSkill>(2962u); AI_NATIVE_REQUIRE_DECLARATION(ZeroCardViewAsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ZeroCardViewAsSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ZeroCardViewAsSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(OneCardViewAsSkill)), 2u);
 types.recordActivateSource<OneCardViewAsSkill>(2963u); AI_NATIVE_REQUIRE_DECLARATION(OneCardViewAsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(OneCardViewAsSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(OneCardViewAsSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(FilterSkill)), 2u);
 types.recordActivateSource<FilterSkill>(2964u); AI_NATIVE_REQUIRE_DECLARATION(FilterSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(FilterSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(FilterSkill, isResponseOrUse, ViewAsSkill);
 types.emplace(std::type_index(typeid(TriggerSkill)), 2u);
 types.recordActivateSource<TriggerSkill>(2965u); AI_NATIVE_REQUIRE_DECLARATION(TriggerSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TriggerSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ScenarioRule)), 2u);
 types.recordActivateSource<ScenarioRule>(2966u); AI_NATIVE_REQUIRE_DECLARATION(ScenarioRule, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ScenarioRule, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(MasochismSkill)), 2u);
 types.recordActivateSource<MasochismSkill>(2967u); AI_NATIVE_REQUIRE_DECLARATION(MasochismSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(MasochismSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(PhaseChangeSkill)), 2u);
 types.recordActivateSource<PhaseChangeSkill>(2968u); AI_NATIVE_REQUIRE_DECLARATION(PhaseChangeSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(PhaseChangeSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(DrawCardsSkill)), 2u);
 types.recordActivateSource<DrawCardsSkill>(2969u); AI_NATIVE_REQUIRE_DECLARATION(DrawCardsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(DrawCardsSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(GameStartSkill)), 2u);
 types.recordActivateSource<GameStartSkill>(2970u); AI_NATIVE_REQUIRE_DECLARATION(GameStartSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(GameStartSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(TriggerSkillV2)), 2u);
 types.recordActivateSource<TriggerSkillV2>(2971u); AI_NATIVE_REQUIRE_DECLARATION(TriggerSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(TriggerSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TriggerSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(RetrialSkill)), 2u);
 types.recordActivateSource<RetrialSkill>(2972u); AI_NATIVE_REQUIRE_DECLARATION(RetrialSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(RetrialSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(SPConvertSkill)), 2u);
 types.recordActivateSource<SPConvertSkill>(2973u); AI_NATIVE_REQUIRE_DECLARATION(SPConvertSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(SPConvertSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ProhibitSkill)), 3u);
 types.recordActivateSource<ProhibitSkill>(2974u); AI_NATIVE_REQUIRE_DECLARATION(ProhibitSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ProhibitSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ProhibitPindianSkill)), 2u);
 types.recordActivateSource<ProhibitPindianSkill>(2975u); AI_NATIVE_REQUIRE_DECLARATION(ProhibitPindianSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ProhibitPindianSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(DistanceSkill)), 3u);
 types.recordActivateSource<DistanceSkill>(2976u); AI_NATIVE_REQUIRE_DECLARATION(DistanceSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(DistanceSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(DistanceSkillV2)), 3u);
 types.recordActivateSource<DistanceSkillV2>(2977u); AI_NATIVE_REQUIRE_DECLARATION(DistanceSkillV2, getBaseAmount, DistanceSkillV2); AI_NATIVE_REQUIRE_DECLARATION(DistanceSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(DistanceSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(MaxCardsSkill)), 3u);
 types.recordActivateSource<MaxCardsSkill>(2978u); AI_NATIVE_REQUIRE_DECLARATION(MaxCardsSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(MaxCardsSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(MaxCardsSkillV2)), 3u);
 types.recordActivateSource<MaxCardsSkillV2>(2979u); AI_NATIVE_REQUIRE_DECLARATION(MaxCardsSkillV2, getBaseAmount, MaxCardsSkillV2); AI_NATIVE_REQUIRE_DECLARATION(MaxCardsSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(MaxCardsSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(TargetModSkill)), 3u);
 types.recordActivateSource<TargetModSkill>(2980u); AI_NATIVE_REQUIRE_DECLARATION(TargetModSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TargetModSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(TargetModSkillV2)), 3u);
 types.recordActivateSource<TargetModSkillV2>(2981u); AI_NATIVE_REQUIRE_DECLARATION(TargetModSkillV2, getBaseAmount, TargetModSkillV2); AI_NATIVE_REQUIRE_DECLARATION(TargetModSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TargetModSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(SlashNoDistanceLimitSkill)), 3u);
 types.recordActivateSource<SlashNoDistanceLimitSkill>(2982u); AI_NATIVE_REQUIRE_DECLARATION(SlashNoDistanceLimitSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(SlashNoDistanceLimitSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(InvaliditySkill)), 3u);
 types.recordActivateSource<InvaliditySkill>(2983u); AI_NATIVE_REQUIRE_DECLARATION(InvaliditySkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(InvaliditySkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(AttackRangeSkill)), 3u);
 types.recordActivateSource<AttackRangeSkill>(2984u); AI_NATIVE_REQUIRE_DECLARATION(AttackRangeSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(AttackRangeSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(AttackRangeSkillV2)), 3u);
 types.recordActivateSource<AttackRangeSkillV2>(2985u); AI_NATIVE_REQUIRE_DECLARATION(AttackRangeSkillV2, getBaseAmount, AttackRangeSkillV2); AI_NATIVE_REQUIRE_DECLARATION(AttackRangeSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(AttackRangeSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ViewAsEquipSkill)), 3u);
 types.recordActivateSource<ViewAsEquipSkill>(2986u); AI_NATIVE_REQUIRE_DECLARATION(ViewAsEquipSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ViewAsEquipSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(CardLimitSkill)), 3u);
 types.recordActivateSource<CardLimitSkill>(2987u); AI_NATIVE_REQUIRE_DECLARATION(CardLimitSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(CardLimitSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(DetachEffectSkill)), 2u);
 types.recordActivateSource<DetachEffectSkill>(2988u); AI_NATIVE_REQUIRE_DECLARATION(DetachEffectSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(DetachEffectSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(WeaponSkill)), 2u);
 types.recordActivateSource<WeaponSkill>(2989u); AI_NATIVE_REQUIRE_DECLARATION(WeaponSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(WeaponSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ArmorSkill)), 2u);
 types.recordActivateSource<ArmorSkill>(2990u); AI_NATIVE_REQUIRE_DECLARATION(ArmorSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ArmorSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(TreasureSkill)), 2u);
 types.recordActivateSource<TreasureSkill>(2991u); AI_NATIVE_REQUIRE_DECLARATION(TreasureSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TreasureSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(EquipSkillV2)), 2u);
 types.recordActivateSource<EquipSkillV2>(2992u); AI_NATIVE_REQUIRE_DECLARATION(EquipSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(EquipSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(EquipSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(WeaponSkillV2)), 2u);
 types.recordActivateSource<WeaponSkillV2>(2993u); AI_NATIVE_REQUIRE_DECLARATION(WeaponSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(WeaponSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(WeaponSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ArmorSkillV2)), 2u);
 types.recordActivateSource<ArmorSkillV2>(2994u); AI_NATIVE_REQUIRE_DECLARATION(ArmorSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(ArmorSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ArmorSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(TreasureSkillV2)), 2u);
 types.recordActivateSource<TreasureSkillV2>(2995u); AI_NATIVE_REQUIRE_DECLARATION(TreasureSkillV2, getBaseAmount, TriggerSkillV2); AI_NATIVE_REQUIRE_DECLARATION(TreasureSkillV2, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(TreasureSkillV2, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(MarkAssignSkill)), 2u);
 types.recordActivateSource<MarkAssignSkill>(2996u); AI_NATIVE_REQUIRE_DECLARATION(MarkAssignSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(MarkAssignSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(PreSelectionMetaSkill)), 2u);
 types.recordActivateSource<PreSelectionMetaSkill>(2997u); AI_NATIVE_REQUIRE_DECLARATION(PreSelectionMetaSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(PreSelectionMetaSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(AnytimeSkill)), 2u);
 types.recordActivateSource<AnytimeSkill>(2998u); AI_NATIVE_REQUIRE_DECLARATION(AnytimeSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(AnytimeSkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(BattleArraySkill)), 2u);
 types.recordActivateSource<BattleArraySkill>(2999u); AI_NATIVE_REQUIRE_DECLARATION(BattleArraySkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(BattleArraySkill, getMaxUsageLimit, Skill);
 types.emplace(std::type_index(typeid(ArraySummonSkill)), 2u);
 types.recordActivateSource<ArraySummonSkill>(3000u); AI_NATIVE_REQUIRE_DECLARATION(ArraySummonSkill, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ArraySummonSkill, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ArraySummonSkill, isResponseOrUse, ViewAsSkill);
}

extern void registerAINativeTypes_src_package_ikai_do_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ikai_ka_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ikai_kin_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ikai_moku_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ikai_sui_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_bangai_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_hana_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_kami_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_kaze_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_shin_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_sp_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_tsuki_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_touhou_yuki_cpp(AINativeTypes&);

extern void registerAINativeTypes_src_client_aux_skills_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_assassins_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_bgm_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_boss_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_doudizhu_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_dream_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_exclusive_cards_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_fire_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_godlailailai_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_formation_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_lord_ex_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_mol_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_momentum_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_newsgs_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_power_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_rule_cards_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_standard_equips_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_standard_qun_generals_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_standard_shu_generals_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_standard_wei_generals_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_standard_wu_generals_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_strategic_advantage_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_h_transformation_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_happy2v2_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_hayate_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_inovation_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_jiange_defense_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_joy_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_lei_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ling_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_maneuvering_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_maotu_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_meleemode_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_mobile_strengthen_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_mobile_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_mobilemougong_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_mobileshiji_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_mountain_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ol_strengthen_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_ol_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_olwenwu_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_sp_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_special1v1_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_special3v3_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_standard_cards_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_standard_generals_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_standard_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_tenshi_reihou_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_tenyear_strengthen_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_tenyear_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_tenyear2_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_thicket_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_tw_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_wind_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_wisdom_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yczh2016_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yczh2017_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yin_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yingbian_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yinhu_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yitian_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2012_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2013_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2014_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2015_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2022_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_yjcm2023_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_package_zombine_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_boss_mode_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_challengedeveloper_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_couple_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_fancheng_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_guandu_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_miniscenarios_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_scenerule_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_takeover_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_scenario_zombie_scenario_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_server_ai_strategy_native_types_cpp(AINativeTypes&);
extern void registerAINativeTypes_src_server_gamerule_cpp(AINativeTypes&);
static const AINativeTypes &aiAllNativeTypes(){
 static const AINativeTypes types=[](){
 AINativeTypes types;types.reserve(5000);
 registerAINativeTypes_src_package_ikai_do_cpp(types);
 registerAINativeTypes_src_package_ikai_ka_cpp(types);
 registerAINativeTypes_src_package_ikai_kin_cpp(types);
 registerAINativeTypes_src_package_ikai_moku_cpp(types);
 registerAINativeTypes_src_package_ikai_sui_cpp(types);
 registerAINativeTypes_src_package_touhou_bangai_cpp(types);
 registerAINativeTypes_src_package_touhou_hana_cpp(types);
 registerAINativeTypes_src_package_touhou_kami_cpp(types);
 registerAINativeTypes_src_package_touhou_kaze_cpp(types);
 registerAINativeTypes_src_package_touhou_shin_cpp(types);
 registerAINativeTypes_src_package_touhou_sp_cpp(types);
 registerAINativeTypes_src_package_touhou_tsuki_cpp(types);
 registerAINativeTypes_src_package_touhou_yuki_cpp(types);
 registerAINativeTypes_src_client_aux_skills_cpp(types);
 registerAINativeTypes_src_package_assassins_cpp(types);
 registerAINativeTypes_src_package_bgm_cpp(types);
 registerAINativeTypes_src_package_boss_cpp(types);
 registerAINativeTypes_src_package_doudizhu_cpp(types);
 registerAINativeTypes_src_package_dream_cpp(types);
 registerAINativeTypes_src_package_exclusive_cards_cpp(types);
 registerAINativeTypes_src_package_fire_cpp(types);
 registerAINativeTypes_src_package_godlailailai_cpp(types);
 registerAINativeTypes_src_package_h_formation_cpp(types);
 registerAINativeTypes_src_package_h_lord_ex_cpp(types);
 registerAINativeTypes_src_package_h_mol_cpp(types);
 registerAINativeTypes_src_package_h_momentum_cpp(types);
 registerAINativeTypes_src_package_h_newsgs_cpp(types);
 registerAINativeTypes_src_package_h_power_cpp(types);
 registerAINativeTypes_src_package_h_rule_cards_cpp(types);
 registerAINativeTypes_src_package_h_standard_equips_cpp(types);
 registerAINativeTypes_src_package_h_standard_qun_generals_cpp(types);
 registerAINativeTypes_src_package_h_standard_shu_generals_cpp(types);
 registerAINativeTypes_src_package_h_standard_wei_generals_cpp(types);
 registerAINativeTypes_src_package_h_standard_wu_generals_cpp(types);
 registerAINativeTypes_src_package_h_strategic_advantage_cpp(types);
 registerAINativeTypes_src_package_h_transformation_cpp(types);
 registerAINativeTypes_src_package_happy2v2_cpp(types);
 registerAINativeTypes_src_package_hayate_cpp(types);
 registerAINativeTypes_src_package_inovation_cpp(types);
 registerAINativeTypes_src_package_jiange_defense_cpp(types);
 registerAINativeTypes_src_package_joy_cpp(types);
 registerAINativeTypes_src_package_lei_cpp(types);
 registerAINativeTypes_src_package_ling_cpp(types);
 registerAINativeTypes_src_package_maneuvering_cpp(types);
 registerAINativeTypes_src_package_maotu_cpp(types);
 registerAINativeTypes_src_package_meleemode_cpp(types);
 registerAINativeTypes_src_package_mobile_strengthen_cpp(types);
 registerAINativeTypes_src_package_mobile_cpp(types);
 registerAINativeTypes_src_package_mobilemougong_cpp(types);
 registerAINativeTypes_src_package_mobileshiji_cpp(types);
 registerAINativeTypes_src_package_mountain_cpp(types);
 registerAINativeTypes_src_package_ol_strengthen_cpp(types);
 registerAINativeTypes_src_package_ol_cpp(types);
 registerAINativeTypes_src_package_olwenwu_cpp(types);
 registerAINativeTypes_src_package_sp_cpp(types);
 registerAINativeTypes_src_package_special1v1_cpp(types);
 registerAINativeTypes_src_package_special3v3_cpp(types);
 registerAINativeTypes_src_package_standard_cards_cpp(types);
 registerAINativeTypes_src_package_standard_generals_cpp(types);
 registerAINativeTypes_src_package_standard_cpp(types);
 registerAINativeTypes_src_package_tenshi_reihou_cpp(types);
 registerAINativeTypes_src_package_tenyear_strengthen_cpp(types);
 registerAINativeTypes_src_package_tenyear_cpp(types);
 registerAINativeTypes_src_package_tenyear2_cpp(types);
 registerAINativeTypes_src_package_thicket_cpp(types);
 registerAINativeTypes_src_package_tw_cpp(types);
 registerAINativeTypes_src_package_wind_cpp(types);
 registerAINativeTypes_src_package_wisdom_cpp(types);
 registerAINativeTypes_src_package_yczh2016_cpp(types);
 registerAINativeTypes_src_package_yczh2017_cpp(types);
 registerAINativeTypes_src_package_yin_cpp(types);
 registerAINativeTypes_src_package_yingbian_cpp(types);
 registerAINativeTypes_src_package_yinhu_cpp(types);
 registerAINativeTypes_src_package_yitian_cpp(types);
 registerAINativeTypes_src_package_yjcm_cpp(types);
 registerAINativeTypes_src_package_yjcm2012_cpp(types);
 registerAINativeTypes_src_package_yjcm2013_cpp(types);
 registerAINativeTypes_src_package_yjcm2014_cpp(types);
 registerAINativeTypes_src_package_yjcm2015_cpp(types);
 registerAINativeTypes_src_package_yjcm2022_cpp(types);
 registerAINativeTypes_src_package_yjcm2023_cpp(types);
 registerAINativeTypes_src_package_zombine_cpp(types);
 registerAINativeTypes_src_scenario_boss_mode_scenario_cpp(types);
 registerAINativeTypes_src_scenario_challengedeveloper_scenario_cpp(types);
 registerAINativeTypes_src_scenario_couple_scenario_cpp(types);
 registerAINativeTypes_src_scenario_fancheng_scenario_cpp(types);
 registerAINativeTypes_src_scenario_guandu_scenario_cpp(types);
 registerAINativeTypes_src_scenario_miniscenarios_cpp(types);
 registerAINativeTypes_src_scenario_scenerule_cpp(types);
 registerAINativeTypes_src_scenario_takeover_scenario_cpp(types);
 registerAINativeTypes_src_scenario_zombie_scenario_cpp(types);
 registerAINativeTypes_src_server_ai_strategy_native_types_cpp(types);
 registerAINativeTypes_src_server_gamerule_cpp(types);
 return types;}();
 return types;
}
unsigned aiNativeEffectClass(const Skill*s){
 if(!s)return 0;
 const auto &types=aiAllNativeTypes();
 const auto i=types.find(std::type_index(typeid(*s)));return i==types.end()?0:i->second;
}
#include "ai-strategy/native-types-activate-rows.inc"
const AIActivateNativeTypeSource *aiActivateNativeTypeSource(const Skill *s) {
 if(!s)return nullptr;
 const auto &types=aiAllNativeTypes();
 const auto it=types.activateSourceIds.find(std::type_index(typeid(*s)));
 if(it==types.activateSourceIds.end() || it->second>=sizeof(activateTypeSources)/sizeof(activateTypeSources[0]))return nullptr;
 return &activateTypeSources[it->second];
}
#include "ai-strategy/native-types-activate-proof.inc"

// ---- public-skill-proof ----
#include "engine.h"
#include "skill-instance-utils.h"
#include <QMetaObject>
#include <QVariant>
namespace {
// Public definition identity, not a private hasSkill result. There is no Lua,
// native correction, event emission or yield between proof and source branch.
// QObject dynamic properties cannot invoke authored property getters. Declared
// Q_PROPERTY values and custom QVariant conversions are deliberately refused.
bool ignore(const Skill*s,bool&safe){
 safe=false;if(!s||s->metaObject()->indexOfProperty("IgnoreInvalidity")>=0)return false;
 const QVariant v=s->QObject::property("IgnoreInvalidity");
 if(!v.isValid()){safe=true;return false;}
 if(v.userType()!=QMetaType::Bool)return false;
 safe=true;return v.toBool();
}
const Skill*definition(const QString&name){QString base;SkillInstanceUtils::parseName(name,base);return Sanguosha->getSkill(base);}
bool has(const Skill*s){
 // A publicly absent definition returns after the SSOT ownership test, before
 // Invalidity in every possible private ownership branch.
 if(!s)return true;
 if(s->isAttachedLordSkill())return true; // first source OR operand, no property read
 bool safe=false;const bool ignored=ignore(s,safe);
 return safe&&(ignored||!s->isVisible());
}
bool current(const Skill*s){return !s||Sanguosha->getSkill(s->objectName())==s;}
}
bool aiPublicHasSkill(const QString&name,bool compound) noexcept {
 try{if(!Sanguosha||!Sanguosha->currentRoom())return false;
  if(!compound)return has(definition(name));
  // Prove the complete public conjunction/disjunction union; never inspect
  // which branch the player's private ownership would short circuit.
  for(const auto&term:name.split('|'))for(const auto&part:term.split('+'))if(!has(definition(part)))return false;
  return true;
 }catch(...){return false;}
}
bool aiPublicHasSkill(const Skill*s,bool includeLose) noexcept {
 try{return Sanguosha&&Sanguosha->currentRoom()&&current(s)&&(!s||(!s->isLordSkill()&&(includeLose||has(s))));}catch(...){return false;}
}
bool aiPublicIsSkillInvalid(const Skill*s) noexcept {
 try{if(!Sanguosha||!Sanguosha->currentRoom()||!current(s))return false;
  if(!s)return true;bool safe=false;return ignore(s,safe)&&safe;
 }catch(...){return false;}
}
bool aiPublicIsSkillInvalid(const QString&name) noexcept {
 try{return Sanguosha&&Sanguosha->currentRoom()&&aiPublicIsSkillInvalid(Sanguosha->getSkill(name));}catch(...){return false;}
}

// ---- getter-correction-proof ----
#include "ai-strategy/lua-seal.h"
#include "lua.hpp"
#include <QStringList>
#include <cstring>
#include "ai-strategy/lua-raw-inspect.h"
extern "C" {
#include "lobject.h"
#include "lopcodes.h"
}
namespace {
struct Row{const char*path;const char*sha;int first,last;AIGetterCorrectionArguments arguments;const char*methods;};
const Row rows[]={
#include "ai-strategy/getter-correction-rows.inc"
};
struct Stack{lua_State*L;int n;~Stack(){lua_settop(L,n);}};
bool getterCorrectionProofFail(QString*r,const QString&s){if(r)*r=s;return false;}
bool bindings(Room&r,lua_State*L,const Row&row,QString*why){
 // These bodies use only ordinary primitive conditions/arithmetic/concatenation
 // and listed native getters. No Lua global lookup or table/object assignment.
 for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER})if(AILuaInspect::primitiveMetatable(L,kind))return getterCorrectionProofFail(why,"correction primitive metamethod replaced");
 for(const auto&name:QString::fromLatin1(row.methods).split('|',Qt::SkipEmptyParts)){
  if(name=="getPrimary"||name=="getSecondary"||name=="getHolder"){
   if(!matchAINativeClassMethod(r,L,"CorrectSkillContext",name))return getterCorrectionProofFail(why,"correction context getter changed: "+name);
  }else if(name=="getMark"){
   if(!matchAINativeDerivedMethods(r,L,"Player",{name}))return getterCorrectionProofFail(why,"correction Player.getMark changed");
  }else if(name=="hasFlag"||name=="faceUp"||name=="isKongcheng"||name=="getGeneralName"||name=="getGeneral2Name"){
   // Whole native bodies read fixed fields/containers only. isKongcheng calls
   // the sealed native getHandcardNum; actual V2 caller supplies ServerPlayer.
   if(!matchAINativeDerivedMethods(r,L,"Player",{name}))return getterCorrectionProofFail(why,"correction pure native getter changed: "+name);
   if(name=="isKongcheng"&&!matchAINativeDerivedMethods(r,L,"Player",{"getHandcardNum"}))return getterCorrectionProofFail(why,"correction hand-count getter changed");
  }else if(name=="objectName"){
   if(!matchAINativeDerivedMethods(r,L,"Player",{name})||!matchAINativeDerivedMethods(r,L,"Skill",{name}))return getterCorrectionProofFail(why,"correction native objectName changed");
  }else return getterCorrectionProofFail(why,"correction unaudited getter");
 }
 return true;
}
}
bool registerAIGetterCorrectionSources(Room&r,lua_State*L,QString*why){
 QStringList done;for(const auto&row:rows){const auto path=QString::fromLatin1(row.path);if(done.contains(path))continue;
  if(!registerAIPinnedSource(r,L,path,row.sha,why))return false;done<<path;
 }return true;
}
bool proveAIGetterCorrection(Room&r,lua_State*L,int index,AIGetterCorrectionArguments arguments,QString*why){
 if(!L||!AILuaInspect::capacity(L,128))return getterCorrectionProofFail(why,"correction inspection stack unavailable");
 Stack stack{L,lua_gettop(L)};index=lua_absindex(L,index);
 if(!lua_isfunction(L,index)||lua_iscfunction(L,index))return getterCorrectionProofFail(why,"correction not a Lua function");
 const Row*row=nullptr;for(const auto&candidate:rows)if(candidate.arguments==arguments&&matchAIPinnedFunction(r,L,index,candidate.path,candidate.first,candidate.last,nullptr)){row=&candidate;break;}
 if(!row)return getterCorrectionProofFail(why,"correction outside complete pinned getter-only subtype");
 const auto*closure=static_cast<const LClosure*>(lua_topointer(L,index));const Proto*p=closure->p;
 if(p->sizep||p->is_vararg||p->numparams!=(arguments==AIGetterCorrectionArguments::Invalidity?3:2))return getterCorrectionProofFail(why,"correction argument/nested closure shape changed");
 for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
  if(std::strcmp(name,"_ENV"))return getterCorrectionProofFail(why,"correction captured non-environment state");
  AILuaInspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);
  if(!same)return getterCorrectionProofFail(why,"correction environment replaced");
 }
 for(int i=0;i<p->sizecode;++i){const auto op=GET_OPCODE(p->code[i]);
  if(op==OP_GETTABUP||op==OP_GETUPVAL||op==OP_SETUPVAL||op==OP_SETTABUP
     ||op==OP_SETTABLE||op==OP_SETI||op==OP_SETFIELD||op==OP_CLOSURE)
   return getterCorrectionProofFail(why,"correction contains external read/write or nested closure");
 }
 return bindings(r,L,*row,why);
}

bool certifyAIGetterCorrectionEntry(Room&r,lua_State*L,int nargs,QString*why){
 // This is the exact callable-plus-arguments tuple immediately before
 // LuaRuntime::protectedCall. Extra lower stack values belong to its caller.
 // Do not infer an event, skill name or saved-slot identity from this tuple.
 if(!L||!AILuaInspect::capacity(L,2048)||(nargs!=2&&nargs!=3))
  return getterCorrectionProofFail(why,"correction entry signature/inspection capacity unavailable");
 const int top=lua_gettop(L),fn=top-nargs;
 if(fn<1)return getterCorrectionProofFail(why,"correction entry callable missing");
 Stack stack{L,top};
 const auto object=[&](int i,const QString&base,const QStringList&methods){
  return matchAINativeObjectMethods(r,L,i,base,methods);
 };
 if(nargs==3){
  // swig/luaskills.i LuaInvaliditySkill::isSkillValid supplies these
  // exact three objects. Null Player/Skill pointers become nil; the pinned
  // podai body explicitly accepts both and has the same whole-effect proof.
  if(!object(fn+1,"LuaInvaliditySkill",{"objectName"})
     ||(!lua_isnil(L,fn+2)&&!object(fn+2,"Player",{"getMark"}))
     ||(!lua_isnil(L,fn+3)&&!object(fn+3,"Skill",{"objectName"})))
   return getterCorrectionProofFail(why,"correction invalidity argument binding changed");
  return proveAIGetterCorrection(r,L,fn,AIGetterCorrectionArguments::Invalidity,why);
 }
 // The V2 native wrapper passes the context by borrowed userdata. Validate
 // its captured class/metatable/getter routes, without fetching primary,
 // secondary or holder; the complete body proof seals their Player methods.
 if(!object(fn+1,"LuaDistanceSkillV2",{"objectName"})
    ||!object(fn+2,"CorrectSkillContext",{"getPrimary","getSecondary","getHolder"}))
  return getterCorrectionProofFail(why,"correction distance argument binding changed");
 return proveAIGetterCorrection(r,L,fn,AIGetterCorrectionArguments::DistanceV2,why);
}

// ---- player-correction ----
#include "maneuvering.h"
#include "tenyear.h"
#include "wrapped-card.h"
#include "ai-strategy/runtime.h"
#include "ai-strategy/xufa.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "room-definition-registry.h"
#include "serverplayer.h"
#include "player-initial-source-origin.h"
#include "card-lifetime-manager.h"
#include "package.h"
#include "settings.h"
#include "server-info.h"
#include <QPointer>
#include <QHash>
#include <QSet>
#include <cstdio>
#include <typeinfo>
namespace {
thread_local AIMaxCardsScope *activeMaxCards=nullptr;
struct PlayerCorrectionStack { lua_State *L;int top;~PlayerCorrectionStack(){lua_settop(L,top);} };
void traceCorrection(int root,const char *phase,const QString &reason=QString()) {
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
  std::fprintf(stderr,"%s phase=%s reason=%s\n",root==18?"R56_ZUNWEI_SEED_SCOPE":root==17?"R56_OWNER_EQUIPMENT_QUERY_SCOPE":root==16?"R56_PUBLIC_ATTACK_RANGE_MATRIX_SCOPE":root==15?"R56_OWNER_PINDIAN_SCOPE":root==14?"R56_OWNER_JILEI_SCOPE":root==13?"R56_WORLD_SKILL_QUERY_SCOPE":root==12?"R56_OWNER_CARD_STATE_SCOPE":root==11?"R56_PUBLIC_SKILL_QUERY_SCOPE":root==10?"R56_SKILL_VALIDITY_SCOPE":root==9?"R56_CONNECTION_QUERY_SCOPE":root==8?"R56_OWNER_CANDIDATES_SCOPE":root==7?"R56_OWNER_EVALUATIONS_SCOPE":root==6?"R56_FIXED_SLASH_PROBES_SCOPE":root==5?"R56_OWNER_ANALEPTIC_SCOPE":root==4?"R56_OWNER_SLASH_SCOPE":root==3?"R56_DISTANCE_SCOPE":root==2?"R56_OWNER_CARD_RULES_SCOPE":root==1?"R56_ATTACK_RANGE_SCOPE":"R56_MAXCARDS_SCOPE",phase,qPrintable(reason));std::fflush(stderr);
 }
}
}
struct AIMaxCardsScope::Data {
 Room &room;lua_State *L;AIMaxCardsScope *previous;
 QPointer<ServerPlayer> target;QList<QPointer<ServerPlayer>> roster;
 quint64 generation=0,mapGeneration=0,weaponRevision=0;
 bool ready=false;Root root;
 bool cardRulesRoot()const noexcept{return root==OwnerCardRules||root==OwnerSlashWithoutCrossbow||root==OwnerAnaleptic||root==FixedSlashProbes||root==FixedOwnerEvaluations||root==OwnerPhysicalCandidates||root==OwnerCardState||root==OwnerJilei||root==ZunweiSeed;}
 QHash<const void *,QSet<int>> entries;
 QHash<const Card *,QPointer<const Card>> policyCards;
 QHash<const WrappedCard *,QPointer<const Card>> physicalFaces;
 QSet<QString> literalFactories;
 std::unique_ptr<CardLifetimeScope> lifetime;
 std::unique_ptr<AIAsyncUniverseScope> async;
 Data(Room&r,ServerPlayer*p,lua_State*l,Root root):room(r),L(l),previous(activeMaxCards),target(p),root(root){}
 bool receiverOrigins() const noexcept {
  if(!room.roomState()->aiPhysicalIdMapSourceOrigin())return false;
  for(const auto &p:roster)if(!p||p->getRoom()!=&room||p->parent()!=&room
   ||!hasAIPlayerCanonicalHandOrigin(*p)
   ||!hasAIPlayerEmptyPublicCardsOrigin(*p)
   ||!hasAIPlayerEmptyLimitationsOrigin(*p))return false;
  return true;
 }
};
AIMaxCardsScope::AIMaxCardsScope(Room&r,ServerPlayer*p,lua_State*L)
 :AIMaxCardsScope(r,p,L,MaxCardsFixedExtra){}
AIMaxCardsScope::AIMaxCardsScope(Room&r,ServerPlayer*p,lua_State*L,Root root):d(new Data(r,p,L,root)) {
 activeMaxCards=this;
 const auto fail=[&](const QString &why){d->ready=false;traceCorrection(d->root,"refused",why);retireAIProjectionSource(r,L);};
 try {
  auto *rt=r.roomRuntime();
  if(!rt||rt->isClosing()||!p||typeid(*p)!=typeid(ServerPlayer)||p->getRoom()!=&r||p->parent()!=&r
   ||!L||rt->lua().rawState()!=L||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
   ||!Sanguosha->currentRoomContext()||Sanguosha->currentRoomContext()->roomRuntime()!=rt
   ||(r.getMode()!="02p"&&r.getMode()!="04p")||Config.EnableHegemony||ServerInfo.EnableHegemony||r.getScenario()) {
   fail("original normal Game VM receiver domain");return;
  }
  d->lifetime.reset(new CardLifetimeScope(globalCardLifetimeManager(),root==ZunweiSeed?"R56ZunweiSeed":root==OwnerEquipmentQueries?"R56OwnerEquipmentQueries":root==PublicAttackRangeMatrix?"R56PublicAttackRangeMatrix":root==OwnerPindian?"R56OwnerPindian":root==OwnerJilei?"R56OwnerJilei":root==WorldSkillQueries?"R56WorldSkillQueries":root==OwnerCardState?"R56OwnerCardState":root==PublicSkillQueries?"R56PublicSkillQueries":root==PlayerSkillValidity?"R56SkillValidity":root==ConnectionQuery?"R56ConnectionQuery":root==OwnerPhysicalCandidates?"R56OwnerCandidates":root==FixedOwnerEvaluations?"R56OwnerEvaluations":root==FixedSlashProbes?"R56FixedSlashProbes":root==OwnerAnaleptic?"R56OwnerAnaleptic":root==OwnerSlashWithoutCrossbow?"R56OwnerSlash":root==PublicDistances?"R56PublicDistances":root==OwnerCardRules?"R56OwnerCardRules":root==AttackRangeFixedExtra?"R56AttackRangeFixedExtra":"R56MaxCardsFixedExtra"));
  if(!hasAICloneInputLineage(r,L)||!hasAIProjectionLineage(r,L)
   ||!hasAIAsyncUniverse(r,L)||!proveAIServerLogEffects(r)
   ||!rt->definitions().hasFactorySourceOrigin(r,L)||!hasAIGlobalFactoryMapOrigin(Sanguosha)
   ||!lua_checkstack(L,8192)) {fail("complete bootstrap/native map/async source absent");return;}
  // The 04p source domain has its own bootstrap-issued mode lineage. Prove
  // its complete callback union independently of the selected skill/holder.
  if(r.getMode()=="04p"){
   QString why;if(!hasAIModeEvaluationLineage(r,L)||!proveAIModeEvaluationUnion(r,L,&why)){
    fail(why.isEmpty()?QStringLiteral("ordinary identity mode source union"):why);return;
   }
  }
  d->generation=rt->lua().generation();d->mapGeneration=r.roomState()->aiPhysicalIdMapGeneration();
  d->weaponRevision=Weapon::rangeRevision();
  // All public definitions and all necessary callback roles are proved before
  // any holder/instance/mark branch. No selected skill list shrinks this union.
  const auto family=[&](const auto&skills){for(const auto*s:skills)if(!s||(aiNativeEffectClass(s)&1u)==0)return false;return true;};
  if(!family(Sanguosha->getMaxCardsSkills())||!family(Sanguosha->getDistanceSkills())
   ||!family(Sanguosha->getAttackRangeSkills())||!family(Sanguosha->getInvaliditySkills())
   ||!family(Sanguosha->getViewAsEquipSkills())) {fail("unreviewed complete correction family");return;}
  if(root==OwnerPindian) {
   // Complete public native family, before canPindian examines either hand or
   // except-self branch. Only the audited Qianjie body (bit4) is currently
   // closed; a new Lua/native Pindian provider fails the whole root.
   for(const auto *skill:Sanguosha->getProhibitPindianSkills())
    if(!skill||(aiNativeEffectClass(skill)&4u)==0) {
     fail("unreviewed complete Pindian prohibit family");return;
    }
  }
  if(d->cardRulesRoot()&&(!family(Sanguosha->getProhibitSkills())
   ||!family(Sanguosha->getTargetModSkills())||!family(Sanguosha->getCardLimitSkills())
   ||!hasAICardPatternsLineage(r,L))) {fail("complete owner-card native correction/pattern family");return;}
  if(root==ConnectionQuery||root==PlayerSkillValidity||root==PublicSkillQueries||root==WorldSkillQueries||root==OwnerPindian||root==PublicAttackRangeMatrix||root==OwnerEquipmentQueries||root==ZunweiSeed) {
   // Cache export and validity dispatch use registered public metadata,
   // including getFrequency/isEquipSkill. Close every implementation
   // independently of the selected skill, holder or applicability branch.
   // No getUsageRef/getAmountRef/activation provider is invoked by this graph.
   for(const QString &name:Sanguosha->getSkillNames()) {
    const Skill *skill=Sanguosha->getSkill(name);
    if(!skill||(aiNativeEffectClass(skill)&2u)==0) {
     fail("complete public connection metadata family");return;
    }
   }
  }
  QString why;QList<LuaFunction> getterFamily;
  const auto capture=[&](const LuaFunction &field,int arguments){
   const LuaFunction fn(field);if(fn==0)return true;
   if(fn.state()!=L||fn.generation()!=d->generation)return false;
   PlayerCorrectionStack stack{L,lua_gettop(L)};AILuaInspect::rawGeti(L,LUA_REGISTRYINDEX,fn.registryRef());
   if(!lua_isfunction(L,-1))return false;
   getterFamily.append(fn);
   // Membership is inert until the COMPLETE source family and all remaining
   // native/custody/async checks succeed below (d->ready is still false).
   d->entries[lua_topointer(L,-1)].insert(arguments);return true;
  };
  bool callbacks=true;
  for(const auto*s:Sanguosha->getMaxCardsSkills()) {
   if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s))callbacks=capture(x->extra_func,2)&&capture(x->fixed_func,2)&&callbacks;
  }
  for(const auto*s:Sanguosha->getDistanceSkills()) {
   if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s))callbacks=capture(x->correct_func,3)&&capture(x->fixed_func,3)&&callbacks;
  }
  for(const auto*s:Sanguosha->getAttackRangeSkills()) {
   if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s))callbacks=capture(x->correct_func,2)&&capture(x->fixed_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s))callbacks=capture(x->extra_func,3)&&capture(x->fixed_func,3)&&callbacks;
  }
  for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s))callbacks=capture(x->skill_valid,3)&&callbacks;
  for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s))callbacks=capture(x->view_as_equip,2)&&callbacks;
  if(d->cardRulesRoot()) {
   for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s))callbacks=capture(x->is_prohibited,5)&&callbacks;
   for(const auto*s:Sanguosha->getTargetModSkills()) {
    if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))callbacks=capture(x->correct_func,2)&&callbacks;
    else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s))callbacks=capture(x->residue_func,4)&&capture(x->distance_limit_func,4)&&capture(x->extra_target_func,3)&&callbacks;
   }
   for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s))
    callbacks=capture(x->limit_list,3)&&capture(x->limit_pattern,3)&&capture(x->limit_reason,3)&&callbacks;
  }
  // Full Room-owned horse definition family, including clone getCorrect.
  // Global HorseSkill uses nonvirtual Horse::getCorrect through native base
  // pointers: private correction is constructor-only, no Lua field dispatch.
  for(const auto*package:rt->packages())for(const Card*c:package->findChildren<Card*>()) {
   if(auto*x=dynamic_cast<const LuaHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
   else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(c))callbacks=capture(x->correct_func,2)&&callbacks;
  }
  if(callbacks)callbacks=proveAIActivateGetterFamily(r,L,getterFamily,&why);
  if(!callbacks){fail(why.isEmpty()?QStringLiteral("complete correction closure/VM family"):why);return;}
  const auto factories=inspectAIPublicFactorySources(r);
  if(!factories.enumerationComplete||!factories.equipmentRecipeFamilyComplete) {fail("equipment resolver source incomplete");return;}
  if(root==ZunweiSeed&&!factories.zunweiSkillRecipeComplete) {
   fail("fixed native ZunweiCard factory alternatives differ");return;
  }
  const auto players=r.getAllPlayers(true);
  if(!players.contains(p)){fail("target outside source Room roster");return;}
  for(auto*receiver:players) {
   if(!receiver||typeid(*receiver)!=typeid(ServerPlayer)){fail("unreviewed native Player receiver");return;}
   d->roster.append(receiver);
  }
  // These are constructor/writer histories, not current empty-card/limit tests.
  // Canonical physical ID excludes virtual-material hand suit dispatch; all
  // other receiver reads are confined by the Game VM and Card lifetime scope.
  if(!d->receiverOrigins()){fail("complete canonical hand/public-card/limitation receiver origins");return;}
  if(d->cardRulesRoot()) {
   // Prove the entire fixed public factory domain before looking at owner
   // material. Runtime maps already have original, monotonic source history.
   QStringList factoryNames{"jink","slash","peach","fire_slash","thunder_slash"};
   if(root==FixedOwnerEvaluations)factoryNames<<"analeptic"<<"dismantlement";
   if(root==OwnerPhysicalCandidates||root==OwnerCardState||root==ZunweiSeed)factoryNames.clear();
   if(root==OwnerJilei)factoryNames=QStringList{QStringLiteral("ex_nihilo")};
   for(const QString&name:factoryNames) {
    if(!Sanguosha->aiStandardCloneFactory(name)){fail("original fixed owner-rule factory recipe differs");return;}
    d->literalFactories.insert(name);
   }
   // Canonical hand source + Room-owned physical map is the custody issuer;
   // native type membership only closes virtual dispatch, never issues custody.
   if(root!=ZunweiSeed)for(const Card *card:p->getCards("he")) {
    if(!r.roomState()->aiPhysicalIdSourceContains(card)
     ||!aiOrdinaryCardPolicyType(card)){fail("owner policy receiver outside canonical native family");return;}
    d->policyCards.insert(card,QPointer<const Card>(card));
    if(typeid(*card)!=typeid(WrappedCard)){fail("physical policy root is not the original wrapper");return;}
    const auto *wrapper=static_cast<const WrappedCard *>(card);
    const Card *face=wrapper->aiPhysicalCloneOwnedFaceOrigin();
    if(!face||!aiOrdinaryCardPolicyType(face)){fail("owned physical policy face source unavailable");return;}
    d->physicalFaces.insert(wrapper,QPointer<const Card>(face));
    d->policyCards.insert(face,QPointer<const Card>(face));
   }
  }
  // Constructors are exact equipment recipes, IDs atomic; LuaFunction copies
  // only snapshot scalar tuples. Fresh products have no parent/connections;
  // deleteLater and approved userdata cleanup enqueue domain retirement. The
  // active CardLifetimeScope prevents reclamation, and this graph pumps no events.
  d->async.reset(new AIAsyncUniverseScope(r,L,AIAsyncUniverseScope::GameplayVariants));
  d->ready=d->async->accepted();
  if(!accepted()){fail("entry continuity invalidated");return;}
  traceCorrection(d->root,"entered");
 }catch(...){fail("native preflight allocation failure");}
}
AIMaxCardsScope::~AIMaxCardsScope() {
 const bool wasReady=d->ready;
 if(wasReady&&(!accepted()||!d->receiverOrigins()))retireAIProjectionSource(d->room,d->L);
 if(wasReady)traceCorrection(d->root,accepted()?"completed":"retired");
 activeMaxCards=d->previous;
}
bool AIMaxCardsScope::accepted() const noexcept {
 // Recheck the original custody receipt, not a sampled getRealCard equality.
 // QPointers fail before dereferencing a retired wrapper or inner object.
 for(auto it=d->physicalFaces.cbegin();it!=d->physicalFaces.cend();++it)
  if(d->policyCards.value(it.key()).data()!=it.key()||!it.value()
    ||it.key()->aiPhysicalCloneOwnedFaceOrigin()!=it.value().data())return false;
 auto *rt=d->room.roomRuntime();return d->ready&&d->target&&rt&&!rt->isClosing()
  &&LuaRuntime::current()==&rt->lua()&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->mapGeneration==d->room.roomState()->aiPhysicalIdMapGeneration()
  &&Weapon::rangeRevisionCurrent(d->weaponRevision)
  &&rt->definitions().hasFactorySourceOrigin(d->room,d->L)
  &&hasAIGlobalFactoryMapOrigin(Sanguosha)
  &&hasAIProjectionLineage(d->room,d->L)&&hasAICloneInputLineage(d->room,d->L)
  &&hasAIAsyncUniverse(d->room,d->L)
  &&(!d->cardRulesRoot()||hasAICardPatternsLineage(d->room,d->L));
}
bool acceptAIPlayerCorrectionGetterEntry(Room&r,lua_State*L,int n) noexcept {
 if(!activeMaxCards||&activeMaxCards->d->room!=&r||activeMaxCards->d->L!=L
  ||n<1||lua_gettop(L)<n+1||!activeMaxCards->accepted())return false;
 const auto i=activeMaxCards->d->entries.constFind(lua_topointer(L,-n-1));
 return i!=activeMaxCards->d->entries.cend()&&i->contains(n);
}
bool preserveAIPlayerCorrectionNativeQuery(Room&r,const Card*card,bool hasCard) noexcept {
 if(!activeMaxCards||&activeMaxCards->d->room!=&r||!activeMaxCards->accepted())return false;
 if(!hasCard)return true;
 if(!activeMaxCards->d->cardRulesRoot())return false;
 const auto it=activeMaxCards->d->policyCards.constFind(card);
 return it!=activeMaxCards->d->policyCards.cend()&&it.value().data()==card;
}
AICardRulesCloneBirth::AICardRulesCloneBirth(const Engine *engine,const Card *card,const QString &name) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||!scope->d->cardRulesRoot()||!card
  ||engine!=Sanguosha||!scope->accepted()||!scope->d->literalFactories.contains(name))return;
 try {
  // This constructor can only be called by Engine itself, immediately after
  // original clone completion. No current ID/name equality supplies authority.
  if(!aiOrdinaryCardPolicyType(card)){
   retireAIProjectionSource(scope->d->room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIProjectionSource(scope->d->room,scope->d->L);}
}

AIPlayerSlashBirth::AIPlayerSlashBirth(Room &room,const Player *owner,const Card *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIMaxCardsScope::OwnerSlashWithoutCrossbow
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Issued before the original source copies getEquips(0). The whole roster's
  // original empty-public-card writer histories exclude material recursion.
  // The private constructor is called only at the intrinsic new Slash site.
  if(!aiOrdinaryCardPolicyType(card)){
   retireAIProjectionSource(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIProjectionSource(room,scope->d->L);}
}

AIOwnerAnalepticBirth::AIOwnerAnalepticBirth(Room &room,const ServerPlayer *owner,const Analeptic *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIMaxCardsScope::OwnerAnaleptic
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Only the original owner's constant native stack constructor issues this.
  // Keep its original function-long lifetime; the getter grant ends sooner.
  if(typeid(*card)!=typeid(Analeptic)){
   retireAIProjectionSource(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIProjectionSource(room,scope->d->L);}
}

bool AIMaxCardsScope::prepareLiteralFactory(const QString &name) noexcept {
 if(activeMaxCards==this&&d->root==FixedOwnerEvaluations&&accepted()
  &&d->literalFactories.contains(name))return true;
 // Complete finite recipes were proved before selecting any DTO key. A key
 // outside them retires BEFORE constructor dispatch, preserving legacy behavior.
 retireAIProjectionSource(d->room,d->L);return false;
}
AIAnalepticNullBirth::AIAnalepticNullBirth(Room &room,const Player *owner,const Card *card) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIMaxCardsScope::FixedOwnerEvaluations
  ||&scope->d->room!=&room||scope->d->target.data()!=owner||!card||!scope->accepted())return;
 try {
  // Private source callsite is the original IsAvailable(nullptr) new expression.
  if(typeid(*card)!=typeid(Analeptic)){
   retireAIProjectionSource(room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIProjectionSource(room,scope->d->L);}
}

bool inAIConnectionQueryScope(const Room &room,lua_State *L) noexcept {
 return activeMaxCards&&activeMaxCards->d->root==AIMaxCardsScope::ConnectionQuery
  &&&activeMaxCards->d->room==&room&&activeMaxCards->d->L==L&&activeMaxCards->accepted();
}

AIZunweiSkillCloneBirth::AIZunweiSkillCloneBirth(const Engine *engine,const Card *card,const QString &name) noexcept {
 auto *scope=activeMaxCards;
 if(!scope||scope->d->root!=AIMaxCardsScope::ZunweiSeed
  ||engine!=Sanguosha||!card||name!=QStringLiteral("ZunweiCard")||!scope->accepted())return;
 try {
  // The Engine's actual completed native allocation issues this receipt.
  // A type/name match is only a dispatch check, never an adoption API.
  if(typeid(*card)!=typeid(ZunweiCard)) {
   retireAIProjectionSource(scope->d->room,scope->d->L);return;
  }
  scope->d->policyCards.insert(card,QPointer<const Card>(card));
 }catch(...){retireAIProjectionSource(scope->d->room,scope->d->L);}
}
