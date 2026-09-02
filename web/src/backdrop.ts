import type { ClientGameState } from "./state";
import { asString } from "./protocol";

const LIGHTBOX = 2;

export interface GameUiConfig {
  backgroundImage: string;
  tableBgImage: string;
  enableAutoBackgroundChange: boolean;
  tableBgByKingdom: Record<string, string>;
}

const fallback: GameUiConfig = {
  backgroundImage: "/assets/system/backdrop/2.jpg",
  tableBgImage: "/assets/system/backdrop/1.jpg",
  enableAutoBackgroundChange: true,
  tableBgByKingdom: {
    wei: "/assets/system/backdrop/wei.jpg",
    shu: "/assets/system/backdrop/shu.jpg",
    wu: "/assets/system/backdrop/wu.jpg",
    qun: "/assets/system/backdrop/qun.jpg",
    jin: "/assets/system/backdrop/jin.jpg",
    god: "/assets/system/backdrop/god.jpg"
  }
};

let config: GameUiConfig = fallback;

export async function loadUiConfig(): Promise<void> {
  try {
    const response = await fetch("/game-ui-config.json");
    if (!response.ok)
      return;
    const next = await response.json() as Partial<GameUiConfig>;
    config = {
      backgroundImage: next.backgroundImage || fallback.backgroundImage,
      tableBgImage: next.tableBgImage || fallback.tableBgImage,
      enableAutoBackgroundChange: next.enableAutoBackgroundChange !== false,
      tableBgByKingdom: { ...fallback.tableBgByKingdom, ...next.tableBgByKingdom }
    };
  } catch {
    config = fallback;
  }
}

export function imagePathToUrl(path: string): string {
  if (!path)
    return "";
  if (path.startsWith("/assets/"))
    return path;
  if (path.startsWith("image/"))
    return `/assets/${path.slice("image/".length)}`;
  if (!path.includes("/")) {
    if (/\.(jpg|jpeg|png|webp|gif)$/i.test(path))
      return `/assets/system/backdrop/${path}`;
    return `/assets/system/backdrop/${path}.jpg`;
  }
  return path;
}

export function lobbyBackgroundUrl(): string {
  return config.backgroundImage;
}

export function defaultTableBgUrl(): string {
  return config.tableBgImage;
}

export function kingdomTableBgUrl(kingdom: string): string {
  return config.tableBgByKingdom[kingdom] || "";
}

export function autoTableBgUrl(state: ClientGameState): string {
  if (!config.enableAutoBackgroundChange)
    return config.tableBgImage;
  let kingdom = asString(state.player(state.selfName)?.kingdom);
  for (const name of state.playerNames) {
    const player = state.player(name);
    if (asString(player?.role) === "lord") {
      kingdom = asString(player?.kingdom);
      break;
    }
  }
  return kingdomTableBgUrl(kingdom) || config.tableBgImage;
}

export function lightboxBackgroundUrl(firstArgument: string): string | undefined {
  if (!firstArgument.startsWith("background="))
    return undefined;
  const name = firstArgument.slice("background=".length);
  if (!name)
    return config.tableBgImage;
  return imagePathToUrl(name);
}

export function isLightbox(animation: number): boolean {
  return animation === LIGHTBOX;
}

export function applySceneBackground(url: string): void {
  if (!url)
    return;
  document.documentElement.style.setProperty("--scene-bg", `url("${url}")`);
}
