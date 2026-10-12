#include "ai-strategy/prototypes.h"

// ---- aliases ----
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QObject>
#include <QJsonArray>
#include <QScopeGuard>
#include <utility>

namespace {
struct AuditedAlias {
    const char *id;
    const char *registry;
    const char *first;
    const char *second;
    const char *canonicalRegistry;
    const char *canonicalFirst;
    const char *canonicalSecond;
    const char *sourceFile;
    int sourceLine;
};
// b6025acc: each alias has a literal top-level reference writer. These are
// names of already-ported function identities, not permission to execute the
// gameplay VM. BGM aliases and non-audited dynamic registrations are excluded.
static const AuditedAlias audited[] = {
    {"ai_cardneed.olzhijian", "ai_cardneed", "olzhijian", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    // Fixed literal writers: dongmanbao-ai.lua1689 and extra-ai.lua1508.
    {"ai_skill_invoke.se_erdao_old", "ai_skill_invoke", "se_erdao_old", "", "ai_skill_invoke", "se_erdao", "", "dongmanbao-ai.lua", 1382},
    {"ai_skill_invoke.heg_guowu", "ai_skill_invoke", "heg_guowu", "", "ai_skill_invoke", "guowu", "", "ol_sp-ai.lua", 45},
    // scarlet-ai.lua728/1115/2997 retain these exact effective HC functions.
    {"ai_skill_invoke.s4_txbw_niansheng", "ai_skill_invoke", "s4_txbw_niansheng", "", "ai_skill_invoke", "luanfeng", "", "tenyear_hc-ai.lua", 5024},
    {"ai_choicemade_filter.cardChosen.s4_longhun", "ai_choicemade_filter", "cardChosen", "s4_longhun", "ai_choicemade_filter", "cardChosen", "peiqi", "tenyear_hc-ai.lua", 2414},
    {"ai_choicemade_filter.cardChosen.s4_yuezhe", "ai_choicemade_filter", "cardChosen", "s4_yuezhe", "ai_choicemade_filter", "cardChosen", "peiqi", "tenyear_hc-ai.lua", 2414},
    {"ai_cardneed.GodJiuse", "ai_cardneed", "GodJiuse", "", "ai_cardneed", "slash", "", "smart-ai.lua", 4110},
    {"ai_cardneed.GodSaodang", "ai_cardneed", "GodSaodang", "", "ai_cardneed", "slash", "", "smart-ai.lua", 4110},
    // Literal writer: ling-ai.lua:194.
    {"ai_cardneed.zhulou", "ai_cardneed", "zhulou", "", "ai_cardneed", "weapon", "", "smart-ai.lua", 4105},
    // Literal writer: newstar-ai.lua:702. A later sijyu-ai.lua overwrite of
    // slash is deliberately a different identity and must not enable this row.
    {"ai_cardneed.os_chitu", "ai_cardneed", "os_chitu", "", "ai_cardneed", "slash", "", "smart-ai.lua", 4110},
    {"ai_skill_invoke.diy_k_luoshang_judge", "ai_skill_invoke", "diy_k_luoshang_judge", "", "ai_skill_invoke", "noszhenlie", "", "standard-ai.lua", 403},
    {"ai_skill_invoke.Promote_luoyi", "ai_skill_invoke", "Promote_luoyi", "", "ai_skill_invoke", "nosluoyi", "", "standard-ai.lua", 1531},
    {"ai_cardneed.Luayezhan", "ai_cardneed", "Luayezhan", "", "ai_cardneed", "nosluoyi", "", "standard-ai.lua", 1566},
    {"ai_cardneed.Promote_luoyi", "ai_cardneed", "Promote_luoyi", "", "ai_cardneed", "nosluoyi", "", "standard-ai.lua", 1566},
    {"ai_cardneed.heg_luoyi", "ai_cardneed", "heg_luoyi", "", "ai_cardneed", "nosluoyi", "", "standard-ai.lua", 1566},
    {"ai_skill_invoke.Promote_tieji", "ai_skill_invoke", "Promote_tieji", "", "ai_skill_invoke", "nostieji", "", "standard-ai.lua", 1735},
    {"ai_skill_invoke.lxtx_shouyue_tieqi", "ai_skill_invoke", "lxtx_shouyue_tieqi", "", "ai_skill_invoke", "nostieji", "", "standard-ai.lua", 1735},
    {"ai_skill_invoke.PlusTiandu", "ai_skill_invoke", "PlusTiandu", "", "ai_skill_invoke", "jianxiong", "", "standard-ai.lua", 2078},
    {"ai_skill_invoke.PlusLangmou", "ai_skill_invoke", "PlusLangmou", "", "ai_skill_invoke", "fankui", "", "standard-ai.lua", 2156},
    {"ai_skill_invoke.SixLangmou", "ai_skill_invoke", "SixLangmou", "", "ai_skill_invoke", "fankui", "", "standard-ai.lua", 2156},
    {"ai_choicemade_filter.cardChosen.PlusLangmou", "ai_choicemade_filter", "cardChosen", "PlusLangmou", "ai_choicemade_filter", "cardChosen", "fankui", "standard-ai.lua", 2167},
    {"ai_choicemade_filter.cardChosen.SixLangmou", "ai_choicemade_filter", "cardChosen", "SixLangmou", "ai_choicemade_filter", "cardChosen", "fankui", "standard-ai.lua", 2167},
    {"ai_skill_cardchosen.PlusLangmou", "ai_skill_cardchosen", "PlusLangmou", "", "ai_skill_cardchosen", "fankui", "", "standard-ai.lua", 2186},
    {"ai_skill_cardchosen.SixLangmou", "ai_skill_cardchosen", "SixLangmou", "", "ai_skill_cardchosen", "fankui", "", "standard-ai.lua", 2186},
    {"ai_need_damaged.PlusLangmou", "ai_need_damaged", "PlusLangmou", "", "ai_need_damaged", "fankui", "", "standard-ai.lua", 2211},
    {"ai_need_damaged.SixLangmou", "ai_need_damaged", "SixLangmou", "", "ai_need_damaged", "fankui", "", "standard-ai.lua", 2211},
    {"ai_cardneed.TH_LastJudgement", "ai_cardneed", "TH_LastJudgement", "", "ai_cardneed", "guicai", "", "standard-ai.lua", 2281},
    {"ai_slash_prohibit.qhstandardganglie", "ai_slash_prohibit", "qhstandardganglie", "", "ai_slash_prohibit", "ganglie", "", "standard-ai.lua", 2341},
    {"ai_slash_prohibit.s4_ganglie", "ai_slash_prohibit", "s4_ganglie", "", "ai_slash_prohibit", "ganglie", "", "standard-ai.lua", 2341},
    {"ai_skill_invoke.s4_txbw_luoyi", "ai_skill_invoke", "s4_txbw_luoyi", "", "ai_skill_invoke", "luoyi", "", "standard-ai.lua", 2541},
    {"ai_skill_invoke.sfofl_luoyi", "ai_skill_invoke", "sfofl_luoyi", "", "ai_skill_invoke", "luoyi", "", "standard-ai.lua", 2541},
    {"ai_cardneed.qhstandardluoyi", "ai_cardneed", "qhstandardluoyi", "", "ai_cardneed", "luoyi", "", "standard-ai.lua", 2552},
    {"ai_cardneed.sr_aozhan", "ai_cardneed", "sr_aozhan", "", "ai_cardneed", "luoyi", "", "standard-ai.lua", 2552},
    {"ai_skill_invoke.PlusYiji", "ai_skill_invoke", "PlusYiji", "", "ai_skill_invoke", "yiji", "", "standard-ai.lua", 2606},
    {"ai_can_damagehp.qhstandardyiji", "ai_can_damagehp", "qhstandardyiji", "", "ai_can_damagehp", "yiji", "", "standard-ai.lua", 2677},
    {"ai_card_intention.#PlusRende_Card", "ai_card_intention", "#PlusRende_Card", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.#s3_fans_xianjia", "ai_card_intention", "#s3_fans_xianjia", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.Dragon_rendeCard", "ai_card_intention", "Dragon_rendeCard", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.SixRendeCard", "ai_card_intention", "SixRendeCard", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.TenyearRendeCard", "ai_card_intention", "TenyearRendeCard", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.sfofl_renwang", "ai_card_intention", "sfofl_renwang", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.songli", "ai_card_intention", "songli", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_card_intention.tenyearrende", "ai_card_intention", "tenyearrende", "", "ai_card_intention", "RendeCard", "", "standard-ai.lua", 2877},
    {"ai_cardneed.lxtx_shouyue_wusheng", "ai_cardneed", "lxtx_shouyue_wusheng", "", "ai_cardneed", "wusheng", "", "standard-ai.lua", 3207},
    {"ai_cardneed.s4_fuhan", "ai_cardneed", "s4_fuhan", "", "ai_cardneed", "wusheng", "", "standard-ai.lua", 3207},
    {"ai_cardneed.s4_wuhu_wusheng", "ai_cardneed", "s4_wuhu_wusheng", "", "ai_cardneed", "wusheng", "", "standard-ai.lua", 3207},
    {"ai_cardneed.tenyearwusheng", "ai_cardneed", "tenyearwusheng", "", "ai_cardneed", "wusheng", "", "standard-ai.lua", 3207},
    {"ai_skill_pindian.sfofl_yijue", "ai_skill_pindian", "sfofl_yijue", "", "ai_skill_pindian", "yijue", "", "standard-ai.lua", 3319},
    {"ai_cardneed.sfofl_yijue", "ai_cardneed", "sfofl_yijue", "", "ai_cardneed", "yijue", "", "standard-ai.lua", 3328},
    {"ai_cardneed.Dragon_feijiang", "ai_cardneed", "Dragon_feijiang", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.heg_paoxiao", "ai_cardneed", "heg_paoxiao", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.kenewpaoxiao", "ai_cardneed", "kenewpaoxiao", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.keqifendi", "ai_cardneed", "keqifendi", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.lianxie", "ai_cardneed", "lianxie", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.lxtx_shouyue_paoxiao", "ai_cardneed", "lxtx_shouyue_paoxiao", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.moupaoxiao", "ai_cardneed", "moupaoxiao", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_cardneed.s4_wuhu_heduan", "ai_cardneed", "s4_wuhu_heduan", "", "ai_cardneed", "paoxiao", "", "standard-ai.lua", 3360},
    {"ai_choicemade_filter.skillInvoke.qhstandardtieqi", "ai_choicemade_filter", "skillInvoke", "qhstandardtieqi", "ai_choicemade_filter", "skillInvoke", "tieji", "standard-ai.lua", 3610},
    {"ai_cardneed.moujizhi", "ai_cardneed", "moujizhi", "", "ai_cardneed", "jizhi", "", "standard-ai.lua", 3660},
    {"ai_cardneed.s4_cangzhuo", "ai_cardneed", "s4_cangzhuo", "", "ai_cardneed", "jizhi", "", "standard-ai.lua", 3660},
    {"ai_card_intention.#LuaFanjianCard", "ai_card_intention", "#LuaFanjianCard", "", "ai_card_intention", "FanjianCard", "", "standard-ai.lua", 4182},
    {"ai_card_intention.NeoFanjianCard", "ai_card_intention", "NeoFanjianCard", "", "ai_card_intention", "FanjianCard", "", "standard-ai.lua", 4182},
    {"ai_slash_prohibit.dl_liuli", "ai_slash_prohibit", "dl_liuli", "", "ai_slash_prohibit", "liuli", "", "standard-ai.lua", 4534},
    {"ai_cardneed.dl_liuli", "ai_cardneed", "dl_liuli", "", "ai_cardneed", "liuli", "", "standard-ai.lua", 4543},
    {"ai_target_revises.FourJianxiong", "ai_target_revises", "FourJianxiong", "", "ai_target_revises", "jianxiong", "", "standard_cards-ai.lua", 463},
    {"ai_target_revises.PlusWulve", "ai_target_revises", "PlusWulve", "", "ai_target_revises", "jianxiong", "", "standard_cards-ai.lua", 463},
    {"ai_target_revises.qhstandardjianxiong", "ai_target_revises", "qhstandardjianxiong", "", "ai_target_revises", "jianxiong", "", "standard_cards-ai.lua", 463},
    {"ai_target_revises.sfofl_xiongtu", "ai_target_revises", "sfofl_xiongtu", "", "ai_target_revises", "jianxiong", "", "standard_cards-ai.lua", 463},
    {"ai_target_revises.sfofl_lezong", "ai_target_revises", "sfofl_lezong", "", "ai_target_revises", "xiangle", "", "standard_cards-ai.lua", 486},
    {"ai_skill_playerchosen.@fatechuanxin2", "ai_skill_playerchosen", "@fatechuanxin2", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.@fatejianzhong2", "ai_skill_playerchosen", "@fatejianzhong2", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.fjsp_youlong", "ai_skill_playerchosen", "fjsp_youlong", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.heg_fangyuan", "ai_skill_playerchosen", "heg_fangyuan", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.jgfengxing", "ai_skill_playerchosen", "jgfengxing", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.kexiajue", "ai_skill_playerchosen", "kexiajue", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.manheng", "ai_skill_playerchosen", "manheng", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.s2_jizhan", "ai_skill_playerchosen", "s2_jizhan", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.sandun", "ai_skill_playerchosen", "sandun", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.se_chongzhuang", "ai_skill_playerchosen", "se_chongzhuang", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.sfofl_shuangren", "ai_skill_playerchosen", "sfofl_shuangren", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.sgszhenhua", "ai_skill_playerchosen", "sgszhenhua", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_skill_playerchosen.sr_wuwei", "ai_skill_playerchosen", "sr_wuwei", "", "ai_skill_playerchosen", "zero_card_as_slash", "", "standard_cards-ai.lua", 701},
    {"ai_card_intention.#sgkgodqianqiCard", "ai_card_intention", "#sgkgodqianqiCard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.#sr_qingchengcard", "ai_card_intention", "#sr_qingchengcard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.BawangCard", "ai_card_intention", "BawangCard", "", "ai_card_intention", "ShensuCard", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.IceSlash", "ai_card_intention", "IceSlash", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.PlusShensu_Card", "ai_card_intention", "PlusShensu_Card", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.QingyiCard", "ai_card_intention", "QingyiCard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.TenyearShensuCard", "ai_card_intention", "TenyearShensuCard", "", "ai_card_intention", "ShensuCard", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.TouduCard", "ai_card_intention", "TouduCard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.diancp", "ai_card_intention", "diancp", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.sr_xiahoucard", "ai_card_intention", "sr_xiahoucard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.wangzhe", "ai_card_intention", "wangzhe", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.zhouchuCard", "ai_card_intention", "zhouchuCard", "", "ai_card_intention", "Slash", "", "standard_cards-ai.lua", 758},
    {"ai_card_intention.#s3_jxwj_weiwan", "ai_card_intention", "#s3_jxwj_weiwan", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.#sr_fangxincard", "ai_card_intention", "#sr_fangxincard", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.JiuzhuCard", "ai_card_intention", "JiuzhuCard", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.SecondTenyearChunlaoWineCard", "ai_card_intention", "SecondTenyearChunlaoWineCard", "", "ai_card_intention", "ChunlaoWineCard", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.SevenWeiYuan", "ai_card_intention", "SevenWeiYuan", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.SixChunlaoWineCard", "ai_card_intention", "SixChunlaoWineCard", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.TenyearChunlaoWineCard", "ai_card_intention", "TenyearChunlaoWineCard", "", "ai_card_intention", "ChunlaoWineCard", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.WeidaiCard", "ai_card_intention", "WeidaiCard", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_card_intention.kerenxinCard", "ai_card_intention", "kerenxinCard", "", "ai_card_intention", "Peach", "", "standard_cards-ai.lua", 1089},
    {"ai_skill_invoke.ShemaSkill", "ai_skill_invoke", "ShemaSkill", "", "ai_skill_invoke", "kylin_bow", "", "standard_cards-ai.lua", 1453},
    {"ai_skill_invoke.kenewbazhen", "ai_skill_invoke", "kenewbazhen", "", "ai_skill_invoke", "eight_diagram", "", "standard_cards-ai.lua", 1471},
    {"ai_skill_invoke.sfofl_young_eight_diagram", "ai_skill_invoke", "sfofl_young_eight_diagram", "", "ai_skill_invoke", "eight_diagram", "", "standard_cards-ai.lua", 1471},
    {"ai_skill_cardask.shoot-jink", "ai_skill_cardask", "shoot-jink", "", "ai_skill_cardask", "archery-attack-jink", "", "standard_cards-ai.lua", 1619},
    {"ai_card_intention.#sgkgodhuchiCard", "ai_card_intention", "#sgkgodhuchiCard", "", "ai_card_intention", "Duel", "", "standard_cards-ai.lua", 1802},
    {"ai_choicemade_filter.cardChosen.FourYijue", "ai_choicemade_filter", "cardChosen", "FourYijue", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.LuaJianzhi", "ai_choicemade_filter", "cardChosen", "LuaJianzhi", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.LuaLongzhen_o", "ai_choicemade_filter", "cardChosen", "LuaLongzhen_o", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.MeowXiaoji", "ai_choicemade_filter", "cardChosen", "MeowXiaoji", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.PlusTieji", "ai_choicemade_filter", "cardChosen", "PlusTieji", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.SE_Shanguang", "ai_choicemade_filter", "cardChosen", "SE_Shanguang", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.SixWuzhiCard", "ai_choicemade_filter", "cardChosen", "SixWuzhiCard", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.TH_ForbiddenFruits", "ai_choicemade_filter", "cardChosen", "TH_ForbiddenFruits", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.anni", "ai_choicemade_filter", "cardChosen", "anni", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.bf_xuanlve", "ai_choicemade_filter", "cardChosen", "bf_xuanlve", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.bf_zhiman", "ai_choicemade_filter", "cardChosen", "bf_zhiman", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.eweicheng", "ai_choicemade_filter", "cardChosen", "eweicheng", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.ezhuanquan", "ai_choicemade_filter", "cardChosen", "ezhuanquan", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.f_chongzhen", "ai_choicemade_filter", "cardChosen", "f_chongzhen", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.feijiangts", "ai_choicemade_filter", "cardChosen", "feijiangts", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.gonglzs", "ai_choicemade_filter", "cardChosen", "gonglzs", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.haigu", "ai_choicemade_filter", "cardChosen", "haigu", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_jinfa", "ai_choicemade_filter", "cardChosen", "heg_jinfa", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_lianpian", "ai_choicemade_filter", "cardChosen", "heg_lianpian", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_mobile_tanfeng", "ai_choicemade_filter", "cardChosen", "heg_mobile_tanfeng", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_mouduan", "ai_choicemade_filter", "cardChosen", "heg_mouduan", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_tenyear_dechao", "ai_choicemade_filter", "cardChosen", "heg_tenyear_dechao", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.heg_zhefu", "ai_choicemade_filter", "cardChosen", "heg_zhefu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.jianwushu", "ai_choicemade_filter", "cardChosen", "jianwushu", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.jiuwei", "ai_choicemade_filter", "cardChosen", "jiuwei", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.kanshi", "ai_choicemade_filter", "cardChosen", "kanshi", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.kejiexianliwei", "ai_choicemade_filter", "cardChosen", "kejiexianliwei", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.kejieyaoxieqin", "ai_choicemade_filter", "cardChosen", "kejieyaoxieqin", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.kemumu", "ai_choicemade_filter", "cardChosen", "kemumu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.kenewgirlshibing", "ai_choicemade_filter", "cardChosen", "kenewgirlshibing", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.keyaoxieqin", "ai_choicemade_filter", "cardChosen", "keyaoxieqin", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.luafeiqi", "ai_choicemade_filter", "cardChosen", "luafeiqi", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.luayonglue", "ai_choicemade_filter", "cardChosen", "luayonglue", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.meizlfeiren", "ai_choicemade_filter", "cardChosen", "meizlfeiren", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.meizlguiyuan", "ai_choicemade_filter", "cardChosen", "meizlguiyuan", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.mouxiaoji", "ai_choicemade_filter", "cardChosen", "mouxiaoji", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.mythluochong", "ai_choicemade_filter", "cardChosen", "mythluochong", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.mythmumu", "ai_choicemade_filter", "cardChosen", "mythmumu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhfiremengjin", "ai_choicemade_filter", "cardChosen", "qhfiremengjin", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhfirequhu", "ai_choicemade_filter", "cardChosen", "qhfirequhu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhstandardfanjian", "ai_choicemade_filter", "cardChosen", "qhstandardfanjian", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhstandardpoxi", "ai_choicemade_filter", "cardChosen", "qhstandardpoxi", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhwindfenji", "ai_choicemade_filter", "cardChosen", "qhwindfenji", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhwindjiewei", "ai_choicemade_filter", "cardChosen", "qhwindjiewei", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.qhwindshensu", "ai_choicemade_filter", "cardChosen", "qhwindshensu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.renzha", "ai_choicemade_filter", "cardChosen", "renzha", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s3_xiaoyong", "ai_choicemade_filter", "cardChosen", "s3_xiaoyong", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s3_youlong", "ai_choicemade_filter", "cardChosen", "s3_youlong", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s3_yt_qujin_caoxiu", "ai_choicemade_filter", "cardChosen", "s3_yt_qujin_caoxiu", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s4_fani", "ai_choicemade_filter", "cardChosen", "s4_fani", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s4_ganglie", "ai_choicemade_filter", "cardChosen", "s4_ganglie", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s4_jianghu", "ai_choicemade_filter", "cardChosen", "s4_jianghu", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s4_kuiwei", "ai_choicemade_filter", "cardChosen", "s4_kuiwei", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.s4_txbw_chili", "ai_choicemade_filter", "cardChosen", "s4_txbw_chili", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.se_mingqie", "ai_choicemade_filter", "cardChosen", "se_mingqie", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.sfofl_lyre", "ai_choicemade_filter", "cardChosen", "sfofl_lyre", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.thedolls", "ai_choicemade_filter", "cardChosen", "thedolls", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.weareidle", "ai_choicemade_filter", "cardChosen", "weareidle", "ai_choicemade_filter", "cardChosen", "dismantlement", "standard_cards-ai.lua", 2179},
    {"ai_choicemade_filter.cardChosen.xianzhencard", "ai_choicemade_filter", "cardChosen", "xianzhencard", "ai_choicemade_filter", "cardChosen", "snatch", "standard_cards-ai.lua", 2179},
    {"ai_card_intention.huayuanCard", "ai_card_intention", "huayuanCard", "", "ai_card_intention", "IronChain", "", "maneuvering-ai.lua", 471},
    {"ai_cardneed.tenyearduanliang", "ai_cardneed", "tenyearduanliang", "", "ai_cardneed", "duanliang", "", "thicket-ai.lua", 140},
    {"ai_skill_invoke.kofliegong", "ai_skill_invoke", "kofliegong", "", "ai_skill_invoke", "liegong", "", "wind-ai.lua", 138},
    {"ai_skill_invoke.shenxian", "ai_skill_invoke", "shenxian", "", "ai_skill_invoke", "luoying", "", "yjcm-ai.lua", 9},
    {"ai_skill_invoke.yinli", "ai_skill_invoke", "yinli", "", "ai_skill_invoke", "luoying", "", "yjcm-ai.lua", 9},
    // Round28 fixed extra-ai literal alias identities.
    {"ai_cardneed.yinghun_po", "ai_cardneed", "yinghun_po", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_cardneed.heg_qiangxi", "ai_cardneed", "heg_qiangxi", "", "ai_cardneed", "weapon", "", "smart-ai.lua", 4105},
    {"ai_cardneed.heg_liegong", "ai_cardneed", "heg_liegong", "", "ai_cardneed", "slash", "", "smart-ai.lua", 4110},
    {"ai_cardneed.heg_xiaoji", "ai_cardneed", "heg_xiaoji", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_skill_invoke.heg_yicheng", "ai_skill_invoke", "heg_yicheng", "", "ai_skill_invoke", "yicheng", "", "original-hegemony/heg-formation-ai.lua", 41},
    {"ai_cardneed.heg_huyuan", "ai_cardneed", "heg_huyuan", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_cardneed.heg_ol_liegong", "ai_cardneed", "heg_ol_liegong", "", "ai_cardneed", "slash", "", "smart-ai.lua", 4110},
    {"ai_cardneed.bf_xuanlve", "ai_cardneed", "bf_xuanlve", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_skill_playerchosen.bf_sec_tiaodu", "ai_skill_playerchosen", "bf_sec_tiaodu", "", "ai_skill_playerchosen", "bf_tiaodu", "", "extra-ai.lua", 1963},
    {"ai_skill_playerchosen.bf_third_tiaodu", "ai_skill_playerchosen", "bf_third_tiaodu", "", "ai_skill_playerchosen", "bf_tiaodu", "", "extra-ai.lua", 1963},
    {"ai_cardneed.twyj_qijia", "ai_cardneed", "twyj_qijia", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_cardneed.twyj_huzhu", "ai_cardneed", "twyj_huzhu", "", "ai_cardneed", "equip", "", "smart-ai.lua", 4099},
    {"ai_skill_playerchosen.command1", "ai_skill_playerchosen", "command1", "", "ai_skill_playerchosen", "damage", "", "smart-ai.lua", 4652},
    {"ai_skill_playerchosen.heg_fudi", "ai_skill_playerchosen", "heg_fudi", "", "ai_skill_playerchosen", "damage", "", "smart-ai.lua", 4652},
    {"ai_card_priority.heg_suzhi", "ai_card_priority", "heg_suzhi", "", "ai_card_priority", "jizhi", "", "smart-ai.lua", 10206},
    {"ai_skill_choice.heg_nos_huaiyi", "ai_skill_choice", "heg_nos_huaiyi", "", "ai_skill_choice", "heg_huaiyi", "", "extra-ai.lua", 3956},
    {"ai_skill_playerschosen.heg_nos_huaiyi", "ai_skill_playerschosen", "heg_nos_huaiyi", "", "ai_skill_playerschosen", "heg_huaiyi", "", "extra-ai.lua", 3960},
};

class AliasCapture final : public QObject {
public:
    AliasCapture(Room &room, QJsonObject value)
        : QObject(&room), snapshot(std::move(value))
    { setObjectName(QStringLiteral("isolated-strategy-alias-capture")); }
    const QJsonObject snapshot;
};
const AliasCapture *capture(const Room &room)
{
    for (QObject *child : room.children())
        if (auto *found = dynamic_cast<AliasCapture *>(child)) return found;
    return nullptr;
}

// Leave the raw value on the stack. A missing raw value is proven absent only
// if ordinary Lua lookup has no __index to synthesize a different result.
bool pushRawField(lua_State *state, int table, const char *key)
{
    table = lua_absindex(state, table);
    if (!lua_istable(state, table)) { lua_pushnil(state); return false; }
    lua_pushstring(state, key); lua_rawget(state, table);
    if (!lua_isnil(state, -1)) return true;
    if (!lua_getmetatable(state, table)) return true;
    lua_pushliteral(state, "__index"); lua_rawget(state, -2);
    const bool absent = lua_isnil(state, -1);
    lua_pop(state, 2);
    return absent;
}
bool pushRegistryPath(lua_State *state, const char *registry, const char *first, const char *second)
{
    lua_pushglobaltable(state);
    bool known = pushRawField(state, -1, "sgs"); lua_remove(state, -2);
    known = pushRawField(state, -1, registry) && known; lua_remove(state, -2);
    known = pushRawField(state, -1, first) && known; lua_remove(state, -2);
    if (*second) { known = pushRawField(state, -1, second) && known; lua_remove(state, -2); }
    return known;
}
QJsonArray path(const char *first, const char *second)
{
    QJsonArray value{QString::fromUtf8(first)};
    if (*second) value.append(QString::fromUtf8(second));
    return value;
}
QJsonObject functionOrigin(lua_State *state, int index)
{
    if (!lua_isfunction(state, index)) return {};
    lua_pushvalue(state, index);
    lua_Debug info{};
    if (!lua_getinfo(state, ">S", &info)) return {};
    return {{"source", QString::fromUtf8(info.source ? info.source : "")},
        {"line", info.linedefined}, {"what", QString::fromLatin1(info.what ? info.what : "")}};
}
}

void captureAIStrategyAliases(Room &room)
{
    // Calling again deliberately does not observe later mutable registry writes.
    LuaRuntime &runtime = room.roomRuntime()->lua();
    LuaRuntime::Binding binding(runtime, false);
    if (capture(room)) return;
    QJsonObject entries;
    lua_State *state = runtime.state();
    if (!state) {
        new AliasCapture(room, {{"captured", false}, {"reason", "legacy_runtime_unavailable"}});
        return;
    }
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    for (const AuditedAlias &row : audited) {
        const int base = lua_gettop(state);
        const bool aliasKnown = pushRegistryPath(state, row.registry, row.first, row.second);
        const int alias = lua_absindex(state, -1);
        const bool present = !lua_isnil(state, alias);
        const QJsonObject aliasOrigin = functionOrigin(state, alias);
        const bool canonicalKnown = pushRegistryPath(state, row.canonicalRegistry,
            row.canonicalFirst, row.canonicalSecond);
        const int canonical = lua_absindex(state, -1);
        const QJsonObject canonicalOrigin = functionOrigin(state, canonical);
        const bool matched = aliasKnown && canonicalKnown && lua_isfunction(state, alias)
            && lua_isfunction(state, canonical) && lua_rawequal(state, alias, canonical);
        const bool sourceMatches = canonicalOrigin.value("what").toString() == "Lua"
            && canonicalOrigin.value("source").toString() == QStringLiteral("@lua/ai/") + QString::fromUtf8(row.sourceFile)
            && canonicalOrigin.value("line").toInt(-1) == row.sourceLine;
        QJsonObject value{{"known", aliasKnown && canonicalKnown}, {"alias_known", aliasKnown},
            {"canonical_known", canonicalKnown}, {"present", present}, {"matched", matched},
            {"source_matches", sourceMatches}, {"enabled", matched && sourceMatches},
            {"registry", QString::fromUtf8(row.registry)}, {"path", path(row.first,row.second)},
            {"canonical_registry", QString::fromUtf8(row.canonicalRegistry)},
            {"canonical_path", path(row.canonicalFirst,row.canonicalSecond)},
            {"alias_type", QString::fromLatin1(lua_typename(state,lua_type(state,alias)))},
            {"alias_source", aliasOrigin.value("source")}, {"alias_line", aliasOrigin.value("line")},
            {"canonical_source", canonicalOrigin.value("source")}, {"canonical_line", canonicalOrigin.value("line")}};
        // os_chitu retains whichever audited Slash closure its literal writer
        // observed. Keep generic canonical identity/enabled semantics unchanged.
        if (QString::fromUtf8(row.id) == QStringLiteral("ai_cardneed.os_chitu")) {
            QString variant;
            if (aliasKnown && aliasOrigin.value("what").toString() == QStringLiteral("Lua")) {
                const QString source = aliasOrigin.value("source").toString();
                const int line = aliasOrigin.value("line").toInt(-1);
                if (source == QStringLiteral("@lua/ai/smart-ai.lua") && line == 4110)
                    variant = QStringLiteral("smart_ai");
                else if (source == QStringLiteral("@lua/ai/sijyu-ai.lua") && line == 1)
                    variant = QStringLiteral("sijyu");
            }
            value.insert("alias_variant", variant);
            value.insert("alias_source_matches", !variant.isEmpty());
        }
        if (!aliasKnown || !canonicalKnown) value.insert("reason", "lookup_not_proven_raw");
        else if (!present) value.insert("reason", "absent_at_legacy_load");
        else if (!matched) value.insert("reason", "present_different_identity");
        else if (!sourceMatches) value.insert("reason", "canonical_source_changed");
        entries.insert(QString::fromUtf8(row.id), value);
        lua_settop(state, base);
    }
    // These two live keys can be overwritten in either package load order.
    // Capture each actual body independently; ai_skills aliases are not an
    // alternate dispatch source. No policy callback or __index is executed.
    QJsonObject variants;
    const auto captureJiuzi = [&](const char *registry, const char *key,
                                   int typemoonLine, int touhouLine) {
        const int base = lua_gettop(state);
        const bool known = pushRegistryPath(state, registry, key, "");
        const bool present = !lua_isnil(state, -1);
        const QJsonObject origin = functionOrigin(state, -1);
        QString variant;
        if (known && origin.value("what").toString() == QStringLiteral("Lua")) {
            if (origin.value("source").toString() == QStringLiteral("@lua/ai/typemoon-ai.lua")
                && origin.value("line").toInt(-1) == typemoonLine) variant = QStringLiteral("typemoon");
            if (origin.value("source").toString() == QStringLiteral("@lua/ai/touhouproject-ai.lua")
                && origin.value("line").toInt(-1) == touhouLine) variant = QStringLiteral("touhou");
        }
        QJsonObject value{{"known", known}, {"present", present},
            {"source", origin.value("source")}, {"line", origin.value("line")},
            {"variant", variant}, {"type", QString::fromLatin1(lua_typename(state,lua_type(state,-1)))}};
        variants.insert(QString::fromUtf8(registry) + '.' + QString::fromUtf8(key), value);
        lua_settop(state, base);
    };
    captureJiuzi("ai_fill_skill", "Jiuzi", 27, 6307);
    captureJiuzi("ai_skill_use_func", "#JiuziCard", 56, 6319);
    {
        const int base = lua_gettop(state);
        const bool known = pushRegistryPath(state, "ai_skill_invoke", "aocai", "");
        const bool present = !lua_isnil(state, -1);
        const QJsonObject origin = functionOrigin(state, -1);
        QString variant;
        if (known && origin.value("what").toString() == QStringLiteral("Lua")) {
            if (origin.value("source").toString() == QStringLiteral("@lua/ai/sp-ai.lua")
                && origin.value("line").toInt(-1) == 940) variant = QStringLiteral("sp");
            if (origin.value("source").toString() == QStringLiteral("@lua/ai/ol_qifu-ai.lua")
                && origin.value("line").toInt(-1) == 1) variant = QStringLiteral("ol_qifu");
        }
        variants.insert(QStringLiteral("ai_skill_invoke.aocai"), QJsonObject{
            {"known", known}, {"present", present}, {"source", origin.value("source")},
            {"line", origin.value("line")}, {"variant", variant},
            {"type", QString::fromLatin1(lua_typename(state,lua_type(state,-1)))}});
        lua_settop(state, base);
    }
    new AliasCapture(room, {{"captured", true}, {"audited_count", int(sizeof(audited)/sizeof(audited[0]))},
        {"entries", entries}, {"source_variants", variants}});
}

void projectAIStrategyAliases(AIWorldView &world, Room &room)
{
    const AliasCapture *captured = capture(room);
    world.strategyFacts.insert("audited_aliases", captured ? captured->snapshot
        : QJsonObject{{"captured", false}, {"reason", "legacy_load_snapshot_missing"}});
}

// ---- conditional-prototypes ----
#include "card.h"
#include "engine.h"
#include "serverplayer.h"

namespace {
bool currentOwner(const AIRequest &request, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() && !request.viewerObjectName.isEmpty()
        && request.viewerObjectName == viewer->objectName()
        && request.stateRevision == viewer->getRoom()->roomRuntime()->stateRevision();
}
bool play(const AIRequest &request)
{
    return request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
}
bool response(const AIRequest &request, const QString &pattern)
{
    return (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)
        && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
        && request.pattern == pattern;
}
bool defaultFace(int suit, int number, const QList<int> &costs)
{
    return suit == int(Card::NoSuit) && number == 0 && costs.isEmpty();
}
const Card *singleOwnerMaterial(const QList<int> &costs, const ServerPlayer *viewer,
                                const QList<int> &zone)
{
    if (costs.size() != 1 || costs.first() < 0 || !zone.contains(costs.first())
        || viewer->getRoom()->getCardOwner(costs.first()) != viewer) return nullptr;
    return Sanguosha->getCard(costs.first());
}
}

int aiAuditedConditionalSourcePrototype(const AIRequest &request, const QString &skill,
                                       const QString &prototype, const ServerPlayer *viewer)
{
    if (!currentOwner(request, viewer)) return -1;
    // Fixed tenyear2.cpp Zunwei::viewAs returns new ZunweiCard with zero
    // materials. This only names the readonly/selected prototype; existing
    // native activation, instance, factory and final target gates still run.
    if (skill == "zunwei" && prototype == "ZunweiCard")
        return play(request) ? 0 : -1;
    // Fixed sijyuoffline.lua18018/18040-18060 and source AI4885: only
    // this first response question constructs the zero-material SkillCard.
    // The second @@sfofl_zhengan_basic question creates an ordinary BasicCard.
    if (skill == "sfofl_zhengan_basic" && prototype == "#sfofl_zhengan_basicCard")
        return request.kind == AIRequest::UseCard
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@sfofl_zhengan"
            && request.handlingMethod == Card::MethodUse ? 0 : -1;
    // Fixed gaoda.lua5791/6238. Their response products are ordinary cards.
    // Fixed AI1579/1768 literally uses %d: ListS2I skips this invalid integer,
    // so LuaSkillCard::Parse produces zero subcards, not ID 0 and not an error.
    if (skill == "ntdtwo" && prototype == "#ntdtwo")
        return play(request) && request.pattern != "@@ntdtwo" && viewer->getMark("@NTD2") > 0 ? 0 : -1;
    if (skill == "ntdthree" && prototype == "#ntdthree")
        return play(request) && request.pattern != "@@ntdthree" && viewer->getMark("@NTD3") > 0 ? 0 : -1;
    // gaoda.lua3441: PLAY uses one non-Nullification Trick outside lizhan.
    // The exact chosen card is checked by the existing native payment pipeline.
    if (skill == "luashipo" && prototype == "#luashipo")
        return play(request) && viewer->getPile("lizhan").size() < 3 ? 1 : -1;
    // gaoda.lua7229: the owner property chooses SkillCard versus normal card.
    if (skill == "haidao" && prototype == "#haidaocard")
        return response(request, "@@haidao") && viewer->property("haidao").toString() == "armor" ? 0 : -1;
    // gaoda.lua7884 and AI2558-2600: only marks 2/3 enter the source turn fill.
    // Source mark-3 fill is an empty seed; its use function later supplies two
    // ordered hand costs. Never reinterpret mark-1's Slash as this SkillCard.
    if (skill == "aoyi" && prototype == "#aoyi" && play(request) && !viewer->hasUsed("#aoyi")) {
        const int mark = viewer->getMark("@aoyi");
        if (mark == 2) return 0;
        if (mark == 3 && viewer->getHandcardNum() >= 2) return 1;
    }
    return -1;
}

bool aiAuditedConditionalOrdinaryProduct(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &costs,
                                        const ServerPlayer *viewer)
{
    if (!currentOwner(request, viewer)) return false;
    if ((sourceSkill == "ntdtwo" || sourceSkill == "ntdthree")
        && printedSkill == sourceSkill && name == "dismantlement")
        return response(request, "@@" + sourceSkill) && defaultFace(suit, number, costs);
    if (sourceSkill == "haidao" && printedSkill == "haidaocard"
        && response(request, "@@haidao") && defaultFace(suit, number, costs)) {
        // These are the only ordinary products emitted by AI2258-2337.
        return (name == "slash" || name == "iron_chain" || name == "shoot")
            && viewer->property("haidao").toString() == name;
    }
    if (sourceSkill == "luashipo" && printedSkill == "luashipo" && name == "nullification"
        && response(request, "nullification")) {
        const Card *material = singleOwnerMaterial(costs, viewer, viewer->getPile("lizhan"));
        return material && suit == int(material->getSuit()) && number == material->getNumber();
    }
    if (sourceSkill == "aoyi" && printedSkill == "aoyi" && name == "slash"
        && response(request, "@@aoyi") && viewer->getMark("@aoyi") == 1) {
        // Native permits non-equipped cards; fixed AI2528 selects only hand.
        const Card *material = singleOwnerMaterial(costs, viewer, viewer->handCards());
        return material && material->isRed()
            && suit == int(material->getSuit()) && number == material->getNumber();
    }
    return false;
}

bool aiAuditedConditionalOrdinaryPreview(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &costs,
                                        const ServerPlayer *viewer)
{
    if (aiAuditedConditionalOrdinaryProduct(request, sourceSkill, printedSkill, name,
                                          suit, number, costs, viewer)) return true;
    // AI2534 probes targets before choosing a material. No native product or
    // mark check occurs at that source point: preserve this hypothesis even if
    // the later selected proposal is rejected by native mark/cost validation.
    return currentOwner(request, viewer) && response(request, "@@aoyi")
        && sourceSkill == "aoyi" && printedSkill.isEmpty() && name == "slash"
        && suit == int(Card::NoSuitRed) && number == 0 && costs.isEmpty();
}

// ---- borrowed-callbacks ----
namespace {
struct Origin {const char *registry,*key,*source;int line,last;};
const Origin origins[]={
#include "ai-strategy/borrowed-origins.inc"
};
bool matchedOrigin(const char *registry,const QString &key,const lua_Debug &info)
{
    for(const Origin &o:origins)
        if(qstrcmp(o.registry,registry)==0 && key==QString::fromUtf8(o.key)
           && qstrcmp(o.source,info.source)==0 && o.line==info.linedefined
           && o.last==info.lastlinedefined) return true;
    return false;
}

// Metadata only: never invoke __index, a callback, or a userdata conversion.
// A non-plain registry is explicitly unknown, including apparent raw absence.
bool registry(lua_State *L, const char *name)
{
    lua_pushglobaltable(L); lua_pushliteral(L,"sgs"); lua_rawget(L,-2);
    if (!lua_istable(L,-1)) return false;
    lua_pushstring(L,name); lua_rawget(L,-2);
    return lua_istable(L,-1) && !lua_getmetatable(L,-1);
}
QJsonObject callback(lua_State *L,const char *name,const QString &key)
{
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    QJsonObject result{{"known",false}};
    if (!registry(L,name)) return result;
    const QByteArray bytes=key.toUtf8();
    lua_pushlstring(L,bytes.constData(),size_t(bytes.size())); lua_rawget(L,-2);
    const int type=lua_type(L,-1);
    result.insert("known",true);
    result.insert("type",QString::fromLatin1(lua_typename(L,type)));
    if(type==LUA_TBOOLEAN) result.insert("value",bool(lua_toboolean(L,-1)));
    if(type==LUA_TFUNCTION) {
        lua_Debug info{};lua_pushvalue(L,-1);
        if (!lua_getinfo(L,">S",&info) || !info.source || info.what[0]=='C') {
            result.insert("known",false);return result;
        }
        result.insert("source",QString::fromUtf8(info.source));
        result.insert("line",info.linedefined);
        result.insert("matched",matchedOrigin(name,key,info));
    }
    return result;
}
bool roster(lua_State *L,QJsonArray &names)
{
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    if(!registry(L,"ai_skills")) return false;
    const int list=lua_absindex(L,-1);
    for(int index=1;index<=4097;++index) {
        lua_rawgeti(L,list,index);
        if(lua_isnil(L,-1)) return true; // exact ipairs termination
        if(index>4096 || !lua_istable(L,-1) || lua_getmetatable(L,-1)) return false;
        lua_pushliteral(L,"name");lua_rawget(L,-2);
        if(lua_type(L,-1)!=LUA_TSTRING) return false;
        size_t length=0;const char *name=lua_tolstring(L,-1,&length);
        if(length>4096) return false;
        names.append(QString::fromUtf8(name,int(length)));lua_pop(L,2);
    }
    return false;
}
}
void projectAIBorrowedCallbacks(AIWorldView &world,Room &room,ServerPlayer *viewer)
{
    if(!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    lua_State *L=room.roomRuntime()->lua().state();if(!L)return;
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    if(viewer->hasSkill("chenbiyi",true)) {
        QJsonObject facts=world.strategyFacts.value("shixinrumo_yi").toObject();
        const QString key=viewer->property("chenbiyi_skill").toString();
        if(key.toUtf8().size()<=4096) {
            facts.insert("chenbiyi_skill",key);QJsonObject groups;
            for(const char *name:{"ai_fill_skill","ai_guhuo_card","ai_cardsview"})
                groups.insert(QString::fromLatin1(name),QJsonObject{{key,callback(L,name,key)}});
            facts.insert("chenbiyi_callback_presence",groups);
        }
        world.strategyFacts.insert("shixinrumo_yi",facts);
    }
    if(viewer->hasSkill("kuilei",true)) {
        QJsonObject facts=world.strategyFacts.value("legends").toObject();
        const QString record=viewer->getTag("KuileiSkills").toString();
        if(record.toUtf8().size()<=4096)facts.insert("KuileiSkills",record);
        QJsonArray names;if(roster(L,names))facts.insert("source_fill_skills",names);
        world.strategyFacts.insert("legends",facts);
    }
}

// ---- use-dispatch ----
#include <limits>

namespace {
bool rawField(lua_State *state, int table, const QByteArray &key)
{
    table = lua_absindex(state, table);
    if (!lua_istable(state, table)) { lua_pushnil(state); return false; }
    lua_pushlstring(state, key.constData(), size_t(key.size())); lua_rawget(state, table);
    if (!lua_isnil(state, -1)) return true;
    if (!lua_getmetatable(state, table)) return true;
    lua_pushliteral(state, "__index"); lua_rawget(state, -2);
    const bool known = lua_isnil(state, -1); lua_pop(state, 2);
    return known;
}
QJsonObject origin(lua_State *state, int function)
{
    if (!lua_isfunction(state, function)) return {};
    lua_pushvalue(state, function); lua_Debug info{};
    if (!lua_getinfo(state, ">S", &info)) return {};
    return {{"source", QString::fromUtf8(info.source ? info.source : "")}, {"line", info.linedefined}};
}
bool disclosedId(const AIRequest &request, int id)
{
    const auto cardsHave = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId == id) return true;
        return false;
    };
    const auto playerHas = [&](const AIPlayerView &player) {
        if (cardsHave(player.knownCards) || cardsHave(player.equips) || cardsHave(player.judgingArea)) return true;
        for (const auto &pile : player.piles) if (cardsHave(pile.cards)) return true;
        return false;
    };
    if (cardsHave(request.worldView.handCards) || cardsHave(request.worldView.discardPile)
        || cardsHave(request.choiceOptions.cards) || playerHas(request.worldView.self)) return true;
    for (const auto &player : request.worldView.players) if (playerHas(player)) return true;
    return false;
}
QJsonObject useRow(lua_State *state, int registry, bool registryKnown, const QString &key, const AIRequest &request, bool originalEntry)
{
    const int top = lua_gettop(state);
    auto restore = qScopeGuard([&] { lua_settop(state, top); });
    QJsonObject row{{"key", key}, {"known", false}};
    if (!rawField(state, registry, key.toUtf8()) || !registryKnown) return row;
    int function = lua_absindex(state, -1);
    row.insert("known", true);
    row.insert("present", !lua_isnil(state, function));
    if (lua_isnil(state, function)) return row;
    row.insert("truthy", bool(lua_toboolean(state, function)));
    row.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, function))));
    if (!lua_isfunction(state, function)) { row.insert("kind", "nonfunction"); return row; }
    auto info = origin(state, function);
    if (info.value("source") == "@lua/ai/original-hegemony/heg-loader-ai.lua"
        && info.value("line") == QJsonValue(92)) {
        // One audited loader wrapper captures only its provider function. Its
        // dynamic normalizer must still be the exact fixed body. No callback,
        // arbitrary upvalue, __index, userdata or card face is evaluated here.
        const int before = lua_gettop(state);
        lua_pushglobaltable(state);
        bool normalizerKnown = rawField(state, -1, "sgs");
        normalizerKnown = rawField(state, -1, "originalHegemonyCardWire") && normalizerKnown;
        const auto wireOrigin = normalizerKnown ? origin(state, -1) : QJsonObject();
        lua_settop(state, before);
        if (!originalEntry || wireOrigin.value("source") != "@lua/ai/original-hegemony/heg-common-ai.lua"
            || wireOrigin.value("line") != QJsonValue(32)) {
            row.insert("known", false); row.insert("reason", "Original use-card wire normalizer is not the audited provider");
            return row;
        }
        row.insert("wrapper_source", info.value("source"));
        row.insert("wrapper_line", info.value("line"));
        bool found = false;
        for (int i = 1; const char *name = lua_getupvalue(state, function, i); ++i) {
            if (QByteArray(name) == "original" && lua_isfunction(state, -1)) {
                function = lua_absindex(state, -1); info = origin(state, function);
                found = true; break;
            }
            lua_pop(state, 1);
        }
        if (!found) {
            row.insert("known", false); row.insert("reason", "Original use-card wrapper provider is unavailable");
            return row;
        }
        row.insert("original_wire_wrapper", true);
    }
    row.insert("source", info.value("source")); row.insert("line", info.value("line"));
    row.insert("kind", "callback");
    if (info.value("source") != "@lua/ai/smart-ai.lua") return row;
    const int line = info.value("line").toInt();
    const char *wanted = line == 3896 || line == 3922 ? "cn" : line == 3958 ? "id" : nullptr;
    if (!wanted) return row;
    // Read only the one audited scalar closure capture. Never execute a hook,
    // index a userdata, or expose arbitrary upvalues.
    bool found = false;
    for (int i = 1; const char *name = lua_getupvalue(state, function, i); ++i) {
        if (QByteArray(name) == wanted) {
            if (*wanted == 'c' && lua_type(state, -1) == LUA_TSTRING) {
                const QString className = QString::fromUtf8(lua_tostring(state, -1));
                if (!className.isEmpty()) { row.insert("kind", "class"); row.insert("class_name", className); found = true; }
            } else if (*wanted == 'i' && lua_isinteger(state, -1)) {
                const lua_Integer value = lua_tointeger(state, -1);
                if (value >= 0 && value <= std::numeric_limits<int>::max() && disclosedId(request, int(value))) {
                    row.insert("kind", "physical"); row.insert("card_id", int(value)); found = true;
                }
            }
        }
        lua_pop(state, 1);
        if (found) break;
    }
    if (!found) { row.insert("known", false); row.insert("reason", "generated handler scalar or disclosed card is unavailable"); }
    return row;
}
}

void projectAIUseCardDispatch(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (request.kind != AIRequest::UseCard || !viewer || viewer->getRoom() != &room
        || viewer->objectName() != request.viewerObjectName) return;
    QJsonObject result{{"known", false}, {"request_present", request.hasSkillActionContext}};
    if (request.hasSkillActionContext) result.insert("request_valid", AiLegacyRequestView(request, viewer).isValid());
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (state) {
        const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state);
        const int globals = lua_absindex(state, -1);
        bool known = rawField(state, globals, "SmartAI");
        // middleclass explicitly stores methods in this raw dictionary.
        known = rawField(state, -1, "__instanceDict") && known;
        known = rawField(state, -1, "askForUseCard") && known;
        const auto entry = known ? origin(state, -1) : QJsonObject();
        result.insert("entry_source", entry.value("source")); result.insert("entry_line", entry.value("line"));
        QString variant;
        if (entry.value("source") == "@lua/ai/smart-ai.lua" && entry.value("line") == 3978) variant = "ordinary";
        else if (entry.value("source") == "@lua/ai/original-hegemony/heg-common-ai.lua" && entry.value("line") == 2032) variant = "original";
        if (!variant.isEmpty()) { result.insert("entry_variant", variant); result.insert("known", true); }
        lua_settop(state, globals);
        known = rawField(state, globals, "sgs");
        known = rawField(state, -1, "ai_skill_use") && known;
        const int registry = lua_absindex(state, -1);
        result.insert("raw", useRow(state, registry, known, request.pattern, request, variant == "original"));
        result.insert("prompt", useRow(state, registry, known, request.prompt.section(':', 0, 0), request, variant == "original"));
    }
    request.choiceOptions.context.insert("use_card_dispatch", result);
    request.worldView.decisionContext.insert("use_card_dispatch", result);
}

// Aocai's legacy invoke asks askForCard(pattern, prompt, 1), where 1 is
// data and the method is nil. Capture only its actual prompt-hook presence;
// this does not mint a nested response request or execute that callback.
void projectAIAocaiInvokeDispatch(AIRequest &request, Room &room, const QStringList &asked)
{
    if (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != "aocai") return;
    QJsonObject result{{"known", true}, {"asked", QJsonArray::fromStringList(asked)}};
    QJsonObject callback{{"known", false}};
    if (asked.size() >= 2) {
        const QString key = asked.at(1).section(':', 0, 0);
        callback.insert("key", key);
        LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
        lua_State *state = room.roomRuntime()->lua().state();
        if (state) {
            const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
            lua_pushglobaltable(state);
            bool known = rawField(state, -1, "sgs");
            known = rawField(state, -1, "ai_skill_cardask") && known;
            known = rawField(state, -1, key.toUtf8()) && known;
            callback.insert("known", known);
            if (known) {
                callback.insert("present", lua_isfunction(state, -1));
                callback.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, -1))));
                const auto info = origin(state, -1);
                callback.insert("source", info.value("source")); callback.insert("line", info.value("line"));
            }
        }
    }
    result.insert("cardask", callback);
    request.choiceOptions.context.insert("aocai_invoke", result);
}

// Yitian's actual CardAsked response trigger (yitian.cpp735-766), not an
// activation or an Aocai scope. This is readonly callback metadata only.
void projectAILianliSlashInvokeDispatch(AIRequest &request, Room &room, const QStringList &asked)
{
    if (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != "lianli-slash"
        || asked.size() < 3 || asked.first() != "slash" || asked.last() != "response"
        || asked.at(1).contains("lianli-slash")) return;
    QJsonObject result{{"known", true}, {"asked", QJsonArray::fromStringList(asked)}};
    const QString key = asked.at(1).section(':', 0, 0);
    QJsonObject callback{{"known", false}, {"key", key}};
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (state) {
        const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state);
        bool known = rawField(state, -1, "sgs");
        known = rawField(state, -1, "ai_skill_cardask") && known;
        known = rawField(state, -1, key.toUtf8()) && known;
        callback.insert("known", known);
        if (known) {
            callback.insert("present", lua_isfunction(state, -1));
            callback.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, -1))));
            const auto info = origin(state, -1);
            callback.insert("source", info.value("source")); callback.insert("line", info.value("line"));
        }
    }
    result.insert("cardask", callback);
    request.choiceOptions.context.insert("lianli_slash_invoke", result);
}

// ---- nested-cardask ----
#include <QDebug>
#include <QSet>
#include <memory>

namespace {
QJsonObject failure(const QString &reason)
{
    return {{"error", "AI_UNSUPPORTED"}, {"reason", reason}, {"key", "nested_askForCard.parse"}};
}
bool inquiry(const AIRequest &request, Room &room, ServerPlayer *viewer, QJsonObject *data = nullptr, QString *contextKey = nullptr)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::SkillInvoke
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    const bool lianli = request.choiceOptions.reason == "lianli-slash";
    if (!lianli && request.choiceOptions.reason != "aocai") return false;
    const QString key = lianli ? QStringLiteral("lianli_slash_invoke") : QStringLiteral("aocai_invoke");
    const auto invocation = request.choiceOptions.context.value(key).toObject();
    const auto asked = invocation.value("asked").toArray();
    if (!invocation.value("known").toBool() || asked.size() < 2
        || !asked[0].isString() || !asked[1].isString()) return false;
    if (lianli && (asked.size() < 3 || asked[0] != "slash" || asked.last() != "response"
        || asked[1].toString().contains("lianli-slash"))) return false;
    if (contextKey) *contextKey = key;
    if (data) *data = invocation;
    return true;
}
QSet<int> disclosedOwnerCards(const AIRequest &request)
{
    QSet<int> result;
    const auto append = [&](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId >= 0) result.insert(card.effectiveId);
    };
    append(request.worldView.handCards);
    append(request.worldView.self.equips);
    append(request.worldView.self.judgingArea);
    for (const auto &pile : request.worldView.self.piles) append(pile.cards);
    return result;
}
QJsonObject classified(const char *outcome)
{
    return {{"kind", "nested_card_parse"}, {"outcome", QString::fromLatin1(outcome)}};
}
}

QJsonObject inspectAINestedCardaskString(Room &room, ServerPlayer *viewer,
                                       const AIRequest &request, const QString &value)
{
    if (!inquiry(request, room, viewer)) return failure("nested parser is outside the current finite nil-method inquiry");
    // Bound native regex parsing and material-list allocation. Rejected inputs
    // stay unknown; the bound is not reported as a donor Card::Parse failure.
    if (value.size() > 4096 || value.contains(QChar::Null)) return failure("nested parser input is outside its bounded text domain");
    const QString copy = value.section(QStringLiteral("->"), 0, 0);
    const bool virtualSyntax = value.startsWith('@') || value.startsWith('$')
        || value.startsWith('#') || value.contains('=');
    if (!virtualSyntax && copy != ".") {
        bool numeric = false;
        const int id = copy.toInt(&numeric);
        if (numeric && (!disclosedOwnerCards(request).contains(id) || room.getCardOwner(id) != viewer))
            return failure("nested physical parser identity is not disclosed and owned by this viewer");
    }
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaScope(room.roomRuntime()->lua(), false);
    // Fixed Card::Parse and LuaSkillCard::Parse only clone native/public
    // prototypes and copy callback references here; no strategy/filter/use
    // callback is invoked. Never inspect a virtual material's live card face.
    const Card *parsed = Card::Parse(value);
    if (!parsed) return classified("invalid");
    if (!parsed->isVirtualCard()) return classified("requires_nil_method");
    std::unique_ptr<const Card> temporary(parsed);
    return classified(parsed->subcardsLength() == 0 ? "empty_virtual" : "requires_nil_method");
}

void projectAINestedCardaskCompulsory(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    QJsonObject invocation;
    QString contextKey;
    if (!inquiry(request, room, viewer, &invocation, &contextKey)) return;
    QString pattern = invocation.value("asked").toArray().at(0).toString();
    if (pattern.endsWith('!')) pattern.chop(1);
    QJsonObject result{{"known", false}, {"pattern", pattern}};
    const QSet<int> disclosed = disclosedOwnerCards(request);
    QList<int> ids;
    for (const Card *card : viewer->getCards("he")) {
        if (!card || card->getEffectiveId() < 0) {
            invocation.insert("compulsory", result);
            request.choiceOptions.context.insert(contextKey, invocation);
            return;
        }
        ids << card->getEffectiveId();
    }
    for (int id : viewer->getHandPile()) if (!ids.contains(id)) ids << id;
    // The lookup map is deliberately independent of candidate filtering and
    // keep order. Lua retains the exact source zone and sorting traversal.
    QJsonObject matches;
    bool complete = true;
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaScope(room.roomRuntime()->lua(), false);
    for (int id : ids) {
        if (!disclosed.contains(id) || room.getCardOwner(id) != viewer) { complete = false; break; }
        const Card *card = Sanguosha->getCard(id);
        if (!card) { complete = false; break; }
        matches.insert(QString::number(id), Sanguosha->matchPattern(pattern, viewer, card));
    }
    if (complete) { result.insert("known", true); result.insert("by_id", matches); }
    invocation.insert("compulsory", result);
    request.choiceOptions.context.insert(contextKey, invocation);
}

QJsonObject reportAILianliCardaskError(Room &room, ServerPlayer *viewer,
                                      const AIRequest &request, const QString &message)
{
    if (request.choiceOptions.reason != "lianli-slash" || !inquiry(request, room, viewer)
        || message.isEmpty() || message.size() > 16384 || message.contains(QChar::Null))
        return failure("Lianli source error diagnostic is outside the current bounded inquiry");
    // Fixed SmartAI.initialize pcall catches an ordinary askForSkillInvoke
    // error, emits these diagnostics, and returns nil. LuaAI converts that
    // successful nil result to false without TrustAI or personality RNG.
    const auto console = [&](const QString &line) {
        room.output(line);
        qWarning("%s", qPrintable(line));
    };
    console(QStringLiteral("[AI_CALLBACK_ERROR] askForSkillInvoke: ") + message);
    console(QStringLiteral("askForSkillInvoke"));
    console(message);
    room.outputEventStack();
    console(QStringLiteral("lianli-slash"));
    return {{"kind", "lianli_source_error"}, {"error_class", "ordinary_lua_error"},
            {"message", message}, {"invoke", false}};
}

// ---- response-source-products-response-2 ----
#include "skill.h"

bool isAITongyiSource(const AIRequest &r,const QString &skill,const QString &policyClass)
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

Card *buildAITongyiSource(const AIRequest &r,Room &room,ServerPlayer *p,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,
    const QString &policyClass,const QString &declaration,const QList<int> &costs)
{
    if(!p || !isAITongyiSource(r,activation.key.skillName,policyClass)
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

#include <cmath>
#include <climits>
AICardsViewObject::AICardsViewObject()=default;
AICardsViewObject::~AICardsViewObject()=default;
AICardsViewObject::AICardsViewObject(AICardsViewObject &&) noexcept=default;
AICardsViewObject &AICardsViewObject::operator=(AICardsViewObject &&) noexcept=default;
AICardsViewObject buildAICardsViewObject(const AIRequest &r,Room &room,ServerPlayer *p,
    const QJsonObject &scope,const QString &registry,const QString &hook,
    const QJsonObject &proposal,bool attachRequest,const Card *ownedSource)
{
    AICardsViewObject out;
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

bool aiTongyiConstruction(const AIRequest &r,Room &room,ServerPlayer *p,const QJsonObject &spec)
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
            || scope.value("version")!=1 || !isAITongyiSource(r,"tongyiTQ",cls))continue;
        const auto identity=scope.value("identity").toObject();
        const SkillInstanceRef activation(identity.value("activation_owner").toString(),
            SkillInstanceKey(identity.value("activation_skill").toString(),identity.value("activation_instance").toInt()));
        const SkillInstanceRef source(identity.value("source_owner").toString(),
            SkillInstanceKey(identity.value("source_skill").toString(),identity.value("source_instance").toInt()));
        // Reuse precisely the source product proof, not a generic clone/name grant.
        // This temporary prototype has no receipt and no gameplay effect.
        std::unique_ptr<Card> proof(buildAITongyiSource(r,room,p,activation,source,cls,name,{}));
        if(proof && r.stateRevision==room.roomRuntime()->stateRevision())return true;
    }
    return false;
}

// ---- response-source-products-response ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/anime.h"
#include "lua-wrapper.h"
#include <QRegularExpression>

AIResponseSourcePolicy::AIResponseSourcePolicy() = default;
AIResponseSourcePolicy::~AIResponseSourcePolicy() = default;
AIResponseSourcePolicy::AIResponseSourcePolicy(AIResponseSourcePolicy &&) noexcept = default;
AIResponseSourcePolicy &AIResponseSourcePolicy::operator=(AIResponseSourcePolicy &&) noexcept = default;
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
AIResponseSourcePolicy buildAIResponseSourcePolicy(const AIRequest &r,Room &room,ServerPlayer *p,
    const QJsonObject &scope,const QString &registry,const QString &hookKey,
    const QJsonValue &proposal,bool attachRequest) {
    AIResponseSourcePolicy out;
    const auto unknown=[&](const char *why) {out.error=QString::fromLatin1(why);return std::move(out);};
    const auto absent=[&]() {out.status=AIResponseSourcePolicy::Absent;return std::move(out);};
    static const QSet<QString> registries{"ai_guhuo_card","ai_cardsview_valuable","ai_cardsview","ai_view_as"};
    const QString skill=scope.value("skill_name").toString();
    const QJsonObject identity=scope.value("identity").toObject();
    if(!p || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || !aiRangeRequestCurrent(room,r,p) || !registries.contains(registry)
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
                QString error;out.physicalCard=attachAIDanlvePhysical(room,r,p,scope,registry,hookKey,id,&error);
                if(!out.physicalCard){out.error=error;return std::move(out);}
                out.bindingSpec={{"physical_id",id},{"physical_request_attached",true}};
                out.status=AIResponseSourcePolicy::Parsed;return out;
            }
            out.physicalCard=Sanguosha->getCard(id);
            if(!out.physicalCard)return absent();
            out.bindingSpec={{"physical_id",id}};out.status=AIResponseSourcePolicy::Parsed;return out;
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
    out.status=AIResponseSourcePolicy::Parsed;return out;
}

// ---- use-revises ----
bool aiUseReviseEvaluationSource(const QString &key)
{
    return key == "use_revise:qiangxi" || key == "use_revise:tiaoxin";
}

std::unique_ptr<Card> aiUseReviseEvaluation(const AIRequest &request, Room &room,
    const ServerPlayer *viewer, const QString &key, const QString &className,
    const QJsonArray &costs, QString *error)
{
    if (error) error->clear();
    auto reject = [error](const QString &message) -> std::unique_ptr<Card> {
        if (error) *error = message;
        return {};
    };
    if (!aiUseReviseEvaluationSource(key) || !viewer || viewer->getRoom() != &room
        || request.decisionId == 0 || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision() || !viewer->isAlive())
        return reject("use-revise evaluation is outside its current owner request");
    const QString skill = key.mid(QStringLiteral("use_revise:").size());
    const QString expected = skill == "qiangxi" ? "QiangxiCard" : "TiaoxinCard";
    if (className != expected || !viewer->hasSkill(skill)
        || costs.size() > (skill == "qiangxi" ? 1 : 0))
        return reject("use-revise evaluation source/class/cost shape differs");

    // Source Card_Parse is a hypothesis. Do not run V2 canActivate/canSelect,
    // hasUsed, using, Jilei or payment checks before the source policy selects.
    QStringList ids;
    for (const auto &value : costs) {
        const double number = value.toDouble(-1);
        if (!value.isDouble() || number < 0 || number > std::numeric_limits<int>::max()
            || number != std::floor(number))
            return reject("use-revise evaluation has an invalid material ID");
        const int id = int(number);
        const Card *material = Sanguosha->getCard(id);
        const Player::Place place = room.getCardPlace(id);
        if (!material || !material->isKindOf("Weapon") || room.getCardOwner(id) != viewer
            || (place != Player::PlaceHand && place != Player::PlaceEquip))
            return reject("use-revise material is not a current owner hand/equip Weapon");
        bool disclosed = false;
        auto matches = [material, id](const AICardView &face) {
            return face.effectiveId == id && face.objectName == material->objectName()
                && face.className == material->getClassName() && face.suit == int(material->getSuit())
                && face.number == material->getNumber();
        };
        const auto &faces = place == Player::PlaceHand ? request.worldView.handCards : request.worldView.self.equips;
        for (const auto &face : faces) if (matches(face)) { disclosed = true; break; }
        if (!disclosed) return reject("use-revise material face was not issued to this viewer");
        ids << QString::number(id);
    }
    const QString wire = "@" + expected + "=" + (ids.isEmpty() ? "." : ids.join('+'));
    const Card *parsed = Card::Parse(wire);
    // Empty error means the actual source constructor is absent, not denial.
    if (!parsed) return {};
    std::unique_ptr<Card> card(const_cast<Card *>(parsed));
    if (!card->isVirtualCard() || card->getClassName() != expected
        || card->getSkillName(false) != skill || card->subcardsLength() != costs.size())
        return reject("use-revise V1 parsed policy identity differs");
    for (int i = 0; i < costs.size(); ++i)
        if (card->getSubcards().at(i) != costs.at(i).toInt())
            return reject("use-revise V1 parsed policy changed material order");
    return card;
}

// ---- delegate-uses ----
#include "ai-strategy/owner.h"
#include <QHash>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
namespace {
struct Source { QString source, destination; bool foreignFrom=false; };
bool current(const AIRequest &r, const ServerPlayer *p) {
 return p && p->getRoom() && r.viewerObjectName == p->objectName()
  && r.stateRevision == p->getRoom()->roomRuntime()->stateRevision();
}
Source source(const AIRequest &r, const QString &key) {
 if (r.kind == AIRequest::PlayersChosen && r.choiceOptions.reason == key) {
  if (key == "mobilebenxi") return {"mobilebenxiUse", "JincanmouData"};
  if (key == "fanyin") return {"fanyinData", "yb_zhuzhan2_data"};
 }
 if (key == "@@jieyingh" && r.kind == AIRequest::UseCard && r.pattern == key
     && r.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
  return {"jieyinghData", "yb_zhuzhan2_data"};
 if (r.kind == AIRequest::PlayerChosen) {
  const auto reason=r.choiceOptions.reason;
  if(key=="ov_zenhui" && reason==key)return {"ov_zenhuiData","yb_zhuzhan2_data",true};
  if(key=="ov_yizhu" && reason==key)return {"ov_yizhuData","yb_zhuzhan2_data",true};
  if(reason=="ov_qirang" && key=="ov_qirang:yb_fujia2_data")return {"ov_qirangData","yb_fujia2_data",true};
  if(reason=="ov_qirang" && key=="ov_qirang:yb_zhuzhan2_data")return {"ov_qirangData","yb_zhuzhan2_data",true};
 }
 if(key=="ov_ronggui" && r.kind==AIRequest::RespondCard && r.choiceOptions.question=="askForCard"
    && r.pattern=="BasicCard" && r.prompt.startsWith("ov_ronggui0:"))
  return {"@incoming","JincanmouData",true};
 return {};
}
bool slot(const QString &s) { return s == "JincanmouData" || s == "yb_zhuzhan2_data" || s == "yb_fujia2_data"; }
bool reader(const AIRequest &r,const QString &s) {
 if (s == "JincanmouData") return r.kind == AIRequest::PlayerChosen && (r.choiceOptions.reason == "jincanmou" || r.choiceOptions.reason == "ov_ronggui");
 if(s=="yb_fujia2_data")return r.kind==AIRequest::PlayerChosen && r.choiceOptions.reason=="yb_fujia2";
 return s == "yb_zhuzhan2_data" && (((r.kind == AIRequest::PlayerChosen || r.kind == AIRequest::Discard) && r.choiceOptions.reason == "yb_zhuzhan2")
  || (r.kind == AIRequest::PlayerChosen && r.choiceOptions.reason == "ov_lihuo"));
}
bool delegateUsesDisclosed(const AIRequest &r, ServerPlayer *p, int id) {
 for (const auto &c:r.worldView.handCards) if (c.effectiveId==id) return true;
 for (const auto &c:r.worldView.discardPile) if (c.effectiveId==id) return true;
 for (const auto &c:r.choiceOptions.cards) if (c.effectiveId==id) return true;
 auto players=r.worldView.players; players.prepend(r.worldView.self);
 for (const auto &v:players) {
  for (const auto &c:v.equips) if(c.effectiveId==id)return true;
  for (const auto &c:v.knownCards) if(c.effectiveId==id)return true;
  for (const auto &c:v.judgingArea) if(c.effectiveId==id)return true;
  for (const auto &pile:v.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 }
 return readAIStrategyPublicDisclosure(*p->getRoom(),p,id).value("known")==true;
}
bool delegateUsesRoster(Room &room,const Player *p) {
 for (const Player *v:room.getAllPlayers(true)) if (v==p) return true;
 return false;
}
bool observed(const AIRequest&r,ServerPlayer*p,const CardUseStruct&u,bool foreignFrom=false) {
 if (!u.card || !u.from || !delegateUsesRoster(*p->getRoom(),u.from) || (!foreignFrom && u.from!=p)) return false;
 for (const auto *t:u.to) if (!t || !delegateUsesRoster(*p->getRoom(),t)) return false;
 // Only material IDs are inspected before disclosure, never derived face.
 if (!u.card->isVirtualCard() && !delegateUsesDisclosed(r,p,u.card->getEffectiveId())) return false;
 for(int id:u.card->getSubcards())if(!delegateUsesDisclosed(r,p,id))return false;
 return true;
}
QJsonObject useJson(const Card *card,const QString &from,const QStringList &to,const AIDelegateUseProfile &profile) {
 return {{"card",profile(card)},{"from",from},{"to",QJsonArray::fromStringList(to)}};
}
class DelegateMetadata final : public QObject {
public: DelegateMetadata(Room &room,bool value):QObject(&room),knownNil(value){}
 const bool knownNil;
};
const DelegateMetadata *metadata(Room &room) {
 for(auto*c:room.children())if(auto*m=dynamic_cast<DelegateMetadata*>(c))return m;
 return nullptr;
}
struct Observation {
 QPointer<ServerPlayer> viewer;
 AIReadonlyCardLease lease;
 QList<int> materials;
 int physical=-1;
 bool constructedLihuo=false;
 QString from;
 QStringList to;
};
QHash<Room *,QHash<const Player *,QHash<QString,Observation>>> observations;
QSet<Room *> connected;
struct Input { Observation observation; quint64 decision=0, revision=0; };
QHash<Room *,QHash<const Player *,QHash<QString,Input>>> inputs;
QRecursiveMutex observationMutex;
void connectRoom(Room *room) {
 if(connected.contains(room))return;
 connected.insert(room);
 QObject::connect(room,&QObject::destroyed,[room]{
  QMutexLocker lock(&observationMutex);
  observations.remove(room);inputs.remove(room);connected.remove(room);
 });
}
Observation delegateUsesCapture(ServerPlayer*p,const CardUseStruct&u) {
 Observation o;o.viewer=p;o.lease=AIOwnerReadonlyCardStore::observe(u.card);
 o.from=u.from->objectName();o.materials=u.card->getSubcards();
 o.physical=u.card->isVirtualCard()?-1:u.card->getEffectiveId();
 for(const auto*t:u.to)o.to<<t->objectName();return o;
}
bool validObservation(const AIRequest&r,ServerPlayer*p,const Observation&o) {
 if(o.viewer!=p || !o.lease)return false;
 if(o.physical>=0&&!delegateUsesDisclosed(r,p,o.physical))return false;
 for(int id:o.materials)if(!delegateUsesDisclosed(r,p,id))return false;
 const Card*c=o.lease();if(!c||c->getSubcards()!=o.materials)return false;
 QStringList names=o.to;names<<o.from;
 for(const auto&name:names){bool found=false;for(const auto*v:p->getRoom()->getAllPlayers(true))if(v->objectName()==name)found=true;if(!found)return false;}
 return true;
}
}
void projectAIDelegates(AIRequest&r,Room&room,ServerPlayer*p,const AIDelegateUseProfile&profile,const QVariant&incoming) {
 if(!current(r,p)||p->getRoom()!=&room)return;
 // Capture only authenticated current native source uses, not a Lua-supplied face.
 QJsonObject overseas;
 for(const QString &k:{QString("ov_zenhui"),QString("ov_qirang:yb_fujia2_data"),QString("ov_qirang:yb_zhuzhan2_data"),QString("ov_yizhu"),QString("ov_ronggui")}) {
  const auto spec=source(r,k);if(spec.source.isEmpty())continue;
  QJsonObject row{{"source_key",k},{"destination",spec.destination},{"known",false}};
  const auto v=spec.source=="@incoming"?incoming:p->getTag(spec.source);
  if(v.canConvert<CardUseStruct>()) {
   const auto u=v.value<CardUseStruct>();
   if(observed(r,p,u,spec.foreignFrom)) {
    auto o=delegateUsesCapture(p,u);
    if(o.lease&&o.lease()) {
     row.insert("known",true);row.insert("use",useJson(u.card,o.from,o.to,profile));
     QMutexLocker lock(&observationMutex);connectRoom(&room);
     inputs[&room][p].insert(k,Input{o,r.decisionId,r.stateRevision});
    }
   }
  }
  overseas.insert(k,row);
 }
 if(!overseas.isEmpty())r.choiceOptions.context.insert("overseas_delegate_uses",overseas);
 const QString key=r.kind==AIRequest::UseCard?r.pattern:r.choiceOptions.reason;
 const auto s=source(r,key);if(s.source.isEmpty())return;
 QJsonObject row{{"source_key",key},{"destination",s.destination},{"known",false}};
 const QVariant v=p->getTag(s.source);
 if(v.canConvert<CardUseStruct>()) {
  const auto u=v.value<CardUseStruct>();
  if(observed(r,p,u)) {
   QStringList to;for(const auto *t:u.to)to<<t->objectName();
   row.insert("known",true);row.insert("use",useJson(u.card,p->objectName(),to,profile));
  }
 } else if(!v.isValid() && key=="@@jieyingh") {row.insert("known",true);row.insert("use",false);}
 if(key=="mobilebenxi")row.insert("global_p_known_nil",metadata(room)&&metadata(room)->knownNil);
 r.choiceOptions.context.insert("nested_delegate",row);
}
bool recordAIDelegateUse(const AIRequest&r,ServerPlayer*p,const QString&key,QString*error) {
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 if(!current(r,p))return fail("delegate observation requires current viewer request");
 const auto s=source(r,key);if(s.source.isEmpty())return fail("delegate source does not match current native question");
 const bool overseas=key.startsWith("ov_");
 const auto row=overseas?r.choiceOptions.context.value("overseas_delegate_uses").toObject().value(key).toObject():r.choiceOptions.context.value("nested_delegate").toObject();
 if(row.value("source_key")!=key||row.value("destination")!=s.destination||row.value("known")!=true||!row.value("use").isObject())return fail("delegate source was not lawfully projected");
 Observation o;
 if(overseas) {
  QMutexLocker lock(&observationMutex);
  const auto input=inputs.value(p->getRoom()).value(p).value(key);
  if(input.decision!=r.decisionId||input.revision!=r.stateRevision)return fail("delegate native source capture is stale");
  o=input.observation;
 } else {
  const auto v=p->getTag(s.source);if(!v.canConvert<CardUseStruct>())return fail("delegate source use unavailable");
  const auto u=v.value<CardUseStruct>();if(!observed(r,p,u))return fail("delegate source use is not currently disclosed");
  o=delegateUsesCapture(p,u);
 }
 if(!validObservation(r,p,o))return fail("delegate observed card graph is no longer disclosed or live");
 // Tag-backed source setTag reads the current native tag at the reached write.
 // Decision/revision alone cannot prove a native same-revision replacement.
 // Ronggui instead consumes the immutable incoming native question capture.
 if(overseas && s.source!="@incoming") {
  const auto v=p->getTag(s.source);
  if(!v.canConvert<CardUseStruct>())return fail("delegate source tag was cleared or replaced");
  const auto u=v.value<CardUseStruct>();
  if(!observed(r,p,u,s.foreignFrom) || u.card!=o.lease() || u.from->objectName()!=o.from)
   return fail("delegate source tag card or actor changed after projection");
  QStringList targets;for(const auto*t:u.to)targets<<t->objectName();
  if(targets!=o.to)return fail("delegate source tag target order changed after projection");
 }
 {QMutexLocker lock(&observationMutex);Room*room=p->getRoom();connectRoom(room);observations[room][p].insert(s.destination,std::move(o));}
 if(error)error->clear();return true;
}
bool recordAILihuoDelegateUse(const AIRequest &r,ServerPlayer *p,const CardUseStruct &use,
                                    const AIReadonlyCardLease &lease,QString *error) {
 auto fail=[&](const char *why){if(error)*error=QString::fromLatin1(why);return false;};
 if(!current(r,p)||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="ov_lihuo"
    ||r.choiceOptions.question!="askForSkillInvoke"||!p->hasSkill("ov_lihuo")
    ||!lease||lease()!=use.card||use.from!=p||!use.card||use.card->objectName()!="fire_slash"
    ||use.card->getSkillName(false)!="mobilelihuo"||!observed(r,p,use))
  return fail("Lihuo constructed observation does not match the current owner inquiry");
 Observation value;value.viewer=p;value.lease=lease;value.materials=use.card->getSubcards();value.physical=-1;value.constructedLihuo=true;value.from=p->objectName();
 for(auto *target:use.to)value.to<<target->objectName();
 {QMutexLocker lock(&observationMutex);Room *room=p->getRoom();connectRoom(room);observations[room][p].insert("yb_zhuzhan2_data",value);}
 if(error)error->clear();return true;
}

QJsonObject restoreAIDelegateUse(const AIRequest&r,ServerPlayer*p,const QString&destination,const AIDelegateUseProfile&profile,const AIDelegateUseProfile&constructedLihuoProfile) {
 QJsonObject out{{"present",false}};
 if(!current(r,p)||!slot(destination)||!reader(r,destination))return out;
 Observation o;
 {
  QMutexLocker lock(&observationMutex);
  const auto ri=observations.constFind(p->getRoom());if(ri==observations.cend())return out;
  const auto pi=ri->constFind(p);if(pi==ri->cend())return out;
  const auto i=pi->constFind(destination);if(i==pi->cend())return out;
  o=*i;
 }
 out.insert("present",true);out.insert("known",false);
 // Keep unknown in place on failure; falling back resurrects an older native tag.
 if(!validObservation(r,p,o))return out;
 if(o.constructedLihuo && (!constructedLihuoProfile || o.from!=p->objectName()
    || o.lease()->objectName()!="fire_slash" || o.lease()->getSkillName(false)!="mobilelihuo"))return out;
 out.insert("known",true);out.insert("use",useJson(o.lease(),o.from,o.to,o.constructedLihuo?constructedLihuoProfile:profile));return out;
}
void noteAIDelegateTagWrite(const Player*p,const QString&key) {
 if(!p||!slot(key))return;
 QMutexLocker lock(&observationMutex);
 for(auto ri=observations.begin();ri!=observations.end();++ri){auto pi=ri->find(p);if(pi!=ri->end())pi->remove(key);}
}
void noteAIDelegateTagsClear(const Player*p) {
 if(!p)return;QMutexLocker lock(&observationMutex);for(auto ri=observations.begin();ri!=observations.end();++ri)ri->remove(p);
}
void clearAIDelegateObservations(Room&room) { QMutexLocker lock(&observationMutex);observations.remove(&room);inputs.remove(&room); }

void captureAIDelegateMetadata(Room &room) {
 if(metadata(room))return;
 auto &vm=room.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);
 lua_State *L=vm.state();if(!L){new DelegateMetadata(room,false);return;}
 const int top=lua_gettop(L);const auto reset=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 lua_pushliteral(L,"p");lua_rawget(L,globals);bool known=lua_isnil(L,-1);lua_pop(L,1);
 if(lua_getmetatable(L,globals)){lua_pushliteral(L,"__index");lua_rawget(L,-2);known=known&&lua_isnil(L,-1);lua_pop(L,2);}
 // Load-time only, with fixed effective function provenance. Full fixed corpus
 // bytecode audit found zero _ENV.p stores; the two SETFIELD p instructions
 // are local AmazingGrace count tables (smart-ai.lua9361), not globals.
 bool fixed=false;lua_pushliteral(L,"sgs");lua_rawget(L,globals);
 if(lua_istable(L,-1)){lua_pushliteral(L,"ai_skill_playerschosen");lua_rawget(L,-2);
  if(lua_istable(L,-1)){lua_pushliteral(L,"mobilebenxi");lua_rawget(L,-2);
   if(lua_isfunction(L,-1)){lua_Debug info{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&info))
    fixed=QString::fromUtf8(info.source?info.source:"").endsWith("/mobile_st_standard-ai.lua")&&info.linedefined==1873&&QString::fromLatin1(info.what?info.what:"")=="Lua";
   }
  }
 }
 new DelegateMetadata(room,known&&fixed);
}
int aiJieyinghDelegatePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype) {
 return current(r,p)&&p->hasSkill("jieyingh")&&skill=="jieyingh"&&prototype=="JieyinghCard"
  &&r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@jieyingh"?0:-1;
}
