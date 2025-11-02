#pragma once
#include <rtaudio/RtAudio.h>
#include <functional>

class AudioIO {
public:
  using Callback = std::function<void(float* out, unsigned nframes)>;

  bool open(unsigned sampleRate = 48000, unsigned frames = 128);
  void close();
  void set_callback(Callback cb) { cb_ = std::move(cb); }

private:
  static int rt_cb(void* out, void* in, unsigned nFrames, double streamTime,
                   RtAudioStreamStatus status, void* userData);
  RtAudio audio_;
  Callback cb_;
};
