# Test suite registration policy

CTest 是測試套件入口 (test suite entry point)，不是每個案例各自一條的清單。

新增功能首先加入既有 test suite；只有具備不同依賴 (dependency)、執行環境
(runtime environment)、逾時等級 (timeout class) 或失敗領域 (failure domain)
時，才新增 CTest entry。`qsan_add_ctest()` 的 `qsan` 是 QSanguosha 專案前綴，
與 AddressSanitizer 無關。

主要 labels 只使用：

```text
fast  client-core  protocol  server  runtime  ui  network  packaging  stress
```

multi-case suite 必須逐案例輸出 PASS/FAIL、繼續執行仍可安全隔離的其餘案例、
在結尾輸出 failure summary，且任一 child failure 都令 suite exit non-zero。

Registration and labels are defined by [`qsan_add_ctest()` in CMakeLists.txt](CMakeLists.txt). Query a configured build with `ctest --test-dir <build-dir> -N -C Debug` to list its entries without running them.

## Suite reference

| CTest entry | 內部 coverage | 單獨除錯 |
|---|---|---|
| `qsanguosha_server_unit` | room-notifier、skill-runtime、request、card-movement、extra-turn、room-roster、player-lifecycle、player-decision | `qsanguosha_server_tests --suite <name>` |
| `qsanguosha_server_cli_contract` | parser、`--help`、`--version`、config validation、config/CLI precedence | 直接執行 `qsanguosha_cli_tests` 可只跑 parser |
| `qsanguosha_network_integration` | level 1、2、3，逐 level process isolation | `qsanguosha_network_integration_tests --level <1|2|3> --server <path>` |
| `qsanguosha_client_core_contract` | QtCore contract、production registry、artifact drift | 直接執行 `qsanguosha_client_core_tests`；GUI build 的 CTest 另傳 registry generator |
| `qsanguosha_ui_contract` | startup、network、multimedia report schemas | 直接執行原 report-test executable |
| `qsanguosha_ui_runner_contract` | local-response parser、startup/network smoke CLI、skill UI runner CLI | 直接執行原 executable/script |
| `qsanguosha_runtime_contract` | lua-runtime、room-runtime、card-lifetime、card-lifetime-lua、synthetic-30 | `qsanguosha_runtime_tests --suite <name>` |
| `qsanguosha_card_lifetime_stress` | synthetic-50 | `qsanguosha_runtime_tests --suite card-lifetime-synthetic-50 --seed <seed>` |
| `qsanguosha_card_lifetime_source_check` | ownership ledger/static ingress check | 直接執行 `tools/check-card-lifetime.py` |
| `qsanguosha_roomthread_perf` | V2 分表、優先序保序、Room 私有排序鍵、mutex profile 契約 | 直接執行 `qsanguosha_roomthread_perf_tests` |
| `qsanguosha_card_overview_contract` | classifier、model | 直接執行原 classifier/model executable |

The historical mapping and recorded counts are in the [consolidation record](process/ctest-consolidation.md).

## Protocol V2 rule

Protocol V2 優先擴充：

```text
qsanguosha_protocol_contract
qsanguosha_protocol_integration
```

header、split、partial、coalesce、hello、reject 等應是 suite 內部 cases；除非它們的
dependency/runtime/timeout/failure domain 不同，否則不得逐 case 新增 CTest entry。

## Runtime layering

普通 CTest 只涵蓋 unit、contract 與 lightweight integration。Linux GUI Xvfb startup、
完整 GUI network game、AppImage/portable package、Docker image、Windows deploy 與
跨平台 client/server 等 heavyweight smoke 繼續由 GitHub Actions 或本機專用 runner
管理，不納入一般 `ctest --output-on-failure`。
