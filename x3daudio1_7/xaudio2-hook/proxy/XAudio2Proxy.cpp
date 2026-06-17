#include "stdafx.h"
#include "XAudio2Proxy.h"
#include "graph/AudioGraphMapper.h"
#include "XAPO/HrtfEffect.h"
#include "XAPO/SphEffect.h"

#include "logger.h"
#include "util.h"

XAudio2Proxy::XAudio2Proxy()
{
	logger::logDebug("Constructing XAudio2Proxy");
}

HRESULT XAudio2Proxy::CreateInstance(IUnknown * original, REFIID riid, void ** ppvObject, ISpatializedDataExtractor & spatializedDataExtractor)
{
	auto self = new ATL::CComObjectNoLock<XAudio2Proxy>;

	self->SetVoid(nullptr);

	auto hrtfEffectFactory = []()
	{
		auto instance = new SphXapoEffect();
		instance->Initialize(nullptr, 0);
		return instance;
	};

	self->SetGraphFactory([hrtfEffectFactory = std::move(hrtfEffectFactory), &spatializedDataExtractor](auto & xaudio) { return new AudioGraphMapper(xaudio, spatializedDataExtractor, hrtfEffectFactory); });

	self->InternalFinalConstructAddRef();
	HRESULT hr = self->_AtlInitialConstruct();
	if (SUCCEEDED(hr))
		hr = self->FinalConstruct();
	if (SUCCEEDED(hr))
		hr = self->_AtlFinalConstruct();
	self->InternalFinalConstructRelease();

	if (SUCCEEDED(hr))
	{
		hr = original->QueryInterface(__uuidof(IXAudio2), reinterpret_cast<void**>(&self->_original));
	}

	if (SUCCEEDED(hr))
		hr = self->QueryInterface(riid, ppvObject);

	if (hr != S_OK)
		delete self;

#if defined(_DEBUG)
	XAUDIO2_DEBUG_CONFIGURATION config = { };
	config.LogThreadID = true;
	config.TraceMask = XAUDIO2_LOG_WARNINGS;
	//config.TraceMask = config.TraceMask | XAUDIO2_LOG_FUNC_CALLS | XAUDIO2_LOG_DETAIL;
	config.BreakMask = XAUDIO2_LOG_ERRORS;

	self->_original->SetDebugConfiguration(&config);
#endif

	return hr;
}

HRESULT XAudio2Proxy::CreateActualDebugInstance(IUnknown* original, const IID& riid, void** ppvObject)
{
	IXAudio2 * xaudio;
	original->QueryInterface(__uuidof(IXAudio2), reinterpret_cast<void**>(&xaudio));
	*ppvObject = xaudio;
	XAUDIO2_DEBUG_CONFIGURATION config;
	config.LogThreadID = true;
	config.TraceMask = XAUDIO2_LOG_WARNINGS;
	//config.TraceMask = config.TraceMask | XAUDIO2_LOG_FUNC_CALLS | XAUDIO2_LOG_DETAIL;
	config.BreakMask = XAUDIO2_LOG_ERRORS;

	xaudio->SetDebugConfiguration(&config);
	return S_OK;
}

STDMETHODIMP XAudio2Proxy::GetDeviceCount(UINT32 * pCount)
{
	return _original->GetDeviceCount(pCount);
}

STDMETHODIMP XAudio2Proxy::GetDeviceDetails(UINT32 Index, XAUDIO2_DEVICE_DETAILS * pDeviceDetails)
{
	return _original->GetDeviceDetails(Index, pDeviceDetails);
}

STDMETHODIMP XAudio2Proxy::Initialize(UINT32 Flags, XAUDIO2_PROCESSOR XAudio2Processor)
{
	logger::logDebug("Initializing XAudio2Proxy");
	const HRESULT result = _original->Initialize(Flags | XAUDIO2_DEBUG_ENGINE, XAudio2Processor);
	logger::logDebug(L"(Not)constructed XAudio2Proxy with result " + std::to_wstring(result));

	_graph.reset(_graphFactory(*_original));

	return result;
}

STDMETHODIMP XAudio2Proxy::RegisterForCallbacks(IXAudio2EngineCallback * pCallback)
{
	return _original->RegisterForCallbacks(pCallback);
}

STDMETHODIMP_(void) XAudio2Proxy::UnregisterForCallbacks(IXAudio2EngineCallback * pCallback)
{
	_original->UnregisterForCallbacks(pCallback);
}

STDMETHODIMP XAudio2Proxy::CreateSourceVoice(IXAudio2SourceVoice ** ppSourceVoice, const WAVEFORMATEX * pSourceFormat, UINT32 Flags, float MaxFrequencyRatio,
	IXAudio2VoiceCallback * pCallback, const XAUDIO2_VOICE_SENDS * pSendList, const XAUDIO2_EFFECT_CHAIN * pEffectChain)
{
	try
	{
		*ppSourceVoice = _graph->CreateSourceVoice(pSourceFormat, Flags, MaxFrequencyRatio, pCallback, pSendList, pEffectChain);
		return S_OK;
	}
	catch (std::bad_alloc &)
	{
		return E_OUTOFMEMORY;
	}
}

STDMETHODIMP XAudio2Proxy::CreateSubmixVoice(IXAudio2SubmixVoice ** ppSubmixVoice, UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 ProcessingStage, const XAUDIO2_VOICE_SENDS * pSendList, const XAUDIO2_EFFECT_CHAIN * pEffectChain)
{
	try
	{
		*ppSubmixVoice = _graph->CreateSubmixVoice(InputChannels, InputSampleRate, Flags, ProcessingStage, pSendList, pEffectChain);
		return S_OK;
	}
	catch (std::bad_alloc &)
	{
		return E_OUTOFMEMORY;
	}
}

STDMETHODIMP XAudio2Proxy::CreateMasteringVoice(IXAudio2MasteringVoice ** ppMasteringVoice, UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 DeviceIndex, const XAUDIO2_EFFECT_CHAIN * pEffectChain)
{
	try
	{
		*ppMasteringVoice = _graph->CreateMasteringVoice(InputChannels, InputSampleRate, Flags, DeviceIndex, pEffectChain);
		return S_OK;
	}
	catch (std::bad_alloc &)
	{
		return E_OUTOFMEMORY;
	}
}

STDMETHODIMP XAudio2Proxy::StartEngine()
{
	return _original->StartEngine();
}

STDMETHODIMP_(void) XAudio2Proxy::StopEngine()
{
	_original->StopEngine();
}

STDMETHODIMP XAudio2Proxy::CommitChanges(UINT32 OperationSet)
{
	return _original->CommitChanges(OperationSet);
}

STDMETHODIMP_(void) XAudio2Proxy::GetPerformanceData(XAUDIO2_PERFORMANCE_DATA * pPerfData)
{
	return _original->GetPerformanceData(pPerfData);
}

void XAudio2Proxy::SetGraphFactory(AudioGraphFactory factory)
{
	_graphFactory = std::move(factory);
}

STDMETHODIMP_(void) XAudio2Proxy::SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION * pDebugConfiguration, void * pReserved)
{
	_original->SetDebugConfiguration(pDebugConfiguration, pReserved);
}
