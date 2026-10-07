# 牌桌 QML 掛載元件實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 讓擴展包以標記綁定（`Engine::addQmlMark`）或 Room 指令（`Room::addQmlElement` 等）在現有 `QGraphicsScene` 牌桌上掛載 QML 元件，橫向、直向皆正確定位。

**Architecture:** 伺服器保存目前元件狀態，經新通知 `S_COMMAND_QML_ELEMENT`（136）送達客戶端，重連時由 `marshal` 補送；標記綁定沿用既有標記同步。客戶端在 `FitView` viewport 上疊一層透明 `QQuickWidget`（`QmlTableLayer`），由 `RoomScene` 提供座位與桌面區域的場景座標，圖層換算為 viewport 座標後放置元件。

**Tech Stack:** C++17、Qt 6.11（Quick、QuickWidgets、QtQuick.Effects、Network）、SWIG Lua、Python 3（E2E 腳本）。

**Spec:** `docs/superpowers/specs/2026-10-07-qml-table-elements-design.md`

## Global Constraints

- 新通知編號：`S_COMMAND_QML_ELEMENT = 136`；payload 位置欄位依序為 `op, id, qml, anchor, data, scope`。
- `op` 只有 `add`、`update`、`remove`、`clear`；`scope` 只有 `all`、`player`。
- 單一元件 `data` 以 compact JSON 序列化後不超過 16384 bytes；每個 receiver 最多 64 個元件。
- QML 路徑：相對路徑、副檔名 `.qml`、不得為絕對路徑、不得含 `:`、清理後不得以 `..` 開頭。
- 標記錨點：`mark-area`、`avatar`、`top`、`bottom`；Room 錨點：`table-center`、`screen-top`、`screen-bottom`、`screen-top-left`、`screen-top-right`、`screen-bottom-left`、`screen-bottom-right`、`seat:<objectName>`。
- 不把 `ClientPlayer*` 或任何 `QObject` 指標交給 QML；`qs` 一律為 `QVariantMap` 快照。
- 圖層只在 `QSAN_ENABLE_QML` 為真時編譯；XP legacy 建置不含此圖層。
- 原始碼註釋用英文，文件用繁體書面語。
- 建置前先執行 `pgrep -af "ninja|cmake --build"`；有其他建置在跑就等它結束（同一 build tree 並行連結會 `ld` Bus error）。
- 建置指令：`cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8`，產物為 `debug/QSanguosha`、`debug/qsanguosha_server`。

---

## 檔案結構

| 檔案 | 動作 | 責任 |
|---|---|---|
| `src/core/qml-element-path.h/.cpp` | 新增 | 伺服器與客戶端共用的 QML 路徑規則 |
| `src/core/engine.h/.cpp` | 修改 | `addQmlMark` 登記表與查詢 |
| `src/core/protocol.h` | 修改 | 新通知編號 |
| `src/core/protocol/protocol-payload-registry.cpp` | 修改 | payload 編碼、通知登記、schema |
| `src/client/core/client-game-state-reducer.cpp` | 修改 | 新通知的處置分類 |
| `src/client/client-log-formatter.cpp` | 修改 | 新通知不產生文字 |
| `artifacts/protocol-v2-flow-matrix.json` | 修改 | 流程矩陣新增一筆 |
| `src/server/room.h/.cpp` | 修改 | 四個 Room API、狀態保存、上限 |
| `src/server/player-lifecycle-service.cpp` | 修改 | 重連補送 |
| `src/server/game-session-controller.cpp` | 修改 | 開局清空 |
| `swig/qml-element.i` | 新增 | Lua table → `QVariantMap` typemap |
| `swig/sanguosha.i` | 修改 | 綁定 `addQmlMark` 與四個 Room API |
| `src/client/client.h/.cpp` | 修改 | callback 與兩個 signal |
| `src/client/clientplayer.cpp` | 修改 | 已登記的 `@` 標記不進標記文件 |
| `src/ui/generic-cardcontainer-ui.cpp` | 修改 | 已登記的 `&` 標記不建按鈕 |
| `src/ui/qml-table-layer.h/.cpp` | 新增 | 透明 QML 圖層：建立、定位、輸入、快照 |
| `qml/table/TableLayer.qml`、`qml/table/table_qml.qrc` | 新增 | 圖層根 QML 與資源檔 |
| `src/ui/roomscene.h/.cpp` | 修改 | 提供座位與桌面幾何、接上圖層 |
| `src/ui/game-view.h/.cpp` | 修改 | 建立圖層、疊放順序、視覺模式、refit |
| `cmake/QSanguoshaSources.cmake`、`CMakeLists.txt` | 修改 | 新檔案與 qrc |
| `src/ui/testing/network-ui-smoke-*.h/.cpp` | 修改 | `qml_elements:<n>` 階段與視窗尺寸旗標 |
| `tools/autotest/gui_network_smoke.py` | 修改 | `--client-window` 轉送 |
| `tools/autotest/qml_table_layer_smoke.py` | 新增 | E2E：橫向與直向各打一局 |
| `docs/qml-table-elements.md` | 新增 | 擴展作者指南 |

---

### Task 1: E2E 腳本（先寫，預期失敗）

依 `AGENTS.md`，驗證以 E2E 為主，先把所有失敗條件寫成腳本。此任務完成時腳本必然失敗（客戶端尚未輸出 `qml_elements` 階段）。

**Files:**
- Create: `tools/autotest/qml_table_layer_smoke.py`

**Interfaces:**
- Consumes: `tools/autotest/gui_network_smoke.py` 既有參數；Task 9 會新增 `--client-window WxH`。
- Produces: 客戶端日誌中的 `NETWORK_UI_STAGE {"stage": "qml_elements:<n>", ...}`，欄位定義見 Task 9 的 `QmlTableLayer::snapshot()`。

- [ ] **Step 1: 建立腳本**

```python
#!/usr/bin/env python3
"""E2E: extensions mount QML elements on the table (Engine:addQmlMark, Room:addQmlElement).

Flow
----
    1. Build a probe asset tree like ui_theme_runtime_smoke.py: the repository is
       symlinked, lua/config.lua declares extensions/qmltableprobe.lua, and
       qmlprobe/ holds the probe QML files.
    2. The probe extension binds "@qmlprobe_*" marks to qmlprobe/Mark.qml and, on
       the first turn, adds a broadcast board, a board for the human seat only, a
       board that follows another seat, one element whose file is missing, and one
       request outside the game folder that the server must refuse. On the second
       turn it updates the board, removes the human's board and clears the mark.
    3. Run gui_network_smoke.py once landscape (1280x720) and once portrait
       (540x960, responsive layout) and gate on the client's "qml_elements:<n>" stages.

Usage:
    python3 tools/autotest/qml_table_layer_smoke.py --exe-root . \\
        --server-exe debug/qsanguosha_server --client-exe debug/QSanguosha \\
        --artifact-dir artifacts/qml-table-layer --seed 20261007 --no-xvfb --platform xcb
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STAGE_MARKER = "NETWORK_UI_STAGE"
PROBE_SCRIPT = "extensions/qmltableprobe.lua"

PROBE_LUA = r'''
-- E2E probe for table QML elements; generated by tools/autotest/qml_table_layer_smoke.py.
local extension = sgs.Package("qmltableprobe", sgs.Package_GeneralPack)

sgs.Sanguosha:addQmlMark("@qmlprobe_*", "qmlprobe/Mark.qml", "avatar")

local function humanSeat(room)
	for _, p in sgs.qlist(room:getPlayers()) do
		if p:getState() == "online" then return p end
	end
	return nil
end

local function otherSeat(room, human)
	for _, p in sgs.qlist(room:getPlayers()) do
		if p ~= human then return p end
	end
	return nil
end

local probe = sgs.CreateTriggerSkill {
	name = "#qmltableprobe",
	global = true,
	priority = 20,
	frequency = sgs.Skill_Compulsory,
	events = { sgs.TurnStart },
	can_trigger = function(self, target)
		return target ~= nil
	end,
	on_trigger = function(self, event, player, data)
		local room = player:getRoom()
		local step = room:getTag("QmlTableProbeStep"):toInt() + 1
		room:setTag("QmlTableProbeStep", sgs.QVariant(step))
		local human = humanSeat(room)
		if step == 1 then
			room:addQmlElement("board", "qmlprobe/Board.qml", "table-center", { round = 1, names = { "a", "b" } })
			if human then
				room:setPlayerMark(human, "@qmlprobe_star", 2)
				room:addQmlElement("mine", "qmlprobe/Board.qml", "screen-top", { round = 0 }, human)
				local other = otherSeat(room, human)
				if other then
					room:addQmlElement("follow", "qmlprobe/Board.qml", "seat:" .. other:objectName(), { round = 9 })
				end
			end
			room:addQmlElement("nofile", "qmlprobe/Missing.qml", "screen-bottom", {})
			-- Outside the game folder: the server must refuse it and the client never sees it.
			room:addQmlElement("outside", "../qmlprobe-outside.qml", "table-center", {})
		elseif step == 2 then
			room:updateQmlElement("board", { round = 2 })
			if human then
				room:removeQmlElement("mine", human)
				room:setPlayerMark(human, "@qmlprobe_star", 0)
			end
		end
		return false
	end,
}

local skills = sgs.SkillList()
if not sgs.Sanguosha:getSkill("#qmltableprobe") then skills:append(probe) end
sgs.Sanguosha:addSkills(skills)

return { extension }
'''

MARK_QML = '''import QtQuick
Rectangle {
    property var qs
    implicitWidth: 24
    implicitHeight: 24
    radius: 12
    color: "gold"
    Text { anchors.centerIn: parent; text: qs && qs.data ? qs.data.value : "" }
}
'''

BOARD_QML = '''import QtQuick
Rectangle {
    property var qs
    property bool qsInteractive: true
    property int clicks: 0
    implicitWidth: 160
    implicitHeight: 40
    color: "#c0000000"
    Text {
        anchors.centerIn: parent
        color: "white"
        text: "round " + (qs && qs.data ? qs.data.round : "?")
    }
    TapHandler { onTapped: parent.clicks += 1 }
}
'''

RUNS = [("landscape", "1280x720"), ("portrait", "540x960")]


def symlink_farm(source, target, skip=()):
    os.makedirs(target, exist_ok=True)
    for name in os.listdir(source):
        if name in skip:
            continue
        os.symlink(os.path.join(source, name), os.path.join(target, name))


def build_probe_tree(repo, tree):
    if os.path.lexists(tree):
        shutil.rmtree(tree)
    symlink_farm(repo, tree, skip={"lua", "extensions", "packages", ".git", "build", "builds", "artifacts"})
    # The package catalog refuses a symlinked packages/ directory.
    if os.path.isdir(os.path.join(repo, "packages")):
        shutil.copytree(os.path.join(repo, "packages"), os.path.join(tree, "packages"), symlinks=True)
    symlink_farm(os.path.join(repo, "lua"), os.path.join(tree, "lua"), skip={"config.lua"})
    symlink_farm(os.path.join(repo, "extensions"), os.path.join(tree, "extensions"))

    with open(os.path.join(repo, "lua", "config.lua"), encoding="utf-8") as f:
        config = f.read()
    start = config.index("extension_names = {")
    end = config.index("\n\t},", start)
    body = config[start:end].rstrip()
    if not body.endswith(","):
        body += ","
    config = config[:start] + body + '\n\t\t"%s",' % PROBE_SCRIPT + config[end:]
    with open(os.path.join(tree, "lua", "config.lua"), "w", encoding="utf-8") as f:
        f.write(config)
    with open(os.path.join(tree, PROBE_SCRIPT), "w", encoding="utf-8") as f:
        f.write(PROBE_LUA)
    probe_dir = os.path.join(tree, "qmlprobe")
    os.makedirs(probe_dir)
    for name, text in (("Mark.qml", MARK_QML), ("Board.qml", BOARD_QML)):
        with open(os.path.join(probe_dir, name), "w", encoding="utf-8") as f:
            f.write(text)


def read_stages(client_log):
    stages = []
    with open(client_log, encoding="utf-8", errors="replace") as f:
        for line in f:
            at = line.find(STAGE_MARKER)
            if at < 0:
                continue
            try:
                stages.append(json.loads(line[at + len(STAGE_MARKER):].strip()))
            except ValueError:
                pass
    return [s for s in stages if str(s.get("stage", "")).startswith("qml_elements:")]


def center(rect):
    return (rect["x"] + rect["w"] / 2.0, rect["y"] + rect["h"] / 2.0)


def inside(point, rect, slack=2.0):
    return (rect["x"] - slack <= point[0] <= rect["x"] + rect["w"] + slack
            and rect["y"] - slack <= point[1] <= rect["y"] + rect["h"] + slack)


def intersects(a, b):
    return not (a["x"] + a["w"] <= b["x"] or b["x"] + b["w"] <= a["x"]
                or a["y"] + a["h"] <= b["y"] or b["y"] + b["h"] <= a["y"])


def evaluate(stages, orientation):
    problems = []
    if not stages:
        return ["the client never reported a qml_elements stage"]
    seen = {}
    for stage in stages:
        for element in stage.get("elements", []):
            seen.setdefault(element["key"], []).append((stage, element))

    def last(key):
        return seen[key][-1] if key in seen else (None, None)

    for key in seen:
        if "outside" in key:
            problems.append("an element outside the game folder reached the client: %s" % key)

    # Broadcast board: shown at the table center, then updated in place.
    stage, board = last("all/board")
    if board is None:
        problems.append("all/board never appeared")
    else:
        rounds = [e.get("data", {}).get("round") for _, e in seen["all/board"]]
        if 2 not in rounds:
            problems.append("all/board never showed round 2 after updateQmlElement: %r" % rounds)
        if len({e.get("instance") for _, e in seen["all/board"] if e.get("instance")}) != 1:
            problems.append("all/board was recreated instead of updated in place")
        if board.get("error"):
            problems.append("all/board failed to load: %s" % board["error"])
        if not board.get("visible"):
            problems.append("all/board is not visible")
        elif not inside(center(board["rect"]), {"x": stage["table_center"]["x"] - 2,
                                                "y": stage["table_center"]["y"] - 2, "w": 4, "h": 4}):
            problems.append("all/board center %r is not at the table center %r"
                            % (center(board["rect"]), stage["table_center"]))

    # Targeted board: appears for the human, then removed.
    if "player/mine" not in seen:
        problems.append("player/mine never appeared")
    elif any(e["key"] == "player/mine" for e in stages[-1].get("elements", [])):
        problems.append("player/mine is still shown after removeQmlElement")
    else:
        _, mine = seen["player/mine"][-1]
        header = stages[-1].get("header_rect")
        if header and mine.get("visible") and intersects(mine["rect"], header):
            problems.append("player/mine overlaps the header")

    # Seat-following board: centered inside its seat.
    _, follow = last("all/follow")
    if follow is None:
        problems.append("all/follow never appeared")
    elif follow.get("visible") and not inside(center(follow["rect"]), follow["seat_rect"]):
        problems.append("all/follow center %r is outside its seat %r" % (center(follow["rect"]), follow["seat_rect"]))

    # Missing file: reported, never created, game continues.
    _, missing = last("all/nofile")
    if missing is None:
        problems.append("all/nofile never reached the layer")
    elif not missing.get("error") or missing.get("instance"):
        problems.append("all/nofile should carry an error and no item: %r" % missing)

    # Mark binding on the human's own seat (dashboard avatar area).
    mark_keys = [k for k in seen if k.startswith("mark/") and k.endswith("/@qmlprobe_star")]
    if not mark_keys:
        problems.append("the @qmlprobe_star mark never became a QML element")
    else:
        _, mark = seen[mark_keys[0]][0]
        if mark.get("data", {}).get("value") != 2:
            problems.append("mark element data is %r, expected value 2" % mark.get("data"))
        if mark.get("visible") and not inside(center(mark["rect"]), mark["seat_rect"]):
            problems.append("mark element center %r is outside the avatar area %r"
                            % (center(mark["rect"]), mark["seat_rect"]))
        if any(e["key"] == mark_keys[0] for e in stages[-1].get("elements", [])):
            problems.append("the mark element is still shown after the mark dropped to 0")

    # Table and screen elements never cover the hand / dashboard area.
    for stage in stages:
        interaction = stage.get("interaction_rect")
        for element in stage.get("elements", []):
            if element.get("player") or not element.get("visible") or not interaction:
                continue
            if element["anchor"] in ("table-center", "screen-top") and intersects(element["rect"], interaction):
                problems.append("%s overlaps the dashboard area in %s" % (element["key"], stage["stage"]))

    if orientation == "portrait":
        profiles = {s.get("profile") for s in stages}
        if "portrait" not in profiles:
            problems.append("portrait run never reported profile=portrait: %r" % profiles)
    return problems


def run_once(args, repo, tree, artifacts, orientation, window):
    label = "qml-table-%s-%s-%s" % (orientation, args.mode, args.seed)
    command = [sys.executable, os.path.join(HERE, "gui_network_smoke.py"),
               "--exe-root", repo, "--workdir", tree,
               "--mode", args.mode, "--seed", args.seed,
               "--artifact-dir", artifacts, "--label", label,
               "--platform", args.platform, "--process-timeout", args.process_timeout,
               "--client-window", window,
               "--known-base-defect", "server-teardown-crash"]
    if args.no_xvfb:
        command.append("--no-xvfb")
    for flag, value in (("--server-exe", args.server_exe), ("--client-exe", args.client_exe)):
        if value:
            command += [flag, os.path.abspath(value)]
    env = dict(os.environ, QSAN_ASSET_ROOT=tree)
    smoke = subprocess.run(command, env=env)
    client_log = os.path.join(artifacts, "network-ui-smoke-%s-client.log" % label)
    problems = []
    stages = []
    if smoke.returncode != 0:
        problems.append("gui_network_smoke.py exited %d" % smoke.returncode)
    if not os.path.isfile(client_log):
        problems.append("no client log at %s" % client_log)
    else:
        stages = read_stages(client_log)
        problems += evaluate(stages, orientation)
    return label, stages, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe-root", default=".")
    parser.add_argument("--server-exe")
    parser.add_argument("--client-exe")
    parser.add_argument("--artifact-dir", default="artifacts/qml-table-layer")
    parser.add_argument("--mode", default="02p")
    parser.add_argument("--seed", default="20261007")
    parser.add_argument("--platform", default="xcb")
    parser.add_argument("--no-xvfb", action="store_true")
    parser.add_argument("--process-timeout", default="900")
    parser.add_argument("--orientation", choices=("landscape", "portrait", "both"), default="both")
    args = parser.parse_args()

    repo = os.path.abspath(args.exe_root)
    artifacts = os.path.abspath(args.artifact_dir)
    os.makedirs(artifacts, exist_ok=True)
    tree = os.path.join(artifacts, "probe-tree")
    build_probe_tree(repo, tree)

    summary = {"mode": args.mode, "seed": args.seed, "runs": []}
    failed = False
    for orientation, window in RUNS:
        if args.orientation not in ("both", orientation):
            continue
        label, stages, problems = run_once(args, repo, tree, artifacts, orientation, window)
        failed = failed or bool(problems)
        summary["runs"].append({"orientation": orientation, "window": window, "label": label,
                                "stages": len(stages), "ok": not problems, "problems": problems})
        print("\n[%s] qml_elements stages: %d" % (orientation, len(stages)))
        for problem in problems:
            print("  - %s" % problem, file=sys.stderr)
    summary["ok"] = not failed
    with open(os.path.join(artifacts, "qml-table-layer-summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print("QML table layer smoke %s" % ("FAILED" if failed else "PASSED"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: 確認腳本語法正確**

Run: `python3 -m py_compile tools/autotest/qml_table_layer_smoke.py && echo ok`
Expected: `ok`

- [ ] **Step 3: Commit**

```bash
git add tools/autotest/qml_table_layer_smoke.py
git commit -m "test(qml-table): add the E2E gate for table QML elements before the feature"
```

---

### Task 2: 路徑規則與標記登記表（engine）

**Files:**
- Create: `src/core/qml-element-path.h`、`src/core/qml-element-path.cpp`
- Modify: `CMakeLists.txt`（engine 原始碼清單，`src/core/runtime-paths.cpp` 旁，約第 342 行）
- Modify: `src/core/engine.h`（Resource Alias 區塊後，約第 305 行；成員約第 344 行）
- Modify: `src/core/engine.cpp`（`Engine::getResourceAliasList` 之後）
- Modify: `swig/sanguosha.i`（`class Engine` 的 `addResourceAlias` 宣告後，約第 2014 行）

**Interfaces:**
- Produces:
  - `bool QmlElementPath::isAllowed(const QString &path, QString *error = nullptr);`
  - `struct QmlMarkBinding { QString pattern; QString qmlPath; QString anchor; bool isValid() const; };`
  - `void Engine::addQmlMark(const QString &pattern, const QString &qmlPath, const QString &anchor = QStringLiteral("mark-area"));`
  - `QmlMarkBinding Engine::qmlMarkFor(const QString &mark) const;`
  - `bool Engine::isQmlMark(const QString &mark) const;`
  - Lua：`sgs.Sanguosha:addQmlMark(pattern, qmlPath[, anchor])`

- [ ] **Step 1: 新增 `src/core/qml-element-path.h`**

```cpp
#ifndef QML_ELEMENT_PATH_H
#define QML_ELEMENT_PATH_H

#include <QString>

namespace QmlElementPath
{
// Lexical rule shared by Room and the client layer: a relative ".qml" path that
// stays inside the game folder. The client also checks that the file exists.
bool isAllowed(const QString &path, QString *error = nullptr);
}

#endif
```

- [ ] **Step 2: 新增 `src/core/qml-element-path.cpp`**

```cpp
#include "qml-element-path.h"

#include <QDir>

bool QmlElementPath::isAllowed(const QString &path, QString *error)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    const auto fail = [&](const QString &why) {
        if (error)
            *error = QStringLiteral("\"%1\" %2").arg(path, why);
        return false;
    };
    if (clean.isEmpty() || clean == QLatin1String("."))
        return fail(QStringLiteral("is empty"));
    // ':' also rules out "qrc:", "http:" and Windows drive letters.
    if (QDir::isAbsolutePath(clean) || clean.contains(QLatin1Char(':'))
        || clean == QLatin1String("..") || clean.startsWith(QLatin1String("../")))
        return fail(QStringLiteral("must be a path inside the game folder"));
    if (!clean.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive))
        return fail(QStringLiteral("is not a .qml file"));
    return true;
}
```

- [ ] **Step 3: 加入 engine 原始碼清單**

在 `CMakeLists.txt` 的 `src/core/runtime-paths.h` 下一行加入：

```cmake
    src/core/qml-element-path.cpp
    src/core/qml-element-path.h
```

- [ ] **Step 4: `engine.h` 宣告**

在 `QStringList getResourceAliasList(...) const;` 之後加入：

```cpp

    // QML mark bindings: a mark named `pattern` (or starting with it, when it ends in '*')
    // is drawn by `qmlPath` on its seat instead of the stock mark text or button.
    void addQmlMark(const QString &pattern, const QString &qmlPath,
                    const QString &anchor = QStringLiteral("mark-area"));
    QmlMarkBinding qmlMarkFor(const QString &mark) const;
    bool isQmlMark(const QString &mark) const;
```

在 `class Engine` 之前（同檔案頂部其他前置宣告附近）加入：

```cpp
struct QmlMarkBinding
{
    QString pattern;
    QString qmlPath;
    QString anchor;
    bool isValid() const { return !qmlPath.isEmpty(); }
};
```

在 `m_resourceAliasLists` 成員之後加入：

```cpp
    QList<QmlMarkBinding> m_qmlMarks;
```

- [ ] **Step 5: `engine.cpp` 實作**

檔頭加入 `#include "qml-element-path.h"`，並在 `Engine::getResourceAliasList` 之後加入：

```cpp
void Engine::addQmlMark(const QString &pattern, const QString &qmlPath, const QString &anchor)
{
    static const QStringList anchors{QStringLiteral("mark-area"), QStringLiteral("avatar"),
        QStringLiteral("top"), QStringLiteral("bottom")};
    QString error;
    if (pattern.isEmpty() || pattern == QLatin1String("*")) {
        qWarning().noquote() << "Engine::addQmlMark: a mark name or prefix is required";
        return;
    }
    if (!anchors.contains(anchor)) {
        qWarning().noquote() << "Engine::addQmlMark:" << pattern << "unknown anchor" << anchor;
        return;
    }
    if (!QmlElementPath::isAllowed(qmlPath, &error)) {
        qWarning().noquote() << "Engine::addQmlMark:" << pattern << error;
        return;
    }
    for (QmlMarkBinding &binding : m_qmlMarks) {
        if (binding.pattern == pattern) {
            binding.qmlPath = qmlPath;
            binding.anchor = anchor;
            return;
        }
    }
    m_qmlMarks.append({pattern, qmlPath, anchor});
}

QmlMarkBinding Engine::qmlMarkFor(const QString &mark) const
{
    // Exact names win; otherwise the longest matching prefix.
    const QmlMarkBinding *best = nullptr;
    for (const QmlMarkBinding &binding : m_qmlMarks) {
        if (binding.pattern == mark)
            return binding;
        if (binding.pattern.endsWith(QLatin1Char('*'))) {
            const QString prefix = binding.pattern.chopped(1);
            if (mark.startsWith(prefix) && (!best || prefix.size() > best->pattern.size() - 1))
                best = &binding;
        }
    }
    return best ? *best : QmlMarkBinding();
}

bool Engine::isQmlMark(const QString &mark) const
{
    return !m_qmlMarks.isEmpty() && qmlMarkFor(mark).isValid();
}
```

- [ ] **Step 6: SWIG 宣告**

在 `swig/sanguosha.i` 的 `class Engine` 裡、`QString getResourceAlias(...) const;` 之後加入：

```
	// QML mark binding: the mark is drawn by qmlPath on its seat (see docs/qml-table-elements.md).
	void addQmlMark(const char*pattern, const char*qmlPath, const char*anchor = "mark-area");
```

- [ ] **Step 7: 建置**

Run: `pgrep -af "ninja|cmake --build"; cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8 2>&1 | tail -5`
Expected: 以 `Linking CXX executable` 結尾，無 `error:`。

- [ ] **Step 8: Commit**

```bash
git add src/core/qml-element-path.h src/core/qml-element-path.cpp src/core/engine.h src/core/engine.cpp swig/sanguosha.i CMakeLists.txt
git commit -m "feat(engine): let extensions bind marks to QML files"
```

---

### Task 3: 協定 `S_COMMAND_QML_ELEMENT`

**Files:**
- Modify: `src/core/protocol.h:183`
- Modify: `src/core/protocol/protocol-payload-registry.cpp`（`encodeRoomNotificationPayload` 約第 850 行；`ROOM_NOTIFICATION` 清單約第 1199 行；schema 表約第 1396 行）
- Modify: `src/client/core/client-game-state-reducer.cpp:358-361`
- Modify: `src/client/client-log-formatter.cpp:470`
- Modify: `artifacts/protocol-v2-flow-matrix.json`

**Interfaces:**
- Produces: `QSanProtocol::S_COMMAND_QML_ELEMENT`；客戶端 callback 名稱 `Client::handleQmlElement`（Task 5 實作）；typed payload `QmlElementPayload` 欄位 `op, id, qml, anchor, data, scope`。

- [ ] **Step 1: 指令編號**

`src/core/protocol.h`：

```cpp
        S_COMMAND_SET_UI_THEME = 135,
        S_COMMAND_QML_ELEMENT = 136
```

- [ ] **Step 2: payload 編碼**

在 `case S_COMMAND_SET_UI_THEME:` 那兩行之後加入：

```cpp
    case S_COMMAND_QML_ELEMENT:
        return positionalPayload(value, {"op", "id", "qml", "anchor", "data", "scope"}, output, error);
```

- [ ] **Step 3: 登記通知與 schema**

在 `ROOM_NOTIFICATION(S_COMMAND_SET_UI_THEME, ...)` 之後加入：

```cpp
    ROOM_NOTIFICATION(S_COMMAND_QML_ELEMENT, "Client::handleQmlElement", "QmlElementPayload");
```

在 `{QStringLiteral("UiThemePayload"), ...},` 之後加入：

```cpp
        {QStringLiteral("QmlElementPayload"), {QStringLiteral("op"), QStringLiteral("id"), QStringLiteral("qml"), QStringLiteral("anchor"), QStringLiteral("data"), QStringLiteral("scope")}},
```

- [ ] **Step 4: reducer 與文字格式化**

`client-game-state-reducer.cpp`，在 `case S_COMMAND_ANIMATE:` 之前加一行，讓它與動畫同屬「與文字無關」：

```cpp
    case S_COMMAND_QML_ELEMENT:
    case S_COMMAND_ANIMATE:
    case S_COMMAND_PLAY_AUDIO:
        // Both drive desktop presentation only; a text transcript has nothing
        // to say about an animation or a sound.
        return ClientFlowDisposition::ExplicitTextIrrelevant;
```

並把註釋改成 `// These drive desktop presentation only (animations, sounds, mounted QML); a text transcript has nothing to say about them.`

`client-log-formatter.cpp`，在 `case QSanProtocol::S_COMMAND_SET_UI_THEME:` 之後加入：

```cpp
    case QSanProtocol::S_COMMAND_QML_ELEMENT:
```

- [ ] **Step 5: 流程矩陣**

在 `artifacts/protocol-v2-flow-matrix.json` 的 `S_COMMAND_SET_UI_THEME` 物件之後插入：

```json
        {
            "command": "S_COMMAND_QML_ELEMENT",
            "command_id": 136,
            "consumer": "Client::handleQmlElement",
            "correlation": "none",
            "current_payload_shape": "typed_object",
            "destination": "client",
            "encoder": "ProtocolPayloadRegistry::encodeObjectPayload",
            "message_type": "notification",
            "migration_status": "complete",
            "optional_fields": [
            ],
            "parser": "ProtocolPayloadRegistry::validateObjectPayload",
            "producer": "Room/RoomNotifier",
            "production_call_site_evidence": [
                "Room/RoomNotifier",
                "Client::handleQmlElement"
            ],
            "replay_eligibility": "record",
            "reply_command_id": 0,
            "required_fields": [
                "schema_version",
                "op",
                "id",
                "qml",
                "anchor",
                "data",
                "scope"
            ],
            "source": "room",
            "target_typed_schema": "QmlElementPayload"
        },
```

並把 `summary` 的 `production_flow_count`、`typed_complete`、`typed_registry_flow_count` 由 149 改為 150。

- [ ] **Step 6: 驗證 JSON 與建置**

Run: `python3 -c "import json;json.load(open('artifacts/protocol-v2-flow-matrix.json'))" && echo json-ok`
Expected: `json-ok`

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8 2>&1 | tail -3`
Expected: 成功連結，無 `error:`。`ROOM_NOTIFICATION` 只記錄 consumer 的字串名稱，`Client::handleQmlElement` 要到 Task 5 才實作，此時不影響連結。

- [ ] **Step 7: Commit**

```bash
git add src/core/protocol.h src/core/protocol/protocol-payload-registry.cpp src/client/core/client-game-state-reducer.cpp src/client/client-log-formatter.cpp artifacts/protocol-v2-flow-matrix.json
git commit -m "feat(protocol): add the QML element notification (136)"
```

---

### Task 4: 伺服器 Room API、重連補送、Lua 綁定

**Files:**
- Modify: `src/server/room.h`（`resetUi` 宣告後約第 682 行；私有成員 `m_uiThemeHistory` 後約第 1087 行）
- Modify: `src/server/room.cpp`（`Room::resetUi` 之後）
- Modify: `src/server/player-lifecycle-service.cpp:1001-1004`
- Modify: `src/server/game-session-controller.cpp`（`m_room.m_uiThemeHistory.clear();` 那一行）
- Create: `swig/qml-element.i`
- Modify: `swig/sanguosha.i`（`%include "resolution-history.i"` 之後約第 2332 行；Room 宣告 `void resetUi(...)` 之後約第 2572 行）

**Interfaces:**
- Consumes: `QmlElementPath::isAllowed`（Task 2）、`S_COMMAND_QML_ELEMENT`（Task 3）。
- Produces:
  - `void Room::addQmlElement(const QString &id, const QString &qmlPath, const QString &anchor, const QVariantMap &qmlData = QVariantMap(), ServerPlayer *player = nullptr);`
  - `void Room::updateQmlElement(const QString &id, const QVariantMap &qmlData, ServerPlayer *player = nullptr);`
  - `void Room::removeQmlElement(const QString &id, ServerPlayer *player = nullptr);`
  - `void Room::clearQmlElements(ServerPlayer *player = nullptr);`
  - 通知 payload：`[op, id, qml, anchor, data, scope]`，`scope` 為 `"all"` 或 `"player"`。

- [ ] **Step 1: `room.h` 宣告**

在 `void resetUi(ServerPlayer *player = nullptr);` 之後加入：

```cpp
    // Extension QML elements on the table (docs/qml-table-elements.md). Without `player`
    // they address the elements everyone sees; with it, only that player's own elements.
    void addQmlElement(const QString &id, const QString &qmlPath, const QString &anchor,
                       const QVariantMap &qmlData = QVariantMap(), ServerPlayer *player = nullptr);
    void updateQmlElement(const QString &id, const QVariantMap &qmlData, ServerPlayer *player = nullptr);
    void removeQmlElement(const QString &id, ServerPlayer *player = nullptr);
    void clearQmlElements(ServerPlayer *player = nullptr);
```

在 `QList<QPair<QString, QVariant>> m_uiThemeHistory;` 之後加入：

```cpp
    struct QmlElementState
    {
        QString qml;
        QString anchor;
        QVariantMap data;
    };
    // Live QML elements: receiver name ("" for everyone) -> id -> current state.
    // Reconnecting players get the current state, not every update that led to it.
    QHash<QString, QMap<QString, QmlElementState>> m_qmlElements;
    void sendQmlElement(ServerPlayer *player, const QVariantList &arg);
```

- [ ] **Step 2: `room.cpp` 實作**

檔頭加入 `#include "qml-element-path.h"`、`#include <QJsonDocument>`、`#include <QJsonObject>`（已有者略過），並在 `Room::resetUi` 之後加入：

```cpp
namespace {
constexpr int kMaxQmlDataBytes = 16 * 1024;
constexpr int kMaxQmlElementsPerReceiver = 64;

bool isQmlElementAnchor(const QString &anchor)
{
    static const QStringList anchors{QStringLiteral("table-center"), QStringLiteral("screen-top"),
        QStringLiteral("screen-bottom"), QStringLiteral("screen-top-left"), QStringLiteral("screen-top-right"),
        QStringLiteral("screen-bottom-left"), QStringLiteral("screen-bottom-right")};
    return anchors.contains(anchor)
        || (anchor.startsWith(QLatin1String("seat:")) && anchor.size() > 5);
}

int qmlDataBytes(const QVariantMap &data)
{
    return QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact).size();
}

QVariantList qmlElementArg(const QString &op, const QString &id, const QString &qml, const QString &anchor,
                           const QVariantMap &data, bool targeted)
{
    return QVariantList{op, id, qml, anchor, QVariant(data),
        targeted ? QStringLiteral("player") : QStringLiteral("all")};
}
}

void Room::sendQmlElement(ServerPlayer *player, const QVariantList &arg)
{
    if (player)
        doNotify(player, S_COMMAND_QML_ELEMENT, arg);
    else
        doBroadcastNotify(S_COMMAND_QML_ELEMENT, arg);
}

void Room::addQmlElement(const QString &id, const QString &qmlPath, const QString &anchor,
                         const QVariantMap &qmlData, ServerPlayer *player)
{
    QString error;
    if (id.isEmpty()) {
        qWarning().noquote() << "Room::addQmlElement: an id is required";
        return;
    }
    if (!QmlElementPath::isAllowed(qmlPath, &error)) {
        qWarning().noquote() << "Room::addQmlElement:" << id << error;
        return;
    }
    if (!isQmlElementAnchor(anchor)) {
        qWarning().noquote() << "Room::addQmlElement:" << id << "unknown anchor" << anchor;
        return;
    }
    if (qmlDataBytes(qmlData) > kMaxQmlDataBytes) {
        qWarning().noquote() << "Room::addQmlElement:" << id << "data exceeds" << kMaxQmlDataBytes << "bytes";
        return;
    }
    auto &elements = m_qmlElements[player ? player->objectName() : QString()];
    if (!elements.contains(id) && elements.size() >= kMaxQmlElementsPerReceiver) {
        qWarning().noquote() << "Room::addQmlElement:" << id << "exceeds" << kMaxQmlElementsPerReceiver << "elements";
        return;
    }
    elements.insert(id, {qmlPath, anchor, qmlData});
    sendQmlElement(player, qmlElementArg(QStringLiteral("add"), id, qmlPath, anchor, qmlData, player));
}

void Room::updateQmlElement(const QString &id, const QVariantMap &qmlData, ServerPlayer *player)
{
    const QString receiver = player ? player->objectName() : QString();
    auto scope = m_qmlElements.find(receiver);
    if (scope == m_qmlElements.end() || !scope->contains(id)) {
        qWarning().noquote() << "Room::updateQmlElement: no element" << id;
        return;
    }
    QVariantMap merged = scope->value(id).data;
    for (auto it = qmlData.cbegin(); it != qmlData.cend(); ++it)
        merged.insert(it.key(), it.value());
    if (qmlDataBytes(merged) > kMaxQmlDataBytes) {
        qWarning().noquote() << "Room::updateQmlElement:" << id << "data exceeds" << kMaxQmlDataBytes << "bytes";
        return;
    }
    (*scope)[id].data = merged;
    sendQmlElement(player, qmlElementArg(QStringLiteral("update"), id, QString(), QString(), qmlData, player));
}

void Room::removeQmlElement(const QString &id, ServerPlayer *player)
{
    const QString receiver = player ? player->objectName() : QString();
    auto scope = m_qmlElements.find(receiver);
    if (scope == m_qmlElements.end() || scope->remove(id) == 0)
        return;
    sendQmlElement(player, qmlElementArg(QStringLiteral("remove"), id, QString(), QString(), QVariantMap(), player));
}

void Room::clearQmlElements(ServerPlayer *player)
{
    if (m_qmlElements.remove(player ? player->objectName() : QString()) == 0)
        return;
    sendQmlElement(player, qmlElementArg(QStringLiteral("clear"), QString(), QString(), QString(), QVariantMap(), player));
}
```

若 `qWarning` 尚未可用，加入 `#include <QDebug>`。

- [ ] **Step 3: 重連補送**

`player-lifecycle-service.cpp`，在 `m_uiThemeHistory` 迴圈之後加入：

```cpp
    for (auto scope = m_room.m_qmlElements.cbegin(); scope != m_room.m_qmlElements.cend(); ++scope) {
        const bool targeted = !scope.key().isEmpty();
        if (targeted && scope.key() != player->objectName())
            continue;
        for (auto it = scope.value().cbegin(); it != scope.value().cend(); ++it) {
            const QVariantList arg{QStringLiteral("add"), it.key(), it->qml, it->anchor, QVariant(it->data),
                targeted ? QStringLiteral("player") : QStringLiteral("all")};
            m_notifier.doNotify(player, S_COMMAND_QML_ELEMENT, arg);
        }
    }
```

- [ ] **Step 4: 開局清空**

`game-session-controller.cpp`：

```cpp
	m_room.m_uiThemeHistory.clear();
	m_room.m_qmlElements.clear();/*
```

（原本的 `/*` 註釋起點移到新行尾。）

- [ ] **Step 5: 新增 `swig/qml-element.i`**

```
%{

#include <QVariant>
#include <cmath>

namespace {

constexpr int kQmlDataMaxDepth = 8;

bool readQmlDataTable(lua_State *L, int index, QVariant &out, int depth);

bool readQmlDataValue(lua_State *L, int index, QVariant &out, int depth)
{
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        out = QVariant();
        return true;
    case LUA_TBOOLEAN:
        out = QVariant(lua_toboolean(L, index) != 0);
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            out = QVariant::fromValue<qint64>(static_cast<qint64>(lua_tointeger(L, index)));
            return true;
        }
        if (!std::isfinite(double(lua_tonumber(L, index))))
            return false;
        out = QVariant(double(lua_tonumber(L, index)));
        return true;
    case LUA_TSTRING: {
        size_t length = 0;
        const char *raw = lua_tolstring(L, index, &length);
        out = QVariant(QString::fromUtf8(raw, qsizetype(length)));
        return true;
    }
    case LUA_TTABLE:
        return readQmlDataTable(L, index, out, depth + 1);
    default:
        // Functions, userdata and threads have no JSON form.
        return false;
    }
}

// A table with only string keys becomes a map; a sequence 1..n becomes a list; mixed tables are refused.
bool readQmlDataTable(lua_State *L, int index, QVariant &out, int depth)
{
    if (depth > kQmlDataMaxDepth)
        return false;
    const int table = lua_absindex(L, index);
    const lua_Integer length = lua_Integer(lua_rawlen(L, table));
    QVariantMap map;
    lua_Integer count = 0;
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        ++count;
        if (length == 0) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                lua_pop(L, 2);
                return false;
            }
            QVariant value;
            if (!readQmlDataValue(L, -1, value, depth)) {
                lua_pop(L, 2);
                return false;
            }
            map.insert(QString::fromUtf8(lua_tostring(L, -2)), value);
        }
        lua_pop(L, 1);
    }
    if (length == 0) {
        out = map;
        return true;
    }
    if (count != length)
        return false;
    QVariantList list;
    for (lua_Integer i = 1; i <= length; ++i) {
        lua_rawgeti(L, table, i);
        QVariant value;
        const bool ok = readQmlDataValue(L, -1, value, depth);
        lua_pop(L, 1);
        if (!ok)
            return false;
        list << value;
    }
    out = list;
    return true;
}

}

%}

%typemap(in) const QVariantMap &qmlData (QVariantMap parsed) {
    if (!lua_isnil(L, $input)) {
        QVariant value;
        if (!lua_istable(L, $input) || !readQmlDataTable(L, $input, value, 0)
            || value.userType() != QMetaType::QVariantMap) {
            SWIG_Lua_pusherrstring(L, "QML element data must be a table with string keys holding nil, booleans, numbers, strings or tables");
            SWIG_fail;
        }
        parsed = value.toMap();
    }
    $1 = &parsed;
}

%typecheck(SWIG_TYPECHECK_POINTER) const QVariantMap &qmlData {
    $1 = lua_istable(L, $input) || lua_isnil(L, $input);
}
```

- [ ] **Step 6: `sanguosha.i` 引入與宣告**

在 `%include "resolution-history.i"` 下一行加入 `%include "qml-element.i"`。在 Room 的 `void resetUi(ServerPlayer*player = nullptr);` 之後加入：

```
	void addQmlElement(const char*id, const char*qmlPath, const char*anchor, const QVariantMap &qmlData = QVariantMap(), ServerPlayer*player = nullptr);
	void updateQmlElement(const char*id, const QVariantMap &qmlData, ServerPlayer*player = nullptr);
	void removeQmlElement(const char*id, ServerPlayer*player = nullptr);
	void clearQmlElements(ServerPlayer*player = nullptr);
```

- [ ] **Step 7: 建置並檢查 SWIG overload**

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8 2>&1 | tail -3`
Expected: 成功連結，無 `error:`。

派 `swig-binding-checker` subagent 檢查本次 `swig/*.i` 變更，確認既有 Lua overload 沒有遺失。
Expected: 報告「no lost overloads」。

- [ ] **Step 8: Commit**

```bash
git add src/server/room.h src/server/room.cpp src/server/player-lifecycle-service.cpp src/server/game-session-controller.cpp swig/qml-element.i swig/sanguosha.i
git commit -m "feat(room): let a room add, update and remove table QML elements"
```

---

### Task 5: 客戶端接收與標記分流

**Files:**
- Modify: `src/client/client.h`（public 方法 `setUiTheme` 旁約第 166 行；signals `ui_theme_changed` 旁約第 518 行）
- Modify: `src/client/client.cpp`（callback 登記約第 132 行；`Client::setUiTheme` 之後；`Client::setMark` 約第 2587 行）
- Modify: `src/client/clientplayer.cpp:376`
- Modify: `src/ui/generic-cardcontainer-ui.cpp:698`

**Interfaces:**
- Consumes: `Engine::isQmlMark`（Task 2）。
- Produces:
  - `void Client::handleQmlElement(const QVariant &arg);`
  - signal `void Client::qml_element_received(const QVariantMap &payload);`
  - signal `void Client::qml_mark_changed(const QString &player, const QString &mark, int value);`

- [ ] **Step 1: `client.h`**

在 `void setUiTheme(const QVariant &arg);` 之後加入 `void handleQmlElement(const QVariant &arg);`。在 `void ui_theme_changed(...)` 之後加入：

```cpp
    // Room::addQmlElement and friends: {op, id, qml, anchor, data, scope}.
    void qml_element_received(const QVariantMap &payload);
    // A mark bound by Engine::addQmlMark changed; value 0 removes it.
    void qml_mark_changed(const QString &player, const QString &mark, int value);
```

- [ ] **Step 2: `client.cpp`**

callback 登記：

```cpp
	m_callbacks[S_COMMAND_SET_UI_THEME] = &Client::setUiTheme;
	m_callbacks[S_COMMAND_QML_ELEMENT] = &Client::handleQmlElement;
```

在 `Client::setUiTheme` 之後：

```cpp
void Client::handleQmlElement(const QVariant &arg)
{
	emit qml_element_received(arg.toMap());
}
```

`Client::setMark` 中 `player->setMark(mark, value);` 之後：

```cpp
	if (Sanguosha->isQmlMark(mark))
		emit qml_mark_changed(who, mark, value);
```

- [ ] **Step 3: `@` 標記文件略過已登記者**

`clientplayer.cpp` 標記文件迴圈內：

```cpp
			if (key.startsWith("@")&&marks[key]>0&&!Sanguosha->isQmlMark(key)) {
```

確認檔頭已有 `#include "engine.h"`，沒有就加入。

- [ ] **Step 4: `&` 標記不建按鈕**

`PlayerCardContainer::updateMark` 函式開頭：

```cpp
void PlayerCardContainer::updateMark(const QString &mark_name, int mark_num)
{
    // A QML-bound mark is drawn by QmlTableLayer instead of a pile button.
    if (Sanguosha->isQmlMark(mark_name))
        return;
```

- [ ] **Step 5: 建置**

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8 2>&1 | tail -3`
Expected: 成功連結，無 `error:`。

- [ ] **Step 6: Commit**

```bash
git add src/client/client.h src/client/client.cpp src/client/clientplayer.cpp src/ui/generic-cardcontainer-ui.cpp
git commit -m "feat(client): route QML elements and QML-bound marks away from the stock mark UI"
```

---

### Task 6: `QmlTableLayer` 本體（建立、更新、刪除、快照）

**Files:**
- Create: `src/ui/qml-table-layer.h`、`src/ui/qml-table-layer.cpp`
- Create: `qml/table/TableLayer.qml`、`qml/table/table_qml.qrc`
- Modify: `cmake/QSanguoshaSources.cmake`（`src/ui/room-overlay-host.cpp` 旁約第 223 行；moc header 清單 `src/ui/EmbeddedQmlLoader.h` 旁約第 389 行）
- Modify: `CMakeLists.txt`（`home_qml.qrc` 旁約第 1411 行；XP legacy `REMOVE_ITEM` 清單約第 286 行與 moc 清單約第 300 行）

**Interfaces:**
- Consumes: `QmlElementPath::isAllowed`、`QSanRuntimePaths::assetPath`、`Engine::qmlMarkFor`。
- Produces（Task 7、8、9 使用）：

```cpp
struct QmlSeatGeometry { QString player; QRectF sceneRect; qreal itemScale = 1.0; bool visible = true; bool self = false; QVariantMap snapshot; };
struct QmlTableGeometry { QRectF mainRect; QRectF headerRect; QRectF interactionRect; QPointF tableCenter;
                          RoomLayoutEngine::Profile profile = RoomLayoutEngine::Profile::LegacyLandscape; bool compactSeats = false; };
class QmlTableLayer : public QQuickWidget {
public:
    using GeometryProvider = std::function<void(QList<QmlSeatGeometry> *, QmlTableGeometry *)>;
    QmlTableLayer(QGraphicsView *view, QWidget *parent);
    void setGeometryProvider(GeometryProvider provider);
    void setVisualMode(qreal saturation, qreal contrast);
    QJsonObject snapshot() const;
public slots:
    void handleElement(const QVariantMap &payload);
    void setMark(const QString &player, const QString &mark, int value);
    void scheduleRelayout();
signals:
    void elementsChanged();
};
```

元件鍵：Room 元件為 `<scope>/<id>`（`all/board`、`player/mine`），標記元件為 `mark/<player>/<mark>`。

- [ ] **Step 1: 新增 `qml/table/TableLayer.qml`**

```qml
import QtQuick
import QtQuick.Effects

// Root of QmlTableLayer. Extension elements are created from C++ into `content`.
Item {
    id: root
    // FitView::applyVisualMode keeps these in step with the table's grayscale / high-contrast filter.
    property real saturation: 0
    property real contrast: 0

    Item {
        id: content
        objectName: "content"
        anchors.fill: parent
        // The software scene graph has no shader effects; elements then stay in colour.
        layer.enabled: (root.saturation !== 0 || root.contrast !== 0)
            && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            saturation: root.saturation
            contrast: root.contrast
        }
    }
}
```

- [ ] **Step 2: 新增 `qml/table/table_qml.qrc`**

```xml
<RCC>
  <qresource prefix="/QSanguosha/Table">
    <file alias="TableLayer.qml">TableLayer.qml</file>
  </qresource>
</RCC>
```

- [ ] **Step 3: 新增 `src/ui/qml-table-layer.h`**

```cpp
#ifndef QML_TABLE_LAYER_H
#define QML_TABLE_LAYER_H

#include "room-layout-engine.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QQuickWidget>
#include <QSet>
#include <QVariantMap>
#include <functional>

class QGraphicsView;
class QQmlComponent;
class QQuickItem;
class QTimer;

// One seat as the layer sees it: scene geometry plus a value snapshot of its
// player. Never a ClientPlayer*: the layer and its QML may outlive the player.
struct QmlSeatGeometry
{
    QString player;
    QRectF sceneRect;      // Photo, or the dashboard avatar area for the dashboard seat.
    qreal itemScale = 1.0; // The seat item's own scale in the scene.
    bool visible = true;
    bool self = false;
    QVariantMap snapshot;  // {objectName, general, seat, kingdom, alive, self}
};

// Table zones in scene coordinates, filled for both the legacy and the responsive layout.
struct QmlTableGeometry
{
    QRectF mainRect;
    QRectF headerRect;      // Invalid when the layout has no header.
    QRectF interactionRect; // Dashboard / hand area; table and screen elements stay off it.
    QPointF tableCenter;
    RoomLayoutEngine::Profile profile = RoomLayoutEngine::Profile::LegacyLandscape;
    bool compactSeats = false;
};

// Transparent QML layer over the room view for extension elements
// (Room::addQmlElement, Engine::addQmlMark). See docs/qml-table-elements.md.
class QmlTableLayer final : public QQuickWidget
{
    Q_OBJECT
public:
    using GeometryProvider = std::function<void(QList<QmlSeatGeometry> *, QmlTableGeometry *)>;

    QmlTableLayer(QGraphicsView *view, QWidget *parent);
    ~QmlTableLayer() override;

    void setGeometryProvider(GeometryProvider provider);
    void setVisualMode(qreal saturation, qreal contrast);
    // Smoke-test evidence: every live element with its view geometry and data.
    QJsonObject snapshot() const;

public slots:
    void handleElement(const QVariantMap &payload);
    void setMark(const QString &player, const QString &mark, int value);
    void scheduleRelayout();

signals:
    // Emitted after a relayout that follows an element being added, updated or removed.
    void elementsChanged();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Element
    {
        QString source;   // Relative .qml path as the room sent it.
        QString anchor;
        QString player;   // Seat the element follows; empty for table and screen anchors.
        QVariantMap data;
        QPointer<QQuickItem> frame; // Positioning wrapper owned by the layer.
        QPointer<QQuickItem> item;  // The extension's root item.
        QString error;
        QRectF viewRect;
        QRectF seatRect;
        bool visible = false;
        QVariantMap lastQs;
    };

    void addElement(const QString &key, Element element);
    void removeElement(const QString &key);
    QQuickItem *createItem(Element &element);
    void relayout();
    bool interactiveAt(const QPointF &pos) const;
    void setPassThrough(bool passThrough);

    QPointer<QGraphicsView> m_view;
    GeometryProvider m_provider;
    QPointer<QQuickItem> m_content;
    QMap<QString, Element> m_elements; // Sorted keys give a stable stacking order.
    QHash<QString, QQmlComponent *> m_components;
    QSet<QString> m_reportedErrors;
    QList<QRectF> m_interactiveRects;
    QTimer *m_relayoutTimer = nullptr;
    QmlTableGeometry m_table;
    bool m_reportPending = false;
    bool m_forwarding = false;
};

#endif
```

- [ ] **Step 4: 新增 `src/ui/qml-table-layer.cpp`（建立、更新、刪除、快照、網路限制）**

```cpp
#include "qml-table-layer.h"

#include "engine.h"
#include "qml-element-path.h"
#include "runtime-paths.h"

#include <QDir>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>

namespace {

// Extension QML is local presentation: only file: and qrc: load, everything else fails.
class LocalOnlyNetworkAccessManager final : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        const QString scheme = request.url().scheme();
        if (scheme == QLatin1String("file") || scheme == QLatin1String("qrc"))
            return QNetworkAccessManager::createRequest(op, request, data);
        QNetworkRequest blocked(request);
        blocked.setUrl(QUrl(QStringLiteral("qsan-blocked:")));
        return QNetworkAccessManager::createRequest(op, blocked, data);
    }
};

class LocalOnlyNetworkFactory final : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        return new LocalOnlyNetworkAccessManager(parent);
    }
};

QJsonObject rectJson(const QRectF &rect)
{
    return QJsonObject{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("w"), rect.width()}, {QStringLiteral("h"), rect.height()}};
}

}

QmlTableLayer::QmlTableLayer(QGraphicsView *view, QWidget *parent)
    : QQuickWidget(parent)
    , m_view(view)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setClearColor(Qt::transparent);
    setFocusPolicy(Qt::NoFocus);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setMouseTracking(true);
    engine()->setNetworkAccessManagerFactory(new LocalOnlyNetworkFactory);
    engine()->addImportPath(QSanRuntimePaths::assetPath(QStringLiteral(".")));
    setSource(QUrl(QStringLiteral("qrc:/QSanguosha/Table/TableLayer.qml")));
    if (QQuickItem *root = rootObject())
        m_content = root->findChild<QQuickItem *>(QStringLiteral("content"));
    if (!m_content)
        qWarning().noquote() << "QmlTableLayer: TableLayer.qml failed to load:" << errors();

    m_relayoutTimer = new QTimer(this);
    m_relayoutTimer->setSingleShot(true);
    m_relayoutTimer->setInterval(16); // At most one relayout per frame.
    connect(m_relayoutTimer, &QTimer::timeout, this, &QmlTableLayer::relayout);

    setPassThrough(true);
    if (m_view) {
        m_view->viewport()->installEventFilter(this);
        setGeometry(m_view->viewport()->rect());
    }
}

QmlTableLayer::~QmlTableLayer()
{
    // Items and components must go before QQuickWidget destroys the engine they belong to.
    for (Element &element : m_elements)
        delete element.frame.data();
    m_elements.clear();
    qDeleteAll(m_components);
    m_components.clear();
}

void QmlTableLayer::setGeometryProvider(GeometryProvider provider)
{
    m_provider = std::move(provider);
    scheduleRelayout();
}

void QmlTableLayer::setVisualMode(qreal saturation, qreal contrast)
{
    if (QQuickItem *root = rootObject()) {
        root->setProperty("saturation", saturation);
        root->setProperty("contrast", contrast);
    }
}

void QmlTableLayer::scheduleRelayout()
{
    if (!m_relayoutTimer->isActive())
        m_relayoutTimer->start();
}

void QmlTableLayer::handleElement(const QVariantMap &payload)
{
    const QString op = payload.value(QStringLiteral("op")).toString();
    const QString scope = payload.value(QStringLiteral("scope")).toString() == QLatin1String("player")
        ? QStringLiteral("player") : QStringLiteral("all");
    const QString key = scope + QLatin1Char('/') + payload.value(QStringLiteral("id")).toString();
    const QVariantMap data = payload.value(QStringLiteral("data")).toMap();
    if (op == QLatin1String("add")) {
        Element element;
        element.source = payload.value(QStringLiteral("qml")).toString();
        element.anchor = payload.value(QStringLiteral("anchor")).toString();
        if (element.anchor.startsWith(QLatin1String("seat:")))
            element.player = element.anchor.mid(5);
        element.data = data;
        addElement(key, element);
    } else if (op == QLatin1String("update")) {
        auto it = m_elements.find(key);
        if (it == m_elements.end())
            return;
        for (auto field = data.cbegin(); field != data.cend(); ++field)
            it->data.insert(field.key(), field.value());
    } else if (op == QLatin1String("remove")) {
        removeElement(key);
    } else if (op == QLatin1String("clear")) {
        const QString prefix = scope + QLatin1Char('/');
        for (const QString &existing : m_elements.keys())
            if (existing.startsWith(prefix))
                removeElement(existing);
    } else {
        qWarning().noquote() << "QmlTableLayer: unknown op" << op;
        return;
    }
    m_reportPending = true;
    scheduleRelayout();
}

void QmlTableLayer::setMark(const QString &player, const QString &mark, int value)
{
    const QString key = QStringLiteral("mark/%1/%2").arg(player, mark);
    if (value <= 0) {
        removeElement(key);
    } else if (m_elements.contains(key)) {
        m_elements[key].data.insert(QStringLiteral("value"), value);
    } else {
        const QmlMarkBinding binding = Sanguosha->qmlMarkFor(mark);
        if (!binding.isValid())
            return;
        Element element;
        element.source = binding.qmlPath;
        element.anchor = binding.anchor;
        element.player = player;
        element.data = QVariantMap{{QStringLiteral("mark"), mark}, {QStringLiteral("value"), value}};
        addElement(key, element);
    }
    m_reportPending = true;
    scheduleRelayout();
}

void QmlTableLayer::addElement(const QString &key, Element element)
{
    removeElement(key);
    if (m_content) {
        if (QQuickItem *item = createItem(element)) {
            auto *frame = new QQuickItem(m_content);
            frame->setParent(m_content);
            frame->setVisible(false);
            item->setParent(frame);
            item->setParentItem(frame);
            element.frame = frame;
            element.item = item;
        }
    } else {
        element.error = QStringLiteral("table layer failed to load");
    }
    if (!element.error.isEmpty() && !m_reportedErrors.contains(element.source)) {
        m_reportedErrors.insert(element.source);
        qWarning().noquote() << "QmlTableLayer:" << key << element.error;
    }
    m_elements.insert(key, element);
}

void QmlTableLayer::removeElement(const QString &key)
{
    auto it = m_elements.find(key);
    if (it == m_elements.end())
        return;
    if (it->frame)
        it->frame->deleteLater();
    m_elements.erase(it);
}

QQuickItem *QmlTableLayer::createItem(Element &element)
{
    QString error;
    if (!QmlElementPath::isAllowed(element.source, &error)) {
        element.error = error;
        return nullptr;
    }
    const QString file = QSanRuntimePaths::assetPath(QDir::cleanPath(QDir::fromNativeSeparators(element.source)));
    if (!QFileInfo(file).isFile()) {
        element.error = QStringLiteral("\"%1\" not found").arg(element.source);
        return nullptr;
    }
    QQmlComponent *&component = m_components[file];
    if (!component)
        component = new QQmlComponent(engine(), QUrl::fromLocalFile(file), QQmlComponent::PreferSynchronous);
    if (!component->isReady()) {
        element.error = component->isError() ? component->errorString() : QStringLiteral("component is not ready");
        return nullptr;
    }
    QObject *object = component->beginCreate(engine()->rootContext());
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        element.error = QStringLiteral("\"%1\" root is not an Item").arg(element.source);
        return nullptr;
    }
    component->completeCreate();
    return item;
}

QJsonObject QmlTableLayer::snapshot() const
{
    QJsonArray elements;
    for (auto it = m_elements.cbegin(); it != m_elements.cend(); ++it) {
        QJsonObject entry{
            {QStringLiteral("key"), it.key()},
            {QStringLiteral("source"), it->source},
            {QStringLiteral("anchor"), it->anchor},
            {QStringLiteral("player"), it->player},
            {QStringLiteral("visible"), it->visible},
            {QStringLiteral("error"), it->error},
            {QStringLiteral("rect"), rectJson(it->viewRect)},
            {QStringLiteral("data"), QJsonObject::fromVariantMap(it->data)},
            {QStringLiteral("interactive"), it->item && it->item->property("qsInteractive").toBool()},
            {QStringLiteral("instance"), it->item
                ? QString::number(reinterpret_cast<quintptr>(it->item.data()), 16) : QString()},
        };
        if (!it->player.isEmpty())
            entry.insert(QStringLiteral("seat_rect"), rectJson(it->seatRect));
        elements.append(entry);
    }
    const auto map = [this](const QRectF &rect) {
        return m_view && rect.isValid() ? QRectF(m_view->mapFromScene(rect).boundingRect()) : QRectF();
    };
    QJsonObject result{{QStringLiteral("elements"), elements},
                       {QStringLiteral("profile"), QString()},
                       {QStringLiteral("compact"), m_table.compactSeats}};
    if (m_view) {
        const QPointF center = m_view->mapFromScene(m_table.tableCenter);
        result.insert(QStringLiteral("table_center"),
                      QJsonObject{{QStringLiteral("x"), center.x()}, {QStringLiteral("y"), center.y()}});
        if (m_table.headerRect.isValid())
            result.insert(QStringLiteral("header_rect"), rectJson(map(m_table.headerRect)));
        if (m_table.interactionRect.isValid())
            result.insert(QStringLiteral("interaction_rect"), rectJson(map(m_table.interactionRect)));
    }
    return result;
}
```

`relayout()`、`event()`、`eventFilter()`、`interactiveAt()`、`setPassThrough()` 於 Task 7、Task 8 補上；本任務先放最小版本讓建置通過：

```cpp
void QmlTableLayer::relayout()
{
    if (m_reportPending) {
        m_reportPending = false;
        emit elementsChanged();
    }
}

bool QmlTableLayer::interactiveAt(const QPointF &) const
{
    return false;
}

void QmlTableLayer::setPassThrough(bool passThrough)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, passThrough);
}

bool QmlTableLayer::event(QEvent *event)
{
    return QQuickWidget::event(event);
}

bool QmlTableLayer::eventFilter(QObject *watched, QEvent *event)
{
    return QQuickWidget::eventFilter(watched, event);
}
```

- [ ] **Step 5: CMake 登記**

`cmake/QSanguoshaSources.cmake`：在 `src/ui/room-overlay-host.cpp` 之前加入 `src/ui/qml-table-layer.cpp`；在 moc header 清單 `src/ui/EmbeddedQmlLoader.h` 之後加入 `src/ui/qml-table-layer.h`。

`CMakeLists.txt`：
- 在 `"${CMAKE_CURRENT_SOURCE_DIR}/qml/home/home_qml.qrc"` 下一行加入 `"${CMAKE_CURRENT_SOURCE_DIR}/qml/table/table_qml.qrc"`。
- 在 XP legacy 的 `list(REMOVE_ITEM QSAN_SOURCES ...)` 加入 `src/ui/qml-table-layer.cpp`，在對應的 moc `REMOVE_ITEM` 加入 `src/ui/qml-table-layer.h`。
- 確認 GUI target 已連結 `Qt6::Network`（`grep -n "Qt6::Network" CMakeLists.txt`）；若沒有，在 `Qt6::QuickWidgets` 下一行加入。
- 確認 Qt 已含 `QtQuick.Effects`：`ls ~/.local/qt6111/*/qml/QtQuick/Effects`，應列出 `qmldir`。

- [ ] **Step 6: 建置**

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha -j8 2>&1 | tail -3`
Expected: 成功連結。

- [ ] **Step 7: Commit**

```bash
git add src/ui/qml-table-layer.h src/ui/qml-table-layer.cpp qml/table/TableLayer.qml qml/table/table_qml.qrc cmake/QSanguoshaSources.cmake CMakeLists.txt
git commit -m "feat(ui): add the QML table layer that hosts extension elements"
```

---

### Task 7: 幾何與錨點（含直向）並接上 `RoomScene`／`FitView`

**Files:**
- Modify: `src/ui/qml-table-layer.cpp`（取代 Task 6 的最小 `relayout()`）
- Modify: `src/ui/roomscene.h`（`attachOverlay` 宣告旁；成員 `m_overlayHost` 旁）
- Modify: `src/ui/roomscene.cpp`（`RoomScene::attachOverlay` 之後）
- Modify: `src/ui/game-view.h`（成員 `m_overlay` 旁）
- Modify: `src/ui/game-view.cpp`（`ensureRoomOverlay`、`applyVisualMode`、`refit` 結尾 `m_overlay->raise();` 處、`resizeEvent`）

**Interfaces:**
- Consumes: Task 6 的 `QmlTableLayer`、`QmlSeatGeometry`、`QmlTableGeometry`；Task 5 的兩個 Client signal。
- Produces:
  - `void RoomScene::attachQmlLayer(QmlTableLayer *layer);`
  - `QmlTableLayer *RoomScene::qmlLayer() const;`
  - signal `void RoomScene::qmlLayerAttached();`
  - `void RoomScene::collectQmlGeometry(QList<QmlSeatGeometry> *seats, QmlTableGeometry *table) const;`
  - `qs` 欄位：`data`、`player`、`scale`、`profile`、`compact`。

- [ ] **Step 1: 圖層 `relayout()`**

在 `qml-table-layer.cpp` 匿名命名空間加入：

```cpp
QString profileName(RoomLayoutEngine::Profile profile)
{
    switch (profile) {
    case RoomLayoutEngine::Profile::CompactPortrait: return QStringLiteral("portrait");
    case RoomLayoutEngine::Profile::CompactLandscape: return QStringLiteral("compact-landscape");
    case RoomLayoutEngine::Profile::Medium: return QStringLiteral("medium");
    case RoomLayoutEngine::Profile::ExpandedSplit: return QStringLiteral("split");
    case RoomLayoutEngine::Profile::Book: return QStringLiteral("book");
    case RoomLayoutEngine::Profile::Tabletop: return QStringLiteral("tabletop");
    case RoomLayoutEngine::Profile::LargeRoom: return QStringLiteral("large-room");
    case RoomLayoutEngine::Profile::LegacyLandscape: break;
    }
    return QStringLiteral("landscape");
}

constexpr qreal kGap = 4.0;
constexpr qreal kEdge = 8.0;
```

以下列版本取代最小 `relayout()`，並在 `snapshot()` 中把 `profile` 改為 `profileName(m_table.profile)`：

```cpp
void QmlTableLayer::relayout()
{
    if (!m_view || !m_content)
        return;
    QList<QmlSeatGeometry> seats;
    QmlTableGeometry table;
    if (m_provider)
        m_provider(&seats, &table);
    m_table = table;
    const auto map = [this](const QRectF &rect) {
        return rect.isValid() ? QRectF(m_view->mapFromScene(rect).boundingRect()) : QRectF();
    };
    const qreal viewScale = m_view->transform().m11();
    const QRectF main = table.mainRect.isValid() ? map(table.mainRect) : QRectF(rect());
    const QRectF header = map(table.headerRect);
    const QRectF interaction = map(table.interactionRect);
    QHash<QString, const QmlSeatGeometry *> seatByPlayer;
    for (const QmlSeatGeometry &seat : seats)
        seatByPlayer.insert(seat.player, &seat);

    // Group elements that share an anchor; each group is laid out as one row.
    struct Placement { Element *element; QSizeF size; qreal scale; };
    QMap<QString, QList<Placement>> groups;
    for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        Element &element = *it;
        element.visible = false;
        if (!element.frame || !element.item) {
            element.viewRect = QRectF();
            continue;
        }
        const QmlSeatGeometry *seat = element.player.isEmpty() ? nullptr : seatByPlayer.value(element.player);
        if (!element.player.isEmpty() && (!seat || !seat->visible)) {
            element.frame->setVisible(false);
            element.viewRect = QRectF();
            continue;
        }
        const qreal scale = seat ? viewScale * seat->itemScale : viewScale;
        QVariantMap qs{{QStringLiteral("data"), element.data},
                       {QStringLiteral("player"), seat ? QVariant(seat->snapshot) : QVariant()},
                       {QStringLiteral("scale"), scale},
                       {QStringLiteral("profile"), profileName(table.profile)},
                       {QStringLiteral("compact"), table.compactSeats}};
        if (qs != element.lastQs) {
            element.item->setProperty("qs", qs);
            element.lastQs = qs;
        }
        QSizeF size(element.item->implicitWidth(), element.item->implicitHeight());
        if (size.isEmpty())
            size = element.item->size();
        element.seatRect = seat ? map(seat->sceneRect) : QRectF();
        QString group = element.anchor;
        if (seat) {
            const bool centered = element.anchor == QLatin1String("avatar") || element.anchor.startsWith(QLatin1String("seat:"));
            // Ribbon seats are too small for rows outside them: every non-centred seat anchor
            // becomes one row along the inside bottom of the seat.
            group = element.player + QLatin1Char('|')
                + (table.compactSeats && !centered ? QStringLiteral("compact") : (centered ? QStringLiteral("center") : element.anchor));
        }
        groups[group].append({&element, size * scale, scale});
    }

    m_interactiveRects.clear();
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        const QList<Placement> &row = group.value();
        qreal width = -kGap, height = 0;
        for (const Placement &p : row) {
            width += p.size.width() + kGap;
            height = qMax(height, p.size.height());
        }
        const QString kind = group.key().section(QLatin1Char('|'), -1);
        const Element *first = row.first().element;
        const QRectF seat = first->seatRect;
        QPointF origin;
        bool clipToSeat = false;
        if (!first->player.isEmpty()) {
            if (kind == QLatin1String("center"))
                origin = QPointF(seat.center().x() - width / 2, seat.center().y() - height / 2);
            else if (kind == QLatin1String("compact")) {
                origin = QPointF(seat.left() + 2, seat.bottom() - height - 2);
                clipToSeat = true;
            } else if (kind == QLatin1String("top"))
                origin = QPointF(seat.left(), seat.top() - height - 2);
            else if (kind == QLatin1String("bottom"))
                origin = QPointF(seat.left(), seat.bottom() + 2);
            else // mark-area
                origin = QPointF(seat.left() + 2, seat.top() + 2);
        } else if (kind == QLatin1String("table-center")) {
            const QPointF center = m_view->mapFromScene(table.tableCenter);
            origin = QPointF(center.x() - width / 2, center.y() - height / 2);
        } else if (kind == QLatin1String("screen-top")) {
            const qreal top = header.isValid() ? header.bottom() + kEdge : main.top() + kEdge;
            origin = QPointF(main.center().x() - width / 2, top);
        } else if (kind == QLatin1String("screen-bottom")) {
            const qreal bottom = interaction.isValid() ? interaction.top() - kEdge : main.bottom() - kEdge;
            origin = QPointF(main.center().x() - width / 2, bottom - height);
        } else if (kind == QLatin1String("screen-top-left")) {
            origin = QPointF(main.left() + kEdge, (header.isValid() ? header.bottom() : main.top()) + kEdge);
        } else if (kind == QLatin1String("screen-top-right")) {
            origin = QPointF(main.right() - kEdge - width, (header.isValid() ? header.bottom() : main.top()) + kEdge);
        } else if (kind == QLatin1String("screen-bottom-left")) {
            origin = QPointF(main.left() + kEdge, (interaction.isValid() ? interaction.top() : main.bottom()) - kEdge - height);
        } else { // screen-bottom-right
            origin = QPointF(main.right() - kEdge - width, (interaction.isValid() ? interaction.top() : main.bottom()) - kEdge - height);
        }

        qreal x = origin.x();
        for (const Placement &p : row) {
            Element &element = *p.element;
            QRectF box(QPointF(x, origin.y()), p.size);
            x += p.size.width() + kGap;
            if (clipToSeat)
                box = box.intersected(seat);
            element.frame->setPosition(box.topLeft());
            element.frame->setSize(box.size());
            element.frame->setClip(clipToSeat);
            element.item->setTransformOrigin(QQuickItem::TopLeft);
            element.item->setScale(p.scale);
            element.item->setPosition(QPointF(0, 0));
            element.frame->setVisible(!box.isEmpty());
            element.visible = !box.isEmpty();
            element.viewRect = box;
            if (element.visible && element.item->property("qsInteractive").toBool())
                m_interactiveRects.append(box);
        }
    }

    if (m_reportPending) {
        m_reportPending = false;
        emit elementsChanged();
    }
}
```

- [ ] **Step 2: `RoomScene` 提供幾何並接上圖層**

`roomscene.h`：在 `class RoomOverlayHost;` 前置宣告旁加入 `class QmlTableLayer;` 與 `struct QmlSeatGeometry;`、`struct QmlTableGeometry;`。public 區 `attachOverlay` 宣告後加入：

```cpp
    void attachQmlLayer(QmlTableLayer *layer);
    QmlTableLayer *qmlLayer() const { return m_qmlLayer; }
    void collectQmlGeometry(QList<QmlSeatGeometry> *seats, QmlTableGeometry *table) const;
```

signals 區加入 `void qmlLayerAttached();`；`m_overlayHost` 成員旁加入 `QPointer<QmlTableLayer> m_qmlLayer;`。

`roomscene.cpp` 檔頭加入 `#include "qml-table-layer.h"`（以 `#if QSAN_ENABLE_QML` 包住，與 `EmbeddedQmlLoader.h` 相同處理），在 `RoomScene::attachOverlay` 之後加入：

```cpp
void RoomScene::attachQmlLayer(QmlTableLayer *layer)
{
#if QSAN_ENABLE_QML
    m_qmlLayer = layer;
    QPointer<RoomScene> self(this);
    layer->setGeometryProvider([self](QList<QmlSeatGeometry> *seats, QmlTableGeometry *table) {
        if (self)
            self->collectQmlGeometry(seats, table);
    });
    connect(this, &QGraphicsScene::changed, layer, &QmlTableLayer::scheduleRelayout);
    connect(this, &RoomScene::responsiveGeometryChanged, layer, &QmlTableLayer::scheduleRelayout);
    connect(ClientInstance, &Client::qml_element_received, layer, &QmlTableLayer::handleElement);
    connect(ClientInstance, &Client::qml_mark_changed, layer, &QmlTableLayer::setMark);
    // Marks that arrived before this layer existed (reconnect, rebuilt scene).
    for (const ClientPlayer *player : ClientInstance->getPlayers()) {
        for (const QString &mark : player->getMarkNames()) {
            if (Sanguosha->isQmlMark(mark))
                layer->setMark(player->objectName(), mark, player->getMark(mark));
        }
    }
    emit qmlLayerAttached();
#else
    Q_UNUSED(layer)
#endif
}

void RoomScene::collectQmlGeometry(QList<QmlSeatGeometry> *seats, QmlTableGeometry *table) const
{
#if QSAN_ENABLE_QML
    const auto snapshotOf = [](const ClientPlayer *player, bool self) {
        return QVariantMap{
            {QStringLiteral("objectName"), player->objectName()},
            {QStringLiteral("general"), player->getGeneralName()},
            {QStringLiteral("seat"), player->getSeat()},
            {QStringLiteral("kingdom"), player->getKingdom()},
            {QStringLiteral("alive"), player->isAlive()},
            {QStringLiteral("self"), self}};
    };
    // Paged-out seats keep isVisible() but drop to opacity 0.
    const auto shown = [](const QGraphicsItem *item) {
        return item->isVisible() && item->effectiveOpacity() > 0.0;
    };
    if (dashboard && dashboard->getPlayer()) {
        const ClientPlayer *player = dashboard->getPlayer();
        QmlSeatGeometry seat;
        seat.player = player->objectName();
        // The avatar area, not the whole dashboard: in portrait the dashboard spans the hand row.
        seat.sceneRect = dashboard->getAvatarAreaSceneBoundingRect();
        seat.itemScale = dashboard->sceneTransform().m11();
        seat.visible = shown(dashboard);
        seat.self = true;
        seat.snapshot = snapshotOf(player, true);
        seats->append(seat);
    }
    for (Photo *photo : photos) {
        const ClientPlayer *player = photo->getPlayer();
        if (!player)
            continue;
        QmlSeatGeometry seat;
        seat.player = player->objectName();
        seat.sceneRect = photo->sceneBoundingRect();
        seat.itemScale = photo->sceneTransform().m11();
        seat.visible = shown(photo);
        seat.snapshot = snapshotOf(player, false);
        seats->append(seat);
    }
    if (m_responsiveEnabled && m_responsiveLayout.valid) {
        const auto &layout = m_responsiveLayout;
        table->mainRect = layout.mainRect;
        table->headerRect = layout.headerRect;
        table->interactionRect = layout.interactionRect;
        table->tableCenter = layout.tableCenter;
        table->profile = layout.profile;
        table->compactSeats = layout.seatPresentation == RoomLayoutEngine::SeatPresentation::Ribbon;
    } else {
        table->mainRect = sceneRect();
        table->interactionRect = dashboard ? dashboard->sceneBoundingRect() : QRectF();
        table->tableCenter = m_tablePile ? m_tablePile->sceneBoundingRect().center() : sceneRect().center();
    }
#else
    Q_UNUSED(seats)
    Q_UNUSED(table)
#endif
}
```

若 `Photo::getPlayer()` 回傳型別不是 `const ClientPlayer *`，以 `qobject_cast<const ClientPlayer *>` 轉型；若 `m_tablePile` 名稱不同，以 `grep -n "TablePile \*" src/ui/roomscene.h` 確認。

- [ ] **Step 3: `FitView` 建立圖層、疊放、視覺模式、refit**

`game-view.h`：前置宣告 `class QmlTableLayer;`，成員 `QPointer<RoomOverlayHost> m_overlay;` 之後加入 `QPointer<QmlTableLayer> m_qmlLayer;`。

`game-view.cpp` 檔頭加入：

```cpp
#if QSAN_ENABLE_QML
#include "qml-table-layer.h"
#endif
```

`ensureRoomOverlay` 中 `room->attachOverlay(m_overlay);` 之後加入：

```cpp
#if QSAN_ENABLE_QML
    delete m_qmlLayer;
    m_qmlLayer = new QmlTableLayer(this, viewport());
    // Below the overlay host: the inspector and seat scroller cover extension elements.
    m_qmlLayer->stackUnder(m_overlay);
    room->attachQmlLayer(m_qmlLayer);
    connect(room, &QObject::destroyed, m_qmlLayer, &QObject::deleteLater);
    m_qmlLayer->show();
#endif
```

`applyVisualMode` 的 `#elif !defined(QSAN_XP_LEGACY)` 區塊內，`m_overlay->setGraphicsEffect(...)` 之後加入：

```cpp
#if QSAN_ENABLE_QML
    // The QML layer is a separate GPU surface; its root applies the same filter itself.
    if (m_qmlLayer)
        m_qmlLayer->setVisualMode(active && isGrayscaleMode() ? -1.0 : 0.0,
                                  active && !isGrayscaleMode() ? 0.35 : 0.0);
#endif
```

`refit` 中 `m_overlay->raise();` 之後加入：

```cpp
#if QSAN_ENABLE_QML
        if (m_qmlLayer) {
            m_qmlLayer->setGeometry(viewport()->rect());
            m_qmlLayer->stackUnder(m_overlay);
            m_qmlLayer->scheduleRelayout();
        }
#endif
```

在 `FitView::resizeEvent` 中，呼叫基底之後加入同樣的 `setGeometry(viewport()->rect())` 與 `scheduleRelayout()`。

- [ ] **Step 4: 建置**

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha -j8 2>&1 | tail -3`
Expected: 成功連結。

- [ ] **Step 5: 生命週期審查**

派 `card-lifetime-reviewer` subagent 審查本任務的 diff，重點是圖層、QML 引擎、`RoomScene` 與 `ClientInstance` 的銷毀順序（provider lambda 以 `QPointer<RoomScene>` 防護、`~QmlTableLayer` 先刪元件與 component）。
Expected: 無高嚴重度問題；有問題先修再繼續。

- [ ] **Step 6: Commit**

```bash
git add src/ui/qml-table-layer.cpp src/ui/roomscene.h src/ui/roomscene.cpp src/ui/game-view.h src/ui/game-view.cpp
git commit -m "feat(ui): anchor table QML elements to seats and table zones, portrait included"
```

---

### Task 8: 滑鼠與觸控

**Files:**
- Modify: `src/ui/qml-table-layer.cpp`（取代 Task 6 的 `interactiveAt`、`event`、`eventFilter`）

**Interfaces:**
- Consumes: Task 7 填入的 `m_interactiveRects`。
- Produces: 只有 `qsInteractive: true` 的元件吃得到指標事件；其餘落到牌桌。

- [ ] **Step 1: 實作**

檔頭加入 `#include "pointer-hover-delivery.h"`、`#include <QMouseEvent>`、`#include <QTouchEvent>`、`#include <QCoreApplication>`，並以下列版本取代：

```cpp
bool QmlTableLayer::interactiveAt(const QPointF &pos) const
{
    for (const QRectF &rect : m_interactiveRects)
        if (rect.contains(pos))
            return true;
    return false;
}

void QmlTableLayer::setPassThrough(bool passThrough)
{
    if (testAttribute(Qt::WA_TransparentForMouseEvents) != passThrough)
        setAttribute(Qt::WA_TransparentForMouseEvents, passThrough);
}

// While the pointer is over an interactive element the layer takes input itself;
// leaving it hands the next events back to the table.
bool QmlTableLayer::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
        if (!m_forwarding && !interactiveAt(static_cast<QMouseEvent *>(event)->position()))
            setPassThrough(true);
        break;
    case QEvent::HoverMove:
        if (!m_forwarding && !interactiveAt(static_cast<QHoverEvent *>(event)->position()))
            setPassThrough(true);
        break;
    case QEvent::Leave:
        if (!m_forwarding)
            setPassThrough(true);
        break;
    default:
        break;
    }
    return QQuickWidget::event(event);
}

bool QmlTableLayer::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport())
        return QQuickWidget::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::Resize:
        setGeometry(m_view->viewport()->rect());
        scheduleRelayout();
        break;
    case QEvent::MouseMove:
    case QEvent::HoverEnter:
    case QEvent::HoverMove: {
        const QPointF pos = event->type() == QEvent::MouseMove
            ? static_cast<QMouseEvent *>(event)->position() : static_cast<QHoverEvent *>(event)->position();
        if (m_forwarding) {
            QCoreApplication::sendEvent(this, event);
            return true;
        }
        if (interactiveAt(pos)) {
            setPassThrough(false);
            // Wayland may send hover without a mouse move; QQuickWidget only maps mouse moves.
            qsanForwardPointerHoverAsMouseMove(this, event);
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        // A click that reached the viewport first (no hover before it) is handed over whole.
        if (interactiveAt(static_cast<QMouseEvent *>(event)->position())) {
            m_forwarding = true;
            setPassThrough(false);
            QCoreApplication::sendEvent(this, event);
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (m_forwarding) {
            QCoreApplication::sendEvent(this, event);
            m_forwarding = false;
            return true;
        }
        break;
    case QEvent::TouchBegin: {
        const auto *touch = static_cast<QTouchEvent *>(event);
        if (!touch->points().isEmpty() && interactiveAt(touch->points().first().position())) {
            m_forwarding = true;
            setPassThrough(false);
            QCoreApplication::sendEvent(this, event);
            return true;
        }
        break;
    }
    case QEvent::TouchUpdate:
        if (m_forwarding) {
            QCoreApplication::sendEvent(this, event);
            return true;
        }
        break;
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (m_forwarding) {
            QCoreApplication::sendEvent(this, event);
            m_forwarding = false;
            return true;
        }
        break;
    default:
        break;
    }
    return QQuickWidget::eventFilter(watched, event);
}
```

viewport 與圖層位置相同（圖層填滿 viewport），事件座標不需轉換。

- [ ] **Step 2: 建置**

Run: `cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha -j8 2>&1 | tail -3`
Expected: 成功連結。

- [ ] **Step 3: Commit**

```bash
git add src/ui/qml-table-layer.cpp
git commit -m "feat(ui): let only interactive table QML elements take pointer and touch input"
```

---

### Task 9: smoke 量測點與 E2E 轉綠

**Files:**
- Modify: `src/ui/testing/network-ui-smoke-report.h/.cpp`（新旗標 `--network-ui-smoke-window`）
- Modify: `src/ui/testing/network-ui-smoke-controller.h/.cpp`
- Modify: `tools/autotest/gui_network_smoke.py`（`--client-window`）

**Interfaces:**
- Consumes: `RoomScene::qmlLayer()`、`RoomScene::qmlLayerAttached`、`QmlTableLayer::elementsChanged`、`QmlTableLayer::snapshot()`、`FitView::setResponsiveRoomEnabled`。
- Produces: 階段 `qml_elements:<n>`，details 即 `snapshot()` 的內容（`elements`、`profile`、`compact`、`table_center`、`header_rect`、`interaction_rect`）。

- [ ] **Step 1: 旗標**

`network-ui-smoke-report.h`，在 `FlagScreenshotPath` 之後：

```cpp
    static const char *const FlagWindowSize;        // "--network-ui-smoke-window" (WxH)
```

並宣告 `static QSize parseWindowSize(const QStringList &arguments);`（檔頭加 `#include <QSize>`）。

`network-ui-smoke-report.cpp`：

```cpp
const char *const NetworkUiSmokeReport::FlagWindowSize = "--network-ui-smoke-window";

QSize NetworkUiSmokeReport::parseWindowSize(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagWindowSize), &found);
    const QStringList parts = value.split(QLatin1Char('x'));
    if (!found || parts.size() != 2)
        return QSize();
    return QSize(parts.at(0).toInt(), parts.at(1).toInt());
}
```

- [ ] **Step 2: 控制器**

`network-ui-smoke-controller.h`：成員加入 `QSize m_windowSize;`、`int m_qmlSnapshots = 0;`，私有 slot 加入 `void onQmlElementsChanged();`、`void attachQmlLayer();`。

`network-ui-smoke-controller.cpp`：
- `configure()` 末尾：`m_windowSize = NetworkUiSmokeReport::parseWindowSize(arguments);`
- `attach()` 中 `m_mainWindow = mainWindow;` 之後：

```cpp
    if (mainWindow && m_windowSize.isValid())
        mainWindow->resize(m_windowSize);
```

- `onRoomSceneCreated()` 中 `m_roomSceneReady = true;` 之後：

```cpp
    // A portrait window only becomes the portrait table with the responsive layout on.
    if (m_windowSize.isValid() && m_windowSize.height() > m_windowSize.width() && m_mainWindow) {
        if (FitView *view = m_mainWindow->findChild<FitView *>())
            view->setResponsiveRoomEnabled(true);
    }
    connect(scene, &RoomScene::qmlLayerAttached, this, &NetworkUiSmokeController::attachQmlLayer,
            Qt::UniqueConnection);
    attachQmlLayer();
```

- 新增：

```cpp
void NetworkUiSmokeController::attachQmlLayer()
{
#if QSAN_ENABLE_QML
    if (m_roomScene.isNull() || !m_roomScene->qmlLayer())
        return;
    connect(m_roomScene->qmlLayer(), &QmlTableLayer::elementsChanged,
            this, &NetworkUiSmokeController::onQmlElementsChanged, Qt::UniqueConnection);
#endif
}

// Evidence, not a gate: qml_table_layer_smoke.py judges these snapshots.
void NetworkUiSmokeController::onQmlElementsChanged()
{
#if QSAN_ENABLE_QML
    if (m_finished || m_roomScene.isNull() || !m_roomScene->qmlLayer())
        return;
    emitStage(QStringLiteral("qml_elements:%1").arg(++m_qmlSnapshots), true,
              m_roomScene->qmlLayer()->snapshot());
#endif
}
```

檔頭加入 `#include "game-view.h"`，以及在 `#if QSAN_ENABLE_QML` 內 `#include "qml-table-layer.h"`。

若 `emitStage` 會拒絕未知階段名稱（`NetworkUiSmokeReport::isKnownStage`），比照 `ui_theme_changed:` 的處理方式放行 `qml_elements:` 前綴：`grep -n "ui_theme_changed" src/ui/testing/network-ui-smoke-report.cpp`，在同一處加入 `qml_elements:`。

- [ ] **Step 3: `gui_network_smoke.py`**

`build_client_command` 中 `--effects-profile` 判斷之後：

```python
    if getattr(args, "client_window", None):
        command += ["--network-ui-smoke-window", args.client_window]
        if args.xvfb:
            width, height = args.client_window.split("x")
            screen = "%dx%d" % (max(1280, int(width)), max(720, int(height)))
```

並把 xvfb 那行的 `"-screen 0 1280x720x24"` 改為使用 `screen`（未設定時維持 `1280x720`）：

```python
    if args.xvfb:
        screen = "1280x720"
        if getattr(args, "client_window", None):
            width, height = (int(v) for v in args.client_window.split("x"))
            screen = "%dx%d" % (max(1280, width), max(720, height))
        command = ["xvfb-run", "-a", "-s", "-screen 0 %sx24" % screen] + command
```

（上面第一段只保留 `command += [...]` 那一行，螢幕尺寸統一由第二段處理。）

parser 加入：

```python
    parser.add_argument("--client-window", default=None,
                        help="client window size WxH; taller than wide turns on the portrait (responsive) table")
```

- [ ] **Step 4: 建置**

Run: `pgrep -af "ninja|cmake --build"; cmake --build builds/cmake-linux-gui-gcc-debug --target QSanguosha qsanguosha_server -j8 2>&1 | tail -3`
Expected: 成功連結。

- [ ] **Step 5: 跑 E2E**

依 `memory/linux-gui-network-smoke.md` 的環境（WSLg 用 `--no-xvfb --platform xcb`）：

Run: `python3 tools/autotest/qml_table_layer_smoke.py --exe-root . --server-exe debug/qsanguosha_server --client-exe debug/QSanguosha --artifact-dir artifacts/qml-table-layer --seed 20261007 --no-xvfb --platform xcb`
Expected: 最後一行 `QML table layer smoke PASSED`；`artifacts/qml-table-layer/qml-table-layer-summary.json` 兩個 run 的 `ok` 皆為 `true`。

失敗時依 `problems` 逐項回到對應任務修正，不放寬腳本條件。直向 run 若回報 `profile` 不是 `portrait`，先確認 540x960 視窗是否被主視窗最小尺寸撐大（`--network-ui-smoke-window` 之後量 `mainWindow->size()`）。

- [ ] **Step 6: 回歸既有主題 smoke**

Run: `python3 tools/autotest/ui_theme_runtime_smoke.py --exe-root . --server-exe debug/qsanguosha_server --client-exe debug/QSanguosha --artifact-dir artifacts/ui-theme-runtime --seed 20261007 --no-xvfb --platform xcb`
Expected: `UI theme runtime smoke PASSED`。

- [ ] **Step 7: Commit**

```bash
git add src/ui/testing/network-ui-smoke-report.h src/ui/testing/network-ui-smoke-report.cpp src/ui/testing/network-ui-smoke-controller.h src/ui/testing/network-ui-smoke-controller.cpp tools/autotest/gui_network_smoke.py
git commit -m "test(qml-table): report table QML elements from the network smoke, landscape and portrait"
```

---

### Task 10: 擴展作者文件與記憶

**Files:**
- Create: `docs/qml-table-elements.md`
- Modify: `docs/README.md`（分類索引加一行）
- Modify: `memory/state.md`（以 shell 局部更新，UTF8-BOM、LF）

- [ ] **Step 1: 新增 `docs/qml-table-elements.md`**

內容（繁體書面語）須涵蓋：
1. 兩條路線的 Lua API 與範例（直接沿用規格「擴展 API」一節的程式碼）。
2. QML 端合約：`qs.data`、`qs.player`、`qs.scale`、`qs.profile`、`qs.compact`、`qsInteractive`，以及 `implicitWidth`／`implicitHeight` 決定尺寸。
3. 錨點表，以及直向（`qs.compact` 為真）時座位錨點改為座位內底部一列。
4. 限制：只在 GUI 客戶端顯示，TUI、Web、Excel、Sheets 不顯示，不可承載遊戲必要資訊；不能連網；`data` 16 KB 與每接收者 64 個元件的上限；路徑規則。
5. 觸控：沒有懸停，互動改用 `TapHandler`，可互動範圍不小於 48 邏輯像素。
6. 需要玩家作答時使用 `askForQml`（連結 `docs/ask-for-qml.md`）。
7. 驗證方式：`tools/autotest/qml_table_layer_smoke.py` 的指令。

- [ ] **Step 2: `docs/README.md` 索引**

在 UI／擴展相關分類下加入：

```markdown
- [牌桌 QML 掛載元件](qml-table-elements.md)：擴展以標記或 Room 指令在牌桌上放 QML 元件。
```

- [ ] **Step 3: 更新 `memory/state.md`**

把 `[State]` 中「牌桌 QML 掛載元件（2026-10-07）」那一行改為已完成狀態（提交範圍、E2E 指令與產物路徑），並在 `[Debts]` 記下未驗項目：錄影重播、TUI 實機、Android 觸控。

- [ ] **Step 4: Commit**

```bash
git add docs/qml-table-elements.md docs/README.md
git commit -m "docs: guide extension authors through table QML elements"
```
