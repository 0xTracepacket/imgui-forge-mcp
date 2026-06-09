// UiRenderer.h - Generic Dear ImGui spec interpreter.
//
// Drop this (and UiRenderer.cpp) into ANY Dear ImGui project. Inside your
// frame (between ImGui::NewFrame and ImGui::Render), call:
//
//     static UiState ui;                 // persists widget values across frames
//     ui_render::RenderSpec(specJson, ui);
//
// where specJson is an nlohmann::json object shaped like spec/ui_spec.json.
//
// The spec is the single source of truth ("the document"). Widget values the
// user edits live in UiState and survive spec hot-reloads; when the spec's own
// value for a widget changes (e.g. the spec is edited), that wins instead.
#pragma once
#include <string>
#include <unordered_map>
#include "../vendor/json.hpp"

namespace ui_render {

// Runtime value + interaction state, keyed by widget "id".
// Layout can be hot-reloaded freely without losing what the user typed/dragged.
struct UiState {
    // live values the widgets read/write (slider floats, checkbox bools, text...)
    std::unordered_map<std::string, nlohmann::json> values;
    // last spec-provided value we saw, to detect spec-driven changes
    std::unordered_map<std::string, nlohmann::json> lastSpec;
    // id of most recently clicked button (handy for debugging actions)
    std::string lastClicked;
};

// Render every window in the spec. spec = { "windows": [ ...nodes... ] }.
void RenderSpec(const nlohmann::json& spec, UiState& state);

} // namespace ui_render
