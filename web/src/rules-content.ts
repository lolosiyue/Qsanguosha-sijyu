import { canonical, sha256 } from "./rules-identity";

export type ContentRole = "rules" | "presentation" | "ai";
export interface ContentEntry { path: string; role: ContentRole; size: number; sha256: string }
export interface RuntimeExtension { name: string; script: string; dependencies: string[]; libs: string[]; lang: string[]; ai: string[] }
export interface RuntimePackage { id: string; version: string; dependencies: string[]; assets: Record<string, string> }
export interface RuntimeContentV2 { schema_version: 2; profile: "declared-v2"; extensions: RuntimeExtension[] }
export interface RuntimeContentV3 { schema_version: 3; profile: "packages-v1"; extensions: RuntimeExtension[]; packages: RuntimePackage[] }
export type RuntimeContent = RuntimeContentV2 | RuntimeContentV3;
export interface ContentManifestV2 { schema_version: 2; profile: "declared-v2"; runtime_content: RuntimeContentV2; files: ContentEntry[] }
export interface ContentManifestV3 { schema_version: 3; profile: "packages-v1"; runtime_content: RuntimeContentV3; files: ContentEntry[] }
export type ContentManifest = ContentManifestV2 | ContentManifestV3;

const CORE = new Set(["lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
  "lua/sgs_ex.lua", "lua/lib/json.lua"]);
const PATH = /^[A-Za-z0-9_./-]+\.lua$/;
const PACKAGE_ID = /^[a-z0-9][a-z0-9_-]*$/;

function validPath(path: string): boolean {
  return path.length > 0 && path[0] !== "/" && PATH.test(path)
    && path.split("/").every(part => part !== "" && part !== "." && part !== "..")
    && !path.includes("\\");
}

function safeParts(path: string): boolean {
  return path.length > 0 && !path.startsWith("/") && !path.includes("\\")
    && !/[\u0000-\u001f%?#]/.test(path)
    && path.split("/").every(part => part !== "" && part !== "." && part !== "..");
}

function packageLuaRole(path: string, role: ContentRole, ids: Set<string>): boolean {
  const match = /^packages\/([^/]+)\/lua\/(.+\.lua)$/.exec(path);
  if (!match || !ids.has(match[1])) return false;
  const remainder = match[2];
  if (role === "ai") return remainder.startsWith("ai/");
  if (role === "presentation") return false;
  return !remainder.startsWith("ai/");
}

function validRolePath(entry: ContentEntry, ids: Set<string>): boolean {
  if (entry.path.startsWith("packages/")) {
    if (entry.role === "presentation") {
      const match = /^packages\/([^/]+)\/translation\/.+\.lua$/.exec(entry.path);
      return !!match && ids.has(match[1]);
    }
    return packageLuaRole(entry.path, entry.role, ids);
  }
  if (entry.role === "ai") return entry.path.startsWith("lua/ai/")
    || entry.path === "lua/lib/middleclass.lua";
  if (entry.role === "presentation") return entry.path.startsWith("lang/");
  return CORE.has(entry.path) || /^extensions\/[^/]+\.lua$/.test(entry.path)
    || (entry.path.startsWith("lua/") && !entry.path.startsWith("lua/ai/")
      && entry.path !== "lua/lib/middleclass.lua");
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function validatePackages(value: unknown): RuntimePackage[] {
  if (!Array.isArray(value) || value.length > 4096) throw new Error("rules_content_unsupported");
  const byId = new Map<string, RuntimePackage>();
  const ids = new Set<string>();
  const folded = new Set<string>();
  const allAssetKeys = new Set<string>();
  const packages: RuntimePackage[] = [];
  for (const raw of value) {
    if (!record(raw) || typeof raw.id !== "string" || !PACKAGE_ID.test(raw.id)
        || typeof raw.version !== "string" || !raw.version
        || !Array.isArray(raw.dependencies) || !raw.dependencies.every(x => typeof x === "string")
        || !record(raw.assets)) throw new Error("rules_content_unsupported");
    const id = raw.id;
    if (folded.has(id.toLowerCase())) throw new Error("rules_content_unsupported");
    folded.add(id.toLowerCase());
    ids.add(id);
    const assets: Record<string, string> = {};
    const keys = new Set<string>();
    for (const [key, target] of Object.entries(raw.assets)) {
      const lower = key.toLowerCase();
      if ((!key.startsWith("image/") && !key.startsWith("audio/")) || !safeParts(key)
          || !key.slice(key.indexOf("/") + 1) || keys.has(lower) || typeof target !== "string"
          || !safeParts(target) || (key.startsWith("image/") && !target.startsWith("image/"))
          || (key.startsWith("audio/") && !target.startsWith("audio/")) || allAssetKeys.has(lower))
        throw new Error("rules_content_unsupported");
      keys.add(lower);
      allAssetKeys.add(lower);
      assets[key] = target;
    }
    const dependencies = raw.dependencies as string[];
    if (new Set(dependencies.map(x => x.toLowerCase())).size !== dependencies.length
        || dependencies.some(dep => !PACKAGE_ID.test(dep) || dep.toLowerCase() === id.toLowerCase()))
      throw new Error("rules_content_unsupported");
    const item = { id, version: raw.version, dependencies: [...dependencies], assets };
    byId.set(id, item);
    packages.push(item);
  }
  for (const item of packages) {
  }
  const emitted = new Set<string>();
  for (const item of packages) {
    if (item.dependencies.some(dep => !byId.has(dep) || !emitted.has(dep)))
      throw new Error("rules_content_unsupported");
    emitted.add(item.id);
  }
  return packages;
}

export function validateContentManifest(value: unknown): ContentManifest {
  if (!record(value)) throw new Error("rules_content_unsupported");
  const isV2 = value.schema_version === 2 && value.profile === "declared-v2";
  const isV3 = value.schema_version === 3 && value.profile === "packages-v1";
  if (!isV2 && !isV3) throw new Error("rules_content_unsupported");
  const version = isV2 ? 2 : 3;
  const runtime = value.runtime_content;
  if (!record(runtime) || runtime.schema_version !== version || runtime.profile !== value.profile
      || !Array.isArray(runtime.extensions)) throw new Error("rules_content_unsupported");
  const packages = isV3 ? validatePackages(runtime.packages) : [];
  const packageIds = new Set(packages.map(item => item.id));
  const names = new Set<string>();
  const foldedNames = new Set<string>();
  const declaredPaths = new Set<string>();
  const foldedDeclaredPaths = new Set<string>();
  const runtimeExtensions: RuntimeExtension[] = [];
  for (const raw of runtime.extensions) {
    if (!record(raw)) throw new Error("rules_content_unsupported");
    const { name, script, dependencies, libs, lang, ai } = raw;
    if (typeof name !== "string" || !name || names.has(name)
        || (isV3 && foldedNames.has(name.toLowerCase())) || typeof script !== "string"
        || !Array.isArray(dependencies) || !Array.isArray(libs) || !Array.isArray(lang) || !Array.isArray(ai)
        || !dependencies.every(item => typeof item === "string") || !libs.every(item => typeof item === "string")
        || !lang.every(item => typeof item === "string") || !ai.every(item => typeof item === "string"))
      throw new Error("rules_content_unsupported");
    const validDeclaredPath = (path: string, role: "script" | "libs" | "lang" | "ai") => {
      const foldedPath = path.toLowerCase();
      if (!validPath(path) || declaredPaths.has(path) || (isV3 && foldedDeclaredPaths.has(foldedPath))) return false;
      let permitted: boolean;
      if (path.startsWith("packages/")) {
        const packageRole: ContentRole = role === "ai" ? "ai" : role === "lang" ? "presentation" : "rules";
        permitted = validRolePath({ path, role: packageRole, size: 0, sha256: "" }, packageIds);
      } else {
        permitted = role === "script" ? /^extensions\/[^/]+\.lua$/.test(path)
          : role === "libs" ? path.startsWith("lua/") && !path.startsWith("lua/ai/")
            && !CORE.has(path) && path !== "lua/lib/middleclass.lua"
          : role === "lang" ? path.startsWith("lang/")
          : path.startsWith("lua/ai/") || path === "lua/lib/middleclass.lua";
      }
      if (permitted) {
        declaredPaths.add(path);
        foldedDeclaredPaths.add(foldedPath);
      }
      return permitted;
    };
    if (!validDeclaredPath(script, "script")
        || !(libs as string[]).every(path => validDeclaredPath(path, "libs"))
        || !(lang as string[]).every(path => validDeclaredPath(path, "lang"))
        || !(ai as string[]).every(path => validDeclaredPath(path, "ai")))
      throw new Error("rules_content_unsupported");
    const seenDependencies = new Set<string>();
    for (const dependency of dependencies as string[]) {
      if (!names.has(dependency) || seenDependencies.has(dependency)) throw new Error("rules_content_unsupported");
      seenDependencies.add(dependency);
    }
    names.add(name);
    foldedNames.add(name.toLowerCase());
    runtimeExtensions.push({ name, script, dependencies: [...dependencies as string[]],
      libs: [...libs as string[]], lang: [...lang as string[]], ai: [...ai as string[]] });
  }
  if (!Array.isArray(value.files) || value.files.length === 0 || value.files.length > 8192)
    throw new Error("rules_content_unsupported");
  const seen = new Set<string>();
  const foldedPaths = new Set<string>();
  const files: ContentEntry[] = [];
  for (const raw of value.files) {
    if (!record(raw)) throw new Error("rules_content_unsupported");
    const { path, role, size, sha256: digest } = raw;
    if (typeof path !== "string" || !validPath(path)
        || (role !== "rules" && role !== "presentation" && role !== "ai")
        || !Number.isSafeInteger(size) || Number(size) < 0 || Number(size) > 64 * 1024 * 1024
        || typeof digest !== "string" || !/^[0-9a-f]{64}$/.test(digest) || seen.has(path)
        || (isV3 && foldedPaths.has(path.toLowerCase()))) throw new Error("rules_content_unsupported");
    const typed = { path, role, size: Number(size), sha256: digest } as ContentEntry;
    if (!validRolePath(typed, packageIds)) throw new Error("rules_content_unsupported");
    if (isV3 && path.startsWith("packages/") && !declaredPaths.has(path))
      throw new Error("rules_content_unsupported");
    seen.add(path);
    foldedPaths.add(path.toLowerCase());
    files.push(typed);
  }
  for (const path of CORE) {
    const entry = files.find(item => item.path === path);
    if (!entry || entry.role !== "rules") throw new Error("rules_content_unsupported");
  }
  return isV2
    ? { schema_version: 2, profile: "declared-v2", runtime_content: {
      schema_version: 2, profile: "declared-v2", extensions: runtimeExtensions }, files }
    : { schema_version: 3, profile: "packages-v1", runtime_content: {
      schema_version: 3, profile: "packages-v1", extensions: runtimeExtensions, packages }, files };
}

export type ContentFetcher = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

export async function fetchContent(manifestValue: unknown, fetcher: ContentFetcher = fetch, includeAi = false): Promise<Map<string, Uint8Array>> {
  const manifest = validateContentManifest(manifestValue);
  const result = new Map<string, Uint8Array>();
  for (const entry of manifest.files) {
    if (entry.role === "ai" && !includeAi) continue;
    let response: Response;
    try {
      response = await fetcher(`/rules/content/${entry.sha256}`, {
        cache: "force-cache", credentials: "omit", redirect: "error"
      });
    } catch { throw new Error("rules_reload_required"); }
    if (!response.ok || response.redirected) throw new Error("rules_reload_required");
    let bytes: Uint8Array;
    try { bytes = new Uint8Array(await response.arrayBuffer()); }
    catch { throw new Error("rules_reload_required"); }
    if (bytes.length !== entry.size || await sha256(bytes) !== entry.sha256)
      throw new Error("rules_reload_required");
    result.set(entry.path, bytes);
  }
  return result;
}

export interface ContentFs {
  mkdirTree(path: string): void;
  writeFile(path: string, bytes: Uint8Array): void;
  readFile(path: string): Uint8Array;
  readdir(path: string): string[];
  lstat(path: string): { mode: number };
  isDir(mode: number): boolean;
  isFile(mode: number): boolean;
  unlink?(path: string): void;
  rmdir?(path: string): void;
}

export function installContent(fs: ContentFs, files: Map<string, Uint8Array>,
                               runtimeContent?: RuntimeContent): void {
  if (!fs.unlink || !fs.rmdir) throw new Error("rules_reload_required");
  clearDirectory(fs, "/assets");
  fs.mkdirTree("/assets");
  for (const [path, bytes] of files) {
    const full = `/assets/${path}`;
    fs.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    fs.writeFile(full, bytes);
  }
  if (runtimeContent)
    fs.writeFile("/assets/runtime-content.json", new TextEncoder().encode(JSON.stringify(runtimeContent)));
}

function clearDirectory(fs: ContentFs, directory: string): void {
  if (!fs.unlink || !fs.rmdir) throw new Error("rules_reload_required");
  let names: string[];
  try { names = fs.readdir(directory); } catch { return; }
  for (const name of names) {
    if (name === "." || name === "..") continue;
    const path = `${directory}/${name}`;
    const stat = fs.lstat(path);
    if (fs.isDir(stat.mode)) { clearDirectory(fs, path); fs.rmdir(path); }
    else if (fs.isFile(stat.mode)) fs.unlink(path);
    else throw new Error("rules_reload_required");
  }
}

export async function verifyInstalledContent(fs: ContentFs, manifestValue: unknown, includeAi = false): Promise<void> {
  const manifest = validateContentManifest(manifestValue);
  const expected = new Set(manifest.files.filter(entry => includeAi || entry.role !== "ai").map(entry => entry.path));
  const actual: string[] = [];
  function inventory(directory: string): void {
    for (const name of fs.readdir(directory)) {
      if (name === "." || name === "..") continue;
      const path = `${directory}/${name}`;
      const stat = fs.lstat(path);
      if (fs.isDir(stat.mode)) inventory(path);
      else if (fs.isFile(stat.mode)) {
        if (path !== "/assets/runtime-content.json") actual.push(path.slice("/assets/".length));
      } else throw new Error("rules_reload_required");
    }
  }
  inventory("/assets");
  try {
    const path = "/assets/runtime-content.json";
    if (!fs.isFile(fs.lstat(path).mode)) throw new Error();
    const bytes = fs.readFile(path);
    const installed: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    if (canonical(installed) !== canonical(manifest.runtime_content)) throw new Error();
  } catch { throw new Error("rules_reload_required"); }
  if (actual.length !== expected.size || actual.some(path => !expected.has(path)))
    throw new Error("rules_reload_required");
  for (const entry of manifest.files) {
    if (entry.role === "ai" && !includeAi) continue;
    const path = `/assets/${entry.path}`;
    const stat = fs.lstat(path);
    if (!fs.isFile(stat.mode)) throw new Error("rules_reload_required");
    const bytes = new Uint8Array(fs.readFile(path));
    if (bytes.length !== entry.size || await sha256(bytes) !== entry.sha256)
      throw new Error("rules_reload_required");
  }
}
