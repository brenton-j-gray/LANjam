#include "ServerGuiApp.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <cinttypes>

#define GLFW_INCLUDE_NONE
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "gui/GuiStyle.h"

namespace {

void init_log(ServerState& state, const char* msg) {
  std::lock_guard<std::mutex> lock(state.logMutex);
  state.log.emplace_back(msg);
  while (state.log.size() > 200) state.log.pop_front();
}

} // namespace

int run_server_gui(ServerState& shared) {
  if (!glfwInit()) {
    std::printf("[ServerGUI] glfwInit failed\n");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(960, 600, "LAN Jam Server", nullptr, nullptr);
  if (!window) {
    std::printf("[ServerGUI] window creation failed\n");
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::printf("[ServerGUI] gladLoadGLLoader failed\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ApplyLanJamStyle(1.0f);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  init_log(shared, "Server GUI ready.");

  while (!glfwWindowShouldClose(window) && !shared.quitRequested.load()) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(800.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(800.0f, 420.0f), ImVec2(800.0f, 700.0f));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("LAN Jam Server", nullptr, windowFlags);

    ImGui::BeginChild("ControlStrip", ImVec2(0.0f, 130.0f), true);
    int portInt = static_cast<int>(shared.port.load());
    ImGui::Text("Listen Port");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("##ListenPort", &portInt)) {
      portInt = std::clamp(portInt, 1, 65535);
      shared.port.store(static_cast<uint16_t>(portInt));
    }

    bool running = shared.running.load();
    ImGui::Text("Status: %s", running ? "Running" : "Stopped");
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Start Server", ImVec2(140.0f, 0.0f))) shared.startRequested.store(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop Server", ImVec2(140.0f, 0.0f))) shared.stopRequested.store(true);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Discoveries: %" PRIu64 "   Handshakes: %" PRIu64 "   Packets: %" PRIu64,
                shared.discoveryCount.load(),
                shared.handshakeCount.load(),
                shared.packetsForwarded.load());
    ImGui::EndChild();

    ImGui::Spacing();

    std::vector<ServerPeerInfo> peersSnapshot;
    {
      std::lock_guard<std::mutex> lock(shared.peersMutex);
      peersSnapshot = shared.peers;
    }
    std::vector<std::string> logSnapshot;
    {
      std::lock_guard<std::mutex> lock(shared.logMutex);
      logSnapshot.assign(shared.log.begin(), shared.log.end());
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Columns(2, "ServerColumns");
    ImGui::SetColumnWidth(0, avail.x * 0.55f);

    ImGui::Text("Peers (%zu)", peersSnapshot.size());
    if (ImGui::BeginTable("PeersTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
      ImGui::TableSetupColumn("Endpoint");
      ImGui::TableSetupColumn("Packets");
      ImGui::TableSetupColumn("Last seen (ms)");
      ImGui::TableHeadersRow();
      auto now = std::chrono::steady_clock::now();
      for (const auto& peer : peersSnapshot) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(peer.endpoint.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%" PRIu64, peer.packetsForwarded);
        ImGui::TableSetColumnIndex(2);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - peer.lastSeen).count();
        ImGui::Text("%lld", static_cast<long long>(ms));
      }
      ImGui::EndTable();
    }

    ImGui::NextColumn();
    ImGui::Text("Event Log");
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& line : logSnapshot) {
      ImGui::TextUnformatted(line.c_str());
    }
    if (!logSnapshot.empty()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  shared.quitRequested.store(true);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
