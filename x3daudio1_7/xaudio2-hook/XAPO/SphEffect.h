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

// Single-object adaptation of Bela's SphericalHarmonicsEngine.
// Differences from Bela: no numSoundObjects/vector indexing (one object per XAPO instance);
// objectConfigs and coordinates removed (see note above); 
// radiusGain passed into setObjectRSH by the caller.
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

private:
    float D_[kNumDrivers][kAmbi] = {};        // ambisonic decoder matrix

    float targetEnergy_ = 1.0f;

    std::array<float, kAmbi> objectY = {};                  // spherical harmonic values
    std::array<LinearSmoothedValue, kNumDrivers> smoothedG; // smoothed gain values
};

// XAudio2's XAPO system is built on COM, and every COM object needs a unique identifier so the runtime can find, create, and version it.
// CXAPOParametersBase is Microsoft's base class that handles the plumbing of getting parameters safely from the game thread to the audio thread. 
// It internally triple-buffers HrtfXapoParam so the game can write a new position while the audio thread is reading the old one without a lock. 
// SphXapoEffect inherits from it and adds the spherephone-specific audio math on top.
class __declspec(uuid("{2A4E6F8B-1C3D-5A7E-9B2C-4D6F8A0C2E4B}")) SphXapoEffect : public CXAPOParametersBase
{
public:
    // --- Configuration --------------------------------------------------
    // kNumBassDrivers: sub-bass drivers fed from the crossover LPF.
    // kNumDrivers:     total physical output channels = SphericalHarmonicsEngine::kNumDrivers + kNumBassDrivers.
    // To change hardware, update SphericalHarmonicsEngine::kNumDrivers and the
    // positions table in SphEffect.cpp — the static_assert there will catch count mismatches.
    static constexpr int kNumBassDrivers = 2;
    static constexpr int kNumDrivers     = SphericalHarmonicsEngine::kNumDrivers + kNumBassDrivers; // 10

    // constants match kGainRampSeconds in Spherephones-Bela/render.cpp
    // Crossover frequency dividing the small-driver HPF from the bass LPF.
    static constexpr float kCrossoverHz = 200.0f;
    static constexpr double kGainRampSeconds = 0.02;  // 20 ms 
    // 0.0 = fully mono bass (both subs identical), 1.0 = current full L/R split.
    static constexpr float kBassPanAmount = 0.f;
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
    void computeGains(float azimuthRad, float elevationRad, float volume);

    static XAPO_REGISTRATION_PROPERTIES _regProps;

    SphericalHarmonicsEngine _engine;
    Biquad _biquadHP[SphericalHarmonicsEngine::kNumDrivers]; // high-pass for small drivers
    Biquad _biquadLP[kNumBassDrivers];                       // low-pass for bass channels

    WAVEFORMATEX _inputFormat;
    WAVEFORMATEX _outputFormat;
    HrtfXapoParam _params[3]; // ring bufffer as CXAPOParametersBase requires
};
