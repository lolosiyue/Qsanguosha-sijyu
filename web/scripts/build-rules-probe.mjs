import { build } from "vite";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
const web = fileURLToPath(new URL("../", import.meta.url));
if (!process.argv[2]) throw new Error("output directory required");
await build({ configFile: resolve(web, "vite.config.ts"), root: resolve(web, "../tests/client_runtime/runtime-browser"),
  base: "./", build: { outDir: resolve(process.argv[2]), emptyOutDir: false, target: "es2022" } });
