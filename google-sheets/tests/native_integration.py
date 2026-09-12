"""Bounded gateway/native integration probe for the Sheets bridge.

This script is intentionally separate from the fast source-contract tests.  It
starts the real bridge through :class:`gateway.NativeSession`, routes requests
through the real loopback gateway, and keeps a sanitized evidence transcript.
It is not a Google Sheets UI test and must only be run after the native bridge
build checkpoint is approved.
"""
from __future__ import annotations

import argparse
import collections
import copy
import hashlib
import http.client
import importlib.util
import itertools
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import uuid
import re


def load_gateway():
    path = Path(__file__).parents[1] / "gateway.py"
    spec = importlib.util.spec_from_file_location("sheets_gateway_native_probe", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("gateway_import_failed")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gateway = load_gateway()


def digest(value: object) -> str:
    if isinstance(value, bytes):
        return hashlib.sha256(value).hexdigest()
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                    separators=(",", ":"), default=str).encode()).hexdigest()


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def pid_digest(pid: int | None) -> str | None:
    return hashlib.sha256(str(pid).encode()).hexdigest() if pid is not None else None


def safe_code(value: object) -> str:
    return re.sub(r"[^A-Za-z0-9_.:-]", "_", str(value))[:120]


def scalar(value: object) -> str:
    """Make a compact, secret-free description suitable for transcript output."""
    if isinstance(value, dict):
        return "{" + ",".join(sorted(str(key) for key in value)) + "}"
    if isinstance(value, list):
        return f"list[{len(value)}]"
    return type(value).__name__


class Evidence:
    def __init__(self) -> None:
        self.events: list[dict] = []
        self.types: set[str] = set()
        self.game_over = False
        self.winner = ""
        self.trustee_observed = False

    def add(self, name: str, **fields: object) -> None:
        self.events.append({"event": name, **fields})

    def http(self, name: str, status: int, body: object) -> None:
        # Do not serialize request/response bodies: snapshots can contain
        # private cards and responses can contain session credentials.
        self.add(name, status=status, body_type=scalar(body), body_hash=digest(body))
        self.inspect_trustee(body)

    def inspect_trustee(self, value: object) -> None:
        if isinstance(value, dict):
            if value.get("trustee_engaged") is True or value.get("kind") == "trustee_engaged":
                self.trustee_observed = True
            for child in value.values():
                self.inspect_trustee(child)
        elif isinstance(value, list):
            for child in value:
                self.inspect_trustee(child)

    def inspect_snapshot(self, snapshot: dict) -> None:
        interaction = snapshot.get("interaction") or {}
        if interaction.get("type"):
            self.types.add(str(interaction["type"]))
        game = ((snapshot.get("state") or {}).get("game") or {})
        # Inspect actual state, never the transcript's response hashes.
        for player in (snapshot.get("view") or {}).get("players", []):
            if player.get("label") == "SheetsQA" and (
                    player.get("state") == "trust" or player.get("trusted") is True):
                self.trustee_observed = True
        if game.get("game_over") is True:
            self.game_over = True
            result = game.get("result")
            if isinstance(result, dict):
                winners = result.get("winner_tokens")
                if result.get("standoff") is True:
                    self.winner = "standoff"
                elif isinstance(winners, list) and winners and all(
                        isinstance(value, str) and value.strip() for value in winners):
                    self.winner = "+".join(winners)


class GatewayClient:
    def __init__(self, host: str, port: int, session: str, token: str, evidence: Evidence):
        self.host, self.port = host, port
        self.session, self.token, self.evidence = session, token, evidence

    def request(self, method: str, path: str, body: dict | None = None,
                *, token: str | None = None, session: str | None = None,
                authenticated: bool = True) -> tuple[int, object]:
        connection = http.client.HTTPConnection(self.host, self.port, timeout=20)
        headers = {"Connection": "close"}
        if authenticated:
            headers["Authorization"] = "Bearer " + (token if token is not None else self.token)
            headers["X-QSan-Session"] = session if session is not None else self.session
        if body is not None:
            raw = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
            headers["Content-Type"] = "application/json"
        else:
            raw = None
        try:
            connection.request(method, path, body=raw, headers=headers)
            response = connection.getresponse()
            payload = response.read()
            content_type = response.getheader("Content-Type", "")
            value: object = json.loads(payload.decode()) if "json" in content_type else payload
            self.evidence.http(method + " " + path.split("?", 1)[0], response.status, value)
            return response.status, value
        finally:
            connection.close()


def envelope(session: str, command_id: str, name: str, args: dict,
             generation: str = "0", revision: str = "0") -> dict:
    return {"api_version": 1, "session": session, "id": command_id,
            "generation": generation, "revision": revision, "name": name, "args": args}


def first_list(payload: dict, names: tuple[str, ...]) -> list:
    for name in names:
        value = payload.get(name)
        if isinstance(value, list) and value:
            return value
    return []


def values_for(value: object, names: tuple[str, ...]) -> list:
    """Extract native candidate arrays without assuming one payload wrapper."""
    if isinstance(value, dict):
        result: list = []
        for key, child in value.items():
            if key in names and isinstance(child, list):
                result.extend(child)
            result.extend(values_for(child, names))
        return result
    if isinstance(value, list):
        result: list = []
        for child in value:
            result.extend(values_for(child, names))
        return result
    return []


def item_id(value: object) -> str:
    if isinstance(value, dict):
        for key in ("id", "name", "value", "player_name"):
            if key in value:
                return str(value[key])
    return str(value)


def item_int(value: object) -> int | None:
    text = item_id(value)
    try:
        return int(text)
    except (TypeError, ValueError):
        return None


def choose_draft(interaction: dict, snapshot: dict) -> dict | None:
    """Build only conservative, native-shaped drafts from the advertised request."""
    kind = str(interaction.get("type", ""))
    payload = interaction.get("payload") or {}
    minimum = interaction.get("min", 0)
    try:
        minimum = max(0, int(minimum))
    except (TypeError, ValueError):
        minimum = 0
    options = values_for(interaction, ("options", "enumerated", "choices"))
    cards = values_for(interaction, ("selectable_cards", "cards", "visible_cards"))
    players = values_for(interaction, ("selectable_players", "target_players", "players"))
    generals = values_for(interaction, ("generals", "candidates"))
    if kind in {"choose_general", "choose_direction", "choice", "choose_suit",
                "choose_kingdom", "skill_invoke", "trigger_order", "choose_order",
                "choose_role_3v3", "luck_card", "ask_general"}:
        value = options[0] if options else None
        if isinstance(value, dict):
            value = item_id(value)
        return {"option": str(value)} if value is not None else None
    if kind == "choose_player":
        return {"targets": [item_id(players[0])]} if players else None
    if kind in {"arrange_general"}:
        values = [item_id(x) for x in generals]
        return {"order": values} if values else None
    if kind == "choose_role":
        roles = interaction.get("roles") or payload.get("roles") or []
        if not players or not roles:
            return None
        return {"assignments": [{"name": item_id(players[0]),
                                  "value": str(roles[0])}]}
    if kind == "skill_guanxing":
        ids = [value for x in cards if (value := item_int(x)) is not None]
        return {"top": ids, "bottom": []} if ids else None
    if kind == "skill_yiji":
        if not players:
            return None
        ids = [value for x in cards if (value := item_int(x)) is not None]
        return {"cards": ids[:max(1, minimum)],
                "target": item_id(players[0])}
    if kind in {"exchange_card", "ask_peach", "skill_gongxin", "play_card",
                "response_card", "discard_card", "nullification", "show_card",
                "amazing_grace", "pindian", "choose_card"}:
        ids = [value for x in cards if (value := item_int(x)) is not None]
        draft: dict = {"cards": ids[:minimum]} if minimum else {"cards": []}
        if players:
            draft["targets"] = [item_id(players[0])]
        return draft
    return None


def draft_candidates(interaction: dict, snapshot: dict) -> list[dict]:
    """Return a small deterministic set; every candidate is native preflighted."""
    first = choose_draft(interaction, snapshot)
    if first is None:
        return []
    candidates = [first]
    kind = str(interaction.get("type", ""))
    if kind in {"exchange_card", "ask_peach", "skill_gongxin", "play_card",
                "response_card", "discard_card", "nullification", "show_card",
                "amazing_grace", "pindian", "choose_card", "skill_yiji"}:
        cards = [value for x in values_for(interaction, ("selectable_cards", "cards", "visible_cards"))
                 if (value := item_int(x)) is not None]
        players = values_for(interaction, ("selectable_players", "target_players", "players"))
        try:
            minimum = max(0, int(interaction.get("min", 0)))
            maximum = max(minimum, int(interaction.get("max", minimum)))
        except (TypeError, ValueError):
            minimum = maximum = 0
        if cards and minimum <= 3:
            for size in range(minimum, min(maximum, 3) + 1):
                for subset in itertools.combinations(cards, size):
                    candidate = {"cards": list(subset)}
                    if kind == "skill_yiji" and players:
                        candidate["target"] = item_id(players[0])
                    elif players:
                        candidate["targets"] = [item_id(players[0])]
                    if candidate not in candidates:
                        candidates.append(candidate)
    return candidates[:12]


class OwnedProcesses:
    """Remember live identities before parent exit; PID reuse is not an orphan."""
    def __init__(self, pid: int):
        import psutil  # type: ignore
        self.psutil = psutil
        self.parent = psutil.Process(pid)
        self.identities: dict[int, float] = {}
        self.ports: set[int] = set()
        self.sample()

    def sample(self) -> None:
        # AccessDenied and missing psutil must fail verification, never mean [].
        try:
            processes = [self.parent] + self.parent.children(recursive=True)
        except self.psutil.NoSuchProcess:
            processes = []
        for process in processes:
            try:
                self.identities[process.pid] = process.create_time()
                for connection in process.connections(kind="tcp"):
                    if connection.status == self.psutil.CONN_LISTEN:
                        self.ports.add(connection.laddr.port)
            except self.psutil.NoSuchProcess:
                continue

    def survivors(self) -> list[int]:
        alive = []
        for pid, created in self.identities.items():
            try:
                process = self.psutil.Process(pid)
                if process.create_time() == created and process.is_running():
                    alive.append(pid)
            except self.psutil.NoSuchProcess:
                continue
        return alive


def port_released(port: int) -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.5)
        try:
            probe.connect(("127.0.0.1", port))
        except OSError:
            return True
        return False


def owner_pipe_eof(bridge: Path, asset_root: Path, directory: Path, evidence: Evidence) -> None:
    orphan = None
    try:
        orphan = gateway.NativeSession(bridge, asset_root, directory, 180)
        pid = orphan.process.pid
        owned = OwnedProcesses(pid)
        orphan.process.stdin.close()
        exit_code = orphan.process.wait(timeout=30)
        survivors = owned.survivors()
        evidence.add("owner_pipe_eof", pid_hash=pid_digest(pid), exit_code=exit_code,
                     expected_exit_code=86, green=False,
                     survivors=survivors, ports_released=all(port_released(p) for p in owned.ports))
        if exit_code != 86 or survivors or not all(port_released(p) for p in owned.ports):
            raise RuntimeError("owner_pipe_eof_unexpected_exit")
    finally:
        if orphan is not None:
            orphan.close()


def run(args: argparse.Namespace) -> int:
    evidence = Evidence()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run_root = output / ("run-" + uuid.uuid4().hex)
    gateway.private_directory(run_root)
    bridge = args.bridge.resolve(strict=True)
    asset_root = args.asset_root.resolve(strict=True)
    (asset_root / "image").resolve(strict=True)
    sessions = []
    owned_processes = {}
    server = None
    server_thread = None
    clients: list[GatewayClient] = []
    exit_code = 1
    try:
        service = gateway.Gateway(sessions)
        server = gateway.Server(("127.0.0.1", 0), service)
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        gateway_port = server.server_address[1]
        evidence.add("gateway_ready", port=gateway_port, bridge_sha256=file_digest(bridge))
        for index in range(2):
            slot = run_root / ("slot-" + str(index + 1))
            session = gateway.NativeSession(bridge, asset_root, slot, 180)
            sessions.append(session)
            owned_processes[session.session] = OwnedProcesses(session.process.pid)
            service.request_times[session.session] = collections.deque()
            evidence.add("native_ready", slot=index + 1, pid_hash=pid_digest(session.process.pid),
                         native_port=session.port,
                         observed_pids=list(owned_processes[session.session].identities))
        for index, session in enumerate(sessions):
            nonce = "nonce-" + uuid.uuid4().hex
            status, pair = GatewayClient("127.0.0.1", gateway_port, session.session,
                                         "", evidence).request(
                                             "POST", "/v1/pair",
                                             {"code": session.code, "pair_nonce": nonce},
                                             authenticated=False)
            if status != 200:
                raise RuntimeError("pair_failed")
            recovered_status, recovered = GatewayClient("127.0.0.1", gateway_port,
                                                        session.session, "", evidence).request(
                                                            "POST", "/v1/pair",
                                                            {"code": session.code, "pair_nonce": nonce},
                                                            authenticated=False)
            if recovered_status != 200 or recovered != pair:
                raise RuntimeError("pair_lost_reply_not_recoverable")
            client = GatewayClient("127.0.0.1", gateway_port, pair["session"],
                                   pair["token"], evidence)
            clients.append(client)
            bad_status, _ = client.request("GET", "/v1/updates?after=0", token="0" * 64)
            cross_status, _ = client.request("GET", "/v1/updates?after=0",
                                             session=sessions[1 - index].session)
            if bad_status != 401 or cross_status != 401:
                raise RuntimeError("cross_session_auth_not_denied")
        probe = clients[0]
        first = envelope(probe.session, "100", "catalog", {})
        status1, body1 = probe.request("POST", "/v1/commands", first)
        status2, body2 = probe.request("POST", "/v1/commands", first)
        conflict = dict(first, args={"unexpected": True})
        status3, body3 = probe.request("POST", "/v1/commands", conflict)
        if status1 != 200 or status2 != 200 or body1 != body2 or status3 != 200 \
                or not isinstance(body3, dict) or body3.get("error") != "command_id_conflict":
            raise RuntimeError("command_id_cache_contract_failed")
        status, snapshot = probe.request("GET", "/v1/updates?after=0")
        if status != 200 or not isinstance(snapshot, dict):
            raise RuntimeError("initial_snapshot_failed")
        if isinstance(snapshot, dict):
            probe.evidence.inspect_snapshot(snapshot.get("snapshot", snapshot))
        handles: list[str] = []
        def collect(value: object) -> None:
            if isinstance(value, dict):
                for key, item in value.items():
                    if key == "image" or key.endswith("_image"):
                        if isinstance(item, str) and item.startswith("/v1/assets/"):
                            handles.append(item)
                    collect(item)
            elif isinstance(value, list):
                for item in value:
                    collect(item)
        collect(snapshot)
        if handles:
            asset_status, _ = probe.request("GET", handles[0])
            if asset_status != 200:
                raise RuntimeError("opaque_asset_fetch_failed")
        else:
            evidence.add("asset_fetch", status="SKIPPED_NO_CATALOG_IMAGE")
        if not args.skip_game:
            settings = {"ServerName": "SheetsQA", "GameMode": "05p",
                        "OperationNoLimit": True, "CountDownSeconds": 0,
                        "OriginAIDelay": 20, "AIDelayAD": 20,
                        "AlterAIDelayAD": False}
            host = envelope(probe.session, "200", "host", {
                "private": True, "name": "SheetsQA", "avatar": "caocao",
                "robots": 4, "settings": settings})
            host_status, host_body = probe.request("POST", "/v1/commands", host)
            if host_status != 200 or not isinstance(host_body, dict) or not host_body.get("ok"):
                raise RuntimeError("native_host_05p_failed")
            deadline = time.monotonic() + args.game_timeout
            next_id = 201
            after = "0"
            handled_request = None
            ready_sent = False
            stop_observed = False
            while time.monotonic() < deadline and not evidence.game_over:
                owned_processes[sessions[0].session].sample()
                status, value = probe.request("GET", "/v1/updates?after=" + after)
                if status != 200 or not isinstance(value, dict):
                    time.sleep(1.0)
                    continue
                if isinstance(value.get("sequence"), str):
                    after = value["sequence"]
                snap = value.get("snapshot", value)
                if not isinstance(snap, dict):
                    time.sleep(1.0)
                    continue
                evidence.inspect_snapshot(snap)
                connection_state = str(snap.get("connection", "")).lower()
                if connection_state in {"closed", "disconnected", "error", "failed"}:
                    raise RuntimeError("native_connection_lost:" + connection_state)
                for event in value.get("events", []):
                    if isinstance(event, dict) and event.get("kind") == "error":
                        data = event.get("data") or {}
                        code = safe_code(data.get("code", data.get("message", "unknown")))
                        evidence.add("native_event_error", code=code)
                        raise RuntimeError("native_event_error:" + code)
                players = (snap.get("view") or {}).get("players", [])
                if not ready_sent and connection_state in {"connected", "active", "ready"} \
                        and isinstance(players, list) and len(players) >= 5:
                    ready = envelope(probe.session, str(next_id), "ready", {"ready": True},
                                     str(snap.get("generation", "0")), str(snap.get("revision", "0")))
                    next_id += 1
                    ready_status, ready_body = probe.request("POST", "/v1/commands", ready)
                    if ready_status != 200 or not isinstance(ready_body, dict) or not ready_body.get("ok"):
                        raise RuntimeError("native_ready_failed")
                    ready_sent = True
                interaction = snap.get("interaction") or {}
                interaction_type = str(interaction.get("type", ""))
                # request_id is a snapshot identity; interaction is a typed
                # descriptor and does not carry the top-level command id.
                request_id = str(snap.get("request_id", ""))
                if args.stop_at_interaction and interaction_type == args.stop_at_interaction:
                    evidence.add("shutdown_at_interaction", type=interaction_type,
                                 request_id_present=request_id not in {"", "0"})
                    stop_observed = True
                    break
                if (request_id not in {"", "0"} and interaction.get("type") not in {None, "", "none"}
                        and request_id != handled_request):
                    generation = str(snap.get("generation", "0"))
                    revision = str(snap.get("revision", "0"))
                    confirmed = False
                    candidate_sources = [interaction]
                    candidates_seen: set[str] = set()
                    source_index = 0
                    while source_index < len(candidate_sources) and not confirmed:
                        candidate_interaction = candidate_sources[source_index]
                        source_index += 1
                        for draft in draft_candidates(candidate_interaction, snap):
                            marker = json.dumps(draft, sort_keys=True, ensure_ascii=False)
                            if marker in candidates_seen:
                                continue
                            candidates_seen.add(marker)
                            select = envelope(probe.session, str(next_id), "select",
                                              {"request_id": request_id, "draft": draft}, generation, revision)
                            next_id += 1
                            select_status, select_body = probe.request("POST", "/v1/commands", select)
                            selection = ((select_body.get("result") or {}).get("selection", {})
                                         if isinstance(select_body, dict) else {})
                            if select_status != 200 or not selection.get("can_confirm"):
                                latest_ui = selection.get("ui") if isinstance(selection, dict) else None
                                if isinstance(latest_ui, dict):
                                    updated = copy.deepcopy(candidate_interaction)
                                    updated["ui"] = latest_ui
                                    candidate_sources.append(updated)
                                continue
                            submit = envelope(probe.session, str(next_id), "submit",
                                               {"request_id": request_id, "draft": draft}, generation, revision)
                            next_id += 1
                            submit_status, submit_body = probe.request("POST", "/v1/commands", submit)
                            if submit_status != 200 or not isinstance(submit_body, dict) or not submit_body.get("ok"):
                                raise RuntimeError("native_submit_failed")
                            confirmed = True
                            handled_request = request_id
                            break
                    if not confirmed and interaction.get("cancelable") is True:
                        cancel = envelope(probe.session, str(next_id), "cancel",
                                           {"request_id": request_id}, generation, revision)
                        next_id += 1
                        cancel_status, cancel_body = probe.request("POST", "/v1/commands", cancel)
                        if cancel_status != 200 or not isinstance(cancel_body, dict) or not cancel_body.get("ok"):
                            raise RuntimeError("native_cancel_failed")
                        handled_request = request_id
                    elif not confirmed:
                        raise RuntimeError("mandatory_interaction_unresolved:" + str(interaction.get("type")))
                time.sleep(1.0)
            if args.stop_at_interaction and not stop_observed:
                raise TimeoutError("stop_interaction_not_observed:" + args.stop_at_interaction)
            if not args.stop_at_interaction and not evidence.game_over:
                raise TimeoutError("game_timeout_without_game_over")
        owner_pipe_eof(bridge, asset_root, run_root / "owner-loss", evidence)
        exit_code = 0
    except KeyboardInterrupt:
        evidence.add("interrupted", reason="keyboard_interrupt")
    except Exception as error:
        evidence.add("failure", error=type(error).__name__ + ":" + str(error)[:240])
    finally:
        for session in reversed(sessions):
            pid = session.process.pid if session.process else None
            native_port = session.port
            observed = owned_processes.get(session.session)
            cleanup_inspection_error = None
            try:
                if observed:
                    observed.sample()
            except Exception as error:
                cleanup_inspection_error = type(error).__name__
            session.close()
            try:
                children = observed.survivors() if observed else None
                owned_ports_released = bool(observed) and all(port_released(p) for p in observed.ports)
            except Exception as error:
                cleanup_inspection_error = type(error).__name__
                children, owned_ports_released = None, False
            evidence.add("native_closed", pid_hash=pid_digest(pid), exit_code=session.exit_code,
                         clean_exit=session.clean_exit, forced_termination=session.forced_termination,
                         descendants_after_close=children,
                         owned_ports_released=owned_ports_released,
                         cleanup_inspection_error=cleanup_inspection_error,
                         port_released=port_released(native_port) if native_port else None)
            if (not session.clean_exit or children is None or children or cleanup_inspection_error
                    or not owned_ports_released or (native_port and not port_released(native_port))):
                exit_code = 1
        if server is not None:
            server.shutdown()
            server.server_close()
            if server_thread:
                server_thread.join(timeout=5)
            gateway_released = port_released(server.server_address[1])
            evidence.add("gateway_closed", port=server.server_address[1],
                         port_released=gateway_released)
            if not gateway_released:
                exit_code = 1
        if args.skip_game:
            evidence.add("game", status="SKIPPED")
        if exit_code == 0 and not args.skip_game and not args.stop_at_interaction:
            if not evidence.game_over or not evidence.winner:
                exit_code = 1
            if evidence.trustee_observed:
                exit_code = 1
        summary = {"passed": exit_code == 0, "game_over": evidence.game_over,
                   "scope": "native_shutdown_probe" if args.stop_at_interaction else "native_gateway_probe",
                   "real_sheets_acceptance": "NOT_RUN",
                   "trustee_observed": evidence.trustee_observed,
                   "no_trustee_acceptance": "NOT_PROVEN_by_sampled_snapshots",
                   "process_coverage": "sampled_live_pid_create_time_and_listeners",
                   "winner": evidence.winner, "interaction_types": sorted(evidence.types),
                   "events": evidence.events}
        (run_root / "transcript.json").write_text(json.dumps(summary, ensure_ascii=False,
                                                              indent=2) + "\n", encoding="utf-8")
        (run_root / "result.json").write_text(json.dumps(summary, ensure_ascii=False,
                                                          indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"passed": summary["passed"], "output": str(run_root),
                          "game_over": summary["game_over"], "winner": summary["winner"],
                          "interaction_types": summary["interaction_types"]}, ensure_ascii=False))
    return exit_code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--asset-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--game-timeout", type=int, default=900)
    parser.add_argument("--skip-game", action="store_true")
    parser.add_argument("--stop-at-interaction", choices=("choose_general", "play_card"))
    args = parser.parse_args()
    if args.game_timeout < 1:
        parser.error("--game-timeout must be positive")
    if args.skip_game and args.stop_at_interaction:
        parser.error("--stop-at-interaction requires the native game path")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
