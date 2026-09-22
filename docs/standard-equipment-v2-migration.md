# Standard 裝備技能 V2 遷移

範圍為 `src/package/standard-cards.cpp`，包含 standard、EX 與木牛流馬。
契約沿用 [ViewAsSkillV2 遷移規範](active-skill-v2-migration-guide.md)
及 [CorrectSkillV2](engine-correct-skills.md)。

## 遷移盤點

| 技能 | V2 形式 | 保留行為 |
|---|---|---|
| 諸葛連弩 | `TargetModSkillV2` | 只修正 Residue，保留 999；國戰以武器本身轉牌時不提供次數 |
| 方天畫戟 | `TargetModSkillV2` | 最後手牌的殺增加兩個目標，不提供其他修正 |
| 馬匹 | `DistanceSkillV2` | 保留國戰實體馬判定、身份局虛擬馬及原最小／最大值合併 |
| 青龍偃月刀 | `WeaponSkillV2` | CardOffset 選出追殺機會；PreCardUsed 的追殺日誌移至 record |
| 丈八蛇矛 | `ViewAsSkillV2` | 兩張非裝備牌轉普通 Slash；保留手牌堆、出牌／回應／回應使用與 `Slash` history |
| 貫石斧選牌 | `ViewAsSkillV2` | 僅 `@axe`，恰好兩張非雞肋牌，不能選裝備中的斧；保留 DummyCard 與原 MethodDiscard 付款 |
| 木牛流馬選牌 | `ViewAsSkillV2` | 僅出牌階段一張手牌；放入私有牌堆後可選擇轉移寶物；保留 `WoodenOxCard` history 與卸裝清次數 |

其餘九個裝備觸發技已使用 `WeaponSkillV2`、`ArmorSkillV2` 或
`TreasureSkillV2`，本批不改寫其效果。

三個數值修正技採 `CorrectSkill_System`：每次查詢只計一次，再由現有
`hasWeapon`／馬匹判定控制實體裝備與虛擬裝備來源，避免按同名實例重複疊加。

V2 選牌以 `request.initiator->hasEquip(candidate)` 判斷裝備區；不能使用
引擎版固定回傳 false 的 `Card::isEquipped()`，以確保客戶端與伺服器限制一致。

木牛流馬保留帶 `Q_OBJECT`／`Q_INVOKABLE` 的 `WoodenOxCard` 名稱殼，
改繼承 `ActiveSkillCard`；不再自訂 `use()`，效果改由視為技的 V2 `effect()` 執行。
這保留舊 AI、卡名字串、`MethodNone` 和歷史名稱，無須修改外部 Lua AI。
放入牌堆屬技能效果，`willThrowSelectedCards()` 為 false，避免預設 pay 棄掉材料。

## 驗證狀態

`tests/skill-runtime-coordinator-test.cpp` 新增 `standardEquipmentV2Contracts()`：
覆蓋伺服器重建、重複／他人材料拒絕、丈八回應與 history、斧自身排除、
木牛流馬舊字串及限次、連弩國戰材料例外、方天最後手牌及馬匹修正。

- 靜態：已完成介面、選牌區域、history、舊 AI 字串核對與 `git diff --check`。
- 建置、focused executable、GUI、完整對局：**NOT RUN**，仍需檢查點授權。
- 後續驗收需包含丈八出牌／決鬥回應、斧取消／棄置一次、青龍追殺／取消、
  木牛流馬存牌／轉移／卸裝，以及三個被動修正的實際 client/server 一致性。
