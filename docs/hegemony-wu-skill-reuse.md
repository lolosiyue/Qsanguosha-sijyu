# 國戰標準吳將：共用技能與制衡／度勢 V2

2026-09-24 原始碼檢查點。共用技能直接在身份局原始定義升級 V2，國戰只引用同一技能 ID；不在吳將包建立相同效果的技能副本。保留目前身份局規則，不以已停用的國戰變體覆寫原版效果。

| 武將 | 直接引用的技能 ID |
| --- | --- |
| 孫權 | 獨立 V2 `heg_zhiheng` |
| 甘寧 | `qixi` |
| 呂蒙 | `keji`；另保留差異技能 `heg_mouduan` |
| 黃蓋 | `kurou` |
| 周瑜 | `yingzi`、`fanjian` |
| 大喬 | `guose`、`liuli` |
| 陸遜 | `qianxun`；另保留新技能 `heg_duoshi` |
| 孫尚香 | `jieyin`、`xiaoji` |
| 孫堅 | `yinghun` |
| 小喬 | `tenyeartianxiang`、`hongyan` |
| 太史慈 | `tianyi` |
| 周泰 | `buqu`、`mobilefenji` |
| 魯肅 | `haoshi`、`dimeng` |
| 張昭張紘 | `zhijian`、`guzheng` |
| 丁奉 | `duanbing`、`fenxun` |

| 邊界 | 處理 |
| --- | --- |
| 共用版本 | 22 個共用技能使用既有的效果、翻譯、AI 與 related helpers。苦肉、英姿、反間、國色、謙遜、不屈等採目前註冊的版本；不額外補入原武將其他技能，例如詐降。 |
| V2 範圍 | 原版 `keji`、`yingzi`、`qianxun`、`xiaoji`、`yinghun`、`buqu`、`haoshi`、`guzheng` 使用 `TriggerSkillV2`；`qixi`、`kurou`、`fanjian`、`jieyin`、`tianyi`、`dimeng`、`zhijian` 及相應 response view-as 使用 `ViewAsSkillV2`。原有 `duanbing`／`fenxun` 已是 V2。`hongyan` 保留唯一 `FilterSkill` 定義，現有架構無 FilterSkillV2。 |
| 制衡 | 出牌階段每 activation instance 限一次；選取一至體力上限張自己的可棄置手牌／裝備牌，經通用 proxy 原子棄置後摸等量牌，棄光手牌不額外摸牌。選牌與付款前均檢查上限及可棄置性。`cost` 保存張數至 execution-local amount，允許 V2 數值攔截；效果只在 invoker 存活時摸牌。普通 `zhiheng` 不修改。 |
| 制衡相容 | 沿用 `heg_zhiheng` 翻譯及 `HZhihengCard` history key；實際卡牌為 `ActiveSkillCard`，不恢復舊 HZhihengCard 類別。一般及國戰 AI 改建通用 proxy，保留原選牌策略與 activation identity；次數由 V2 實例配額管線判斷。 |
| 度勢 | 一張紅色手牌轉為 `HAwaitExhausted`；僅出牌使用。`createCard()` 無副作用，材料付款、目標與效果沿用普通錦囊管線。 |
| 次數 | 使用 `Limit_Phase`、上限 4，按 activation instance 計算；沿用 `DuoshiAE` history key。刪除錦囊內額外增加同一 history 的邏輯，避免一次計兩次。 |
| 舊定義 | 工作區未提交的 H 副本與 h-standard-wu-variants 已清理；本提交保留既有共用 ID，只將小喬改接 tenyeartianxiang、周泰增加 mobilefenji。HMouduan 後續補齊為直接 V2，使用共用用牌紀錄與 moveField。 |
| 奮激變體 | 共用 MobileFenji 原定義並升級 TriggerSkillV2，保留 mobilefenji 翻譯與 AI；按手殺版於回合結束（切換至 NotActive）詢問，空手牌角色先摸兩張，持有者後失去 1 點體力。取代 H 版結束階段開始的時機。 |
| 謀斷 V2 | 直接讀取共用 HXxyRecord 的 PhaseUsedCards 值快照，於結束階段開始檢查本回合出牌階段使用過四種花色或基本／錦囊／裝備三種類別；無花色不算花色，技能牌及純打出不計。共用紀錄於 NotActive 清除，多個出牌階段累積。按有效技能實例觸發，不另建計數器。BGM 文／武切換版與此效果不同，不引用。 |
| 謀斷移牌 | 直接以 canMoveField("ej") 檢查可移牌，並呼叫 moveField(owner, objectName(), true, "ej")；不保留技能內的 destinations 或選牌副本。共用 moveField 修正空裝備欄位判斷，統一來源／牌／目標合法性與回覆後核對，使用既有 movefield 提示與技能 _from／_to 選擇介面。 |
| 翻譯 | 共用技能沿原版翻譯，不另建立國戰同效果鍵。工作區翻譯清理未產生本提交所需的新鍵。 |
| 四技能共用決定 | 反間沿原版 fanjian V2：只有不同花色裝備、沒有手牌時仍可選展示／棄牌。短兵與奮迅沿原版 duanbing／fenxun V2。刪除 h-standard-wu-variants.cpp/.h 及其 CMake、套件註冊接線。 |
| 天香身份局變體 | 小喬改引用十週年 tenyeartianxiang，其 ViewAsSkillV2 選牌入口共用原 TenyearTianxiangCard；紅桃手牌防止傷害，再選無來源 1 點傷害並摸至多 5 張，或失去 1 體力並取得棄牌。不保留舊 heg_tianxiang 各選項每回合一次及原傷害來源限制。外層仍為回應派送器，接受回應才發動 V2，取消不記一次發動。 |
| 回應與後續入口 | `GuoseViewAsSkill`、`LiuliViewAsSkill`、`TianxiangViewAsSkill` 改為 V2；流離／天香外層保留原回應詢問入口，接受回應才進入 V2，避免取消時記錄發動或重複發動。國色外層 CardFinished 摸牌及 `#tianxiang` 傷害後續仍共用原版，保留技能於用牌途中失去時的後續效果。 |
| 梟姬發動粒度 | 每次移牌事件一個 V2 activation；按離開裝備區的牌數逐次詢問並摸牌，拒絕即停止，其餘次數保存在當次 SkillContext。並非每張裝備各自獨立的 V2 攔截。 |
| 原版相容入口 | 保留原技能 ID、翻譯、AI 與具名 SkillCard 序列化；V2 view-as 使用 request 驗證並建立原卡牌，原卡牌管線仍負責目標／付款／效果，不再另建國戰副本。被動修正與效果後續 helper 依原版接線，不宣稱所有 helper 均已 V2 化。 |
| 孫策魂殤 | 改為授予共用 `yinghun`／`yingzi`；只撤銷本次暫時授予的技能，保留角色原有同名技能。不保留 `HYinghun`／`HYingzi` 類別。 |
| 本地待提交測試來源 | 吳將共用註冊／related helpers、度勢 V2 契約，以及制衡最大體力與當前體力差異、空選／超量／重複拒絕、付款摸牌及單次配額案例；付款後精確揭將案例使用度勢的普通牌轉換。好施案例查共用 ID。 |
| 驗證 | 靜態引用與差異檢查；建置、focused executable、GUI、完整局與 CI 均 NOT RUN。本地 CTest 未執行。 |

工作區既有吳將共用註冊契約補上 V2 類型斷言；該整合測試含其他未提交工作，本提交不納入。原碼已調整不等於暗將、多實例、AI 或完整對局已驗收；本輪未建置或執行測試。
