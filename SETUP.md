# Setup guide

Get the Dear ImGui menu designer running on a fresh machine.

```
   MCP client ──(MCP tools)──▶ spec/ui_spec.json ──(file watch)──▶ imgui_tool.exe
                                  the document                       live preview window
```

Two pieces talk only through `spec/ui_spec.json`: the **MCP server** (Node, edits the
JSON) and the **preview host** (C++, renders the JSON live). No sockets, no ports.

## 1. Prerequisites

| Need | Why | Check |
|------|-----|-------|
| **Windows 10/11** | host uses Win32 + DirectX 11 | — |
| **Visual Studio 2019/2022/2026** + "Desktop development with C++" | `cl.exe` + Windows SDK | `build.bat` auto-detects it |
| **Node.js 18+** | runs the MCP server / Figma bridge | `node --version` |
| **git** | clone the repo | `git --version` |

No CMake needed — `build.bat` finds Visual Studio via `vswhere`.

## 2. Clone & build

```bat
git clone <repo-url> imguitool
cd imguitool

REM build the preview host -> build\imgui_tool.exe
build.bat

REM install MCP server dependencies
cd mcp && npm install && cd ..

REM (optional) install the Figma bridge dependencies
cd figma && npm install && cd ..
```

`vendor/` contains Dear ImGui (docking branch), `nlohmann/json.hpp`, and the
stb_image_write header — no extra fetch step.

If `build.bat` can't find Visual Studio, point it at your `vcvars64.bat`:
```bat
set VS_VCVARS=C:\Path\To\VC\Auxiliary\Build\vcvars64.bat
build.bat
```

## 3. Register the MCP server

The repo ships a project-scoped `.mcp.json`; an MCP client opened in this folder picks
it up. To register manually, add a stdio server to your client's MCP config:
`command: node`, `args: ["C:/full/path/to/imguitool/mcp/server.mjs"]`, and set env
`IMGUI_SPEC_PATH` to the spec file's absolute path if the working directory isn't the
project root.

## 4. Run it

```bat
build\imgui_tool.exe spec\ui_spec.json
```
You'll see the sample **Demo** window plus a small status strip (load state + FPS).
Restart the MCP client so it loads the tools (MCP servers load at client startup).
Then edit the spec by hand or through the tools — the window updates within ~250 ms.

### Figma sync (optional)
```bat
node figma\bridge.mjs
```
Then in the Figma desktop app: **Plugins → Development → Import plugin from manifest…**
→ `figma\plugin\manifest.json`, and run it. See **`FIGMA.md`**.

## 5. Verify the pieces independently

**MCP server alone:**
```bat
cd mcp && node test_client.mjs
```
Expect a tool list, an added window, and a generated C++ snippet.

**Host alone:** edit `spec/ui_spec.json` and save — the window reflects it immediately.
Invalid JSON keeps the last good layout and shows a red error in the status strip.

## 6. Configuration reference

| Setting | Where | Default |
|---------|-------|---------|
| Spec path (server) | env `IMGUI_SPEC_PATH` | `<project>/spec/ui_spec.json` |
| Spec path (host) | first CLI arg to `imgui_tool.exe` | `spec/ui_spec.json` |
| VS toolchain | env `VS_VCVARS` (override) | auto-detected via `vswhere` |
| Hot-reload poll rate | `host/main.cpp` (`> 250` ms) | 250 ms |

## 7. Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `LNK1104: cannot open file ...imgui_tool.exe` | The preview is still running — close it (or `taskkill /IM imgui_tool.exe /F`) before rebuilding. |
| `vswhere.exe not found` | Install VS with the C++ workload, or set `VS_VCVARS`. |
| Client has no imgui-tool tools | Restart the MCP client after registering; confirm the server shows as connected. |
| Edits don't appear | Server and host using different spec files — align `IMGUI_SPEC_PATH` with the host's CLI arg. |
| Red "JSON parse error" | The last edit produced invalid JSON; the host keeps the previous layout until it's valid. |
| `node` not recognized | Node not installed / not on PATH. |

## 8. Reusing the UI in your own C++ project

- Copy `host/UiRenderer.{h,cpp}` + `vendor/json.hpp`, ship your `.json`, and call
  `ui_render::RenderSpec(spec, state)` in your ImGui frame.
- Or call the `export_cpp` MCP tool and paste the generated `RenderUI()`.
