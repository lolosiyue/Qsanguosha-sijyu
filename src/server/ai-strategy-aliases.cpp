#include "ai-strategy-aliases.h"
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
