import { loadTranslations } from "./i18n";
import { loadUiConfig } from "./backdrop";
import { start } from "./ui";

await loadUiConfig();
await loadTranslations();
start();
window.addEventListener("popstate", () => window.location.reload());
