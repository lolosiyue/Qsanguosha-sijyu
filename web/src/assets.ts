import { packageAssetAlias, packageAssetUri } from "./package-assets";

const ASSET_SAFE = /^[A-Za-z0-9._+-]+$/;

export const UNKNOWN_CARD_URL = "/assets/card/unknown.jpg";
export const CARD_BACK_URL = "/assets/system/card-back.png";

const IMAGE_SUFFIXES = [".jpg", ".png", ".webp", ".jpeg"];

export { packageAssetUri };

function packageImageUrl(assetPath: string): string | null {
  return packageAssetAlias(`image/${assetPath}`);
}

function stemUrls(stems: string[]): string[] {
  const urls: string[] = [];
  for (const stem of stems) {
    for (const suffix of IMAGE_SUFFIXES) {
      const path = `${stem}${suffix}`;
      if (path.startsWith("/assets/")) {
        const alias = packageAssetAlias(`image/${path.slice("/assets/".length)}`);
        if (alias) urls.push(alias);
      }
      urls.push(path);
    }
  }
  return [...new Set(urls)];
}

export function isSafeAssetName(name: string): boolean {
  return ASSET_SAFE.test(name);
}

export function cardFaceUrl(objectName: string): string {
  if (!isSafeAssetName(objectName))
    return UNKNOWN_CARD_URL;
  return packageImageUrl(`card/${objectName}.jpg`) ?? `/assets/card/${objectName}.jpg`;
}

export function generalFaceUrls(name: string): string[] {
  if (!isSafeAssetName(name))
    return [];
  return stemUrls([
    `/assets/generals/card/${name}`,
    `/assets/general/card/${name}`,
    `/assets/card/${name}`
  ]);
}

export function fullskinUrls(name: string): string[] {
  if (!isSafeAssetName(name))
    return [];
  return stemUrls([
    `/assets/fullskin/generals/full/${name}`,
    `/assets/fullskin/generals/fulldual/${name}`
  ]).concat(generalFaceUrls(name));
}

export function kingdomIconUrls(kingdom: string): string[] {
  if (!isSafeAssetName(kingdom))
    return [];
  return stemUrls([`/assets/kingdom/icon/${kingdom}`]);
}

export function roleIconUrls(role: string): string[] {
  if (!isSafeAssetName(role))
    return [];
  // Desktop RoleComboBox only uses .png. stemUrls() tries .jpg first, so every
  // full render 404s then swaps to png and the icon flashes.
  return [
    packageImageUrl(`system/roles/${role}.png`) ?? `/assets/system/roles/${role}.png`,
    packageImageUrl(`system/roles/small-${role}.png`) ?? `/assets/system/roles/small-${role}.png`
  ];
}

export function magatamaUrl(hp: number): string {
  const clamped = Math.max(0, Math.min(5, Math.floor(hp)));
  return packageImageUrl(`system/magatamas/${clamped}.png`) ?? `/assets/system/magatamas/${clamped}.png`;
}

export function bindAssetImage(img: HTMLImageElement, urls: string[], fallback = ""): void {
  let index = 0;
  const tryNext = () => {
    while (index < urls.length) {
      const candidate = urls[index++];
      if (candidate.startsWith("package://")) {
        const resolved = packageAssetUri(candidate);
        if (!resolved) continue;
        img.src = resolved;
        return;
      }
      img.src = candidate;
      return;
    }
    img.removeEventListener("error", tryNext);
    if (fallback)
      img.src = fallback;
    else
      img.hidden = true;
  };
  img.addEventListener("error", tryNext);
  tryNext();
}

export function assetImg(urls: string[], fallback = "", className = ""): HTMLImageElement {
  const img = document.createElement("img");
  img.alt = "";
  img.draggable = false;
  if (className)
    img.className = className;
  bindAssetImage(img, urls, fallback);
  return img;
}
