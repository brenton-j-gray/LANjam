#pragma once

struct LanJamCudaVoiceEngine;

LanJamCudaVoiceEngine* lanjam_cuda_create_engine(int maxVoices, int maxFrames, float sampleRate);
void lanjam_cuda_destroy_engine(LanJamCudaVoiceEngine* engine);

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
    float* elapsedMs);
