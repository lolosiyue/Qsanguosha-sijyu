// Web client full-game driver over Chrome DevTools Protocol.
// Drives the real built UI: connect -> fill lobby -> ready -> pick general -> trust.
// Requires: vite preview serving web/dist, qsanguosha_server with --websocket-port,
// and Chrome/Edge started with --remote-debugging-port.
import fs from "node:fs";

const args = {};
for (let i = 2; i < process.argv.length; i += 2)
  args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];

const CDP_PORT = args["cdp-port"] ?? "9222";
const PAGE_URL = args.url ?? "http://127.0.0.1:5173/";
const GENERAL = args.general ?? "s4_sunjian";
const MARKER = args.marker ?? "";
const NAME = args.name ?? "web-driver";
const TIMEOUT_MS = Number(args["timeout-min"] ?? 45) * 60_000;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function pageTarget() {
  for (;;) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json();
      const page = list.find((t) => t.type === "page" && t.url.startsWith(PAGE_URL.replace(/\/$/, "")));
      if (page) return page;
    } catch { /* browser not up yet */ }
    await sleep(500);
  }
}

const target = await pageTarget();
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  ws.addEventListener("open", resolve, { once: true });
  ws.addEventListener("error", reject, { once: true });
});

let seq = 0;
const pendingCalls = new Map();
const consoleLines = [];
ws.addEventListener("message", (event) => {
  const msg = JSON.parse(event.data);
  if (msg.id && pendingCalls.has(msg.id)) {
    pendingCalls.get(msg.id)(msg);
    pendingCalls.delete(msg.id);
  } else if (msg.method === "Runtime.consoleAPICalled") {
    const text = msg.params.args.map((a) => a.value ?? a.description ?? "").join(" ");
    consoleLines.push(`[${msg.params.type}] ${text}`);
  } else if (msg.method === "Runtime.exceptionThrown") {
    consoleLines.push(`[exception] ${JSON.stringify(msg.params.exceptionDetails.exception?.description ?? msg.params)}`);
  }
});
function cdp(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve) => pendingCalls.set(id, resolve));
}
async function evalJs(expression) {
  const res = await cdp("Runtime.evaluate", {
    expression, returnByValue: true, awaitPromise: true
  });
  if (res.result?.exceptionDetails)
    throw new Error(JSON.stringify(res.result.exceptionDetails));
  return res.result?.result?.value;
}

await cdp("Runtime.enable");
await cdp("Page.enable");

// Wait for the connect form, fill it, submit.
for (;;) {
  if (await evalJs(`!!document.querySelector('#connect-name')`)) break;
  await sleep(500);
}
await evalJs(`(() => {
  const set = (id, v) => { const e = document.querySelector(id);
    e.value = v; e.dispatchEvent(new Event('input', { bubbles: true })); };
  set('#connect-name', ${JSON.stringify(NAME)});
  set('#connect-avatar', ${JSON.stringify(GENERAL)});
  document.querySelector('.connect-form').requestSubmit();
  return true;
})()`);
console.log("[driver] signup submitted, avatar =", GENERAL);

// In-page autopilot: fills lobby, readies, assigns roles (FreeAssign CHOOSE_ROLE
// panel), picks the designated general (rewritten over the wire when the server
// allows FreeChoose), then enables trust so the server AI plays the seat out.
await evalJs(`(() => {
  window.__auto = { robots:false, ready:false, roleAssigned:false, picked:false,
                    confirmed:false, trusted:false, done:false, result:"",
                    pickerSeen:0, genRewritten:0 };
  const byText = (t) => [...document.querySelectorAll("button")]
    .find(b => b.textContent.trim() === t && !b.disabled);
  const panelOf = (title) => {
    const h = [...document.querySelectorAll("h2")].find(x => x.textContent.includes(title));
    return h ? h.closest("section") : null;
  };
  // Rewrite CHOOSE_GENERAL(10)/ASK_GENERAL(65) replies so the server-side
  // FreeChoose cheat accepts the designated general even when it is not in
  // the offered list. WebSocket.prototype.send is resolved at call time.
  if (!window.__genPatcher) {
    const origSend = WebSocket.prototype.send;
    WebSocket.prototype.send = function (data) {
      if (typeof data === "string") {
        try {
          const m = JSON.parse(data);
          if (m && m.type === "reply" && (m.command === 10 || m.command === 65)
              && m.payload && typeof m.payload === "object") {
            m.payload.general = ${JSON.stringify(GENERAL)};
            window.__auto.genRewritten++;
            data = JSON.stringify(m);
          }
        } catch (e) { /* non-JSON frame */ }
      }
      return origSend.call(this, data);
    };
    window.__genPatcher = true;
  }
  setInterval(() => {
    const a = window.__auto;
    if (document.querySelector(".wait-panel")) {
      if (!a.robots) { const b = byText("加滿機器人"); if (b) { b.click(); a.robots = true; } }
      else if (!a.ready) { const b = byText("準備"); if (b) { b.click(); a.ready = true; } }
    }
    // FreeAssign: assign a valid 20p spread (1 lord / 8 loyalist / 10 rebel /
    // 1 renegade) across the seat selects, then submit.
    const rolePanel = panelOf("分配身分");
    if (rolePanel && !a.roleAssigned) {
      const sels = [...rolePanel.querySelectorAll("select")];
      const roles = ["lord", "loyalist","loyalist","loyalist","loyalist",
        "loyalist","loyalist","loyalist","loyalist",
        "rebel","rebel","rebel","rebel","rebel","rebel","rebel","rebel","rebel","rebel",
        "renegade"];
      if (sels.length) {
        sels.forEach((s, i) => {
          if (roles[i]) { s.value = roles[i]; s.dispatchEvent(new Event("change", { bubbles: true })); }
        });
        a.roleAssigned = true;
      }
    } else if (a.roleAssigned && !a.roleSent) {
      const ok = rolePanel && [...rolePanel.querySelectorAll("button")]
        .find(b => b.textContent.trim() === "送出" && !b.disabled);
      if (ok) { ok.click(); a.roleSent = true; }
    }
    const pick = document.querySelector(".general-pick");
    if (pick && !a.picked) {
      a.pickerSeen++;
      const btns = [...pick.querySelectorAll("button")];
      const btn = btns.find(b => (b.querySelector("img")?.getAttribute("src") ?? "").includes(${JSON.stringify(GENERAL)}))
        ?? btns[0];
      if (btn) { btn.click(); a.picked = true; }
    } else if (a.picked && !a.confirmed) {
      const ok = byText("確定");
      if (ok) { ok.click(); a.confirmed = true; }
    }
    if (a.confirmed && !a.trusted) {
      const t = byText("託管");
      if (t) { t.click(); a.trusted = true; }
    }
    const result = document.querySelector("section.game-result");
    if (result) { a.done = true; a.result = result.innerText; }
  }, 150);
  return true;
})()`);
console.log("[driver] autopilot installed");

const deadline = Date.now() + TIMEOUT_MS;
let last = "";
for (;;) {
  const state = await evalJs(`JSON.stringify({ a: window.__auto, phase: document.querySelector('.phase-badge')?.textContent ?? '' })`)
    .then((s) => JSON.parse(s));
  const brief = state.a
    ? `robots=${state.a.robots} ready=${state.a.ready} roles=${state.a.roleSent ?? false} pickers=${state.a.pickerSeen} picked=${state.a.picked} gen_rw=${state.a.genRewritten ?? 0} confirmed=${state.a.confirmed} trusted=${state.a.trusted} done=${state.a.done}`
    : "no-state";
  if (brief !== last) { console.log(`[driver] ${new Date().toISOString()} ${brief}`); last = brief; }
  if (state.a?.done) {
    console.log("[driver] GAME RESULT:\n" + state.a.result);
    break;
  }
  if (MARKER && fs.existsSync(MARKER)) {
    const tail = fs.readFileSync(MARKER, "utf8").trim().split("\n").slice(-3).join("\n");
    if (/GAME_OVER|game_over|winner/i.test(tail)) {
      console.log("[driver] server marker reports game over:\n" + tail);
    }
  }
  if (Date.now() > deadline) {
    console.log("[driver] TIMEOUT waiting for game over");
    break;
  }
  await sleep(5000);
}

const shot = await cdp("Page.captureScreenshot", { format: "png" });
if (shot.result?.data) {
  fs.writeFileSync("web-client-final.png", Buffer.from(shot.result.data, "base64"));
  console.log("[driver] screenshot saved: web-client-final.png");
}
if (consoleLines.length) {
  fs.writeFileSync("web-client-console.log", consoleLines.join("\n"));
  console.log(`[driver] ${consoleLines.length} console lines saved: web-client-console.log`);
}
ws.close();
process.exit(0);
