import { validateContentManifest } from "./rules-content";

let activeAliases = new Map<string, string>();
let activePackageIds = new Set<string>();
const PACKAGE_URI_ROOTS = new Set(["image", "audio", "lua", "translation", "data"]);

export function resetPackageAssets(): void {
  activeAliases = new Map();
  activePackageIds = new Set();
}

/** Install aliases from the verified content descriptor accepted for this client session. */
export function installPackageAssets(manifestValue: unknown): void {
  const manifest = validateContentManifest(manifestValue);
  const aliases = new Map<string, string>();
  const packageIds = new Set<string>();
  if (manifest.schema_version === 3) {
    for (const item of manifest.runtime_content.packages) {
      packageIds.add(item.id);
      for (const [key, target] of Object.entries(item.assets))
        aliases.set(key.toLowerCase(), packageRoute(item.id, target));
    }
  }
  activeAliases = aliases;
  activePackageIds = packageIds;
}

/** Resolve an engine logical image/audio name through its verified package alias. */
export function packageAssetAlias(logicalPath: string): string | null {
  const normalized = logicalPath.replace(/^\/+/, "");
  if (!safeRelativePath(normalized)) return null;
  return activeAliases.get(normalized.toLowerCase()) ?? null;
}

/** Resolve an explicit package://id/path URI without searching another package. */
export function packageAssetUri(uri: string): string | null {
  if (!uri.startsWith("package://")) return null;
  const remainder = uri.slice("package://".length);
  const slash = remainder.indexOf("/");
  if (slash <= 0) return null;
  const id = remainder.slice(0, slash);
  const path = remainder.slice(slash + 1);
  if (!activePackageIds.has(id) || !safeRelativePath(path)) return null;
  const mappedPath = path.startsWith("general/") ? `image/${path}` : path;
  const root = mappedPath.split("/", 1)[0];
  if (!PACKAGE_URI_ROOTS.has(root) || !mappedPath.startsWith(`${root}/`)) return null;
  return packageRoute(id, mappedPath);
}

export function resolveAssetSource(source: string): string | null {
  if (source.startsWith("package://")) return packageAssetUri(source);
  if (source.startsWith("image/") || source.startsWith("audio/")) return packageAssetAlias(source);
  return null;
}

function safeRelativePath(path: string): boolean {
  return path.length > 0 && !path.startsWith("/") && !path.includes("\\")
    && /^[A-Za-z0-9_./-]+$/.test(path)
    && path.split("/").every(part => part !== "" && part !== "." && part !== "..");
}

function packageRoute(id: string, path: string): string {
  return `/packages/${encodeURIComponent(id)}/${path.split("/").map(encodeURIComponent).join("/")}`;
}
