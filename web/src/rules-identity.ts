// Compatibility only: matching digests are not authentication or server trust.
export interface RulesIdentity {
  schema_version: 1;
  available: true;
  profile: "builtin-v1";
  protocol_version: 2;
  bridge_schema: 1;
  rules_abi: "qsan-client-rules-v1";
  source_sha256: string;
  bindings_sha256: string;
  lua_sha256: string;
  card_registry_sha256: string;
  card_count: number;
  packages: string[];
  interaction_schemas: Record<string, number>;
}
const hashes = ["source_sha256", "bindings_sha256", "lua_sha256", "card_registry_sha256"] as const;
function object(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
export function readRulesIdentity(value: unknown): RulesIdentity | null {
  if (!object(value) || value.schema_version !== 1 || value.available !== true
      || value.profile !== "builtin-v1" || value.protocol_version !== 2 || value.bridge_schema !== 1
      || value.rules_abi !== "qsan-client-rules-v1" || !Number.isSafeInteger(value.card_count)
      || Number(value.card_count) <= 0 || !hashes.every(key => typeof value[key] === "string"
        && /^[0-9a-f]{64}$/.test(value[key] as string))
      || !Array.isArray(value.packages) || value.packages.length === 0 || value.packages.length > 4096
      || !value.packages.every(name => typeof name === "string" && name.length > 0 && name.length <= 256)
      || new Set(value.packages).size !== value.packages.length || !object(value.interaction_schemas)
      || value.interaction_schemas["card-selection"] !== 1
      || !Object.entries(value.interaction_schemas).every(([key, version]) => key.length > 0
        && key.length <= 128 && Number.isSafeInteger(version) && Number(version) > 0))
    return null;
  return value as unknown as RulesIdentity;
}

// Return a stable diagnostic, never fall back to game_version/card_count.
export function rulesIdentityError(server: unknown, runtime: unknown): string {
  const remote = readRulesIdentity(server);
  if (!remote) return "server_rules_identity_missing_or_unsupported";
  const local = readRulesIdentity(runtime);
  if (!local) return "runtime_rules_identity_missing_or_unsupported";
  for (const key of hashes) {
    if (remote[key] !== local[key]) return `rules_identity_mismatch:${key}`;
  }
  if (remote.card_count !== local.card_count) return "rules_identity_mismatch:card_count";
  if (JSON.stringify(remote.packages) !== JSON.stringify(local.packages))
    return "rules_identity_mismatch:packages";
  const keys = Object.keys(remote.interaction_schemas).sort();
  if (JSON.stringify(keys) !== JSON.stringify(Object.keys(local.interaction_schemas).sort())
      || keys.some(key => remote.interaction_schemas[key] !== local.interaction_schemas[key]))
    return "rules_identity_mismatch:interaction_schemas";
  return "";
}
