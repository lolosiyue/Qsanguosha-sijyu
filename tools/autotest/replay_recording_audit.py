"""Audit actual Replay V2 files and optional native inspector captures.

This reads product recordings only; it does not create games or fixture packets.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path


GET, LOSE, TAKE_AG, KNOWN = 39, 40, 49, 57
PROPERTY, MARK, GAME_OVER, ROUND, DYNAMIC, SYNC = 30, 19, 36, 89, 98, 133


def read_events(path):
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line]
    if rows[0].get("format") != "qsanguosha-replay" or rows[0].get("format_version") != 2:
        raise ValueError(f"Not a Replay V2 file: {path}")
    return rows[1:]


def move_shape(event):
    message = event["message"]
    payload = message["payload"]
    moves = []
    for original in payload["moves"]:
        move = {key: value for key, value in original.items() if key not in ("card_ids", "open")}
        move["card_count"] = len(original["card_ids"])
        moves.append(move)
    return json.dumps([message["command"], payload["move_id"], moves], sort_keys=True)


def fold_hands(events, last):
    hands = defaultdict(list)
    problems = []
    for index, event in enumerate(events[:last + 1]):
        message = event["message"]
        command, payload = message["command"], message.get("payload", {})
        if command == SYNC and payload.get("phase") == "begin":
            hands.clear()
        elif command in (GET, LOSE):
            for move in payload["moves"]:
                side = "to" if command == GET else "from"
                name = move[f"{side}_player"]
                if not name or move[f"{side}_place"] != 0:
                    continue
                hand = hands[name]
                for card in move["card_ids"]:
                    if command == GET:
                        hand.append(card)
                    elif card in hand:
                        hand.remove(card)
                    elif -1 in hand:
                        hand.remove(-1)
                    elif card < 0 and hand:
                        hand.pop()
                    else:
                        problems.append(f"event {index}: remove {card} from empty/inconsistent {name} hand")
        elif command == TAKE_AG and payload.get("move_cards"):
            name = payload.get("taker")
            if name:
                hands[name].append(payload["card_id"])
        elif command == KNOWN:
            name, cards = payload.get("player_name"), payload.get("card_ids", [])
            if name and len(cards) == len(hands[name]):
                hands[name] = list(cards)
    return hands, problems


def audit(index_path, inspection_path, controller, controlled):
    catalogue = json.loads(index_path.read_text(encoding="utf-8-sig"))
    streams, checks, failures = {}, [], []
    initial_shapes = {}
    for seat in catalogue["perspectives"]:
        name = seat["player_name"]
        path = index_path.parent / seat["file"]
        events = read_events(path)
        streams[name] = events
        errors = []
        times = [int(event["elapsed_ms"]) for event in events]
        ids = [event["message"]["message_id"] for event in events]
        counts = Counter(event["message"]["command"] for event in events)
        rounds, recorded_self, role = 0, "", ""
        sync_begins = sync_ends = 0
        initial_hand = []
        additions = Counter()
        for event in events:
            message = event["message"]
            command, payload = message["command"], message.get("payload", {})
            if command == ROUND:
                rounds += 1
            if command == SYNC:
                sync_begins += payload.get("phase") == "begin"
                sync_ends += payload.get("phase") == "end"
                if payload.get("phase") == "end" and "round" in payload:
                    rounds = payload["round"]
            if command == PROPERTY and payload.get("player_name") == "MG_SELF":
                if payload.get("property_name") == "objectName":
                    recorded_self = payload.get("string_value", "")
                if payload.get("property_name") == "role":
                    role = payload.get("string_value", "")
            if command == DYNAMIC:
                additions[payload["player_name"]] += 1
            if command == GET:
                for move in payload["moves"]:
                    if move["to_player"] == name and move["reason"]["skill_name"] == "InitialHandCards":
                        initial_hand.extend(move["card_ids"])
        if recorded_self != name:
            errors.append("self identity mismatch")
        if times != sorted(times) or not times or min(times) < int(seat["available_from_ms"]):
            errors.append("invalid elapsed/birth coordinates")
        if len(ids) != len(set(ids)):
            errors.append("duplicate message ids")
        if len(events) != seat.get("event_count"):
            errors.append("index event count mismatch")
        if "sha256" in seat and hashlib.sha256(path.read_bytes()).hexdigest() != seat["sha256"]:
            errors.append("index SHA-256 mismatch")
        if "bytes" in seat and path.stat().st_size != int(seat["bytes"]):
            errors.append("index byte count mismatch")
        if rounds != catalogue["round_count"]:
            errors.append("recorded round count differs from index")
        if catalogue["status"] == "completed" and counts[GAME_OVER] != 1:
            errors.append("completed stream needs exactly one GAME_OVER")
        if not counts[GET] or not counts[LOSE]:
            errors.append("missing movement coverage")
        if int(seat["available_from_ms"]) > 0:
            if times[0] != int(seat["available_from_ms"]) or sync_begins != 1 or sync_ends != 1:
                errors.append("late seat missing complete birth bootstrap")
            if role != seat["role"] or not role:
                errors.append("late seat role bootstrap mismatch")
            if not initial_hand or any(card < 0 for card in initial_hand):
                errors.append("late seat initial private hand missing")
        else:
            initial_shapes[name] = [move_shape(event) for event in events if event["message"]["command"] in (GET, LOSE)]
        checks.append(dict(player=name, status="failed" if errors else "passed", events=len(events),
                           get=counts[GET], lose=counts[LOSE], rounds=rounds, role=role,
                           available_from_ms=seat["available_from_ms"], initial_hand=initial_hand,
                           dynamic_additions=dict(additions), errors=errors))
        failures.extend(f"{name}: {error}" for error in errors)

    baseline = next(iter(initial_shapes.values()), [])
    parity = {name: shapes == baseline for name, shapes in initial_shapes.items()}
    failures.extend(f"{name}: movement sequence differs from original seats" for name, same in parity.items() if not same)

    control = {}
    if controller and controlled:
        events = streams[controller]
        active, visible_draws, redacted_other_draws, contexts = False, 0, 0, []
        controlled_moves = [event["message"] for event in streams[controlled]
                            if event["message"]["command"] in (GET, LOSE)]
        move_index, compared_private_moves, private_mismatches = 0, 0, 0
        for event in events:
            message = event["message"]
            command, payload = message["command"], message.get("payload", {})
            if command == MARK and payload.get("player_name") == controller:
                if payload.get("mark_name") == f"HandcardVisible_{controlled}+dualcontrol+sys_":
                    active = payload.get("value", payload.get("mark_value", 0)) > 0
            if command == 128:
                contexts.append(payload.get("player_name"))
            if command in (GET, LOSE):
                if active and move_index < len(controlled_moves):
                    actual_moves = controlled_moves[move_index]["payload"]["moves"]
                    for offset, move in enumerate(payload["moves"]):
                        if ((move["from_player"] == controlled and move["from_place"] == 0)
                                or (move["to_player"] == controlled and move["to_place"] == 0)):
                            compared_private_moves += 1
                            if offset >= len(actual_moves) or move["card_ids"] != actual_moves[offset]["card_ids"]:
                                private_mismatches += 1
                move_index += 1
            if command == GET and active:
                for move in payload["moves"]:
                    if move["to_place"] != 0 or move["from_place"] != 6:
                        continue
                    if move["to_player"] == controlled and all(card >= 0 for card in move["card_ids"]):
                        visible_draws += 1
                    elif move["to_player"] not in (controller, controlled) and all(card < 0 for card in move["card_ids"]):
                        redacted_other_draws += 1
        control = dict(controller=controller, controlled=controlled, visible_controlled_draws=visible_draws,
                       redacted_other_draws=redacted_other_draws, context_switches=contexts,
                       compared_private_moves=compared_private_moves, private_move_mismatches=private_mismatches)
        if not visible_draws or not redacted_other_draws or controlled not in contexts:
            failures.append("online control/visibility coverage incomplete")
        if not compared_private_moves or private_mismatches:
            failures.append("controller private cards differ from the controlled seat's recording")

    hand_checks = []
    if inspection_path:
        inspection = json.loads(inspection_path.read_text(encoding="utf-8-sig"))
        for stage in inspection["stages"]:
            if stage["status"] == "skipped":
                continue
            hands, problems = fold_hands(streams[stage["source_player"]], stage["applied_event_index"])
            mismatches = []
            own_matches = False
            for player in stage["players"]:
                name, hand = player["player_name"], hands[player["player_name"]]
                if len(hand) != player["hand_count"]:
                    mismatches.append(dict(player=name, expected=len(hand), actual=player["hand_count"]))
                if name == stage["source_player"]:
                    own_matches = sorted(hand) == sorted(player["hand_ids"])
            passed = stage["status"] == "passed" and own_matches and not mismatches and not problems
            hand_checks.append(dict(stage=stage["stage"], status="passed" if passed else "failed",
                                    own_hand_matches=own_matches, count_mismatches=mismatches, problems=problems))
            if not passed:
                failures.append(f"{stage['stage']}: native hand/state audit failed")
        if inspection.get("status") != "passed":
            failures.append("native inspector did not complete successfully")
    return dict(status="failed" if failures else "passed", recording_status=catalogue["status"],
                round_count=catalogue["round_count"], perspectives=checks, original_seat_movement_parity=parity,
                control=control, native_hands=hand_checks, errors=failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--inspection", type=Path)
    parser.add_argument("--controller")
    parser.add_argument("--controlled")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.index, args.inspection, args.controller, args.controlled)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(dict(status=result["status"], perspectives=len(result["perspectives"]),
                          native_stages=len(result["native_hands"]), errors=result["errors"]), ensure_ascii=False))
    return result["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
