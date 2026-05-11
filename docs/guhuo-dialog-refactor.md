# GuhuoDialog / JuguanDialog 重構說明

## 概述

將 `GuhuoDialog` 和 `JuguanDialog` 從彈出式對話框改為 Dashboard 手牌區渲染虛擬卡牌。

## 交互流程

```
1. 點擊視為技能按鈕 → Dashboard 顯示虛擬卡牌（替換手牌區）
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
│                    2. RoomScene 攔截                             │
│  onGuhuoSkillActivated() / onJuguanSkillActivated()            │
│  ├── dialog->getAvailableCards()                                │
│  ├── dashboard->showGuhuoCards()                                │
│  ├── ok_button->setEnabled(true)                                │
│  └── connect(ok_button, clicked, _onGuhuoConfirm)              │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    3. Dashboard 渲染虛擬卡牌                      │
│  showGuhuoCards()                                               │
│  ├── 隱藏手牌 m_handCards                                        │
│  ├── 建立 CardItem（虛擬卡牌）                                    │
│  └── _m_guhuoSelected = nullptr                                 │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    4. 點擊虛擬卡牌                                │
│  _onGuhuoCardClicked()                                          │
│  ├── 清除舊選中狀態                                               │
│  ├── 設置新選中狀態（高亮）                                        │
│  ├── Self->setTag(skillName, card)                              │
│  └── _m_guhuoSelected = item                                    │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    5. 點擊確定按鈕                                │
│  ok_button::click() → _onGuhuoConfirm()                         │
│  ├── 檢查 _m_guhuoSelected                                      │
│  ├── emit guhuoCardSelected(card)                               │
│  └── hideGuhuoCards()                                           │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    6. 發送請求                                   │
│  onGuhuoCardSelected() → useSelectedCard()                      │
└─────────────────────────────────────────────────────────────────┘
```

## Dialog 類型

| Dialog | 用途 | 卡牌來源 | Lua 支援 |
|--------|------|----------|----------|
| **GuhuoDialog** | 選擇卡牌類型（基本牌/錦囊牌） | 遍歷 `BasicCard`/`TrickCard` | `guhuo_type = "lrd"` |
| **JuguanDialog** | 選擇指定卡牌名稱 | 傳入 `card_names` 字串 | `juguan_type = "slash,duel"` |
| **TiansuanDialog** | 選擇選項（非卡牌） | 傳入 `choices` 字串 | `tiansuan_type = "hp,hand"` |

> **注意**：`TiansuanDialog` 選擇的是選項而非卡牌，不適用此渲染方式。

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
| `src/ui/dashboard.h` | 新增 `m_guhuoItems`、`m_guhuoCardMap`、`_m_guhuoActive`、`_m_guhuoSkillName`、`_m_guhuoSelected` |
| `src/ui/dashboard.cpp` | 實作 `showGuhuoCards()`、`hideGuhuoCards()`、`_adjustGuhuoCards()`、`_onGuhuoCardClicked()`、`_onGuhuoConfirm()` |

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
| `src/ui/roomscene.h` | 新增 `onGuhuoSkillActivated()`、`onGuhuoSkillDeactivated()`、`onGuhuoCardSelected()`、`onGuhuoCancelled()` 及 Juguan 對應方法 |
| `src/ui/roomscene.cpp` | 攔截技能按鈕點擊，連接確定按鈕 |

## 技術細節

### 虛擬卡牌渲染

```cpp
// Dashboard::showGuhuoCards()
foreach (Card *card, cards) {
    CardItem *item = new CardItem(card);  // 直接使用 Card*，不依賴 ID
    item->setParentItem(_m_middleFrame);
    item->setZValue(100);
    connect(item, SIGNAL(clicked()), this, SLOT(_onGuhuoCardClicked()));
    m_guhuoItems.append(item);
    m_guhuoCardMap[item] = card;
}
```

### 選中狀態管理

```cpp
// Dashboard::_onGuhuoCardClicked()
if (_m_guhuoSelected) {
    _m_guhuoSelected->setSelected(false);  // 清除舊選中
}
item->setSelected(true);  // 設置新選中
_m_guhuoSelected = item;
Self->setTag(_m_guhuoSkillName, QVariant::fromValue(card));
```

### 確定按鈕連接

```cpp
// RoomScene::onGuhuoSkillActivated()
ok_button->setEnabled(true);
connect(ok_button, SIGNAL(clicked()), dashboard, SLOT(_onGuhuoConfirm()), Qt::UniqueConnection);
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
