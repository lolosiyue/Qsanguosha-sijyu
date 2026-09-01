import {
  Command,
  PLACE_DELAYED_TRICK,
  PLACE_EQUIP,
  PLACE_HAND,
  PLACE_JUDGE,
  PLACE_TABLE,
  asBool,
  asNumber,
  asNumberList,
  asString,
  asStringList,
  isObject,
  type JsonObject
} from "./protocol";
import {
  CARD_BACK_URL,
  UNKNOWN_CARD_URL,
  assetImg,
  cardFaceUrl,
  fullskinUrls,
  generalFaceUrls,
  kingdomIconUrls,
  magatamaUrl,
  roleIconUrls
} from "./assets";
import { replyForCommand } from "./replies";
import { drawQr } from "./qr";
import { cardRecord, formatInteractionPrompt, tr } from "./i18n";
import { formatPresentationEvent, logPlayerName } from "./log-text";
import {
  cardSelectable,
  playerSelectable,
  targetsAreFeasible,
  useMode
} from "./eligibility";
import { LiveSession, defaultWsUrl, parseRoute, roomShareUrl } from "./session";
import type { PlayerState } from "./state";
import {
  applySceneBackground,
  defaultTableBgUrl,
  lobbyBackgroundUrl
} from "./backdrop";

const session = new LiveSession();
const route = parseRoute();

interface UiState {
  name: string;
  avatar: string;
  ws: string;
  reconnect: boolean;
  selectedCards: number[];
  selectedPlayers: string[];
  selectedOption: string;
  top: number[];
  bottom: number[];
  assignments: Record<string, string>;
  qmlText: string;
  skillInstance: number;
  logPinned: boolean;
  hiddenIndex: number;
}

const ui: UiState = {
  name: localStorage.getItem("qsan-name") || "web-player",
  avatar: localStorage.getItem("qsan-avatar") || "caocao",
  ws: defaultWsUrl(),
  reconnect: route.reconnect,
  selectedCards: [],
  selectedPlayers: [],
  selectedOption: "",
  top: [],
  bottom: [],
  assignments: {},
  qmlText: "{}",
  skillInstance: 0,
  logPinned: true,
  hiddenIndex: -1
};

function currentCardId(): number {
  return ui.selectedCards.find((id) => id >= 0) ?? -1;
}

function pruneSelection(): void {
  const interaction = session.interaction;
  if (!interaction)
    return;
  const { command, payload } = interaction;
  const skill = ui.selectedOption;
  ui.selectedCards = ui.selectedCards.filter((id) =>
    id < 0 || cardSelectable(session.state, command, payload, id, skill));
  const cardId = currentCardId();
  ui.selectedPlayers = ui.selectedPlayers.filter((name) =>
    playerSelectable(session.state, command, payload, name, cardId, ui.selectedPlayers, skill));
}

function isCardClickable(cardId: number): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  return cardSelectable(session.state, interaction.command, interaction.payload, cardId, ui.selectedOption);
}

function isPlayerClickable(name: string): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  const cardId = currentCardId();
  if (interaction.command === Command.CHOOSE_PLAYER)
    return playerSelectable(session.state, interaction.command, interaction.payload, name, cardId, ui.selectedPlayers, ui.selectedOption);
  if (useMode(interaction.command) !== "play")
    return false;
  if (!ui.selectedOption && cardId < 0)
    return false;
  return playerSelectable(session.state, interaction.command, interaction.payload, name, cardId, ui.selectedPlayers, ui.selectedOption);
}

function resetSelection(): void {
  ui.selectedCards = [];
  ui.selectedPlayers = [];
  ui.selectedOption = "";
  ui.skillInstance = 0;
  ui.hiddenIndex = -1;
}

function hasCommand(command: number, ids: number[]): boolean {
  return ids.includes(command);
}

function app(): HTMLElement {
  return document.getElementById("app") as HTMLElement;
}

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string> = {},
  children: (Node | string)[] = []
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (key === "class")
      node.className = value;
    else
      node.setAttribute(key, value);
  }
  for (const child of children)
    node.append(child);
  return node;
}

const INTERACTION_TITLES: Record<number, string> = {
  [Command.CHOOSE_ROLE]: "分配身分",
  [Command.CHOOSE_GENERAL]: "選擇武將",
  [Command.ASK_GENERAL]: "選擇武將",
  [Command.CHOOSE_DIRECTION]: "選擇座次方向",
  [Command.PLAY_CARD]: "出牌",
  [Command.RESPONSE_CARD]: "打出牌",
  [Command.DISCARD_CARD]: "棄牌",
  [Command.EXCHANGE_CARD]: "換牌",
  [Command.ASK_PEACH]: "求桃",
  [Command.NULLIFICATION]: "無懈可擊",
  [Command.MULTIPLE_CHOICE]: "選擇",
  [Command.INVOKE_SKILL]: "發動技能",
  [Command.CHOOSE_PLAYER]: "選擇角色",
  [Command.CHOOSE_CARD]: "選擇卡牌",
  [Command.CHOOSE_SUIT]: "選擇花色",
  [Command.CHOOSE_KINGDOM]: "選擇勢力",
  [Command.AMAZING_GRACE]: "五穀豐登",
  [Command.SKILL_GUANXING]: "觀星",
  [Command.SKILL_GONGXIN]: "攻心",
  [Command.SKILL_YIJI]: "遺計",
  [Command.PINDIAN]: "拼點",
  [Command.TRIGGER_ORDER]: "技能發動順序",
  [Command.ARRANGE_GENERAL]: "排列武將",
  [Command.LUCK_CARD]: "手氣卡",
  [Command.SURRENDER]: "投降",
  [Command.CHOOSE_ORDER]: "選擇順序",
  [Command.CHOOSE_ROLE_3V3]: "選擇身分",
  [Command.SHOW_CARD]: "展示牌",
  [Command.QML_INTERACT]: "互動"
};

function cardNumberText(value: number): string {
  if (value === 1)
    return "A";
  if (value === 11)
    return "J";
  if (value === 12)
    return "Q";
  if (value === 13)
    return "K";
  return value > 0 ? String(value) : "";
}

function cardLabel(cardId: number): string {
  const card = session.state.card(cardId);
  const catalog = cardRecord(cardId);
  const raw = asString(card?.object_name)
    || asString(card?.card_name)
    || asString(catalog?.object_name)
    || `#${cardId}`;
  const name = tr(raw);
  const suitRaw = card?.suit ?? catalog?.suit;
  const suit = typeof suitRaw === "number"
    ? ["spade", "club", "heart", "diamond"][suitRaw] ?? ""
    : asString(suitRaw);
  const number = asNumber(card?.number, asNumber(catalog?.number));
  if (!suit && number <= 0)
    return name;
  return `${name}[${tr(suit)}${cardNumberText(number)}]`;
}

function cardObjectName(cardId: number): string {
  const card = session.state.card(cardId);
  const catalog = cardRecord(cardId);
  return asString(card?.object_name)
    || asString(card?.card_name)
    || asString(catalog?.object_name);
}

function renderCard(
  cardId: number,
  selected: boolean,
  hidden = false,
  hiddenIndex = -1,
  selectable = true,
  dim = false
): HTMLButtonElement {
  const button = el("button", {
    class: `card${selected ? " selected" : ""}${hidden ? " hidden" : ""}${!selectable && dim ? " disabled" : ""}${!selectable && !dim ? " display" : ""}`
  });
  button.type = "button";
  const objectName = cardObjectName(cardId);
  const face = hidden
    ? assetImg([CARD_BACK_URL], CARD_BACK_URL)
    : assetImg([cardFaceUrl(objectName)], UNKNOWN_CARD_URL);
  const caption = el("span", { class: "card-caption" }, [hidden ? "暗牌" : cardLabel(cardId)]);
  button.append(face, caption);
  if (!selectable)
    return button;
  button.addEventListener("click", () => {
    const mode = session.interaction ? useMode(session.interaction.command) : "free";
    if (hidden) {
      ui.selectedCards = [-1];
      ui.hiddenIndex = hiddenIndex;
    } else if (mode === "play" || mode === "response") {
      ui.selectedCards = ui.selectedCards.includes(cardId) ? [] : [cardId];
      ui.selectedPlayers = [];
      ui.hiddenIndex = -1;
    } else if (ui.selectedCards.includes(cardId)) {
      ui.selectedCards = ui.selectedCards.filter((id) => id !== cardId);
      ui.hiddenIndex = -1;
    } else {
      ui.selectedCards = [...ui.selectedCards, cardId];
      ui.hiddenIndex = -1;
    }
    render();
  });
  return button;
}

function connectForm(): HTMLElement {
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

function sharePanel(compact = false): HTMLElement {
  const roomId = asNumber(session.state.connectionValue("room_id"), -1);
  const box = el("div", { class: compact ? "share share-mini" : "share" });
  if (roomId < 0 || session.phase !== "active")
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

function waitingRoom(): HTMLElement {
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

function playerGeneralName(player: PlayerState | undefined): string {
  return asString(player?.general) || asString(player?.avatar);
}

function togglePlayer(name: string): void {
  if (!isPlayerClickable(name))
    return;
  ui.selectedPlayers = ui.selectedPlayers.includes(name)
    ? ui.selectedPlayers.filter((item) => item !== name)
    : [...ui.selectedPlayers, name];
  render();
}

function photoCard(name: string, kind: "photo" | "dash"): HTMLElement {
  const player = session.state.player(name);
  const general = playerGeneralName(player);
  const selected = ui.selectedPlayers.includes(name);
  const clickable = isPlayerClickable(name);
  const button = el("button", {
    class: `${kind}${name === session.state.selfName ? " self" : ""}${player?.alive === false ? " dead" : ""}${selected ? " selected" : ""}${kind === "photo" && !clickable ? " disabled" : ""}`
  });
  button.type = "button";
  const art = el("div", { class: "photo-art" });
  if (general)
    art.append(assetImg(fullskinUrls(general), "", "fullskin"));
  const kingdom = asString(player?.kingdom);
  if (kingdom)
    art.append(assetImg(kingdomIconUrls(kingdom), "", "kingdom"));
  const role = asString(player?.role) || "unknown";
  art.append(assetImg(roleIconUrls(role), "", "role"));
  const hp = asNumber(player?.hp);
  const maxHp = asNumber(player?.max_hp);
  const mag = el("div", { class: "magatamas" });
  mag.append(assetImg([magatamaUrl(hp)], "", "magatama"));
  mag.append(el("span", {}, [`${hp}/${maxHp}`]));
  const handCount = asNumber(session.state.playerValue(name, "hand_count"));
  const meta = el("div", { class: "photo-meta" });
  meta.append(
    el("strong", { class: "screen-name" }, [asString(player?.screen_name, name)]),
    el("div", { class: "general-name" }, [tr(general)]),
    mag,
    el("div", { class: "hand-count" }, [`手${handCount}`])
  );
  art.append(meta);
  button.append(art);
  button.addEventListener("click", () => togglePlayer(name));
  const wrap = el("div", { class: `${kind}-wrap` });
  wrap.append(button);
  const tricks = session.state.cardsForPlayer(name, PLACE_DELAYED_TRICK);
  if (tricks.length) {
    const judge = el("div", { class: "photo-judge" });
    for (const id of tricks)
      judge.append(renderCard(id, false, false, -1, false));
    wrap.append(judge);
  }
  return wrap;
}

function tableView(): HTMLElement {
  const table = el("div", { class: "table" });
  const seats = el("div", { class: "photos" });
  for (const name of session.state.playerNames) {
    if (name === session.state.selfName)
      continue;
    seats.append(photoCard(name, "photo"));
  }
  if (!seats.childElementCount)
    seats.append(el("p", { class: "status" }, ["等待其他角色"]));
  table.append(seats, tablePileView());
  return table;
}

function tablePileView(): HTMLElement {
  const pile = el("div", { class: "table-pile" });
  const tableIds = session.state.cardsAtPlace(PLACE_TABLE)
    .filter((id) => asString(session.state.card(id)?.pile) !== "ren_pile");
  const renIds = session.state.cardsAtPlace(PLACE_TABLE, "ren_pile");
  const judgeIds = session.state.cardsAtPlace(PLACE_JUDGE);
  const appendGroup = (label: string, ids: number[]) => {
    if (ids.length === 0)
      return;
    pile.append(el("span", { class: "status" }, [label]));
    for (const id of ids)
      pile.append(renderCard(id, false, false, -1, false));
  };
  appendGroup("處理區", tableIds);
  appendGroup(tr("ren_pile") === "ren_pile" ? "仁" : tr("ren_pile"), renIds);
  appendGroup("判定", judgeIds);
  if (!pile.childElementCount)
    pile.append(el("span", { class: "status" }, ["桌面空"]));
  return pile;
}

function logView(): HTMLElement {
  const log = el("div", { class: "log" });
  for (const event of session.state.presentationEvents.slice(-80)) {
    const line = formatPresentationEvent(event, (name) =>
      event.command === Command.SPEAK
        ? (asString(session.state.player(name)?.screen_name) || name)
        : logPlayerName(session.state, name));
    if (line)
      log.append(el("p", {}, [line]));
  }
  log.addEventListener("scroll", () => {
    ui.logPinned = log.scrollHeight - log.scrollTop - log.clientHeight < 32;
  });
  requestAnimationFrame(() => {
    if (ui.logPinned)
      log.scrollTop = log.scrollHeight;
  });
  return log;
}

function dashboardView(): HTMLElement {
  const dash = el("section", { class: "dashboard" });
  const self = session.state.selfName;
  dash.append(photoCard(self, "dash"));
  const body = el("div", { class: "dash-body" });
  const equips = el("div", { class: "dash-equips" });
  const equipIds = session.state.cardsForPlayer(self, PLACE_EQUIP);
  if (equipIds.length === 0)
    equips.append(el("span", { class: "status" }, ["裝備空"]));
  for (const id of equipIds)
    equips.append(renderCard(id, ui.selectedCards.includes(id), false, -1, isCardClickable(id), !isCardClickable(id)));
  body.append(equips, pileRow(self), skillBar(), handView(), interactionView());
  dash.append(body);
  return dash;
}

function pileRow(player: string): HTMLElement {
  const row = el("div", { class: "dash-piles" });
  const piles = session.state.playerValue(player, "piles");
  if (!isObject(piles))
    return row;
  for (const [name, value] of Object.entries(piles)) {
    const ids = asNumberList(value);
    if (ids.length === 0)
      continue;
    row.append(el("span", { class: "status" }, [tr(name)]));
    for (const id of ids)
      row.append(renderCard(id, ui.selectedCards.includes(id), false, -1, isCardClickable(id), !isCardClickable(id)));
  }
  return row;
}

function visibleSkills(): { name: string; instanceId: number }[] {
  const instances = session.state.playerValue(session.state.selfName, "skill_instances");
  if (isObject(instances)) {
    const result: { name: string; instanceId: number }[] = [];
    for (const value of Object.values(instances)) {
      if (!isObject(value) || asBool(value.visible, true) === false)
        continue;
      const name = asString(value.skill_name);
      if (name)
        result.push({ name, instanceId: asNumber(value.instance_id) });
    }
    if (result.length)
      return result;
  }
  return asStringList(session.state.playerValue(session.state.selfName, "skills"))
    .map((name) => ({ name, instanceId: 0 }));
}

function skillBaseName(skillName: string): string {
  return skillName.replace(/^#/u, "").replace(/&$/u, "");
}

function skillDescription(skillName: string): string {
  if (!skillName)
    return "";
  const base = skillBaseName(skillName);
  const key = `:${base}`;
  const text = tr(key);
  if (text === key)
    return "";
  return text.replace(/<[^>]+>/gu, "").replace(/&nbsp;/gu, " ").trim();
}

function skillBar(): HTMLElement {
  const row = el("div", { class: "skill-bar" });
  const skills = visibleSkills();
  if (skills.length === 0)
    return row;
  for (const skill of skills) {
    const selected = ui.selectedOption === skill.name;
    const desc = skillDescription(skill.name);
    const button = el("button", {
      class: `skill-btn${selected ? " primary" : ""}`,
      title: desc || tr(skill.name)
    }, [tr(skill.name)]);
    button.addEventListener("click", () => {
      if (ui.selectedOption === skill.name) {
        ui.selectedOption = "";
        ui.skillInstance = 0;
      } else {
        ui.selectedOption = skill.name;
        ui.skillInstance = skill.instanceId;
      }
      render();
    });
    row.append(button);
  }
  const selectedDesc = skillDescription(ui.selectedOption);
  if (ui.selectedOption)
    row.append(el("p", { class: "skill-desc" }, [
      selectedDesc || `${tr(ui.selectedOption)}（無技能描述）`
    ]));
  return row;
}

function playCardText(cardIds: number[], skillName: string): string {
  const ids = cardIds.filter((id) => id >= 0);
  const sub = ids.length > 0 ? ids.join("+") : ".";
  if (skillName) {
    const base = skillBaseName(skillName);
    if (base.includes("_")
        || /^(nos|ol|tw|mobile|tenyear|jx|yj|js|sp|mt|mou|new)/u.test(base))
      return `#${base}:${sub}:`;
    const klass = `${base.charAt(0).toUpperCase()}${base.slice(1)}Card`;
    return `@${klass}=${sub}`;
  }
  if (ids.length === 1) {
    const card = session.state.card(ids[0]);
    return asString(card?.card_string) || String(ids[0]);
  }
  return "";
}

function handView(): HTMLElement {
  const hand = el("div", { class: "hand" });
  const ids = session.state.cardsForPlayer(session.state.selfName, PLACE_HAND);
  if (ids.length === 0)
    hand.append(el("span", { class: "status" }, ["手牌空"]));
  for (const id of ids)
    hand.append(renderCard(id, ui.selectedCards.includes(id), false, -1, isCardClickable(id), !isCardClickable(id)));
  return hand;
}

function optionButtons(values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "cards" });
  for (const value of values) {
    const button = el("button", { class: ui.selectedOption === value ? "primary" : "" }, [tr(value)]);
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

function generalPicker(values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "general-pick" });
  for (const value of values) {
    const button = el("button", { class: ui.selectedOption === value ? "primary" : "" });
    button.append(assetImg(generalFaceUrls(value), "", "portrait"), el("span", {}, [tr(value)]));
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

function payloadOptions(payload: JsonObject): string[] {
  return [
    ...asStringList(payload.options),
    ...asStringList(payload.candidates),
    ...asStringList(payload.roles),
    ...asStringList(payload.choices)
  ];
}

function interactionView(): HTMLElement {
  const root = el("section", { class: "prompt" });
  const interaction = session.interaction;
  if (!interaction) {
    if (!asBool(session.state.gameValue("started")))
      return waitingRoom();
    root.append(el("p", { class: "status" }, ["等待詢問"]));
    const trust = el("button", {}, ["託管"]);
    trust.addEventListener("click", () => session.trust(true));
    const surrender = el("button", { class: "danger" }, ["投降"]);
    surrender.addEventListener("click", () => session.surrender());
    root.append(trust, surrender);
    return root;
  }
  const { command, payload, messageId } = interaction;
  root.append(el("h2", {}, [INTERACTION_TITLES[command] ?? `詢問 ${command}`]));
  const prompt = asString(payload.prompt) || asString(payload.skill_name);
  if (prompt) {
    const text = asString(payload.prompt)
      ? formatInteractionPrompt(prompt, (name) => logPlayerName(session.state, name))
      : tr(prompt);
    root.append(el("p", {}, [text]));
  }
  if (session.interactionError)
    root.append(el("p", { class: "error" }, [session.interactionError]));

  const submit = (builder: () => JsonObject) => {
    try {
      session.sendReply(command, messageId, builder());
      resetSelection();
    } catch (error) {
      session.interactionError = error instanceof Error ? error.message : String(error);
      render();
    }
  };

  const cancel = el("button", {}, ["取消"]);
  cancel.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));

  if (hasCommand(command, [Command.CHOOSE_GENERAL, Command.ASK_GENERAL])) {
    const options = payloadOptions(payload);
    root.append(generalPicker(options, (value) => {
      ui.selectedOption = value;
      submit(() => replyForCommand(command, { option: value }));
    }));
    root.append(cancel);
    return root;
  }

  if (hasCommand(command, [
    Command.MULTIPLE_CHOICE,
    Command.CHOOSE_SUIT, Command.CHOOSE_KINGDOM, Command.CHOOSE_DIRECTION,
    Command.TRIGGER_ORDER, Command.CHOOSE_ROLE_3V3
  ])) {
    const options = payloadOptions(payload);
    root.append(optionButtons(options, (value) => {
      ui.selectedOption = value;
      submit(() => replyForCommand(command, { option: value }));
    }));
    root.append(cancel);
    return root;
  }

  if (command === Command.INVOKE_SKILL || command === Command.SURRENDER || command === Command.LUCK_CARD) {
    const yes = el("button", { class: "primary" }, ["是"]);
    const no = el("button", {}, ["否"]);
    yes.addEventListener("click", () => submit(() => replyForCommand(command, { bool: true })));
    no.addEventListener("click", () => submit(() => replyForCommand(command, { bool: false })));
    root.append(yes, no);
    return root;
  }

  if (command === Command.CHOOSE_ORDER) {
    root.append(optionButtons(["0", "1"], (value) => {
      submit(() => replyForCommand(command, { int: Number(value) }));
    }));
    return root;
  }

  if (command === Command.CHOOSE_ROLE) {
    const players = session.state.playerNames;
    const roles = ["lord", "loyalist", "rebel", "renegade"];
    for (const player of players) {
      const select = el("select");
      for (const role of roles) {
        const option = el("option", { value: role }, [tr(role)]);
        if (ui.assignments[player] === role)
          option.selected = true;
        select.append(option);
      }
      select.addEventListener("change", () => {
        ui.assignments[player] = select.value;
      });
      root.append(el("label", {}, [`${player} `, select]));
    }
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => {
      const assignments: Record<string, string> = {};
      for (const player of players)
        assignments[player] = ui.assignments[player] || roles[0];
      return replyForCommand(command, { assignments });
    }));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.CHOOSE_PLAYER) {
    root.append(el("p", {}, ["點 Photo 或自身頭像選玩家"]));
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { players: ui.selectedPlayers })));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.SKILL_GUANXING) {
    const ids = asNumberList(payload.card_ids ?? payload.cards);
    if (ui.top.length === 0 && ui.bottom.length === 0)
      ui.top = [...ids];
    root.append(el("p", {}, ["上：點牌移到下"]));
    const row = el("div", { class: "cards" });
    for (const id of ui.top) {
      const card = renderCard(id, false);
      card.addEventListener("click", () => {
        ui.top = ui.top.filter((item) => item !== id);
        ui.bottom.push(id);
        render();
      }, { once: true });
      row.append(card);
    }
    const bottom = el("div", { class: "cards" });
    for (const id of ui.bottom)
      bottom.append(renderCard(id, true));
    const ok = el("button", { class: "primary" }, ["確定"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { top: ui.top, bottom: ui.bottom })));
    root.append(row, el("p", {}, ["下"]), bottom, ok);
    return root;
  }

  if (command === Command.SKILL_YIJI) {
    const ids = asNumberList(payload.card_ids);
    root.append(el("p", {}, ["選手牌再點座位"]));
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, ["交給所選玩家"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, {
      cardIds: ui.selectedCards,
      yijiTarget: ui.selectedPlayers[0] ?? ""
    })));
    root.append(row, ok, cancel);
    return root;
  }

  if (command === Command.ARRANGE_GENERAL) {
    const generals = asStringList(payload.generals ?? payload.general_names ?? payload.candidates);
    root.append(generalPicker(generals, (value) => {
      ui.selectedOption = value;
      render();
    }));
    const ok = el("button", { class: "primary" }, ["送出順序（點選後按）"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, {
      generals: ui.selectedOption ? [ui.selectedOption, ...generals.filter((item) => item !== ui.selectedOption)] : generals
    })));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.QML_INTERACT) {
    const area = el("textarea");
    area.value = ui.qmlText;
    area.rows = 6;
    const ok = el("button", { class: "primary" }, ["送出 JSON"]);
    ok.addEventListener("click", () => {
      ui.qmlText = area.value;
      submit(() => replyForCommand(command, { qml: JSON.parse(area.value) as JsonObject }));
    });
    root.append(el("p", {}, ["未知 QML type 請填 JSON 或取消"]), area, ok, cancel);
    return root;
  }

  if (command === Command.CHOOSE_CARD) {
    const target = asString(payload.player);
    const flags = asString(payload.zone_flags) || "hej";
    const visible = asBool(payload.hand_cards_visible);
    const disabled = new Set(asNumberList(payload.disabled_card_ids));
    const row = el("div", { class: "cards" });
    const appendKnown = (ids: number[]) => {
      for (const id of ids) {
        if (disabled.has(id))
          continue;
        row.append(renderCard(id, ui.selectedCards.includes(id)));
      }
    };
    if (flags.includes("e"))
      appendKnown(session.state.cardsForPlayer(target, PLACE_EQUIP));
    if (flags.includes("j")) {
      appendKnown(session.state.cardsForPlayer(target, PLACE_DELAYED_TRICK));
    }
    if (flags.includes("h")) {
      const handCount = asNumber(session.state.playerValue(target, "hand_count"));
      if (visible)
        appendKnown(session.state.cardsForPlayer(target, PLACE_HAND));
      else {
        for (let index = 0; index < handCount; index++)
          row.append(renderCard(-1, ui.hiddenIndex === index, true, index));
      }
    }
    const ok = el("button", { class: "primary" }, ["選這張"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? -1 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (hasCommand(command, [Command.SKILL_GONGXIN, Command.AMAZING_GRACE])) {
    const ids = [...new Set(asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids)))];
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? 0 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (hasCommand(command, [
    Command.PLAY_CARD, Command.RESPONSE_CARD, Command.ASK_PEACH,
    Command.NULLIFICATION, Command.SHOW_CARD, Command.PINDIAN,
    Command.EXCHANGE_CARD, Command.DISCARD_CARD
  ])) {
    const skills = visibleSkills();
    if (command === Command.PLAY_CARD && skills.length) {
      const selected = ui.selectedOption ? tr(ui.selectedOption) : "未選";
      root.append(el("p", {}, [`技能（再點一次取消）：${selected}`]));
    }
    const extras = [...new Set(
      asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids))
    )].filter((id) => {
      const owner = asString(session.state.card(id)?.owner);
      return owner !== session.state.selfName;
    });
    if (extras.length) {
      const row = el("div", { class: "cards" });
      for (const id of extras)
        row.append(renderCard(id, ui.selectedCards.includes(id), false, -1, isCardClickable(id), !isCardClickable(id)));
      root.append(el("p", {}, ["額外可選牌"]), row);
    }
    const ok = el("button", { class: "primary" }, ["送出"]);
    const mode = useMode(command);
    const cardId = currentCardId();
    const playBlocked = mode === "play" && !ui.selectedOption
      && (cardId < 0 || !targetsAreFeasible(session.state, session.state.selfName, cardId, ui.selectedPlayers));
    const responseBlocked = mode === "response" && cardId < 0;
    if (playBlocked || responseBlocked)
      ok.setAttribute("disabled", "true");
    ok.addEventListener("click", () => {
      if (command === Command.EXCHANGE_CARD || command === Command.DISCARD_CARD) {
        submit(() => replyForCommand(command, { cardIds: ui.selectedCards }));
        return;
      }
      submit(() => replyForCommand(command, {
        cardText: playCardText(ui.selectedCards.filter((id) => id >= 0), ui.selectedOption),
        targets: ui.selectedPlayers,
        skillName: ui.selectedOption,
        instanceId: ui.skillInstance
      }));
    });
    const pass = command === Command.PLAY_CARD ? el("button", {}, ["結束出牌"]) : cancel;
    if (command === Command.PLAY_CARD)
      pass.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));
    root.append(el("p", {}, ["點 Dashboard 技能（可再點取消），選手牌／裝備，再點目標 Photo，然後送出"]), ok, pass);
    return root;
  }

  root.append(el("p", { class: "error" }, [`未覆蓋的互動 ${command}`]), cancel);
  return root;
}

export function render(): void {
  pruneSelection();
  const root = app();
  root.replaceChildren();
  const shell = el("div", { class: "app" });
  if (session.phase === "idle" || session.phase === "failed") {
    applySceneBackground(lobbyBackgroundUrl());
    const logo = assetImg(["/assets/logo/logo.png"], "", "logo");
    logo.alt = "QSanguosha";
    shell.className = "app idle";
    shell.append(logo, connectForm());
    root.append(shell);
    return;
  }
  const toolbar = el("div", { class: "toolbar" });
  toolbar.append(sharePanel(true));
  toolbar.append(el("strong", {}, ["QSanguosha"]));
  toolbar.append(el("span", { class: "status" }, [
    `${session.phase} ${asString(session.state.connectionValue("room_id"))}`
  ]));
  const tableBg = asString(session.state.gameValue("table_bg")) || defaultTableBgUrl();
  applySceneBackground(tableBg);
  if (!asBool(session.state.gameValue("started"))) {
    shell.className = "app wait";
    shell.append(toolbar, waitingRoom());
    if (session.interaction) {
      const view = interactionView();
      view.classList.add("wait-panel");
      shell.append(view);
    }
    root.append(shell);
    return;
  }
  shell.className = "app room";
  shell.append(toolbar, tableView(), logView(), dashboardView());
  root.append(shell);
}

export function start(): void {
  let frame = 0;
  session.onChange(() => {
    if (frame)
      return;
    frame = requestAnimationFrame(() => {
      frame = 0;
      try {
        render();
      } catch (error) {
        session.interactionError = error instanceof Error ? error.message : String(error);
      }
    });
  });
  render();
}
