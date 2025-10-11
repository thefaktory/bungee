// Copyright (C) 2020-2025 Parabola Research Limited
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Modes.h"

#ifndef BUNGEE_VISIBILITY
#	define BUNGEE_VISIBILITY
#endif

// This header file contains a C++ API and an equivalent C API.
// Bungee is a C++ library first and users should prefer the C++ API if they are able to use it.
#ifdef __cplusplus
#	include <cstdint>
#	define BUNGEE_PREFIX extern "C" BUNGEE_VISIBILITY const Bungee::Functions
namespace Bungee {
#else
#	include <stdint.h>
#	define BUNGEE_PREFIX BUNGEE_VISIBILITY const struct Functions
#	define bool char
#endif

// An object of type Request is passed to the audio stretcher every time an audio grain is processed.
struct Request
{
	// Frame-offset within the input audio of the centre-point of the current audio grain.
	// NaN signifies an invalid grain that produces no audio output and may be used for flushing.
	double position;

	// Output audio speed. Value of 1 means speed should be unchanged relative to the input audio.
	// Used by Stretcher's internal algorithms only when it's not possible to determine speed by
	// subtracting Request::position of previous grain from Request::position of current grain.
	double speed;

	// Adjustment as a frequency multiplier with a value of 1 meaning no pitch adjustment
	double pitch;

	// Set to have the stretcher forget all previous grains and restart on this grain.
	bool reset;

	// How resampling should be applied to this grain.
	enum ResampleMode resampleMode;
};

// Information to describe a chunk of audio that the audio stretcher requires as input for the current grain.
// Note that input chunks of consecutive grains often overlap and are usually centred on the grain's
// Request::position.
struct InputChunk
{
	// Frame offsets relative to the start of the audio track
	int begin;
	int end;
};

// Describes a chunk of audio output
// Output chunks do not overlap and can be appended for seamless playback
struct OutputChunk
{
	float *data; // audio output data, not aligned and not interleaved
	int frameCount;
	intptr_t channelStride; // nth audio channel audio starts at data[n * channelStride]
#ifdef __cplusplus
	static constexpr int begin = 0, end = 1;
#endif
	const struct Request *request[2]; // request[0] corresponds to the first frame of data, request[1] corresponds to the frame after the last frame of data.
};

// Stretcher audio sample rates, in Hz
struct SampleRates
{
	int input;
	int output;
};

// This struct is not part of the C++ API. It is necessary here to facilitate extern "C" linkage to shared libaries.
// Users of the C API may use this struct to access the functions of the Bungee stretcher.
struct Functions
{
	const char *(*edition)(void);
	const char *(*version)(void);
	void *(*create)(struct SampleRates sampleRates, int channelCount, int log2SynthesisHopAdjust);
	void (*destroy)(void *implementation);
	void (*enableInstrumentation)(void *implementation, int enable);
	int (*maxInputFrameCount)(const void *implementation);
	void (*preroll)(const void *implementation, struct Request *request);
	void (*next)(const void *implementation, struct Request *request);
	struct InputChunk (*specifyGrain)(void *implementation, const struct Request *request, double bufferStartPosition);
	void (*analyseGrain)(void *implementation, const float *data, intptr_t channelStride, int muteFrameCountHead, int muteFrameCountTail);
	void (*synthesiseGrain)(void *implementation, struct OutputChunk *outputChunk);
	bool (*isFlushed)(const void *implementation);
};

#ifdef __cplusplus
}
#else
#	undef bool
#endif

// These are not part of the C++ API and exist primarily to facilitate extern "C" linkage to Bungee's shared libaries.
// C++ users should instantiate Stretcher<>
BUNGEE_PREFIX *getFunctionsBungeeBasic(void);
BUNGEE_PREFIX *getFunctionsBungeePro(void);

#ifdef __cplusplus
namespace Bungee {

// Stretcher<Basic> is the open-source implementation contained in this repository
struct Basic
{
	static constexpr auto getFunctions = &getFunctionsBungeeBasic;
};

// Stretcher<Pro> is an enhanced and optimised implementation that is available under commercial license
struct Pro
{
	static constexpr auto getFunctions = &getFunctionsBungeePro;
};

// This is the Bungee stretcher class that users should instantiate.
template <class Edition>
struct Stretcher
{
	// Reports, for example, "Pro" or "Basic"
	static inline const char *edition()
	{
		return Edition::getFunctions()->edition();
	}

	// Reports the release number of the library, for example "1.2.3"
	static inline const char *version()
	{
		return Edition::getFunctions()->version();
	}

	// Initialises a stretcher instance with the specified sample rates and number of channels.
	// The parameter log2SynthesisHopAdjust influences the granularity of the stretcher. In general, setting
	// this parameter non-zero will reduce output audio quality but a value of -1 or +1 may be desirable
	// under some circumstances.
	// log2SynthesisHopAdjust=-1 doubles granular frequency, reducing latency and possibly improving weak transients.
	// log2SynthesisHopAdjust=1 halves granular frequency, possibly benefiting dense tones.
	inline Stretcher(SampleRates sampleRates, int channelCount, int log2SynthesisHopAdjust = 0) :
		functions(Edition::getFunctions()),
		state(functions->create(sampleRates, channelCount, log2SynthesisHopAdjust))
	{
	}

	inline ~Stretcher()
	{
		functions->destroy(state);
	}

	// If called with a true parameter, enables verbose diagnostics and checks that report to
	// the system log file on iOS, Mac and Android, or to stderr on other platforms.
	inline void enableInstrumentation(bool enable)
	{
		functions->enableInstrumentation(state, enable);
	}

	// Returns the largest number of frames that might be requested by specifyGrain()
	// This helps the caller to allocate large enough buffers because it is guaranteed that
	// InputChunk::frameCount() will not exceed this number.
	inline int maxInputFrameCount() const
	{
		return functions->maxInputFrameCount(state);
	}

	// This function adjusts request.position so that the stretcher has a run in of a few
	// grains before hitting the requested position. Without preroll, the first milliseconds
	// of audio might sound weak or initial transients might be lost.
	inline void preroll(Request &request) const
	{
		functions->preroll(state, &request);
	}

	// This function prepares request.position and request.reset for the subsequent grain.
	// Typically called within a granular loop where playback at constant request.speed is desired.
	inline void next(Request &request) const
	{
		functions->next(state, &request);
	}

	// Specify a grain of audio and compute the necessary segment of input audio.
	// After calling this function, call analyseGrain.
	inline InputChunk specifyGrain(const Request &request, double bufferStartPosition = 0.)
	{
		return functions->specifyGrain(state, &request, bufferStartPosition);
	}

	// Begins processing the grain. The audio data should correspond to the range
	// specified by specifyGrain's return value. After calling this function, call synthesiseGrain.
	// The "muteFrame" parameters specify a number of input frames that are unavailable at the
	// start (muteFrameCountHead) and end (muteFrameCountTail) of the audio data. These frame
	// ranges will not be read from the input buffer and mute, zero-valued frames will be used instead.
	inline void analyseGrain(const float *data, intptr_t channelStride, int muteFrameCountHead = 0, int muteFrameCountTail = 0)
	{
		functions->analyseGrain(state, data, channelStride, muteFrameCountHead, muteFrameCountTail);
	}

	// Complete processing of the grain of audio that was previously set up with calls to specifyGrain and analyseGrain.
	inline void synthesiseGrain(OutputChunk &outputChunk)
	{
		functions->synthesiseGrain(state, &outputChunk);
	}

	// Returns true if every grain in the stretcher's pipeline is invalid (its Request::position was NaN).
	inline bool isFlushed() const
	{
		return functions->isFlushed(state);
	}

	const Functions *const functions;
	void *const state;
};

} // namespace Bungee
#endif
