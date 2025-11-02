# LANjam

LANjam is a tiny LAN jam-session prototype that pairs an UDP fan-out server with lightweight clients that generate and exchange raw PCM audio frames. Each client renders a local synth voice for zero-latency monitoring while mixing in audio received from peers, giving you a feel for the eventual networked instrument.

## Features
- Low-latency UDP transport with minimal fan-out server.
- Simple jitter buffer to smooth network delivery.
- RtAudio-backed output and synth voice for instant audio feedback.
- Dear ImGui + GLFW front-end for realtime synth control.
- Builds with CMake and vcpkg-managed dependencies (ASIO, RtAudio, ImGui, GLFW, GLAD).

## Prerequisites
- CMake 3.25+
- A C++20 compiler (MSVC 2022, Clang 15+, or GCC 12+)
- RtAudio and ASIO headers (fetched automatically via vcpkg)
- [vcpkg](https://github.com/microsoft/vcpkg) (manifest mode) with `VCPKG_ROOT` exported in your environment

## Configure & Build
```powershell
# From the repository root
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

If you want a Debug build, swap `Release` for `Debug`. The resulting binaries land in `build/Release/` (or `build/Debug/`).

## Run It
- `lan_jam_server`: Accepts a UDP port (default `50000`) and fans audio packets out to all connected peers.
- `lan_jam_client_gui`: Launches the GUI client. Pick an address/port, click **Connect**, and tweak synth parameters live.
- `lan_jam_client`: Legacy console client kept around for quick headless tests.

## Test Drive

### A) Single-machine sanity check (localhost)
Start the server (leave this window open):
```powershell
.\build\Release\lan_jam_server.exe 50000
```

Start one client (new terminal):
```powershell
.\build\Release\lan_jam_client_gui.exe
```
In the GUI window, leave the defaults (`127.0.0.1 / 50000`) and click **Connect**. You should hear a saw wave immediately (that's your local synth).

Start a second client (another terminal):
```powershell
.\build\Release\lan_jam_client_gui.exe
```
Click **Connect** in the second window. Now you'll hear your local saw plus a slightly delayed second saw (the "remote" one fanned out by the server).

Quit a client: close the GUI window or click **Quit**.  
Quit the server: `Ctrl+C`.

### B) Two-machine LAN test
On the server machine:
```powershell
ipconfig
```
Grab the IPv4 Address for your active adapter, e.g. `192.168.1.42`.

Start the server:
```powershell
.\build\Release\lan_jam_server.exe 50000
```

On each client machine (replace with your server's IP inside the GUI):
```powershell
.\build\Release\lan_jam_client_gui.exe
```
Enter the server IP (e.g. `192.168.1.42`), leave the port at `50000`, then click **Connect**.

**Windows firewall tip**  
First run may pop a firewall prompt for the server. Allow on Private networks.  
If clients can't connect: Windows Security -> Firewall -> Allow an app -> let both lan_jam_server.exe and lan_jam_client_gui.exe through on Private.

### C) Useful knobs (if audio crackles or is silent)
- In src/audio/AudioIO.cpp, bump buffer from 128 -> 256 in open(48000, 128).
- Lower synth gain in `src/audio/SynthVoice.cpp` (change `0.15f` down a notch).
- Use wired Ethernet for cleaner jitter baseline.
- Make sure the default output device in Windows matches where you expect sound.

### D) What “good” looks like
- One client: clean, immediate saw (zero-latency local monitor).
- Two+ clients: same saw plus a slightly delayed second voice. Delay shrinks later when we add Opus + a smarter jitter buffer.

## Roadmap Ideas
- Encode audio with Opus to shrink bandwidth and jitter impact.
- Replace the placeholder UDP fan-out with a stateful mixer.
- Expose adjustable latency, gain staging, and device choices via CLI flags.
- Harden the jitter buffer and add metrics so you can tune network performance.
