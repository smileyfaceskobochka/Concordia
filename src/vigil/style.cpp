#include "style.h"
#include <imgui.h>

namespace Vigil {

// Catppuccin Mocha palette
namespace {
constexpr auto c = [](unsigned int hex, float a = 1.0f) {
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                ((hex)&0xFF) / 255.0f, a);
};
}

void applyCustomStyle() {
  ImGuiStyle &style = ImGui::GetStyle();

  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 4);
  style.ItemInnerSpacing = ImVec2(4, 4);
  style.IndentSpacing = 18.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 8.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 4.0f;

  auto &col = style.Colors;

  // ── Text ────────────────────────────────────────────────────────
  col[ImGuiCol_Text] = c(0xcdd6f4);
  col[ImGuiCol_TextDisabled] = c(0x6c7086);
  col[ImGuiCol_TextSelectedBg] = c(0xeba0ac, 0.35f);

  // ── Backgrounds ─────────────────────────────────────────────────
  col[ImGuiCol_WindowBg] = c(0x1e1e2e, 0.94f);
  col[ImGuiCol_ChildBg] = c(0x181825);
  col[ImGuiCol_PopupBg] = c(0x1e1e2e, 0.94f);
  col[ImGuiCol_MenuBarBg] = c(0x181825);
  col[ImGuiCol_ModalWindowDimBg] = c(0x11111b, 0.60f);

  // ── Borders ─────────────────────────────────────────────────────
  col[ImGuiCol_Border] = c(0x313244);
  col[ImGuiCol_BorderShadow] = c(0x000000, 0.00f);

  // ── Frame (widget body) ─────────────────────────────────────────
  col[ImGuiCol_FrameBg] = c(0x313244);
  col[ImGuiCol_FrameBgHovered] = c(0x45475a);
  col[ImGuiCol_FrameBgActive] = c(0x585b70);

  // ── Title ───────────────────────────────────────────────────────
  col[ImGuiCol_TitleBg] = c(0x11111b);
  col[ImGuiCol_TitleBgActive] = c(0x181825);
  col[ImGuiCol_TitleBgCollapsed] = c(0x11111b, 0.70f);

  // ── Scrollbar ───────────────────────────────────────────────────
  col[ImGuiCol_ScrollbarBg] = c(0x1e1e2e);
  col[ImGuiCol_ScrollbarGrab] = c(0x45475a);
  col[ImGuiCol_ScrollbarGrabHovered] = c(0x585b70);
  col[ImGuiCol_ScrollbarGrabActive] = c(0x6c7086);

  // ── Widgets ─────────────────────────────────────────────────────
  col[ImGuiCol_CheckMark] = c(0xeba0ac);
  col[ImGuiCol_SliderGrab] = c(0xeba0ac, 0.78f);
  col[ImGuiCol_SliderGrabActive] = c(0xeba0ac);
  col[ImGuiCol_Button] = c(0x313244);
  col[ImGuiCol_ButtonHovered] = c(0xeba0ac, 0.50f);
  col[ImGuiCol_ButtonActive] = c(0xeba0ac, 0.80f);

  // ── Header / tree ───────────────────────────────────────────────
  col[ImGuiCol_Header] = c(0x313244);
  col[ImGuiCol_HeaderHovered] = c(0xeba0ac, 0.35f);
  col[ImGuiCol_HeaderActive] = c(0xeba0ac, 0.55f);

  // ── Separator ───────────────────────────────────────────────────
  col[ImGuiCol_Separator] = c(0x313244);
  col[ImGuiCol_SeparatorHovered] = c(0x45475a);
  col[ImGuiCol_SeparatorActive] = c(0x585b70);

  // ── Resize / grab ───────────────────────────────────────────────
  col[ImGuiCol_ResizeGrip] = c(0xeba0ac, 0.25f);
  col[ImGuiCol_ResizeGripHovered] = c(0xeba0ac, 0.50f);
  col[ImGuiCol_ResizeGripActive] = c(0xeba0ac, 0.80f);

  // ── Tabs ────────────────────────────────────────────────────────
  col[ImGuiCol_Tab] = c(0x181825);
  col[ImGuiCol_TabHovered] = c(0xeba0ac, 0.50f);
  col[ImGuiCol_TabActive] = c(0x313244);
  col[ImGuiCol_TabUnfocused] = c(0x11111b);
  col[ImGuiCol_TabUnfocusedActive] = c(0x1e1e2e);

  // ── Plot ────────────────────────────────────────────────────────
  col[ImGuiCol_PlotLines] = c(0x585b70);
  col[ImGuiCol_PlotLinesHovered] = c(0xeba0ac);
  col[ImGuiCol_PlotHistogram] = c(0x89b4fa);
  col[ImGuiCol_PlotHistogramHovered] = c(0x74c7ec);

  // ── Table ───────────────────────────────────────────────────────
  col[ImGuiCol_TableHeaderBg] = c(0x181825);
  col[ImGuiCol_TableBorderStrong] = c(0x313244);
  col[ImGuiCol_TableBorderLight] = c(0x45475a);
  col[ImGuiCol_TableRowBg] = c(0x000000, 0.00f);
  col[ImGuiCol_TableRowBgAlt] = c(0xffffff, 0.04f);

  // ── Misc ────────────────────────────────────────────────────────
  col[ImGuiCol_DragDropTarget] = c(0xfab387, 0.90f);
  col[ImGuiCol_NavHighlight] = c(0xeba0ac);
  col[ImGuiCol_NavWindowingHighlight] = c(0xffffff, 0.70f);
  col[ImGuiCol_NavWindowingDimBg] = c(0x11111b, 0.50f);
}

} // namespace Vigil
