// Copyright (C) 2020-2025 Parabola Research Limited
// SPDX-License-Identifier: MPL-2.0

#include "Assert.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace Bungee::Assert {

#if BUNGEE_SELF_TEST
#	ifndef BUNGEE_ASSERT_FAIL_EXTERNAL
void fail(int level, const char *message, const char *file, int line)
{
	fprintf(stderr, "Failed: BUNGEE_ASSERT%d(%s)  at (%s: %d)\n", level, message, file, line);
	std::abort();
}
#	endif

FloatingPointExceptions::FloatingPointExceptions(int allowed) :
	allowed(allowed)
{
	auto success = !std::fegetenv(&original);
	BUNGEE_ASSERT1(success);

	success = !std::feclearexcept(~allowed & FE_ALL_EXCEPT);
	BUNGEE_ASSERT1(success);

#	ifdef __GLIBC__
	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_ALL_EXCEPT & ~allowed);
	std::signal(SIGFPE, [](int signum) { std::abort(); });
#	endif
}

void FloatingPointExceptions::check() const
{
	BUNGEE_ASSERT1(!std::fetestexcept(~allowed & FE_INEXACT));
	BUNGEE_ASSERT1(!std::fetestexcept(~allowed & FE_UNDERFLOW));
	BUNGEE_ASSERT1(!std::fetestexcept(~allowed & FE_OVERFLOW));
	BUNGEE_ASSERT1(!std::fetestexcept(~allowed & FE_DIVBYZERO));
	BUNGEE_ASSERT1(!std::fetestexcept(~allowed & FE_INVALID));
}

FloatingPointExceptions::~FloatingPointExceptions()
{
	check();
	auto success = !std::fesetenv(&original);
	BUNGEE_ASSERT1(success);
}

#endif

} // namespace Bungee::Assert
