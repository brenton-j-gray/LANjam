#include "GuiStyle.h"

#include <imgui.h>

void ApplyLanJamStyle(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.ScrollbarSize = 14.0f;
  style.WindowPadding = ImVec2(14.0f, 12.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
  style.TabRounding = 4.0f;

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_WindowBg]        = ImVec4(0.08f, 0.09f, 0.12f, 1.0f);
  colors[ImGuiCol_Header]          = ImVec4(0.18f, 0.32f, 0.52f, 0.76f);
  colors[ImGuiCol_HeaderHovered]   = ImVec4(0.23f, 0.40f, 0.62f, 0.86f);
  colors[ImGuiCol_HeaderActive]    = ImVec4(0.28f, 0.45f, 0.70f, 0.90f);
  colors[ImGuiCol_Button]          = ImVec4(0.20f, 0.35f, 0.60f, 0.80f);
  colors[ImGuiCol_ButtonHovered]   = ImVec4(0.24f, 0.42f, 0.70f, 0.90f);
  colors[ImGuiCol_ButtonActive]    = ImVec4(0.18f, 0.32f, 0.54f, 0.86f);
  colors[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.18f, 0.28f, 0.74f);
  colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.26f, 0.38f, 0.88f);
  colors[ImGuiCol_FrameBgActive]   = ImVec4(0.22f, 0.32f, 0.46f, 0.98f);
  colors[ImGuiCol_Tab]             = ImVec4(0.16f, 0.28f, 0.48f, 0.86f);
  colors[ImGuiCol_TabHovered]      = ImVec4(0.24f, 0.40f, 0.66f, 0.90f);
  colors[ImGuiCol_TabActive]       = ImVec4(0.20f, 0.34f, 0.58f, 0.92f);
  colors[ImGuiCol_Separator]       = ImVec4(0.24f, 0.34f, 0.46f, 0.80f);

  if (scale != 1.0f) {
    ImGui::GetStyle().ScaleAllSizes(scale);
  }
}
