#include "AudioIO.h"
#include <rtaudio/RtAudio.h>
#include <cstring>
#include <exception>

int AudioIO::rt_cb(void* out, void*, unsigned nFrames, double, RtAudioStreamStatus, void* user) {
  auto* self = static_cast<AudioIO*>(user);
  auto* outF = static_cast<float*>(out);
  std::memset(outF, 0, nFrames * sizeof(float));
  if (self->cb_) self->cb_(outF, nFrames);
  return 0;
}

bool AudioIO::open(unsigned sampleRate, unsigned frames) {
  if (audio_.getDeviceCount() < 1) return false;
  RtAudio::StreamParameters oparams;
  oparams.deviceId = audio_.getDefaultOutputDevice();
  oparams.nChannels = 1;
  try {
    audio_.openStream(&oparams, nullptr, RTAUDIO_FLOAT32, sampleRate, &frames, &AudioIO::rt_cb, this);
    audio_.startStream();
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "RtAudio error: %s\n", e.what());
    return false;
  }
}
void AudioIO::close() {
  if (audio_.isStreamOpen()) {
    try {
      if (audio_.isStreamRunning()) audio_.stopStream();
      audio_.closeStream();
    } catch (...) {}
  }
}
bool AudioIO::start() {
  if (!audio_.isStreamOpen()) return false;
  try {
    if (!audio_.isStreamRunning()) audio_.startStream();
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "RtAudio start error: %s\n", e.what());
    return false;
  }
}
bool AudioIO::stop() {
  if (!audio_.isStreamOpen()) return false;
  try {
    if (audio_.isStreamRunning()) audio_.stopStream();
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "RtAudio stop error: %s\n", e.what());
    return false;
  }
}
