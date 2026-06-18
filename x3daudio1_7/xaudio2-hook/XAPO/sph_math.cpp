#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sph_math.h"

#define SAF_PI  ( 3.14159265358979323846264338327950288f )
#define SAF_PId ( 3.14159265358979323846264338327950288 )
#define ORDER2NSH(order) ((order+1)*(order+1))
#define SQRT4PI ( 3.544907701811032f )

void* malloc1d(size_t dim1_data_size) {
    void *ptr = malloc(dim1_data_size);
    return ptr;
}

void* calloc1d(size_t dim1, size_t data_size) {
    void *ptr = calloc(dim1, data_size);
    return ptr;
}

void utility_svsmul(float* a, const float* s, const int len, float* c)
{
    float scalar = s[0];

    if (c == NULL)
    {
        // in-place multiply: a[i] *= scalar
        for (int i = 0; i < len; i++)
            a[i] *= scalar;
    }
    else
    {
        // out-of-place multiply: c[i] = a[i] * scalar
        for (int i = 0; i < len; i++)
            c[i] = a[i] * scalar;
    }
}

/* Simple in-place scalar multiply: X[i] *= alpha */
void simple_sscal(int N, float alpha, float* X)
{
    for (int i = 0; i < N; i++)
        X[i] *= alpha;
}

static const long double factorials_21[21] =
{1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0, 3628800.0, 39916800.0, 479001600.0, 6.2270208e9, 8.71782891e10, 1.307674368000000e12, 2.092278988800000e13, 3.556874280960000e14, 6.402373705728000e15, 1.216451004088320e17, 2.432902008176640e18};
long double factorial(int n)
{
    int i;
    long double ff;
    if(n<21)
        return factorials_21[n];
    else{
        ff = 1.0;
        for(i = 1; i<=n; i++)
            ff *= (long double)i;
        return ff;
    }
}

void getLoudspeakerDecoderMtx(float* ls_dirs_deg, int nLS, int order, float* decMtx)
{
    int i, j, nSH;
    float scale;
    float* Y_ls;

    nSH = ORDER2NSH(order);
    scale = 1.0f/SQRT4PI;

    /* Sampling Ambisonic Decoder (SAD) is simply the loudspeaker
     * spherical harmonic matrix scaled by the number of loudspeakers.
     */
    Y_ls = (float*) malloc1d(nSH*nLS*sizeof(float));
    getRSH(order, ls_dirs_deg, nLS, Y_ls);
    //    cblas_sscal(nLS*nSH, scale, Y_ls, 1);
    simple_sscal(nLS * nSH, scale, Y_ls);

    for(i=0; i<nLS; i++)
        for(j=0; j<nSH; j++)
            decMtx[i*nSH+j] = (4.0f*SAF_PI) * Y_ls[j*nLS + i]/(float)nLS;
    free(Y_ls);
}

void getRSH(int N, float* dirs_deg, int nDirs, float* Y)
{
    int i, nSH;
    float scale;
    float* dirs_rad;

    if (nDirs < 1) return;

    nSH = (N + 1) * (N + 1);
    scale = sqrtf(4.0f * SAF_PI);

    /* convert [azi, elev] in degrees to [azi, inclination] in radians */
    dirs_rad = (float*)malloc1d(nDirs * 2 * sizeof(float));
    for (i = 0; i < nDirs; i++) {
        dirs_rad[i * 2 + 0] = dirs_deg[i * 2 + 0] * SAF_PI / 180.0f;
        dirs_rad[i * 2 + 1] = SAF_PI / 2.0f - (dirs_deg[i * 2 + 1] * SAF_PI / 180.0f);
    }

    /* get real-valued spherical harmonics */
    getSHreal(N, dirs_rad, nDirs, Y);

    /* remove sqrt(4*pi) normalisation term */
    utility_svsmul(Y, &scale, nSH * nDirs, NULL);

    free(dirs_rad);
}

void getSHreal(int order, float* dirs_rad, int nDirs, float* Y  /* the SH weights: (order+1)^2 x nDirs */) 
{
    int dir, j, n, m, idx_Y;
    double* Lnm;
    double* p_nm, *cos_incl;
    double* norm_real;

    if (nDirs < 1) return;

    Lnm      = (double*)malloc1d((2 * order + 1) * nDirs * sizeof(double));
    norm_real = (double*)malloc1d((2 * order + 1) * sizeof(double));
    cos_incl  = (double*)malloc1d(nDirs * sizeof(double));
    p_nm      = (double*)malloc1d((order + 1) * nDirs * sizeof(double));

    for (dir = 0; dir < nDirs; dir++)
        cos_incl[dir] = cos((double)dirs_rad[dir * 2 + 1]);

    idx_Y = 0;
    for (n = 0; n <= order; n++) {
        /* vector of unnormalised associated Legendre functions of current order */
        unnorm_legendreP(n, cos_incl, nDirs, p_nm); /* includes Condon-Shortley phase term */
        
        for(dir=0; dir<nDirs; dir++){
            /* cancel the Condon-Shortley phase from the definition of the Legendre functions to result in signless real SH */
            if (n != 0)
                for(m=-n, j=0; m<=n; m++, j++)
                    Lnm[j*nDirs+dir] = pow(-1.0, (double)abs(m)) * p_nm[abs(m)*nDirs+dir];
            else
                Lnm[dir] = p_nm[dir];
        }

        /* normalisation */
        for(m=-n, j=0; m<=n; m++, j++)
            norm_real[j] = sqrt( (2.0*(double)n+1.0) * (double)factorial(n-abs(m)) / (4.0*SAF_PId*(double)factorial(n+abs(m))) );
        
        /* norm_real * Lnm_real .* CosSin; */
        for(dir=0; dir<nDirs; dir++){
            for(m=-n, j=0; m<=n; m++, j++){
                if(j<n)
                    Y[(j+idx_Y)*nDirs+dir] = (float)(norm_real[j] * Lnm[j*nDirs+dir] * sqrt(2.0)*sin((double)(n-j)*(double)dirs_rad[dir*2]));
                else if(j==n)
                    Y[(j+idx_Y)*nDirs+dir] = (float)(norm_real[j] * Lnm[j*nDirs+dir]);
                else /* (j>n) */
                    Y[(j+idx_Y)*nDirs+dir] = (float)(norm_real[j] * Lnm[j*nDirs+dir] * sqrt(2.0)*cos((double)(abs(m))*(double)dirs_rad[dir*2]));
            }
        }
        
        /* increment */
        idx_Y = idx_Y + (2*n+1);
    }

    free(p_nm);
    free(Lnm);
    free(norm_real);
    free(cos_incl);
}

void unnorm_legendreP(int n, double* x, int lenX, double* y  /* FLAT: (n+1) x lenX  */)
{
    int i, m;
    double s, norm, scale;
    double* P, *s_n, *tc, *sqrt_n;

    if (n == 0) {
        for (i = 0; i < lenX; i++) y[i] = 1.0;
        return;
    }

    /* alloc */
    P      = (double*)calloc1d((n + 3) * lenX, sizeof(double));
    s_n    = (double*)malloc1d(lenX * sizeof(double));
    tc     = (double*)malloc1d(lenX * sizeof(double));
    sqrt_n = (double*)malloc1d((2 * n + 1) * sizeof(double));

    /* init */
    for (i = 0; i < lenX; i++) {
        s      = sqrt(1.0 - pow(x[i], 2.0)) + 2.23e-20;
        s_n[i] = pow(-s, (double)n);
        tc[i]  = -2.0 * x[i] / s;
    }
    for (i = 0; i < 2 * n + 1; i++) sqrt_n[i] = sqrt((double)i);

    norm = 1.0;
    for (i = 1; i <= n; i++) norm *= 1.0 - 1.0 / (2.0 * (double)i);

    /* Starting values for downwards recursion */
    for (i = 0; i < lenX; i++) {
        P[(n) * lenX + i]     = sqrt(norm) * s_n[i];
        P[(n - 1) * lenX + i] = P[(n) * lenX + i] * tc[i] * (double)n / sqrt_n[2 * n];
    }

    /* 3-step downwards recursion to m == 0 */
    for (m = n - 2; m >= 0; m--)
        for (i = 0; i < lenX; i++)
            P[(m) * lenX + i] = (P[(m + 1) * lenX + i] * tc[i] * ((double)m + 1.0)
                - P[(m + 2) * lenX + i] * sqrt_n[n + m + 2] * sqrt_n[n - m - 1])
                / (sqrt_n[n + m + 1] * sqrt_n[n - m]);

    /* keep up to the last 3 elements in P */
    for (i = 0; i < n + 1; i++)
        memcpy(&(y[i * lenX]), &(P[i * lenX]), lenX * sizeof(double));

    /* Account for polarity when x == -/+1 for first value of P */
    for (i = 0; i < lenX; i++)
        if (sqrt(1.0 - pow(x[i], 2.0)) == 0)
            y[i] = pow(x[i], (double)n);

    /* scale each row by: sqrt((n+m)!/(n-m)!) */
    for (m = 1; m < n; m++) {
        scale = 1.0;
        for (i = n - m + 1; i < n + m + 1; i++) scale *= sqrt_n[i];
        for (i = 0; i < lenX; i++) y[m * lenX + i] *= scale;
    }
    scale = 1.0;
    for (i = 1; i < 2 * n + 1; i++) scale *= sqrt_n[i];
    for (i = 0; i < lenX; i++) y[n * lenX + i] *= scale;

    free(P);
    free(s_n);
    free(tc);
    free(sqrt_n);
}

float L2_norm3(float v[3]) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

void cart2sph(float* cart,
              int nDirs,
              int anglesInDegreesFLAG,
              float* sph)
{
    int i;
    float hypotxy;

    for(i=0; i<nDirs; i++){
        hypotxy = sqrtf(cart[i*3]*cart[i*3] + cart[i*3+1]*cart[i*3+1]);
        sph[i*3]   = atan2f(cart[i*3+1], cart[i*3]);
        sph[i*3+1] = atan2f(cart[i*3+2], hypotxy);
        sph[i*3+2] = L2_norm3(&cart[i*3]);
    }

    /* Return in degrees instead... */
    if(anglesInDegreesFLAG){
        for(i=0; i<nDirs; i++){
            sph[i*3] *= (180.0f/SAF_PI);
            sph[i*3+1] *= (180.0f/SAF_PI);
        }
    }
}

/* ===== Helpers for VBAP / AllRAD (no external deps) ===== */

static void sph2cart_unit(float az_deg, float el_deg, float v[3])
{
    float az = az_deg * (SAF_PI/180.0f);
    float el = el_deg * (SAF_PI/180.0f);
    float ce = cosf(el);
    v[0] = ce * cosf(az);
    v[1] = ce * sinf(az);
    v[2] = sinf(el);
}

static void cart2sph_unit(const float v[3], float* az_deg, float* el_deg)
{
    float hypotxy = sqrtf(v[0]*v[0] + v[1]*v[1]);
    *az_deg = atan2f(v[1], v[0]) * (180.0f/SAF_PI);
    *el_deg = atan2f(v[2], hypotxy) * (180.0f/SAF_PI);
}

/* Invert row-major 3x3 matrix. Returns 1 on success, 0 if singular. */
static int invert3x3(const float m[9], float inv[9])
{
    float det = m[0]*(m[4]*m[8] - m[5]*m[7])
              - m[1]*(m[3]*m[8] - m[5]*m[6])
              + m[2]*(m[3]*m[7] - m[4]*m[6]);
    if (fabsf(det) < 1e-12f)
        return 0;
    float invDet = 1.0f/det;
    inv[0] =  (m[4]*m[8] - m[5]*m[7]) * invDet;
    inv[1] = -(m[1]*m[8] - m[2]*m[7]) * invDet;
    inv[2] =  (m[1]*m[5] - m[2]*m[4]) * invDet;
    inv[3] = -(m[3]*m[8] - m[5]*m[6]) * invDet;
    inv[4] =  (m[0]*m[8] - m[2]*m[6]) * invDet;
    inv[5] = -(m[0]*m[5] - m[2]*m[3]) * invDet;
    inv[6] =  (m[3]*m[7] - m[4]*m[6]) * invDet;
    inv[7] = -(m[0]*m[7] - m[1]*m[6]) * invDet;
    inv[8] =  (m[0]*m[4] - m[1]*m[3]) * invDet;
    return 1;
}

void vbap3D_oneSource(float src_az_deg,
                      float src_el_deg,
                      float* ls_dirs_deg,
                      int nLS,
                      int* faces,
                      int nFaces,
                      float* gains)
{
    int f, c;
    float p[3];
    sph2cart_unit(src_az_deg, src_el_deg, p);

    /* Search faces for the one giving the largest minimum gain. For an
     * interior point this picks the unique triangle with all g>=0. For
     * edge/vertex/coplanar cases it picks one of the (tied) candidates. */
    int bestFace = -1;
    float bestMin = -1e30f;
    float bestG[3] = {0.0f, 0.0f, 0.0f};

    for (f = 0; f < nFaces; f++) {
        int i = faces[3*f+0];
        int j = faces[3*f+1];
        int k = faces[3*f+2];
        /* L has rows = speaker unit vectors, so VBAP gives g = p * L^-1. */
        float L[9];
        sph2cart_unit(ls_dirs_deg[2*i+0], ls_dirs_deg[2*i+1], &L[0]);
        sph2cart_unit(ls_dirs_deg[2*j+0], ls_dirs_deg[2*j+1], &L[3]);
        sph2cart_unit(ls_dirs_deg[2*k+0], ls_dirs_deg[2*k+1], &L[6]);
        float Linv[9];
        if (!invert3x3(L, Linv))
            continue;
        float g[3];
        for (c = 0; c < 3; c++)
            g[c] = p[0]*Linv[0*3+c] + p[1]*Linv[1*3+c] + p[2]*Linv[2*3+c];
        float mn = g[0];
        if (g[1] < mn) mn = g[1];
        if (g[2] < mn) mn = g[2];
        if (mn > bestMin) {
            bestMin = mn;
            bestFace = f;
            bestG[0] = g[0]; bestG[1] = g[1]; bestG[2] = g[2];
        }
    }

    for (int n = 0; n < nLS; n++)
        gains[n] = 0.0f;

    if (bestFace >= 0) {
        /* Clip negatives (handles edge/coplanar cases) and unit-energy normalize. */
        if (bestG[0] < 0.0f) bestG[0] = 0.0f;
        if (bestG[1] < 0.0f) bestG[1] = 0.0f;
        if (bestG[2] < 0.0f) bestG[2] = 0.0f;
        float E = bestG[0]*bestG[0] + bestG[1]*bestG[1] + bestG[2]*bestG[2];
        if (E > 1e-20f) {
            float s = 1.0f/sqrtf(E);
            bestG[0] *= s; bestG[1] *= s; bestG[2] *= s;
        }
        gains[faces[3*bestFace+0]] = bestG[0];
        gains[faces[3*bestFace+1]] = bestG[1];
        gains[faces[3*bestFace+2]] = bestG[2];
    }
}

void getLoudspeakerDecoderMtx_AllRAD(float* ls_dirs_deg,
                                     int nLS,
                                     int* faces,
                                     int nFaces,
                                     int order,
                                     float* decMtx)
{
    int i, m, d;
    int nSH = (order+1)*(order+1);

    /* Generate Fibonacci-sphere virtual loudspeaker grid. AllRAD only
     * requires the grid to be approximately uniform for SH integration;
     * it does not need to be a strict t-design. N=512 gives <<1% SH
     * integration error at order 2. */
    const int N = 512;
    float* td_dirs_deg = (float*)malloc1d(N * 2 * sizeof(float));
    const float ga = SAF_PI * (3.0f - sqrtf(5.0f)); /* golden angle */
    for (i = 0; i < N; i++) {
        float z = 1.0f - 2.0f*((float)i + 0.5f)/(float)N;
        float r = sqrtf(1.0f - z*z);
        float theta = ga * (float)i;
        float v[3] = { r*cosf(theta), r*sinf(theta), z };
        cart2sph_unit(v, &td_dirs_deg[2*i+0], &td_dirs_deg[2*i+1]);
    }

    /* SH at virtual speakers: nSH x N row-major. Then scale by 1/sqrt(4π)
     * to match the SAF AllRAD formula (saf_hoa_internal.c:144). */
    float* Y_td = (float*)malloc1d(nSH * N * sizeof(float));
    getRSH(order, td_dirs_deg, N, Y_td);
    {
        float scale_Y = 1.0f / sqrtf(4.0f * SAF_PI);
        simple_sscal(nSH * N, scale_Y, Y_td);
    }

    /* VBAP gains for each virtual speaker onto the real loudspeakers:
     * G_td is N x nLS row-major. */
    float* G_td = (float*)calloc1d((size_t)N * nLS, sizeof(float));
    for (i = 0; i < N; i++) {
        vbap3D_oneSource(td_dirs_deg[2*i+0], td_dirs_deg[2*i+1],
                         ls_dirs_deg, nLS,
                         faces, nFaces,
                         &G_td[(size_t)i*nLS]);
    }

    /* decMtx[d][m] = (4π/N) * sum_i G_td[i][d] * Y_td[m][i]
     * (saf_hoa_internal.c:147-151). */
    float decScale = 4.0f * SAF_PI / (float)N;
    for (d = 0; d < nLS; d++) {
        for (m = 0; m < nSH; m++) {
            float sum = 0.0f;
            for (i = 0; i < N; i++)
                sum += G_td[(size_t)i*nLS + d] * Y_td[(size_t)m*N + i];
            decMtx[d*nSH + m] = decScale * sum;
        }
    }

    free(td_dirs_deg);
    free(Y_td);
    free(G_td);
}