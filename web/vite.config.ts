import fs from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { IncomingMessage, ServerResponse } from "node:http";
import { defineConfig, type Plugin } from "vite";

const root = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(root, "..");
const imageRoot = path.resolve(repoRoot, "image");
// Runtime package trees are supplied at launch; source defaults keep local dev simple.
const runtimeRoot = path.resolve(process.env.QSAN_RUNTIME_ROOT ?? repoRoot);
const packageRoot = path.resolve(runtimeRoot, "packages");
// Bind this Web loader to the deployment emitted by the native/WASM build.
// An unprovisioned frontend can build, but cannot enable native rules at runtime.
const bundlePath = path.resolve(root, "public/rules/qsanguosha_client_wasm.bundle.json");
const deploymentId = fs.existsSync(bundlePath)
  ? createHash("sha256").update(fs.readFileSync(bundlePath)).digest("hex") : "";

const MIME: Record<string, string> = {
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".webp": "image/webp",
  ".avif": "image/avif",
  ".bmp": "image/bmp",
  ".ico": "image/x-icon",
  ".svg": "image/svg+xml",
  ".gif": "image/gif",
  ".ogg": "audio/ogg",
  ".mp3": "audio/mpeg",
  ".wav": "audio/wav",
  ".flac": "audio/flac",
  ".m4a": "audio/mp4",
  ".mp4": "video/mp4",
  ".webm": "video/webm",
  ".ttf": "font/ttf",
  ".otf": "font/otf",
  ".woff": "font/woff",
  ".woff2": "font/woff2",
  ".lua": "text/plain; charset=utf-8",
  ".json": "application/json; charset=utf-8"
};

function isInsideRoot(file: string, dir: string): boolean {
  const resolved = path.resolve(file);
  const base = path.resolve(dir);
  if (process.platform === "win32")
    return resolved.toLowerCase().startsWith(base.toLowerCase() + path.sep);
  return resolved.startsWith(base + path.sep);
}

function assetRelativePath(req: IncomingMessage): string {
  let raw = "";
  try { raw = decodeURIComponent((req.url ?? "").split("?")[0] ?? ""); }
  catch { return ""; }
  raw = raw.replace(/^[/\\]+/, "");
  return raw.replace(/^assets[/\\]/, "");
}

function servePackageFile(req: IncomingMessage, res: ServerResponse, _next: () => void): void {
  // Package misses must remain HTTP errors, rather than falling through to the SPA.
  const reject = (status: number): void => { res.statusCode = status; res.end(); };
  const relative = assetRelativePath(req).replace(/^packages[/\\]/, "");
  if (!/^[A-Za-z0-9_./-]+$/.test(relative)
      || relative.split("/").some(part => !part || part === "." || part === "..")) { reject(403); return; }
  const file = path.resolve(packageRoot, relative);
  if (!isInsideRoot(file, packageRoot)) { reject(403); return; }
  if (!fs.existsSync(file)) { reject(404); return; }
  try {
    const realRoot = fs.realpathSync(packageRoot);
    const realFile = fs.realpathSync(file);
    if (!isInsideRoot(realFile, realRoot)) { reject(403); return; }
    if (!fs.statSync(realFile).isFile()) { reject(404); return; }
    const ext = path.extname(realFile).toLowerCase();
    if (!(ext in MIME)) { reject(404); return; }
    res.setHeader("Content-Type", MIME[ext]);
    fs.createReadStream(realFile).pipe(res);
  } catch { reject(404); }
}

function serveLocalImage(req: IncomingMessage, res: ServerResponse, next: () => void): void {
  const relative = assetRelativePath(req);
  if (!relative || relative.includes("..")) {
    next();
    return;
  }
  const file = path.resolve(imageRoot, relative);
  if (!isInsideRoot(file, imageRoot) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    next();
    return;
  }
  const ext = path.extname(file).toLowerCase();
  if (!(ext in MIME)) {
    next();
    return;
  }
  res.setHeader("Content-Type", MIME[ext]);
  fs.createReadStream(file).pipe(res);
}

function iniValue(text: string, key: string, fallback: string): string {
  const match = text.match(new RegExp(`^${key}=(.*)$`, "m"));
  return match ? match[1].trim() : fallback;
}

function toAssetUrl(imagePath: string): string {
  if (imagePath.startsWith("image/"))
    return `/assets/${imagePath.slice("image/".length)}`;
  if (imagePath.startsWith("/assets/"))
    return imagePath;
  return imagePath;
}

function skinString(text: string, key: string, fallback = ""): string {
  const match = text.match(new RegExp(`"${key}"\\s*:\\s*"([^"]+)"`));
  return match ? match[1] : fallback;
}

function serveGameUiConfig(_req: IncomingMessage, res: ServerResponse, next: () => void): void {
  const iniPath = path.resolve(repoRoot, "config.ini");
  const skinPath = path.resolve(repoRoot, "skins/fulldefaultSkin.image.json");
  if (!fs.existsSync(iniPath)) {
    next();
    return;
  }
  const ini = fs.readFileSync(iniPath, "utf8");
  const skin = fs.existsSync(skinPath) ? fs.readFileSync(skinPath, "utf8") : "";
  const kingdoms = ["wei", "shu", "wu", "qun", "jin", "god"];
  const tableBgByKingdom: Record<string, string> = {};
  for (const kingdom of kingdoms) {
    const value = skinString(skin, `tableBg${kingdom}`);
    if (value)
      tableBgByKingdom[kingdom] = toAssetUrl(value);
  }
  const body = JSON.stringify({
    backgroundImage: toAssetUrl(iniValue(ini, "BackgroundImage", "image/system/backdrop/2.jpg")),
    tableBgImage: toAssetUrl(iniValue(ini, "TableBgImage", skinString(skin, "tableBg", "image/system/backdrop/default.jpg"))),
    enableAutoBackgroundChange: iniValue(ini, "EnableAutoBackgroundChange", "true") === "true",
    tableBgByKingdom
  });
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  res.end(body);
}

function requestPath(req: IncomingMessage): string {
  return ((req as { originalUrl?: string }).originalUrl ?? req.url ?? "").split("?")[0];
}

function localImagePlugin(): Plugin {
  return {
    name: "qsanguosha-local-images",
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (requestPath(req) === "/game-ui-config.json") {
          serveGameUiConfig(req, res, next);
          return;
        }
        next();
      });
      server.middlewares.use("/assets", serveLocalImage);
      server.middlewares.use("/packages", servePackageFile);
    },
    configurePreviewServer(server) {
      server.middlewares.use((req, res, next) => {
        if (requestPath(req) === "/game-ui-config.json") {
          serveGameUiConfig(req, res, next);
          return;
        }
        next();
      });
      server.middlewares.use("/assets", serveLocalImage);
      server.middlewares.use("/packages", servePackageFile);
    }
  };
}

export default defineConfig({
  define: { __QSAN_RULES_DEPLOYMENT_ID__: JSON.stringify(deploymentId) },
  appType: "spa",
  server: {
    host: true,
    port: 5173,
    fs: { allow: [root, path.resolve(root, "..")] }
  },
  preview: {
    host: true,
    port: 5173
  },
  plugins: [localImagePlugin()]
});
