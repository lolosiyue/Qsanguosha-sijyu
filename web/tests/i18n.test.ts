import { describe, expect, it } from "vitest";
import { installRulesTranslations, resetRulesTranslations, setTranslationsForTest, tr } from "../src/i18n";

describe("translation overlays", () => {
  it("overlays native translations without dropping static UI strings", () => {
    setTranslationsForTest({ ui_ok: "確定", shared: "固定" });
    installRulesTranslations({ extension_key: "擴展", shared: "原生" });
    expect(tr("ui_ok")).toBe("確定");
    expect(tr("extension_key")).toBe("擴展");
    expect(tr("shared")).toBe("原生");
    resetRulesTranslations();
    expect(tr("extension_key")).toBe("extension_key");
    expect(tr("shared")).toBe("固定");
  });

  it("rejects non string native values", () => {
    setTranslationsForTest({ ui_ok: "確定" });
    expect(() => installRulesTranslations({ broken: 1 })).toThrow("rules_identity_invalid");
    expect(tr("ui_ok")).toBe("確定");
  });
});
