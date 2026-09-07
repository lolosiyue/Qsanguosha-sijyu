# CorrectSkillV2 驗證矩陣

> **現況（2026-09-06）**：本表自 2026-07-24（commit 3240e46）後未維護。`lua/test/` 已整個刪除（commit a904221，由 CTest＋`tools/autotest/` 取代）；原「executable 啟動失敗（`-1073741701`／0xC000007B 等環境阻塞）」已解除——專案已遷 CMake＋Qt 6.11.1（`tools/build-cmake.ps1`／`tools/build-release.ps1`），executable 可正常啟動。下表「狀態」欄為 2026-07-24 當時結論，無法本機重驗的測項一律標「待重審」。

## Fixture

| 類型 | 位置 | 覆蓋 |
|---|---|---|
| C++ `~test` | `src/package/standard-generals.cpp` | 四類 V2、五種 selector、fixed、amount 事件與 correctState |
| Lua factory smoke | `lua/test/examples/test_correct_skill_v2.lua`（**已移除，失效**） | 四個 factory、nil／false／true／數字／錯誤 fail-closed |
| Room integration | `lua/test/examples/test_correct_skill_v2_room.lua`（**已移除，失效**） | 多實例、owner 隔離、selector、set/add/reset、事件、精確失效、Residue 無限 |

fixture 技能均以 `#correct_v2_*_test` 命名。System fixture 預設不貢獻，只有 primary 的 `correct_v2_system_enabled` mark 大於零才啟用，避免影響正常對局。

> **現況（2026-09-06）**：上列兩個 Lua fixture 隨 `lua/test/` 整個刪除（commit a904221「test: remove obsolete lua/test example suite (superseded by autotest tooling)」），該覆蓋由 CTest＋`tools/autotest/` 取代；C++ `~test` fixture（`src/package/standard-generals.cpp`）仍存在。

## 自動／手動狀態

| 項目 | 狀態 | 證據／限制 |
|---|---|---|
| Release x64 | 待重審 | 2026-07-24 舊證據（MSBuild `0 errors`／185 warnings）已過時：建置入口已改 CMake（`tools/build-cmake.ps1`／`tools/build-release.ps1`），需以新流程重審 |
| SWIG 重新產生 | 待重審 | 舊手動命令 `tools/swig/swig.exe -c++ -lua swig/sanguosha.i` 已棄用：現由根 `CMakeLists.txt` 的 `add_custom_command` 於 build tree 自動生成（`builds/cmake-vs2026/generated/sanguosha_wrap.cxx`） |
| TriggerEvent enum 對齊 | 待重審 | 舊結論「138 項」過時：`src/core/structs.h` 的 `enum TriggerEvent` 現為 144 項（2026-09-06 實測）；SWIG 對齊需重新驗證 |
| Lua process smoke | 待重審 | 原「環境阻塞（executable 啟動回傳 `-1073741701`，0xC000007B）」已失效：CMake＋Qt 6.11.1 後 executable 正常啟動，需重跑 |
| console tests | 待重審 | 原環境阻塞已解除；現有 CTest server unit／CLI suite 是否涵蓋原範圍需重審 |
| 完整 Room lifecycle | 待重審 | C++ fixture 仍在 `src/package/standard-generals.cpp`；環境已可執行，需以 CTest／`tools/autotest/` 實跑（Lua room integration fixture 已隨 `lua/test/` 移除） |

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
