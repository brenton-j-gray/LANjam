#include "GuiApp.h"

#include <atomic>
#include <cstdio>
#include <algorithm>
#include <cinttypes>
#include <string>
#include <cfloat>
#include <vector>
#include <complex>
#include <array>

#define GLFW_INCLUDE_NONE
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "GuiStyle.h"
#include "audio/SynthVoice.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}

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

  GLFWwindow* window = glfwCreateWindow(900, 580, "LAN Jam Client", nullptr, nullptr);
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
  ApplyLanJamStyle(1.0f);
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

    std::string discoveryMsg;
    std::string discoveredHost;
    {
      std::lock_guard<std::mutex> lock(shared.discoveryMutex);
      discoveryMsg = shared.discoveryMessage;
      discoveredHost = shared.discoveredHost;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pianoButtonWidth = 46.0f;
    const float pianoSpacing = 4.0f;
    const float pianoContentWidth = 12.0f * pianoButtonWidth + 11.0f * pianoSpacing;
    const float windowWidth = std::max(420.0f, pianoContentWidth + style.WindowPadding.x * 2.0f + 24.0f);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 120.0f), ImVec2(windowWidth, FLT_MAX));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("LAN Jam Client", nullptr, windowFlags);

    if (ImGui::BeginTabBar("ClientTabs")) {
      if (ImGui::BeginTabItem("Connection")) {
        bool updateHostBuf = initHost;
        if (shared.hostDirty.exchange(false)) updateHostBuf = true;
        if (updateHostBuf) {
          std::snprintf(hostBuf, sizeof(hostBuf), "%s", shared.serverHost.c_str());
          initHost = false;
        }

        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("Server", hostBuf, IM_ARRAYSIZE(hostBuf));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        int p = static_cast<int>(shared.serverPort);
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("Port", &p);
        p = std::clamp(p, 0, 65535);
        shared.serverPort = static_cast<uint16_t>(p);
        if (p == 0) { ImGui::SameLine(); ImGui::TextUnformatted("(auto)"); }

        if (ImGui::Button("Connect")) {
          shared.serverHost = hostBuf;
          shared.connectRequested.store(true);
        }
        ImGui::SameLine();
        bool discovering = shared.discovering.load();
        ImGui::BeginDisabled(discovering);
        if (ImGui::Button("Discover LAN")) {
          shared.discoverRequested.store(true);
        }
        ImGui::EndDisabled();
        if (discovering) {
          ImGui::SameLine();
          ImGui::TextUnformatted("Searching...");
        } else if (!discoveryMsg.empty()) {
          ImGui::TextWrapped("%s", discoveryMsg.c_str());
        }
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Synth")) {
        int octave = shared.params.octave.load();
        if (ImGui::SliderInt("Octave", &octave, 1, 7)) {
          shared.params.octave.store(octave);
        }

        ImGui::Text("Piano");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 6.0f));
        int currentNote = shared.params.note.load();
        for (int i = 0; i < 12; ++i) {
          ImGui::PushID(i);
          bool selected = (currentNote == i);
          ImVec2 size(46.0f, 0.0f);
          if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.24f, 0.60f, 0.36f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.70f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.55f, 0.32f, 1.0f));
          }
          if (ImGui::Button(kNoteNames[i], size)) {
            shared.params.note.store(i);
            currentNote = i;
          }
          if (selected) ImGui::PopStyleColor(3);
          if (i != 11) ImGui::SameLine();
          ImGui::PopID();
        }
        ImGui::PopStyleVar();

        ImGui::SeparatorText("Oscillators");
        const char* oscWaves[] = {"Saw", "Square", "Sine"};
        for (int osc = 0; osc < 3; ++osc) {
          ImGui::PushID(osc);
          ImGui::BeginGroup();
          ImGui::Text("Osc %d", osc + 1);
          int wave = shared.params.osc[osc].wave.load();
          if (ImGui::Combo("Wave", &wave, oscWaves, IM_ARRAYSIZE(oscWaves))) {
            shared.params.osc[osc].wave.store(wave);
          }
          int oct = shared.params.osc[osc].octave.load();
          if (ImGui::SliderInt("Octave", &oct, -24, 24)) {
            shared.params.osc[osc].octave.store(oct);
          }
          float det = shared.params.osc[osc].detune.load();
          if (ImGui::SliderFloat("Detune (cents)", &det, -200.0f, 200.0f, "%.1f")) {
            shared.params.osc[osc].detune.store(det);
          }
          float phase = shared.params.osc[osc].phase.load();
          if (ImGui::SliderFloat("Phase", &phase, 0.0f, 360.0f, "%.0f")) {
            shared.params.osc[osc].phase.store(phase);
          }
          ImGui::EndGroup();
          if (osc != 2) ImGui::SameLine();
          ImGui::PopID();
        }

        ImGui::SeparatorText("Filter");

        const char* filterTypes[] = {"Low-pass", "Band-pass", "High-pass"};
        int filterType = shared.params.filterType.load();
        if (ImGui::Combo("Filter Type", &filterType, filterTypes, IM_ARRAYSIZE(filterTypes))) {
          shared.params.filterType.store(filterType);
        }

        float cutoff = shared.params.cutoff.load();
        if (ImGui::SliderFloat("Cutoff Hz", &cutoff, 40.0f, 16000.0f, "%.0f")) shared.params.cutoff.store(cutoff);

        float q = shared.params.resonance.load();
        if (ImGui::SliderFloat("Filter Q", &q, 0.2f, 8.0f, "%.2f")) shared.params.resonance.store(q);

        int stages = shared.params.filterSlope.load();
        if (ImGui::SliderInt("Slope (stages)", &stages, 1, 4)) shared.params.filterSlope.store(stages);

        float rg = shared.params.remoteGain.load();
        if (ImGui::SliderFloat("Remote Gain", &rg, 0.0f, 1.0f, "%.2f")) shared.params.remoteGain.store(rg);

        constexpr int kResponsePoints = 128;
        static std::array<float, kResponsePoints> response{};
        const float sampleRate = 48000.0f;
        const float logStart = std::log10(20.0f);
        const float logEnd = std::log10(sampleRate * 0.5f);

        float stage_b0, stage_b1, stage_b2, stage_a1, stage_a2;
        SynthVoice::computeCoefficients(static_cast<SynthVoice::FilterType>(filterType), cutoff, q, sampleRate, stage_b0, stage_b1, stage_b2, stage_a1, stage_a2);

        for (int i = 0; i < kResponsePoints; ++i) {
          float t = kResponsePoints == 1 ? 0.0f : static_cast<float>(i) / (kResponsePoints - 1);
          float freq = std::pow(10.0f, logStart + t * (logEnd - logStart));
          float w = 2.0f * kPi * freq / sampleRate;
          float cosw = std::cos(w);
          float sinw = std::sin(w);
          float cos2 = std::cos(2.0f * w);
          float sin2 = std::sin(2.0f * w);
          float numReal = stage_b0 + stage_b1 * cosw + stage_b2 * cos2;
          float numImag = -(stage_b1 * sinw + stage_b2 * sin2);
          float denReal = 1.0f + stage_a1 * cosw + stage_a2 * cos2;
          float denImag = -(stage_a1 * sinw + stage_a2 * sin2);
          float mag = std::sqrt(numReal * numReal + numImag * numImag) / std::sqrt(denReal * denReal + denImag * denImag + 1e-12f);
          mag = std::pow(mag, static_cast<float>(stages));
          response[i] = 20.0f * std::log10(std::max(mag, 1e-5f));
        }
        ImGui::PlotLines("Frequency Response (dB)", response.data(), kResponsePoints, 0, nullptr, -60.0f, 6.0f, ImVec2(0.0f, 120.0f));
        ImGui::Text("Cutoff: %.0f Hz | Q: %.2f | Stages: %d", cutoff, q, stages);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Transport & Stats")) {
        bool audioRunning = shared.audioRunning.load();
        ImGui::Text("Audio status: %s", audioRunning ? "Running" : "Stopped");
        ImGui::BeginDisabled(audioRunning);
        if (ImGui::Button("Start Audio")) shared.audioStartRequested.store(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!audioRunning);
        if (ImGui::Button("Stop Audio")) shared.audioStopRequested.store(true);
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("RX packets: %u", shared.stats.rxPackets.load());
        ImGui::Text("Jitter depth: %zu blocks", shared.stats.jitterDepth.load());
        ImGui::Text("XRuns: %u", shared.stats.xruns.load());

        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    if (ImGui::Button("Quit", ImVec2(80.0f, 0.0f))) shared.quitRequested.store(true);

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
