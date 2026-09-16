import { afterEach, describe, expect, it } from "vitest";
import { generalFaceUrls, packageAssetUri } from "../src/assets";
import { installPackageAssets, packageAssetAlias, resetPackageAssets, resolveAssetSource } from "../src/package-assets";

afterEach(resetPackageAssets);

describe("local package asset resolution", () => {
  it("resolves native general aliases and explicit package URIs to the local same-origin route", () => {
    installPackageAssets({ schema_version: 3, profile: "packages-v1",
      runtime_content: { schema_version: 3, profile: "packages-v1", extensions: [], packages: [{
        id: "addon", version: "1", dependencies: [], assets: {
          "image/generals/card/custom.jpg": "image/generals/card/custom.jpg",
          "audio/skill/attack.ogg": "audio/skill/attack.ogg"
        }
      }] }, files: [
        ...["lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua", "lua/sgs_ex.lua", "lua/lib/json.lua"]
          .map(path => ({ path, role: "rules", size: 0, sha256: "0".repeat(64) }))
      ] });

    expect(generalFaceUrls("custom")[0]).toBe("/packages/addon/image/generals/card/custom.jpg");
    expect(packageAssetAlias("image/generals/card/custom.jpg"))
      .toBe("/packages/addon/image/generals/card/custom.jpg");
    expect(packageAssetUri("package://addon/general/custom"))
      .toBe("/packages/addon/image/general/custom");
    expect(packageAssetUri("package://addon/lua/ai/custom.lua"))
      .toBe("/packages/addon/lua/ai/custom.lua");
    expect(packageAssetUri("package://addon/translation/custom.lua"))
      .toBe("/packages/addon/translation/custom.lua");
    expect(packageAssetUri("package://addon/data/custom.json"))
      .toBe("/packages/addon/data/custom.json");
    expect(packageAssetUri("package://unknown/general/custom")).toBeNull();
    expect(packageAssetUri("package://addon/../other/general/custom")).toBeNull();
    expect(packageAssetUri("package://%61ddon/image/general/custom")).toBeNull();
    expect(packageAssetUri("package://addon/video/custom.mp4")).toBeNull();
    expect(packageAssetUri("package://addon/image/%2e%2e/other/custom")).toBeNull();
    expect(packageAssetAlias("audio/skill/attack.ogg"))
      .toBe("/packages/addon/audio/skill/attack.ogg");
    expect(packageAssetUri("package://addon/audio/skill/attack.ogg"))
      .toBe("/packages/addon/audio/skill/attack.ogg");
    expect(resolveAssetSource("package://addon/audio/skill/attack.ogg"))
      .toBe("/packages/addon/audio/skill/attack.ogg");
    expect(resolveAssetSource("audio/skill/attack.ogg"))
      .toBe("/packages/addon/audio/skill/attack.ogg");
    // An explicit URI is scoped to its named package and never searches aliases elsewhere.
    expect(packageAssetUri("package://addon/general/missing"))
      .toBe("/packages/addon/image/general/missing");
  });

  it("clears package aliases for v2 content", () => {
    installPackageAssets({ schema_version: 2, profile: "declared-v2",
      runtime_content: { schema_version: 2, profile: "declared-v2", extensions: [] }, files: [
        ...["lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua", "lua/sgs_ex.lua", "lua/lib/json.lua"]
          .map(path => ({ path, role: "rules", size: 0, sha256: "0".repeat(64) }))
      ] });
    expect(packageAssetAlias("image/generals/card/custom.jpg")).toBeNull();
    expect(packageAssetUri("package://addon/general/custom")).toBeNull();
  });
});
