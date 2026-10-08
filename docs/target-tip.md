# Target tips（目標提示）

V2 factory 的 `target_tip` 是資料式的預覽規則。桌面選牌／選目標時，既有
Photo 及 ActionModel 會顯示提示；取消選牌、切换請求或移除候選人會清除。
合法性仍使用原本的 targetFilter、ClientCore 與伺服器驗證；提示不會修改
可選狀態、投票數、選擇順序或送出的回覆。

```lua
target_tip = {
    { text = "額外摸1", when = {
        owner = "source", card_name = "slash", candidate_wounded = true,
    } },
    { text = "不生效", when = {
        owner = "candidate", card_color = "black",
    } },
}
```

每條規則的 `when` 是 AND 條件；省略代表無条件。每次對同一 provider 查詢取
第一個命中的 `text`。不同 provider 的文字去重，最多顯示三則。
`owner` 指規則的來源：`source`（使用者技能）、`candidate`（候選目標技能），
或 `card`（選牌所關聯且使用者持有的技能）。同一人可以是使用者也是候選目標。
文字以純文字顯示，不執行 HTML 或 Lua；需要翻譯時作者可在註冊規則前完成翻譯。

| `when` 欄位 | 型別／含義 |
| --- | --- |
| `owner` | `source`、`candidate` 或 `card` |
| `card_name`, `card_type`, `card_color` | 選牌名稱、類型、顏色的精確字串 |
| `source_has_skill`, `candidate_has_skill` | 快照中已允許此 viewer 知道的技能名稱；不代表技能當下有效 |
| `candidate_self` | 候選人是否等於使用者（boolean） |
| `candidate_selected` | 是否在已選目標中（boolean） |
| `candidate_wounded` | 公開 HP 是否低於 max HP（boolean） |
| `selectable` | 既有桌面可選狀態（boolean）；只讀輸入 |
| `candidate_hp_lte`, `source_hp_lte` | 公開 HP 上限（整數） |
| `selected_target_count_lte` | 已選目標票數上限（整數） |
| `selected_card_count_gte` | 已選牌／子牌數下限（整數） |

Native `Skill::targetTip(const TargetTipQuery&)` 接受 value snapshot，沒有
Player、ServerPlayer、Card 或 Room 指標，且不是虛擬 callback。所有規則由
`Skill::setTargetTipRules(json)` 在註冊時驗證。未知欄位、錯誤型別、小數計數、
控制字元與超限輸入都拒收；失敗時清除先前規則。上限為 32 條規則、16 KiB JSON、
每則 120 個 UTF-16 code units。

Client 邊界只接受 ClientPlayer。它讀取持有實例與公開數值，不呼叫技能事件、
`hasSkill`、`isSkillInvalid`、cost、effect 或 Lua 回呼。隱藏 helper 必須沿同一
玩家的精確父實例追溯到允許 viewer 看見的 root；國戰未公開的武將來源、
缺失父實例、循環父鏈或資訊不足都不顯示。快照不含身分、其他人的手牌、牌堆、
private mark 或技能實例 state。

提示是作者聲明的預覽，不是效果預測或保證：它不執行 invalidity、動態修正、
技能付費或觸發流程。若效果會受這些條件影響，請使用帶條件的措辭，或省略提示。
桌面無選牌的請求不提供目標提示；其他前端可讀 ActionModel 的
`players[].target_tip`，本次不改動 QML。

Focused contract（使用正式 Qt 6.11 SDK）：

```sh
c++ -std=c++17 -fPIC tools/autotest/target_tip_contract_probe.cpp \
  src/client/core/game-action-model.cpp src/client/core/interaction-model.cpp \
  -Isrc/core -Isrc/client/core -I"$QTDIR/include" -I"$QTDIR/include/QtCore" \
  -L"$QTDIR/lib" -lQt6Core -o /tmp/target-tip-contract
/tmp/target-tip-contract
```
