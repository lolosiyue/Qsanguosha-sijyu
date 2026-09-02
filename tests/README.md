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

## Consolidated entries

| CTest entry | 內部 coverage | 單獨除錯 |
|---|---|---|
| `qsanguosha_server_unit` | room-notifier、skill-runtime、request、card-movement、extra-turn、room-roster、player-lifecycle、player-decision | `qsanguosha_server_tests --suite <name>` |
| `qsanguosha_server_cli_contract` | parser、`--help`、`--version`、config validation、config/CLI precedence | 直接執行 `qsanguosha_cli_tests` 可只跑 parser |
| `qsanguosha_network_integration` | level 1、2、3，逐 level process isolation | `qsanguosha_network_integration_tests --level <1|2|3> --server <path>` |
| `qsanguosha_client_core_contract` | QtCore contract、production registry 29/29、artifact drift | 直接執行 `qsanguosha_client_core_tests`；GUI build 的 CTest 另傳 registry generator |
| `qsanguosha_ui_contract` | startup、network、multimedia report schemas | 直接執行原 report-test executable |
| `qsanguosha_ui_runner_contract` | local-response parser、startup/network smoke CLI、skill UI runner CLI | 直接執行原 executable/script |
| `qsanguosha_runtime_contract` | lua-runtime、room-runtime、card-lifetime、card-lifetime-lua、synthetic-30 | `qsanguosha_runtime_tests --suite <name>` |
| `qsanguosha_card_lifetime_stress` | synthetic-50 | `qsanguosha_runtime_tests --suite card-lifetime-synthetic-50 --seed <seed>` |
| `qsanguosha_card_lifetime_source_check` | ownership ledger/static ingress check | 直接執行 `tools/check-card-lifetime.py` |
| `qsanguosha_roomthread_perf` | V2 分表、優先序保序、Room 私有排序鍵、mutex profile 契約 | 直接執行 `qsanguosha_roomthread_perf_tests` |
| `qsanguosha_card_overview_contract` | classifier、model | 直接執行原 classifier/model executable |

## Coverage migration

| 舊 CTest | 新 suite |
|---|---|
| `qsanguosha_room_notifier`、`qsanguosha_skill_runtime_coordinator`、`qsanguosha_request_coordinator`、`qsanguosha_card_movement_service`、`qsanguosha_extra_turn_scheduler`、`qsanguosha_room_roster`、`qsanguosha_player_lifecycle_service`、`qsanguosha_player_decision_service` | `qsanguosha_server_unit` |
| `qsanguosha_server_cli_parser`、`qsanguosha_server_cli_help`、`qsanguosha_server_cli_version`、`qsanguosha_server_config_check`、`qsanguosha_server_config_precedence` | `qsanguosha_server_cli_contract` |
| `qsanguosha_network_integration_level1`、`level2`、`level3` | `qsanguosha_network_integration` |
| `qsanguosha_client_core_contract`、`qsanguosha_client_interaction_matrix` | `qsanguosha_client_core_contract` |
| `qsanguosha_ui_startup_smoke_contract`、`qsanguosha_network_ui_smoke_contract`、`qsanguosha_multimedia_smoke_contract` | `qsanguosha_ui_contract` |
| `qsanguosha_local_response_ui_case_parser`、`qsanguosha_ui_startup_smoke_cli_contract`、`qsanguosha_network_ui_smoke_cli_contract`、`qsanguosha_skill_ui_runner_contract` | `qsanguosha_ui_runner_contract` |
| `qsanguosha_lua_runtime_isolation`、`qsanguosha_room_runtime_isolation`、`qsanguosha_card_lifetime`、`qsanguosha_card_lifetime:synthetic-30`、`qsanguosha_card_lifetime:lua` | `qsanguosha_runtime_contract` |
| `qsanguosha_card_lifetime:synthetic-50` | `qsanguosha_card_lifetime_stress` |
| `qsanguosha_card_lifetime:source-check` | `qsanguosha_card_lifetime_source_check` |
| `qsanguosha_card_overview_classifier`、`qsanguosha_card_overview_model` | `qsanguosha_card_overview_contract` |
| `qsanguosha_engine_smoke`、`qsanguosha_protocol_messages`、`qsanguosha_effects_profile_contract`、`qsanguosha_interaction_reply_adapter_contract`、`qsanguosha_packaging_contract`、`qsanguosha_runtime_paths`、`qsanguosha_server_logging`、`qsanguosha_replay_game_state_protocol`、`qsanguosha_photo_layout_fit` | 保留原 entry 與 assertions |
| `qsanguosha_systemd_unit`、`qsanguosha_server_console`、`qsanguosha_server_logging_smoke` | UNIX 保留原 entry；runtime/failure domain 不同 |

以上 mapping 保留全部舊 child commands；consolidation 只改 CTest registration 與父層
failure reporting，不刪除原測試函式、assertions 或 executables。

本次 consolidation 的 Windows Debug 實測為 `40 → 18` 條 CTest，40 個舊
coverage units 全部可在上表的新 entry 找到；UNIX GUI 定義由 `46 → 22`
（server-only 舊版因沒有獨立 matrix entry，為 `45 → 22`），額外四條是 systemd、
console/logging smoke 與合併後 network integration。

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
