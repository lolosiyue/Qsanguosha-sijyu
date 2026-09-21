# CTest consolidation record

This preserves the registration migration and measurements recorded with the consolidation. The original record did not identify a measurement date or commit. Query the configured build with `ctest -N` for its current entries.

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
