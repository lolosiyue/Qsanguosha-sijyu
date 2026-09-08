/** Rules identity is a compatibility contract, not authentication or a signature.
 * Both identities must describe loaded content; never infer either from a card count.
 */
export interface RulesIdentity {
  schema_version: 1;
  profile: string;
  protocol_major: number;
  bridge_api: number;
  rules_code_sha256: string;
  lua_content_sha256: string;
  lua_bindings_sha256: string;
  card_registry_sha256: string;
  cpp_packages: string[];
  interaction_schemas: Record<string, number>;
}

export interface RulesIdentityComparison {
  compatible: boolean;
  reason: string;
}

const hashes = ["rules_code_sha256", "lua_content_sha256", "lua_bindings_sha256",
  "card_registry_sha256"] as const;
const fields = ["schema_version", "profile", "protocol_major", "bridge_api",
  ...hashes, "cpp_packages", "interaction_schemas"];
const name = /^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}(?![\s\S])/;
const hash = /^[0-9a-f]{64}(?![\s\S])/;
const object = (value: unknown): value is Record<string, unknown> =>
  value !== null && typeof value === "object" && !Array.isArray(value);
const version = (value: unknown): value is number =>
  typeof value === "number" && Number.isSafeInteger(value) && value >= 1 && value <= 2147483647;

export function isRulesIdentity(value: unknown): value is RulesIdentity {
  if (!object(value) || Object.keys(value).length !== fields.length
      || !fields.every(key => Object.prototype.hasOwnProperty.call(value, key))
      || value.schema_version !== 1 || typeof value.profile !== "string" || !name.test(value.profile)
      || !version(value.protocol_major) || !version(value.bridge_api)
      || !hashes.every(key => typeof value[key] === "string" && hash.test(value[key] as string))
      || !Array.isArray(value.cpp_packages) || value.cpp_packages.length < 1
      || value.cpp_packages.length > 512
      || !value.cpp_packages.every(item => typeof item === "string" && name.test(item))
      || new Set(value.cpp_packages).size !== value.cpp_packages.length
      || !object(value.interaction_schemas)) return false;
  const schemas = Object.entries(value.interaction_schemas);
  return schemas.length >= 1 && schemas.length <= 128
    && schemas.every(([key, item]) => name.test(key) && version(item));
}

/** Pure comparison: object key order is irrelevant; package order is significant.
 * No fallback on version text, card count, a subset of hashes, or absent metadata.
 */
export function compareRulesIdentity(local: unknown, server: unknown): RulesIdentityComparison {
  const fail = (reason: string): RulesIdentityComparison => ({ compatible: false, reason });
  if (!isRulesIdentity(local)) return fail("rules_identity_invalid_local");
  if (local.protocol_major !== 2 || local.bridge_api !== 1)
    return fail("rules_identity_unsupported_local_contract");
  if (server === undefined || server === null) return fail("rules_identity_missing_server");
  if (!isRulesIdentity(server)) return fail("rules_identity_invalid_server");
  for (const key of ["profile", "protocol_major", "bridge_api", ...hashes] as const) {
    if (local[key] !== server[key]) return fail(`rules_identity_mismatch:${key}`);
  }
  if (local.cpp_packages.length !== server.cpp_packages.length
      || local.cpp_packages.some((item, index) => item !== server.cpp_packages[index]))
    return fail("rules_identity_mismatch:cpp_packages");
  const left = Object.keys(local.interaction_schemas).sort();
  const right = Object.keys(server.interaction_schemas).sort();
  if (left.length !== right.length || left.some((key, index) => key !== right[index]
      || local.interaction_schemas[key] !== server.interaction_schemas[key]))
    return fail("rules_identity_mismatch:interaction_schemas");
  return { compatible: true, reason: "" };
}
