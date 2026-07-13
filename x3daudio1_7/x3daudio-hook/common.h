#pragma once

#include "math/math_types.h"

#include <X3DAudio.h>
#include <crtdbg.h>
#include "graph/ISpatializedDataExtractor.h"
#include "logger.h"

// Divides the distance fed into the volume curve (not spatialData.distance/azimuth/elevation)
// so the curve thinks the emitter is closer — avoids multiplying VolumeMultiplier afterward,
// which is where headroom runs out and clips. 1.0 = no change.
constexpr float volumeDistanceDivider = 1.5f;

// Compresses distance beyond a knee (a fraction of CurveDistanceScaler, so it scales per-sound)
// so far sounds stay audible without exceeding what a knee-distance sound already produces —
// warps distance rather than gain, same trick as volumeDistanceDivider, to avoid clipping.
constexpr float farKneeFraction = 0.5f;      // fraction of CurveDistanceScaler where compression begins
constexpr float farCompressionRatio = 0.4f;  // 1.0 = no compression, lower = more aggressive beyond the knee

inline float compressFarDistance(float distance, float distanceScaler)
{
	const float knee = farKneeFraction * distanceScaler;
	return distance <= knee ? distance : knee + (distance - knee) * farCompressionRatio;
}

inline math::vector3 to_vector3(const X3DAUDIO_VECTOR & vector)
{
	return math::vector3{ vector.x, vector.y, vector.z };
}

inline float sample_curve(const X3DAUDIO_DISTANCE_CURVE * pCurve, const float distanceScaler, const float distance)
{
	const float normalized_distance = distance / distanceScaler;

	if (pCurve == nullptr)
	{
		const float clamped_distance = std::max(1.0f, normalized_distance);
		return 1.0f / clamped_distance;
	}

	if (pCurve->PointCount < 2)
	{
		logger::logRelease("Warning: number of points in the volume curve is less than two");
		return 0;
	}

	const std::size_t pointCount = pCurve->PointCount;
	const auto points = pCurve->pPoints;

	if (normalized_distance >= 1.0f)
		return points[pointCount - 1].DSPSetting;

	const auto greater_point = std::upper_bound(pCurve->pPoints, pCurve->pPoints + pCurve->PointCount, normalized_distance, [=](const auto & dist, const auto & point) { return dist < point.Distance; });
	if (greater_point != pCurve->pPoints + pCurve->PointCount)
	{
		if (greater_point > &pCurve->pPoints[0])
		{
			const auto * less_point = greater_point - 1;

			const float fraction = (normalized_distance - less_point->Distance) / (greater_point->Distance - less_point->Distance);
			return less_point->DSPSetting + (greater_point->DSPSetting - less_point->DSPSetting) * fraction;
		}
		else
		{
			return greater_point->DSPSetting;
		}
	}
	else
	{
		return (greater_point - 1)->DSPSetting;
	}
}

inline void CalculateDelay(const X3DAUDIO_LISTENER & listener, const X3DAUDIO_EMITTER & emitter, X3DAUDIO_DSP_SETTINGS & dspSettings)
{
	dspSettings.pDelayTimes[0] = dspSettings.pDelayTimes[1] = 0.0f;
}


inline void CalculateDopplerFactor(float speedOfSound, const X3DAUDIO_LISTENER & listener, const X3DAUDIO_EMITTER & emitter, X3DAUDIO_DSP_SETTINGS & dspSettings)
{
	const auto emitterToListener = to_vector3(listener.Position) - to_vector3(emitter.Position);
	const auto emitterToListenerDistance = math::length(emitterToListener);

	if (emitterToListenerDistance > 0.00000011920929)
	{
		const float maxVelocityComponent = std::nextafter(speedOfSound, 0.0f);

		const float listenerVelocityComponent = math::dot(emitterToListener, to_vector3(listener.Velocity)) / emitterToListenerDistance;
		const float emitterVelocityComponent = math::dot(emitterToListener, to_vector3(emitter.Velocity)) / emitterToListenerDistance;
		const float scaledListenerVelocityComponent = std::min(emitter.DopplerScaler * listenerVelocityComponent, maxVelocityComponent);
		const float scaledEmitterVeclocityComponent = std::min(emitter.DopplerScaler * emitterVelocityComponent, maxVelocityComponent);

		dspSettings.ListenerVelocityComponent = listenerVelocityComponent;
		dspSettings.EmitterVelocityComponent = emitterVelocityComponent;
		dspSettings.DopplerFactor = (speedOfSound - scaledListenerVelocityComponent) / (speedOfSound - scaledEmitterVeclocityComponent);
	}
	else
	{
		dspSettings.DopplerFactor = 1.0f;
		dspSettings.ListenerVelocityComponent = 0.0f;
		dspSettings.EmitterVelocityComponent = 0.0f;
	}
}


inline void CalculateReverb(const X3DAUDIO_LISTENER & listener, const X3DAUDIO_EMITTER & emitter, X3DAUDIO_DSP_SETTINGS & dspSettings)
{
	const auto emitterToListener = to_vector3(listener.Position) - to_vector3(emitter.Position);
	const auto emitterToListenerDistance = math::length(emitterToListener);
	dspSettings.ReverbLevel = sample_curve(emitter.pReverbCurve, emitter.CurveDistanceScaler, emitterToListenerDistance);
}


inline SpatialData CommonX3DAudioCalculate(float speedOfSound, const X3DAUDIO_LISTENER * pListener, const X3DAUDIO_EMITTER * pEmitter, UINT32 Flags, X3DAUDIO_DSP_SETTINGS * pDSPSettings)
{
	if (Flags & X3DAUDIO_CALCULATE_REVERB)
	{
		CalculateReverb(*pListener, *pEmitter, *pDSPSettings);
	}

	if (Flags & X3DAUDIO_CALCULATE_DELAY)
	{
		_ASSERT(pDSPSettings->pDelayTimes != nullptr);
		CalculateDelay(*pListener, *pEmitter, *pDSPSettings);
	}

	if (Flags & X3DAUDIO_CALCULATE_DOPPLER)
	{
		CalculateDopplerFactor(speedOfSound, *pListener, *pEmitter, *pDSPSettings);
	}

	// changing left-hand to ortodox right-hand
	const math::vector3 listener_position(pListener->Position.x, pListener->Position.y, pListener->Position.z);
	const math::vector3 emitter_position(pEmitter->Position.x, pEmitter->Position.y, pEmitter->Position.z);

	const auto listener_to_emitter = emitter_position - listener_position;

	const math::vector3 listener_frame_front(pListener->OrientFront.x, pListener->OrientFront.y, pListener->OrientFront.z);
	const math::vector3 listener_frame_up(pListener->OrientTop.x, pListener->OrientTop.y, pListener->OrientTop.z);
	const auto listener_frame_right = math::cross(listener_frame_up, listener_frame_front);

	const math::matrix3 listener_basis(listener_frame_right, listener_frame_up, listener_frame_front);
	const auto world_to_listener_matrix = (listener_basis);

	SpatialData spatialData;

	const auto relative_position = world_to_listener_matrix * listener_to_emitter;
	const auto distance = math::length(listener_to_emitter);

	const float compressedDistance = compressFarDistance(distance, pEmitter->CurveDistanceScaler);
	const float dividedDistance = compressedDistance / volumeDistanceDivider;

	spatialData.present = true;
	spatialData.volume_multiplier = sample_curve(pEmitter->pVolumeCurve, pEmitter->CurveDistanceScaler, dividedDistance);
	spatialData.azimuth = distance > std::numeric_limits<float>::epsilon() ? std::atan2(relative_position[0], relative_position[2]) : 0.0f;
	spatialData.elevation = distance > std::numeric_limits<float>::epsilon() ? std::asin(math::normalize(relative_position)[1]) : 0.0f;
	spatialData.distance = distance;
	// Diagnostic-only; no pContext exists on this X3DAudio 1.7 emitter, so position is the closest thing to an id here.
	spatialData.emitterPosX = pEmitter->Position.x;
	spatialData.emitterPosY = pEmitter->Position.y;
	spatialData.emitterPosZ = pEmitter->Position.z;

	// Throttled instrumentation for tuning volumeDistanceDivider by ear against
	// real values. rawVolumeMultiplier is what volume_multiplier would be
	// WITHOUT the divider (recomputed here only for comparison, not used for
	// audio) — if it's already 1.0 too, the divider had nothing to do because
	// this emitter's curve was already fully saturated at the true distance.
	static int distanceLogThrottle = 0;
	if (++distanceLogThrottle >= 100)
	{
		distanceLogThrottle = 0;
		const float rawVolumeMultiplier = sample_curve(pEmitter->pVolumeCurve, pEmitter->CurveDistanceScaler, distance);
		logger::logRelease(L"[x3daudio-distance] pos=(", pEmitter->Position.x, L",", pEmitter->Position.y, L",", pEmitter->Position.z, L")",
			L" distance=", distance, L" compressedDistance=", compressedDistance, L" dividedDistance=", dividedDistance,
			L" azimuthDeg=", spatialData.azimuth * (180.0f / 3.14159265358979323846f),
			L" elevationDeg=", spatialData.elevation * (180.0f / 3.14159265358979323846f),
			L" pEmitter->CurveDistanceScaler=", pEmitter->CurveDistanceScaler, L" pEmitter->pVolumeCurve=", pEmitter->pVolumeCurve,
			L" rawVolumeMultiplier=", rawVolumeMultiplier, L" spatialData.volume_multiplier=", spatialData.volume_multiplier);
	}

	return spatialData;
}
