import { asNumber, isObject, type JsonObject, type JsonValue } from "./protocol";
import type { ClientGameState } from "./state";
import type { RulesEvaluation } from "./rules-client";

export interface Metric<T> {
  known: boolean;
  value: T | null;
}

function fixedDistanceValues(stored: JsonValue | undefined): number[] {
  if (Array.isArray(stored))
    return stored.map((entry) => asNumber(entry));
  if (typeof stored === "number" && Number.isFinite(stored))
    return [stored];
  if (typeof stored === "string" && stored.trim() !== "") {
    const parsed = Number(stored);
    if (Number.isFinite(parsed))
      return [parsed];
  }
  return [];
}

function nativeMetric<T>(evaluation: RulesEvaluation | null | undefined, player: string,
                         field: "handMax" | "attackRange"): Metric<T> {
  const metrics = evaluation?.player_metrics;
  const record = isObject(metrics) && isObject(metrics[player])
    ? metrics[player] as JsonObject : undefined;
  const entry = record && isObject(record[field]) ? record[field] as JsonObject : undefined;
  if (!entry)
    return { known: false, value: null };
  return {
    known: entry.known === true,
    value: entry.known === true ? entry.value as T : null
  };
}

function nativePairMetric<T>(evaluation: RulesEvaluation | null | undefined, from: string, to: string,
                             field: "distanceTo" | "inAttackRange"): Metric<T> {
  const metrics = evaluation?.player_metrics;
  const record = isObject(metrics) && isObject(metrics[from])
    ? metrics[from] as JsonObject : undefined;
  const pairs = record && isObject(record[field]) ? record[field] as JsonObject : undefined;
  const entry = pairs && isObject(pairs[to]) ? pairs[to] as JsonObject : undefined;
  if (!entry)
    return { known: false, value: null };
  return {
    known: entry.known === true,
    value: entry.known === true ? entry.value as T : null
  };
}

export function syncedHandMax(state: ClientGameState, player: string): Metric<number> {
  const data = state.player(player);
  if (!data || !Object.prototype.hasOwnProperty.call(data, "hand_max"))
    return { known: false, value: null };
  return { known: true, value: asNumber(data.hand_max) };
}

export function syncedDistanceTo(state: ClientGameState, from: string, to: string): Metric<number> {
  if (!from || !to)
    return { known: false, value: null };
  if (from === to)
    return { known: true, value: 0 };
  const distances = isObject(state.playerValue(from, "fixed_distances"))
    ? state.playerValue(from, "fixed_distances") as JsonObject : undefined;
  const values = fixedDistanceValues(distances?.[to]);
  if (values.length > 0)
    return { known: true, value: Math.min(...values) };
  const cached = state.playerValue(from, `distanceTo_${to}`);
  if (cached === undefined || cached === null || cached === "")
    return { known: false, value: null };
  return { known: true, value: Math.max(asNumber(cached), 1) };
}

export function displayHandMax(state: ClientGameState, player: string,
                               evaluation: RulesEvaluation | null = null): Metric<number> {
  const native = nativeMetric<number>(evaluation, player, "handMax");
  return native.known ? native : syncedHandMax(state, player);
}

export function displayDistanceTo(state: ClientGameState, from: string, to: string,
                                  evaluation: RulesEvaluation | null = null): Metric<number> {
  const native = nativePairMetric<number>(evaluation, from, to, "distanceTo");
  return native.known ? native : syncedDistanceTo(state, from, to);
}

export function displayAttackRange(evaluation: RulesEvaluation | null, player: string): Metric<number> {
  return nativeMetric<number>(evaluation, player, "attackRange");
}

export function displayInAttackRange(evaluation: RulesEvaluation | null, from: string,
                                     to: string): Metric<boolean> {
  return nativePairMetric<boolean>(evaluation, from, to, "inAttackRange");
}

export function formatMetric(metric: Metric<number | boolean>, suffix = ""): string {
  if (!metric.known || metric.value === null)
    return "？";
  if (typeof metric.value === "boolean")
    return metric.value ? "是" : "否";
  return `${metric.value}${suffix}`;
}

export function playerHandLabel(state: ClientGameState, player: string,
                                evaluation: RulesEvaluation | null = null): string {
  const count = asNumber(state.playerValue(player, "hand_count"));
  const max = displayHandMax(state, player, evaluation);
  return max.known ? `手${count}/${max.value}` : `手${count}`;
}

export function targetRangeLabel(state: ClientGameState, self: string, target: string,
                                 evaluation: RulesEvaluation | null = null): string {
  const distance = displayDistanceTo(state, self, target, evaluation);
  const range = displayAttackRange(evaluation, self);
  const inside = displayInAttackRange(evaluation, self, target);
  const parts = [`距${formatMetric(distance)}`];
  if (range.known || inside.known)
    parts.push(`攻${formatMetric(range)}`);
  if (inside.known)
    parts.push(inside.value ? "範圍內" : "範圍外");
  return parts.join(" ");
}

