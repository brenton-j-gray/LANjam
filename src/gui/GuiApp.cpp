#include "GuiApp.h"

#include <atomic>
#include <cstdio>
#include <algorithm>
#include <cinttypes>
#include <string>
#include <cfloat>
#include <vector>
#include <cmath>
#include <complex>
#include <array>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>

#define GLFW_INCLUDE_NONE
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "GuiStyle.h"
#include "audio/SynthVoice.h"

// Simple ImGui rotary knob widget (returns true if value changed)
// showLabelBelow: when false the knob will not render its label/number below the control
static bool ImGuiKnob(const char* label, int* v, int v_min, int v_max, float size = 48.0f, bool showLabelBelow = true) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  // Use the pointer to the value as the ID base so the same label can be used
  // in multiple places (e.g., top transport + Sequencer tab) without conflicts.
  ImGui::PushID((const void*)v);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 center = ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
  ImGui::InvisibleButton(label, ImVec2(size, size));
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  // background circle
  float radius = size * 0.5f - 4.0f;
  draw->AddCircleFilled(center, radius, ImGui::GetColorU32(ImGuiCol_FrameBg));

  // compute angles
  float t = 0.0f;
  if (v_max > v_min) t = static_cast<float>((*v - v_min)) / static_cast<float>(v_max - v_min);
  const float a_min = -135.0f * (3.14159265f / 180.0f);
  const float a_max = 135.0f * (3.14159265f / 180.0f);
  float angle = a_min + t * (a_max - a_min);

  // arc indicator (simple line)
  ImVec2 p1 = ImVec2(center.x + std::cos(angle) * (radius - 6.0f), center.y + std::sin(angle) * (radius - 6.0f));
  draw->AddLine(center, p1, ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : ImGuiCol_Button), 3.0f);

  // knob border
  draw->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border));

  // interaction: vertical drag changes value
  bool changed = false;
  if (active && ImGui::GetIO().MouseDown[0]) {
    float sensitivity = (v_max - v_min) / 100.0f; if (sensitivity < 0.5f) sensitivity = 0.5f;
    float delta = -io.MouseDelta.y * sensitivity;
    int nv = *v + static_cast<int>(std::round(delta));
    nv = std::clamp(nv, v_min, v_max);
    if (nv != *v) { *v = nv; changed = true; }
  }

  // label and numeric (optionally drawn below the knob)
  if (showLabelBelow) {
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size + 4.0f));
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::Text("%d", *v);
    ImGui::NewLine();
  }
  ImGui::PopID();
  return changed;
}

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

  // Larger default window so all Synth controls (oscillators etc.) are visible
  GLFWwindow* window = glfwCreateWindow(1200, 700, "LAN Jam Client", nullptr, nullptr);
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

  // Sequencer grid dimensions used by the GUI (must match GuiState::sequencer grid)
  constexpr int kSeqRows = 12;
  constexpr int kSeqSteps = 16;

  // Sequencer is now stored in shared.sequencer (lock-free atomics) and advanced from the audio callback

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

    // (Transport controls moved inside the main ImGui window so UI is contained in a single element)

      

  // (Sequencer state defined above near thread start)

  const ImGuiStyle& style = ImGui::GetStyle();
  // Slightly narrower piano keys and tighter spacing to reduce overall window width
  const float pianoButtonWidth = 36.0f;
  const float pianoSpacing = 3.0f;
    const float pianoContentWidth = 12.0f * pianoButtonWidth + 11.0f * pianoSpacing;
    const float windowWidth = std::max(420.0f, pianoContentWidth + style.WindowPadding.x * 2.0f + 24.0f);
    // Make the main ImGui window match the GLFW window size and lock it (no floating elements)
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("LAN Jam Client", nullptr, windowFlags);

    // --- Transport controls (now inside the main window at its top) ---
    ImGui::BeginGroup();
    int bpmVal = shared.sequencer.bpm.load();
    // Draw the knob without its label so we can place the numeric inline with controls
    if (ImGuiKnob("BPM", &bpmVal, 40, 240, 56.0f, false)) {
      shared.sequencer.bpm.store(bpmVal);
    }
    ImGui::SameLine();
    ImGui::Text("BPM %d", bpmVal);
    ImGui::SameLine();
    bool isPlaying = shared.sequencer.playing.load();
    if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
      shared.sequencer.playing.store(!isPlaying);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
      shared.sequencer.playing.store(false);
      shared.sequencer.step.store(0);
      shared.noteGate.store(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
      shared.sequencer.step.store(0);
      shared.sequencer.playing.store(true);
    }
    ImGui::SameLine();
    int poly = shared.polyphony.load();
    ImGui::PushItemWidth(100.0f);
    if (ImGui::SliderInt("Poly", &poly, 1, 64)) {
      shared.polyphony.store(poly);
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();

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
        // Use press-and-hold behavior: while the button is active (mouse held) the gate is true.
        int currentNote = shared.params.note.load();
        static int activeKeyHeld = -1; // which key is currently held by mouse (GUI thread only)
        for (int i = 0; i < 12; ++i) {
          ImGui::PushID(i);
          bool selected = (currentNote == i);
          ImVec2 size(46.0f, 0.0f);
          if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.24f, 0.60f, 0.36f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.70f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.55f, 0.32f, 1.0f));
          }

          // Create the piano key button (ImGui::Button returns true on click release). We
          // inspect the item's active state to detect mouse-hold for gate behavior.
          ImGui::Button(kNoteNames[i], size);
          bool held = ImGui::IsItemActive();
          if (held) {
            // when held, set the selected note and raise the gate
            shared.params.note.store(i);
            currentNote = i;
            // request a note-on event for this key (audio thread will consume)
            shared.noteOnRequests.fetch_or(static_cast<uint16_t>(1u << i));
            activeKeyHeld = i;
          } else {
            // if this key was previously held but now released, drop the gate
            if (activeKeyHeld == i) {
              // request a note-off event for this key
              shared.noteOffRequests.fetch_or(static_cast<uint16_t>(1u << i));
              activeKeyHeld = -1;
            }
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
          // Use a reduced item width for oscillator controls to make columns narrower
          ImGui::PushItemWidth(140.0f);
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
          ImGui::PopItemWidth();
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

  ImGui::SeparatorText("Amplitude Envelope (ADSR)");
  float attack = shared.params.envAttack.load();
  if (ImGui::SliderFloat("Attack (s)", &attack, 0.001f, 2.0f, "%.3f")) shared.params.envAttack.store(attack);
  float decay = shared.params.envDecay.load();
  if (ImGui::SliderFloat("Decay (s)", &decay, 0.001f, 2.0f, "%.3f")) shared.params.envDecay.store(decay);
  float sustain = shared.params.envSustain.load();
  if (ImGui::SliderFloat("Sustain", &sustain, 0.0f, 1.0f, "%.2f")) shared.params.envSustain.store(sustain);
  float release = shared.params.envRelease.load();
  if (ImGui::SliderFloat("Release (s)", &release, 0.001f, 5.0f, "%.3f")) shared.params.envRelease.store(release);

        // Sequencer moved to its own tab (see below)

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

      if (ImGui::BeginTabItem("Sequencer")) {
        // --- Sequencer tab ---
        ImGui::Text("Sequencer");

        // BPM control (writes into shared.sequencer) - rotary knob
        int bpmVal = shared.sequencer.bpm.load();
        if (ImGuiKnob("BPM", &bpmVal, 40, 240, 48.0f)) {
          shared.sequencer.bpm.store(bpmVal);
        }
        ImGui::SameLine();
        bool isPlaying = shared.sequencer.playing.load();
        if (ImGui::Button(isPlaying ? "Stop" : "Play")) {
          shared.sequencer.playing.store(!isPlaying);
          if (shared.sequencer.playing.load()) {
            shared.sequencer.step.store(0);
          } else {
            shared.noteGate.store(false);
          }
        }

        // Table layout: first column for note name, remaining columns for steps
        if (ImGui::BeginTable("seq_table", 1 + kSeqSteps, ImGuiTableFlags_SizingFixedFit)) {
          ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, 40.0f);
          for (int c = 0; c < kSeqSteps; ++c) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 20.0f);
          ImGui::TableHeadersRow();

          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
          int activeStep = shared.sequencer.step.load();
          bool playingNow = shared.sequencer.playing.load();
          for (int r = kSeqRows - 1; r >= 0; --r) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kNoteNames[r]);
            for (int s = 0; s < kSeqSteps; ++s) {
              ImGui::TableSetColumnIndex(1 + s);
              ImGui::PushID((r<<8) | s);
              ImVec2 bsize(18.0f, 18.0f);
              bool val = (shared.sequencer.grid[r][s].load() != 0);
              bool isActiveStep = playingNow && (s == activeStep);

              // Coloring priority: if the cell is active (val) show orange.
              // Otherwise if this column is the active step, show the step highlight (blue).
              if (val) {
                // orange for active cell
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.45f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.60f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.40f, 0.08f, 1.0f));
                if (ImGui::Button("##cell", bsize)) {
                  uint8_t cur = shared.sequencer.grid[r][s].load();
                  shared.sequencer.grid[r][s].store(cur ? 0 : 1);
                }
                // draw a small centered dot to indicate an active cell (contrasting color)
                {
                  ImVec2 imin = ImGui::GetItemRectMin();
                  ImVec2 imax = ImGui::GetItemRectMax();
                  ImVec2 center = ImVec2((imin.x + imax.x) * 0.5f, (imin.y + imax.y) * 0.5f);
                  float w = imax.x - imin.x;
                  float h = imax.y - imin.y;
                  float dotR = std::min(w, h) * 0.16f; // small radius relative to button size
                  ImDrawList* draw = ImGui::GetWindowDrawList();
                  draw->AddCircleFilled(center, dotR, ImGui::GetColorU32(ImGuiCol_Text));
                }
                ImGui::PopStyleColor(3);
              } else {
                if (isActiveStep) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.8f, 1.0f));
                if (ImGui::Button("##cell", bsize)) {
                  uint8_t cur = shared.sequencer.grid[r][s].load();
                  shared.sequencer.grid[r][s].store(cur ? 0 : 1);
                }
                if (isActiveStep) ImGui::PopStyleColor();
              }

              ImGui::PopID();
            }
          }
          ImGui::PopStyleVar();
          ImGui::EndTable();
        }

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

    // Sequencer timing is handled by the high-resolution thread started above.

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

  // Signal other threads that the GUI is exiting
  shared.quitRequested.store(true);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  std::printf("[GUI] loop ended\n");
  return 0;
}
