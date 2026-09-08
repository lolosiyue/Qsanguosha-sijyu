import { build } from "vite";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
const web = fileURLToPath(new URL("../", import.meta.url));
const [outDir, bundle] = process.argv.slice(2);
if (!outDir || !bundle) throw new Error("output directory and deployment bundle required");
// Bind the Worker to the deployment it is tested against, exactly as vite.config.ts
// binds the shipped loader to the packaged one.
const deploymentId = createHash("sha256").update(readFileSync(bundle)).digest("hex");
await build({ configFile: false, root: web, publicDir: false,
  define: { __QSAN_RULES_DEPLOYMENT_ID__: JSON.stringify(deploymentId) },
  build: { outDir: resolve(outDir), emptyOutDir: false, target: "es2022", minify: false,
    lib: { entry: resolve(web, "src/rules-worker.ts"), formats: ["es"] },
    rollupOptions: { output: { entryFileNames: "rules-worker.js" } } } });
