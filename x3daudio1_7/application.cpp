#include "stdafx.h"
#include "application.h"

#include "ISpatializationGlue.h"

#include <memory>
#include "x3daudio-hook/Matrix/MatrixEncodingX3DAudioProxy.h"
#include "x3daudio-hook/Matrix/Sound3DRegistry.h"
#include "x3daudio-hook/Sequenced/SequencedX3DAudioProxy.h"
#include "proxy/XAudio2Proxy.h"
#include "logger.h"

#define GLUE_SEQUENCED
//#define GLUE_MATRIX_ENCODING
//#define CREATE_AUTHENTIC_DEBUG_XAUDIO

std::unique_ptr<ISpatializationGlue> s_glue;


void EnsureInitialized()
{
	if (s_glue == nullptr)
	{
#if defined(GLUE_MATRIX_ENCODING)
		s_glue = std::make_unique<MatrixEncodingX3DAudioProxy>(&Sound3DRegistry::GetInstance());
#elif defined(GLUE_SEQUENCED)
		s_glue = std::make_unique<SequencedX3DAudioProxy>();
#endif
	}
}

ISpatializedDataExtractor & get_spatialized_data_extractor()
{
	EnsureInitialized();
	return *s_glue;
}

IX3DAudioProxy & get_x3daudio_proxy()
{
	EnsureInitialized();
	return *s_glue;
}

HRESULT create_xaudio_proxy(ATL::CComPtr<IUnknown> original, const IID& riid, void** ppv)
{
#if defined(CREATE_AUTHENTIC_DEBUG_XAUDIO)
	return XAudio2Proxy::CreateActualDebugInstance(originalObject.Detach(), riid, ppv);
#else
	return XAudio2Proxy::CreateInstance(original.Detach(), riid, ppv, get_spatialized_data_extractor());
#endif
}
