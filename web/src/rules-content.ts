import { sha256 } from "./rules-identity";

export type ContentRole = "rules" | "presentation" | "ai";
export interface ContentEntry { path: string; role: ContentRole; size: number; sha256: string }
export interface ContentManifest { schema_version: 1; profile: "declared-v1"; files: ContentEntry[] }

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
  if (object.schema_version !== 1 || object.profile !== "declared-v1")
    throw new Error("rules_content_unsupported");
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
  return { schema_version: 1, profile: "declared-v1", files };
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

export function installContent(fs: ContentFs, files: Map<string, Uint8Array>): void {
  if (!fs.unlink || !fs.rmdir) throw new Error("rules_reload_required");
  clearDirectory(fs, "/assets");
  fs.mkdirTree("/assets");
  for (const [path, bytes] of files) {
    const full = `/assets/${path}`;
    fs.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    fs.writeFile(full, bytes);
  }
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
      else if (fs.isFile(stat.mode)) actual.push(path.slice("/assets/".length));
      else throw new Error("rules_reload_required");
    }
  }
  inventory("/assets");
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
