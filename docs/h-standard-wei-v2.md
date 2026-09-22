# 標準國戰魏勢力技能共用與 V2

2026-09-22 原始碼檢查點。依使用者決定，優先引用效果相符的 `nos*`／既有技能；只有真正有差異的技能保留 `heg_*` 並改為 V2。武將 ID、體力、性別、珠聯璧合不變。

## 共用映射

| 國戰武將 | 直接引用的技能 ID |
| --- | --- |
| 曹操 | `nosjianxiong` |
| 司馬懿 | `nosfankui`、`nosguicai` |
| 夏侯惇 | `nosganglie` |
| 張遼 | `nostuxi` |
| 許褚 | `nosluoyi` |
| 郭嘉 | `tiandu`、`nosyiji` |
| 甄姬 | `qingguo` |
| 夏侯淵 | `shensu` |
| 張郃 | `qiaobian` |
| 徐晃 | `duanliang` |
| 曹仁 | `nosjushou` |
| 典韋 | `qiangxi` |
| 荀彧 | `quhu`、`jieming` |
| 曹丕 | `xingshang`、`fangzhu` |

18 個共用技能使用 `General::addSkill(QString)` 引用原註冊；不建立別名殼、不複製效果、不改寫共用技能。技能卡、history、回應 pattern、翻譯、AI callback 與 related helper 使用原技能定義。已移除 `HTuxiCard`、`HShensuCard`、`HQiaobianCard`、`HQiangxiCard`、`HQuhuCard` 及其 meta-object 註冊。

`HStandardWeiGeneral.lua` 只保留 15 名武將的名稱／稱號，以及洛神、洛神輔助與驍果的名稱、正文和提示。18 個共用技能的舊 `heg_*` 翻譯與提示已移除；沿用原 ID 的翻譯，不重新定義 `nos*`／既有鍵。

這裡以技能規則為共用邊界。移牌、距離檢查、拼點及代價時序採既有實作，不保留 donor 的舊流程複本；例如強襲採現有 `QiangxiCard` 結算流程。共用技能原本是 V1 的仍為 V1，本輪沒有擴大修改其多實例或國戰暗將相容性。

## 保留的差異技能

| 技能 | 差異與 V2 接線 |
| --- | --- |
| `heg_luoshen` | 黑色判定牌先留處理區，整次判定循環結束才取得；不同於既有洛神通常逐張取得。主技能與 `#heg_luoshen-move` 均使用 `TriggerSkillV2`。每次發動的牌清單保存在區域變數，不使用跨發動的 Player Tag；收牌前重新核對位置，回合中斷時清理仍在處理區的牌。 |
| `heg_xiaoguo` | 對方棄裝備時不讓技能持有者額外摸牌。候選列出所有合資格持有者；`cost` 只選基本手牌，`pay` 重檢歸屬、區域及可棄置性後付款；目標放入 `SkillContext.targets`，以 `effectTarget` 處理棄裝備或傷害，傷害使用 V2 amount。 |

暗將同意、精確來源揭將、失效檢查及多實例展開沿用 V2 管線；實作不自行揭將，也不建立共用技能的 V2 包裝層。

## 檢查與限制

| 項目 | 狀態 |
| --- | --- |
| 靜態檢查 | 核對 15 名武將、18 個引用、3 個 V2 類別、舊技能卡引用移除及 patch 空白。 |
| 工作區測試來源 | content 契約更新為共用技能指標與清單檢查；draw-events 改用 `nosluoyi`；新增 `wei-xiaoguo` 案例覆蓋多持有者、選牌不付款、重複／失效付款、棄裝不摸牌及拒棄傷害。這些跨勢力測試檔尚未追蹤，未納入本次翻譯提交，亦尚未執行。 |
| 外部 Lua AI | 共用技能改走原 ID 的 callback。`lua/ai/original-hegemony/` 仍有舊 `heg_*` 查詢及未使用的 donor callback；跨技能評估尚待另批整理，本輪未修改外部 AI 倉庫。 |
| 執行驗證 | 建置、focused executable、CTest、GUI、完整對局及 CI 均 NOT RUN；不以舊搬運結果代替本次驗證。 |

V2 契約參考 [TriggerSkillV2 系統說明](TriggerSkillV2系統說明.md)。
