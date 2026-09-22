# 舊 hegemony 共用技能歸位與 V2 遷移

2026-09-22 原始碼檢查點。刪除 `src/package/hegemony.cpp/.h` 及 CMake source/header 條目，移除 StandardPackage 的 `addSharedGeneralSkills` 註冊鉤子。八個共用技能保留原技能 ID；新版國戰繼續引用同一份定義。

| 身份武將／來源 | 技能 | 位置與形式 |
| --- | --- | --- |
| SP 甘夫人 | shushen、shenzhi | `sp.cpp`，TriggerSkillV2 |
| OL 樂進、潘鳳 | xiaoguo、kuangfu | `ol.cpp`，TriggerSkillV2 |
| Mobile 丁奉 | fenxun | `mobile.cpp`，ViewAsSkillV2 |
| Mobile 何太后 | zhendu、qiluan | `mobile.cpp`，TriggerSkillV2 |
| Mobile 丁奉／其他共用者 | duanbing | `mobile.cpp`，TargetModSkillV2；距離 1 條件歸入技能，Slash 透過既有候選目標修正查詢取得額外名額。 |

## 生命週期

| 技能 | 遷移契約 |
| --- | --- |
| 骁果、鸩毒 | cost 只選牌，pay 重查持有者、手牌區及可棄置性後付款；targets 明確經 V2 單目標攔截。骁果枚舉所有持有者，保留身份版「對方棄裝備後本人摸一張」。 |
| 淑慎 | 依回復點數產生候選次數；cost 選其他角色，effectTarget 執行回復一點或摸兩張。 |
| 神智 | 候選與付款均檢查整手可棄；一次支付整手並凍結張數，依支付後體力判斷回復。 |
| 短兵 | 候選目標需距離 1；轉化時消耗進攻馬須排除其距離修正。既有 V2 collector 處理實例有效性、數值及來源；需借助額外目標時才明置。 |
| 奮迅 | 每實例每出牌階段一次；通用 ActiveSkillCard 付款，保留 FenxunCard history key，刪除舊 SkillCard 類別。持續距離記錄保留所有已選對象，global `#fenxun-clear` 生命週期 helper 在回合結束／本人死亡清理，失去技能後仍可清理。該 helper 沿用既有 TriggerSkill 系統回呼，不是可發動技能。 |
| 戚亂 | record 按實例記錄擊殺，只在 Death 的死者席次記一次；回合結束將計數轉成固定候選次數。cost 詢問，effect 每次摸三張；移除實例不向新實例轉移擊殺記錄。 |

`heg_xiaoguo`、`heg_shushen`、`heg_shenzhi` 有不同國戰規則，本次保留其已有 V2 實作。沒有啟用 `extensions/temp/extraheg.lua`，只調整 Slash 的既有 ExtraTarget 查詢接線，未新增引擎介面。

共用技能正文／提示／語音字幕移入 SP、Mobile、OL 翻譯檔；HFormation 不再提供身份版鸩毒／戚亂翻譯。刪除 `HegemonyPackage.lua`、`HegemonyPackageLines.lua` 及 manifest 載入入口；身份版樂進、甘夫人、丁奉、潘鳳的配音別名移到對應包，珠聯璧合沿用 `HegemonyMode.lua`，其餘退役包專用翻譯刪除。現有 Lua loader 仍載入 `hegemony-ai.lua`，其中奮迅改用 `ai_fill_skill.fenxun` 與 `ai_skill_use_func.fenxun`，不再解析已刪除的 FenxunCard。

## 驗證

| Gate | 狀態 |
| --- | --- |
| 定向來源／註冊／引用／翻譯靜態檢查、git diff --check | 完成 |
| 既有 content contract | 工作樹內已補 V2 型別、FenxunCard 退役及 wu-skills 回歸測例，未執行；其所在整套測試檔原先尚未追蹤，本次提交不混入其他任務的測試套件 |
| 建置、focused executable、CTest、完整對局、GUI、CI | NOT RUN；等待檢查點建置／驗證授權 |

後續建議 gate：重新 configure（刪除了 CMake source/header），只建置受影響 GUI/server 與 content test target；授權後執行 60 秒內的既有 focused content／技能驗證，不執行 CTest。
