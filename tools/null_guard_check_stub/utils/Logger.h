#pragma once
// tools\null_guard_check only: stands in for include\utils\Logger.h (spdlog, CommonLib) so include\utils\NullGuard.h
// compiles off-game. warn() is counted and printed, because "warned once per place" is what the check measures.
#include <cstdio>
#include <format>
#include <string>
#include <utility>
inline int g_warns = 0;
namespace logger
{
	template <class... A>
	void warn(std::format_string<A...> a_fmt, A&&... a_args)
	{
		++g_warns;
		std::puts(std::format(a_fmt, std::forward<A>(a_args)...).c_str());
	}
}
