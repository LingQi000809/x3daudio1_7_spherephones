#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* for size_t */

/* Memory helpers */
void* malloc1d(size_t dim1_data_size);
void* calloc1d(size_t dim1, size_t data_size);

/* Vector / scalar operations */
void utility_svsmul(float* a, const float* s, const int len, float* c);
void simple_sscal(int n, float alpha, float* x);

/* Ambisonic / spherical harmonic routines */
void getLoudspeakerDecoderMtx(float* ls_dirs_deg, int nLS, int order, float* decMtx);

/* 3D Vector Base Amplitude Panning (Pulkki) for a single source direction.
 * faces: nFaces*3 indices into ls_dirs_deg, defining the loudspeaker
 * triangulation (convex hull of unit-vector speaker positions).
 * gains: caller-allocated output buffer of length nLS. */
void vbap3D_oneSource(float src_az_deg,
                      float src_el_deg,
                      float* ls_dirs_deg,
                      int nLS,
                      int* faces,
                      int nFaces,
                      float* gains);

/* All-Round Ambisonic Decoder (Zotter & Frank, JAES 2012).
 * Decodes ambisonics to a dense uniform virtual loudspeaker grid
 * (Fibonacci sphere) via SAD, then re-pans each virtual speaker to the
 * real loudspeakers via 3D VBAP. Energy-preserving and well-suited to
 * irregular loudspeaker layouts. decMtx is nLS x (order+1)^2.
 * Ported from leomccormack/Spatial_Audio_Framework: saf_hoa_internal.c. */
void getLoudspeakerDecoderMtx_AllRAD(float* ls_dirs_deg,
                                     int nLS,
                                     int* faces,
                                     int nFaces,
                                     int order,
                                     float* decMtx);

void getRSH(int N,
            float* dirs_deg,
            int nDirs,
            float* Y);

void getSHreal(int order,
               float* dirs_rad,
               int nDirs,
               float* Y); /* (order+1)^2 x nDirs */

/* Unnormalised associated Legendre polynomials */
void unnorm_legendreP(int n,
                      double* x,
                      int lenX,
                      double* y); /* FLAT: (n+1) x lenX */

/* Utility functions */
float L2_norm3(float v[3]);

void cart2sph(float* cart,
              int nDirs,
              int anglesInDegreesFLAG,
              float* sph);

#ifdef __cplusplus
}
#endif
