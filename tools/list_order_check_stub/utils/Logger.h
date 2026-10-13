#pragma once
// tools\list_order_check only: stands in for include\utils\Logger.h (spdlog, CommonLib) so source\Personalization.cpp
// compiles off-game. The messages are dropped.
#include <format>
namespace logger
{
	template <class... A> void info(A&&...) {}
	template <class... A> void warn(A&&...) {}
	template <class... A> void debug(A&&...) {}
	template <class... A> void error(A&&...) {}
}
