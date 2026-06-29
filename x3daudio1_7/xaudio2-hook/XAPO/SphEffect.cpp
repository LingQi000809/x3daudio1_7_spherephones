#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include "SphEffect.h"
#include "sph_math.h"
#include "logger.h"

void LinearSmoothedValue::reset(double sampleRate, double rampTimeSeconds)
{
    double totalSamples = rampTimeSeconds * sampleRate;
    rampSamples = (totalSamples > 1.0) ? static_cast<int>(totalSamples) : 1;
    samplesRemaining = 0;
    current = target;
    step = 0.0f;
}

void LinearSmoothedValue::setCurrentAndTargetValue(float newValue)
{
    current = newValue;
    target = newValue;
    samplesRemaining = 0;
    step = 0.0f;
}

void LinearSmoothedValue::setTargetValue(float newValue)
{
    if (rampSamples <= 1) {
        current = newValue;
        target = newValue;
        samplesRemaining = 0;
        step = 0.0f;
        return;
    }

    target = newValue;
    samplesRemaining = rampSamples;
    step = (target - current) / static_cast<float>(rampSamples);
}

float LinearSmoothedValue::getNextValue()
{
    if (samplesRemaining > 0) {
        current += step;
        --samplesRemaining;
    }
    else {
        current = target;
    }
    return current;
}

float LinearSmoothedValue::getCurrentValue() const
{
    return current;
}

// RBJ Audio EQ Cookbook biquad coefficients. Direct Form I, normalized so a0 = 1.
void Biquad::setLowpass(double sampleRate, double fc, double Q)
{
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double a0 = 1.0 + alpha;
    double b0d = (1.0 - cosw0) * 0.5;
    double b1d =  1.0 - cosw0;
    double b2d = (1.0 - cosw0) * 0.5;
    double a1d = -2.0 * cosw0;
    double a2d =  1.0 - alpha;

    b0 = static_cast<float>(b0d / a0);
    b1 = static_cast<float>(b1d / a0);
    b2 = static_cast<float>(b2d / a0);
    a1 = static_cast<float>(a1d / a0);
    a2 = static_cast<float>(a2d / a0);
    reset();
}

void Biquad::setHighpass(double sampleRate, double fc, double Q)
{
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double a0 = 1.0 + alpha;
    double b0d =  (1.0 + cosw0) * 0.5;
    double b1d = -(1.0 + cosw0);
    double b2d =  (1.0 + cosw0) * 0.5;
    double a1d = -2.0 * cosw0;
    double a2d =  1.0 - alpha;

    b0 = static_cast<float>(b0d / a0);
    b1 = static_cast<float>(b1d / a0);
    b2 = static_cast<float>(b2d / a0);
    a1 = static_cast<float>(a1d / a0);
    a2 = static_cast<float>(a2d / a0);
    reset();
}

// =============================================================================
// Driver positions + decoder helpers (file-scope, used by SphericalHarmonicsEngine)
// =============================================================================

// ---------------------------------------------------------------------------
// Speaker positions for the spherephone small drivers (azimuth, elevation in degrees).
// Convention: +azimuth = left, -azimuth = right, +elevation = up.
//
// To change the small driver count or layout:
//   1. Set SphericalHarmonicsEngine::kNumDrivers in SphEffect.h
//   2. Add/remove {az, el} pairs here
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
    sizeof(kDriverPositionsDeg) / sizeof(kDriverPositionsDeg[0]) == SphericalHarmonicsEngine::kNumDrivers,
    "kDriverPositionsDeg must have one {az,el} entry per small driver — update this array when changing SphericalHarmonicsEngine::kNumDrivers");

// ---------------------------------------------------------------------------

static void buildDecoderMatrix(float D[SphericalHarmonicsEngine::kNumDrivers][SphericalHarmonicsEngine::kAmbi])
{
    float ls_dirs_deg[SphericalHarmonicsEngine::kNumDrivers * 2];
    for (int i = 0; i < SphericalHarmonicsEngine::kNumDrivers; ++i)
    {
        ls_dirs_deg[i * 2 + 0] = kDriverPositionsDeg[i][0];
        ls_dirs_deg[i * 2 + 1] = kDriverPositionsDeg[i][1];
    }

    // Convex-hull triangulation of the 8 drivers on the unit sphere. The L
    // and R caps are coplanar quads (4 drivers each at y=±sin(90°)·cos(0°))
    // so we hand-code a consistent diagonal split rather than rely on a
    // generic convex-hull algorithm. Driver indices match ls_dirs_deg above:
    //   0:L_top  1:L_back  2:L_front  3:L_bot
    //   4:R_top  5:R_back  6:R_front  7:R_bot
    // 12 triangles total: 2 per cap + 2 per side quad (top/bot/front/back).
    static int faces[12 * 3] = {
        0, 2, 3,   // L cap, top-front-bot
        0, 1, 3,   // L cap, top-back-bot
        4, 6, 7,   // R cap, top-front-bot
        4, 5, 7,   // R cap, top-back-bot
        0, 2, 6,   // top-front quad
        0, 6, 4,
        2, 3, 7,   // bot-front quad
        2, 7, 6,
        0, 1, 5,   // top-back quad
        0, 5, 4,
        1, 3, 7,   // bot-back quad
        1, 7, 5,
    };
    constexpr int kNumFaces = 12;

    float decMtx[SphericalHarmonicsEngine::kNumDrivers * SphericalHarmonicsEngine::kAmbi];
    // SAD (original):
    //   getLoudspeakerDecoderMtx(ls_dirs_deg, kNumDrivers, kOrder, decMtx);
    // AllRAD (Zotter & Frank, ported from SAF saf_hoa_internal.c::getAllRAD):
    getLoudspeakerDecoderMtx_AllRAD(ls_dirs_deg, SphericalHarmonicsEngine::kNumDrivers,
                                    faces, kNumFaces,
                                    SphericalHarmonicsEngine::kOrder, decMtx);

    for (int d = 0; d < SphericalHarmonicsEngine::kNumDrivers; ++d)
        for (int m = 0; m < SphericalHarmonicsEngine::kAmbi; ++m)
            D[d][m] = decMtx[d * SphericalHarmonicsEngine::kAmbi + m];
}

// =============================================================================
// SphericalHarmonicsEngine
// =============================================================================

SphericalHarmonicsEngine::SphericalHarmonicsEngine()
{
    for (int d = 0; d < kNumDrivers; ++d)
        smoothedG[d].setCurrentAndTargetValue(0.0f);
    for (int m = 0; m < kAmbi; ++m)
        objectY[m] = 0.0f;

    buildDecoderMatrix(D_);

    // Calibrate targetEnergy_ by sweeping the sphere and taking the peak
    // Σ g_d² produced by the active decoder. setObjectRSH then normalizes
    // every source direction to this energy, flattening any residual
    // loudness variation. AllRAD already reduces this variation a lot;
    // the post-norm cleans up what's left without hurting localization.
    constexpr float invSqrt4Pi = 1.0f / 3.544907701811032f;
    constexpr int kAzSteps = 36;  // every 10°
    constexpr int kElSteps = 19;  // every 10°, inclusive of poles
    float maxEnergy = 0.0f;
    for (int ia = 0; ia < kAzSteps; ++ia) {
        float az = -180.0f + (360.0f * ia) / kAzSteps;
        for (int ie = 0; ie < kElSteps; ++ie) {
            float el = -90.0f + (180.0f * ie) / (kElSteps - 1);
            float dir[2] = { az, el };
            float Y_src[kAmbi] = { 0 };
            getRSH(kOrder, dir, 1, Y_src);
            float E = 0.0f;
            for (int d = 0; d < kNumDrivers; ++d) {
                float gd = 0.0f;
                for (int m = 0; m < kAmbi; ++m)
                    gd += D_[d][m] * Y_src[m];
                gd *= invSqrt4Pi;
                E += gd * gd;
            }
            if (E > maxEnergy) maxEnergy = E;
        }
    }
    targetEnergy_ = (maxEnergy > 0.0f) ? maxEnergy : 1.0f;
}

SphericalHarmonicsEngine::~SphericalHarmonicsEngine() = default;

void SphericalHarmonicsEngine::prepare(double sampleRate, double rampTimeSeconds)
{
    for (int d = 0; d < kNumDrivers; ++d)
        smoothedG[d].reset(sampleRate, rampTimeSeconds);
}

float SphericalHarmonicsEngine::getNextGain(int driverIndex)
{
    return smoothedG[driverIndex].getNextValue();
}

void SphericalHarmonicsEngine::setObjectRSH(const float* Y_src, float radiusGain)
{
    if (Y_src == nullptr)
        return;
    //copy spherical harmonics to class member variable
    std::copy(Y_src, Y_src + kAmbi, objectY.begin());

    constexpr float invSqrt4Pi = 1.0f / 3.544907701811032f;

    float g[kNumDrivers];
    for (int d = 0; d < kNumDrivers; ++d) {
        float gd = 0.0f;
        gd += D_[d][0] * Y_src[0] + D_[d][1] * Y_src[1] + D_[d][2] * Y_src[2];
        gd += D_[d][3] * Y_src[3] + D_[d][4] * Y_src[4] + D_[d][5] * Y_src[5];
        gd += D_[d][6] * Y_src[6] + D_[d][7] * Y_src[7] + D_[d][8] * Y_src[8];
        g[d] = gd * invSqrt4Pi;
    }

    // energy-preserving normalization
    float energy = 0.0f;
    for (int d = 0; d < kNumDrivers; ++d)
        energy += g[d] * g[d];
    constexpr float kEnergyFloor = 1e-6f;
    float norm = std::sqrt(targetEnergy_ / std::max(energy, kEnergyFloor));

    for (int d = 0; d < kNumDrivers; ++d)
        smoothedG[d].setTargetValue(g[d] * norm * radiusGain);
}

const std::array<float, SphericalHarmonicsEngine::kAmbi>& SphericalHarmonicsEngine::getObjectRSH() const
{
    return objectY;
}

void SphericalHarmonicsEngine::getTargetGains(float out[kNumDrivers]) const
{
    for (int d = 0; d < kNumDrivers; ++d)
        out[d] = smoothedG[d].getTargetValue();
}

// =============================================================================
// SphXapoEffect
// =============================================================================


// Computes per-driver SH gains for a single point source at (azDeg, elDeg)
// in the physics convention (+az = left).  Cost is a small one-off malloc
// inside getRSH — only called at startup for non-spatialized matrix setup.
static void computeStaticGainsAtDeg(float azDeg, float elDeg,
                                     float gains[SphericalHarmonicsEngine::kNumDrivers])
{
    constexpr float invSqrt4Pi = 1.0f / 3.544907701811032f;

    float D[SphericalHarmonicsEngine::kNumDrivers][SphericalHarmonicsEngine::kAmbi];
    buildDecoderMatrix(D);

    float src_dir_deg[2] = {azDeg, elDeg};
    float Y_src[SphericalHarmonicsEngine::kAmbi] = {};
    getRSH(SphericalHarmonicsEngine::kOrder, src_dir_deg, 1, Y_src);

    for (int d = 0; d < SphericalHarmonicsEngine::kNumDrivers; ++d) {
        float g = 0.0f;
        g += D[d][0] * Y_src[0] + D[d][1] * Y_src[1] + D[d][2] * Y_src[2];
        g += D[d][3] * Y_src[3] + D[d][4] * Y_src[4] + D[d][5] * Y_src[5];
        g += D[d][6] * Y_src[6] + D[d][7] * Y_src[7] + D[d][8] * Y_src[8];
        gains[d] = g * invSqrt4Pi;
    }
}

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

// Music and UI sounds (footstep menu clicks, background music) bypass X3DAudio entirely — they're just stereo.
ChannelMatrix SphXapoEffect::buildNonSpatialMatrix(const ChannelMatrix& sourceMatrix)
{
    // Encode stereo as two ITU-standard point sources: FL at +30° left, FR at −30° right.
    // The game's clientMatrix already carries per-source FL/FR mix weights, so we
    // apply them as coefficients into the SH decode rather than replacing them.
    float flGains[SphericalHarmonicsEngine::kNumDrivers], frGains[SphericalHarmonicsEngine::kNumDrivers];
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

        for (int d = 0; d < SphericalHarmonicsEngine::kNumDrivers; ++d)
            matrix.SetValue(src, d, wFL * flGains[d] + wFR * frGains[d]);

        // Bass channels: sum of left and right small driver groups.
        // Drivers 0-3 = left side, drivers 4-7 = right side.
        float bassL = 0.0f, bassR = 0.0f;
        for (int d = 0; d < SphericalHarmonicsEngine::kNumDrivers / 2; ++d) {
            bassL += wFL * flGains[d]                                          + wFR * frGains[d];
            bassR += wFL * flGains[d + SphericalHarmonicsEngine::kNumDrivers/2] + wFR * frGains[d + SphericalHarmonicsEngine::kNumDrivers/2];
        }
        matrix.SetValue(src, SphericalHarmonicsEngine::kNumDrivers,     bassL);
        matrix.SetValue(src, SphericalHarmonicsEngine::kNumDrivers + 1, bassR);
    }
    return matrix;
}

// Passes signals below limiterKnee through unchanged; soft-curves anything
// above it to ±1 using tanh only in that small headroom region.
// Unlike a full tanh, most of the waveform stays linear so loudness and
// clarity are preserved — only genuine peaks get rounded.
static constexpr float limiterKnee = 0.9f;
static inline float limitOutput(float x)
{
    float a = std::abs(x);
    if (a <= limiterKnee) return x;
    float over     = a - limiterKnee;
    float headroom = 1.0f - limiterKnee;
    return std::copysign(limiterKnee + headroom * std::tanh(over / headroom), x);
}

SphXapoEffect::SphXapoEffect()
    : CXAPOParametersBase(&_regProps, reinterpret_cast<BYTE*>(_params), sizeof(HrtfXapoParam), FALSE)
{
    _params[0] = {};
    _params[1] = {};
    _params[2] = {};
    // _engine is fully initialized by SphericalHarmonicsEngine::SphericalHarmonicsEngine()
}

// called once by XAudio2 when the voice graph is built, before audio starts flowing. 
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

        _engine.prepare(_inputFormat.nSamplesPerSec, kGainRampSeconds);

        // Initialize crossover filters
        for (int d = 0; d < SphericalHarmonicsEngine::kNumDrivers; ++d)
            _biquadHP[d].setHighpass(_inputFormat.nSamplesPerSec, kCrossoverHz);
        for (int d = 0; d < kNumBassDrivers; ++d)
            _biquadLP[d].setLowpass(_inputFormat.nSamplesPerSec, kCrossoverHz);
    }
    return hr;
}

// bridge the X3DAudio coordinate system (azimuth in radians, +right) to the physics convention the SH math expects (+left)
void SphXapoEffect::computeGains(float azimuthRad, float elevationRad, float volume)
{
    // Negate azimuth: X3DAudio gives +az=right, getRSH expects +az=left (physics convention).
    float src_dir_deg[2] = {-azimuthRad * (180.0f / 3.14159265358979323846f),
                              elevationRad * (180.0f / 3.14159265358979323846f)};
    float Y_src[SphericalHarmonicsEngine::kAmbi] = {};
    getRSH(SphericalHarmonicsEngine::kOrder, src_dir_deg, 1, Y_src);
    float compressedVolume = std::pow(std::max(0.0f, volume), volumeCurveExponent);
    _engine.setObjectRSH(Y_src, compressedVolume * amplification);

    static int throttle = 0;
    if (++throttle >= 100)
    {
        throttle = 0;
        logger::logSpatialGains(src_dir_deg[0], src_dir_deg[1], compressedVolume * amplification, _lastOutput, _peakOutput, SphericalHarmonicsEngine::kNumDrivers);
        std::fill(std::begin(_peakOutput), std::end(_peakOutput), 0.0f);
    }
}

// the audio hot path, called by XAudio2 for every buffer (~every 10ms).
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
        for (UINT32 n = 0; n < frameCount; ++n)
        {
            // Downmix stereo input to mono (single object per XAPO instance).
            float input = (inCh == 1)
                ? pInput[n]
                : (pInput[n * 2] + pInput[n * 2 + 1]) * 0.5f;

            float driverMix[SphericalHarmonicsEngine::kNumDrivers];
            for (int channel = 0; channel < SphericalHarmonicsEngine::kNumDrivers; ++channel)
                driverMix[channel] = input * _engine.getNextGain(channel);

            // bass low-passed
            float bassL = driverMix[0] + driverMix[1] + driverMix[2] + driverMix[3];
            float bassR = driverMix[4] + driverMix[5] + driverMix[6] + driverMix[7];
            float bassMono = 0.5f * (bassL + bassR);
            float bassDiff = 0.5f * (bassL - bassR);
            float bassLOut = bassMono + kBassPanAmount * bassDiff;
            float bassROut = bassMono - kBassPanAmount * bassDiff;
            // Bass drivers occupy output channels kBassLeftOut and kBassRightOut.
            // Equivalent to Bela's audioWrite(context, n, kBassLeftOut/kBassRightOut, ...).
            // pOutput[n * kNumDrivers + SphericalHarmonicsEngine::kNumDrivers]     = _biquadLP[0].process(bassLOut);
            // pOutput[n * kNumDrivers + SphericalHarmonicsEngine::kNumDrivers + 1] = _biquadLP[1].process(bassROut);
            pOutput[n * kNumDrivers + kBassLeftOut -1]  = limitOutput(_biquadLP[0].process(bassLOut));
            pOutput[n * kNumDrivers + kBassRightOut -1] = limitOutput(_biquadLP[1].process(bassROut));

            // Small drivers occupy output channels listed in kOutputChannelToDriver, matching kDriverPositionsDeg order.
            // Equivalent to Bela's audioWrite(context, n, kOutputChannelToDriver[channel], ...).
            for (int channel = 0; channel < SphericalHarmonicsEngine::kNumDrivers; ++channel) {
                // pOutput[n * kNumDrivers + channel] = _biquadHP[channel].process(driverMix[channel]);
                int driverIndex = kOutputChannelToDriver[channel];
                float s = limitOutput(_biquadHP[channel].process(driverMix[channel]));
                pOutput[n * kNumDrivers + driverIndex - 1] = s;
                _lastOutput[channel] = s;
                if (std::abs(s) > _peakOutput[channel]) _peakOutput[channel] = std::abs(s);
            }
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
