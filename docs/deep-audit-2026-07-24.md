# 深度審計報告：Qt6/CMake 遷移補充發現

> **存檔文件（歷史分析）**：2026-07-24 審計事實快照；所列發現多數已解決或改以 compat shim 繞過，「審計事實仍然有效」僅指當時掃描結果，**不代表現況**：
> - **QTextCodec → QStringConverter（§3.1）：已解決**。engine.cpp 兩處已改用 `QStringConverter`（另見 server.cpp:1967、scenario-overview.cpp:45）；`src/pch.h:24` 僅餘無害 include。
> - **qrand/qsrand（§9 #2）：compat shim 繞過，未遷移**。`src/pch.h:34-42` 以 `inline qrand()/qsrand()`（包 `QRandomGenerator`）保留全部呼叫點（449+ 仍存在）。
> - **QRegExp（§9 #3）：保留**。Qt 6.5 仍提供（棄用）QRegExp；`pch.h:23` 顯式 include 維持可編譯，18 處呼叫點未遷移。
> - **CMake（§9 #1）：已完成**。qmake 專案已由 CMake 3.28+ 取代（`CMakePresets.json`）。
> - 現行且具規範性的實作決策請參閱 [跨平台現代化與功能移植計劃](cross-platform-modernization-plan.md)。如有衝突，以新計劃為準；尤其是 Qt 6、CMake、Android 與音訊後端策略。

> 版本：2026-07-24 ｜ 狀態：審計報告（不含代碼變更）
> 範圍：全倉四維度深度掃描（核心引擎、UI/圖形、網路/Lua、內容系統）
> 與 `modernization-roadmap.md`（2026-07-20）互補：本文件聚焦路線圖**未覆蓋**或**需重新評估**的發現

---

## 1. 審計方法

四個並行分析維度，每個維度由獨立代理深度掃描：

| 維度 | 覆蓋目錄 | 關鍵發現數 |
|------|----------|-----------|
| 核心引擎 | `src/core/` | 12 |
| UI/圖形 | `src/ui/`, `ui-script/` | 11 |
| 網路/Lua | `src/server/`, `swig/`, `lua/`, `lib/`, `tests/` | 10 |
| 內容系統 | `src/package/`, `extensions/`, `lang/`, `audio/` | 7 |

---

## 2. 專案規模快照

| 指標 | 數值 |
|------|------|
| C++ 原始檔 | ~498 檔案 |
| Lua 腳本 | ~275 檔案（`lua/` 170 + `extensions/` 105） |
| SWIG 自動生成程式碼 | `sanguosha_wrap.cxx` 151,039 行 |
| QML 特效檔 | 35 個（`ui-script/`） |
| 單元測試 | 2 個手寫測試，無測試框架，無 CI |
| 嵌入式 Lua | 5.2.4（2015 年，已 EOL） |
| 第三方依賴 | 全部 vendored（原始碼或二進位直接提交） |

---

## 3. 路線圖未覆蓋的 Qt6 阻斷點

路線圖 Phase 1 列出了 `qrand`（459 處）、`QRegExp`（32 處）、`SIGNAL`/`SLOT`（1,200 處）。
以下為**漏列**的 Qt6 硬性阻斷：

### 3.1 `QTextCodec`（2 處）

**位置**：`src/core/engine.cpp`
```cpp
QTextCodec::codecForName("GBK")
QTextCodec::codecForName("UTF-8")
```
**影響**：Qt6 完全移除 `QTextCodec`。應改用 `QStringConverter`（Qt 6.0+）。
**風險**：低。僅 2 處，GBK 編碼可用 `QStringDecoder` 替代。

### 3.2 `Q_ENUMS` → `Q_ENUM`（4+ 標頭檔）

**位置**：`skill.h`, `card.h`, `player.h`, `general.h`
```cpp
Q_ENUMS(Frequency)    // 舊式
Q_ENUM(Frequency)      // 新式（Qt 5.5+）
```
**影響**：Qt 6 中 `Q_ENUMS` 已棄用，雖仍可編譯但失去編譯期檢查。
**風險**：極低。機械替換。

### 3.3 GLSL `#version 120`（2 處）

**位置**：`src/ui/SpineGlItem.cpp`, `src/ui/SpineEffectWidget.cpp`
```glsl
#version 120  // OpenGL 2.1, 2006 年
```
**影響**：
- macOS 已棄用相容性剖面（compatibility profile）
- Qt 6 RHI 抽象層不使用 raw GLSL
- 未來 macOS 版本可能完全無法執行
**風險**：中。需升級至 `#version 150 core` 或改用 Qt 6 RHI 著色系統。

### 3.4 `beginNativePainting()` / `endNativePainting()` hack

**位置**：`src/ui/SpineGlItem::paint()`
```cpp
painter->beginNativePainting();
// 手動 rebind FBO、unbind VAO、重置 blend state
glDrawElements(...);
painter->endNativePainting();
```
**影響**：Qt 6 文件明確不鼓勵此模式。QPainter 與 raw GL 交錯會導致狀態汙染。
**風險**：高。這是 Spine 骨骼動畫的核心渲染路徑。

### 3.5 `QApplication::font("QMainWindow")`（className 多載）

**位置**：`src/core/settings.cpp`（2 處）
**影響**：Qt 6 已棄用 className 參數的 `QFont QApplication::font()` 多載。
**風險**：極低。

---

## 4. 架構層關鍵發現（路線圖未覆蓋）

### 4.1 Engine：88KB 的「宇宙物件」

`engine.cpp`（~2,600 行）同時持有 **7+ 不相關職責**：

```
Engine
├── 翻譯/i18n（addTranslationEntry, translate）
├── 卡牌管理 + 反射式複製（cloneCard via QMetaObject）
├── 技能註冊 + 生命週期
├── 遊戲模式管理
├── 武將/套件載入
├── Lua state 所有權 + 鎖管理
├── 音效播放
├── 資源別名
├── 角色映射
├── Room 註冊（thread-local map）
├── 劇本載入
└── 手冊文檔生成（！）
```

路線圖聚焦 Room/RoomScene 拆分，但 **Engine 本身的拆分同樣緊迫**。

### 4.2 已知的執行緒安全隱患（程式碼自行承認）

10 個技能 getter 使用 `static QList<const ...Skill*>` 快取，**無任何同步機制**。
程式碼內註解坦承：

```cpp
// 建議：去掉 static 緩存。
// 如果技能系統支持動態加載/卸載，static 緩存會導致存儲了過期的指針，
// 再次引發崩潰。
```

受影響函數：`getProhibitSkills`, `getDistanceSkills`, `getMaxCardsSkills`, `getTargetModSkills`,
`getInvaliditySkills`, `getGlobalTriggerSkills`, `getAttackRangeSkills`, `getViewAsEquipSkills`,
`getCardLimitSkills`, `getProhibitPindianSkills`

### 4.3 6 處 lock/tryLock 複製貼上

以下函數包含**完全相同的 10 行** mutex lock/tryLock/unlock 模式：
`correctDistance`, `correctMaxCards`, `correctCardTarget`, `correctAttackRange`,
`hasResidueUnlimited`, `correctSkillValidity`

**建議**：抽為 `ScopedSkillLock` RAII 守衛或 inline helper。

### 4.4 智慧指標使用：幾乎為零

| 類型 | 出現次數 | 位置 |
|------|---------|------|
| `std::unique_ptr` | 12 | 僅 `SpineGlItem.h`, `SpineEffectWidget.h`（新 Spine 模組） |
| `std::shared_ptr` | 0 | — |
| `QSharedPointer` | 1 檔案 | `skill-execution-registry.h`（75 行，近期重構） |
| `QPointer` | 1 處 | `skills` hash（追蹤技能生命週期） |
| 原始 `new` | 8,738 | 全專案 |

**結論**：與路線圖一致——不適合全面導入智慧指標。但 `skill-execution-registry.h` 證明新模式可行，應作為後續重構的參考範式。

### 4.5 `const_cast` 濫用

`engine.cpp` 中 3 處 `const_cast`：內部存 `QPointer<Skill>`（mutable），公開 API 回傳 `const Skill*`，
型別不匹配迫使 const_cast。

---

## 5. UI/圖形層關鍵發現（路線圖未覆蓋）

### 5.1 三種渲染系統混用

```
QGraphicsView（主場景）
  ├── QGraphicsItems（卡牌、頭像、按鈕、標記…數百個獨立 draw call）
  ├── QOpenGLWidget（Spine 骨骼動畫 viewport）
  └── QQuickWidget（QML 特效浮層，35 個 .qml 檔）
```

三個系統的 Z-order、事件傳遞、效能特性各不相同。Qt 6 的長期路徑應是 Qt Quick + RHI 統一。

### 5.2 重複的 GraphicsBox 類別

存在兩個幾乎相同的類別：
- `src/ui/graphics-box.h` / `graphics-box.cpp`
- `src/ui/graphicsbox.h` / `graphicsbox.cpp`

兩者都有 `paintGraphicsBoxStyle()`, `stylize()`, `moveToCenter()`。
其中一個極可能是合併殘留的死碼。

### 5.3 SpineEffectWidget：開發到一半被 Qt 5 限制阻擋

`roomscene.cpp:5598` 註解坦承：
```
SpineEffectWidget 無法使用，因為 FitView 已使用 QOpenGLWidget 作為 viewport，
Qt 5 禁止巢狀 QOpenGLWidget
```

這是一個**已完成但無法整合**的組件。Qt 6 是否解除此限制需驗證。

### 5.4 逐像素 CPU 9-slice 縮放

`Window` 建構子（`src/ui/window.cpp:24-41`）：
```cpp
for (int r = 0; r < out_h; r++) {
    for (int c = 0; c < out_w; c++) {
        // 逐像素計算來源座標並複製
    }
}
```
這在每次對話框彈出時執行。應改用 `QPixmap::scaled()` 或 GPU 端處理。

### 5.5 GIF 經由 QGraphicsProxyWidget

`GraphicsPixmapHoverItem` 用 `QGraphicsProxyWidget` 包 `QLabel` 來顯示 GIF 動圖。
`QGraphicsProxyWidget` 以效能差著稱（每次繪製繞道 widget 渲染管線）。

**建議**：用 `QTimer` + `QMovie::currentPixmap()` 直接在 `paint()` 中繪製。

### 5.6 QML 檔案：重複程式碼嚴重

33 個 QML 檔中，`resolveFullskinImageSource` 函數被**複製到多個檔案**，
無共用組件庫、無設計系統。每個特效是獨立的孤立實作。

---

## 6. 網路/Lua 層關鍵發現

### 6.1 網路協議現況

```
格式：[globalSerial, localSerial, packetDescription, command, messageBody]
傳輸：JSON 陣列，換行分隔，明文 TCP（無 TLS）
命令數：131+（含近期新增 S_COMMAND_SKILL_INSTANCE=130, S_COMMAND_CARD_PROVENANCE=131）
```

路線圖 Phase 2-① 的 `IRoomChannel` + `ProtocolCodec` 方案是正確方向。
補充建議：
- TLS 應納入 Phase 2（公網部署必需）
- 換行分隔在 messageBody 含 `\n` 時會斷幀（目前靠 JSON 編碼避開，但無顯式長度前綴）

### 6.2 Lua 版本與綁定層

| 項目 | 現況 |
|------|------|
| Lua 版本 | 5.2.4（2015，EOL） |
| 綁定方式 | SWIG 自動生成 `sanguosha_wrap.cxx`（151,039 行） |
| SWIG 介面 | `sanguosha.i`（~2,200 行）+ `luaskills.i`（~3,831 行） |
| Lua thread pool | 99 個 thread 從共享 `lua_State` round-robin 分配 |
| C++/Lua 技能橋接 | 20+ 種 `Lua*Skill` 包裝類別，每個 virtual method 透過 `LuaFunction`（int registry index）回呼 Lua |

**關鍵風險**：SWIG 生成層（151K 行）是整個重構中**最脆弱的點**。
任何 C++ API 改動都需要同步更新 `.i` 並重新生成。

### 6.3 Lua 5.2 → 5.4 升級注意

- Lua 5.3 引入整數型別（`lua_Integer`），可能影響 SWIG 生成的型別轉換
- Lua 5.4 改變 GC 行為（generational mode），99-thread pool 模式需重新驗證
- `LUA_REGISTRYINDEX` 語義未變，`LuaFunction`（int 索引）機制應相容

### 6.4 FMOD Ex 狀態

- FMOD Ex 是 Firelight Technologies 的遺留產品（2000s）
- `CONFIG(audio)` 已在 `.pro` 中隔離，遷移成本可控
- 替代：Qt Multimedia（Qt 6 大幅改進）、SDL2_mixer（更輕量）

---

## 7. 內容系統關鍵發現

### 7.1 新增一個武將需碰 11 個位置

```
C++ 路徑（目前主流）：
 1. package/*.h         → 宣告 Skill 類別
 2. package/*.cpp       → 實作 Skill 邏輯
 3. package/*.cpp       → 宣告 SkillCard 類別
 4. package/*.cpp       → 實作 SkillCard 邏輯
 5. package/*.cpp       → new General(...) + addSkill(...)
 6. package/*.cpp       → addMetaObject<SkillCard>()
 7. package/*.cpp       → related_skills.insertMulti()
 8. lang/zh_CN/Package/ → 翻譯文字
 9. lang/zh_CN/Audio/   → 語音台詞
10. audio/skill/        → 放置 .ogg 檔案
11. 重新編譯 C++ 專案
```

Lua 路徑省去 C++ 步驟，但仍需手寫 `sgs.Sanguosha:setAudioType(...)`。

### 7.2 音效系統：手動綁定每組武將-技能

```lua
sgs.Sanguosha:setAudioType("lvbu", "wushuang", "1,2")
sgs.Sanguosha:setAudioType("shenlvbu", "wushuang", "1,2")
sgs.Sanguosha:setAudioType("lvlingqi", "wushuang", "5,6")
```

全部音效檔平放在 `audio/skill/`。當多個擴展衝突時會直接覆蓋。

**建議**：改為慣例自動發現——`audio/skill/{generalName}/{skillName}{n}.ogg`。

### 7.3 翻譯系統：字首約定無型別

```
"key"      = 顯示名稱
":key"     = 描述
"#key"     = 標題
"$key"     = 語音台詞
"~key"     = 陣亡台詞
"@key"     = 提示文字
```

全部存在扁平 Lua table，無 schema 驗證，無國際化基礎設施。

### 7.4 巨型武將包現況

| 檔案 | 行數 | 大小 |
|------|------|------|
| `tenyear2.cpp` | 27,983 | 926 KB |
| `ol.cpp` | 27,684 | 925 KB |
| `mobile.cpp` | 19,547 | 650 KB |

這些檔案由大量重複的 C++ 樣板程式碼構成（每武將 ~50-200 行）。

**建議**：除了路線圖的檔案切分，考慮定義 JSON/YAML 格式來描述簡單技能，
讓程式碼生成器自動產生 C++/Lua。可減少 60%+ 手寫量。

---

## 8. 測試與 CI 現況

| 項目 | 現況 |
|------|------|
| 測試框架 | 無（無 Catch2/GoogleTest/QTest） |
| 測試數量 | 2 個手寫測試（replay-game-state ~55 行、skill-instance-utils ~165 行） |
| CI/CD | 無（無 `.github/`、無任何 CI 設定） |
| Lua 測試 | Lua Test Runner 存在（`lua/test/`），但未自動化 |

**建議的最小可行 CI**：
```yaml
# GitHub Actions
- Windows MSVC 編譯（Debug + Release）
- luac -p 掃描 extensions/ + lua/（語法檢查）
- 兩個現有 C++ 測試
```

---

## 9. 綜合優先級矩陣（含路線圖對照）

### 🔴 遷移阻斷點（必須先做）

| # | 項目 | 量 | 路線圖有嗎？ | 備註 |
|---|------|-----|-------------|------|
| 1 | CMake 遷移 | 全專案 | Phase 4（長期） | **應提升至 Phase 1**，Qt6 官方不再支援 qmake |
| 2 | `qrand` → `QRandomGenerator` | 459 | Phase 1 ✅ | Qt6 已移除 |
| 3 | `QRegExp` → `QRegularExpression` | 32 | Phase 1 ✅ | Qt6 已移除 |
| 4 | `QTextCodec` → `QStringConverter` | 2 | ❌ 漏列 | Qt6 已移除 |
| 5 | `Q_ENUMS` → `Q_ENUM` | 4+ headers | ❌ 漏列 | Qt6 棄用 |

### 🟡 高優先級（遷移後立即做）

| # | 項目 | 路線圖有嗎？ | 備註 |
|---|------|-------------|------|
| 6 | 建立 CI | Phase 0 ✅ | — |
| 7 | GLSL `#version 120` → `150 core` / RHI | ❌ 漏列 | macOS 相容性 |
| 8 | `beginNativePainting` 移除 | ❌ 漏列 | Qt6 不鼓勵 |
| 9 | FMOD Ex → Qt Multimedia | Phase 4 ✅ | — |
| 10 | Lua 5.2.4 → 5.4.x | Phase 4 ✅ | — |
| 11 | 補測試基礎設施（Catch2） | 部分（Phase 0 Lua test） | 需補充 C++ 測試 |

### 🟢 中優先級（架構改進）

| # | 項目 | 路線圖有嗎？ | 備註 |
|---|------|-------------|------|
| 12 | 協議層抽象（IRoomChannel） | Phase 2-① ✅ | — |
| 13 | TLS 加密 | ❌ 漏列 | `QSslSocket` |
| 14 | 消除 10 個 static 快取 race | ❌ 漏列 | 程式碼自認 |
| 15 | 消除 6 處 lock 複製貼上 | ❌ 漏列 | 抽 RAII 守衛 |
| 16 | 刪除重複 GraphicsBox | ❌ 漏列 | 死碼清理 |
| 17 | 巨型武將包分割 | Phase 3 ✅ | — |
| 18 | Room 職責拆分 | Phase 2-③ ✅ | — |

### 🔵 長期方向

| # | 項目 | 備註 |
|---|------|------|
| 19 | sol3 prototype（評估替代 SWIG） | 先做小規模驗證 |
| 20 | Protobuf/FlatBuffers 協議 | 需協議層抽象先完成 |
| 21 | Lua per-Room 隔離 | 目前共享 state，崩潰影響全服 |
| 22 | 音效自動發現（取代 setAudioType） | 簡化內容創作 |
| 23 | 技能 DSL（JSON/YAML 宣告式定義） | 減少 C++ 樣板 |
| 24 | i18n 基礎設施 | 若有意國際化 |
| 25 | QGraphicsView → Qt Quick + RHI | 長期 UI 現代化 |

---

## 10. 建議的 Phase 0.5（路線圖補充）

以下項目建議插入現有路線圖的 Phase 0 與 Phase 1 之間：

```
Phase 0.5：Qt6 阻斷點完整掃描（本文件 §3 補充項目）
  ├── QTextCodec → QStringConverter        （2 處，低風險）
  ├── Q_ENUMS → Q_ENUM                     （4+ headers，極低風險）
  ├── GLSL #version 120 → 150 core         （2 處，中風險）
  └── QApplication::font() className 多載   （2 處，極低風險）

Phase 1.5：渲染層清理
  ├── 刪除重複 GraphicsBox 類別
  ├── Window 逐像素縮放 → QPixmap::scaled()
  ├── GIF QGraphicsProxyWidget → paint() + QTimer
  └── beginNativePainting 重構（FBO 紋理方案）
```

---

## 11. 風險矩陣

| 風險 | 可能性 | 影響 | 緩解措施 |
|------|--------|------|----------|
| SWIG 重新生成失敗 | 中 | 高 | 保留舊 `sanguosha_wrap.cxx` 作為 fallback；先在小分支驗證 |
| Lua 5.4 API 不相容 | 中 | 高 | 先在獨立分支升級 Lua 原始碼，跑 Lua test runner |
| `foreach` 變 range-for 引入 detach bug | 高 | 中 | 路線圖已暫緩；逐檔案人工審查 |
| GLSL 升級後 Spine 渲染異常 | 中 | 中 | 保留舊 shader 作為 fallback；用 FBO 方案隔離 |
| CMake 遷移遺漏編譯標誌 | 中 | 中 | 從 `Makefile.Debug`/`Makefile.Release` 提取完整編譯參數 |
| 巨型檔案分割破壞 include 順序 | 低 | 中 | 每檔分割為獨立 commit；全編譯驗證 |

---

## 12. 執行規範（與路線圖 §9 一致）

| 規則 | 內容 |
|------|------|
| 批次原則 | 小批次獨立提交；每子項一批 |
| 分支策略 | 每子項一 feature 分支，自 `main` 切出 |
| Commit 風格 | 英文訊息，`refactor:`/`chore:`/`fix:` 前綴 |
| 回歸要求 | 涉及遊戲邏輯：跑 Lua Test；純語法：至少全編譯通過 |
| SWIG 同步 | 動到公開 API 時同步更新 `.i` 並重新生成 |

---

## 附錄 A：關鍵檔案清單

| 檔案 | 行數 | 角色 | 主要問題 |
|------|------|------|----------|
| `swig/sanguosha_wrap.cxx` | 151,039 | SWIG 自動生成 | 過度生成，維護風險 |
| `src/package/tenyear2.cpp` | 27,983 | 十週年武將包 | 巨型檔案 |
| `src/package/ol.cpp` | 27,684 | OL 武將包 | 巨型檔案 |
| `src/package/mobile.cpp` | 19,547 | 手殺武將包 | 巨型檔案 |
| `src/server/room.cpp` | 8,665 | 遊戲房間 | God Object |
| `src/ui/roomscene.cpp` | 6,750 | 遊戲場景 | 巨石，三種渲染混用 |
| `src/core/engine.cpp` | 2,600 | 核心引擎 | 7+ 職責 |
| `src/core/player.cpp` | 3,181 | 玩家類別 | 與 Room 耦合 |
| `src/ui/dashboard.cpp` | 2,291 | 玩家面板 | QGraphicsObject 為主 |
| `src/ui/generic-cardcontainer-ui.cpp` | 2,413 | 卡牌容器基底 | QGraphicsProxyWidget |
| `src/core/skill.cpp` | 1,451 | 技能系統 | V2 系統良好，但舊 code 殘留 |
| `swig/luaskills.i` | 3,831 | Lua 技能綁定 | 極其完整但過度 |

## 附錄 B：第三方依賴清單

| 依賴 | 版本 | 位置 | 許可證 | 建議 |
|------|------|------|--------|------|
| Lua | 5.2.4 | `src/lua/`（vendored source） | MIT | 升級至 5.4.x |
| SWIG | ? | `tools/swig/swig.exe` | GPL-like | 評估 sol3 替代 |
| Spine (C++) | 2.x | `src/spine/`（vendored source） | Spine Runtimes License | 評估升級 4.x |
| Google Breakpad | ? | `src/breakpad/`（vendored） | BSD | 考慮改用 Crashpad |
| FMOD Ex | ? | `lib/`（pre-built binaries） | 專有 | → Qt Multimedia |
| FreeType | ? | `lib/`（pre-built） | FreeType License | 改用 vcpkg/Conan |
| VLD | ? | `include/vld/`, `lib/` | LGPL | 僅 debug，可保留 |
| Qt | 5.14.2 | 系統安裝 | LGPLv3/GPLv3 | → Qt 6.x |

---

*本文件由 2026-07-24 全倉四維度深度審計生成，與 `modernization-roadmap.md`（2026-07-20）互補閱讀。*
