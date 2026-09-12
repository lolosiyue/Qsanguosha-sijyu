import { describe, expect, it } from "vitest";
import { fetchContent, installContent, validateContentManifest, verifyInstalledContent } from "../src/rules-content";

const core = ["lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua", "lua/sgs_ex.lua", "lua/lib/json.lua"];
const hash = "a".repeat(64);
function manifest(extra: Record<string, unknown>[] = []) {
  return { schema_version: 2, profile: "declared-v2",
    runtime_content: { schema_version: 2, profile: "declared-v2", extensions: [] }, files: [
    ...core.map(path => ({ path, role: "rules", size: 1, sha256: hash })), ...extra
  ] };
}

describe("rules content delivery", () => {
  it("accepts declared core and skips future AI entries", () => {
    const value = validateContentManifest(manifest([
      { path: "lua/ai/custom-ai.lua", role: "ai", size: 1, sha256: hash },
      { path: "lua/lib/middleclass.lua", role: "ai", size: 0, sha256: hash },
      { path: "lua/middleclass.lua", role: "rules", size: 0, sha256: hash }
    ]));
    expect(value.files).toHaveLength(8);
  });

  it("uses the same extension role boundaries as the native descriptor", () => {
    const value = { ...manifest(), runtime_content: { schema_version: 2, profile: "declared-v2", extensions: [{
      name: "x", script: "extensions/x.lua", dependencies: [], libs: [], lang: [], ai: ["lua/lib/middleclass.lua"]
    }] } };
    expect(validateContentManifest(value).runtime_content.extensions[0].ai).toEqual(["lua/lib/middleclass.lua"]);
    for (const fields of [
      { ai: ["lua/ai/../core.lua"] }, { libs: ["lua/config.lua"] },
      { libs: ["lua/lib/x.lua", "lua/lib/x.lua"] }, { lang: ["lang/../core.lua"] }
    ]) {
      const extension = { ...value.runtime_content.extensions[0], ...fields };
      expect(() => validateContentManifest({ ...value, runtime_content: {
        ...value.runtime_content, extensions: [extension]
      } })).toThrow("rules_content_unsupported");
    }
  });

  it("rejects traversal, duplicates, and AI outside the AI directory", () => {
    for (const entry of [
      { path: "lua/../evil.lua", role: "rules", size: 1, sha256: hash },
      { path: core[0], role: "rules", size: 1, sha256: hash },
      { path: "lua/not-ai.lua", role: "ai", size: 1, sha256: hash },
      { path: "extensions/nested/x.lua", role: "rules", size: 1, sha256: hash },
      { path: "lua/lib/middleclass.lua", role: "rules", size: 1, sha256: hash }
    ]) expect(() => validateContentManifest(manifest([entry]))).toThrow("rules_content_unsupported");
  });

  it("fetches fixed hash routes and verifies bytes", async () => {
    const bytes = new TextEncoder().encode("x");
    const digest = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))]
      .map(value => value.toString(16).padStart(2, "0")).join("");
    const value = { schema_version: 2, profile: "declared-v2",
      runtime_content: { schema_version: 2, profile: "declared-v2", extensions: [] }, files: [
      ...core.map(path => ({ path, role: "rules", size: 1, sha256: digest })),
      { path: "extensions/x.lua", role: "rules", size: 1, sha256: digest }
    ] };
    const urls: string[] = [];
    const result = await fetchContent(value, async (input, init) => {
      urls.push(String(input));
      expect(init?.redirect).toBe("error");
      return new Response(bytes, { status: 200 });
    });
    expect(urls).toHaveLength(6);
    expect(urls.every(url => url === `/rules/content/${digest}`)).toBe(true);
    expect(result.get("extensions/x.lua")).toEqual(bytes);
  });

  it("rejects a changed byte", async () => {
    const value = manifest([{ path: "extensions/x.lua", role: "rules", size: 1, sha256: hash }]);
    await expect(fetchContent(value, async () => new Response(new Uint8Array([9]))))
      .rejects.toThrow("rules_reload_required");
  });

  it("converts network failures to a reload error", async () => {
    const value = manifest();
    await expect(fetchContent(value, async () => { throw new Error("offline"); }))
      .rejects.toThrow("rules_reload_required");
  });

  it("requires the installed asset inventory to match exactly", async () => {
    const emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const value = { schema_version: 2, profile: "declared-v2",
      runtime_content: { schema_version: 2, profile: "declared-v2", extensions: [] }, files: core.map(path =>
      ({ path, role: "rules", size: 0, sha256: emptyHash })) };
    const files = new Map<string, Uint8Array>();
    const fs = {
      mkdirTree() {}, writeFile(path: string, bytes: Uint8Array) { files.set(path, bytes); },
      readFile(path: string) { return files.get(path) ?? new Uint8Array(); },
      readdir(path: string) {
        const prefix = path.endsWith("/") ? path : `${path}/`;
        return [...new Set([...files.keys()].filter(key => key.startsWith(prefix))
          .map(key => key.slice(prefix.length).split("/")[0]))];
      },
      lstat(path: string) { return { mode: files.has(path) ? 1 : 2 }; },
      isDir(mode: number) { return mode === 2; }, isFile(mode: number) { return mode === 1; },
      unlink(path: string) { files.delete(path); }, rmdir() {}
    };
    installContent(fs, new Map(core.map(path => [`${path}`, new Uint8Array()] as const)),
      validateContentManifest(value).runtime_content);
    await verifyInstalledContent(fs, value);
    const descriptorPath = "/assets/runtime-content.json";
    const descriptor = files.get(descriptorPath)!;
    files.delete(descriptorPath);
    await expect(verifyInstalledContent(fs, value)).rejects.toThrow("rules_reload_required");
    files.set(descriptorPath, new TextEncoder().encode(JSON.stringify({
      ...value.runtime_content, extensions: [{ name: "injected", path: "extensions/injected.lua" }]
    })));
    await expect(verifyInstalledContent(fs, value)).rejects.toThrow("rules_reload_required");
    files.set(descriptorPath, descriptor);
    await verifyInstalledContent(fs, value);
    files.set("/assets/stale.lua", new Uint8Array());
    await expect(verifyInstalledContent(fs, value)).rejects.toThrow("rules_reload_required");
  });
});
