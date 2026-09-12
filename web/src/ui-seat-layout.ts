import { asNumber, asString, asStringList } from "./protocol";
import type { UiBind } from "./ui-types";
import { playerHandLabel } from "./player-metrics";

// Exact region rows from RoomScene::updateTable; the row index is opponent count - 1.
const REGULAR: readonly number[][] = [
  [1],
  [5, 6],
  [5, 1, 6],
  [3, 1, 1, 4],
  [3, 1, 1, 1, 4],
  [5, 5, 1, 1, 6, 6],
  [5, 5, 1, 1, 1, 6, 6],
  [3, 3, 7, 7, 7, 7, 4, 4],
  [3, 3, 7, 7, 7, 7, 7, 4, 4],
  [3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4],
  [3, 3, 3, 7, 7, 7, 7, 7, 7, 4, 4, 4],
  [3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4],
  [3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4],
  [3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4],
  [3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4],
  [3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4],
  [3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4],
  [3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4],
  [3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 4, 4, 4]
];
const HULAO = [[1, 1, 1], [3, 3, 1], [3, 1, 4], [1, 4, 4]];
const KOF = [[3, 1, 1, 1, 4], [1, 1, 1, 4, 4], [3, 3, 1, 1, 1]];

export function seatRing(bind: UiBind): string[] {
  const state = bind.session.state;
  // Stable sorting preserves the supplied order when seat metadata is absent.
  const names = state.playerNames.filter((name) => state.player(name)?.removed !== true)
    .slice().sort((a, b) => asNumber(state.player(a)?.seat, Number.MAX_SAFE_INTEGER)
      - asNumber(state.player(b)?.seat, Number.MAX_SAFE_INTEGER));
  const self = names.indexOf(state.selfName);
  return self < 0 ? names : [...names.slice(self + 1), ...names.slice(0, self)];
}

export function nativeSeatRegion(bind: UiBind, name: string): string {
  const ring = seatRing(bind);
  const state = bind.session.state;
  const mode = asString(state.setup.game_mode);
  const selfSeat = asNumber(state.player(state.selfName)?.seat, 0);
  const special = (mode === "04_1v3" || mode === "04_boss") && ring.length === 3 && selfSeat >= 1 && selfSeat <= 4
    ? HULAO[selfSeat - 1] : mode === "06_3v3" && ring.length === 5 && selfSeat >= 1
      ? KOF[(selfSeat - 1) % 3] : undefined;
  return String((special ?? REGULAR[ring.length - 1])?.[ring.indexOf(name)] ?? 7);
}

export function seatLabel(bind: UiBind, name: string, _order: number): string {
  const player = bind.session.state.player(name);
  const seat = asNumber(player?.seat, -1);
  return `${seat > 0 ? `座位 ${seat}` : "座次未提供"}，${asString(player?.screen_name, name)}，`
    + `${playerHandLabel(bind.session.state, name)}，${player?.alive === false ? "已死亡" : "存活"}`;
}

/** Each render owns its observers. Return cleanup before replacing the old DOM. */
export function setupBoardLayout(root: HTMLElement, bind: UiBind): () => void {
  const table = root.querySelector<HTMLElement>(".table");
  const seats = root.querySelector<HTMLElement>(".seat-ring");
  const center = root.querySelector<HTMLElement>(".table-center");
  if (!table || !seats || !center) return () => undefined;
  const cards = [...seats.querySelectorAll<HTMLElement>(".photo-wrap")];
  const portrait = window.matchMedia("(max-width: 719px), (orientation: portrait)");
  const presentation = bind.ui.presentation ??= {};
  const state = bind.session.state;
  const currentFocus = asStringList(state.gameValue("focus"))[0] || asString(state.gameValue("current_player"));
  const focusChanged = currentFocus !== presentation.focusPlayer;
  presentation.focusPlayer = currentFocus;
  let disposed = false;
  let frame = 0;
  let strip = false;
  let previousWidth = 0;
  const groups = Array.from({ length: 8 }, (_, region) => cards.filter((card) => Number(card.dataset.region) === region));
  // Right column and upper row run in reverse; left column runs forwards.
  groups.forEach((group, region) => { if (region !== 4 && region !== 6) group.reverse(); });
  const resetCardPositions = () => cards.forEach((card) => {
    card.style.removeProperty("position"); card.style.removeProperty("left"); card.style.removeProperty("top");
  });
  const measure = () => {
    if (disposed) return;
    const width = seats.clientWidth;
    const height = seats.clientHeight;
    if (!width || !height) return;
    const wasStrip = strip;
    strip = portrait.matches || cards.length > REGULAR.length;
    table.classList.toggle("seat-strip", strip);
    const gap = 8;
    let photoWidth = Math.min(152, width / 3);
    let photoHeight = 0;
    let fits = cards.length === 0;
    if (!strip) {
      // Fit actual rendered information, including wrapped marks/equipment. Never hide L0 to fit.
      for (; photoWidth >= 96; photoWidth -= 8) {
        table.style.setProperty("--seat-width", `${photoWidth}px`);
        table.style.setProperty("--seat-art-height", `${Math.max(54, photoWidth * .66)}px`);
        photoHeight = Math.max(0, ...cards.map((card) => card.offsetHeight));
        const topCount = Math.max(groups[0].length + groups[1].length + groups[2].length, groups[7].length);
        const topHeight = topCount ? photoHeight + gap : 0;
        const topWidth = groups[7].length ? width - gap * 2 : width - (photoWidth + gap) * 2;
        fits = topWidth >= topCount * (photoWidth + gap)
          && [3, 4, 5, 6].every((region) => {
            const available = height - gap * 2 - (region === 3 || region === 4 ? topHeight : 0);
            return groups[region].length * (photoHeight + gap) <= available;
          }) && photoHeight + gap * 2 <= height;
        if (fits) break;
      }
      if (!fits) strip = true;
    }
    table.classList.toggle("seat-strip", strip);
    if (strip) {
      resetCardPositions();
      table.style.setProperty("--seat-width", "124px");
      table.style.setProperty("--seat-art-height", "84px");
      center.style.removeProperty("position"); center.style.removeProperty("inset");
      if (!wasStrip || previousWidth !== width) seats.scrollLeft = presentation.seatScrollLeft ?? 0;
    } else {
      const topHeight = groups[0].length || groups[1].length || groups[2].length || groups[7].length ? photoHeight + gap : 0;
      groups.forEach((group, region) => {
        const vertical = region >= 3 && region <= 6;
        const left = region === 0 || region === 3 || region === 5 ? width - photoWidth - gap : gap;
        const fullWidth = region === 7;
        const availableWidth = fullWidth ? width - gap * 2 : width - (photoWidth + gap) * 2;
        const rowLeft = fullWidth ? gap : photoWidth + gap;
        const availableHeight = height - gap * 2 - (region === 3 || region === 4 ? topHeight : 0);
        const yStart = gap + (region === 3 || region === 4 ? topHeight : 0)
          + Math.max(0, (availableHeight - group.length * (photoHeight + gap) + gap) / 2);
        const xStart = rowLeft + Math.max(0, (availableWidth - group.length * (photoWidth + gap) + gap) / 2);
        group.forEach((card, index) => {
          card.style.position = "absolute";
          card.style.left = `${vertical ? left : xStart + index * (photoWidth + gap)}px`;
          card.style.top = `${vertical ? yStart + index * (photoHeight + gap) : gap}px`;
        });
      });
      center.style.position = "absolute";
      center.style.inset = `${seats.offsetTop + topHeight + gap}px ${photoWidth + gap * 2}px ${gap}px`;
    }
    previousWidth = width;
  };
  const queueMeasure = () => {
    cancelAnimationFrame(frame);
    frame = requestAnimationFrame(measure);
  };
  const rememberScroll = () => {
    if (!strip) return;
    presentation.seatScrollLeft = seats.scrollLeft;
    const midpoint = seats.scrollLeft + seats.clientWidth / 2;
    const visible = cards.filter((card) => card.offsetWidth > 0);
    const nearest = visible.sort((a, b) => Math.abs(a.offsetLeft + a.clientWidth / 2 - midpoint)
      - Math.abs(b.offsetLeft + b.clientWidth / 2 - midpoint))[0];
    if (nearest) presentation.browsedSeatIndex = Number(nearest.dataset.seatOrder);
  };
  seats.addEventListener("scroll", rememberScroll, { passive: true });
  const observer = new ResizeObserver(queueMeasure);
  observer.observe(seats);
  portrait.addEventListener("change", queueMeasure);
  measure();
  frame = requestAnimationFrame(() => {
    if (disposed) return;
    measure();
    if (strip && focusChanged && currentFocus) {
      const target = cards.find((card) => card.dataset.player === currentFocus);
      if (target && !target.hidden) seats.scrollLeft = target.offsetLeft - (seats.clientWidth - target.clientWidth) / 2;
    } else if (strip) seats.scrollLeft = presentation.seatScrollLeft ?? 0;
  });
  return () => {
    disposed = true; cancelAnimationFrame(frame); observer.disconnect();
    portrait.removeEventListener("change", queueMeasure); seats.removeEventListener("scroll", rememberScroll);
  };
}
