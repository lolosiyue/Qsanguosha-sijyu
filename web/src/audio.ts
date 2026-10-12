import { isSafeAssetName } from "./assets";
import { summarizeGameResult } from "./game-result";
import { packageAssetAlias, packageAssetUri } from "./package-assets";
import { asBool, asNumber, asString, Command, GameEvent, type JsonObject } from "./protocol";
import type { ClientGameState } from "./state";

// Mirrors skins/defaultSkin.audio.json: fixed common effects, then per-category defaults.
const COMMON_EFFECTS: Record<string, string> = {
  weapon: "audio/card/common/weapon.ogg",
  armor: "audio/card/common/armor.ogg",
  horse: "audio/card/common/horse.ogg",
  chain: "audio/card/common/chain.ogg",
  treasure: "audio/card/common/armor.ogg"
};
const CATEGORY_DEFAULTS: Record<string, string> = {
  equip: "audio/equip/%1.ogg",
  male: "audio/card/male/%1.ogg",
  female: "audio/card/female/%1.ogg"
};
const MAX_SKILL_VARIANTS = 32;

const playing = new Map<string, HTMLAudioElement>();
const probes = new Map<string, Promise<boolean>>();
const skillSources = new Map<string, Promise<string[]>>();

/** Engine logical audio paths: a verified package alias first, then the local audio/ tree served beside assets/. */
export function audioPathToUrl(path: string): string {
  if (path.startsWith("package://"))
    return packageAssetUri(path) ?? "";
  if (!path.startsWith("audio/") || !/^[A-Za-z0-9_./+-]+$/.test(path) || path.includes(".."))
    return "";
  return packageAssetAlias(path) ?? `/${path}`;
}

// Skill voices are enumerated like Skill::initMediaSource, so misses must be real HTTP errors.
function pathExists(path: string): Promise<boolean> {
  const url = audioPathToUrl(path);
  if (!url)
    return Promise.resolve(false);
  let pending = probes.get(url);
  if (!pending) {
    pending = fetch(url, { method: "HEAD" })
      .then((response) => response.ok && (response.headers.get("content-type") ?? "").startsWith("audio/"))
      .catch(() => false);
    probes.set(url, pending);
  }
  return pending;
}

function playUrl(url: string, superpose: boolean): void {
  const current = playing.get(url);
  // Desktop backends skip a file that is still playing unless superposition is requested.
  if (current && !superpose && !current.paused && !current.ended)
    return;
  const element = new Audio(url);
  playing.set(url, element);
  element.addEventListener("ended", () => {
    if (playing.get(url) === element)
      playing.delete(url);
  });
  // Autoplay policy rejects playback before the first user gesture; joining a room is one.
  void element.play().catch(() => {
    if (playing.get(url) === element)
      playing.delete(url);
  });
}

/** Play the first existing candidate, like the desktop's ordered QFile::exists fallbacks. */
export async function playAudio(paths: string | string[], superpose = true): Promise<void> {
  for (const path of Array.isArray(paths) ? paths : [paths]) {
    if (await pathExists(path)) {
      playUrl(audioPathToUrl(path), superpose);
      return;
    }
  }
}

async function enumerateSkillSources(skill: string): Promise<string[]> {
  const found: string[] = [];
  for (let index = 1; index <= MAX_SKILL_VARIANTS; index++) {
    const path = `audio/skill/${skill}${index}.ogg`;
    if (!await pathExists(path))
      break;
    found.push(path);
  }
  if (!found.length && await pathExists(`audio/skill/${skill}.ogg`))
    found.push(`audio/skill/${skill}.ogg`);
  return found;
}

/** QSanRoomSkin::getPlayerAudioEffectPath: skin entry, then skill sources, then the category default. */
export async function playSkillEffect(skill: string, category: string, index: number): Promise<void> {
  if (!isSafeAssetName(skill))
    return;
  const common = category === "common" ? COMMON_EFFECTS[skill] : undefined;
  if (common) {
    await playAudio(common);
    return;
  }
  let sources = skillSources.get(skill);
  if (!sources) {
    sources = enumerateSkillSources(skill);
    skillSources.set(skill, sources);
  }
  const files = await sources;
  if (files.length) {
    const pick = index < 1 ? Math.floor(Math.random() * files.length) : Math.min(index, files.length) - 1;
    await playAudio(files[pick]);
    return;
  }
  const fallback = CATEGORY_DEFAULTS[category];
  if (fallback)
    await playAudio(fallback.replace("%1", skill));
}

function deathPaths(general: string): string[] {
  // General::lastWord: the general's own line, then the origin general of a derived name.
  const paths = [`audio/death/${general}.ogg`];
  const origin = general.split("_").pop() ?? "";
  if (origin && origin !== general)
    paths.push(`audio/death/${origin}.ogg`);
  return paths;
}

/** Side effects RoomScene plays for room notifications. State is the last committed native view. */
export function playNotificationAudio(command: number, payload: JsonObject, state: ClientGameState): void {
  switch (command) {
  case Command.PLAY_AUDIO:
    // Room::playAudioEffect's second field is the superpose flag; the wire names it "loop".
    void playAudio(asString(payload.path), asBool(payload.loop));
    break;
  case Command.LOG_EVENT:
    if (asNumber(payload.event) === GameEvent.PLAY_EFFECT && asNumber(payload.audio_type) !== 0)
      void playSkillEffect(asString(payload.skill_name), asString(payload.category), asNumber(payload.audio_type));
    break;
  case Command.SET_EMOTION: {
    const emotion = asString(payload.emotion);
    if (emotion === "chain")
      void playAudio(COMMON_EFFECTS.chain);
    else if (emotion.startsWith("weapon/") || emotion.startsWith("armor/"))
      void playSkillEffect(emotion.split("/").pop() ?? "", "equip", -1);
    break;
  }
  case Command.KILL_PLAYER: {
    const general = asString(state.player(asString(payload.player_name))?.general);
    if (isSafeAssetName(general))
      void playAudio(deathPaths(general));
    break;
  }
  case Command.GAME_OVER: {
    const result = summarizeGameResult(payload, state.playerNames, state.selfName);
    if (result.selfOutcome !== "victory") {
      void playAudio("audio/system/lose.ogg");
      break;
    }
    const lines: string[] = [];
    for (const winner of result.winners) {
      const player = state.player(winner.objectName);
      const general = asString(player?.general);
      if (player?.alive && isSafeAssetName(general))
        lines.push(`audio/win/${general}.ogg`);
    }
    void playAudio(lines);
    void playAudio("audio/system/win.ogg");
    break;
  }
  default:
    break;
  }
}
