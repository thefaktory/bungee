// Copyright (C) 2020-2025 Parabola Research Limited
// SPDX-License-Identifier: MPL-2.0

#include "Bungee.h"

#include <algorithm>
#include <cassert>
#include <span>

#pragma once

namespace Bungee {

// This class is a wrapper for Bungee::Stretch<> that provides an easy to use API
// for "streaming" applications where Bungee is used for forward playback only.
// See ../cmd/main.cpp for example usage.
template <class Implementation>
class Stream
{
	class InputBuffer
	{
		const int channelStride;
		const int channelCount;
		std::vector<float> buffer;
		int begin = 0;
		int end = 0;

	public:
		Stretcher<Implementation> &stretcher;
		InputChunk inputChunk{};

		InputBuffer(Stretcher<Implementation> &stretcher, int maxSampleCount, int channelCount) :
			channelStride(maxSampleCount),
			channelCount(channelCount),
			buffer(channelStride * channelCount),
			stretcher(stretcher)
		{
		}

		void append(int inputSampleCount, const float *const *inputPointers)
		{
			int discard = 0;

			if (inputChunk.begin < end)
			{
				if (begin < inputChunk.begin)
				{
					for (int x = 0; x < (int)buffer.size(); x += channelStride)
						std::move(
							&buffer[x + inputChunk.begin - begin],
							&buffer[x + end - begin],
							&buffer[x]);
					begin = inputChunk.begin;
				}
			}
			else
			{
				discard = std::min(inputChunk.begin - begin, inputSampleCount);
				begin = end;
			}

			for (int c = 0; c < channelCount; ++c)
				if (inputPointers)
					std::copy(
						&inputPointers[c][discard],
						&inputPointers[c][inputSampleCount],
						&buffer[(end - begin) + c * channelStride]);
				else
					std::fill(
						&buffer[(end - begin) + c * channelStride],
						&buffer[(end - begin) + c * channelStride + (inputSampleCount - discard)],
						0.f);
			begin += discard;
			end += inputSampleCount;
			assert(end >= begin);
			assert(end - begin <= channelStride);
		}

		auto endPosition() const
		{
			return end;
		}

		void analyseGrain() const
		{
			const int muteHead = begin - inputChunk.begin;
			const int muteTail = inputChunk.end - end;
			assert(muteHead >= (inputChunk.end - inputChunk.begin) || muteTail <= 0);
			stretcher.analyseGrain(buffer.data() - muteHead, channelStride, muteHead, muteTail);
		}

	};

	const int channelCount;

	InputBuffer inputBuffer;

	Request request{};

	OutputChunk outputChunk{};
	int outputChunkConsumed = 0;

	double samplesNeeded = 0.;

public:
	Stream(Stretcher<Implementation> &stretcher, int maxInputSampleCount, int channelCount) :
		channelCount(channelCount),
		inputBuffer(stretcher, stretcher.maxInputFrameCount() + maxInputSampleCount, channelCount)
	{
		request.position = std::numeric_limits<double>::quiet_NaN();
	}

	// Processes a segment of audio. Returns the number of output samples that were rendered to outputPointers.
	// The number of samples will be set by dithering either to floor(outputSampleCount) or ceil(outputSampleCount).
	int process(
		const float *const *inputPointers, // Array of pointers, one for each channel of input audio: set to nullptr for mute input
		float *const *outputPointers, // Array of pointers, one for each channel of output audio
		int inputSampleCount, // Number of input audio samples to be processed
		double outputSampleCount, // Number of audio output samples requred: this may be fractional. This, together with stretcher sample rate settings, controls playback speed.
		double pitch = 1.) // Audio pitch shift (see Request::pitch)
	{
		inputBuffer.append(inputSampleCount, inputPointers);

		request.speed = inputSampleCount / outputSampleCount;
		request.pitch = pitch;

		samplesNeeded += outputSampleCount;

		int sampleCounter = 0;
		for (bool processGrain = false; sampleCounter != std::round(samplesNeeded); processGrain = true)
		{
			if (processGrain)
			{
				if (!std::isnan(request.position))
				{
					inputBuffer.analyseGrain();
					inputBuffer.stretcher.synthesiseGrain(outputChunk);
					outputChunkConsumed = 0;
				}

				[[maybe_unused]] const double proportionRemaining = 1. - sampleCounter / std::round(outputSampleCount);
				const double proportionRemainingDenominator = std::round(outputSampleCount);
				const double proportionRemainingNumerator = proportionRemainingDenominator - sampleCounter;

				const auto position = inputBuffer.endPosition() - inputBuffer.stretcher.maxInputFrameCount() / 2 - inputSampleCount * proportionRemainingNumerator / proportionRemainingDenominator;
				request.reset = !(position > request.position);
				request.position = position;
				inputBuffer.inputChunk = inputBuffer.stretcher.specifyGrain(request);
			}

			if (outputChunk.request[0] && !std::isnan(outputChunk.request[0]->position))
			{
				const int need = std::round(samplesNeeded) - sampleCounter;
				const int available = outputChunk.frameCount - outputChunkConsumed;
				const int n = std::min(need, available);

				for (int c = 0; c < channelCount; ++c)
					std::copy(
						outputChunk.data + outputChunkConsumed + c * outputChunk.channelStride,
						outputChunk.data + outputChunkConsumed + c * outputChunk.channelStride + n,
						outputPointers[c] + sampleCounter);

				sampleCounter += n;
				outputChunkConsumed += n;
			}
		}

		assert(sampleCounter == std::floor(outputSampleCount) || sampleCounter == std::ceil(outputSampleCount));
		samplesNeeded -= sampleCounter;
		return sampleCounter;
	};

	// Current position in the input stream. This is sum of inputSampleCount over all process() calls.
	int inputPosition() const
	{
		return inputBuffer.endPosition();
	}

	// Current position of the output stream in terms of input samples.
	double outputPosition() const
	{
		return outputChunk.request[0]->position + outputChunkConsumed * (outputChunk.request[1]->position - outputChunk.request[0]->position) / outputChunk.frameCount;
	}

	// Latency due to the stretcher. Units are input samples.
	double latency() const
	{
		return inputPosition() - outputPosition();
	}

	float occupancy() const
	{
		int absEnd = abs(inputBuffer.end);
		int absBegin = abs(inputBuffer.begin);
		if (absEnd > absBegin)
			return static_cast<float>(absEnd) / static_cast<float>(std::numeric_limits<int>::max());
		else
			return static_cast<float>(absBegin) / static_cast<float>(std::numeric_limits<int>::max());
	}
};

} // namespace Bungee