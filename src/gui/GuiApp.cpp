#include "GuiApp.h"

#include <atomic>
#include <cstdio>
#include <algorithm>
#include <string>

#define GLFW_INCLUDE_NONE
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

static void glfw_error_callback(int error, const char* description) {
  std::printf("[GUI] GLFW Error %d: %s\n", error, description);
}

int run_gui(GuiState& shared) {
  std::printf("[GUI] run_gui starting (GLFW + OpenGL3)\n");

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    std::printf("[GUI] glfwInit failed\n");
    return 1;
  }
  std::printf("[GUI] glfwInit ok\n");

  // Request GL 3.3 core (works well with ImGui OpenGL3 backend)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(800, 520, "LAN Jam Synth", nullptr, nullptr);
  if (!window) {
    std::printf("[GUI] glfwCreateWindow failed\n");
    glfwTerminate();
    return 1;
  }
  std::printf("[GUI] window created\n");

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::printf("[GUI] gladLoadGLLoader failed (OpenGL loader)\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
  std::printf("[GUI] gladLoadGLLoader ok\n");
  std::printf("[GUI] gladLoadGL ok\n");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
  std::printf("[GUI] ImGui backend init ok\n");

  static char hostBuf[64];
  bool initHost = true;

  while (!glfwWindowShouldClose(window) && !shared.quitRequested.load()) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Controls");
    // Network
    ImGui::Text("Network");
    if (initHost) { std::snprintf(hostBuf, sizeof(hostBuf), "%s", shared.serverHost.c_str()); initHost = false; }
    ImGui::InputText("Server", hostBuf, IM_ARRAYSIZE(hostBuf));
    int p = static_cast<int>(shared.serverPort);
    ImGui::SameLine();
    ImGui::InputInt("Port", &p);
    p = std::clamp(p, 1, 65535);
    shared.serverPort = static_cast<uint16_t>(p);
    if (ImGui::Button("Connect")) { shared.serverHost = hostBuf; shared.connectRequested.store(true); }

    ImGui::Separator();
    // Synth
    ImGui::Text("Synth");
    const char* waves[] = {"Saw", "Square", "Sine"};
    int wf = shared.params.waveform.load();
    if (ImGui::Combo("Waveform", &wf, waves, IM_ARRAYSIZE(waves))) shared.params.waveform.store(wf);

    float f  = shared.params.freq.load();
    float c  = shared.params.cutoff.load();
    float r  = shared.params.resonance.load();
    float rg = shared.params.remoteGain.load();
    bool changed = false;
    changed |= ImGui::SliderFloat("Frequency Hz", &f, 40.0f, 1000.0f, "%.1f");
    changed |= ImGui::SliderFloat("Cutoff Hz",   &c, 100.0f, 8000.0f, "%.0f");
    changed |= ImGui::SliderFloat("Resonance",   &r, 0.05f, 0.95f, "%.2f");
    changed |= ImGui::SliderFloat("Remote Gain", &rg, 0.0f, 1.0f, "%.2f");
    if (changed) {
      shared.params.freq.store(f);
      shared.params.cutoff.store(c);
      shared.params.resonance.store(r);
      shared.params.remoteGain.store(rg);
    }

    ImGui::Separator();
    ImGui::Text("Stats");
    ImGui::Text("RX packets: %u", shared.stats.rxPackets.load());
    ImGui::Text("Jitter depth: %zu blocks", shared.stats.jitterDepth.load());
    ImGui::Text("XRuns: %u", shared.stats.xruns.load());
    if (ImGui::Button("Quit")) shared.quitRequested.store(true);
    ImGui::End();

    // Render
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  std::printf("[GUI] loop ended\n");
  return 0;
}
