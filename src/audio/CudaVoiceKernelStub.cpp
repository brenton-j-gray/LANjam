#include "audio/CudaVoiceKernel.h"

#if !defined(LANJAM_ENABLE_CUDA)

LanJamCudaVoiceEngine* lanjam_cuda_create_engine(int, int, float) {
  return nullptr;
}

void lanjam_cuda_destroy_engine(LanJamCudaVoiceEngine*) {}

bool lanjam_cuda_render_saw_block(
    LanJamCudaVoiceEngine*,
    const float*,
    float*,
    const int*,
    const int*,
    const float*,
    const float*,
    float*,
    int*,
    float*,
    float*,
    int,
    int,
    float,
    float,
    float,
    float,
    float,
    int,
    float,
    float*,
    float*) {
  return false;
}

#endif
