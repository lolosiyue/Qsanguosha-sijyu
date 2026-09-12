import { UNKNOWN_CARD_URL, assetImg, cardFaceUrl, generalFaceUrls } from "./assets";
import { formatInteractionPrompt, tr } from "./i18n";
import { logPlayerName } from "./log-text";
import {
  Command,
  PLACE_DELAYED_TRICK,
  PLACE_EQUIP,
  PLACE_HAND,
  asBool,
  asNumber,
  asNumberList,
  asString,
  asStringList,
  isObject,
  type JsonObject
} from "./protocol";
import { replyCommand, replyForCommand } from "./replies";
import type { RulesEvaluation } from "./rules-client";
import { cardLabel, renderCard } from "./ui-cards";
import { waitingRoom } from "./ui-connect";
import { el, hasCommand } from "./ui-dom";
import type { UiBind } from "./ui-types";

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

function physicalCardText(bind: UiBind, cardIds: number[]): string {
  const ids = cardIds.filter((id) => id >= 0);
  if (ids.length === 1) {
    const card = bind.session.state.card(ids[0]);
    return asString(card?.card_string) || String(ids[0]);
  }
  return "";
}

function optionButtons(bind: UiBind, values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "cards" });
  for (const value of values) {
    const button = el("button", { class: bind.ui.selectedOption === value ? "primary" : "" }, [tr(value)]);
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

function generalPicker(bind: UiBind, values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "general-pick" });
  for (const value of values) {
    const button = el("button", { class: bind.ui.selectedOption === value ? "primary" : "" });
    button.append(assetImg(generalFaceUrls(value), "", "portrait"), el("span", {}, [tr(value)]));
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

const RULES_STATUS_TEXT: Record<string, string> = {
  idle: "準備規則資料",
  loading: "載入規則中",
  evaluating: "判定選擇中",
  failed: "規則預覽已停用；請重新連線",
  unsupported: "目前內容不受此規則套件支援"
};

const RULES_REASON_TEXT: Record<string, string> = {
  select_one_card: "請選擇一張牌",
  incomplete_card_selection: "請繼續選擇子卡",
  declaration_required: "請先選擇宣告",
  invalid_declaration: "請重新選擇宣告",
  incomplete_targets: "請選擇合適目標",
  skill_unavailable: "目前無法發動此技能",
  subcard_rejected: "請移除不符合技能條件的子卡",
  card_unavailable: "目前無法使用此牌",
  card_limited: "此牌受到使用限制",
  pattern_mismatch: "此牌不符合目前詢問",
  target_prohibited: "此牌無法指定該目標",
  target_unavailable: "目標已不可指定",
  rearrangement_incomplete: "每張牌都要放到上或下",
  rearrangement_out_of_range: "上下兩區的張數不符合要求",
  selection_count_out_of_range: "選擇的張數不符合要求",
  incomplete_selection_state: "缺少規則資料"
};

const DECLARATION_LABELS: Record<string, string> = {
  guhuo: "宣告牌名",
  juguan: "宣告牌名",
  tiansuan: "宣告"
};

const DECLARATION_PROMPTS: Record<string, string> = {
  guhuo: "請選擇宣告的牌名",
  juguan: "請選擇宣告的牌名",
  tiansuan: "請選擇宣告"
};

const ZONE_LABELS: Record<string, string> = {
  hand: "手牌",
  equip: "裝備",
  hand_pile: "手牌區牌堆",
  expand_pile: "技能牌堆",
  sibling_pile: "他人牌堆"
};

// The native answer for the selection the shell is currently showing. Anything
// else — a stale request, a newer state revision — reads as "not ready yet".
function nativeEvaluation(bind: UiBind): RulesEvaluation | null {
  return bind.rules.current(bind.session, bind.rulesSelection()) ? bind.rules.result : null;
}

// The shared ClientCore payload for this request. Enumerated prompts render
// their set and counts from here rather than from raw wire payload fields.
function nativePayload(evaluation: RulesEvaluation | null): JsonObject {
  const request = evaluation?.interaction;
  const payload = isObject(request) ? request.payload : undefined;
  return isObject(payload) ? payload : {};
}

function rulesStatus(bind: UiBind, evaluation: RulesEvaluation | null): HTMLElement[] {
  const { rules } = bind;
  const nodes: HTMLElement[] = [el("p", { class: "status", role: "status" }, [
    evaluation?.known ? "規則已就緒" : (RULES_STATUS_TEXT[rules.status] || "等待規則判定")
  ])];
  if (rules.error || (evaluation && !evaluation.known))
    nodes.push(el("p", { class: "error" }, [
      rules.error || `目前無法判定：${evaluation?.reason || "缺少規則資料"}`
    ]));
  else if (evaluation?.reason)
    nodes.push(el("p", { class: "status" }, [
      RULES_REASON_TEXT[evaluation.reason] || "目前選擇無法確認"
    ]));
  return nodes;
}

// Positive confirmation always sends the C++ encoder's payload, and only while
// that payload still belongs to the request and reply command on screen.
function nativeConfirm(
  bind: UiBind,
  command: number,
  messageId: string,
  submit: (builder: () => JsonObject) => void,
  label = "送出"
): HTMLButtonElement {
  const evaluation = nativeEvaluation(bind);
  const ok = el("button", { class: "primary" }, [label]);
  ok.disabled = !evaluation?.known || !evaluation.can_confirm || !evaluation.wire;
  ok.addEventListener("click", () => submit(() => {
    const current = nativeEvaluation(bind);
    if (!current?.known || !current.can_confirm || !current.wire)
      throw new Error("規則尚未完成目前選擇的判定");
    if (current.wire.command !== replyCommand(command) || current.wire.reply_to !== messageId)
      throw new Error("此規則結果已不屬於目前詢問");
    // The native reply encoder owns the canonical payload for this request.
    return current.wire.payload;
  }));
  return ok;
}

function countHint(minimum: number, maximum: number, total: number): string {
  if (minimum < 0 || maximum < 0)
    return `最多 ${total} 張`;
  return minimum === maximum ? `需 ${minimum} 張` : `需 ${minimum}–${maximum} 張`;
}

function payloadOptions(payload: JsonObject): string[] {
  return [
    ...asStringList(payload.options),
    ...asStringList(payload.candidates),
    ...asStringList(payload.roles),
    ...asStringList(payload.choices)
  ];
}

export function interactionView(bind: UiBind): HTMLElement {
  const { session, ui } = bind;
  const root = el("section", { class: "prompt" });
  const interaction = session.interaction;
  if (asBool(session.state.gameValue("game_over"))) {
    root.append(el("p", { class: "status" }, ["遊戲已結束"]));
    return root;
  }
  if (!interaction) {
    if (!asBool(session.state.gameValue("started")))
      return waitingRoom(bind);
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
      bind.resetSelection();
    } catch (error) {
      session.interactionError = error instanceof Error ? error.message : String(error);
      bind.render();
    }
  };

  const cancel = el("button", {}, ["取消"]);
  cancel.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));

  if (hasCommand(command, [Command.CHOOSE_GENERAL, Command.ASK_GENERAL])) {
    const options = payloadOptions(payload);
    root.append(generalPicker(bind, options, (value) => {
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
    root.append(optionButtons(bind, options, (value) => {
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
    root.append(optionButtons(bind, ["0", "1"], (value) => {
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
    // The draft is normalized before the native query, so this view only draws
    // it. Both bounds and the reply come from the shared ClientCore request.
    const evaluation = nativeEvaluation(bind);
    const native = nativePayload(evaluation);
    const total = ui.top.length + ui.bottom.length;
    root.append(...rulesStatus(bind, evaluation));
    const move = (id: number, toBottom: boolean) => {
      if (session.interaction !== interaction)
        return;
      ui.top = ui.top.filter((item) => item !== id);
      ui.bottom = ui.bottom.filter((item) => item !== id);
      if (toBottom)
        ui.bottom = [...ui.bottom, id];
      else
        ui.top = [...ui.top, id];
      bind.render();
    };
    const zone = (ids: number[], selected: boolean, toBottom: boolean): HTMLElement => {
      const row = el("div", { class: "cards" });
      for (const id of ids) {
        const card = renderCard(bind, id, selected, false, -1, false);
        card.classList.add("movable");
        card.addEventListener("click", () => move(id, toBottom));
        row.append(card);
      }
      return row;
    };
    const bounds = (minKey: string, maxKey: string, count: number): string =>
      `${count} 張，${countHint(asNumber(native[minKey], -1), asNumber(native[maxKey], -1), total)}`;
    root.append(
      el("p", {}, [`上（點牌移到下）：${bounds("min_top", "max_top", ui.top.length)}`]),
      zone(ui.top, false, true),
      el("p", {}, [`下（點牌移回上）：${bounds("min_bottom", "max_bottom", ui.bottom.length)}`]),
      zone(ui.bottom, true, false),
      nativeConfirm(bind, command, messageId, submit, "確定")
    );
    return root;
  }

  if (command === Command.SKILL_YIJI) {
    const evaluation = nativeEvaluation(bind);
    const native = nativePayload(evaluation);
    const ids = asNumberList(native.cards ?? payload.card_ids);
    const selectable = new Set(evaluation?.known ? evaluation.selectable_cards : ids);
    root.append(...rulesStatus(bind, evaluation));
    root.append(el("p", {}, [
      `選牌再點座位：${countHint(asNumber(evaluation?.selection_min, -1),
        asNumber(evaluation?.selection_max, -1), ids.length)}`
    ]));
    const row = el("div", { class: "cards" });
    for (const id of ids) {
      const enabled = selectable.has(id);
      const card = renderCard(bind, id, ui.selectedCards.includes(id), false, -1, false, !enabled);
      if (enabled) {
        card.classList.add("movable");
        card.addEventListener("click", () => {
          if (session.interaction !== interaction)
            return;
          ui.selectedCards = ui.selectedCards.includes(id)
            ? ui.selectedCards.filter((item) => item !== id)
            : [...ui.selectedCards, id];
          bind.render();
        });
      }
      row.append(card);
    }
    const candidates = evaluation?.next_targets.candidates ?? asStringList(payload.players);
    root.append(row, el("p", { class: "status" }, [
      ui.selectedPlayers.length
        ? `交給 ${logPlayerName(session.state, ui.selectedPlayers[0])}`
        : `可交給：${candidates.map((name) => logPlayerName(session.state, name)).join("、") || "（無）"}`
    ]));
    root.append(nativeConfirm(bind, command, messageId, submit, "交給所選玩家"), cancel);
    return root;
  }

  if (command === Command.ARRANGE_GENERAL) {
    const generals = asStringList(payload.generals ?? payload.general_names ?? payload.candidates);
    root.append(generalPicker(bind, generals, (value) => {
      ui.selectedOption = value;
      bind.render();
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
        row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
      }
    };
    if (flags.includes("e"))
      appendKnown(session.state.cardsForPlayer(target, PLACE_EQUIP));
    if (flags.includes("j"))
      appendKnown(session.state.cardsForPlayer(target, PLACE_DELAYED_TRICK));
    if (flags.includes("h")) {
      const handCount = asNumber(session.state.playerValue(target, "hand_count"));
      if (visible)
        appendKnown(session.state.cardsForPlayer(target, PLACE_HAND));
      else {
        for (let index = 0; index < handCount; index++)
          row.append(renderCard(bind, -1, ui.hiddenIndex === index, true, index));
      }
    }
    const ok = el("button", { class: "primary" }, ["選這張"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? -1 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (command === Command.SKILL_GONGXIN) {
    const evaluation = nativeEvaluation(bind);
    const native = nativePayload(evaluation);
    const ids = [...new Set(asNumberList(native.visible_cards ?? payload.card_ids)
      .concat(asNumberList(payload.enabled_card_ids)))];
    const selectable = new Set(evaluation?.known
      ? evaluation.selectable_cards : asNumberList(payload.enabled_card_ids));
    root.append(...rulesStatus(bind, evaluation));
    const row = el("div", { class: "cards" });
    for (const id of ids) {
      const enabled = selectable.has(id);
      const card = renderCard(bind, id, ui.selectedCards.includes(id), false, -1, false, !enabled);
      if (enabled) {
        card.classList.add("movable");
        card.addEventListener("click", () => {
          if (session.interaction !== interaction)
            return;
          ui.selectedCards = ui.selectedCards.includes(id) ? [] : [id];
          bind.render();
        });
      }
      row.append(card);
    }
    root.append(row, nativeConfirm(bind, command, messageId, submit), cancel);
    return root;
  }

  if (command === Command.AMAZING_GRACE) {
    const ids = [...new Set(asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids)))];
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? 0 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (bind.rules.supports(command)) {
    const evaluation = nativeEvaluation(bind);
    root.append(...rulesStatus(bind, evaluation));
    if (ui.selectedOption) {
      const active = evaluation?.skills.find((skill) => skill.name === ui.selectedOption
        && skill.instance_id === ui.skillInstance);
      const notes: string[] = [`技能（再點一次取消）：${tr(ui.selectedOption)}`];
      if (active) {
        // Amount, committed usage and instance invalidation are reported by the
        // runtime; only canActivate/cardSelectionFeasible decide legality.
        notes.push(active.subcard_min < 0
          ? "子卡張數由技能判定"
          : `子卡 ${countHint(active.subcard_min, active.subcard_max, active.subcard_max)}`);
        if (active.usage_scope !== "none" && active.usage_used >= 0)
          notes.push(`本${({ turn: "回合", round: "輪", phase: "階段", game: "局" } as Record<string, string>)[active.usage_scope] || "次"}已用 ${active.usage_used} 次`);
        if (active.invalid)
          notes.push("此技能實例已失效");
        if (active.expand_pile)
          notes.push(`可用牌堆：${active.expand_pile.split(",").map((name) => tr(name.trim())).join("、")}`);
      }
      root.append(el("p", {}, [notes.join("｜")]));
    }
    if (evaluation?.declarations.length) {
      // One control per dialog shape (guhuo / juguan / tiansuan), never per
      // general. The offered values are the runtime's, not a browser guess.
      const dialog = asString(evaluation.declaration_dialog.type);
      const declaration = el("select");
      const placeholder = el("option", { value: "" }, [DECLARATION_PROMPTS[dialog] ?? "請選擇宣告"]);
      placeholder.disabled = true;
      declaration.append(placeholder);
      for (const value of evaluation.declarations)
        declaration.append(el("option", { value }, [tr(value)]));
      declaration.value = evaluation.declarations.includes(ui.ruleDeclaration) ? ui.ruleDeclaration : "";
      declaration.addEventListener("change", () => {
        const current = nativeEvaluation(bind);
        if (session.interaction !== interaction || !current?.declarations.includes(declaration.value)) {
          bind.render();
          return;
        }
        // Native rules own declaration choices; changing one invalidates its subcards and targets.
        ui.ruleDeclaration = declaration.value;
        ui.selectedCards = [];
        ui.selectedPlayers = [];
        bind.render();
      });
      const field = el("label", { class: "field" }, [
        DECLARATION_LABELS[dialog] ?? "宣告", " ", declaration
      ]);
      // Guhuo and juguan declare a card name, so show the face being claimed.
      if (dialog !== "tiansuan" && ui.ruleDeclaration)
        field.append(assetImg([cardFaceUrl(ui.ruleDeclaration)], UNKNOWN_CARD_URL, "declared-card"));
      root.append(field);
    }

    // Keep explicit removal controls even if a state change hides a selected card
    // or removes a target from the native next-step candidates.
    if (ui.selectedCards.length) {
      const cards = el("div", { class: "cards" });
      ui.selectedCards.forEach((id, index) => {
        const remove = el("button", {}, [`${index + 1}. ${cardLabel(bind, id)} ×`]);
        remove.title = "移除此牌";
        remove.addEventListener("click", () => {
          if (session.interaction !== interaction)
            return;
          ui.selectedCards = ui.selectedCards.filter((_id, selectedIndex) => selectedIndex !== index);
          ui.selectedPlayers = [];
          bind.render();
        });
        cards.append(remove);
      });
      root.append(el("p", {}, [ui.selectedOption ? "子卡順序" : "已選牌"]), cards);
    }
    if (ui.selectedPlayers.length) {
      const targets = el("div", { class: "cards" });
      ui.selectedPlayers.forEach((name, index) => {
        const remove = el("button", {}, [`${index + 1}. ${logPlayerName(session.state, name)} ×`]);
        remove.title = "撤回這一票";
        remove.addEventListener("click", () => {
          if (session.interaction === interaction)
            bind.removeTarget(index);
        });
        targets.append(remove);
      });
      root.append(el("p", {}, ["目標順序（點 × 撤回一票）"]), targets);
    }
    // Hand and equips already have their own rows. Everything else the runtime
    // offers — hand pile, expand pile, a sibling's pile — is grouped by the
    // zone the runtime reported instead of being inferred from ownership.
    const piles = session.state.playerValue(session.state.selfName, "piles");
    const inDashboard = new Set([
      ...session.state.cardsForPlayer(session.state.selfName, PLACE_HAND),
      ...session.state.cardsForPlayer(session.state.selfName, PLACE_EQUIP),
      ...(isObject(piles) ? Object.values(piles).flatMap(asNumberList) : [])
    ]);
    const extras = [...new Set(
      asNumberList(payload.card_ids).concat(
        asNumberList(payload.enabled_card_ids), evaluation?.selectable_cards ?? [])
    )].filter((id) => !inDashboard.has(id));
    const grouped = new Map<string, number[]>();
    for (const id of extras) {
      const zone = evaluation?.card_zones[String(id)]
        ?? (asString(session.state.card(id)?.owner) === session.state.selfName
          ? "expand_pile" : "sibling_pile");
      grouped.set(zone, [...(grouped.get(zone) ?? []), id]);
    }
    for (const [zone, ids] of grouped) {
      const row = el("div", { class: "cards" });
      for (const id of ids)
        row.append(renderCard(bind, id, ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
      root.append(el("p", {}, [ZONE_LABELS[zone] ?? "額外可選牌"]), row);
    }
    const ok = nativeConfirm(bind, command, messageId, submit);
    const pass = command === Command.PLAY_CARD ? el("button", {}, ["結束出牌"]) : cancel;
    if (command === Command.PLAY_CARD)
      pass.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));
    root.append(el("p", {}, ["依序選牌，再點目標頭像加一票；已選項目可單獨移除"]), ok, pass);
    return root;
  }

  if (hasCommand(command, [Command.SHOW_CARD, Command.PINDIAN, Command.EXCHANGE_CARD, Command.DISCARD_CARD])) {
    const extras = [...new Set(
      asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids))
    )].filter((id) => asString(session.state.card(id)?.owner) !== session.state.selfName);
    if (extras.length) {
      const row = el("div", { class: "cards" });
      for (const id of extras)
        row.append(renderCard(bind, id, ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
      root.append(el("p", {}, ["額外可選牌"]), row);
    }
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => {
      if (command === Command.EXCHANGE_CARD || command === Command.DISCARD_CARD) {
        submit(() => replyForCommand(command, { cardIds: ui.selectedCards }));
        return;
      }
      submit(() => replyForCommand(command, { cardText: physicalCardText(bind, ui.selectedCards) }));
    });
    root.append(el("p", {}, ["選牌後送出"]), ok, cancel);
    return root;
  }

  root.append(el("p", { class: "error" }, [`未覆蓋的互動 ${command}`]), cancel);
  return root;
}
