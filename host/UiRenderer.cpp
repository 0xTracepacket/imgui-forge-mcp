// Interprets a JSON spec into Dear ImGui widget calls. See UiRenderer.h.
#include "UiRenderer.h"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/misc/cpp/imgui_stdlib.h"
#include <vector>
#include <cstring>

using nlohmann::json;

namespace ui_render {
namespace {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
const json& jget(const json& o, const char* k) {
    static const json null = json(nullptr);
    auto it = o.find(k);
    return it == o.end() ? null : *it;
}
std::string str(const json& o, const char* k, const char* def = "") {
    const json& v = jget(o, k);
    return v.is_string() ? v.get<std::string>() : std::string(def);
}
float fnum(const json& o, const char* k, float def = 0.f) {
    const json& v = jget(o, k); return v.is_number() ? v.get<float>() : def;
}
int inum(const json& o, const char* k, int def = 0) {
    const json& v = jget(o, k); return v.is_number() ? v.get<int>() : def;
}
bool bnum(const json& o, const char* k, bool def = false) {
    const json& v = jget(o, k); return v.is_boolean() ? v.get<bool>() : def;
}
ImVec2 vec2(const json& v, ImVec2 def = ImVec2(0, 0)) {
    if (v.is_array() && v.size() >= 2) return ImVec2(v[0].get<float>(), v[1].get<float>());
    return def;
}
ImVec4 vec4(const json& v, ImVec4 def = ImVec4(1, 1, 1, 1)) {
    if (v.is_array() && v.size() >= 4) return ImVec4(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>());
    if (v.is_array() && v.size() == 3) return ImVec4(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), 1.f);
    return def;
}

// Stable unique id per node. Uses node "id"; falls back to a path-derived one.
std::string nodeId(const json& n, const std::string& path) {
    const json& id = jget(n, "id");
    return id.is_string() ? id.get<std::string>() : path;
}

// Returns the live, mutable value for a widget. User edits persist across
// reloads; if the spec's value changes (an external edit) it overrides the runtime.
json& liveValue(UiState& s, const std::string& id, const json& specVal) {
    auto last = s.lastSpec.find(id);
    bool specChanged = (last == s.lastSpec.end()) || (last->second != specVal);
    auto cur = s.values.find(id);
    if (cur == s.values.end() || specChanged) s.values[id] = specVal;
    s.lastSpec[id] = specVal;
    return s.values[id];
}

// "##id" suffix so duplicate labels stay distinct in ImGui's id stack.
std::string lbl(const std::string& text, const std::string& id) {
    return text + "##" + id;
}

// ---------------------------------------------------------------------------
// name -> enum maps
// ---------------------------------------------------------------------------
ImGuiWindowFlags windowFlags(const json& arr) {
    ImGuiWindowFlags f = 0;
    if (!arr.is_array()) return f;
    for (auto& e : arr) {
        std::string s = e.get<std::string>();
        if (s == "NoTitleBar") f |= ImGuiWindowFlags_NoTitleBar;
        else if (s == "NoResize") f |= ImGuiWindowFlags_NoResize;
        else if (s == "NoMove") f |= ImGuiWindowFlags_NoMove;
        else if (s == "NoScrollbar") f |= ImGuiWindowFlags_NoScrollbar;
        else if (s == "NoCollapse") f |= ImGuiWindowFlags_NoCollapse;
        else if (s == "AlwaysAutoResize") f |= ImGuiWindowFlags_AlwaysAutoResize;
        else if (s == "NoBackground") f |= ImGuiWindowFlags_NoBackground;
        else if (s == "NoSavedSettings") f |= ImGuiWindowFlags_NoSavedSettings;
        else if (s == "NoBringToFrontOnFocus") f |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        else if (s == "NoDecoration") f |= ImGuiWindowFlags_NoDecoration;
        else if (s == "MenuBar") f |= ImGuiWindowFlags_MenuBar;
        else if (s == "HorizontalScrollbar") f |= ImGuiWindowFlags_HorizontalScrollbar;
        else if (s == "NoFocusOnAppearing") f |= ImGuiWindowFlags_NoFocusOnAppearing;
    }
    return f;
}

int colEnum(const std::string& s) {
    // common subset; unknown -> -1
    if (s == "Text") return ImGuiCol_Text;
    if (s == "TextDisabled") return ImGuiCol_TextDisabled;
    if (s == "WindowBg") return ImGuiCol_WindowBg;
    if (s == "ChildBg") return ImGuiCol_ChildBg;
    if (s == "PopupBg") return ImGuiCol_PopupBg;
    if (s == "Border") return ImGuiCol_Border;
    if (s == "FrameBg") return ImGuiCol_FrameBg;
    if (s == "FrameBgHovered") return ImGuiCol_FrameBgHovered;
    if (s == "FrameBgActive") return ImGuiCol_FrameBgActive;
    if (s == "TitleBg") return ImGuiCol_TitleBg;
    if (s == "TitleBgActive") return ImGuiCol_TitleBgActive;
    if (s == "Button") return ImGuiCol_Button;
    if (s == "ButtonHovered") return ImGuiCol_ButtonHovered;
    if (s == "ButtonActive") return ImGuiCol_ButtonActive;
    if (s == "Header") return ImGuiCol_Header;
    if (s == "HeaderHovered") return ImGuiCol_HeaderHovered;
    if (s == "HeaderActive") return ImGuiCol_HeaderActive;
    if (s == "CheckMark") return ImGuiCol_CheckMark;
    if (s == "SliderGrab") return ImGuiCol_SliderGrab;
    if (s == "SliderGrabActive") return ImGuiCol_SliderGrabActive;
    if (s == "Tab") return ImGuiCol_Tab;
    if (s == "Separator") return ImGuiCol_Separator;
    if (s == "PlotLines") return ImGuiCol_PlotLines;
    if (s == "PlotHistogram") return ImGuiCol_PlotHistogram;
    return -1;
}

// style var: returns index, and whether it takes a vec2 (else float)
int varEnum(const std::string& s, bool& isVec2) {
    isVec2 = false;
    if (s == "Alpha") return ImGuiStyleVar_Alpha;
    if (s == "DisabledAlpha") return ImGuiStyleVar_DisabledAlpha;
    if (s == "WindowRounding") return ImGuiStyleVar_WindowRounding;
    if (s == "WindowBorderSize") return ImGuiStyleVar_WindowBorderSize;
    if (s == "FrameRounding") return ImGuiStyleVar_FrameRounding;
    if (s == "FrameBorderSize") return ImGuiStyleVar_FrameBorderSize;
    if (s == "IndentSpacing") return ImGuiStyleVar_IndentSpacing;
    if (s == "GrabRounding") return ImGuiStyleVar_GrabRounding;
    if (s == "GrabMinSize") return ImGuiStyleVar_GrabMinSize;
    if (s == "ChildRounding") return ImGuiStyleVar_ChildRounding;
    if (s == "PopupRounding") return ImGuiStyleVar_PopupRounding;
    if (s == "TabRounding") return ImGuiStyleVar_TabRounding;
    if (s == "WindowPadding") { isVec2 = true; return ImGuiStyleVar_WindowPadding; }
    if (s == "FramePadding") { isVec2 = true; return ImGuiStyleVar_FramePadding; }
    if (s == "ItemSpacing") { isVec2 = true; return ImGuiStyleVar_ItemSpacing; }
    if (s == "ItemInnerSpacing") { isVec2 = true; return ImGuiStyleVar_ItemInnerSpacing; }
    if (s == "CellPadding") { isVec2 = true; return ImGuiStyleVar_CellPadding; }
    return -1;
}

// forward decl
void renderNode(const json& n, UiState& st, const std::string& path);

void renderChildren(const json& n, UiState& st, const std::string& path) {
    const json& kids = jget(n, "children");
    if (!kids.is_array()) return;
    for (size_t i = 0; i < kids.size(); ++i)
        renderNode(kids[i], st, path + "/" + std::to_string(i));
}

// build a const char* vector from a json string array (kept alive by caller)
std::vector<std::string> strList(const json& arr) {
    std::vector<std::string> out;
    if (arr.is_array()) for (auto& e : arr) out.push_back(e.is_string() ? e.get<std::string>() : "");
    return out;
}

// ---------------------------------------------------------------------------
// the big dispatch: one node -> ImGui calls
// ---------------------------------------------------------------------------
void renderNode(const json& n, UiState& st, const std::string& path) {
    if (!n.is_object()) return;
    std::string type = str(n, "type");
    const json& p = jget(n, "props").is_object() ? jget(n, "props") : n; // props or inline
    std::string id = nodeId(n, path);

    // ---- text family ----
    if (type == "text") {
        const json& c = jget(p, "color");
        if (c.is_array()) ImGui::TextColored(vec4(c), "%s", str(p, "text").c_str());
        else ImGui::TextUnformatted(str(p, "text").c_str());
    }
    else if (type == "text_disabled") ImGui::TextDisabled("%s", str(p, "text").c_str());
    else if (type == "text_wrapped")  ImGui::TextWrapped("%s", str(p, "text").c_str());
    else if (type == "bullet_text")   ImGui::BulletText("%s", str(p, "text").c_str());
    else if (type == "label_text")    ImGui::LabelText(lbl(str(p,"label"),id).c_str(), "%s", str(p, "text").c_str());
    else if (type == "bullet")        ImGui::Bullet();
    else if (type == "separator_text") ImGui::SeparatorText(str(p, "text").c_str());

    // ---- layout / spacing ----
    else if (type == "separator") ImGui::Separator();
    else if (type == "same_line") ImGui::SameLine(fnum(p, "offset", 0.f), fnum(p, "spacing", -1.f));
    else if (type == "new_line")  ImGui::NewLine();
    else if (type == "spacing")   ImGui::Spacing();
    else if (type == "dummy")     ImGui::Dummy(vec2(jget(p, "size"), ImVec2(0, 0)));
    else if (type == "indent")    ImGui::Indent(fnum(p, "width", 0.f));
    else if (type == "unindent")  ImGui::Unindent(fnum(p, "width", 0.f));

    // ---- buttons & basic ----
    else if (type == "button") {
        if (ImGui::Button(lbl(str(p, "label", "Button"), id).c_str(), vec2(jget(p, "size"), ImVec2(0, 0))))
            st.lastClicked = id;
    }
    else if (type == "small_button") {
        if (ImGui::SmallButton(lbl(str(p, "label", "Button"), id).c_str())) st.lastClicked = id;
    }
    else if (type == "arrow_button") {
        std::string d = str(p, "dir", "Right");
        ImGuiDir dir = d == "Left" ? ImGuiDir_Left : d == "Up" ? ImGuiDir_Up : d == "Down" ? ImGuiDir_Down : ImGuiDir_Right;
        if (ImGui::ArrowButton(("##ab" + id).c_str(), dir)) st.lastClicked = id;
    }
    else if (type == "checkbox") {
        json& v = liveValue(st, id, jget(p, "value").is_boolean() ? jget(p, "value") : json(false));
        bool b = v.get<bool>();
        if (ImGui::Checkbox(lbl(str(p, "label", "Checkbox"), id).c_str(), &b)) v = b;
    }
    else if (type == "radio_button") {
        // group: props.value (int) selected index, props.options [labels]
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int sel = v.get<int>();
        auto opts = strList(jget(p, "options"));
        for (size_t i = 0; i < opts.size(); ++i) {
            if (ImGui::RadioButton(lbl(opts[i], id + std::to_string(i)).c_str(), &sel, (int)i)) {}
            if (i + 1 < opts.size() && bnum(p, "horizontal", true)) ImGui::SameLine();
        }
        v = sel;
    }
    else if (type == "progress_bar") {
        ImGui::ProgressBar(fnum(p, "fraction", 0.f), vec2(jget(p, "size"), ImVec2(-FLT_MIN, 0)),
                           jget(p, "overlay").is_string() ? str(p, "overlay").c_str() : nullptr);
    }

    // ---- text input ----
    else if (type == "input_text") {
        json& v = liveValue(st, id, jget(p, "value").is_string() ? jget(p, "value") : json(""));
        std::string s = v.get<std::string>();
        bool changed = jget(p, "hint").is_string()
            ? ImGui::InputTextWithHint(lbl(str(p, "label"), id).c_str(), str(p, "hint").c_str(), &s)
            : ImGui::InputText(lbl(str(p, "label"), id).c_str(), &s);
        if (changed) v = s;
    }
    else if (type == "input_text_multiline") {
        json& v = liveValue(st, id, jget(p, "value").is_string() ? jget(p, "value") : json(""));
        std::string s = v.get<std::string>();
        if (ImGui::InputTextMultiline(lbl(str(p, "label"), id).c_str(), &s, vec2(jget(p, "size"), ImVec2(0, 0)))) v = s;
    }
    else if (type == "input_int") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int x = v.get<int>();
        if (ImGui::InputInt(lbl(str(p, "label"), id).c_str(), &x, inum(p, "step", 1), inum(p, "step_fast", 100))) v = x;
    }
    else if (type == "input_float") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0.f));
        float x = v.get<float>();
        if (ImGui::InputFloat(lbl(str(p, "label"), id).c_str(), &x, fnum(p, "step", 0.f), fnum(p, "step_fast", 0.f),
                              str(p, "format", "%.3f").c_str())) v = x;
    }

    // ---- sliders (scalar + vec) ----
    else if (type == "slider_int") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int x = v.get<int>();
        if (ImGui::SliderInt(lbl(str(p, "label"), id).c_str(), &x, inum(p, "min", 0), inum(p, "max", 100))) v = x;
    }
    else if (type == "slider_float") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0.f));
        float x = v.get<float>();
        if (ImGui::SliderFloat(lbl(str(p, "label"), id).c_str(), &x, fnum(p, "min", 0.f), fnum(p, "max", 1.f),
                               str(p, "format", "%.3f").c_str())) v = x;
    }
    else if (type == "slider_float2" || type == "slider_float3" || type == "slider_float4") {
        int n2 = type.back() - '0';
        json def = json::array(); for (int i = 0; i < n2; i++) def.push_back(0.f);
        json& v = liveValue(st, id, jget(p, "value").is_array() ? jget(p, "value") : def);
        float a[4] = {0,0,0,0}; for (int i = 0; i < n2 && i < (int)v.size(); i++) a[i] = v[i].get<float>();
        bool ch = false;
        if (n2 == 2) ch = ImGui::SliderFloat2(lbl(str(p,"label"),id).c_str(), a, fnum(p,"min",0.f), fnum(p,"max",1.f));
        else if (n2 == 3) ch = ImGui::SliderFloat3(lbl(str(p,"label"),id).c_str(), a, fnum(p,"min",0.f), fnum(p,"max",1.f));
        else ch = ImGui::SliderFloat4(lbl(str(p,"label"),id).c_str(), a, fnum(p,"min",0.f), fnum(p,"max",1.f));
        if (ch) { for (int i = 0; i < n2; i++) v[i] = a[i]; }
    }
    else if (type == "slider_angle") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0.f));
        float x = v.get<float>();
        if (ImGui::SliderAngle(lbl(str(p, "label"), id).c_str(), &x, fnum(p, "min", -360.f), fnum(p, "max", 360.f))) v = x;
    }
    else if (type == "vslider_float") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0.f));
        float x = v.get<float>();
        if (ImGui::VSliderFloat(lbl(str(p,"label"),id).c_str(), vec2(jget(p,"size"),ImVec2(20,160)), &x,
                                fnum(p,"min",0.f), fnum(p,"max",1.f))) v = x;
    }

    // ---- drags ----
    else if (type == "drag_int") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int x = v.get<int>();
        if (ImGui::DragInt(lbl(str(p,"label"),id).c_str(), &x, fnum(p,"speed",1.f), inum(p,"min",0), inum(p,"max",0))) v = x;
    }
    else if (type == "drag_float") {
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0.f));
        float x = v.get<float>();
        if (ImGui::DragFloat(lbl(str(p,"label"),id).c_str(), &x, fnum(p,"speed",1.f), fnum(p,"min",0.f), fnum(p,"max",0.f),
                             str(p,"format","%.3f").c_str())) v = x;
    }
    else if (type == "drag_float2" || type == "drag_float3" || type == "drag_float4") {
        int n2 = type.back() - '0';
        json def = json::array(); for (int i = 0; i < n2; i++) def.push_back(0.f);
        json& v = liveValue(st, id, jget(p, "value").is_array() ? jget(p, "value") : def);
        float a[4] = {0,0,0,0}; for (int i = 0; i < n2 && i < (int)v.size(); i++) a[i] = v[i].get<float>();
        float sp = fnum(p,"speed",1.f), mn = fnum(p,"min",0.f), mx = fnum(p,"max",0.f);
        bool ch = false;
        if (n2 == 2) ch = ImGui::DragFloat2(lbl(str(p,"label"),id).c_str(), a, sp, mn, mx);
        else if (n2 == 3) ch = ImGui::DragFloat3(lbl(str(p,"label"),id).c_str(), a, sp, mn, mx);
        else ch = ImGui::DragFloat4(lbl(str(p,"label"),id).c_str(), a, sp, mn, mx);
        if (ch) { for (int i = 0; i < n2; i++) v[i] = a[i]; }
    }

    // ---- combo / list / selectable ----
    else if (type == "combo") {
        auto items = strList(jget(p, "items"));
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int sel = v.get<int>();
        const char* preview = (sel >= 0 && sel < (int)items.size()) ? items[sel].c_str() : "";
        if (ImGui::BeginCombo(lbl(str(p, "label"), id).c_str(), preview)) {
            for (int i = 0; i < (int)items.size(); ++i) {
                bool selected = (sel == i);
                if (ImGui::Selectable(items[i].c_str(), selected)) { sel = i; v = sel; }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    else if (type == "listbox") {
        auto items = strList(jget(p, "items"));
        json& v = liveValue(st, id, jget(p, "value").is_number() ? jget(p, "value") : json(0));
        int sel = v.get<int>();
        if (ImGui::BeginListBox(lbl(str(p, "label"), id).c_str(), vec2(jget(p, "size"), ImVec2(0, 0)))) {
            for (int i = 0; i < (int)items.size(); ++i) {
                bool selected = (sel == i);
                if (ImGui::Selectable(lbl(items[i], id + std::to_string(i)).c_str(), selected)) { sel = i; v = sel; }
            }
            ImGui::EndListBox();
        }
    }
    else if (type == "selectable") {
        json& v = liveValue(st, id, jget(p, "value").is_boolean() ? jget(p, "value") : json(false));
        bool b = v.get<bool>();
        if (ImGui::Selectable(lbl(str(p, "label"), id).c_str(), &b)) v = b;
    }

    // ---- color ----
    else if (type == "color_edit3" || type == "color_edit4" ||
             type == "color_picker3" || type == "color_picker4" || type == "color_button") {
        bool hasA = (type == "color_edit4" || type == "color_picker4");
        int comps = hasA ? 4 : 3;
        json def = json::array(); for (int i = 0; i < comps; i++) def.push_back(i < 3 ? 1.f : 1.f);
        json& v = liveValue(st, id, jget(p, "value").is_array() ? jget(p, "value") : def);
        float c[4] = {1,1,1,1}; for (int i = 0; i < comps && i < (int)v.size(); i++) c[i] = v[i].get<float>();
        bool ch = false;
        std::string L = lbl(str(p, "label"), id);
        if (type == "color_edit3") ch = ImGui::ColorEdit3(L.c_str(), c);
        else if (type == "color_edit4") ch = ImGui::ColorEdit4(L.c_str(), c);
        else if (type == "color_picker3") ch = ImGui::ColorPicker3(L.c_str(), c);
        else if (type == "color_picker4") ch = ImGui::ColorPicker4(L.c_str(), c);
        else { if (ImGui::ColorButton(L.c_str(), ImVec4(c[0],c[1],c[2], hasA?c[3]:1.f))) st.lastClicked = id; }
        if (ch) { for (int i = 0; i < comps; i++) v[i] = c[i]; }
    }

    // ---- plots ----
    else if (type == "plot_lines" || type == "plot_histogram") {
        const json& d = jget(p, "values");
        std::vector<float> vals; if (d.is_array()) for (auto& e : d) vals.push_back(e.get<float>());
        if (!vals.empty()) {
            ImVec2 sz = vec2(jget(p, "size"), ImVec2(0, 80));
            const char* ov = jget(p, "overlay").is_string() ? str(p, "overlay").c_str() : nullptr;
            if (type == "plot_lines")
                ImGui::PlotLines(lbl(str(p,"label"),id).c_str(), vals.data(), (int)vals.size(), 0, ov, FLT_MAX, FLT_MAX, sz);
            else
                ImGui::PlotHistogram(lbl(str(p,"label"),id).c_str(), vals.data(), (int)vals.size(), 0, ov, FLT_MAX, FLT_MAX, sz);
        }
    }

    // ---- containers ----
    else if (type == "group") {
        ImGui::BeginGroup();
        renderChildren(n, st, path);
        ImGui::EndGroup();
    }
    else if (type == "child") {
        if (ImGui::BeginChild(("##child" + id).c_str(), vec2(jget(p, "size"), ImVec2(0, 0)),
                              bnum(p, "border", false) ? ImGuiChildFlags_Borders : 0)) {
            renderChildren(n, st, path);
        }
        ImGui::EndChild();
    }
    else if (type == "collapsing_header") {
        ImGuiTreeNodeFlags f = bnum(p, "default_open", false) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        if (ImGui::CollapsingHeader(lbl(str(p, "label", "Header"), id).c_str(), f))
            renderChildren(n, st, path);
    }
    else if (type == "tree_node") {
        if (ImGui::TreeNode(lbl(str(p, "label", "Node"), id).c_str())) {
            renderChildren(n, st, path);
            ImGui::TreePop();
        }
    }
    else if (type == "tab_bar") {
        if (ImGui::BeginTabBar(("##tabs" + id).c_str())) {
            const json& kids = jget(n, "children");
            if (kids.is_array()) for (size_t i = 0; i < kids.size(); ++i) {
                const json& tab = kids[i];
                if (str(tab, "type") != "tab_item") continue;
                const json& tp = jget(tab, "props").is_object() ? jget(tab, "props") : tab;
                if (ImGui::BeginTabItem(lbl(str(tp, "label", "Tab"), nodeId(tab, path + "/" + std::to_string(i))).c_str())) {
                    renderChildren(tab, st, path + "/" + std::to_string(i));
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    else if (type == "table") {
        int cols = inum(p, "columns", 1);
        auto headers = strList(jget(p, "headers"));
        if (cols > 0 && ImGui::BeginTable(("##tbl" + id).c_str(), cols,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            if (!headers.empty()) {
                for (int c = 0; c < cols; ++c) ImGui::TableSetupColumn(c < (int)headers.size() ? headers[c].c_str() : "");
                ImGui::TableHeadersRow();
            }
            // children are cells, laid out left-to-right/top-to-bottom
            const json& kids = jget(n, "children");
            if (kids.is_array()) for (size_t i = 0; i < kids.size(); ++i) {
                if ((int)(i % cols) == 0) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex((int)(i % cols));
                renderNode(kids[i], st, path + "/" + std::to_string(i));
            }
            ImGui::EndTable();
        }
    }
    else if (type == "columns") {
        int cols = inum(p, "count", 2);
        ImGui::Columns(cols, ("##cols" + id).c_str(), bnum(p, "border", true));
        const json& kids = jget(n, "children");
        if (kids.is_array()) for (size_t i = 0; i < kids.size(); ++i) {
            renderNode(kids[i], st, path + "/" + std::to_string(i));
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
    else if (type == "menu_bar") {
        if (ImGui::BeginMenuBar()) { renderChildren(n, st, path); ImGui::EndMenuBar(); }
    }
    else if (type == "menu") {
        if (ImGui::BeginMenu(str(p, "label", "Menu").c_str())) { renderChildren(n, st, path); ImGui::EndMenu(); }
    }
    else if (type == "menu_item") {
        const json& shortcut = jget(p, "shortcut");
        if (ImGui::MenuItem(str(p, "label", "Item").c_str(),
                            shortcut.is_string() ? shortcut.get<std::string>().c_str() : nullptr))
            st.lastClicked = id;
    }
    else if (type == "popup") {
        // button that opens an inline popup containing children
        std::string pid = "##popup" + id;
        if (ImGui::Button(lbl(str(p, "label", "Open"), id).c_str())) ImGui::OpenPopup(pid.c_str());
        if (ImGui::BeginPopup(pid.c_str())) { renderChildren(n, st, path); ImGui::EndPopup(); }
    }
    else if (type == "modal") {
        std::string pid = "##modal" + id;
        if (ImGui::Button(lbl(str(p, "label", "Open Modal"), id).c_str())) ImGui::OpenPopup(pid.c_str());
        if (ImGui::BeginPopupModal(pid.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            renderChildren(n, st, path);
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    else if (type == "tooltip") {
        // attaches to the previous sibling item
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            renderChildren(n, st, path);
            ImGui::EndTooltip();
        }
    }
    else if (type == "disabled") {
        ImGui::BeginDisabled(bnum(p, "disabled", true));
        renderChildren(n, st, path);
        ImGui::EndDisabled();
    }
    else if (type == "style_color") {
        int e = colEnum(str(p, "target", "Text"));
        bool pushed = false;
        if (e >= 0) { ImGui::PushStyleColor(e, vec4(jget(p, "color"))); pushed = true; }
        renderChildren(n, st, path);
        if (pushed) ImGui::PopStyleColor();
    }
    else if (type == "style_var") {
        bool isVec2 = false; int e = varEnum(str(p, "target", "Alpha"), isVec2);
        bool pushed = false;
        if (e >= 0) {
            if (isVec2) ImGui::PushStyleVar(e, vec2(jget(p, "value")));
            else ImGui::PushStyleVar(e, fnum(p, "value", 1.f));
            pushed = true;
        }
        renderChildren(n, st, path);
        if (pushed) ImGui::PopStyleVar();
    }
    else {
        // unknown type: render a visible marker
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "[unknown widget type: %s]", type.c_str());
    }

    // optional per-node tooltip shortcut: props.tooltip = "text"
    if (jget(p, "tooltip").is_string() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", str(p, "tooltip").c_str());
}


} // anonymous namespace

// ---------------------------------------------------------------------------
void RenderSpec(const json& spec, UiState& state) {
    const json& windows = jget(spec, "windows");
    if (!windows.is_array()) return;

    for (size_t w = 0; w < windows.size(); ++w) {
        const json& win = windows[w];
        if (!win.is_object()) continue;

        const json& p = jget(win, "props").is_object() ? jget(win, "props") : win;
        std::string wid = nodeId(win, "win" + std::to_string(w));
        std::string title = str(p, "title", "Window");

        // does the window contain a menu_bar child? then add the flag.
        ImGuiWindowFlags flags = windowFlags(jget(p, "flags"));
        const json& kids = jget(win, "children");
        if (kids.is_array())
            for (auto& k : kids) if (str(k, "type") == "menu_bar") { flags |= ImGuiWindowFlags_MenuBar; break; }

        if (jget(p, "pos").is_array())  ImGui::SetNextWindowPos(vec2(jget(p, "pos")), ImGuiCond_FirstUseEver);
        if (jget(p, "size").is_array()) ImGui::SetNextWindowSize(vec2(jget(p, "size")), ImGuiCond_FirstUseEver);

        // "open" state lives in UiState so the X button works and persists
        json& openV = liveValue(state, wid + ".open", jget(p, "open").is_boolean() ? jget(p, "open") : json(true));
        bool open = openV.get<bool>();
        bool closable = bnum(p, "closable", false);

        if (ImGui::Begin(lbl(title, wid).c_str(), closable ? &open : nullptr, flags)) {
            renderChildren(win, state, "win" + std::to_string(w));
        }
        ImGui::End();
        if (closable) openV = open;
    }
}

} // namespace ui_render
