# GuhuoDialog / JuguanDialog 重構說明

## 概述

目前定案是只將 `JuguanDialog` 改為 Dashboard 手牌區 presenter；`GuhuoDialog` 因候選牌數量過大、擴展包會繼續膨脹，回退為原本的彈出式對話框流程。

另見：`docs/guhuo-juguan-presenter-impact.md`，說明這一輪是否影響 SmartAI、哪些層級有變更、哪些協議保持不變。

## 安全分階段方案

### 第一階段：抽離選項邏輯，不更動主流程

- 保留既有 `RoomScene::onSkillActivated()` → `Dashboard::startPending()` 流程
- 在 `GuhuoDialog` / `JuguanDialog` 抽出可重用 API：`prepareOptions()`、`getOptionNames()`、`getOptionCard()`、`applyOption()`、`shouldPopup()`、`hasEnabledOptions()`
- 共享 dialog 只保留選項準備與可用性判斷；`GuhuoSlash` / `NosGuhuoSlash` / `OLGuhuoSlash` 等狀態仍留在各自 `validate()` / `validateInResponse()` 路徑
- `normal_slash` 改為獨立 clone card，避免不同選項共用同一個 `Card *`

### 第二階段：新增 Dashboard presenter

- Dashboard 只負責顯示 `juguan` 類選項，不直接重寫規則
- presenter 應使用第一階段 API 讀取選項，確認後仍呼叫 `applyOption()` 回寫原本 tag
- `RoomScene` 只增加很薄的一層 presenter：目前只攔 `JuguanDialog` 的 skill activation，先在 `Dashboard` 顯示專用 option item，按下確認後才回到既有 `onSkillActivated()` / `startPending()`
- `Dashboard` 的 option item 只保存 `optionName` / `tooltip` / `enabled` 狀態，不直接持有或重用 `CardItem`，避免虛擬牌 id 與生命週期耦合
- option item UX 改成單列疊放，滑鼠移入或選中時再抬高顯示，接近既有手牌區視覺
- presenter 顯示期間會封住 `showCardFilterContainer()`，避免手牌分類視窗覆蓋在 option item 上

### Guhuo 的回退決策

- `GuhuoDialog` 曾短暫接入同一條 presenter 路徑，但 `yuji` 類技能可列出幾十種候選牌，單列疊放在擴展包較多時仍然過度擁擠
- `Guhuo` 還保留 `slash` / `normal_slash` / 回應路徑等歷史兼容負擔，UI 壓縮時可讀性明顯比 `Juguan` 差
- 因此目前把 `GuhuoDialog` 恢復到原本 `popup()/exec()` 的 modal 選牌方式；`JuguanDialog` 保留 presenter，避免兩種問題混在一起

### 第三階段：切換 UI 載體

- 等 Dashboard presenter 驗證穩定後，再把 `popup()/exec()` 換成非阻塞 UI
- 此時才移除舊 dialog 外殼，避免同時存在兩套狀態機

## 交互流程

```
1. 點擊 `Juguan` 類視為技能按鈕 → Dashboard 顯示虛擬卡牌（替換手牌區）
2. 點擊虛擬卡牌 → 卡牌高亮選中
3. 點擊確定按鈕 → 發送技能使用請求
4. 再次點擊技能按鈕 → 取消選擇，恢復手牌區
```

## 流程圖

```
┌─────────────────────────────────────────────────────────────────┐
│                    1. 點擊技能按鈕                               │
│  QSanSkillButton::click() → emit skill_activated()             │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    2. RoomScene 薄 presenter                     │
│  onPresentedDialogSkillActivated()                              │
│  ├── 目前只處理 JuguanDialog                                     │
│  ├── dialog->prepareOptions()                                   │
│  ├── dialog->shouldPopup() / isButtonEnabled()                  │
│  ├── dashboard->showDialogOptions()                             │
│  └── 僅保存目前 skill button / dialog                           │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    3. Dashboard 顯示 option item                │
│  showDialogOptions()                                            │
│  ├── 隱藏手牌 m_handCards                                        │
│  ├── 建立專用 DashboardDialogOptionItem                         │
│  └── 只保存 optionName / tooltip / enabled                      │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    4. 點擊 option                                │
│  _onDialogOptionClicked()                                       │
│  ├── 清除舊選中狀態                                               │
│  ├── 記錄新的 optionName                                          │
│  └── emit dialogOptionSelectionChanged(true)                    │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    5. 點擊確定按鈕                                │
│  RoomScene::doOkButton()                                        │
│  ├── dialog->applyOption(optionName)                            │
│  ├── dashboard->hideDialogOptions()                             │
│  └── 回到 activateSkill()                                        │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    6. 發送請求                                   │
│  既有 onSkillActivated() → startPending() → useSelectedCard()   │
└─────────────────────────────────────────────────────────────────┘
```

## Dialog 類型

| Dialog | 用途 | 卡牌來源 | Lua 支援 |
|--------|------|----------|----------|
| **GuhuoDialog** | 選擇卡牌類型（基本牌/錦囊牌） | 遍歷 `BasicCard`/`TrickCard` | `guhuo_type = "lrd"` |
| **JuguanDialog** | 選擇指定卡牌名稱 | 傳入 `card_names` 字串 | `juguan_type = "slash,duel"` |
| **TiansuanDialog** | 選擇選項（非卡牌） | 傳入 `choices` 字串 | `tiansuan_type = "hp,hand"` |

> **注意**：目前 Dashboard presenter 只實作在 `JuguanDialog`；`GuhuoDialog` 已回退到原本 modal 流程。`TiansuanDialog` 選擇的是選項而非卡牌，也不適用此渲染方式。

## Lua 技能定義範例

```lua
-- GuhuoDialog
guhuo_type = "lrd"  -- l=左基本牌, r=右錦囊牌, d=延時錦囊, s=合併殺

-- JuguanDialog
juguan_type = "slash,duel"           -- 指定卡牌
juguan_type = "all_slashs"           -- 所有殺類卡牌
juguan_type = "$slash,peach"         -- $ 前綴：非出牌階段
juguan_type = "slash,duel!"          -- ! 後綴：強制彈窗

-- TiansuanDialog（不適用此渲染）
tiansuan_type = "hp,hand"
```

## 修改檔案清單

### Dashboard

| 檔案 | 變更 |
|------|------|
| `src/ui/dashboard.h` | 新增 `showDialogOptions()` / `hideDialogOptions()` / `selectedDialogOption()` 與 option item 狀態 |
| `src/ui/dashboard.cpp` | 實作專用 `DashboardDialogOptionItem`，負責顯示、選中與灰化不可用選項 |

### GuhuoDialog

| 檔案 | 變更 |
|------|------|
| `src/package/wind.h` | 新增 `getAvailableCards()`、`getSkillName()`、`_getBasicCards()`、`_getTrickCards()` |
| `src/package/wind.cpp` | 實作 `getAvailableCards()` |

### JuguanDialog

| 檔案 | 變更 |
|------|------|
| `src/package/ol.h` | 新增 `getAvailableCards()`、`getSkillName()` |
| `src/package/ol.cpp` | 實作 `getAvailableCards()` |

### RoomScene

| 檔案 | 變更 |
|------|------|
| `src/ui/roomscene.h` | 新增 presenter helper：`wireSkillDialog()`、`presentSkillDialog()`、`activateSkill()` |
| `src/ui/roomscene.cpp` | `GuhuoDialog` / `JuguanDialog` 改走 presenter；確認後仍回到既有 pending 流程 |

## 技術細節

### 虛擬卡牌渲染

```cpp
// Dashboard::showDialogOptions()
foreach (const QString &optionName, optionNames) {
    DashboardDialogOptionItem *item = new DashboardDialogOptionItem(
        this, optionName, tooltips.value(optionName), optionSize, _m_middleFrame);
    item->setEnabled(enabledOptions.contains(optionName));
    m_dialogOptionItems << item;
}
```

### 選中狀態管理

```cpp
// Dashboard::_onDialogOptionClicked()
if (m_selectedDialogOption == optionName)
    m_selectedDialogOption.clear();
else
    m_selectedDialogOption = optionName;

emit dialogOptionSelectionChanged(!m_selectedDialogOption.isEmpty());
```

### 確定按鈕連接

```cpp
// RoomScene::doOkButton()
if (dashboard->isShowingDialogOptions()) {
    dialog->applyOption(dashboard->selectedDialogOption());
    dashboard->hideDialogOptions();
    activateSkill(skill);
}
```

## 與原有彈窗流程的差異

| 項目 | 原有流程 | 新流程 |
|------|----------|--------|
| **顯示方式** | `QDialog::exec()` 阻塞彈窗 | Dashboard 手牌區渲染 |
| **選擇方式** | 點擊 `QCommandLinkButton` | 點擊 `CardItem` |
| **確認方式** | 選擇後直接確認 | 點擊確定按鈕確認 |
| **取消方式** | 關閉對話框 | 再次點擊技能按鈕 |
| **卡牌渲染** | 按鈕列表 | 標準卡牌尺寸 |
| **視覺位置** | 畫面中央 | 手牌區位置 |

## 相容性

- Lua 技能定義無需修改，完全向後相容
- 原有 `Self->setTag(skillName, card)` 邏輯保持不變
- `ViewAsSkill::getDialog()` 返回值類型不變
