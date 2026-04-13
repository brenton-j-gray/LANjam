#include "audio/CudaVoiceKernel.h"

#if defined(LANJAM_ENABLE_CUDA)

#include <cuda_runtime.h>

struct LanJamCudaVoiceEngine {
  int maxVoices = 0;
  int maxFrames = 0;
  float sampleRate = 48000.0f;
  cudaStream_t stream = nullptr;

  float* dFreq = nullptr;
  float* dPhaseIn = nullptr;
  float* dPhaseOut = nullptr;
  int* dOscWaveType = nullptr;
  int* dOscOctave = nullptr;
  float* dOscDetune = nullptr;
  float* dOscPhaseOffset = nullptr;
  float* dEnvLevelIn = nullptr;
  float* dEnvLevelOut = nullptr;
  int* dEnvStageIn = nullptr;
  int* dEnvStageOut = nullptr;
  float* dEnvIncIn = nullptr;
  float* dEnvIncOut = nullptr;
  float* dFilterStateIn = nullptr;
  float* dFilterStateOut = nullptr;
  float* dVoiceBuffer = nullptr;
  float* dOutMono = nullptr;
};

namespace {

__global__ void render_voices_kernel(
    const float* __restrict__ freq,
    const float* __restrict__ phaseIn,
    float* __restrict__ phaseOut,
    const int* __restrict__ oscWaveType,
    const int* __restrict__ oscOctave,
    const float* __restrict__ oscDetuneCents,
    const float* __restrict__ oscPhaseOffset,
    const float* __restrict__ envLevelIn,
    float* __restrict__ envLevelOut,
    const int* __restrict__ envStageIn,
    int* __restrict__ envStageOut,
    const float* __restrict__ envIncIn,
    float* __restrict__ envIncOut,
    const float* __restrict__ filterStateIn,
    float* __restrict__ filterStateOut,
    float* __restrict__ voiceBuffer,
    int voiceCount,
    int frameCount,
    float sampleRate,
    float envAttackSec,
    float envDecaySec,
    float envSustainLevel,
    float envReleaseSec,
    float filterCutoffHz,
    int filterType) {
  const int voice = blockIdx.x * blockDim.x + threadIdx.x;
  if (voice >= voiceCount) return;

  float envLevel = envLevelIn[voice];
  int stage = envStageIn[voice];
  float envInc = envIncIn[voice];
  float filterState = filterStateIn[voice];
  const float baseInc = freq[voice] / sampleRate;
  const int base = voice * frameCount;
  float phases[3] = {
      phaseIn[voice * 3 + 0],
      phaseIn[voice * 3 + 1],
      phaseIn[voice * 3 + 2]};
  float phaseIncs[3];
  for (int osc = 0; osc < 3; ++osc) {
    phaseIncs[osc] =
        baseInc * exp2f(static_cast<float>(oscOctave[osc]) / 12.0f) * exp2f(oscDetuneCents[osc] / 1200.0f);
  }

  const float attackSamples = fmaxf(1.0f, envAttackSec * sampleRate);
  const float decaySamples = fmaxf(1.0f, envDecaySec * sampleRate);
  const float releaseSamples = fmaxf(1.0f, envReleaseSec * sampleRate);
  const float attackInc = 1.0f / attackSamples;
  const float decayInc = -(1.0f - envSustainLevel) / decaySamples;
  const float releaseFallbackInc = -fmaxf(0.0f, envLevel) / releaseSamples;
  const float clampedCutoff = fminf(fmaxf(filterCutoffHz, 20.0f), sampleRate * 0.45f);
  const float alpha = 1.0f - expf(-6.28318530717958647692f * clampedCutoff / sampleRate);
  constexpr int kEnvIdle = 0;
  constexpr int kEnvAttack = 1;
  constexpr int kEnvDecay = 2;
  constexpr int kEnvSustain = 3;
  constexpr int kEnvRelease = 4;

  for (int frame = 0; frame < frameCount; ++frame) {
    float oscSum = 0.0f;
    for (int osc = 0; osc < 3; ++osc) {
      phases[osc] += phaseIncs[osc];
      phases[osc] -= floorf(phases[osc]);
      float phase = phases[osc] + oscPhaseOffset[osc];
      phase -= floorf(phase);

      float oscSample = 0.0f;
      if (oscWaveType[osc] == 1) {
        oscSample = (phase < 0.5f) ? 1.0f : -1.0f;
      } else if (oscWaveType[osc] == 2) {
        oscSample = sinf(phase * 6.28318530717958647692f);
      } else {
        oscSample = 2.0f * phase - 1.0f;
      }
      oscSum += oscSample;
    }

    switch (stage) {
      case kEnvAttack:
        envLevel += attackInc;
        if (envLevel >= 1.0f) {
          envLevel = 1.0f;
          stage = kEnvDecay;
          envInc = decayInc;
        }
        break;
      case kEnvDecay:
        envLevel += envInc;
        if (envLevel <= envSustainLevel) {
          envLevel = envSustainLevel;
          stage = kEnvSustain;
          envInc = 0.0f;
        }
        break;
      case kEnvSustain:
        envLevel = envSustainLevel;
        break;
      case kEnvRelease:
        if (envInc >= 0.0f) envInc = releaseFallbackInc;
        envLevel += envInc;
        if (envLevel <= 0.0f) {
          envLevel = 0.0f;
          stage = kEnvIdle;
          envInc = 0.0f;
        }
        break;
      case kEnvIdle:
      default:
        envLevel = 0.0f;
        envInc = 0.0f;
        break;
    }

    float sample = (oscSum / 3.0f) * envLevel;
    // Lightweight filter prototype: one-pole LP only for filterType=Low(0).
    if (filterType == 0) {
      filterState += alpha * (sample - filterState);
      sample = filterState;
    }
    voiceBuffer[base + frame] = sample;
  }

  phaseOut[voice * 3 + 0] = phases[0];
  phaseOut[voice * 3 + 1] = phases[1];
  phaseOut[voice * 3 + 2] = phases[2];
  envLevelOut[voice] = envLevel;
  envStageOut[voice] = stage;
  envIncOut[voice] = envInc;
  filterStateOut[voice] = filterState;
}

__global__ void mixdown_kernel(
    const float* __restrict__ voiceBuffer,
    float* __restrict__ outMono,
    int voiceCount,
    int frameCount,
    float gain) {
  const int frame = blockIdx.x * blockDim.x + threadIdx.x;
  if (frame >= frameCount) return;

  float acc = 0.0f;
  for (int voice = 0; voice < voiceCount; ++voice) {
    acc += voiceBuffer[voice * frameCount + frame];
  }
  outMono[frame] = gain * acc;
}

bool ok(cudaError_t err) {
  return err == cudaSuccess;
}

}  // namespace

LanJamCudaVoiceEngine* lanjam_cuda_create_engine(int maxVoices, int maxFrames, float sampleRate) {
  if (maxVoices <= 0 || maxFrames <= 0) return nullptr;

  auto* engine = new LanJamCudaVoiceEngine();
  engine->maxVoices = maxVoices;
  engine->maxFrames = maxFrames;
  engine->sampleRate = sampleRate;

  const size_t voiceBytes = static_cast<size_t>(maxVoices) * sizeof(float);
  const size_t voiceIntBytes = static_cast<size_t>(maxVoices) * sizeof(int);
  const size_t voicePhaseBytes = static_cast<size_t>(maxVoices) * 3 * sizeof(float);
  const size_t oscIntBytes = 3 * sizeof(int);
  const size_t oscFloatBytes = 3 * sizeof(float);
  const size_t voiceFrameBytes = static_cast<size_t>(maxVoices) * static_cast<size_t>(maxFrames) * sizeof(float);
  const size_t outBytes = static_cast<size_t>(maxFrames) * sizeof(float);

  if (!ok(cudaStreamCreate(&engine->stream)) ||
      !ok(cudaMalloc(&engine->dFreq, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dPhaseIn, voicePhaseBytes)) ||
      !ok(cudaMalloc(&engine->dPhaseOut, voicePhaseBytes)) ||
      !ok(cudaMalloc(&engine->dOscWaveType, oscIntBytes)) ||
      !ok(cudaMalloc(&engine->dOscOctave, oscIntBytes)) ||
      !ok(cudaMalloc(&engine->dOscDetune, oscFloatBytes)) ||
      !ok(cudaMalloc(&engine->dOscPhaseOffset, oscFloatBytes)) ||
      !ok(cudaMalloc(&engine->dEnvLevelIn, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dEnvLevelOut, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dEnvStageIn, voiceIntBytes)) ||
      !ok(cudaMalloc(&engine->dEnvStageOut, voiceIntBytes)) ||
      !ok(cudaMalloc(&engine->dEnvIncIn, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dEnvIncOut, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dFilterStateIn, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dFilterStateOut, voiceBytes)) ||
      !ok(cudaMalloc(&engine->dVoiceBuffer, voiceFrameBytes)) ||
      !ok(cudaMalloc(&engine->dOutMono, outBytes))) {
    lanjam_cuda_destroy_engine(engine);
    return nullptr;
  }

  return engine;
}

void lanjam_cuda_destroy_engine(LanJamCudaVoiceEngine* engine) {
  if (!engine) return;
  if (engine->dOutMono) cudaFree(engine->dOutMono);
  if (engine->dVoiceBuffer) cudaFree(engine->dVoiceBuffer);
  if (engine->dEnvIncOut) cudaFree(engine->dEnvIncOut);
  if (engine->dEnvIncIn) cudaFree(engine->dEnvIncIn);
  if (engine->dFilterStateOut) cudaFree(engine->dFilterStateOut);
  if (engine->dFilterStateIn) cudaFree(engine->dFilterStateIn);
  if (engine->dEnvStageOut) cudaFree(engine->dEnvStageOut);
  if (engine->dEnvStageIn) cudaFree(engine->dEnvStageIn);
  if (engine->dEnvLevelOut) cudaFree(engine->dEnvLevelOut);
  if (engine->dEnvLevelIn) cudaFree(engine->dEnvLevelIn);
  if (engine->dOscPhaseOffset) cudaFree(engine->dOscPhaseOffset);
  if (engine->dOscDetune) cudaFree(engine->dOscDetune);
  if (engine->dOscOctave) cudaFree(engine->dOscOctave);
  if (engine->dOscWaveType) cudaFree(engine->dOscWaveType);
  if (engine->dPhaseOut) cudaFree(engine->dPhaseOut);
  if (engine->dPhaseIn) cudaFree(engine->dPhaseIn);
  if (engine->dFreq) cudaFree(engine->dFreq);
  if (engine->stream) cudaStreamDestroy(engine->stream);
  delete engine;
}

bool lanjam_cuda_render_saw_block(
    LanJamCudaVoiceEngine* engine,
    const float* voiceFreqHz,
    float* voicePhase01x3,
    const int* oscWaveType3,
    const int* oscOctave3,
    const float* oscDetuneCents3,
    const float* oscPhaseOffset3,
    float* voiceEnvLevel,
    int* voiceEnvStage,
    float* voiceEnvInc,
    float* voiceFilterState,
    int voiceCount,
    int frameCount,
    float envAttackSec,
    float envDecaySec,
    float envSustainLevel,
    float envReleaseSec,
    float filterCutoffHz,
    int filterType,
    float gain,
    float* outMono,
    float* elapsedMs) {
  if (!engine || !voiceFreqHz || !voicePhase01x3 || !oscWaveType3 || !oscOctave3 ||
      !oscDetuneCents3 || !oscPhaseOffset3 ||
      !voiceEnvLevel || !voiceEnvStage || !voiceEnvInc || !voiceFilterState || !outMono) return false;
  if (voiceCount <= 0 || frameCount <= 0) return false;
  if (voiceCount > engine->maxVoices || frameCount > engine->maxFrames) return false;

  const size_t voiceBytes = static_cast<size_t>(voiceCount) * sizeof(float);
  const size_t voiceIntBytes = static_cast<size_t>(voiceCount) * sizeof(int);
  const size_t voicePhaseBytes = static_cast<size_t>(voiceCount) * 3 * sizeof(float);
  const size_t oscIntBytes = 3 * sizeof(int);
  const size_t oscFloatBytes = 3 * sizeof(float);
  const size_t outBytes = static_cast<size_t>(frameCount) * sizeof(float);

  cudaEvent_t startEvent = nullptr;
  cudaEvent_t endEvent = nullptr;
  if (elapsedMs) {
    cudaEventCreate(&startEvent);
    cudaEventCreate(&endEvent);
    cudaEventRecord(startEvent, engine->stream);
  }

  if (!ok(cudaMemcpyAsync(engine->dFreq, voiceFreqHz, voiceBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dPhaseIn, voicePhase01x3, voicePhaseBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dOscWaveType, oscWaveType3, oscIntBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dOscOctave, oscOctave3, oscIntBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dOscDetune, oscDetuneCents3, oscFloatBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dOscPhaseOffset, oscPhaseOffset3, oscFloatBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dEnvLevelIn, voiceEnvLevel, voiceBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dEnvStageIn, voiceEnvStage, voiceIntBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dEnvIncIn, voiceEnvInc, voiceBytes, cudaMemcpyHostToDevice, engine->stream)) ||
      !ok(cudaMemcpyAsync(engine->dFilterStateIn, voiceFilterState, voiceBytes, cudaMemcpyHostToDevice, engine->stream))) {
    return false;
  }

  constexpr int kVoiceBlockSize = 128;
  const int voiceGrid = (voiceCount + kVoiceBlockSize - 1) / kVoiceBlockSize;
  render_voices_kernel<<<voiceGrid, kVoiceBlockSize, 0, engine->stream>>>(
      engine->dFreq, engine->dPhaseIn, engine->dPhaseOut,
      engine->dOscWaveType, engine->dOscOctave, engine->dOscDetune, engine->dOscPhaseOffset,
      engine->dEnvLevelIn, engine->dEnvLevelOut,
      engine->dEnvStageIn, engine->dEnvStageOut,
      engine->dEnvIncIn, engine->dEnvIncOut,
      engine->dFilterStateIn, engine->dFilterStateOut,
      engine->dVoiceBuffer,
      voiceCount, frameCount, engine->sampleRate,
      envAttackSec, envDecaySec, envSustainLevel, envReleaseSec,
      filterCutoffHz, filterType);

  constexpr int kFrameBlockSize = 128;
  const int frameGrid = (frameCount + kFrameBlockSize - 1) / kFrameBlockSize;
  mixdown_kernel<<<frameGrid, kFrameBlockSize, 0, engine->stream>>>(
      engine->dVoiceBuffer, engine->dOutMono, voiceCount, frameCount, gain);

  if (!ok(cudaMemcpyAsync(voicePhase01x3, engine->dPhaseOut, voicePhaseBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaMemcpyAsync(voiceEnvLevel, engine->dEnvLevelOut, voiceBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaMemcpyAsync(voiceEnvStage, engine->dEnvStageOut, voiceIntBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaMemcpyAsync(voiceEnvInc, engine->dEnvIncOut, voiceBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaMemcpyAsync(voiceFilterState, engine->dFilterStateOut, voiceBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaMemcpyAsync(outMono, engine->dOutMono, outBytes, cudaMemcpyDeviceToHost, engine->stream)) ||
      !ok(cudaStreamSynchronize(engine->stream))) {
    return false;
  }

  if (elapsedMs && startEvent && endEvent) {
    cudaEventRecord(endEvent, engine->stream);
    cudaEventSynchronize(endEvent);
    cudaEventElapsedTime(elapsedMs, startEvent, endEvent);
    cudaEventDestroy(startEvent);
    cudaEventDestroy(endEvent);
  }

  return ok(cudaGetLastError());
}

#endif
