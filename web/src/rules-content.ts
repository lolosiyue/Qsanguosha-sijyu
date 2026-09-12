import { canonical, sha256 } from "./rules-identity";

export type ContentRole = "rules" | "presentation" | "ai";
export interface ContentEntry { path: string; role: ContentRole; size: number; sha256: string }
export interface RuntimeExtension { name: string; script: string; dependencies: string[]; libs: string[]; lang: string[]; ai: string[] }
export interface RuntimeContent { schema_version: 2; profile: "declared-v2"; extensions: RuntimeExtension[] }
export interface ContentManifest { schema_version: 2; profile: "declared-v2"; runtime_content: RuntimeContent; files: ContentEntry[] }

const CORE = new Set(["lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
  "lua/sgs_ex.lua", "lua/lib/json.lua"]);
const PATH = /^[A-Za-z0-9_./-]+\.lua$/;

function validPath(path: string): boolean {
  return path.length > 0 && path[0] !== "/" && PATH.test(path)
    && path.split("/").every(part => part !== "" && part !== "." && part !== "..")
    && !path.includes("\\");
}

function validRolePath(entry: ContentEntry): boolean {
  if (entry.role === "ai") return entry.path.startsWith("lua/ai/")
    || entry.path === "lua/lib/middleclass.lua";
  if (entry.role === "presentation") return entry.path.startsWith("lang/");
  return CORE.has(entry.path) || /^extensions\/[^/]+\.lua$/.test(entry.path)
    || (entry.path.startsWith("lua/") && !entry.path.startsWith("lua/ai/")
      && entry.path !== "lua/lib/middleclass.lua");
}

export function validateContentManifest(value: unknown): ContentManifest {
  if (value === null || typeof value !== "object" || Array.isArray(value))
    throw new Error("rules_content_unsupported");
  const object = value as Record<string, unknown>;
  if (object.schema_version !== 2 || object.profile !== "declared-v2")
    throw new Error("rules_content_unsupported");
  const runtime = object.runtime_content;
  if (runtime === null || typeof runtime !== "object" || Array.isArray(runtime))
    throw new Error("rules_content_unsupported");
  const runtimeObject = runtime as Record<string, unknown>;
  if (runtimeObject.schema_version !== 2 || runtimeObject.profile !== "declared-v2"
      || !Array.isArray(runtimeObject.extensions)) throw new Error("rules_content_unsupported");
  const names = new Set<string>();
  const declaredPaths = new Set<string>();
  const runtimeExtensions: RuntimeExtension[] = [];
  for (const raw of runtimeObject.extensions) {
    if (raw === null || typeof raw !== "object" || Array.isArray(raw)) throw new Error("rules_content_unsupported");
    const extension = raw as Record<string, unknown>;
    const name = extension.name;
    const script = extension.script;
    const dependencies = extension.dependencies;
    const libs = extension.libs;
    const lang = extension.lang;
    const ai = extension.ai;
    if (typeof name !== "string" || !name || names.has(name) || typeof script !== "string"
        || !Array.isArray(dependencies) || !Array.isArray(libs) || !Array.isArray(lang) || !Array.isArray(ai)
        || !dependencies.every(item => typeof item === "string") || !libs.every(item => typeof item === "string")
        || !lang.every(item => typeof item === "string") || !ai.every(item => typeof item === "string"))
      throw new Error("rules_content_unsupported");
    const validDeclaredPath = (path: string, role: "script" | "libs" | "lang" | "ai") => {
      if (!validPath(path) || declaredPaths.has(path)) return false;
      const permitted = role === "script" ? /^extensions\/[^/]+\.lua$/.test(path)
        : role === "libs" ? path.startsWith("lua/") && !path.startsWith("lua/ai/")
          && !CORE.has(path) && path !== "lua/lib/middleclass.lua"
        : role === "lang" ? path.startsWith("lang/")
        : path.startsWith("lua/ai/") || path === "lua/lib/middleclass.lua";
      if (permitted) declaredPaths.add(path);
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
    runtimeExtensions.push({ name, script, dependencies: [...dependencies as string[]],
      libs: [...libs as string[]], lang: [...lang as string[]], ai: [...ai as string[]] });
  }
  if (!Array.isArray(object.files) || object.files.length === 0 || object.files.length > 8192)
    throw new Error("rules_content_unsupported");
  const seen = new Set<string>();
  const files: ContentEntry[] = [];
  for (const raw of object.files) {
    if (raw === null || typeof raw !== "object" || Array.isArray(raw))
      throw new Error("rules_content_unsupported");
    const entry = raw as Record<string, unknown>;
    const role = entry.role;
    if (typeof entry.path !== "string" || !validPath(entry.path)
        || (role !== "rules" && role !== "presentation" && role !== "ai")
        || !Number.isSafeInteger(entry.size) || Number(entry.size) < 0
        || Number(entry.size) > 64 * 1024 * 1024 || typeof entry.sha256 !== "string"
        || !/^[0-9a-f]{64}$/.test(entry.sha256) || seen.has(entry.path))
      throw new Error("rules_content_unsupported");
    const typed = { path: entry.path, role, size: entry.size, sha256: entry.sha256 } as ContentEntry;
    if (!validRolePath(typed)) throw new Error("rules_content_unsupported");
    seen.add(typed.path);
    files.push(typed);
  }
  for (const path of CORE) {
    const entry = files.find(item => item.path === path);
    if (!entry || entry.role !== "rules") throw new Error("rules_content_unsupported");
  }
  return { schema_version: 2, profile: "declared-v2",
    runtime_content: { schema_version: 2, profile: "declared-v2", extensions: runtimeExtensions }, files };
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
    } catch {
      throw new Error("rules_reload_required");
    }
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
    fs.writeFile("/assets/runtime-content.json",
      new TextEncoder().encode(JSON.stringify(runtimeContent)));
}

function clearDirectory(fs: ContentFs, directory: string): void {
  if (!fs.unlink || !fs.rmdir) throw new Error("rules_reload_required");
  let names: string[];
  try { names = fs.readdir(directory); } catch { return; }
  for (const name of names) {
    if (name === "." || name === "..") continue;
    const path = `${directory}/${name}`;
    const stat = fs.lstat(path);
    if (fs.isDir(stat.mode)) {
      clearDirectory(fs, path);
      fs.rmdir(path);
    } else if (fs.isFile(stat.mode)) {
      fs.unlink(path);
    } else {
      throw new Error("rules_reload_required");
    }
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
      }
      else throw new Error("rules_reload_required");
    }
  }
  inventory("/assets");
  // This sidecar selects the rules that Engine actually loads. Exclude it
  // from the Lua inventory, but bind its contents to the validated manifest.
  try {
    const path = "/assets/runtime-content.json";
    if (!fs.isFile(fs.lstat(path).mode)) throw new Error();
    const bytes = fs.readFile(path);
    const installed: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    if (canonical(installed) !== canonical(manifest.runtime_content)) throw new Error();
  } catch {
    throw new Error("rules_reload_required");
  }
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
