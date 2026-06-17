#pragma once

#include "HrtfXapoParam.h"
#include "ChannelMatrix.h"

#define NOMINMAX
#include <xapobase.h>

// Minimal linear ramp — matches the JUCE LinearSmoothedValue interface used in pluginProcessor.cpp.
struct SmoothedGain {
    float current   = 0.0f;
    float target    = 0.0f;
    float step      = 0.0f;
    int   remaining = 0;

    void reset(float v) { current = target = v; remaining = 0; }
    void setTargetValue(float v, int rampSamples) {
        target    = v;
        remaining = rampSamples;
        step      = (target - current) / (rampSamples > 0 ? rampSamples : 1);
    }
    float getNextValue() {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
};

class __declspec(uuid("{2A4E6F8B-1C3D-5A7E-9B2C-4D6F8A0C2E4B}")) SphXapoEffect : public CXAPOParametersBase
{
public:
    // --- Configuration --------------------------------------------------
    // Change kNumDrivers here (8 or 10) when switching headphone hardware.
    // Also add the matching speaker positions in SphEffect.cpp — the
    // static_assert there will give a compile error if counts don't match.
    static constexpr int kOrder      = 2;
    static constexpr int kAmbi       = (kOrder + 1) * (kOrder + 1); // 9 for order 2
    static constexpr int kNumDrivers = 8;
    // --------------------------------------------------------------------

    SphXapoEffect();

    // Builds the static output-mix matrix for non-spatialized (2D) audio such
    // as music and UI sounds.  Encodes stereo as two point sources at the ITU
    // standard ±30° front positions so all spherephone drivers participate.
    static ChannelMatrix buildNonSpatialMatrix(const ChannelMatrix& sourceMatrix);

    STDMETHOD(LockForProcess)(UINT32 inputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters, UINT32 outputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pOutputLockedParameters) override;
    STDMETHOD_(void, Process)(UINT32 inputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS* pInputProcessParameters, UINT32 outputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS* pOutputProcessParameters, BOOL isEnabled) override;

private:
    void computeGains(float azimuthRad, float elevationRad, float volume);

    static XAPO_REGISTRATION_PROPERTIES _regProps;

    float        D_[kNumDrivers][kAmbi];
    SmoothedGain _smoothedGains[kNumDrivers];
    int          _rampSamples = 0;

    WAVEFORMATEX _inputFormat;
    WAVEFORMATEX _outputFormat;
    HrtfXapoParam _params[3];
};
