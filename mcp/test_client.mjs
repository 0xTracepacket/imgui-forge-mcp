// Smoke test: spin up server.mjs over stdio, exercise the tools.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import path from "node:path";
import { fileURLToPath } from "node:url";
const __dirname = path.dirname(fileURLToPath(import.meta.url));

const transport = new StdioClientTransport({
  command: process.execPath,
  args: [path.join(__dirname, "server.mjs")],
  env: { ...process.env, IMGUI_SPEC_PATH: path.join(__dirname, "..", "spec", "ui_spec.json") },
});
const client = new Client({ name: "test", version: "1.0.0" });
await client.connect(transport);

const tools = await client.listTools();
console.log("TOOLS:", tools.tools.map((t) => t.name).join(", "));

const call = async (name, args = {}) => {
  const r = await client.callTool({ name, arguments: args });
  return r.content?.[0]?.text ?? "";
};

console.log("\n-- add_window --");
console.log(await call("add_window", {
  window: { type: "window", props: { title: "Test Panel", size: [320, 240] }, children: [] },
}));

// grab the id we just created
const spec = JSON.parse(await call("get_spec"));
const winId = spec.windows[spec.windows.length - 1].id;
console.log("new window id:", winId);

console.log("\n-- add_widget (slider) --");
console.log(await call("add_widget", {
  parent_id: winId,
  widget: { type: "slider_float", props: { label: "Speed", value: 0.3, min: 0, max: 5 } },
}));

console.log("\n-- list_widgets --");
console.log(await call("list_widgets"));

console.log("\n-- export_cpp --");
console.log((await call("export_cpp")).split("\n").slice(0, 12).join("\n"), "\n...");

await client.close();
console.log("\nOK");
