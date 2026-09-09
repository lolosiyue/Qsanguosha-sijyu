# P1: 宣告式擴展 manifest 與身份拆分 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把擴展載入順序由隱式檔名排序換成 `lua/config.lua` 內的宣告式有序 manifest,並把 rules 身份拆成 build 時封死的 code identity 與執行期協商的 content identity。

**Architecture:** 新增一個純資料的 manifest 解析器(不碰 Lua、不碰 Engine,可獨立單元測試),由 `Engine` 在 `lua/config.lua` 執行後讀入,交給 `lua/sanguosha.lua` 照宣告順序載入擴展。原本「五檔白名單以外一律拒絕」的內容掃描改為「未宣告即拒絕」,Lua 位元組快照拆成核心相與宣告相。身份匯出器新增 `code_id` 子封印,`content_profile` 由 `builtin-v1` 改為 `declared-v1`。

**Tech Stack:** C++17 / Qt 6.11.1、Lua 5.4(經 SWIG bindings)、CMake + CTest、Python 3 harness、TypeScript(web 身份比對)

**Spec:** `docs/superpowers/specs/2026-09-09-extension-compatibility-scope-design.md`

**後續計劃(不在本計劃內):** P2 內容配送(spec §4、G3、G4);P3 TS fallback 清除(spec §5、G5)。

## 2026-09-09 實作紀錄

P1 五個 task、P2 配送與 P3 TS fallback 清除均已實作並完成聚焦驗收。
下方未勾選的 Step 是原始實作配方，並非目前待辦；未逐 task 拆成五個 commit，
本次按完整功能一併提交。最新驗收與剩餘工作以設計文件的實作紀錄為準。

實作時修正了下方範例的幾個問題，以下述結果為準：

- `code_id` 不同依 spec §3.7 回 `rules_version_mismatch`；C++ 與 Web 都重算並驗證子封印，不能只驗總封印。
- 新增 `lang` 宣告會改變 `config.lua` 位元組；驗收比較的是**同一份宣告**下修改翻譯前後的身份。
- G1 使用三個確實帶牌的合成擴展，並比較同一集合的不同順序；空 Package 無法證明 card ID 穩定性。
- `stage_builtin_assets()` 預設明確寫入空 manifest，保持既有 native/WASM 核心 fixtures 一致；只有顯式傳入條目才複製 script、libs、lang。
- `ai` 宣告是可選 server policy，client 不必備有該檔；在磁碟上存在時仍須遵守 symlink 禁令。
- Engine 嚴格要求 `extension_names` 是連續字串陣列，拒絕錯誤型別、稀疏陣列及額外 key。
- Lua 以宣告的精確檔案路徑建立 preload，再 `require`；合法的 `probe.one.lua` 不會誤查成 `probe/one.lua`。
- 使用實際 Qt 6.11.1 `QDir::entryList(QDir::Files)` 核對初始 102 個條目，與宣告逐項一致。此為順序遷移證據，並非完整外部內容的雙次 registry hash 比較。
- BanPackages 採來源守衛；沒有宣稱以未被產品讀取的測試環境變數完成動態驗證。
- 修正既有 fixture runner 編譯阻塞：`ClientPlayer` 改經 protected 唯讀 accessor 讀取 Player 的 fixed distance／attack range 容器，規則行為不變。

原生驗收命令（計劃下方的 `qsanguosha_selection_fixture` 是過期 target 名）：

```sh
cmake --build build/linux-gui-gcc --target qsanguosha_rules_fixture_runner \
  qsanguosha_rules_identity_tests qsanguosha_rules_content_manifest_tests -j 4
python3 tests/client_runtime/check-rules-bundle.py --native-only \
  --native-runner build/linux-gui-gcc/qsanguosha_rules_fixture_runner \
  --artifacts artifacts/declared-manifest-20260909/native
```

`qsanguosha_rules_manifest_native` 已註冊進 CTest，只 bootstrap／匯出身份，不跑對局。
G1/G2 包含追加 ID 保留、同集合換序、缺檔與未宣告內容、library hash、翻譯／AI 排除、
懸空及 AI symlink，以及載入期間改寫 library 後不可重新標記 VM 的反例。
驗收結果：Qt 6.11.1 原生 build 通過；focused CTest **8/8**（含原生 G1/G2、既有選牌 fixtures）；Web identity **9/9**、controller **11/11**、TypeScript 型別檢查及 WASM harness self-test **12/12** 通過。
驗收紀錄見 `artifacts/declared-manifest-20260909/`。

完整工作目錄仍可能有未宣告的 `lua/chat_config.lua`、`lua/lib/sqlite3.lua` 等非翻譯內容，
身份閘會繼續拒絕；P1 測試通過不表示已完成正式內容部署或 P2 的原生／WASM 對等驗收。

## Global Constraints

以下每條逐字取自 spec,適用於本計劃每一個 task:

- **註冊集合與順序決定 card ID**;身份必須同時覆蓋兩者。
- **未宣告的內容一律拒絕** —— 閘不可以移走,只可以由「固定名單」轉為「宣告驅動」。
- **code identity 必須完全相等**;content identity 執行期協商。
- **註冊集合 ≠ 對局啟用集合。** `ServerInfo.BanPackages` 是唯一停用機制;manifest **不可**用作停用機制。
- 核心五檔恆為:`lua/config.lua`、`lua/sanguosha.lua`、`lua/utilities.lua`、`lua/sgs_ex.lua`、`lua/lib/json.lua`。
- server-only AI 路徑恆為:`lua/ai/` 之下任何 `.lua`,加上 `lua/lib/middleclass.lua`。
- 不新增錯誤碼。可用的是 `rules_identity_required`、`rules_identity_invalid`、`rules_content_unsupported`、`rules_reload_required`、`rules_interaction_unsupported`、`rules_version_mismatch`。
- symlink 一律拒絕(現有行為,不得放寬)。
- **不執行完整對局驗收**;不做 pthread / 單機;不做效能與記憶體上限。
- `ctest -L fast` 有既有紅燈基線。任何「跑測試」步驟只判斷**本計劃新增或修改的測試名**,不可用整體綠作為通過條件。

---

## File Structure

| 檔案 | 責任 | 動作 |
|---|---|---|
| `src/core/rules-content-manifest.h` | manifest 資料型別與查詢函式宣告 | 新增 |
| `src/core/rules-content-manifest.cpp` | 分隔字串 → `ContentManifest` 的純資料解析與驗證 | 新增 |
| `tests/protocol/rules-content-manifest-test.cpp` | manifest 解析器單元測試(不需 Engine / Lua) | 新增 |
| `lua/config.lua` | 新增 `extension_names` 有序陣列 | 修改 |
| `lua/sanguosha.lua` | 擴展載入由 `GetFileNames` 改為照宣告順序 | 修改 |
| `src/core/engine.cpp` | 讀入 manifest;快照拆兩相 | 修改 |
| `src/core/engine.h` | 保存 manifest,提供 accessor | 修改 |
| `src/core/rules-bundle-exporter.h` / `.cpp` | 宣告式快照、掃描改寫、身份拆分、`code_id` | 修改 |
| `src/core/protocol/rules-bundle-identity.h` / `.cpp` | `code_id` 封印、`declared-v1` 相容判斷 | 修改 |
| `web/src/rules-identity.ts` | 前端接受 `declared-v1`、比對 `code_id` | 修改 |
| `tools/generate-extension-manifest.py` | 由現有 `extensions/` 生成初始宣告清單 | 新增 |
| `tests/client_runtime/check-extension-manifest.py` | 靜態一致性檢查(集合相等、順序不倒退) | 新增 |
| `tests/client_runtime/check-fixtures.py` | `stage_builtin_assets` 支援宣告的擴展 | 修改 |
| `tests/client_runtime/check-rules-bundle.py` | G1 註冊順序穩定性、G2 宣告閘 | 修改 |
| `tests/CMakeLists.txt` | 新測試目標與 ctest 註冊 | 修改 |

**設計決定 — manifest 為何是分隔字串而非巢狀表:** `GetValueFromLuaState`
(`src/core/util.cpp:206-250`)只處理扁平表:字串陣列,或者 string→string map。
巢狀表會對 table 呼叫 `lua_tostring` 而取到空值。擴展這個函式要動到它現有的每一個
呼叫端(`engine.cpp:1472`、`1493`、`1522`、`settings.cpp:277` 等),風險與本計劃無關。
`package_names` 本身已是逗號分隔字串,分隔字串是這個倉庫的既有慣例。

---

### Task 1: manifest 解析器(純資料)

**Files:**
- Create: `src/core/rules-content-manifest.h`
- Create: `src/core/rules-content-manifest.cpp`
- Test: `tests/protocol/rules-content-manifest-test.cpp`
- Modify: `tests/CMakeLists.txt`(在既有 `qsanguosha_rules_identity_tests` 區塊之後,約 `:769`)

**Interfaces:**
- Consumes: 無(本 task 是最底層)
- Produces:
  - `struct QSanRules::ManifestEntry { QString script; QStringList libs; QStringList lang; QStringList ai; };`
  - `struct QSanRules::ContentManifest { QList<ManifestEntry> entries; QString error; bool isValid() const; };`
  - `ContentManifest QSanRules::parseContentManifest(const QStringList &declared);`
  - `QStringList QSanRules::manifestScripts(const ContentManifest &);`
  - `QStringList QSanRules::manifestHashedFiles(const ContentManifest &);`
  - `QStringList QSanRules::manifestDeliveredFiles(const ContentManifest &);`
  - `QStringList QSanRules::manifestServerOnlyFiles(const ContentManifest &);`

**條目文法(實作與測試都以此為準):**

```
entry   := script ( ';' field )*
field   := key '=' path ( ',' path )*
key     := 'libs' | 'lang' | 'ai'
```

例:`extensions/sijyu.lua;lang=lang/zh_CN/Package/Sijyu.lua;ai=lua/ai/sijyu-ai.lua`

**驗證規則:**

| 規則 | 錯誤 |
|---|---|
| `script` 必須配 `^extensions/[A-Za-z0-9_.-]+\.lua$` | `script must be extensions/<name>.lua` |
| `libs` 必須配 `^lua/[A-Za-z0-9_./-]+\.lua$`,且不得在 `lua/ai/` 下,且不得是核心五檔 | `libs path is not extension library content` |
| `lang` 必須配 `^lang/[A-Za-z0-9_./-]+\.lua$` | `lang path must be under lang/` |
| `ai` 必須配 `^lua/ai/[A-Za-z0-9_./-]+\.lua$`,或恰為 `lua/lib/middleclass.lua` | `ai path is not server-only AI content` |
| 任何路徑不得含 `..`、`./`、前導 `/` 或反斜線 | `path traversal is not allowed` |
| 同一條目內同一個 key 重複出現 | `duplicate field key` |
| 未知 key | `unknown field key` |
| 整份 manifest 中任何路徑重複出現(不論角色) | `duplicate path` |

`error` 格式固定為 `"entry <index>: <reason>"`,`<index>` 由 0 起。空清單有效。

- [ ] **Step 1: 寫失敗測試**

Create `tests/protocol/rules-content-manifest-test.cpp`:

```cpp
#include "rules-content-manifest.h"

#include <QTextStream>

namespace {
int caseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++caseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

bool emptyManifestIsValid()
{
    const auto manifest = QSanRules::parseContentManifest({});
    return expect(manifest.isValid(), QStringLiteral("empty manifest is valid"))
        && expect(manifest.entries.isEmpty(), QStringLiteral("empty manifest has no entries"))
        && expect(QSanRules::manifestScripts(manifest).isEmpty(),
                  QStringLiteral("empty manifest yields no scripts"));
}

bool declarationOrderIsPreserved()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/zabing.lua"),
        QStringLiteral("extensions/AIgeneral.lua"),
        QStringLiteral("extensions/sijyu.lua")});
    const QStringList expected{QStringLiteral("extensions/zabing.lua"),
                               QStringLiteral("extensions/AIgeneral.lua"),
                               QStringLiteral("extensions/sijyu.lua")};
    return expect(manifest.isValid(), QStringLiteral("ordered manifest is valid"))
        && expect(QSanRules::manifestScripts(manifest) == expected,
                  QStringLiteral("declaration order is preserved verbatim"));
}

bool satelliteRolesAreSeparated()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/LuaOldEnemy.lua;libs=lua/luaoldenemy_lib.lua"),
        QStringLiteral("extensions/sijyu.lua;lang=lang/zh_CN/Package/Sijyu.lua;ai=lua/ai/sijyu-ai.lua")});
    const QStringList hashed{QStringLiteral("extensions/LuaOldEnemy.lua"),
                             QStringLiteral("lua/luaoldenemy_lib.lua"),
                             QStringLiteral("extensions/sijyu.lua")};
    const QStringList delivered{QStringLiteral("extensions/LuaOldEnemy.lua"),
                                QStringLiteral("lua/luaoldenemy_lib.lua"),
                                QStringLiteral("extensions/sijyu.lua"),
                                QStringLiteral("lang/zh_CN/Package/Sijyu.lua")};
    const QStringList serverOnly{QStringLiteral("lua/ai/sijyu-ai.lua")};
    return expect(manifest.isValid(), QStringLiteral("satellite manifest is valid"))
        && expect(QSanRules::manifestHashedFiles(manifest) == hashed,
                  QStringLiteral("hashed closure excludes lang and ai"))
        && expect(QSanRules::manifestDeliveredFiles(manifest) == delivered,
                  QStringLiteral("delivered closure includes lang but excludes ai"))
        && expect(QSanRules::manifestServerOnlyFiles(manifest) == serverOnly,
                  QStringLiteral("server-only closure is the ai role"));
}

bool middleclassIsAcceptedAsAi()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/sijyu.lua;ai=lua/lib/middleclass.lua")});
    return expect(manifest.isValid(), QStringLiteral("middleclass is a valid ai path"));
}

bool invalidManifestsAreRejected()
{
    struct Case { const char *entry; const char *reason; };
    static const Case cases[] = {
        {"lua/sneaky.lua", "script must be extensions/<name>.lua"},
        {"extensions/a.lua;libs=lua/config.lua", "libs path is not extension library content"},
        {"extensions/a.lua;libs=lua/ai/smart-ai.lua", "libs path is not extension library content"},
        {"extensions/a.lua;lang=lua/utilities.lua", "lang path must be under lang/"},
        {"extensions/a.lua;ai=lua/lib/sqlite3.lua", "ai path is not server-only AI content"},
        {"extensions/../secret.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua/../etc/x.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua/x.lua;libs=lua/y.lua", "duplicate field key"},
        {"extensions/a.lua;packages=Standard", "unknown field key"}};
    for (const auto &one : cases) {
        const auto manifest = QSanRules::parseContentManifest({QString::fromLatin1(one.entry)});
        const QString expected = QStringLiteral("entry 0: ") + QString::fromLatin1(one.reason);
        if (!expect(manifest.error == expected,
                    QStringLiteral("rejects %1 with %2").arg(QString::fromLatin1(one.entry), expected)))
            return false;
    }
    const auto duplicated = QSanRules::parseContentManifest({
        QStringLiteral("extensions/a.lua"), QStringLiteral("extensions/a.lua")});
    return expect(duplicated.error == QStringLiteral("entry 1: duplicate path"),
                  QStringLiteral("rejects a path declared twice"));
}
}

int main()
{
    if (!emptyManifestIsValid() || !declarationOrderIsPreserved() || !satelliteRolesAreSeparated()
        || !middleclassIsAcceptedAsAi() || !invalidManifestsAreRejected()) {
        return 1;
    }
    QTextStream(stdout) << "rules content manifest: " << caseCount << " checks passed\n";
    return 0;
}
```

Add to `tests/CMakeLists.txt`,緊接既有 `qsanguosha_rules_bundle_harness` 註冊之後:

```cmake
# Pure-data manifest contract: no Engine, no Lua state, no filesystem.
add_executable(qsanguosha_rules_content_manifest_tests
    protocol/rules-content-manifest-test.cpp
    ${QSAN_TEST_ROOT}/src/core/rules-content-manifest.cpp)
target_include_directories(qsanguosha_rules_content_manifest_tests PRIVATE ${QSAN_TEST_ROOT}/src/core)
target_link_libraries(qsanguosha_rules_content_manifest_tests PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(qsanguosha_rules_content_manifest_tests PRIVATE /utf-8)
endif()
add_test(NAME qsanguosha_rules_content_manifest COMMAND qsanguosha_rules_content_manifest_tests)
set_tests_properties(qsanguosha_rules_content_manifest PROPERTIES LABELS "protocol;fast" TIMEOUT 15)
```

- [ ] **Step 2: 跑測試確認失敗**

```sh
cmake --build build/native --target qsanguosha_rules_content_manifest_tests
```

Expected: 編譯失敗,`rules-content-manifest.h: No such file or directory`。

- [ ] **Step 3: 寫最小實作**

Create `src/core/rules-content-manifest.h`:

```cpp
#ifndef QSAN_RULES_CONTENT_MANIFEST_H
#define QSAN_RULES_CONTENT_MANIFEST_H

#include <QList>
#include <QString>
#include <QStringList>

namespace QSanRules {
// One declared extension plus its satellite files. Only `script` has ordering
// semantics; the other roles are sets that the parser normalises by sorting.
struct ManifestEntry {
    QString script;
    QStringList libs;
    QStringList lang;
    QStringList ai;
};

// `error` is empty exactly when the declaration parsed and validated.
struct ContentManifest {
    QList<ManifestEntry> entries;
    QString error;
    bool isValid() const { return error.isEmpty(); }
};

ContentManifest parseContentManifest(const QStringList &declared);

// Registration order. Card IDs depend on this list, so it is never sorted.
QStringList manifestScripts(const ContentManifest &manifest);
// Executable rules content: scripts and libs, in declaration order.
QStringList manifestHashedFiles(const ContentManifest &manifest);
// Everything a browser client must fetch: hashed files plus lang.
QStringList manifestDeliveredFiles(const ContentManifest &manifest);
// Server-owned AI policy: never hashed, never delivered by default.
QStringList manifestServerOnlyFiles(const ContentManifest &manifest);
}
#endif
```

Create `src/core/rules-content-manifest.cpp`:

```cpp
#include "rules-content-manifest.h"

#include <QRegularExpression>
#include <QSet>

namespace QSanRules {
namespace {
const QStringList &coreFiles()
{
    static const QStringList files{QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
        QStringLiteral("lua/utilities.lua"), QStringLiteral("lua/sgs_ex.lua"),
        QStringLiteral("lua/lib/json.lua")};
    return files;
}

bool traverses(const QString &path)
{
    if (path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')))
        return true;
    const auto segments = path.split(QLatin1Char('/'));
    for (const auto &segment : segments)
        if (segment == QLatin1String("..") || segment == QLatin1String(".") || segment.isEmpty())
            return true;
    return false;
}

bool matches(const QString &path, const QString &pattern)
{
    static QHash<QString, QRegularExpression> cache;
    if (!cache.contains(pattern))
        cache.insert(pattern, QRegularExpression(pattern));
    return cache.value(pattern).match(path).hasMatch();
}

QString validatePath(const QString &role, const QString &path)
{
    if (traverses(path))
        return QStringLiteral("path traversal is not allowed");
    if (role == QLatin1String("script")) {
        if (!matches(path, QStringLiteral("^extensions/[A-Za-z0-9_.-]+\\.lua$")))
            return QStringLiteral("script must be extensions/<name>.lua");
    } else if (role == QLatin1String("libs")) {
        if (!matches(path, QStringLiteral("^lua/[A-Za-z0-9_./-]+\\.lua$"))
            || path.startsWith(QLatin1String("lua/ai/")) || coreFiles().contains(path))
            return QStringLiteral("libs path is not extension library content");
    } else if (role == QLatin1String("lang")) {
        if (!matches(path, QStringLiteral("^lang/[A-Za-z0-9_./-]+\\.lua$")))
            return QStringLiteral("lang path must be under lang/");
    } else if (role == QLatin1String("ai")) {
        if (!matches(path, QStringLiteral("^lua/ai/[A-Za-z0-9_./-]+\\.lua$"))
            && path != QLatin1String("lua/lib/middleclass.lua"))
            return QStringLiteral("ai path is not server-only AI content");
    }
    return {};
}
}

ContentManifest parseContentManifest(const QStringList &declared)
{
    ContentManifest manifest;
    QSet<QString> seen;
    for (int index = 0; index < declared.size(); ++index) {
        const auto fail = [&manifest, index](const QString &reason) {
            manifest.error = QStringLiteral("entry %1: %2").arg(index).arg(reason);
            return manifest;
        };
        const QStringList fields = declared.at(index).split(QLatin1Char(';'));
        ManifestEntry entry;
        entry.script = fields.value(0).trimmed();
        QString reason = validatePath(QStringLiteral("script"), entry.script);
        if (!reason.isEmpty())
            return fail(reason);
        if (seen.contains(entry.script))
            return fail(QStringLiteral("duplicate path"));
        seen.insert(entry.script);
        QSet<QString> keys;
        for (int field = 1; field < fields.size(); ++field) {
            const QString text = fields.at(field).trimmed();
            const int equals = text.indexOf(QLatin1Char('='));
            const QString key = equals < 0 ? text : text.left(equals);
            if (key != QLatin1String("libs") && key != QLatin1String("lang")
                && key != QLatin1String("ai"))
                return fail(QStringLiteral("unknown field key"));
            if (keys.contains(key))
                return fail(QStringLiteral("duplicate field key"));
            keys.insert(key);
            QStringList *target = key == QLatin1String("libs") ? &entry.libs
                : key == QLatin1String("lang") ? &entry.lang : &entry.ai;
            for (const QString &raw : text.mid(equals + 1).split(QLatin1Char(','))) {
                const QString path = raw.trimmed();
                reason = validatePath(key, path);
                if (!reason.isEmpty())
                    return fail(reason);
                if (seen.contains(path))
                    return fail(QStringLiteral("duplicate path"));
                seen.insert(path);
                target->append(path);
            }
            target->sort();
        }
        manifest.entries.append(entry);
    }
    return manifest;
}

QStringList manifestScripts(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.script;
    return result;
}

QStringList manifestHashedFiles(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.script << entry.libs;
    return result;
}

QStringList manifestDeliveredFiles(const ContentManifest &manifest)
{
    QStringList result = manifestHashedFiles(manifest);
    for (const auto &entry : manifest.entries)
        result << entry.lang;
    return result;
}

QStringList manifestServerOnlyFiles(const ContentManifest &manifest)
{
    QStringList result;
    for (const auto &entry : manifest.entries)
        result << entry.ai;
    return result;
}
}
```

include 區完整為:`#include <QHash>`、`#include <QRegularExpression>`、`#include <QSet>`。

- [ ] **Step 4: 跑測試確認通過**

```sh
cmake --build build/native --target qsanguosha_rules_content_manifest_tests
ctest --test-dir build/native -R qsanguosha_rules_content_manifest --output-on-failure
```

Expected: PASS,stdout 印 `rules content manifest: <n> checks passed`。

- [ ] **Step 5: Commit**

```bash
git add src/core/rules-content-manifest.h src/core/rules-content-manifest.cpp \
        tests/protocol/rules-content-manifest-test.cpp tests/CMakeLists.txt
git commit -m "feat(core): add declared extension manifest parser"
```

---

### Task 2: 生成初始宣告清單並靜態把關

**Files:**
- Create: `tools/generate-extension-manifest.py`
- Create: `tests/client_runtime/check-extension-manifest.py`
- Modify: `lua/config.lua`(在 `package_names` 區塊之後)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 的條目文法(本 task 只產生與檢查文字,不 link C++)
- Produces: `lua/config.lua` 內的 `extension_names` 有序陣列;
  `check-extension-manifest.py` 的 `--self-test` 與預設檢查模式

**遷移不變式:** 生成的順序必須等於今日 `QDir::entryList(QDir::Files)` 的
`Name | IgnoreCase` 排序結果,使現有 desktop 部署的 card ID 不變。

- [ ] **Step 1: 寫失敗測試**

Create `tests/client_runtime/check-extension-manifest.py`:

```python
#!/usr/bin/env python3
"""Static gate: lua/config.lua must declare exactly the extensions on disk, in order."""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


def declared_entries(config_text: str) -> list[str]:
    block = re.search(r"\n\textension_names = \{\n(.*?)\n\t\},\n", config_text, re.S)
    if not block:
        raise AssertionError("lua/config.lua has no extension_names block")
    entries = re.findall(r'^\t\t"([^"]+)",$', block.group(1), re.M)
    if len(entries) != len([line for line in block.group(1).splitlines() if line.strip()]):
        raise AssertionError("extension_names contains a line that is not a quoted entry")
    return entries


def on_disk(root: Path) -> list[str]:
    names = [path.name for path in (root / "extensions").iterdir()
             if path.is_file() and path.name.endswith(".lua")]
    lowered = [name.lower() for name in names]
    if len(set(lowered)) != len(lowered):
        raise AssertionError("extensions/ has names that collide case-insensitively")
    return ["extensions/" + name for name in sorted(names, key=str.lower)]


def check(root: Path) -> None:
    declared = declared_entries((root / "lua/config.lua").read_text(encoding="utf-8"))
    scripts = [entry.split(";")[0] for entry in declared]
    disk = on_disk(root)
    missing = sorted(set(disk) - set(scripts))
    extra = sorted(set(scripts) - set(disk))
    if missing:
        raise AssertionError("extensions on disk are not declared: " + ", ".join(missing))
    if extra:
        raise AssertionError("declared extensions are missing from disk: " + ", ".join(extra))
    if scripts != disk:
        raise AssertionError("declared order does not match the migrated filename order")


class SelfTest(unittest.TestCase):
    def test_declared_entries_parses_satellites(self):
        text = '\n\textension_names = {\n\t\t"extensions/a.lua",\n\t\t"extensions/b.lua;ai=lua/ai/b-ai.lua",\n\t},\n'
        self.assertEqual(declared_entries(text),
                         ["extensions/a.lua", "extensions/b.lua;ai=lua/ai/b-ai.lua"])

    def test_missing_block_is_an_error(self):
        with self.assertRaisesRegex(AssertionError, "no extension_names block"):
            declared_entries("config = {}\n")

    def test_case_insensitive_collision_is_an_error(self, ):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/Ab.lua").write_text("")
            (root / "extensions/aB.lua").write_text("")
            with self.assertRaisesRegex(AssertionError, "collide case-insensitively"):
                on_disk(root)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.main(argv=[sys.argv[0]], exit=False).result.wasSuccessful() else 1
    check(args.root)
    print("extension manifest: declaration matches extensions/ exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Add to `tests/CMakeLists.txt`,緊接 Task 1 加入的區塊之後:

```cmake
add_test(NAME qsanguosha_extension_manifest
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-extension-manifest.py"
            --root "${QSAN_TEST_ROOT}")
set_tests_properties(qsanguosha_extension_manifest PROPERTIES LABELS "client;protocol;fast" TIMEOUT 15)
add_test(NAME qsanguosha_extension_manifest_selftest
    COMMAND "${Python3_EXECUTABLE}" "${QSAN_TEST_ROOT}/tests/client_runtime/check-extension-manifest.py"
            --self-test)
set_tests_properties(qsanguosha_extension_manifest_selftest PROPERTIES LABELS "client;protocol;fast" TIMEOUT 15)
```

- [ ] **Step 2: 跑測試確認失敗**

```sh
python3 tests/client_runtime/check-extension-manifest.py
```

Expected: `AssertionError: lua/config.lua has no extension_names block`

- [ ] **Step 3: 寫生成器並產出清單**

Create `tools/generate-extension-manifest.py`:

```python
#!/usr/bin/env python3
"""Emit the initial extension_names declaration from the current extensions/ tree.

The order reproduces QDir::entryList(QDir::Files) with the default
Name | IgnoreCase sorting, so migrating to a declared manifest leaves every
existing card ID untouched.
"""
from __future__ import annotations
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def satellites(root: Path, stem: str) -> str:
    fields = []
    ai = root / "lua/ai" / (stem + "-ai.lua")
    if ai.is_file():
        fields.append("ai=lua/ai/" + ai.name)
    return "".join(";" + field for field in fields)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
    names = sorted((path.name for path in (root / "extensions").iterdir()
                    if path.is_file() and path.name.endswith(".lua")), key=str.lower)
    print("\textension_names = {")
    for name in names:
        print('\t\t"extensions/%s%s",' % (name, satellites(root, name[:-4])))
    print("\t},")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

生成並貼進 `lua/config.lua`,緊接 `package_names = { ... },` 區塊之後:

```sh
python3 tools/generate-extension-manifest.py > /tmp/extension_names.lua
```

在 `lua/config.lua` 的 `package_names` 結尾 `\t},` 那一行之後插入產出內容,並在上方加註解:

```lua
	-- 擴展載入順序:顯式宣告,取代 sgs.GetFileNames 的隱式檔名排序。
	-- 順序決定擴展牌的實體 ID,插入位置會推移其後所有牌,只可在尾端追加。
	-- 停用擴展請用 ServerInfo.BanPackages,不要從這裡刪除條目。
	extension_names = {
		-- 生成器輸出貼在此處,每行一個條目
	},
```

`libs` 與 `lang` 欄位由人手補上:目前僅 `extensions/LuaOldEnemy.lua` 需要
`;libs=lua/luaoldenemy_lib.lua`。

- [ ] **Step 4: 跑測試確認通過**

```sh
python3 tests/client_runtime/check-extension-manifest.py
python3 tests/client_runtime/check-extension-manifest.py --self-test
```

Expected: 第一條印 `extension manifest: declaration matches extensions/ exactly`;第二條 `OK`。

**這一步就是 spec G1 最後一行(「生成初始 `extension_names` 前後 `card_registry_hash`
完全相同」)的實作。** 遷移之後沒有「之前」可以在執行期重算,所以等價性在來源層證明:
宣告順序逐字等於 `QDir::entryList(QDir::Files)` 在 `Name | IgnoreCase` 之下的結果,
而註冊順序只由這個清單決定,故 card ID 不可能移動。`check-extension-manifest.py`
永久守住這個等式,任何人手改動順序都會令這個 gate 變紅。

- [ ] **Step 5: Commit**

```bash
git add tools/generate-extension-manifest.py tests/client_runtime/check-extension-manifest.py \
        lua/config.lua tests/CMakeLists.txt
git commit -m "feat(lua): declare the extension load order in config.lua"
```

---

### Task 3: 照宣告載入,並證明 card ID 不變

**Files:**
- Modify: `lua/sanguosha.lua:19-40`
- Modify: `src/core/engine.h:258-259`、`src/core/engine.cpp:336-340`
- Modify: `tests/client_runtime/check-fixtures.py:20-26`
- Modify: `tests/client_runtime/check-rules-bundle.py`(新增 G1)

**Interfaces:**
- Consumes: `QSanRules::parseContentManifest`、`manifestScripts`(Task 1);
  `lua/config.lua` 的 `extension_names`(Task 2)
- Produces:
  - `const QSanRules::ContentManifest &Engine::rulesContentManifest() const`
  - `stage_builtin_assets(source, destination, extensions=())` — Python,新增第三個參數
  - `check-rules-bundle.py` 中的 `registration_stability(args, assets, scratch, env, baseline)`

- [ ] **Step 1: 寫失敗測試**

在 `tests/client_runtime/check-rules-bundle.py` 的 `negative_exports` 之後加入 G1:

```python
def declare(assets, entries):
    """Rewrite the staged config.lua extension_names block in place."""
    path = assets / "lua/config.lua"
    text = path.read_text(encoding="utf-8")
    body = "".join('\t\t"%s",\n' % entry for entry in entries)
    replaced = re.sub(r"\n\textension_names = \{\n.*?\n\t\},\n",
                      "\n\textension_names = {\n%s\t},\n" % body, text, count=1, flags=re.S)
    if replaced == text:
        raise AssertionError("staged config.lua has no extension_names block to rewrite")
    path.write_text(replaced, encoding="utf-8")


def registration_stability(args, assets, scratch, env, staged_entries):
    """G1: appending an extension must not move any existing card ID."""
    baseline = export_native(args, assets, scratch, env, "native-manifest-base")
    identifiers = {card["id"]: card["object_name"] for card in baseline["registry"]}

    (assets / "extensions/zz_probe.lua").write_text(
        "local probe = sgs.Package('ZzProbe')\nreturn probe\n", encoding="utf-8")
    try:
        declare(assets, staged_entries + ["extensions/zz_probe.lua"])
        appended = export_native(args, assets, scratch, env, "native-manifest-appended")
        for card in appended["registry"]:
            if card["id"] in identifiers and identifiers[card["id"]] != card["object_name"]:
                raise AssertionError("appending an extension moved card id %d" % card["id"])

        declare(assets, ["extensions/zz_probe.lua"] + staged_entries)
        prepended = export_native(args, assets, scratch, env, "native-manifest-prepended")
        if (prepended["rules_bundle"]["card_registry_hash"]
                == appended["rules_bundle"]["card_registry_hash"]):
            raise AssertionError("manifest order change was not detected")
    finally:
        (assets / "extensions/zz_probe.lua").unlink()
        declare(assets, staged_entries)
    return baseline
```

在 `check()` 內 `native.stage_builtin_assets(ROOT, assets)` 之後,把 staging 改為帶兩個真實擴展,並在既有 baseline 匯出之後呼叫 `registration_stability`。

- [ ] **Step 2: 跑測試確認失敗**

```sh
python3 tests/client_runtime/check-rules-bundle.py \
  --native-runner build/native/qsanguosha_selection_fixture \
  --artifacts artifacts/rules-bundle
```

Expected: FAIL —— `staged config.lua has no extension_names block to rewrite`
之前會先撞到 staging 未複製 `extensions/`,兩者都算預期失敗。

- [ ] **Step 3: 寫實作**

`tests/client_runtime/check-fixtures.py:20-26` 改為:

```python
def stage_builtin_assets(source: Path, destination: Path, extensions: tuple[str, ...] = ()) -> None:
    """Copy the explicit bootstrap closure plus any declared extensions."""
    for relative in ("lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
                     "lua/sgs_ex.lua", "lua/lib/json.lua", *extensions):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, target)
```

`lua/sanguosha.lua:19-40` 的擴展載入迴圈改為:

```lua
local package_names = {}
for _, entry in ipairs(sgs.GetConfigList("extension_names")) do
	local script = entry:match("^[^;]+")
	local module_name = script:match("^extensions/(.+)%.lua$")
	if not module_name then
		error(("declared extension %s is not extensions/<name>.lua"):format(entry))
	end
	local loaded = require("extensions." .. module_name)
	if sgs.GetConfig("DisableLua", false) then continue end
	if type(loaded) == "table" and loaded.hidden ~= true then
		if #loaded > 0 then
			for _, extension in ipairs(loaded) do
				if extension:inherits("Package") then
					table.insert(package_names, extension:objectName())
					sgs.Sanguosha:addPackage(extension)
				end
			end
		else
			table.insert(package_names, loaded.extension:objectName())
			sgs.Sanguosha:addPackage(loaded.extension)
		end
	elseif type(loaded) == "userdata" and loaded:inherits("Package") then
		table.insert(package_names, loaded:objectName())
		sgs.Sanguosha:addPackage(loaded)
	end
end
```

`require` 對不存在的模組本身就會 `error`,滿足「宣告了、檔案不存在 → 硬失敗」。

`sgs.GetConfigList` 是新的 native 綁定。在 `swig/native.i` 的 `%native` 宣告區加入
`%native(GetConfigList) int GetConfigList(lua_State *lua);`,並在 `%{ ... %}` 實作區加入:

```c
static int GetConfigList(lua_State *lua)
{
	const char *key = luaL_checkstring(lua, 1);
	const QStringList values = Sanguosha->rulesDeclaredList(key);
	lua_createtable(lua, values.length(), 0);
	for (int i = 0; i < values.length(); i++) {
		lua_pushstring(lua, values.at(i).toUtf8().constData());
		lua_rawseti(lua, -2, i + 1);
	}
	return 1;
}
```

`src/core/engine.h`,在 `:106` 的 `rulesPackageOrder()` 旁邊加入:

```cpp
    const QSanRules::ContentManifest &rulesContentManifest() const { return m_rulesContentManifest; }
    QStringList rulesDeclaredList(const QString &key) const;
```

`:258-259` 附近的成員加入:

```cpp
    QSanRules::ContentManifest m_rulesContentManifest;
```

並在檔頭加 `#include "rules-content-manifest.h"`。

`src/core/engine.cpp`,在 `:336`(讀 `package_names` 之前)插入:

```cpp
    m_rulesContentManifest = QSanRules::parseContentManifest(
        GetConfigFromLuaState(bootstrapLua, "extension_names").toStringList());
    if (!m_rulesContentManifest.isValid()) {
        qCritical() << "invalid extension_names declaration:" << m_rulesContentManifest.error;
        exit(1);
    }
```

並在 `src/core/engine.cpp` 中 `Engine::addPackage(const QString &name)`(`:166`)之前加入:

```cpp
QStringList Engine::rulesDeclaredList(const QString &key) const
{
    if (key == QLatin1String("extension_names"))
        return QSanRules::manifestScripts(m_rulesContentManifest);
    return {};
}
```

`src/core/rules-content-manifest.cpp` 需加入 `qsanguosha_engine` 的來源清單
(`CMakeLists.txt:284` 區塊,緊接 `src/core/runtime-paths.cpp` 之後)。

- [ ] **Step 4: 跑測試確認通過**

```sh
cmake --build build/native --target qsanguosha_selection_fixture
python3 tests/client_runtime/check-rules-bundle.py \
  --native-runner build/native/qsanguosha_selection_fixture \
  --artifacts artifacts/rules-bundle
```

Expected: PASS。特別確認 log 中 `native-manifest-appended` 與 `native-manifest-base`
的 registry 對照沒有拋出 `moved card id`。

- [ ] **Step 5: Commit**

```bash
git add lua/sanguosha.lua swig/native.i src/core/engine.h src/core/engine.cpp \
        CMakeLists.txt tests/client_runtime/check-fixtures.py \
        tests/client_runtime/check-rules-bundle.py
git commit -m "feat(engine): load extensions in declared manifest order"
```

**SWIG 提醒:** `swig/native.i` 是手寫的。改動後必須 `git diff` 檢查沒有意外刪除
其他 `%native` 宣告 —— 刪一行會靜靜拆掉一個 Lua 綁定。

---

### Task 4: 宣告式快照與「未宣告即拒絕」

**Files:**
- Modify: `src/core/rules-bundle-exporter.h`
- Modify: `src/core/rules-bundle-exporter.cpp:17-64`
- Modify: `src/core/engine.cpp:312`(相一)與 `:336` 之後(相二)
- Modify: `tests/client_runtime/check-rules-bundle.py`(新增 G2)

**Interfaces:**
- Consumes: `Engine::rulesContentManifest()`(Task 3)
- Produces:
  - `QJsonObject QSanRules::coreLuaSnapshot();`
  - `QJsonObject QSanRules::declaredLuaSnapshot(const ContentManifest &manifest);`
  - `bool QSanRules::contentScanIsDeclared(const ContentManifest &manifest);`
  - `builtinLuaSnapshot()` 移除,呼叫端改用上列三者

- [ ] **Step 1: 寫失敗測試**

在 `check-rules-bundle.py` 的 `negative_exports` 內加入四個新案例(沿用既有
`variants` 迴圈之外,獨立一段):

```python
def declaration_gate(args, assets, scratch, env, baseline, staged_entries):
    """G2: undeclared content is rejected; presentation and AI stay out of the seal."""
    stray = assets / "extensions/undeclared.lua"
    stray.write_text("return nil\n", encoding="utf-8")
    try:
        export_native(args, assets, scratch, env, "native-undeclared", unsupported=True)
    finally:
        stray.unlink()

    declared = assets / staged_entries[0]
    original = declared.read_bytes()
    declared.unlink()
    try:
        export_native(args, assets, scratch, env, "native-missing-declared", unsupported=True)
    finally:
        declared.write_bytes(original)

    translation = assets / "lang/zh_CN/Common.lua"
    translation.parent.mkdir(parents=True, exist_ok=True)
    translation.write_text("return {}\n", encoding="utf-8")
    try:
        export_native(args, assets, scratch, env, "native-undeclared-lang", unsupported=True)
        declare(assets, [staged_entries[0] + ";lang=lang/zh_CN/Common.lua", *staged_entries[1:]])
        with_lang = export_native(args, assets, scratch, env, "native-declared-lang")
        if with_lang["rules_bundle"]["lua_hash"] != baseline["rules_bundle"]["lua_hash"]:
            raise AssertionError("declared lang content leaked into lua_hash")
        translation.write_text("return { greeting = 'x' }\n", encoding="utf-8")
        edited = export_native(args, assets, scratch, env, "native-declared-lang-edited")
        if edited["rules_bundle"]["bundle_id"] != with_lang["rules_bundle"]["bundle_id"]:
            raise AssertionError("editing a translation invalidated the bundle")
    finally:
        translation.unlink()
        declare(assets, staged_entries)
```

並在同一檔案的 `unittest.TestCase` 區塊加入來源守衛,實作 spec G1 第三行
(「只改 `BanPackages` → `card_registry_hash` 不變」):

```python
    def test_ban_packages_is_not_an_identity_input(self):
        # Disabling a package must mask at setup, never change the sealed registry.
        for relative in ("src/core/rules-bundle-exporter.cpp", "src/core/rules-content-manifest.cpp"):
            self.assertNotIn("BanPackages", (ROOT / relative).read_text(encoding="utf-8"),
                             relative + " must not consult the per-game ban list")
```

執行期無法直接測這一行:`exportRegistry` 逐個 `getEngineCard(id)` 走完整個
registry,從不查 `BanPackages`,所以不變式是結構性的。來源守衛防止日後有人把
停用清單接進身份計算。

- [ ] **Step 2: 跑測試確認失敗**

```sh
python3 tests/client_runtime/check-rules-bundle.py \
  --native-runner build/native/qsanguosha_selection_fixture \
  --artifacts artifacts/rules-bundle
```

Expected: FAIL —— 目前掃描見到任何 `extensions/*.lua` 就拒絕,所以
`native-declared-lang` 這條會以 `rules_content_unsupported` 失敗而非成功匯出。

- [ ] **Step 3: 寫實作**

`src/core/rules-bundle-exporter.h` 改為:

```cpp
#ifndef QSAN_RULES_BUNDLE_EXPORTER_H
#define QSAN_RULES_BUNDLE_EXPORTER_H
#include "rules-content-manifest.h"
#include <QJsonObject>
class Engine;
namespace QSanRules {
// Phase one: the fixed core closure, hashed before any Lua executes.
QJsonObject coreLuaSnapshot();
// Phase two: declared scripts and libs, hashed in declaration order, taken
// after config.lua has run but before any extension executes.
QJsonObject declaredLuaSnapshot(const ContentManifest &manifest);
// Every .lua on disk must be core, declared, or a server-only AI path.
bool contentScanIsDeclared(const ContentManifest &manifest);
QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua);
QJsonObject exportRegistry(const Engine &engine);
}
#endif
```

`src/core/rules-bundle-exporter.cpp:17-64` 的 `builtinLuaSnapshot()` 拆為三個函式。
掃描迴圈的判斷式由「不在五檔白名單就拒絕」改為:

```cpp
            const bool serverOnlyAi = path.startsWith(QLatin1String("lua/ai/"))
                || path == QLatin1String("lua/lib/middleclass.lua");
            if (it.fileInfo().isFile() && path.endsWith(QLatin1String(".lua"), Qt::CaseInsensitive)
                && !coreFiles.contains(path) && !declaredPaths.contains(path) && !serverOnlyAi)
                return false;
```

其中 `declaredPaths` 是 `manifestDeliveredFiles(manifest)` 轉成的 `QSet<QString>`。
同時新增反向檢查:`manifestDeliveredFiles` 中任何路徑不存在於磁碟即回傳 `false`。

`declaredLuaSnapshot` 只對 `manifestHashedFiles(manifest)` 逐檔取 SHA-256,
**不含** `lang`,理由見 spec §3.6。

`src/core/engine.cpp:312` 改為 `m_rulesLuaSnapshot = QSanRules::coreLuaSnapshot();`;
在 Task 3 插入的 manifest 讀取之後,緊接加入相二:

```cpp
    if (!QSanRules::contentScanIsDeclared(m_rulesContentManifest)) {
        m_rulesLuaSnapshot = {};
    } else {
        const QJsonObject declared = QSanRules::declaredLuaSnapshot(m_rulesContentManifest);
        if (declared.isEmpty() && !m_rulesContentManifest.entries.isEmpty())
            m_rulesLuaSnapshot = {};
        else
            for (auto it = declared.begin(); it != declared.end(); ++it)
                m_rulesLuaSnapshot.insert(it.key(), it.value());
    }
```

`exportIdentity` 中原本的
`if (loadedLua.isEmpty() || loadedLua != builtinLuaSnapshot())` 改為與
`coreLuaSnapshot()` 合併 `declaredLuaSnapshot(engine.rulesContentManifest())`
的重算結果比較,並保留 `contentScanIsDeclared` 前置檢查。

- [ ] **Step 4: 跑測試確認通過**

```sh
cmake --build build/native --target qsanguosha_selection_fixture
python3 tests/client_runtime/check-rules-bundle.py \
  --native-runner build/native/qsanguosha_selection_fixture \
  --artifacts artifacts/rules-bundle
ctest --test-dir build/native -R "qsanguosha_rules_content_manifest|qsanguosha_extension_manifest" \
  --output-on-failure
```

Expected: 全部 PASS。

- [ ] **Step 5: Commit**

```bash
git add src/core/rules-bundle-exporter.h src/core/rules-bundle-exporter.cpp \
        src/core/engine.cpp tests/client_runtime/check-rules-bundle.py
git commit -m "feat(core): reject undeclared rules content instead of a fixed allowlist"
```

---

### Task 5: 身份拆 code / content 並加 `code_id`

**Files:**
- Modify: `src/core/protocol/rules-bundle-identity.h`
- Modify: `src/core/protocol/rules-bundle-identity.cpp`
- Modify: `src/core/rules-bundle-exporter.cpp:104-113`
- Modify: `web/src/rules-identity.ts:45,51-60`
- Test: `tests/protocol/rules-bundle-identity-test.cpp`

**Interfaces:**
- Consumes: `QSanRules::seal`、`digest`、`validate`、`compatibilityError`(既有)
- Produces:
  - `QJsonObject QSanRules::sealCode(QJsonObject identity);` —— 在 `bundle_id` 之外
    另加 `code_id`,只封 `protocol_version`、`bridge_schema`、`cpp_hash`、
    `bindings_abi`、`interaction_schemas` 五個欄位
  - `compatibilityError` 在 `bundle_id` 比較之前先比 `code_id`
  - `content_profile` 的接受值由 `builtin-v1` 改為 `declared-v1`

- [ ] **Step 1: 寫失敗測試**

在 `tests/protocol/rules-bundle-identity-test.cpp` 的 `matchingAndMismatchingBundles()`
之後加入:

```cpp
bool codeIdentityIsSeparable()
{
    QJsonObject server = identity();
    QJsonObject client = identity();
    if (!expect(server.value(QStringLiteral("code_id")).toString().size() == 64,
                QStringLiteral("sealed identity carries a code_id")))
        return false;

    // Content-only divergence must not look like a code mismatch.
    client.insert(QStringLiteral("lua_hash"), luaHash(QStringLiteral("other")));
    client = QSanRules::seal(client);
    if (!expect(client.value(QStringLiteral("code_id")) == server.value(QStringLiteral("code_id")),
                QStringLiteral("content change leaves code_id untouched"))
        || !expect(QSanRules::compatibilityError(server, client, true)
                       == QStringLiteral("rules_version_mismatch"),
                   QStringLiteral("content divergence reports a version mismatch")))
        return false;

    // Code divergence must be reported before any content comparison.
    QJsonObject other = identity();
    other.insert(QStringLiteral("bindings_abi"), QString(64, QLatin1Char('a')));
    other = QSanRules::seal(other);
    return expect(other.value(QStringLiteral("code_id")) != server.value(QStringLiteral("code_id")),
                  QStringLiteral("bindings change moves code_id"))
        && expect(QSanRules::compatibilityError(server, other, true)
                      == QStringLiteral("rules_reload_required"),
                  QStringLiteral("code divergence is reported without content comparison"));
}
```

並把測試檔內 `identity()` 的 `content_profile` 由 `builtin-v1` 改為 `declared-v1`,
在 `main()` 的條件中加入 `|| !codeIdentityIsSeparable()`。

- [ ] **Step 2: 跑測試確認失敗**

```sh
cmake --build build/native --target qsanguosha_rules_identity_tests
ctest --test-dir build/native -R qsanguosha_rules_identity --output-on-failure
```

Expected: FAIL —— `sealed identity carries a code_id failed`。

- [ ] **Step 3: 寫實作**

`src/core/protocol/rules-bundle-identity.h` 加入宣告:

```cpp
// Seals the build-time half separately so a client can reject an incompatible
// runtime before downloading any content.
QJsonObject sealCode(QJsonObject identity);
```

`src/core/protocol/rules-bundle-identity.cpp`:

```cpp
QJsonObject sealCode(QJsonObject identity)
{
    QJsonObject code;
    for (const char *key : {"protocol_version", "bridge_schema", "cpp_hash",
                            "bindings_abi", "interaction_schemas"})
        code.insert(QLatin1String(key), identity.value(QLatin1String(key)));
    identity.remove(QStringLiteral("code_id"));
    identity.insert(QStringLiteral("code_id"), digest(QStringLiteral("qsan-rules-code-v1"), code));
    return identity;
}
```

`seal()` 改為先呼叫 `sealCode`,再計 `bundle_id`,使 `code_id` 進入總封印:

```cpp
QJsonObject seal(QJsonObject identity)
{
    identity = sealCode(identity);
    identity.remove(QStringLiteral("bundle_id"));
    identity.insert(QStringLiteral("bundle_id"), digest(QStringLiteral("qsan-rules-bundle-v1"), identity));
    return identity;
}
```

`validate()` 的 hash 欄位迴圈加入 `"code_id"`。

`compatibilityError()` 的 `content_profile` 判斷由 `builtin-v1` 改為 `declared-v1`;
並把 `code_id` 比較放在 `bridge_schema` / `bindings_abi` 那一組之內:

```cpp
    if (client.value(QStringLiteral("bridge_schema")) != server.value(QStringLiteral("bridge_schema"))
        || client.value(QStringLiteral("bindings_abi")) != server.value(QStringLiteral("bindings_abi"))
        || client.value(QStringLiteral("code_id")) != server.value(QStringLiteral("code_id")))
        return QStringLiteral("rules_reload_required");
```

`src/core/rules-bundle-exporter.cpp:107` 的 `content_profile` 值由
`QStringLiteral("builtin-v1")` 改為 `QStringLiteral("declared-v1")`。

`web/src/rules-identity.ts:45` 改為:

```ts
  if (value.content_profile !== "declared-v1") throw new Error("rules_content_unsupported");
```

並在 `:56` 的 `rules_reload_required` 判斷中,把 `code_id` 與 `bridge_schema`
並列比較(照該檔既有寫法加入同一個條件式)。

- [ ] **Step 4: 跑測試確認通過**

```sh
cmake --build build/native --target qsanguosha_rules_identity_tests qsanguosha_selection_fixture
ctest --test-dir build/native -R qsanguosha_rules_identity --output-on-failure
python3 tests/client_runtime/check-rules-bundle.py \
  --native-runner build/native/qsanguosha_selection_fixture \
  --artifacts artifacts/rules-bundle
npm ci --prefix web && npx --prefix web tsc --noEmit -p web/tsconfig.json
```

Expected: 全部 PASS;TypeScript 無型別錯誤。

- [ ] **Step 5: Commit**

```bash
git add src/core/protocol/rules-bundle-identity.h src/core/protocol/rules-bundle-identity.cpp \
        src/core/rules-bundle-exporter.cpp web/src/rules-identity.ts \
        tests/protocol/rules-bundle-identity-test.cpp
git commit -m "feat(protocol): split code identity from content identity"
```

---

## 完成後狀態

- `extensions/` 的載入順序由 `lua/config.lua` 顯式決定,新增擴展只可在尾端追加
- 未宣告的 `.lua` 一律令身份失敗;`lang/` 與 `lua/ai/` 可宣告但不入封印
- 身份帶 `code_id`,前端可在下載內容之前判斷 runtime 是否相容
- `content_profile` 為 `declared-v1`,舊 client 乾脆失敗

**翻譯載入保留目錄掃描（使用者確認，2026-09-09）：** 不需要取代
`sgs.GetFileNames`；117 個翻譯檔已加入配送宣告，WASM native 翻譯會在協商通過後
傳給 Web 顯示。`translations.json` 已由原生 TUI 產生，完整 Web build 與成品
Chromium 啟動／翻譯資產驗收通過。詳見設計文件的「翻譯配送與成品補驗」。

**已完成(P2/P3，2026-09-09):** production WASM 內容已分離，Hello/hash 配送、
Worker 預熱／重建與原生 discard/exchange 已實作；`eligibility.ts` 及測試已刪除。
真實 sijyu＋animecard 的 native/WASM 一致性與聚焦驗收記錄見
[設計文件](../specs/2026-09-09-extension-compatibility-scope-design.md#2026-09-09-實作與驗收)。
未測完整對局（依使用者要求）；提交狀態見 Git 歷史。
