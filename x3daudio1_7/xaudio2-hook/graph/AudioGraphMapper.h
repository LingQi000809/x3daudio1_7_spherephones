#pragma once

#include "EdgeRepository.h"

#include <xaudio2.h>
#include <map>
#include <memory>
#include <xaudio2-hook/common_types.h>
#include <functional>


class SphXapoEffect;
class ISpatializedDataExtractor;
class XAudio2SourceVoiceProxy;
class XAudio2VoiceProxy;

typedef std::function<SphXapoEffect* ()> hrtf_effect_factory;

class TailVoiceDescriptor
{
public:
	std::shared_ptr<IXAudio2SubmixVoice> voice;
	UINT32 flags;
	bool isSpatialized;
};

class Node
{
public:
	Node() = default;
	Node(const Node& other) = delete;
	Node& operator=(const Node& other) = delete;

	~Node()
	{
		proxyVoice.reset();
		mainVoice.reset();
		tailVoices.clear(); // must be destroyed last
	}

	std::shared_ptr<IXAudio2Voice> proxyVoice;
	std::shared_ptr<IXAudio2Voice> mainVoice;
	// maps proxy send to actual tail voice
	std::map<Node*, TailVoiceDescriptor> tailVoices;

	UINT32 actualProcessingStage;
	UINT32 inputChannelsCount;
	UINT32 inputSampleRate;
	UINT32 mainOutputChannelsCount;
};

class AudioGraphMapper
{
public:
	// AudioGraphMapper does not own xaudio
	AudioGraphMapper(IXAudio2 & xaudio, ISpatializedDataExtractor & spatializedDataExtractor, hrtf_effect_factory hrtfEffectFactory);
	AudioGraphMapper(const AudioGraphMapper &) = delete;
	AudioGraphMapper& operator=(const AudioGraphMapper& other) = delete;

	~AudioGraphMapper();
	IXAudio2SourceVoice * CreateSourceVoice(
		const WAVEFORMATEX * pSourceFormat,
		UINT32 Flags,
		float MaxFrequencyRatio,
		IXAudio2VoiceCallback * pCallback,
		const XAUDIO2_VOICE_SENDS * pSendList,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);

	IXAudio2SubmixVoice * CreateSubmixVoice(
		UINT32 InputChannels,
		UINT32 InputSampleRate,
		UINT32 Flags,
		UINT32 ProcessingStage,
		const XAUDIO2_VOICE_SENDS * pSendList,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);

	IXAudio2MasteringVoice * CreateMasteringVoice(
		UINT32 InputChannels,
		UINT32 InputSampleRate,
		UINT32 Flags,
		UINT32 DeviceIndex,
		const XAUDIO2_EFFECT_CHAIN * pEffectChain);


private:
	IXAudio2 & _xaudio;
	ISpatializedDataExtractor & _spatializedDataExtractor;
	hrtf_effect_factory _hrtfEffectFactory;
	std::map<IXAudio2Voice*, std::unique_ptr<Node>> _nodes;
	Node * _masteringNode;
	EdgeRepository<Node*> _edges;

	static const XAUDIO2_VOICE_SENDS _emptySends;

	VoiceSends mapProxySendsToActualOnes(const VoiceSends & proxySends) const;
	const Node * getNodeForProxyVoice(IXAudio2Voice* pDestination) const;
	Node * getNodeForProxyVoice(IXAudio2Voice* pDestination);
	void setupCommonCallbacks(XAudio2VoiceProxy* proxyVoice, const std::shared_ptr<IXAudio2Voice> & actualVoice);
	void resetSendsForVoice(XAudio2VoiceProxy* proxyVoice);
	void updateSendsForMainVoice(Node * node);
	std::shared_ptr<IXAudio2SubmixVoice> createTailVoice(Node * senderNode, Node * sendNode, const effect_chain & effectChain);
};