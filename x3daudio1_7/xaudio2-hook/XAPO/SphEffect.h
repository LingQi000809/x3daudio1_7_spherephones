#pragma once

#include "HrtfXapoParam.h"
#include "ChannelMatrix.h"

#define NOMINMAX
#include <xapobase.h>
#include <array>

// ObjectConfiguration is not present here. In Bela it stores per-object position (azimuth, elevation, radius) 
// updated via OSC. In the XAPO, position arrives each Process() call through HrtfXapoParam, so there is nothing to store.

class LinearSmoothedValue
{
public:
    LinearSmoothedValue() = default;

    void reset(double sampleRate, double rampTimeSeconds);
    void setCurrentAndTargetValue(float newValue);
    void setTargetValue(float newValue);
    float getNextValue();
    float getCurrentValue() const;
    float getTargetValue() const { return target; }

private:
    int rampSamples = 1;
    int samplesRemaining = 0;
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
};

class Biquad
{
public:
    Biquad() = default;

    void setLowpass(double sampleRate, double fc, double Q = 0.70710678);
    void setHighpass(double sampleRate, double fc, double Q = 0.70710678);

    inline float process(float x)
    {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

// Single-object adaptation of Bela's SphericalHarmonicsEngine: no numSoundObjects/vector
// indexing (one object per XAPO instance), and radiusGain is passed in by the caller instead.
class SphericalHarmonicsEngine
{
public:
    static constexpr int kOrder      = 2;
    static constexpr int kAmbi       = (kOrder + 1) * (kOrder + 1); // 9
    static constexpr int kNumDrivers = 8;

    SphericalHarmonicsEngine();
    ~SphericalHarmonicsEngine();

    void prepare(double sampleRate, double rampTimeSeconds);

    float getNextGain(int driverIndex);
    void setObjectRSH(const float* Y_src, float radiusGain = 1.0f);
    const std::array<float, kAmbi>& getObjectRSH() const;
    void getTargetGains(float out[kNumDrivers]) const;

private:
    float D_[kNumDrivers][kAmbi] = {};        // ambisonic decoder matrix

    float targetEnergy_ = 1.0f;

    std::array<float, kAmbi> objectY = {};                  // spherical harmonic values
    std::array<LinearSmoothedValue, kNumDrivers> smoothedG; // smoothed gain values
};

// CXAPOParametersBase triple-buffers HrtfXapoParam so the game thread can write a new
// position while the audio thread reads the old one, lock-free.
class __declspec(uuid("{2A4E6F8B-1C3D-5A7E-9B2C-4D6F8A0C2E4B}")) SphXapoEffect : public CXAPOParametersBase
{
public:
    // --- Configuration --------------------------------------------------
    // To change hardware, update SphericalHarmonicsEngine::kNumDrivers and the positions
    // table in SphEffect.cpp — the static_assert there will catch count mismatches.
    static constexpr int kNumBassDrivers = 2;
    static constexpr int kNumDrivers     = SphericalHarmonicsEngine::kNumDrivers + kNumBassDrivers; // 10

    // 1-indexed for the interface; The index corresponds to the driver index, ordered in the same way as the kDriverPositionsDeg table in SphEffect.cpp.
    // [Left Top, Left Back, Left Front, Left Bottom, Right Top, Right Back, Right Front, Right Bottom]
    static constexpr int kOutputChannelToDriver[8] = {8, 9, 10, 7, 3, 4, 5, 2};
    static constexpr int kBassLeftOut  = 6;
    static constexpr int kBassRightOut = 1;

    // constants match kGainRampSeconds in Spherephones-Bela/render.cpp
    // Crossover frequency dividing the small-driver HPF from the bass LPF.
    static constexpr float kCrossoverHz = 200.0f;
    static constexpr double kGainRampSeconds = 0.02;  // 20 ms 
    // 0.0 = fully mono bass (both subs identical), 1.0 = current full L/R split.
    static constexpr float kBassPanAmount = 0.f;

    // Post-summation master gain — like a volume knob on the amp. Raise until the
    // spherephone matches headphone listening levels; can't cause per-source clipping.
    static constexpr float masterVolume = 2.7f;

    // Exponent applied to the X3DAudio VolumeMultiplier before SH decoding (must be > 0).
    // 1.0 = no change. Values below 1 compress dynamic range: quiet sounds (ambient, reverb)
    // get boosted relative to loud sounds (dialog), closing the gap between them.
    static constexpr float volumeCurveExponent = 1.f;

    // A/B toggles to isolate one stream: EnableSpatialSound = positioned 3D sound
    // (Process()/HRTF path); EnableNonSpatialSound = music/UI/indoor dialogue (buildNonSpatialMatrix).
    static constexpr bool EnableSpatialSound    = true;
    static constexpr bool EnableNonSpatialSound = true;

    // Hits only the game's reverb send bus (2-channel, separate from the 10-channel master) in
    // applyNonSpatialOutputMatrix's passthrough branch. The game sets its own wetness/falloff for
    // this bus, so this is a blunt knob to stop it drowning out the distance-attenuated dry signal.
    static constexpr float ReverbSendGain = 0.1f;

    // Overall level for the whole non-spatial stream (music/UI/reverb — stacks with ReverbSendGain
    // for reverb specifically), applied in applyNonSpatialOutputMatrix. Balance against SpatialGain.
    static constexpr float NonSpatialGain = 0.8f;

    // Overall level for spatial (positioned 3D) sound, on top of X3DAudio's VolumeMultiplier,
    // applied in computeGains() before the SH decode. Balance against NonSpatialGain.
    static constexpr float SpatialGain = 2.2f;

    // Calibration: right ear reads slightly louder than left. Multiplies every right-ear driver
    // channel (bass + 4 small drivers) on both the spatial and non-spatial paths.
    static constexpr float RightEarGain = 0.8f;

    // True if the given 1-indexed physical output channel feeds a right-ear
    // driver (kBassRightOut or one of the 4 right small-driver channels in
    // kOutputChannelToDriver, i.e. driver indices 4-7 per kDriverPositionsDeg).
    static constexpr bool IsRightChannel(int channel1Indexed)
    {
        if (channel1Indexed == kBassRightOut) return true;
        for (int d = 4; d < SphericalHarmonicsEngine::kNumDrivers; ++d)
            if (kOutputChannelToDriver[d] == channel1Indexed) return true;
        return false;
    }
    // --------------------------------------------------------------------

    explicit SphXapoEffect();

    // Builds the static output-mix matrix for non-spatialized (2D) audio such
    // as music and UI sounds.  Encodes stereo as two point sources at the ITU
    // standard ±30° front positions so all spherephone drivers participate.
    static ChannelMatrix buildNonSpatialMatrix(const ChannelMatrix& sourceMatrix);

    // Inherited via CXAPOParametersBase
    STDMETHOD(LockForProcess)(UINT32 inputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters, UINT32 outputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pOutputLockedParameters) override;
    STDMETHOD_(void, Process)(UINT32 inputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS* pInputProcessParameters, UINT32 outputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS* pOutputProcessParameters, BOOL isEnabled) override;

private:
    void computeGains(float azimuthRad, float elevationRad, float volume, INT64 sourceId, float posX, float posY, float posZ);

    static XAPO_REGISTRATION_PROPERTIES _regProps;

    SphericalHarmonicsEngine _engine;
    Biquad _biquadHP[SphericalHarmonicsEngine::kNumDrivers]; // high-pass for small drivers
    Biquad _biquadLP[kNumBassDrivers];                       // low-pass for bass channels

    // Last sample value actually written to pOutput per small driver, and the
    // peak absolute value since the previous log tick. Both read by the logging
    // throttle in computeGains; peak is reset to 0 after each log.
    float _lastOutput[SphericalHarmonicsEngine::kNumDrivers] = {};
    float _peakOutput[SphericalHarmonicsEngine::kNumDrivers] = {};

    WAVEFORMATEX _inputFormat;
    WAVEFORMATEX _outputFormat;
    HrtfXapoParam _params[3]; // ring buffer as CXAPOParametersBase requires

    // Per-instance, not static: a shared counter would starve out most concurrent sounds' log lines.
    int _logThrottle = 0;
};
