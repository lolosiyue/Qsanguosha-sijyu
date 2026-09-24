import { assetImg, generalFaceUrls } from "./assets";
import { drawQr } from "./qr";
import { tr } from "./i18n";
import { asBool, asNumber, asString } from "./protocol";
import { defaultWsUrl, roomShareUrl } from "./session";
import { el } from "./ui-dom";
import type { UiBind } from "./ui-types";

export function connectForm(bind: UiBind): HTMLElement {
  const { session, ui, route } = bind;
  const form = el("form", { class: "form connect-form", "aria-labelledby": "connect-heading" });
  const name = el("input", { id: "connect-name", "data-focus-key": "connect-name", value: ui.name, placeholder: "例如：赵云", autocomplete: "nickname" });
  const avatar = el("input", { id: "connect-avatar", "data-focus-key": "connect-avatar", value: ui.avatar, placeholder: "例如：caocao", autocomplete: "off" });
  const ws = el("input", { id: "connect-ws", "data-focus-key": "connect-ws", value: ui.ws, placeholder: "ws://host:9528", inputmode: "url", autocomplete: "url", "aria-describedby": "connect-ws-help" });
  const reconnect = el("input", { id: "connect-reconnect", "data-focus-key": "connect-reconnect", type: "checkbox" });
  reconnect.checked = ui.reconnect;
  name.addEventListener("input", () => { ui.name = name.value; });
  avatar.addEventListener("input", () => { ui.avatar = avatar.value; });
  ws.addEventListener("input", () => { ui.ws = ws.value; });
  reconnect.addEventListener("change", () => { ui.reconnect = reconnect.checked; });
  const submit = el("button", { class: "primary", type: "submit" }, [
    route.roomId === undefined ? "加入目前房间" : `加入房间 ${route.roomId}`
  ]);
  submit.disabled = session.phase === "connecting";
  form.append(
    el("h2", { id: "connect-heading" }, [route.roomId === undefined ? "连线至伺服器" : `加入房间 ${route.roomId}`]),
    el("p", { class: "form-intro" }, ["保留你的暱称与头像，输入伺服器位址即可开始。"]),
    el("label", { class: "field" }, ["暱称", name, el("small", {}, ["其他玩家会看见的名称。"])]),
    el("label", { class: "field" }, ["头像代号", avatar, el("small", {}, ["使用现有武将美术代号，例如 caocao。"])]),
    el("label", { class: "field" }, ["WebSocket 位址", ws, el("small", { id: "connect-ws-help" }, ["伺服器提供的连线地址，通常以 ws:// 开头。"])]),
    el("label", { class: "check-field" }, [reconnect, "重新接回既有席位"]),
    submit
  );
  if (session.phase === "connecting")
    form.append(el("p", { class: "status" }, ["正在载入并核对规则版本…"]));
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
  const copy = el("button", {}, [compact ? "复制网址" : "复制连结"]);
  copy.addEventListener("click", async () => {
    await navigator.clipboard.writeText(url);
    copy.textContent = "已复制";
  });
  const canvas = el("canvas");
  try {
    drawQr(canvas, url, compact ? 64 : 180);
  } catch {
    canvas.replaceWith(el("p", { class: "status" }, ["QR 无法编码"]));
  }
  if (compact)
    box.append(canvas, copy);
  else
    box.append(el("code", {}, [url]), copy, canvas);
  return box;
}

export function waitingRoom(bind: UiBind): HTMLElement {
  const { session } = bind;
  bind.ui.presentation ??= {};
  const root = el("section", { class: "wait-panel", "aria-labelledby": "waiting-heading" });
  const roomId = asNumber(session.state.connectionValue("room_id"), -1);
  const mode = asString(session.state.connectionValue("mode")) || asString(session.state.setup.mode) || "一般房";
  const count = session.state.playerNames.length;
  root.append(el("div", { class: "section-heading" }, [
    el("div", {}, [el("h2", { id: "waiting-heading" }, ["等待房"]), el("p", { class: "status" }, [`${mode} · ${count} 位玩家${roomId >= 0 ? ` · 房号 ${roomId}` : ""}`])]),
    el("span", { class: "phase-badge" }, [session.phase === "active" ? "已连线" : "准备中"])
  ]));
  const roster = el("div", { class: "waiting-roster", role: "list", "aria-label": "房内玩家" });
  for (const name of session.state.playerNames) {
    const player = session.state.player(name);
    const avatar = asString(player?.avatar);
    const hasReadyState = player ? Object.prototype.hasOwnProperty.call(player, "ready") : false;
    const readyState = hasReadyState ? asBool(player?.ready) : false;
    const stateText = hasReadyState ? (readyState ? "已准备" : "等待准备") : "状态未提供";
    const row = el("article", { class: `waiting-player${readyState ? " is-ready" : ""}`, role: "listitem" });
    if (avatar)
      row.append(assetImg(generalFaceUrls(avatar), "", "portrait"));
    row.append(el("div", { class: "waiting-player-copy" }, [
      el("strong", {}, [asString(player?.screen_name, name)]),
      el("span", { class: "status" }, [`${tr(avatar)} · ${stateText}`])
    ]));
    roster.append(row);
  }
  root.append(roster);
  const ready = el("button", { class: "primary" }, ["准备"]);
  ready.addEventListener("click", () => session.setReady(true));
  const robots = el("button", {}, ["加满机器人"]);
  robots.addEventListener("click", () => session.addRobots());
  const canAct = session.phase === "active";
  ready.disabled = !canAct;
  robots.disabled = !canAct;
  const chat = el("input", { id: "waiting-chat", "data-focus-key": "waiting-chat", value: bind.ui.presentation?.chatDraft || "", placeholder: "输入房内讯息", autocomplete: "off", "aria-label": "房内聊天讯息" });
  chat.addEventListener("input", () => { if (bind.ui.presentation) bind.ui.presentation.chatDraft = chat.value; });
  const send = el("button", { type: "button" }, ["送出"]);
  send.disabled = !canAct;
  chat.disabled = !canAct;
  send.addEventListener("click", () => {
    if (canAct && chat.value.trim()) {
      const message = chat.value.trim();
      if (bind.ui.presentation) bind.ui.presentation.chatDraft = "";
      chat.value = "";
      session.chat(message);
    }
  });
  const chatForm = el("form", { class: "waiting-chat" });
  chatForm.addEventListener("submit", (event) => { event.preventDefault(); send.click(); });
  chatForm.append(el("label", { for: "waiting-chat" }, ["房内聊天"]), chat, send);
  root.append(el("div", { class: "waiting-actions" }, [ready, robots]), chatForm);
  return root;
}
