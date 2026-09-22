# 國戰預亮機制

2026-09-22 原始碼檢查點。參考 `TODO/original` 的 `Player::hasSkill/preshowSkill`、`Room::processRequestPreshow`、`QSanSkillButton` 與 SmartAI 自動預亮；建置與執行驗證尚未進行。

| 範圍 | 行為 |
| --- | --- |
| 預亮意義 | 暗置且 `canPreshow()` 的技能必須先預亮，才列入發動候選；預亮本身不明置、不付款、不發動效果。不可預亮的技能沿用原發動規則。 |
| 精確來源 | 私有 opt-in 按 `skill#instanceID` 儲存，主副將同名技能互不干擾。helper／attached 候選沿用現有 root resolver；不以名稱合併根來源。 |
| V2／移植技能 | V2 與原版國戰相容派送器在候選、付款及明置前檢查預亮。`hasSkill()` 仍表示持有／有效。Distance／MaxCards／AttackRange V2 維持明置門檻；TargetMod V2 可預覽自己的暗來源，真正需要修正才亮將，不依賴預亮 toggle。未新增一般 V1 的國戰適配。 |
| 發動 | 預亮後仍可拒絕暗將鎖定技；接受後沿用 cost／pay／精確亮將。取消預亮會在下一個遊戲執行緒處理點生效，付款前仍可擋下該來源。 |
| AI／託管 | 與 donor 自動預亮相同，`getAI()` 接管的來源可提出詢問；保留人的預亮偏好，交還控制後恢復使用該偏好。 |
| Native 操作 | 沿用本人技能按鈕，暗置可預亮技能可切換預亮／取消；按下狀態及 tooltip 等待伺服器通知，不樂觀改動。明置／暗置後恢復對應的普通技能或預亮操作。 |
| 私有通知 | `S_COMMAND_PRESHOW` notification 的 `states` 使用精確實例鍵，只送本人；共用 ClientGameState reducer 繼續存放於本人 `preshow` 狀態。 |
| 網路請求 | typed request 為 `{schema_version:1, skill_name:"skill#id", preshowed:bool}`。拒絕非 bool；伺服器按連線持有者驗證，拒絕模糊名稱、已移除、已明置及禁止明置來源。 |
| 執行緒 | 網路回呼僅加入有上限且合併同鍵的佇列，不讀技能容器。事件入口與互動／競速等待點由遊戲執行緒套用，再回送權威狀態；命令回覆僅代表已收件。 |
| 同步／生命週期 | SkillInstance snapshot 後補送本人預亮完整狀態，涵蓋重連與他人明暗置造成的快照重建。upsert 不把 `visible` 當預亮；暗置清除該 slot，移除／清空技能清除對應私有選擇。 |

| 驗證 | 狀態 |
| --- | --- |
| 靜態核對 | 對照 donor、檢查 owner-only 邊界及實例／佇列生命週期；本批受追蹤檔案 `git diff --check`、翻譯 XML 與協定 JSON 靜態解析通過。全倉庫檢查仍有本批未修改的國戰 Lua 翻譯空白問題。 |
| 契約案例 | `tests/hegemony-rules-test.cpp`：主副同名、非法請求、候選門檻、helper、暗置／移除／重建、owner-only 通知及 snapshot 順序；protocol inventory 補非 bool 拒絕。尚未執行。 |
| 建置／focused／GUI／完整局／CI | NOT RUN。依 AGENTS.md 檢查點規則，另獲授權後才執行約定建置及短測；不執行本地 CTest。 |
| 客戶端界線 | 本批新增操作入口為 native；Web／TUI／Sheets 等可接共用協定，但其操作介面未在本批新增或驗收。 |

原版 console 自動預亮偏好設定未搬入；目前 AI／託管自動候選與 native 手動預亮已接通。國戰接管存檔仍受現有 replay/takeover 支援範圍限制，本批未擴大。
