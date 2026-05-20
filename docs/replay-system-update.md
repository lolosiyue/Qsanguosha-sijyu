# Replay 系統更新說明

## 概述

本次更新為 Replay 系統新增了以下功能：
- 轉換視角
- 接手操作
- 跳到節點
- 每回合快照

---

## 功能說明

### 1. 轉換視角

在 Replay 模式下，點擊任意玩家的 Photo 可以切換到該玩家的視角。

**實作方式**：
- 利用現有的 `S_COMMAND_SWITCH_CONTEXT` 協議
- 修改 `Client::setSelf()` 切換當前視角
- 同步目標玩家的手牌可見性

**相關檔案**：
- `src/ui/roomscene.cpp` - `switchReplayPerspective()`
- `src/client/client.cpp` - `setSelf()`

---

### 2. 接手操作

在 Replay 暫停後，可以選擇一個玩家進行接手操作：
- **目標玩家**：人手操作（顯示 UI 讓玩家回應）
- **其他玩家**：TrustAI 自動回應

**流程**：
```
Replay 暫停 → 選擇玩家 → 啟用接手模式
    ↓
收到 Request:
    ├── 目標玩家 → 顯示 UI，等待玩家回應
    └── 其他玩家 → TrustAI 自動回應
    ↓
記錄所有操作到新錄影
    ↓
結束時保存分支錄影
```

**相關檔案**：
- `src/util/replay-takeover.h/cpp` - `ReplayTakeoverManager`
- `src/client/client.cpp` - `enableTakeover()`, `disableTakeover()`

---

### 3. 跳到節點

時間軸 UI 顯示可點擊的節點小點：
- **綠色**：回合開始
- **紅色**：玩家死亡

**操作方式**：
- 拖動滑桿調整播放位置
- 點擊節點小點跳到該節點

**相關檔案**：
- `src/ui/replay-timeline.h/cpp` - `ReplayTimeline`
- `src/util/replay-index.h/cpp` - `ReplayIndex`
- `src/util/recorder.cpp` - `jumpToNode()`, `jumpToElapsed()`

---

### 4. 每回合快照

**觸發時機**：
- `TurnStart` 事件：每回合開始時
- `BuryVictim` 事件：玩家死亡時

**存儲格式**：JSON 檔案，位於錄影檔案的子目錄

**目錄結構**：
```
replays/
├── game_20260520_123456.replay.txt
└── game_20260520_123456.snapshots/
    ├── turn_001.json
    ├── turn_002.json
    ├── turn_003_death_caocao.json
    └── ...
```

**快照內容**：
- 玩家狀態（HP、手牌、裝備、判定區、標記等）
- 全局狀態（牌堆、棄牌堆、回合數等）

**相關檔案**：
- `src/util/game-snapshot.h/cpp` - `GameSnapshot`
- `src/server/room.cpp` - `saveSnapshot()`
- `src/server/gamerule.cpp` - 觸發點

---

## 新增檔案清單

| 檔案 | 說明 |
|------|------|
| `src/util/game-snapshot.h` | 遊戲狀態快照結構定義 |
| `src/util/game-snapshot.cpp` | 快照序列化/反序列化實作 |
| `src/util/replay-index.h` | 節點索引結構定義 |
| `src/util/replay-index.cpp` | 索引建立與查詢實作 |
| `src/util/replay-game-state.h` | 遊戲狀態重建結構定義 |
| `src/util/replay-game-state.cpp` | 從錄影重建狀態實作 |
| `src/util/replay-takeover.h` | 接手操作管理器定義 |
| `src/util/replay-takeover.cpp` | 接手操作實作 |
| `src/ui/replay-timeline.h` | 時間軸 UI 元件定義 |
| `src/ui/replay-timeline.cpp` | 時間軸 UI 實作 |

---

## API 參考

### Client 端

```cpp
// 檢查是否在接手模式
bool Client::isTakeoverMode() const;

// 取得接手目標
QString Client::getTakeoverTarget() const;

// 啟用接手模式
void Client::enableTakeover(const QString &playerName);

// 停用接手模式
void Client::disableTakeover();

// 保存接手後的錄影
void Client::saveTakeoverReplay(const QString &filepath);
```

### Replayer

```cpp
// 取得索引
ReplayIndex* Replayer::getIndex() const;

// 取得快照
GameSnapshot* Replayer::getSnapshot(int nodeIndex) const;

// 跳到指定節點
void Replayer::jumpToNode(int nodeIndex);

// 跳到指定時間
void Replayer::jumpToElapsed(int elapsed);

// 跳到指定命令索引
void Replayer::seekToPosition(int pairIndex);
```

### Room

```cpp
// 保存快照
void Room::saveSnapshot(const QString &type, const QString &playerName);

// 取得快照
GameSnapshot* Room::getSnapshot(int turnCount) const;

// 設定錄影路徑
void Room::setReplayPath(const QString &path);
```

---

## 使用範例

### 切換視角

```cpp
// 在 RoomScene 中
RoomScene::switchReplayPerspective("player_name");
```

### 啟用接手操作

```cpp
// 啟用接手
ClientInstance->enableTakeover("player_name");

// 停用接手
ClientInstance->disableTakeover();

// 保存新錄影
ClientInstance->saveTakeoverReplay("path/to/new_replay.txt");
```

### 跳到節點

```cpp
// 跳到第 5 回合
ReplayIndex *index = replayer->getIndex();
int nodeIndex = index->findNodeByTurn(5);
replayer->jumpToNode(nodeIndex);
```

---

## 注意事項

1. **編譯驗證**：本次實作尚未進行編譯驗證，建議先編譯測試。

2. **UI 位置**：`ReplayTimeline` 的位置需要根據實際 UI 佈局調整。

3. **狀態重建**：`ReplayGameState` 的完整測試需要實際錄影檔案。

4. **分支錄影**：接手後的新錄影會保存為原檔名加 `_branch_時間戳` 後綴。
