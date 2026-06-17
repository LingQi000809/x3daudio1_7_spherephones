#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void* malloc1d(size_t dim1_data_size);
void* calloc1d(size_t dim1, size_t data_size);
void utility_svsmul(float* a, const float* s, const int len, float* c);
void simple_sscal(int n, float scale, float* x);

void getLoudspeakerDecoderMtx(float* ls_dirs_deg, int nLS, int order, float* decMtx);
void getRSH(int N, float* dirs_deg, int nDirs, float* Y);
void getSHreal(int order, float* dirs_rad, int nDirs, float* Y);
void unnorm_legendreP(int n, double* x, int lenX, double* y);

float L2_norm3(float v[3]);
void cart2sph(float* cart, int nDirs, int anglesInDegreesFLAG, float* sph);

#ifdef __cplusplus
}
#endif
