// Copyright (C) 2020-2025 Parabola Research Limited
// SPDX-License-Identifier: MPL-2.0

#include "Instrumentation.h"

#if defined(__ANDROID__)
#	include <android/log.h>
#elif defined(__APPLE__)
#	include <os/log.h>
#else
#	include <cstdio>
#endif

#include <cstdarg>
#include <cstdlib>

namespace Bungee::Internal {

thread_local Instrumentation *Instrumentation::threadLocal;

void Instrumentation::log(const char *format, ...)
{
#ifndef BUNGEE_NO_LOG
	if (threadLocal->enabled)
	{
		char message[4096];

		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

#	if defined(__ANDROID__)
		__android_log_print(ANDROID_LOG_DEBUG, "Bungee", "%s", message);
#	elif defined(__APPLE__)
		static const auto log = os_log_create("com.parabolaresearch.bungee", "diagnostics");
		os_log_info(log, "%{public}s", message);
#	else
		fprintf(stderr, "Bungee: %s\n", message);
#	endif
		++threadLocal->logCount;
	}
#endif
}

Instrumentation::Call::Call(Instrumentation *instrumentation, int sequence)
{
	threadLocal = instrumentation;
	if (sequence != instrumentation->expected)
	{
		static const char *names[] = {"specifyGrain", "analyseGrain", "synthesiseGrain"};
		log("FATAL: stretcher functions called in the wrong order: %s was called when expecting a call to %s", names[sequence], names[instrumentation->expected]);
		std::abort();
	}
	instrumentation->expected = (sequence + 1) % 3;
}

Instrumentation::Call::~Call()
{
	threadLocal = nullptr;
}

} // namespace Bungee::Internal
