# Scarlet V2 isolated AI：原版策略移植

實作在 `lua/ai/isolated/scarlet-ai.lua`，外部權威來源為 extensions 倉庫的 `ai/isolated/scarlet-ai.lua`。本批覆蓋藏拙／志繼所需共用路徑，不表示 Scarlet 全包或 SmartAI 全部策略已移植。

| 技能 | 原版決策順序 |
|---|---|
| 藏拙 | `getBestTargetOr`：正分候選、推薦 hook／veto、加權隨機；無正分才依序 `findPlayerToDraw`、可摸牌好友、首名候選 |
| 志繼 | `addHandPile("he")` 計黑牌；`canDamage`、`canLoseHp`、桃酒估算及 `ajustDamage` 檢查背水存活；依原順序作 40%／20% 背水、60% 看破、70% 觀星判斷；最後隨機選觀星／看破，無此兩項則首選项 |

藏拙沿用原版 `draw` 分類，雖然規則實際獲得【影】。fallback 只在本次合法候選內運作，不呼叫 legacy SmartAI。志繼的 40% 與 20% 為兩次依序試擲，不是相加為 60%。無跨詢問 facade 快取；既有 dispatcher 保留技能實例身份。

## 共用接口與覆蓋邊界

| 檔案／接口 | 本批覆蓋 | 明確未覆蓋 |
|---|---|---|
| `isolated-facades.lua`：`getHandPileCards`、`addHandPile` | he 與手牌堆的純值卡牌；保持順序、複製輸入、不修改原快照 | 舊快照有 ID 而無非空牌堆 metadata 時 unsupported |
| `isolated-facades.lua`：`getAllPeachNum` | 完殺、自救桃酒、可見 he 牌、原版少量暗牌估算；急救紅牌須能判定 CurrentPlayer／禁桃狀態 | 其他未移植 view-as、未知關係／禁桃狀態 unsupported；不把未登記 hook 當成無轉化能力 |
| `strategy-hooks.lua`：分類、`needDraw`、`getTargetBaseScore`、`getBestTarget`、`getBestTargetOr` | draw 評分、主公／objective 修正、技能與全域推薦註冊、rende／s4_qiaobian 的 draw 推薦、加權隨機 | damage／decrease／recovery 評分，以及任意未移植套件推薦規則；不是完整 SmartAI 推薦器 |
| `decision-core.lua`：`ajustDamage`、`damageIsEffective`、`canDamage`、`canLoseHp` | 無牌普通傷害，包括志繼自傷；公開技能／標記、上限、體力流失替代、傷害 hooks、原版存活判斷 | 卡牌／元素與鐵索傷害、官渡事件、部分私有屬性與對敵收益策略會 unsupported；任意套件 hook 的缺省覆蓋仍需逐包遷移 |

native 只新增純值投影：可見牌堆的 `cards` metadata 與已投影技能的 `has_view_as_skill`。前者沿用 `pileView.open`；後者不公開隱藏技能。無新增 native 呼叫通道。

原版已存在的行為保留：draw 基礎評分中的 `needDraw(to, notDraw)` 使用未定義變數，通常不加分；傷害估算中的 `getMark("@inu_to")` 即使 0 也為 Lua truthy。這些不是本批修正的規則。志繼仍按原 AI 的基準 1 傷估算，未新增 V2 修改後成本的權威預演。

## 實作前列出的失敗情境與待驗收

| 情境 | 預期 |
|---|---|
| 非空牌堆 metadata 缺失、ID 不匹配或閉合牌堆 | 不回部分卡牌集合 |
| 未知關係、救援轉化或傷害條件 | unsupported，不當成 0、非友方或安全自傷 |
| 推薦 hook veto／非有限分數 | veto 排除該目標；壞資料 unsupported |
| 無正分且全為敵方候選 | 按原版 fallback 首名，不擅自改成 pass |
| 背水安全成立／不成立／隨機邊界 | 保留原版短路順序及 RNG 試擲次數 |
| 借用／多實例 | 純值 handler 無跨實例快取；dispatcher 身份與 native 拒絕仍待執行驗證 |

## 驗證狀態

完成原始碼整合、靜態審查與 Lua 語法解析。尚未執行 build、Lua handler、native focused、CTest、完整對局或 CI；新 native 投影須重新建置後才可使用。既有備陣／伐逆不在本批審查範圍。完整契約見 [isolated AI 寫法](isolated-ai-authoring-guide.md)。
