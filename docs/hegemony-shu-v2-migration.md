# 國戰標準蜀將技能共用與 V2 遷移

2026-09-22。範圍為 `h-standard-shu-generals`：規則相同者直接引用現有技能；國戰差異保留 `heg_*` 並使用現有 V2 介面。既有共用技能本身不在本批重寫。

## 共用技能

| 武將 | 直接引用的技能 | 判定 |
| --- | --- | --- |
| 馬超／馬岱 | `mashu` | 依使用者決定直接引用原版馬術，移除 `HMashu`，不另做國戰距離技能。 |
| 黃月英 | `nosqicai` | 只提供錦囊距離修正，不引入新版奇才的裝備保護。 |
| 龐統 | `lianhuan`、`niepan` | 梅花手牌轉鐵索；限定棄牌、回復至三體力、摸三張、復原狀態。 |
| 臥龍 | `huoji`、`kanpo`、`bazhen` | 紅手牌轉火攻、黑手牌轉無懈、無防具時取得虛擬八卦。 |
| 劉禪 | `xiangle`、`fangquan` | 殺的基本牌支付；跳出牌階段、棄手牌令其他角色獲得額外回合。 |
| 孟獲 | `zaiqi` | 摸牌階段改為按已損體力亮牌、紅桃回復、取得其他牌。 |
| 祝融 | `lieren` | 殺造成傷害後拼點，勝利取得對方一張手牌／裝備。 |

共 11 個技能 ID。刪除對應 H 技能複本及 `HFangquanCard`；普通技能的規則、AI 及 related helper 沿現有註冊表解析，不另建別名技能。

## 國戰差異

| 技能 | V2 類型與保留行為 |
| --- | --- |
| 仁德 | `ViewAsSkillV2` 通用 proxy，給牌在 `pay`，三張門檻及回復在效果階段。累計值按實例保存，沿原實作於回合結束清除；舊 `heg_rende` mark 僅供 AI 提示。保留 `HRendeCard` history key，移除專屬卡類。 |
| 武聖 | `ViewAsSkillV2` 產生普通殺；授鉞允許使用非紅色牌。選牌按 request 計算，不依賴全域 Self。 |
| 咆哮 | `TargetModSkillV2` 提供無限殺次數；授鉞無視防具由 related `TriggerSkillV2` 對各目標結算。 |
| 觀星／遺志 | 共用 `HGuanxing` 的 `TriggerSkillV2` 實作，各自保留真正來源。發動選單選擇要明置的技能來源，選擇雙亮時於 `pay` 明置另一來源；雙亮看五張，否則按存活人數。對應實例共享一次觀星，額外取得的實例仍獨立。 |
| 空城 | `TriggerSkillV2` 在成為殺／決鬥目標後取消該目標，保留可暗置的觸發形式，不換成普通版禁止指定。 |
| 龍膽 | `ViewAsSkillV2` 互轉殺／閃；授鉞摸牌由 `TriggerSkillV2` 處理。 |
| 鐵騎 | `TriggerSkillV2` 保留普通紅色判定與授鉞非黑桃判定。原目標順序保存在該張牌，按 V2 倍率逐次詢問、判定及修改對應閃需求。 |
| 集智 | `TriggerSkillV2` 保留轉化牌必須是單張且原牌名相同的限制；不直接換成對所有非延時錦囊生效的 `nosjizhi`。 |
| 烈弓 | `TriggerSkillV2` 逐殺目標結算；授鉞射程 helper 改為 `AttackRangeSkillV2`。 |
| 狂骨 | `TriggerSkillV2` 在每次傷害前按實例保存距離，按傷害點數逐次回復；用傷害紀錄堆疊隔離巢狀傷害，於 `DamageComplete` 清理，不與普通狂骨共用 Tag。 |
| 禍首 | `TriggerSkillV2` 保留國戰的傷害來源替換及來源死亡後無來源傷害；來源 ID 保存在各張南蠻牌，避免巢狀南蠻覆蓋 Room Tag。 |
| 巨象 | `TriggerSkillV2` 保留只取得真實單張南蠻的限制，不採普通版取得任意轉化南蠻材料的行為。 |
| 南蠻免疫 helper | `TriggerSkillV2`，沿 related instance 來源明置及失效檢查，寫回 `CardEffectStruct.nullified`。 |
| 淑慎 | `TriggerSkillV2` 保留只選友方、每回復一點令其摸一張的規則；目標放入本次 `SkillContext.targets`。 |
| 神智 | `TriggerSkillV2` 保留只計算實際可棄手牌的門檻；詢問、棄牌與回復分別放入 `cost`、`pay`、`effect`。 |

一般 TriggerSkillV2 selector 使用 `skill*count`，不沿用 donor 的 `skill->target` 字串；現行 `->target` 解析僅屬裝備路徑。詢問者／事件角色／技能來源分別使用 owner、invoker、activationRef，不以事件主體替代來源。

## 接線與驗證邊界

| 範圍 | 本批內容 |
| --- | --- |
| Formation | 遺志直接建立共用 `HGuanxing("heg_yizhi")`；天覆取得／移除原有 `kanpo`。 |
| AI | 仁德改為 `ai_fill_skill.heg_rende`、`ActiveSkillCard` 與按技能 ID 分派；保留原給牌策略。遺志沿用觀星發動判斷，雙亮選項只回傳本次提供的選項。兩個受影響來源檔已回寫外部 extensions 倉庫，逐檔 SHA-256 一致；外部既存 dirty state 保留，未提交或推送。 |
| 既有測例 | 仁德改經正式 V2 proxy 提交，保留交牌、回復一次、精確明置及回合清理斷言，新增重複材料拒絕；內容檢查改驗共用 ID、V2 類型及舊卡類已移除。 |
| 翻譯 | `HStandardShuGeneral.lua` 移除已共用技能的重複鍵與舊提示；保留國戰技能並對齊現行規則，補齊 related helper 名稱。 |
| 靜態檢查 | 核對 callback 簽名、來源與 related 接線、共用技能註冊、舊類別引用及本批 whitespace。 |
| 建置／focused／CTest | **NOT RUN**：本輪未授權建置或執行驗證；不以靜態檢查代替執行結果。 |
| GUI／完整對局／CI | **NOT RUN**。 |

後續執行驗收須涵蓋仁德多實例與付款攔截、觀星／遺志各種明暗置組合、授鉞多目標及巢狀判定、共用馬術與咆哮可用性、狂骨巢狀傷害、禍首巢狀南蠻／來源死亡、巨象轉化牌排除、淑慎改目標與神智部分手牌不可棄。舊國戰完整對局結果不代表本批已通過。
