#!/usr/bin/env node
// imgui-tool MCP server.
//
// Exposes tools that read and mutate spec/ui_spec.json (the single source of
// truth for the live ImGui preview). Every write is atomic (temp file + rename)
// so the C++ host never reads a half-written file; the host polls the file and
// hot-reloads, so changes appear in the window within ~250ms.
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
// spec lives at <project>/spec/ui_spec.json; server lives at <project>/mcp/
const SPEC_PATH =
  process.env.IMGUI_SPEC_PATH || path.join(__dirname, "..", "spec", "ui_spec.json");

// ---------------------------------------------------------------------------
// spec io
// ---------------------------------------------------------------------------
function loadSpec() {
  try {
    const txt = fs.readFileSync(SPEC_PATH, "utf8");
    const s = JSON.parse(txt);
    if (!s || typeof s !== "object" || !Array.isArray(s.windows)) {
      return { windows: [] };
    }
    return s;
  } catch {
    return { windows: [] };
  }
}

function saveSpec(spec) {
  fs.mkdirSync(path.dirname(SPEC_PATH), { recursive: true });
  const tmp = SPEC_PATH + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(spec, null, 2));
  fs.renameSync(tmp, SPEC_PATH); // atomic on same volume
}

// ---------------------------------------------------------------------------
// tree helpers
// ---------------------------------------------------------------------------
let idCounter = 0;
function genId(type) {
  idCounter += 1;
  return `${type || "w"}_${Date.now().toString(36)}_${idCounter}`;
}

// every node gets a stable id (needed for live value persistence in the host)
function ensureIds(node) {
  if (!node || typeof node !== "object") return node;
  if (!node.id) node.id = genId(node.type);
  const kids = node.children;
  if (Array.isArray(kids)) kids.forEach(ensureIds);
  return node;
}

// depth-first search across windows[] and any nested children[]
function findNode(spec, id, withParent = false) {
  function walk(list, parent) {
    for (let i = 0; i < list.length; i++) {
      const n = list[i];
      if (!n || typeof n !== "object") continue;
      if (n.id === id) return { node: n, list, index: i, parent };
      if (Array.isArray(n.children)) {
        const r = walk(n.children, n);
        if (r) return r;
      }
    }
    return null;
  }
  const r = walk(spec.windows, null);
  if (!r) return null;
  return withParent ? r : r.node;
}

function summarize(spec) {
  const lines = [];
  function walk(list, depth) {
    for (const n of list) {
      if (!n || typeof n !== "object") continue;
      const p = n.props || {};
      const label = p.title || p.label || p.text || "";
      lines.push(`${"  ".repeat(depth)}- [${n.type}] id=${n.id}${label ? ` "${label}"` : ""}`);
      if (Array.isArray(n.children)) walk(n.children, depth + 1);
    }
  }
  walk(spec.windows, 0);
  return lines.length ? lines.join("\n") : "(empty — no windows)";
}

const ok = (text) => ({ content: [{ type: "text", text }] });
const json = (obj) => ({ content: [{ type: "text", text: JSON.stringify(obj, null, 2) }] });

// ---------------------------------------------------------------------------
// widget catalog
// ---------------------------------------------------------------------------
const CATALOG = {
  windows: "Top-level. type=window. props: title, pos[x,y], size[w,h], open(bool), closable(bool), flags[] (NoTitleBar,NoResize,NoMove,NoScrollbar,NoCollapse,AlwaysAutoResize,NoBackground,NoDecoration,MenuBar,...). children[].",
  text: "type=text props{text,color[r,g,b,a]?}; text_disabled{text}; text_wrapped{text}; bullet_text{text}; label_text{label,text}; separator_text{text}; bullet{}",
  buttons: "button{label,size[w,h]?}; small_button{label}; arrow_button{dir:Left|Right|Up|Down}; checkbox{label,value}; radio_button{options[],value(int),horizontal?}; selectable{label,value(bool)}; progress_bar{fraction,overlay?,size?}",
  inputs: "input_text{label,value,hint?}; input_text_multiline{label,value,size?}; input_int{label,value,step?}; input_float{label,value,step?,format?}",
  sliders: "slider_int{label,value,min,max}; slider_float{label,value,min,max,format?}; slider_float2|3|4{label,value[],min,max}; slider_angle{label,value,min,max}; vslider_float{label,value,min,max,size?}",
  drags: "drag_int{label,value,speed?,min?,max?}; drag_float{label,value,speed?,min?,max?,format?}; drag_float2|3|4{label,value[],speed?,min?,max?}",
  selection: "combo{label,items[],value(int)}; listbox{label,items[],value(int),size?}",
  color: "color_edit3|color_edit4{label,value[r,g,b(,a)]}; color_picker3|color_picker4{...}; color_button{label,value[]}",
  plots: "plot_lines{label,values[],overlay?,size?}; plot_histogram{...}",
  layout: "separator{}; same_line{offset?,spacing?}; new_line{}; spacing{}; dummy{size[w,h]}; indent{width?}; unindent{width?}",
  containers: "group{children}; child{size[w,h],border?,children}; collapsing_header{label,default_open?,children}; tree_node{label,children}; tab_bar{children:[tab_item{label,children}]}; table{columns(int),headers[],children:cells}; columns{count,border?,children}",
  menus: "menu_bar{children:[menu]}  (window auto-gets MenuBar flag); menu{label,children:[menu_item|menu]}; menu_item{label,shortcut?}",
  popups: "popup{label,children}  (button opens inline popup); modal{label,children}",
  misc: "tooltip{children}  (attaches to previous sibling); disabled{disabled(bool),children}; style_color{target(ImGuiCol name),color[],children}; style_var{target,value(float or [x,y]),children}. Any node may also set props.tooltip='text'.",
};

// ---------------------------------------------------------------------------
// C++ export (immediate-mode code generator)
// ---------------------------------------------------------------------------
function cppStr(s) {
  return '"' + String(s ?? "").replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
}
// float literal, always with a decimal point so it compiles ("0.0f" not "0f")
function flt(x) {
  const n = Number(x) || 0;
  return (Number.isInteger(n) ? n.toFixed(1) : String(n)) + "f";
}
function exportCpp(spec) {
  const out = [];
  out.push("// Generated by imgui-tool. Paste into a Dear ImGui frame.");
  out.push("// Static locals hold widget state across frames.");
  out.push("void RenderUI() {");
  let uid = 0;
  const v = (p) => `v${uid++}`;
  function emit(node, ind) {
    const pad = "    ".repeat(ind);
    const t = node.type;
    const p = node.props || {};
    const kids = Array.isArray(node.children) ? node.children : [];
    const L = cppStr(p.label ?? p.title ?? "");
    switch (t) {
      case "text": out.push(`${pad}ImGui::TextUnformatted(${cppStr(p.text)});`); break;
      case "text_disabled": out.push(`${pad}ImGui::TextDisabled("%s", ${cppStr(p.text)});`); break;
      case "text_wrapped": out.push(`${pad}ImGui::TextWrapped("%s", ${cppStr(p.text)});`); break;
      case "bullet_text": out.push(`${pad}ImGui::BulletText("%s", ${cppStr(p.text)});`); break;
      case "separator": out.push(`${pad}ImGui::Separator();`); break;
      case "separator_text": out.push(`${pad}ImGui::SeparatorText(${cppStr(p.text)});`); break;
      case "same_line": out.push(`${pad}ImGui::SameLine();`); break;
      case "spacing": out.push(`${pad}ImGui::Spacing();`); break;
      case "button": out.push(`${pad}if (ImGui::Button(${L})) { /* TODO */ }`); break;
      case "checkbox": { const n = v(); out.push(`${pad}static bool ${n} = ${p.value ? "true" : "false"}; ImGui::Checkbox(${L}, &${n});`); break; }
      case "slider_float": { const n = v(); out.push(`${pad}static float ${n} = ${flt(p.value)}; ImGui::SliderFloat(${L}, &${n}, ${flt(p.min)}, ${flt(p.max ?? 1)});`); break; }
      case "slider_int": { const n = v(); out.push(`${pad}static int ${n} = ${(+p.value || 0)}; ImGui::SliderInt(${L}, &${n}, ${(+p.min||0)}, ${(+p.max||100)});`); break; }
      case "input_text": { const n = v(); out.push(`${pad}static char ${n}[256] = ${cppStr(p.value)}; ImGui::InputText(${L}, ${n}, sizeof(${n}));`); break; }
      case "input_int": { const n = v(); out.push(`${pad}static int ${n} = ${(+p.value||0)}; ImGui::InputInt(${L}, &${n});`); break; }
      case "input_float": { const n = v(); out.push(`${pad}static float ${n} = ${(+p.value||0).toFixed(3)}f; ImGui::InputFloat(${L}, &${n});`); break; }
      case "combo": {
        const n = v(); const items = (p.items || []).map(cppStr).join(", ");
        out.push(`${pad}static int ${n} = ${(+p.value||0)}; const char* ${n}_items[] = { ${items} };`);
        out.push(`${pad}ImGui::Combo(${L}, &${n}, ${n}_items, IM_ARRAYSIZE(${n}_items));`);
        break;
      }
      case "color_edit3": { const n = v(); const c = (p.value||[1,1,1]).slice(0,3).map(x=>(+x).toFixed(3)+"f").join(", "); out.push(`${pad}static float ${n}[3] = { ${c} }; ImGui::ColorEdit3(${L}, ${n});`); break; }
      case "color_edit4": { const n = v(); const c = (p.value||[1,1,1,1]).slice(0,4).map(x=>(+x).toFixed(3)+"f").join(", "); out.push(`${pad}static float ${n}[4] = { ${c} }; ImGui::ColorEdit4(${L}, ${n});`); break; }
      case "group": out.push(`${pad}ImGui::BeginGroup();`); kids.forEach(k => emit(k, ind)); out.push(`${pad}ImGui::EndGroup();`); break;
      case "child": out.push(`${pad}if (ImGui::BeginChild("##c${uid++}")) {`); kids.forEach(k => emit(k, ind + 1)); out.push(`${pad}}\n${pad}ImGui::EndChild();`); break;
      case "collapsing_header": out.push(`${pad}if (ImGui::CollapsingHeader(${L})) {`); kids.forEach(k => emit(k, ind + 1)); out.push(`${pad}}`); break;
      case "tree_node": out.push(`${pad}if (ImGui::TreeNode(${L})) {`); kids.forEach(k => emit(k, ind + 1)); out.push(`${pad}    ImGui::TreePop();\n${pad}}`); break;
      case "tab_bar":
        out.push(`${pad}if (ImGui::BeginTabBar("##tb${uid++}")) {`);
        kids.filter(k => k.type === "tab_item").forEach(tab => {
          out.push(`${pad}    if (ImGui::BeginTabItem(${cppStr((tab.props||{}).label||"Tab")})) {`);
          (tab.children||[]).forEach(k => emit(k, ind + 2));
          out.push(`${pad}        ImGui::EndTabItem();\n${pad}    }`);
        });
        out.push(`${pad}    ImGui::EndTabBar();\n${pad}}`);
        break;
      default: out.push(`${pad}// TODO: widget type "${t}" — see UiRenderer.cpp for the runtime version`);
    }
  }
  for (const w of spec.windows) {
    const p = w.props || {};
    out.push(`    if (ImGui::Begin(${cppStr(p.title || "Window")})) {`);
    (w.children || []).forEach(k => emit(k, 2));
    out.push("    }");
    out.push("    ImGui::End();");
  }
  out.push("}");
  return out.join("\n");
}

// ---------------------------------------------------------------------------
// server + tools
// ---------------------------------------------------------------------------
const server = new McpServer({ name: "imgui-tool", version: "1.0.0" });

const NODE_SHAPE = z
  .object({
    id: z.string().optional(),
    type: z.string(),
    props: z.record(z.any()).optional(),
    children: z.array(z.any()).optional(),
  })
  .passthrough();

server.registerTool(
  "get_spec",
  { title: "Get spec", description: "Return the full current UI spec (the live document) as JSON.", inputSchema: {} },
  async () => json(loadSpec())
);

server.registerTool(
  "set_spec",
  {
    title: "Set spec",
    description: "Replace the ENTIRE spec. Must be { windows: [...] }. Use for big rewrites; prefer add/update/remove for edits.",
    inputSchema: { spec: z.object({ windows: z.array(z.any()) }).passthrough() },
  },
  async ({ spec }) => {
    spec.windows.forEach(ensureIds);
    saveSpec(spec);
    return ok(`Spec replaced. ${spec.windows.length} window(s).\n` + summarize(spec));
  }
);

server.registerTool(
  "list_widgets",
  { title: "List widgets", description: "Tree summary of every window/widget with ids, types, and labels.", inputSchema: {} },
  async () => ok(summarize(loadSpec()))
);

server.registerTool(
  "widget_catalog",
  { title: "Widget catalog", description: "List every supported widget type and its props, so you know what you can build.", inputSchema: {} },
  async () => json(CATALOG)
);

server.registerTool(
  "add_window",
  {
    title: "Add window",
    description: "Add a top-level window. Provide a window node (type=window). Returns the assigned id.",
    inputSchema: { window: NODE_SHAPE },
  },
  async ({ window }) => {
    const spec = loadSpec();
    if (window.type !== "window") window.type = "window";
    if (!window.children) window.children = [];
    ensureIds(window);
    spec.windows.push(window);
    saveSpec(spec);
    return ok(`Added window id=${window.id}\n` + summarize(spec));
  }
);

server.registerTool(
  "add_widget",
  {
    title: "Add widget",
    description: "Insert a widget node into a parent (window/group/child/header/tab_item/etc.) by parent_id. Optional index for position; appends if omitted. Returns the new widget id.",
    inputSchema: {
      parent_id: z.string().describe("id of the container to insert into"),
      widget: NODE_SHAPE,
      index: z.number().int().optional(),
    },
  },
  async ({ parent_id, widget, index }) => {
    const spec = loadSpec();
    const parent = findNode(spec, parent_id);
    if (!parent) return ok(`ERROR: parent_id "${parent_id}" not found.`);
    if (!Array.isArray(parent.children)) parent.children = [];
    ensureIds(widget);
    if (typeof index === "number" && index >= 0 && index <= parent.children.length)
      parent.children.splice(index, 0, widget);
    else parent.children.push(widget);
    saveSpec(spec);
    return ok(`Added ${widget.type} id=${widget.id} into ${parent_id}.\n` + summarize(spec));
  }
);

server.registerTool(
  "update_widget",
  {
    title: "Update widget",
    description: "Patch a widget by id. Merges given props into its existing props; optionally changes its type. Pass props:{} keys to overwrite individual values.",
    inputSchema: {
      id: z.string(),
      props: z.record(z.any()).optional(),
      type: z.string().optional(),
    },
  },
  async ({ id, props, type }) => {
    const spec = loadSpec();
    const node = findNode(spec, id);
    if (!node) return ok(`ERROR: id "${id}" not found.`);
    if (type) node.type = type;
    if (props) node.props = { ...(node.props || {}), ...props };
    saveSpec(spec);
    return ok(`Updated id=${id}: ` + JSON.stringify({ type: node.type, props: node.props }));
  }
);

server.registerTool(
  "remove_widget",
  {
    title: "Remove widget",
    description: "Remove a widget or window by id (and all its children).",
    inputSchema: { id: z.string() },
  },
  async ({ id }) => {
    const spec = loadSpec();
    const r = findNode(spec, id, true);
    if (!r) return ok(`ERROR: id "${id}" not found.`);
    r.list.splice(r.index, 1);
    saveSpec(spec);
    return ok(`Removed id=${id}.\n` + summarize(spec));
  }
);

server.registerTool(
  "move_widget",
  {
    title: "Move widget",
    description: "Move/reparent a widget by id into new_parent_id at optional index.",
    inputSchema: {
      id: z.string(),
      new_parent_id: z.string(),
      index: z.number().int().optional(),
    },
  },
  async ({ id, new_parent_id, index }) => {
    const spec = loadSpec();
    const r = findNode(spec, id, true);
    if (!r) return ok(`ERROR: id "${id}" not found.`);
    const parent = findNode(spec, new_parent_id);
    if (!parent) return ok(`ERROR: new_parent_id "${new_parent_id}" not found.`);
    if (parent === r.node) return ok("ERROR: cannot move a node into itself.");
    r.list.splice(r.index, 1);
    if (!Array.isArray(parent.children)) parent.children = [];
    if (typeof index === "number" && index >= 0 && index <= parent.children.length)
      parent.children.splice(index, 0, r.node);
    else parent.children.push(r.node);
    saveSpec(spec);
    return ok(`Moved id=${id} into ${new_parent_id}.\n` + summarize(spec));
  }
);

server.registerTool(
  "clear",
  { title: "Clear", description: "Remove all windows (blank canvas).", inputSchema: {} },
  async () => {
    saveSpec({ windows: [] });
    return ok("Cleared. Canvas is empty.");
  }
);

server.registerTool(
  "export_cpp",
  {
    title: "Export C++",
    description: "Generate immediate-mode Dear ImGui C++ code equivalent to the current spec, to paste directly into your own project (no JSON/interpreter needed).",
    inputSchema: {},
  },
  async () => ok("```cpp\n" + exportCpp(loadSpec()) + "\n```")
);

server.registerTool(
  "screenshot",
  {
    title: "Screenshot preview",
    description: "Capture the live preview window and return the rendered image. Requires the host (imgui_tool.exe) to be running.",
    inputSchema: {},
  },
  async () => {
    const dir = path.dirname(SPEC_PATH);
    const req = path.join(dir, ".shot");
    const out = path.join(dir, "preview.png");
    let prevMtime = 0;
    try { prevMtime = fs.statSync(out).mtimeMs; } catch {}
    fs.writeFileSync(req, "1"); // request a capture on the next frame
    // wait up to ~4s for the host to produce a fresh png
    const deadline = Date.now() + 4000;
    while (Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 100));
      let m = 0;
      try { m = fs.statSync(out).mtimeMs; } catch {}
      const consumed = !fs.existsSync(req);
      if (consumed && m > prevMtime) {
        const b64 = fs.readFileSync(out).toString("base64");
        return { content: [{ type: "image", data: b64, mimeType: "image/png" }] };
      }
    }
    try { fs.unlinkSync(req); } catch {}
    return ok("ERROR: no screenshot produced — is imgui_tool.exe running? (build\\imgui_tool.exe spec\\ui_spec.json)");
  }
);

const transport = new StdioServerTransport();
await server.connect(transport);
