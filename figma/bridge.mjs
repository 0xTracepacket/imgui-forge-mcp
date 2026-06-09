#!/usr/bin/env node
// Realtime bridge between ui_spec.json (ImGui) and the Figma plugin.
//
//   ImGui  --(file watch)-->  bridge  --(WebSocket figmodel)-->  Figma plugin
//   Figma  --(WebSocket figmodel)-->  bridge  --(write spec)-->  ImGui (hot reload)
//
// Conversion runs HERE (Node), so the plugin stays thin: it only paints a
// FigModel onto the canvas and serializes the canvas back to a FigModel.
import { WebSocketServer } from "ws";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { specToFigma, figmaToSpec } from "./convert.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SPEC_PATH = process.env.IMGUI_SPEC_PATH || path.join(__dirname, "..", "spec", "ui_spec.json");
const PORT = Number(process.env.IMGUI_BRIDGE_PORT || 8377);

function loadSpec() {
  try {
    const s = JSON.parse(fs.readFileSync(SPEC_PATH, "utf8"));
    if (s && Array.isArray(s.windows)) return s;
  } catch {}
  return { windows: [] };
}
let ignoreNextChange = 0;
function saveSpec(spec) {
  const tmp = SPEC_PATH + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(spec, null, 2));
  fs.renameSync(tmp, SPEC_PATH);
  ignoreNextChange = Date.now();
}

const wss = new WebSocketServer({ host: "127.0.0.1", port: PORT });
const log = (...a) => console.log("[bridge]", ...a);

function sendFigModel(ws) {
  ws.send(JSON.stringify({ kind: "figmodel", model: specToFigma(loadSpec()) }));
}
function broadcastFigModel() {
  const msg = JSON.stringify({ kind: "figmodel", model: specToFigma(loadSpec()) });
  for (const c of wss.clients) if (c.readyState === 1) c.send(msg);
}

wss.on("connection", (ws) => {
  log("plugin connected");
  sendFigModel(ws); // push current ImGui state so Figma can render it
  ws.on("message", (buf) => {
    let m; try { m = JSON.parse(buf.toString()); } catch { return; }
    if (m.kind === "pull") { sendFigModel(ws); }
    else if (m.kind === "figmodel" && m.model) {
      const spec = figmaToSpec(m.model);
      saveSpec(spec);
      const wins = spec.windows.length;
      log(`imported from Figma -> wrote spec (${wins} window(s))`);
      ws.send(JSON.stringify({ kind: "ack", windows: wins }));
    } else if (m.kind === "ping") {
      ws.send(JSON.stringify({ kind: "pong" }));
    }
  });
  ws.on("close", () => log("plugin disconnected"));
});

// watch the spec file; push to Figma on external changes
let lastMtime = 0;
setInterval(() => {
  let mt = 0;
  try { mt = fs.statSync(SPEC_PATH).mtimeMs; } catch { return; }
  if (mt === lastMtime) return;
  lastMtime = mt;
  // skip the change we just wrote ourselves (avoid echo loop)
  if (Date.now() - ignoreNextChange < 800) return;
  if (wss.clients.size) { log("spec changed -> pushing to Figma"); broadcastFigModel(); }
}, 250);

log(`listening ws://127.0.0.1:${PORT}  spec=${SPEC_PATH}`);
log("start the Figma plugin and it will sync both ways.");
