# imgui-forge

> Design Dear ImGui menus from a JSON spec — live C++ preview with hot-reload, an
> MCP server for programmatic edits, and a bidirectional Figma bridge.

A JSON file (`spec/ui_spec.json`) describes the UI; a native C++ host renders it and
**hot-reloads on every change**, so editing the spec updates the window live. An MCP
server exposes tools to edit the spec programmatically, and a Figma bridge syncs the
design both ways.

`imgui-tool` is the name of the bundled MCP server (`mcp/server.mjs`).

```
   spec/ui_spec.json  ──(file watch)──▶  C++ host window (live preview)
        ▲  ▲
        │  └── MCP server (mcp/server.mjs): add/update/remove widgets
        └───── Figma bridge (figma/): design in Figma, sync both directions
```

## Layout

| Path | What |
|------|------|
| `spec/ui_spec.json` | The UI document (source of truth) |
| `host/UiRenderer.h` / `.cpp` | Reusable spec→ImGui interpreter — drop into your own project |
| `host/main.cpp` | Preview app: Win32 + DirectX11 + hot reload + screenshot |
| `build.bat` | Compiles the host with MSVC (auto-detects VS via vswhere; no cmake) |
| `mcp/server.mjs` | MCP server exposing the editing tools |
| `figma/` | Figma ↔ ImGui bidirectional bridge (see `FIGMA.md`) |
| `.mcp.json` | MCP client registration |
| `vendor/` | Dear ImGui (docking) + nlohmann/json + stb_image_write |

## Setup

1. Build the host (needs Visual Studio with the C++ workload):
   ```
   build.bat
   ```
   Produces `build/imgui_tool.exe`.
2. Install MCP server deps:
   ```
   cd mcp && npm install
   ```
3. Register the MCP server. `.mcp.json` ships in this folder, so an MCP client
   opened here picks it up automatically. Otherwise register the stdio command
   `node mcp/server.mjs` in your client's MCP config.

## Use

1. Launch the preview:
   ```
   build\imgui_tool.exe spec\ui_spec.json
   ```
2. Edit `spec/ui_spec.json` by hand, or drive it through the MCP tools — the
   window updates within ~250 ms (the host polls the file 4×/sec).

## MCP tools

| Tool | Purpose |
|------|---------|
| `get_spec` | Return the whole spec |
| `set_spec` | Replace the whole spec |
| `list_widgets` | Tree of all windows/widgets with ids |
| `widget_catalog` | Every supported widget type + its props |
| `add_window` | Add a top-level window |
| `add_widget` | Insert a widget into a container by `parent_id` |
| `update_widget` | Patch a widget's props (by id) |
| `remove_widget` | Delete a widget/window (by id) |
| `move_widget` | Reparent / reorder (by id) |
| `clear` | Blank canvas |
| `export_cpp` | Emit immediate-mode C++ for the current spec |
| `screenshot` | Capture the live preview and return the image |

## Using the UI in your own C++ project

- **Ship the interpreter** — copy `host/UiRenderer.{h,cpp}` + `vendor/json.hpp`,
  ship your `.json`, and inside your ImGui frame call:
  ```cpp
  static ui_render::UiState ui;
  ui_render::RenderSpec(mySpecJson, ui);
  ```
- **Ship plain code** — call `export_cpp` and paste the generated `RenderUI()`.

## Supported widgets

text / text_disabled / text_wrapped / bullet_text / label_text / separator_text /
bullet · button / small_button / arrow_button / checkbox / radio_button /
selectable / progress_bar · input_text / input_text_multiline / input_int /
input_float · slider_int / slider_float / slider_float2-4 / slider_angle /
vslider_float · drag_int / drag_float / drag_float2-4 · combo / listbox ·
color_edit3-4 / color_picker3-4 / color_button · plot_lines / plot_histogram ·
separator / same_line / new_line / spacing / dummy / indent / unindent · group /
child / collapsing_header / tree_node / tab_bar+tab_item / table / columns ·
menu_bar / menu / menu_item · popup / modal / tooltip / disabled / style_color /
style_var.

Call `widget_catalog` for the exact props of each.

## Figma ↔ ImGui

Design in Figma → see it live in ImGui and vice-versa. See **`FIGMA.md`**:
```
cd figma && npm install
node figma/bridge.mjs
# Figma → Plugins → Development → Import manifest → figma/plugin/manifest.json
```
