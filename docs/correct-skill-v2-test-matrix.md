# CorrectSkillV2 驗證矩陣

## Fixture

| 類型 | 位置 | 覆蓋 |
|---|---|---|
| C++ `~test` | `src/package/standard-generals.cpp` | 四類 V2、五種 selector、fixed、amount 事件與 correctState |
| Lua factory smoke | `lua/test/examples/test_correct_skill_v2.lua` | 四個 factory、nil／false／true／數字／錯誤 fail-closed |
| Room integration | `lua/test/examples/test_correct_skill_v2_room.lua` | 多實例、owner 隔離、selector、set/add/reset、事件、精確失效、Residue 無限 |

fixture 技能均以 `#correct_v2_*_test` 命名。System fixture 預設不貢獻，只有 primary 的 `correct_v2_system_enabled` mark 大於零才啟用，避免影響正常對局。

## 自動／手動狀態

| 項目 | 狀態 | 證據／限制 |
|---|---|---|
| Release x64 | 通過 | MSBuild `0 errors`；最後一次增量建置 185 warnings |
| SWIG 重新產生 | 通過 | `tools/swig/swig.exe -c++ -lua swig/sanguosha.i` |
| TriggerEvent enum 對齊 | 通過 | core 與 SWIG 均 138 項，順序一致 |
| Lua process smoke | 環境阻塞 | executable 啟動回傳 `-1073741701`（Windows image／runtime 問題），未進入 Lua assertion |
| console tests | 環境阻塞 | 現有 console executable 同樣無法在本機 runtime 啟動 |
| 完整 Room lifecycle | 待可執行環境 | fixture 已加入，需在可正常啟動 Release executable 的機器執行 |

## Room integration 期望

| 案例 | 期望 |
|---|---|
| 同一玩家 Primary 兩實例 | 兩次 callback、signed 值相加 |
| 另一玩家同名同 ID | 不污染 Primary 的 holder 集合 |
| Secondary 無 `to` | 零貢獻 |
| Participants | primary／secondary 去重 |
| AllHolders | 全場存活 holder 各實例 |
| System | 無 ref、共享 base、只計算一次 |
| amount set/add/reset | 零與負數保留；reset 回 base |
| Changing modify/cancel | 修改值生效；取消不寫入、不發 Changed |
| 同 ref 遞迴 | nested 寫入回傳 false |
| correctState | 單 key set/remove 只影響指定實例 |
| exact invalidity | 只排除指定 instanceID |
| fixed | 適用結果取最大 |
| TargetMod Residue `-1` | `hasResidueUnlimited()` 為 true |
| 其他 `-1` | 保留有號整數，不轉 1000 |

## 待補環境驗證

- 兩個實際 client 的 snapshot 重連、amount/state delta 與隱藏 metadata 權限封包。
- Legacy Mashu、MaxCards、TargetMod、AttackRange 的實際對局回歸錄像／快照。
- ViewAsSkillV2 的 Play、response-use、pure response、nullification、AI、UI 與中斷 lifecycle；未通過前仍不開放正式技能填充。
