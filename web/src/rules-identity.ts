import { isObject, type JsonObject } from "./protocol";

export const RULES_BRIDGE_SCHEMA = 2;
const hash = /^[0-9a-f]{64}$/;
const hashKeys = ["bundle_id", "code_id", "cpp_hash", "card_registry_hash", "lua_hash", "bindings_abi"];

export function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value !== null && typeof value === "object") {
    const object = value as Record<string, unknown>;
    return `{${Object.keys(object).sort().map(key => `${JSON.stringify(key)}:${canonical(object[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

export function isRulesIdentity(value: unknown): value is JsonObject {
  return isObject(value) && canonical(value).length <= 32768
    && value.schema_version === 1 && value.protocol_version === 2
    && Number.isSafeInteger(value.bridge_schema) && Number(value.bridge_schema) > 0
    && typeof value.ruleset === "string" && value.ruleset.length > 0
    && typeof value.content_profile === "string" && value.content_profile.length > 0
    && hashKeys.every(key => typeof value[key] === "string" && hash.test(value[key] as string))
    && Array.isArray(value.packages) && value.packages.length > 0 && value.packages.length <= 512
    && value.packages.every(name => typeof name === "string" && name.length > 0)
    && new Set(value.packages).size === value.packages.length
    && isObject(value.interaction_schemas) && Object.keys(value.interaction_schemas).length > 0
    && Object.keys(value.interaction_schemas).length <= 256
    && Object.entries(value.interaction_schemas).every(([key, digest]) => key.length > 0
      && typeof digest === "string" && hash.test(digest));
}

export async function sha256(bytes: Uint8Array): Promise<string> {
  const owned = new Uint8Array(bytes.byteLength);
  owned.set(bytes);
  const digest = await crypto.subtle.digest("SHA-256", owned.buffer);
  return [...new Uint8Array(digest)].map(value => value.toString(16).padStart(2, "0")).join("");
}

export async function verifyNativeIdentity(value: unknown): Promise<JsonObject> {
  if (!isRulesIdentity(value)) throw new Error("rules_identity_invalid");
  const unsigned = { ...value };
  delete unsigned.bundle_id;
  if (await sha256(new TextEncoder().encode(`qsan-rules-bundle-v1\0${canonical(unsigned)}`)) !== value.bundle_id)
    throw new Error("rules_identity_invalid");
  const code = { protocol_version: value.protocol_version, bridge_schema: value.bridge_schema,
    cpp_hash: value.cpp_hash, bindings_abi: value.bindings_abi,
    interaction_schemas: value.interaction_schemas };
  if (await sha256(new TextEncoder().encode(`qsan-rules-code-v1\0${canonical(code)}`)) !== value.code_id)
    throw new Error("rules_identity_invalid");
  if (value.content_profile !== "declared-v1") throw new Error("rules_content_unsupported");
  if (value.bridge_schema !== RULES_BRIDGE_SCHEMA) throw new Error("rules_reload_required");
  return value;
}

export function rulesCompatibilityError(server: unknown, client: unknown): string {
  if (isObject(server) && server.error_code === "rules_content_unsupported") return "rules_content_unsupported";
  if (!isRulesIdentity(server) || !isRulesIdentity(client)) return "rules_reload_required";
  if (server.content_profile !== "declared-v1" || client.content_profile !== server.content_profile)
    return "rules_content_unsupported";
  const supported = client.interaction_schemas as JsonObject;
  if (Object.entries(server.interaction_schemas as JsonObject).some(([key, value]) => supported[key] !== value))
    return "rules_interaction_unsupported";
  if (server.code_id !== client.code_id)
    return "rules_version_mismatch";
  return canonical(server) === canonical(client) ? "" : "rules_version_mismatch";
}

export function rulesErrorMessage(code: string): string {
  if (code.includes("rules_content_unsupported") || code.includes("rules_interaction_unsupported"))
    return "此內容包不受支援；請使用伺服器支援的規則套件。";
  if (code.includes("rules_version_mismatch")) return "規則版本不相符；請更新至與伺服器相同的版本。";
  if (/rules_(reload_required|identity_required|identity_invalid)/.test(code))
    return "需要重新載入；請重新整理頁面以載入配對的規則套件。";
  return code;
}

// Deployment metadata binds artifacts, not trust. Callers provide only files
// fetched from the fixed controlled /rules/ location, never server-supplied URLs.
export async function verifyDeployment(value: unknown, files: Record<string, Uint8Array>): Promise<void> {
  if (!isObject(value) || value.schema_version !== 1 || value.bridge_schema !== RULES_BRIDGE_SCHEMA
      || !isObject(value.files) || canonical(Object.keys(value.files).sort()) !== canonical(Object.keys(files).sort()))
    throw new Error("rules_reload_required");
  for (const [name, bytes] of Object.entries(files)) {
    if (value.files[name] !== await sha256(bytes)) throw new Error("rules_reload_required");
  }
}

// Pin the complete deployment even when a replacement has a valid manifest
// and the same bridge version. An old compiled Web loader must still reload.
export async function verifyDeploymentBundle(bytes: Uint8Array, expectedId: string,
    files: Record<string, Uint8Array>): Promise<void> {
  if (!expectedId || await sha256(bytes) !== expectedId) throw new Error("rules_reload_required");
  let manifest: unknown;
  try { manifest = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)); }
  catch { throw new Error("rules_reload_required"); }
  await verifyDeployment(manifest, files);
}
