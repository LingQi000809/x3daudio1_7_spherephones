#include "stdafx.h"
#include "SphEffect.h"
#include "sph_math.h"
#include "logger.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Speaker positions for the spherephone (azimuth, elevation in degrees).
// Convention: +azimuth = left, -azimuth = right, +elevation = up.
//
// To switch to 10 drivers:
//   1. Set kNumDrivers = 10 in SphEffect.h
//   2. Add the two new {az, el} pairs here
//   The static_assert below will catch any count mismatch at compile time.
// ---------------------------------------------------------------------------
static constexpr float kDriverPositionsDeg[][2] = {
    { 90.0f,  45.0f},  // Driver 0: Left Top
    {135.0f,   0.0f},  // Driver 1: Left Back
    { 45.0f,   0.0f},  // Driver 2: Left Front
    { 90.0f, -45.0f},  // Driver 3: Left Bottom
    {-90.0f,  45.0f},  // Driver 4: Right Top
    {-135.0f,  0.0f},  // Driver 5: Right Back
    {-45.0f,   0.0f},  // Driver 6: Right Front
    {-90.0f, -45.0f},  // Driver 7: Right Bottom
};
static_assert(
    sizeof(kDriverPositionsDeg) / sizeof(kDriverPositionsDeg[0]) == SphXapoEffect::kNumDrivers,
    "kDriverPositionsDeg must have one {az,el} entry per driver — update this array when changing kNumDrivers");

// ---------------------------------------------------------------------------

XAPO_REGISTRATION_PROPERTIES SphXapoEffect::_regProps = {
    __uuidof(SphXapoEffect),
    L"Spherical Harmonics Effect",
    L"Copyright (C)2025",
    1,
    0,
    XAPO_FLAG_FRAMERATE_MUST_MATCH
    | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH
    | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH,
    1, 1, 1, 1
};

static void buildDecoderMatrix(float D[SphXapoEffect::kNumDrivers][SphXapoEffect::kAmbi])
{
    float ls_dirs_deg[SphXapoEffect::kNumDrivers * 2];
    for (int i = 0; i < SphXapoEffect::kNumDrivers; ++i)
    {
        ls_dirs_deg[i * 2 + 0] = kDriverPositionsDeg[i][0];
        ls_dirs_deg[i * 2 + 1] = kDriverPositionsDeg[i][1];
    }

    float decMtx[SphXapoEffect::kNumDrivers * SphXapoEffect::kAmbi];
    getLoudspeakerDecoderMtx(ls_dirs_deg, SphXapoEffect::kNumDrivers, SphXapoEffect::kOrder, decMtx);

    for (int d = 0; d < SphXapoEffect::kNumDrivers; ++d)
        for (int m = 0; m < SphXapoEffect::kAmbi; ++m)
            D[d][m] = decMtx[d * SphXapoEffect::kAmbi + m];
}

// Computes per-driver SH gains for a single point source at (azDeg, elDeg)
// in the physics convention (+az = left).  Cost is a small one-off malloc
// inside getRSH — only called at startup for non-spatialized matrix setup.
static void computeStaticGainsAtDeg(float azDeg, float elDeg,
                                     float gains[SphXapoEffect::kNumDrivers])
{
    constexpr float invSqrt4Pi = 1.0f / 3.544907701811032f;

    float D[SphXapoEffect::kNumDrivers][SphXapoEffect::kAmbi];
    buildDecoderMatrix(D);

    float src_dir_deg[2] = {azDeg, elDeg};
    float Y_src[SphXapoEffect::kAmbi] = {};
    getRSH(SphXapoEffect::kOrder, src_dir_deg, 1, Y_src);

    for (int d = 0; d < SphXapoEffect::kNumDrivers; ++d) {
        float g = 0.0f;
        g += D[d][0] * Y_src[0] + D[d][1] * Y_src[1] + D[d][2] * Y_src[2];
        g += D[d][3] * Y_src[3] + D[d][4] * Y_src[4] + D[d][5] * Y_src[5];
        g += D[d][6] * Y_src[6] + D[d][7] * Y_src[7] + D[d][8] * Y_src[8];
        gains[d] = g * invSqrt4Pi;
    }
}

ChannelMatrix SphXapoEffect::buildNonSpatialMatrix(const ChannelMatrix& sourceMatrix)
{
    // Encode stereo as two ITU-standard point sources: FL at +30° left, FR at −30° right.
    // The game's clientMatrix already carries per-source FL/FR mix weights, so we
    // apply them as coefficients into the SH decode rather than replacing them.
    float flGains[kNumDrivers], frGains[kNumDrivers];
    computeStaticGainsAtDeg(+30.0f, 0.0f, flGains); // +30° = left in physics convention
    computeStaticGainsAtDeg(-30.0f, 0.0f, frGains); // −30° = right

    const UINT32 srcCount  = sourceMatrix.GetSourceCount();
    const UINT32 destCount = sourceMatrix.GetDestinationCount();
    ChannelMatrix matrix(srcCount, kNumDrivers);

    for (UINT32 src = 0; src < srcCount; ++src)
    {
        // dest ch 0 = FL channel weight, dest ch 1 = FR channel weight.
        // A mono source typically has GetValue(0,0)=1 and GetValue(0,1)=1,
        // which sums flGains+frGains and produces a centred front image.
        const float wFL = (destCount >= 1) ? sourceMatrix.GetValue(src, 0) : 0.0f;
        const float wFR = (destCount >= 2) ? sourceMatrix.GetValue(src, 1) : wFL;

        for (int d = 0; d < kNumDrivers; ++d)
            matrix.SetValue(src, d, wFL * flGains[d] + wFR * frGains[d]);
    }
    return matrix;
}

SphXapoEffect::SphXapoEffect()
    : CXAPOParametersBase(&_regProps, reinterpret_cast<BYTE*>(_params), sizeof(HrtfXapoParam), FALSE)
{
    _params[0] = {};
    _params[1] = {};
    _params[2] = {};
    for (int d = 0; d < kNumDrivers; ++d) _smoothedGains[d].reset(0.0f);
    buildDecoderMatrix(D_);
}

HRESULT SphXapoEffect::LockForProcess(
    UINT32 InputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters,
    UINT32 OutputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pOutputLockedParameters)
{
    _ASSERT(pInputLockedParameters[0].pFormat->nChannels == 1 ||
            pInputLockedParameters[0].pFormat->nChannels == 2);
    _ASSERT(pOutputLockedParameters[0].pFormat->nChannels == kNumDrivers);

    const HRESULT hr = CXAPOParametersBase::LockForProcess(
        InputLockedParameterCount, pInputLockedParameters,
        OutputLockedParameterCount, pOutputLockedParameters);

    if (SUCCEEDED(hr))
    {
        memcpy(&_inputFormat,  pInputLockedParameters[0].pFormat,  sizeof(_inputFormat));
        memcpy(&_outputFormat, pOutputLockedParameters[0].pFormat, sizeof(_outputFormat));

        constexpr float kGainRampSeconds = 0.05f;  // 50 ms — match kGainRampSeconds in pluginProcessor.cpp
        _rampSamples = static_cast<int>(_inputFormat.nSamplesPerSec * kGainRampSeconds);
    }
    return hr;
}

void SphXapoEffect::computeGains(float azimuthRad, float elevationRad, float volume)
{
    constexpr float invSqrt4Pi = 1.0f / 3.544907701811032f;

    // Negate azimuth: X3DAudio gives +az=right, getRSH expects +az=left (physics convention).
    float src_dir_deg[2] = {-azimuthRad * (180.0f / 3.14159265358979323846f),
                              elevationRad * (180.0f / 3.14159265358979323846f)};
    float Y_src[kAmbi] = {};
    getRSH(kOrder, src_dir_deg, 1, Y_src);

    for (int d = 0; d < kNumDrivers; ++d) {
        float g = 0.0f;
        g += D_[d][0] * Y_src[0] + D_[d][1] * Y_src[1] + D_[d][2] * Y_src[2];
        g += D_[d][3] * Y_src[3] + D_[d][4] * Y_src[4] + D_[d][5] * Y_src[5];
        g += D_[d][6] * Y_src[6] + D_[d][7] * Y_src[7] + D_[d][8] * Y_src[8];
        _smoothedGains[d].setTargetValue(g * invSqrt4Pi * volume, _rampSamples);
    }
}

void SphXapoEffect::Process(
    UINT32 InputProcessParameterCount,
    const XAPO_PROCESS_BUFFER_PARAMETERS* pInputProcessParameters,
    UINT32 OutputProcessParameterCount,
    XAPO_PROCESS_BUFFER_PARAMETERS* pOutputProcessParameters,
    BOOL IsEnabled)
{
    _ASSERT(IsLocked());
    _ASSERT(InputProcessParameterCount == 1);
    _ASSERT(OutputProcessParameterCount == 1);

    const auto params = reinterpret_cast<const HrtfXapoParam*>(BeginProcess());

    const bool   isInputValid = pInputProcessParameters[0].BufferFlags == XAPO_BUFFER_VALID;
    const UINT32 frameCount   = pInputProcessParameters[0].ValidFrameCount;
    const auto   pInput       = reinterpret_cast<const float*>(pInputProcessParameters[0].pBuffer);
    auto         pOutput      = reinterpret_cast<float*>(pOutputProcessParameters[0].pBuffer);

    if (IsEnabled && isInputValid)
    {
        computeGains(params->Azimuth, params->Elevation, params->VolumeMultiplier);

        const UINT32 inCh = _inputFormat.nChannels;
        for (UINT32 i = 0; i < frameCount; ++i)
        {
            const float mono = (inCh == 1)
                ? pInput[i]
                : (pInput[i * 2] + pInput[i * 2 + 1]) * 0.5f;

            for (int d = 0; d < kNumDrivers; ++d)
                pOutput[i * kNumDrivers + d] = mono * _smoothedGains[d].getNextValue();
        }

        pOutputProcessParameters[0].BufferFlags     = XAPO_BUFFER_VALID;
        pOutputProcessParameters[0].ValidFrameCount = frameCount;
    }
    else
    {
        pOutputProcessParameters[0].BufferFlags = XAPO_BUFFER_SILENT;
    }

    EndProcess();
}
