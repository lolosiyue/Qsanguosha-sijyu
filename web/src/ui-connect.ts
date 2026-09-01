import { assetImg, generalFaceUrls } from "./assets";
import { drawQr } from "./qr";
import { tr } from "./i18n";
import { asNumber, asString } from "./protocol";
import { defaultWsUrl, roomShareUrl } from "./session";
import { el } from "./ui-dom";
import type { UiBind } from "./ui-types";

export function connectForm(bind: UiBind): HTMLElement {
  const { session, ui, route } = bind;
  const form = el("form", { class: "form" });
  const name = el("input", { value: ui.name, placeholder: "暱稱" });
  const avatar = el("input", { value: ui.avatar, placeholder: "頭像" });
  const ws = el("input", { value: ui.ws, placeholder: "ws://host:9528" });
  const reconnect = el("input", { type: "checkbox" });
  reconnect.checked = ui.reconnect;
  const submit = el("button", { class: "primary", type: "submit" }, [
    route.roomId === undefined ? "加入目前房間" : `加入房間 ${route.roomId}`
  ]);
  form.append(
    el("label", { class: "field" }, ["暱稱", name]),
    el("label", { class: "field" }, ["頭像", avatar]),
    el("label", { class: "field" }, ["WebSocket 位址", ws]),
    el("label", {}, ["重連 ", reconnect]),
    submit
  );
  if (session.error)
    form.append(el("p", { class: "error" }, [session.error]));
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    ui.name = name.value.trim() || "web-player";
    ui.avatar = avatar.value.trim() || "caocao";
    ui.ws = ws.value.trim() || defaultWsUrl();
    ui.reconnect = reconnect.checked;
    localStorage.setItem("qsan-name", ui.name);
    localStorage.setItem("qsan-avatar", ui.avatar);
    session.connect({
      wsUrl: ui.ws,
      screenName: ui.name,
      avatar: ui.avatar,
      reconnect: ui.reconnect,
      roomId: route.roomId
    });
  });
  return form;
}

export function sharePanel(bind: UiBind, compact = false): HTMLElement {
  const roomId = asNumber(bind.session.state.connectionValue("room_id"), -1);
  const box = el("div", { class: compact ? "share share-mini" : "share" });
  if (roomId < 0 || bind.session.phase !== "active")
    return box;
  const url = roomShareUrl(roomId);
  const copy = el("button", {}, [compact ? "複製網址" : "複製連結"]);
  copy.addEventListener("click", async () => {
    await navigator.clipboard.writeText(url);
    copy.textContent = "已複製";
  });
  const canvas = el("canvas");
  try {
    drawQr(canvas, url, compact ? 64 : 180);
  } catch {
    canvas.replaceWith(el("p", { class: "status" }, ["QR 無法編碼"]));
  }
  if (compact)
    box.append(canvas, copy);
  else
    box.append(el("code", {}, [url]), copy, canvas);
  return box;
}

export function waitingRoom(bind: UiBind): HTMLElement {
  const { session } = bind;
  const root = el("section", { class: "wait-panel" });
  root.append(el("h2", {}, ["等待房"]));
  for (const name of session.state.playerNames) {
    const player = session.state.player(name);
    const avatar = asString(player?.avatar);
    const row = el("p", { class: "waiting-player" });
    if (avatar)
      row.append(assetImg(generalFaceUrls(avatar), "", "portrait"));
    row.append(`${asString(player?.screen_name, name)} (${name}) ${tr(avatar)}`);
    root.append(row);
  }
  const ready = el("button", { class: "primary" }, ["準備"]);
  ready.addEventListener("click", () => session.setReady(true));
  const robots = el("button", {}, ["加滿機器人"]);
  robots.addEventListener("click", () => session.addRobots());
  const chat = el("input", { placeholder: "聊天" });
  const send = el("button", {}, ["送出"]);
  send.addEventListener("click", () => {
    if (chat.value.trim())
      session.chat(chat.value.trim());
    chat.value = "";
  });
  root.append(ready, robots, chat, send);
  return root;
}
