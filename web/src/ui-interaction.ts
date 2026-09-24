import { UNKNOWN_CARD_URL, assetImg, cardFaceUrl, generalFaceUrls } from "./assets";
import { formatInteractionPrompt, tr } from "./i18n";
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
import { cardsIntent, responseIntent } from "./replies";
import type { RulesEvaluation } from "./rules-client";
import { cardLabel, renderCard } from "./ui-cards";
import { waitingRoom } from "./ui-connect";
import { el, hasCommand } from "./ui-dom";
import type { UiBind } from "./ui-types";

const INTERACTION_TITLES: Record<number, string> = {
  [Command.CHOOSE_ROLE]: "web.interaction.choose_role",
  [Command.CHOOSE_GENERAL]: "web.interaction.choose_general",
  [Command.ASK_GENERAL]: "web.interaction.choose_general",
  [Command.CHOOSE_DIRECTION]: "web.interaction.choose_direction",
  [Command.PLAY_CARD]: "web.interaction.play_card",
  [Command.RESPONSE_CARD]: "web.interaction.response_card",
  [Command.DISCARD_CARD]: "web.interaction.discard_card",
  [Command.EXCHANGE_CARD]: "web.interaction.exchange_card",
  [Command.ASK_PEACH]: "web.interaction.ask_peach",
  [Command.NULLIFICATION]: "web.interaction.nullification",
  [Command.MULTIPLE_CHOICE]: "web.interaction.choose",
  [Command.INVOKE_SKILL]: "web.interaction.invoke_skill",
  [Command.CHOOSE_PLAYER]: "web.interaction.choose_player",
  [Command.CHOOSE_CARD]: "web.interaction.choose_card",
  [Command.CHOOSE_SUIT]: "web.interaction.choose_suit",
  [Command.CHOOSE_KINGDOM]: "web.interaction.choose_kingdom",
  [Command.AMAZING_GRACE]: "web.interaction.amazing_grace",
  [Command.SKILL_GUANXING]: "web.interaction.guanxing",
  [Command.SKILL_GONGXIN]: "web.interaction.gongxin",
  [Command.SKILL_YIJI]: "web.interaction.yiji",
  [Command.PINDIAN]: "web.interaction.pindian",
  [Command.TRIGGER_ORDER]: "web.interaction.trigger_order",
  [Command.ARRANGE_GENERAL]: "web.interaction.arrange_general",
  [Command.LUCK_CARD]: "web.interaction.luck_card",
  [Command.SURRENDER]: "web.interaction.surrender",
  [Command.CHOOSE_ORDER]: "web.interaction.choose_order",
  [Command.CHOOSE_ROLE_3V3]: "web.interaction.choose_role",
  [Command.SHOW_CARD]: "web.interaction.show_card",
  [Command.QML_INTERACT]: "web.interaction.qml_interact"
};

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
    // Hegemony options contain an ordered head+deputy pair; ordinary IDs stay single.
    for (const name of value.split("+"))
      button.append(assetImg(generalFaceUrls(name), "", "portrait"), el("span", {}, [tr(name)]));
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

const RULES_STATUS_TEXT: Record<string, string> = {
  idle: "web.rules.status.idle",
  loading: "web.rules.status.loading",
  evaluating: "web.rules.status.evaluating",
  failed: "web.rules.status.failed",
  unsupported: "web.rules.status.unsupported"
};

const INTERACTION_ERROR_TEXT: Record<string, string> = {
  native_submission_pending: "web.rules.error.native_submission_pending",
  native_reply_invalid: "web.rules.error.native_reply_invalid",
  native_submitter_unavailable: "web.rules.error.native_submitter_unavailable"
};

const RULES_REASON_TEXT: Record<string, string> = {
  select_one_card: "web.rules.reason.select_one_card",
  incomplete_card_selection: "web.rules.reason.incomplete_card_selection",
  declaration_required: "web.rules.reason.declaration_required",
  invalid_declaration: "web.rules.reason.invalid_declaration",
  incomplete_targets: "web.rules.reason.incomplete_targets",
  skill_unavailable: "web.rules.reason.skill_unavailable",
  subcard_rejected: "web.rules.reason.subcard_rejected",
  card_unavailable: "web.rules.reason.card_unavailable",
  card_limited: "web.rules.reason.card_limited",
  pattern_mismatch: "web.rules.reason.pattern_mismatch",
  target_prohibited: "web.rules.reason.target_prohibited",
  target_unavailable: "web.rules.reason.target_unavailable",
  rearrangement_incomplete: "web.rules.reason.rearrangement_incomplete",
  rearrangement_out_of_range: "web.rules.reason.rearrangement_out_of_range",
  selection_count_out_of_range: "web.rules.reason.selection_count_out_of_range",
  incomplete_selection_state: "web.rules.reason.incomplete_selection_state"
};

const DECLARATION_LABELS: Record<string, string> = {
  guhuo: "web.declaration.card_name",
  juguan: "web.declaration.card_name",
  tiansuan: "web.declaration.title"
};

const DECLARATION_PROMPTS: Record<string, string> = {
  guhuo: "web.declaration.choose_card_name",
  juguan: "web.declaration.choose_card_name",
  tiansuan: "web.declaration.choose"
};

const ZONE_LABELS: Record<string, string> = {
  hand: "web.zone.hand",
  equip: "web.zone.equip",
  hand_pile: "web.zone.hand_pile",
  expand_pile: "web.zone.expand_pile",
  sibling_pile: "web.zone.sibling_pile"
};

function uiText(key: string): string {
  return tr(key);
}

function uiFormat(key: string, values: Record<string, string | number>): string {
  // Substitute only the template, never placeholders inside a player value.
  return uiText(key).replace(/%([A-Za-z][A-Za-z0-9_]*)/g,
    (token, name: string) => Object.hasOwn(values, name) ? String(values[name]) : token);
}

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
    evaluation?.known ? uiText("web.rules.status.ready") : uiText(RULES_STATUS_TEXT[rules.status] || "web.rules.status.waiting")
  ])];
  if (rules.error || (evaluation && !evaluation.known))
    nodes.push(el("p", { class: "error" }, [
      rules.error || uiFormat("web.rules.error.reason", { reason: evaluation?.reason || uiText("web.rules.reason.missing") })
    ]));
  else if (evaluation?.reason)
    nodes.push(el("p", { class: "status" }, [
      uiText(RULES_REASON_TEXT[evaluation.reason] || "web.rules.reason.unknown")
    ]));
  return nodes;
}

// Submit the exact evaluated draft; native validation and encoding own the
// eventual reply. Retained DOM controls must never confirm a newer draft.
function nativeConfirm(
  bind: UiBind,
  command: number,
  messageId: string,
  submit: (builder: () => JsonObject) => void,
  label = "web.action.submit"
): HTMLButtonElement {
  const actions = bind.rules.actionModel();
  const selectionKey = JSON.stringify(bind.rulesSelection());
  const ok = el("button", { class: "primary" }, [uiText(label)]);
  ok.disabled = !nativeEvaluation(bind)?.known || !actions?.supported || !actions.can_confirm;
  ok.addEventListener("click", () => submit(() => {
    const current = nativeEvaluation(bind);
    const currentActions = bind.rules.actionModel();
    if (!current?.known || !currentActions?.supported || !currentActions.can_confirm)
      throw new Error(uiText("web.rules.error.incomplete"));
    // A retained/queued button must not confirm a newer selection or reconnect.
    if (!actions || actions.session_generation !== currentActions.session_generation
        || actions.presentation_revision !== currentActions.presentation_revision
        || actions.request_id !== currentActions.request_id
        || bind.session.interaction?.messageId !== messageId
        || bind.session.interaction.command !== command
        || selectionKey !== JSON.stringify(bind.rulesSelection()))
      throw new Error(uiText("web.rules.error.expired"));
    return cardsIntent(bind.rulesSelection());
  }));
  return ok;
}

function countHint(minimum: number, maximum: number, total: number): string {
  if (minimum < 0 || maximum < 0)
    return uiFormat("web.count.maximum", { total });
  return minimum === maximum
    ? uiFormat("web.count.exact", { count: minimum })
    : uiFormat("web.count.range", { minimum, maximum });
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
  // Keep one stable interaction envelope so content changes never move the
  // confirmation/cancellation controls. The existing command branches still
  // own their handlers and payload validation; only their mounting point is
  // normalized here.
  const root = el("div", { class: "interaction-content" });
  const shell = el("section", { class: "interaction prompt" });
  const header = el("header", { class: "interaction-header" });
  const actions = el("div", { class: "interaction-actions" });
  const confirmSlot = el("span", { class: "interaction-action-slot interaction-confirm-slot" });
  const cancelSlot = el("span", { class: "interaction-action-slot interaction-cancel-slot" });
  actions.append(confirmSlot, cancelSlot);
  shell.append(header, root, actions);

  const finalize = (): HTMLElement => {
    const buttons = Array.from(root.children)
      .filter((node): node is HTMLButtonElement => node instanceof HTMLButtonElement);
    if (buttons.length === 2) {
      // Every two-action branch constructs its primary action first and its
      // cancellation/pass action second. This also covers 是／否 and 結束出牌
      // without inferring semantics from translated button text or colour.
      confirmSlot.append(buttons[0]);
      cancelSlot.append(buttons[1]);
    } else if (buttons.length === 1) {
      const button = buttons[0];
      (button.textContent?.trim() === uiText("web.action.cancel") ? cancelSlot : confirmSlot).append(button);
    } else {
      // Auxiliary controls are normally nested in candidate/draft rows. Keep
      // any unexpected direct controls visible in content rather than treating
      // them as confirmation semantics.
      for (const button of buttons)
        root.append(button);
    }
    if (!confirmSlot.querySelector("button")) {
      const placeholder = el("button", { class: "interaction-action-placeholder" }, [uiText("web.action.confirm")]);
      placeholder.type = "button";
      placeholder.disabled = true;
      placeholder.setAttribute("aria-hidden", "true");
      confirmSlot.append(placeholder);
    }
    if (!cancelSlot.querySelector("button")) {
      const placeholder = el("button", { class: "interaction-action-placeholder" }, [uiText("web.action.cancel")]);
      placeholder.type = "button";
      placeholder.disabled = true;
      placeholder.setAttribute("aria-hidden", "true");
      cancelSlot.append(placeholder);
    }
    return shell;
  };
  const interaction = session.interaction;
  if (asBool(session.state.gameValue("game_over"))) {
    root.append(el("p", { class: "status" }, [uiText("web.status.game_over")]));
    return finalize();
  }
  if (!interaction) {
    if (!asBool(session.state.gameValue("started")))
      return waitingRoom(bind);
    root.append(el("p", { class: "status" }, [uiText("web.status.waiting_prompt")]));
    const trust = el("button", {}, [uiText("web.action.trust")]);
    trust.addEventListener("click", () => session.trust(true));
    const surrender = el("button", { class: "danger" }, [uiText("web.action.surrender")]);
    surrender.addEventListener("click", () => session.surrender());
    root.append(trust, surrender);
    return finalize();
  }
  const { command, payload, messageId } = interaction;
  header.append(el("h2", {}, [uiText(INTERACTION_TITLES[command] ?? "web.interaction.unknown")]));
  const prompt = asString(payload.prompt) || asString(payload.skill_name);
  if (prompt) {
    const text = asString(payload.prompt)
      ? formatInteractionPrompt(prompt, (name) => playerLabel(bind, name))
      : tr(prompt);
    header.append(el("p", { class: "interaction-prompt" }, [text]));
  }
  if (session.remainingInteractionMs() !== null)
    header.append(el("span", { class: "interaction-deadline", role: "timer", "aria-live": "off" }));
  if (session.interactionError)
    root.append(el("p", { class: "error" }, [
      uiText(INTERACTION_ERROR_TEXT[session.interactionError] ?? session.interactionError)
    ]));

  // Trust remains available while an active prompt is on screen, but it is an
  // auxiliary control rather than a confirmation/cancellation action.
  if (asBool(session.state.gameValue("started"))) {
    const auxiliary = el("div", { class: "interaction-auxiliary-actions" });
    const trust = el("button", {}, [uiText("web.action.trust")]);
    trust.addEventListener("click", () => session.trust(true));
    auxiliary.append(trust);
    header.append(auxiliary);
  }

  const submit = (builder: () => JsonObject) => {
    try {
      session.sendReply(command, messageId, builder());
      // Native submission is asynchronous. Keep the draft if validation fails;
      // the shared request-change path clears it when the reply is accepted.
    } catch (error) {
      session.interactionError = error instanceof Error ? error.message : String(error);
      bind.render();
    }
  };

  const cancel = el("button", {}, [uiText("web.action.cancel")]);
  cancel.addEventListener("click", () => submit(() => responseIntent(command, { cancelled: true })));

  if (hasCommand(command, [Command.CHOOSE_GENERAL, Command.ASK_GENERAL])) {
    const pairs = command === Command.CHOOSE_GENERAL && asBool(session.state.setup.enable_hegemony)
      ? asStringList(payload.hegemony_pairs) : [];
    if (pairs.length) {
      const candidates = asStringList(payload.candidates);
      const draftText = ui.selectedOption;
      const draft = draftText ? draftText.split("+") : [];
      const canHead = (name: string) => pairs.some((pair) => pair.split("+")[0] === name);
      const current = () => session.interaction === interaction && ui.selectedOption === draftText;
      const change = (names: string[]) => {
        if (!current()) return;
        ui.selectedOption = names.join("+");
        bind.render();
      };
      const remove = (name: string) => {
        const rest = draft.filter((item) => item !== name);
        change(rest.length === 1 && !canHead(rest[0]) ? [] : rest);
      };
      const portrait = (name: string, enabled: boolean, selected: boolean, action: () => void) => {
        const button = el("button", { class: selected ? "primary" : "" });
        button.type = "button";
        button.dataset.focusKey = `hegemony-${name}`;
        button.disabled = !enabled;
        button.setAttribute("aria-pressed", String(selected));
        button.append(assetImg(generalFaceUrls(name), "", "portrait"), el("span", {}, [tr(name)]));
        button.addEventListener("click", action);
        return button;
      };
      root.append(el("p", {}, [uiText("web.hegemony.help")]));
      const pool = el("div", { class: "general-pick" });
      for (const name of candidates.filter((item) => !draft.includes(item))) {
        const enabled = draft.length === 0 ? canHead(name)
          : draft.length === 1 && pairs.includes(`${draft[0]}+${name}`);
        pool.append(portrait(name, enabled, false, () => { if (enabled) change([...draft, name]); }));
      }
      root.append(pool);
      const seats = el("div", { class: "general-pick" });
      for (let seat = 0; seat < 2; ++seat) {
        const slot = el("div");
        slot.append(el("strong", {}, [uiText(seat === 0 ? "web.hegemony.head" : "web.hegemony.deputy")]));
        const name = draft[seat];
        slot.append(name ? portrait(name, true, true, () => remove(name)) : el("p", {}, [uiText("web.hegemony.empty")]));
        seats.append(slot);
      }
      const swap = el("button", {}, [uiText("web.hegemony.swap")]);
      swap.type = "button";
      swap.disabled = draft.length !== 2 || !pairs.includes(`${draft[1]}+${draft[0]}`);
      swap.addEventListener("click", () => { if (!swap.disabled) change([...draft].reverse()); });
      seats.append(swap);
      root.append(seats);
      const ok = el("button", { class: "primary" }, [uiText("web.action.confirm")]);
      ok.disabled = !pairs.includes(draftText);
      ok.addEventListener("click", () => {
        if (current() && pairs.includes(draftText)) submit(() => responseIntent(command, { option: draftText }));
      });
      const clear = el("button", {}, [uiText("web.action.cancel")]);
      clear.disabled = draft.length === 0;
      clear.addEventListener("click", () => change([])); // Clear only; this request is mandatory.
      root.append(ok, clear);
      return finalize();
    }
    const options = payloadOptions(payload);
    root.append(generalPicker(bind, options, (value) => {
      ui.selectedOption = value;
      bind.render();
    }));
    const ok = el("button", { class: "primary" }, [uiText("web.action.confirm")]);
    ok.disabled = !ui.selectedOption || !options.includes(ui.selectedOption);
    ok.addEventListener("click", () => {
      if (!ui.selectedOption)
        return;
      submit(() => responseIntent(command, { option: ui.selectedOption }));
    });
    root.append(ok);
    root.append(cancel);
    return finalize();
  }

  if (hasCommand(command, [
    Command.MULTIPLE_CHOICE,
    Command.CHOOSE_SUIT, Command.CHOOSE_KINGDOM, Command.CHOOSE_DIRECTION,
    Command.TRIGGER_ORDER, Command.CHOOSE_ROLE_3V3
  ])) {
    const options = payloadOptions(payload);
    root.append(optionButtons(bind, options, (value) => {
      ui.selectedOption = value;
      bind.render();
    }));
    const ok = el("button", { class: "primary" }, [uiText("web.action.confirm")]);
    ok.disabled = !ui.selectedOption || !options.includes(ui.selectedOption);
    ok.addEventListener("click", () => {
      if (!ui.selectedOption || !options.includes(ui.selectedOption))
        return;
      submit(() => responseIntent(command, { option: ui.selectedOption }));
    });
    root.append(ok);
    root.append(cancel);
    return finalize();
  }

  if (command === Command.INVOKE_SKILL || command === Command.SURRENDER || command === Command.LUCK_CARD) {
    const yes = el("button", { class: "primary" }, [uiText("web.action.yes")]);
    const no = el("button", {}, [uiText("web.action.no")]);
    yes.addEventListener("click", () => submit(() => responseIntent(command, { option: "yes" })));
    no.addEventListener("click", () => submit(() => responseIntent(command, { option: "no" })));
    root.append(yes, no);
    return finalize();
  }

  if (command === Command.CHOOSE_ORDER) {
    const options = ["0", "1"];
    root.append(optionButtons(bind, options, (value) => {
      ui.selectedOption = value;
      bind.render();
    }));
    const ok = el("button", { class: "primary" }, [uiText("web.action.confirm")]);
    ok.disabled = !options.includes(ui.selectedOption);
    ok.addEventListener("click", () => {
      if (!options.includes(ui.selectedOption))
        return;
      submit(() => responseIntent(command, { option: ui.selectedOption }));
    });
    root.append(ok, cancel);
    return finalize();
  }

  if (command === Command.CHOOSE_ROLE) {
    const players = session.state.playerNames;
    const seatsOnly = asBool(session.state.setup.enable_hegemony);
    const roles = seatsOnly ? players.map((_, index) => String(index + 1))
      : ["lord", "loyalist", "rebel", "renegade"];
    if (seatsOnly) root.append(el("p", {}, [uiText("web.assign_seats.help")]));
    for (const player of players) {
      const select = el("select");
      const chosen = ui.assignments[player] || (seatsOnly ? roles[players.indexOf(player)] : roles[0]);
      for (const role of roles) {
        const option = el("option", { value: role }, [seatsOnly ? role : tr(role)]);
        if (chosen === role)
          option.selected = true;
        select.append(option);
      }
      select.addEventListener("change", () => {
        ui.assignments[player] = select.value;
      });
      root.append(el("label", {}, [`${player} `, select]));
    }
    const ok = el("button", { class: "primary" }, [uiText("web.action.submit")]);
    ok.addEventListener("click", () => submit(() => {
      const assignments: Record<string, string> = {};
      for (const player of players)
        assignments[player] = ui.assignments[player] || (seatsOnly ? roles[players.indexOf(player)] : roles[0]);
      return responseIntent(command, { assignments });
    }));
    root.append(ok, cancel);
    return finalize();
  }

  if (command === Command.CHOOSE_PLAYER) {
    root.append(el("p", {}, [uiText("web.choose_player.instruction")]));
    const ok = el("button", { class: "primary" }, [uiText("web.action.submit")]);
    ok.addEventListener("click", () => submit(() => responseIntent(command, { players: ui.selectedPlayers })));
    root.append(ok, cancel);
    return finalize();
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
      uiFormat("web.guanxing.count", { count, bounds: countHint(asNumber(native[minKey], -1), asNumber(native[maxKey], -1), total) });
    root.append(
      el("p", {}, [uiFormat("web.guanxing.top", { bounds: bounds("min_top", "max_top", ui.top.length) })]),
      zone(ui.top, false, true),
      el("p", {}, [uiFormat("web.guanxing.bottom", { bounds: bounds("min_bottom", "max_bottom", ui.bottom.length) })]),
      zone(ui.bottom, true, false),
      nativeConfirm(bind, command, messageId, submit, "web.action.confirm")
    );
    return finalize();
  }

  if (command === Command.SKILL_YIJI) {
    const evaluation = nativeEvaluation(bind);
    const actionModel = bind.rules.actionModel();
    const native = nativePayload(evaluation);
    const ids = asNumberList(native.cards ?? payload.card_ids);
    const selectable = new Set(actionModel?.cards.filter((item) => item.enabled).map((item) => Number(item.id)) ?? []);
    root.append(...rulesStatus(bind, evaluation));
    root.append(el("p", {}, [
      uiFormat("web.yiji.instruction", { bounds: countHint(asNumber(evaluation?.selection_min, -1),
        asNumber(evaluation?.selection_max, -1), ids.length) })
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
    const candidates = actionModel?.players.filter((item) => item.enabled).map((item) => item.id) ?? [];
    root.append(row, el("p", { class: "status" }, [
      ui.selectedPlayers.length
        ? uiFormat("web.yiji.selected_target", { name: playerLabel(bind, ui.selectedPlayers[0]) })
        : uiFormat("web.yiji.available_targets", { names: candidates.map((name) => playerLabel(bind, name)).join("、") || uiText("web.none") })
    ]));
    root.append(nativeConfirm(bind, command, messageId, submit, "web.yiji.submit"), cancel);
    return finalize();
  }

  if (command === Command.ARRANGE_GENERAL) {
    const generals = asStringList(payload.generals ?? payload.general_names ?? payload.candidates);
    root.append(generalPicker(bind, generals, (value) => {
      ui.selectedOption = value;
      bind.render();
    }));
    const ok = el("button", { class: "primary" }, [uiText("web.arrange.submit_order")]);
    ok.addEventListener("click", () => submit(() => responseIntent(command, {
      generals: ui.selectedOption ? [ui.selectedOption, ...generals.filter((item) => item !== ui.selectedOption)] : generals
    })));
    root.append(ok, cancel);
    return finalize();
  }

  if (command === Command.QML_INTERACT) {
    const area = el("textarea");
    area.value = ui.qmlText;
    area.rows = 6;
    const ok = el("button", { class: "primary" }, [uiText("web.qml.submit_json")]);
    ok.addEventListener("click", () => {
      ui.qmlText = area.value;
      submit(() => responseIntent(command, { qml: JSON.parse(area.value) as JsonObject }));
    });
    root.append(el("p", {}, [uiText("web.qml.instruction")]), area, ok, cancel);
    return finalize();
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
    const ok = el("button", { class: "primary" }, [uiText("web.action.choose_card")]);
    ok.addEventListener("click", () => submit(() => responseIntent(command, { cardId: ui.selectedCards[0] ?? -1 })));
    root.append(row, ok, cancel);
    return finalize();
  }

  if (command === Command.SKILL_GONGXIN) {
    const evaluation = nativeEvaluation(bind);
    const actionModel = bind.rules.actionModel();
    const native = nativePayload(evaluation);
    const ids = [...new Set(asNumberList(native.visible_cards ?? payload.card_ids)
      .concat(asNumberList(payload.enabled_card_ids)))];
    const selectable = new Set(actionModel?.cards.filter((item) => item.enabled).map((item) => Number(item.id)) ?? []);
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
    return finalize();
  }

  if (command === Command.AMAZING_GRACE) {
    const ids = [...new Set(asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids)))];
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, [uiText("web.action.submit")]);
    ok.addEventListener("click", () => submit(() => responseIntent(command, { cardIds: ui.selectedCards })));
    root.append(row, ok, cancel);
    return finalize();
  }

  if (bind.rules.supports(command)) {
    const evaluation = nativeEvaluation(bind);
    root.append(...rulesStatus(bind, evaluation));
    if (ui.selectedOption) {
      const active = evaluation?.skills.find((skill) => skill.name === ui.selectedOption
        && skill.instance_id === ui.skillInstance);
      const notes: string[] = [uiFormat("web.skill.selected", { skill: tr(ui.selectedOption) })];
      if (active) {
        // Amount, committed usage and instance invalidation are reported by the
        // runtime; only canActivate/cardSelectionFeasible decide legality.
        notes.push(active.subcard_min < 0
          ? uiText("web.skill.subcard_runtime")
          : uiFormat("web.skill.subcard_count", { bounds: countHint(active.subcard_min, active.subcard_max, active.subcard_max) }));
        if (active.usage_scope !== "none" && active.usage_used >= 0)
          notes.push(uiFormat("web.skill.usage", { scope: uiText(`web.scope.${active.usage_scope}`), count: active.usage_used }));
        if (active.invalid)
          notes.push(uiText("web.skill.invalid_instance"));
        if (active.expand_pile)
          notes.push(uiFormat("web.skill.available_pile", { names: active.expand_pile.split(",").map((name) => tr(name.trim())).join("、") }));
      }
      root.append(el("p", {}, [notes.join("｜")]));
    }
    if (evaluation?.declarations.length) {
      // One control per dialog shape (guhuo / juguan / tiansuan), never per
      // general. The offered values are the runtime's, not a browser guess.
      const dialog = asString(evaluation.declaration_dialog.type);
      const declaration = el("select");
      const placeholder = el("option", { value: "" }, [uiText(DECLARATION_PROMPTS[dialog] ?? "web.declaration.choose")]);
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
        uiText(DECLARATION_LABELS[dialog] ?? "web.declaration.title"), " ", declaration
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
        remove.title = uiText("web.action.remove_card");
        remove.addEventListener("click", () => {
          if (session.interaction !== interaction)
            return;
          ui.selectedCards = ui.selectedCards.filter((_id, selectedIndex) => selectedIndex !== index);
          ui.selectedPlayers = [];
          bind.render();
        });
        cards.append(remove);
      });
      root.append(el("p", {}, [uiText(ui.selectedOption ? "web.selection.subcard_order" : "web.selection.cards")]), cards);
    }
    if (ui.selectedPlayers.length) {
      const targets = el("div", { class: "cards" });
      ui.selectedPlayers.forEach((name, index) => {
        const remove = el("button", {}, [`${index + 1}. ${playerLabel(bind, name)} ×`]);
        remove.title = uiText("web.action.withdraw_target");
        remove.addEventListener("click", () => {
          if (session.interaction === interaction)
            bind.removeTarget(index);
        });
        targets.append(remove);
      });
      root.append(el("p", {}, [uiText("web.selection.target_order")]), targets);
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
        asNumberList(payload.enabled_card_ids),
        bind.rules.actionModel()?.cards.filter((item) => item.enabled).map((item) => Number(item.id)) ?? [])
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
      root.append(el("p", {}, [uiText(ZONE_LABELS[zone] ?? "web.zone.extra")]), row);
    }
    const ok = nativeConfirm(bind, command, messageId, submit);
    const pass = command === Command.PLAY_CARD ? el("button", {}, [uiText("web.action.finish_play")]) : cancel;
    if (command === Command.PLAY_CARD)
      pass.addEventListener("click", () => submit(() => responseIntent(command, { cancelled: true })));
    root.append(el("p", {}, [uiText("web.selection.instruction")]), ok, pass);
    return finalize();
  }

  if (hasCommand(command, [Command.SHOW_CARD, Command.PINDIAN, Command.EXCHANGE_CARD, Command.DISCARD_CARD])) {
    const extras = [...new Set(
      asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids))
    )].filter((id) => asString(session.state.card(id)?.owner) !== session.state.selfName);
    if (extras.length) {
      const row = el("div", { class: "cards" });
      for (const id of extras)
        row.append(renderCard(bind, id, ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
      root.append(el("p", {}, [uiText("web.zone.extra")]), row);
    }
    const ok = el("button", { class: "primary" }, [uiText("web.action.submit")]);
    ok.addEventListener("click", () => {
      if (command === Command.EXCHANGE_CARD || command === Command.DISCARD_CARD) {
        submit(() => responseIntent(command, { cardIds: ui.selectedCards }));
        return;
      }
      submit(() => responseIntent(command, { cardIds: ui.selectedCards, targets: ui.selectedPlayers }));
    });
    root.append(el("p", {}, [uiText("web.selection.submit_instruction")]), ok, cancel);
    return finalize();
  }

  root.append(el("p", { class: "error" }, [uiFormat("web.interaction.unsupported", { command })]), cancel);
  return finalize();
}
function playerLabel(bind: UiBind, name: string): string {
  const player = bind.session.state.player(name);
  return asString(player?.screen_name) || asString(player?.label) || name;
}
