import { SoloController, type SoloCatalog, type SoloOptions } from "./solo-client";
import type { LiveSession } from "./session";
import { el } from "./ui-dom";

const PREF_KEY = "qsan-solo-preferences-v1";

export interface SoloUiHost {
  controller: SoloController;
  session: LiveSession;
  name: string;
  avatar: string;
  render: () => void;
  start: (options: SoloOptions) => void;
  home: () => void;
}

function catalogOptions(catalog: SoloCatalog): SoloOptions {
  const defaults = catalog.defaults;
  const fallbackMode = catalog.modes.find((mode) => mode.id === "05p")?.id
    || (catalog.modes.some((mode) => mode.id === defaults.mode) ? defaults.mode : catalog.modes[0]?.id || "05p");
  const packageIds = new Set(catalog.packages.filter((pkg) => !pkg.forbidden).map((pkg) => pkg.id));
  const stored = (() => {
    try { return JSON.parse(localStorage.getItem(PREF_KEY) || "null") as Partial<SoloOptions> | null; }
    catch { return null; }
  })();
  const mode = typeof stored?.mode === "string" && catalog.modes.some((item) => item.id === stored.mode)
    ? stored.mode : fallbackMode;
  const enabled = Array.isArray(stored?.enabled_packages)
    ? stored.enabled_packages.filter((id): id is string => typeof id === "string" && packageIds.has(id))
    : defaults.enabled_packages.filter((id) => catalog.packages.some((pkg) => pkg.id === id && pkg.enabled && !pkg.forbidden));
  const banned = new Set(catalog.generals.map((general) => general.id));
  const banGenerals = Array.isArray(stored?.ban_generals)
    ? stored.ban_generals.filter((id): id is string => typeof id === "string" && banned.has(id))
    : defaults.ban_generals.filter((id) => banned.has(id));
  const integer = (value: unknown, fallback: number, maximum: number) =>
    typeof value === "number" && Number.isInteger(value) && value >= 0 ? Math.min(value, maximum) : fallback;
  return {
    mode,
    enabled_packages: [...new Set(enabled)],
    ban_generals: [...new Set(banGenerals)],
    operation_timeout: integer(stored?.operation_timeout, defaults.operation_timeout, 3600),
    ai_delay: integer(stored?.ai_delay, defaults.ai_delay, 60000)
  };
}

function saveOptions(options: SoloOptions): void {
  localStorage.setItem(PREF_KEY, JSON.stringify({ version: 1, ...options }));
}

function field(label: string, input: HTMLElement): HTMLElement {
  return el("label", { class: "field" }, [label, input]);
}

export function soloSetup(host: SoloUiHost): HTMLElement {
  const controller = host.controller;
  const panel = el("section", { class: "solo-setup", "aria-labelledby": "solo-heading" });
  panel.append(el("div", { class: "section-heading" }, [el("div", {}, [el("h2", { id: "solo-heading" }, ["單機對局"]), el("p", { class: "status" }, ["使用現有單機內容，設定只保留在這台裝置。"])]), el("span", { class: "phase-badge" }, ["可用"])]));
  if (controller.status === "idle" || controller.status === "loading") {
    const load = el("button", { class: "primary", type: "button" }, [controller.status === "loading" ? "準備中…" : "開始設定"]);
    load.disabled = controller.status === "loading";
    load.addEventListener("click", () => void controller.prepare().catch(() => undefined));
    panel.append(el("p", { class: "status" }, ["與電腦對戰，設定會保留在這台裝置。"]), load);
    return panel;
  }
  if (controller.status === "failed" || !controller.catalog) {
    panel.append(el("p", { class: "error" }, [controller.error || "單機對局目前無法使用。"]));
    const retry = el("button", { class: "primary", type: "button" }, ["重試"]);
    retry.addEventListener("click", () => void controller.prepare().catch(() => undefined));
    panel.append(retry);
    return panel;
  }
  const catalog = controller.catalog;
  const options = catalogOptions(catalog);
  const mode = el("select", { id: "solo-mode", "data-focus-key": "solo-mode" });
  for (const item of catalog.modes)
    mode.append(el("option", { value: item.id }, [`${item.name}（${item.player_count}人）`]));
  mode.value = options.mode;
  const packageSearch = el("input", { id: "solo-package-search", "data-focus-key": "solo-package-search", type: "search", placeholder: "搜尋武將／卡牌包" });
  const packageList = el("div", { class: "solo-check-list" });
  const selectedPackages = new Set(options.enabled_packages);
  const renderPackages = () => {
    packageList.replaceChildren();
    const query = packageSearch.value.trim().toLocaleLowerCase();
    for (const item of catalog.packages) {
      if (query && !`${item.name} ${item.id}`.toLocaleLowerCase().includes(query)) continue;
      const checkbox = el("input", { type: "checkbox" });
      checkbox.checked = selectedPackages.has(item.id);
      checkbox.disabled = item.forbidden;
      checkbox.addEventListener("change", () => checkbox.checked ? selectedPackages.add(item.id) : selectedPackages.delete(item.id));
      packageList.append(el("label", { class: "solo-check" }, [checkbox, `${item.name}${item.forbidden ? "（不可用）" : ""}`]));
    }
  };
  packageSearch.addEventListener("input", renderPackages);
  renderPackages();

  const generalSearch = el("input", { id: "solo-general-search", "data-focus-key": "solo-general-search", type: "search", placeholder: "搜尋武將" });
  const generalList = el("div", { class: "solo-check-list solo-general-list" });
  const bannedGenerals = new Set(options.ban_generals);
  const renderGenerals = () => {
    generalList.replaceChildren();
    const query = generalSearch.value.trim().toLocaleLowerCase();
    for (const item of catalog.generals) {
      if (query && !`${item.name} ${item.id} ${item.package}`.toLocaleLowerCase().includes(query)) continue;
      const checkbox = el("input", { type: "checkbox" });
      checkbox.checked = bannedGenerals.has(item.id);
      checkbox.addEventListener("change", () => checkbox.checked ? bannedGenerals.add(item.id) : bannedGenerals.delete(item.id));
      generalList.append(el("label", { class: "solo-check" }, [checkbox, `${item.name}（${item.package}）`]));
    }
  };
  generalSearch.addEventListener("input", renderGenerals);
  renderGenerals();
  const name = el("input", { id: "solo-name", "data-focus-key": "solo-name", value: host.name, placeholder: "暱稱" });
  const avatar = el("input", { id: "solo-avatar", "data-focus-key": "solo-avatar", value: host.avatar, placeholder: "頭像" });
  const timeout = el("input", { id: "solo-timeout", "data-focus-key": "solo-timeout", type: "number", min: "0", max: "3600", step: "1", value: String(options.operation_timeout) });
  const delay = el("input", { id: "solo-delay", "data-focus-key": "solo-delay", type: "number", min: "0", max: "60000", step: "1", value: String(options.ai_delay) });
  const submit = el("button", { class: "primary", type: "button" }, ["開始單機對局"]);
  const busy = controller.status === "starting" || controller.status === "running" || controller.status === "stopping";
  submit.disabled = busy;
  submit.addEventListener("click", () => {
    const number = (input: HTMLElement, fallback: number, maximum: number) => {
      const value = Number((input as HTMLInputElement).value);
      return Number.isInteger(value) && value >= 0 ? Math.min(value, maximum) : fallback;
    };
    const next: SoloOptions = {
      mode: mode.value,
      enabled_packages: [...selectedPackages],
      ban_generals: [...bannedGenerals],
      operation_timeout: number(timeout, options.operation_timeout, 3600),
      ai_delay: number(delay, options.ai_delay, 60000)
    };
    localStorage.setItem("qsan-name", name.value.trim() || "web-player");
    localStorage.setItem("qsan-avatar", avatar.value.trim() || "caocao");
    saveOptions(next);
    host.start(next);
  });
  const content = el("div", { class: "solo-options" }, [field("暱稱", name), field("頭像代號", avatar), field("模式", mode)]);
  const advanced = el("details", { class: "solo-advanced" });
  advanced.append(el("summary", {}, ["進階設定", el("small", {}, ["卡牌包、禁用武將與節奏"])]),
    field("啟用武將／卡牌包", packageSearch), packageList,
    field("額外禁用武將", generalSearch), generalList,
    field("操作逾時（秒，0 為不限時）", timeout), field("電腦行動延遲（毫秒）", delay));
  panel.append(content, advanced, submit);
  return panel;
}

export function localReturnHome(host: SoloUiHost): HTMLElement {
  const button = el("button", { class: "danger", type: "button" }, ["返回首頁"]);
  button.addEventListener("click", () => host.home());
  return button;
}
