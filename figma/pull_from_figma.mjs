// Pull Figma's current model (export.json) back into ImGui via the bridge.
// Mirrors what the plugin's "Push to ImGui" does.
import fs from "node:fs";
import WS from "ws";
const model = JSON.parse(fs.readFileSync(new URL("./export.json", import.meta.url), "utf8"));
const ws = new WS("ws://127.0.0.1:8377");
ws.on("open", () => { ws.send(JSON.stringify({ kind: "figmodel", model })); console.log("[pull] sent Figma model to bridge"); });
ws.on("message", (b) => {
  const m = JSON.parse(b);
  if (m.kind === "ack") { console.log("[pull] bridge wrote spec, windows:", m.windows); ws.close(); process.exit(0); }
});
ws.on("error", (e) => { console.log("ERR", e.message); process.exit(1); });
setTimeout(() => { console.log("timeout (bridge running?)"); process.exit(1); }, 6000);
