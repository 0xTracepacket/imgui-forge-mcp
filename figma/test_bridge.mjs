// Simulates the Figma plugin: connect, receive the FigModel (ImGui->Figma),
// edit it, push back (Figma->ImGui). Proves the realtime bridge both ways.
import WebSocket from "ws";
const ws = new WebSocket("ws://127.0.0.1:8377");

function editModel(model) {
  // walk and flip the first checkbox value
  let flipped = false;
  (function walk(n) {
    if (!n) return;
    if (!flipped && n.imgui && n.imgui.type === "checkbox") { n.imgui.props.value = !n.imgui.props.value; flipped = true; }
    (n.children || []).forEach(walk);
  })(model);
  return model;
}

ws.on("open", () => console.log("[mock-plugin] connected"));
ws.on("message", (buf) => {
  const m = JSON.parse(buf.toString());
  if (m.kind === "figmodel") {
    console.log("[mock-plugin] received FigModel from ImGui (export OK)");
    const edited = editModel(m.model);
    ws.send(JSON.stringify({ kind: "figmodel", model: edited }));
    console.log("[mock-plugin] pushed edited model back (toggled first checkbox)");
  } else if (m.kind === "ack") {
    console.log("[mock-plugin] bridge ack: wrote", m.windows, "window(s) -> ImGui will hot-reload");
    ws.close(); process.exit(0);
  }
});
ws.on("error", (e) => { console.error("ERR", e.message); process.exit(1); });
setTimeout(() => { console.error("timeout"); process.exit(1); }, 6000);
