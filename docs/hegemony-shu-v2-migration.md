# 國戰標準蜀將技能共用與 V2 遷移

2026-09-24 原始碼檢查點。範圍為 `h-standard-shu-generals`：規則相同者在身份包升級原技能為 V2，國戰直接引用同一 ID；國戰規則差異保留 `heg_*`。

## 共用技能

| 武將 | 直接引用的技能 | 判定 |
| --- | --- | --- |
| 劉備／關羽／諸葛亮／甘夫人 | `rende`、`tenyearwusheng`、`guanxing`、`shushen`、`shenzhi` | 原定義直接原生 V2，模式差異在同一實作中處理，移除 H 副本。 |
| 馬超／馬岱 | `mashu` | 直接引用原版馬術，移除 `HMashu`。原版已改為 `DistanceSkillV2`，每個有效實例預設距離 −1，由引擎統一處理國戰明暗置及精確失效，不另做國戰距離技能。 |
| 龐統 | `lianhuan`、`niepan` | 梅花手牌轉鐵索；限定棄牌、回復至三體力、摸三張、復原狀態。 |
| 黃月英 | `nosqicai` | 錦囊無距離限制，原版改為 TargetModSkillV2；不採額外保護裝備的 `qicai`。 |
| 臥龍 | `huoji`、`kanpo`、`bazhen` | 紅手牌轉火攻、黑手牌轉無懈；原版改用 ViewAsSkillV2。八陣共用原裝備來源與 V2 八卦陣流程。 |
| 劉禪 | `xiangle`、`fangquan` | 殺的基本牌支付；跳出牌階段、棄手牌令其他角色獲得額外回合。 |
| 魏延 | `tenyearkuanggu` | 共用原版變體，逐點選擇回血／摸牌；普通 `kuanggu` 不變。 |
| 祝融 | `lieren` | 殺造成傷害後拼點，勝利取得對方一張手牌／裝備。 |

共 16 個共用技能 ID（含已是 V2 的馬術）。本批在原身份包改寫連環、火計、看破的 `ViewAsSkillV2`，以及涅槃、享樂、放權、烈刃的 `TriggerSkillV2`。刪除 `HegXiangle` 與註冊／重複翻譯，國戰劉禪改引用 `xiangle`。

| 技能 | 本批生命週期與相容契約 |
| --- | --- |
| 連環、火計、看破 | 依 request 選牌並重查單張材料，保留原花色、點數、手牌／可回應手牌堆，以及普通卡重鑄／結算流程；不新增卡類或 AI 協定。 |
| 涅槃 | cost 詢問，pay 消耗限定標記，effect 沿原順序棄牌、回復、摸牌與復原狀態。 |
| 享樂 | 由 V2 chooser 處理暗置鎖定技的選擇／明置；共用原版基本牌支付效果，付款詢問後重讀殺資料，保留巢狀觸發修改。 |
| 放權 | 跳階段資格按技能實例保存；視為技入口升級 V2，保留 `@@fangquan`、`FangquanCard` 與其單一棄牌／效果實作。待執行額外回合以給予者的目標 ID 清單保存，global V2 record 在回合結束排程並清理；已付款後失去技能不取消額外回合。 |
| 烈刃 | cost 詢問並保存本次目標；effectTarget 重查拼點資格，勝利後取得牌，保留原本連環／轉移傷害排除條件。 |

奇才與八陣的明暗置、失效及精確來源由既有引擎流程處理，不另設國戰副本。八陣沿用 `ViewAsEquipSkill` 來源介面與 `ArmorSkillV2` 八卦陣；通知 helper `#bazhen` 改為 `TriggerSkillV2`，不新增轉接基底。再起 `heg_zaiqi` 是棄牌階段結束按本回合紅色棄牌數選友方，與身份版摸牌階段亮牌不同，保留在蜀勢力檔。

## 國戰差異與共用技能的模式分支

| 技能 | V2 類型與保留行為 |
| --- | --- |
| 仁德 | 原版 `rende` 的 ViewAsSkillV2 統一給牌／實例記錄；國戰每名角色限收一次，兩張門檻獎勵使用基本牌；身份局保留回血及續給牌。`Config.EnableHegemony` 分支，history key `RendeCard`，卡類退役。 |
| 武聖 | 共用十周年變體 `tenyearwusheng`，直接 ViewAsSkillV2；既有方塊殺距離 helper 改 TargetModSkillV2，國戰授鉞允許非紅色牌。普通 wusheng 不增加距離或授鉞分支。 |
| 咆哮 | `TargetModSkillV2` 提供無限殺次數；授鉞無視防具由 related `TriggerSkillV2` 對各目標結算。 |
| 觀星／遺志 | 共用 `Guanxing` 的 `TriggerSkillV2` 實作，各自保留真正來源。發動選單選擇要明置的技能來源，選擇雙亮時於 `pay` 明置另一來源；雙亮看五張，否則按存活人數。對應實例共享一次觀星，額外取得的實例仍獨立。 |
| 空城 | `TriggerSkillV2` 在成為殺／決鬥目標後取消該目標，保留可暗置的觸發形式，不換成普通版禁止指定。 |
| 龍膽 | 原生 V2 互轉殺／閃；CardOffset 時，轉換殺可對閃避者以外角色造成傷害，轉換閃可令使用者與自身以外角色回復；授鉞只於用牌／回應摸牌。 |
| 鐵騎 | 逐目標判定，令所選明置武將牌的非鎖定技能本回合失效；授鉞影響全部明置武將牌。目標不棄同花色牌則不能出閃，來源失效按精確實例處理。 |
| 集智 | `TriggerSkillV2` 保留轉化牌必須是單張且原牌名相同的限制；不直接換成對所有非延時錦囊生效的 `nosjizhi`。 |
| 烈弓 | `TriggerSkillV2` 逐殺目標結算；授鉞射程 helper 改為 `AttackRangeSkillV2`。 |
| 狂骨 | 共用 tenyear-strengthen.cpp 原版變體 `tenyearkuanggu`，直接 TriggerSkillV2；保留回血／摸牌及既有升級標記分支。距離按傷害／實例堆疊保存，DamageComplete 清理；移除 HKuanggu，普通 kuanggu 及其記錄 helper 維持原狀。 |
| 禍首 | `TriggerSkillV2` 保留國戰的傷害來源替換及來源死亡後無來源傷害；來源 ID 保存在各張南蠻牌，避免巢狀南蠻覆蓋 Room Tag。 |
| 巨象 | `TriggerSkillV2` 保留只取得真實單張南蠻的限制，不採普通版取得任意轉化南蠻材料的行為。 |
| 南蠻免疫 helper | `TriggerSkillV2`，沿 related instance 來源明置及失效檢查，寫回 `CardEffectStruct.nullified`。 |
| 淑慎 | 共用原版 `shushen`，目標放入 SkillContext；國戰令任意其他角色摸一張，身份局可回復一點或摸兩張。 |
| 神智 | 共用原版 `shenzhi`，cost/pay/effect 分離；國戰按可棄手牌計數，身份局保留全手牌可支付條件。 |

一般 TriggerSkillV2 selector 使用 `skill*count`，不沿用 donor 的 `skill->target` 字串；現行 `->target` 解析僅屬裝備路徑。詢問者／事件角色／技能來源分別使用 owner、invoker、activationRef，不以事件主體替代來源。

## 接線與驗證邊界

| 範圍 | 本批內容 |
| --- | --- |
| Formation | 遺志直接建立共用 `Guanxing("heg_yizhi")`；天覆取得／移除原有 `kanpo`。 |
| AI | 仁德統一 `ai_fill_skill.rende` 與 ActiveSkillCard；國戰 AI 共用原 ID，保留各模式策略。龍膽傷害／回復選人鍵對齊，來源檔回寫外部倉庫前後核對雜湊。 |
| 既有測例 | 仁德經正式 V2 proxy 提交，驗證交牌、兩張門檻的基本牌詢問僅一次、重複收牌者拒絕、精確明置及階段清理；保留重複材料拒絕。內容檢查驗證共用 ID、V2 類型及舊卡類已移除。 |
| 翻譯 | `HStandardShuGeneral.lua` 移除已共用技能的重複鍵與舊提示；保留國戰技能並對齊現行規則，補齊 related helper 名稱。 |
| 靜態檢查 | 核對 callback 簽名、來源與 related 接線、共用技能註冊、舊類別引用及本批 whitespace。 |
| 建置／focused／CTest | **NOT RUN**：本輪未授權建置或執行驗證；不以靜態檢查代替執行結果。 |
| GUI／完整對局／CI | **NOT RUN**。 |

後續執行驗收須涵蓋仁德多實例與付款攔截、觀星／遺志各種明暗置組合、授鉞多目標及巢狀判定、共用馬術與咆哮可用性、狂骨巢狀傷害、禍首巢狀南蠻／來源死亡、巨象轉化牌排除、淑慎改目標與神智部分手牌不可棄。舊國戰完整對局結果不代表本批已通過。

## 2026-09-24 靜態檢查點

- 既有 content contract 補原版 V2 型別、國戰引用、看破舊無懈預檢介面及 `heg_xiangle` 退役檢查；未執行。
- 享樂 AI 的技能查詢、回呼、棄牌提示及 alias 更新至 `xiangle`；5 個檔案回寫 H 外部權威倉庫，逐檔 SHA-256 一致。保留既存 dirty state 與 ahead/behind 分歧，未提交或推送。
- 本批僅做來源、介面、註冊、翻譯及 whitespace 靜態檢查。建置、focused executable、CTest、GUI、完整對局及 CI 均 NOT RUN。
- 待授權的 focused 驗證需涵蓋轉化牌材料／重鑄／無懈、涅槃付款與救援順序、明暗置享樂、放權額外回合及失去來源清理、烈刃拼點與目標攔截。
