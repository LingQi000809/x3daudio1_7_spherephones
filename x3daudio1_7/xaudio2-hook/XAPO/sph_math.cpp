#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sph_math.h"

#define SAF_PI  ( 3.14159265358979323846264338327950288f )
#define SAF_PId ( 3.14159265358979323846264338327950288 )
#define ORDER2NSH(order) ((order+1)*(order+1))
#define SQRT4PI ( 3.544907701811032f )

void* malloc1d(size_t dim1_data_size)
{
    return malloc(dim1_data_size);
}

void* calloc1d(size_t dim1, size_t data_size)
{
    return calloc(dim1, data_size);
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

static const long double factorials_21[21] = {
    1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0, 3628800.0, 39916800.0,
    479001600.0, 6.2270208e9, 8.71782891e10, 1.307674368000000e12, 2.092278988800000e13,
    3.556874280960000e14, 6.402373705728000e15, 1.216451004088320e17, 2.432902008176640e18
};

static long double factorial(int n)
{
    if (n < 21) return factorials_21[n];
    long double ff = 1.0;
    for (int i = 1; i <= n; i++) ff *= (long double)i;
    return ff;
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

float L2_norm3(float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void cart2sph(float* cart, int nDirs, int anglesInDegreesFLAG, float* sph)
{
    int i;
    float hypotxy;

    for (i = 0; i < nDirs; i++) {
        hypotxy     = sqrtf(cart[i * 3] * cart[i * 3] + cart[i * 3 + 1] * cart[i * 3 + 1]);
        sph[i * 3]     = atan2f(cart[i * 3 + 1], cart[i * 3]);
        sph[i * 3 + 1] = atan2f(cart[i * 3 + 2], hypotxy);
        sph[i * 3 + 2] = L2_norm3(&cart[i * 3]);
    }

    /* Return in degrees instead... */
    if (anglesInDegreesFLAG) {
        for (i = 0; i < nDirs; i++) {
            sph[i * 3]     *= (180.0f / SAF_PI);
            sph[i * 3 + 1] *= (180.0f / SAF_PI);
        }
    }
}
