#pragma once

// ============================================================================================
// NullGuard.h - the small helpers AMF's null checks are written with (2.3.0).
//
// Rule 14: every pointer AMF reads is checked first; a null is a real anomaly, so it is logged at
// warn - but ONCE per call site, because most of these run every frame and a reporter who raises
// the log level must still get a readable log. Each helper owns a function-local static for that:
// the macros one `static std::atomic_bool` per expansion, the functions a static set of the call
// sites (std::source_location) that have already warned.
//
//   AMF_REQUIRE(p, retval);     if p is null: warn once here, return retval
//   AMF_REQUIRE_VOID(p);        if p is null: warn once here, return
//   AMF_OR(p, "fallback")       p, or the string literal fallback (warns once here when p is null)
//   amf::Str(p)                 p, or "" - so a std::string / string_view built from foreign text never reads null
//   amf::Call(fn)(args...)      calls a stored callback / function pointer / std::function only when it is set;
//                               a null one warns once here and gives the result type's default ({} / false / 0)
//
// tools/null_check_audit.py recognises all of them: the macros expand to `if (!(p)) {...; return ...;}`, which
// it reads as an early exit; amf::Str and amf::Or are listed in tools/nonnull-contracts.json (their result is
// never null - "" or a string literal); amf::Call's own body tests the callee before calling it.
//
// AMF_REQUIRE / AMF_REQUIRE_VOID expand to a bare if-statement, not do { } while (0), on purpose: the audit
// counts an early exit only when it is a statement of the enclosing block. Do not write them as the lone
// statement of an if without braces.
// ============================================================================================

#include "utils/Logger.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace amf
{
	namespace detail
	{
		// "source\Foo.cpp" from the full path the compiler records (the build trims it, but never rely on that)
		inline std::string_view ShortFile(const char* a_path)
		{
			std::string_view p = a_path ? a_path : "";
			const auto       cut = p.find_last_of("\\/");
			return cut == std::string_view::npos ? p : p.substr(cut + 1);
		}

		inline void WarnNull(const char* a_what, const std::source_location& a_at)
		{
			logger::warn("null check: '{}' was null at {}:{} ({}) - skipped safely; reported once per place",
						 a_what ? a_what : "?", ShortFile(a_at.file_name()), a_at.line(), a_at.function_name());
		}

		// True the first time the call site a_at misses; false every later time.
		inline bool FirstMissAt(const std::source_location& a_at)
		{
			static std::mutex                      s_lock;
			static std::unordered_set<std::uint64_t> s_warned;
			const std::uint64_t h = std::hash<std::string_view>{}(a_at.file_name() ? a_at.file_name() : "") ^
			                        (static_cast<std::uint64_t>(a_at.line()) << 20) ^ a_at.column();
			std::scoped_lock lock(s_lock);
			return s_warned.insert(h).second;
		}
	}

	// p, or "" when p is null (warned once per call site).
	inline const char* Str(const char* a_text, const std::source_location a_at = std::source_location::current())
	{
		if (a_text) { return a_text; }
		if (detail::FirstMissAt(a_at)) { detail::WarnNull("text", a_at); }
		return "";
	}

	// p, or the literal fallback when p is null (warned once per call site). The fallback is a string literal by
	// type (an array reference), so the result can never be null - which is what nonnull-contracts.json says.
	template <std::size_t N>
	const char* Or(const char* a_text, const char (&a_fallback)[N], const char* a_what = "text",
				   const std::source_location a_at = std::source_location::current())
	{
		if (a_text) { return a_text; }
		if (detail::FirstMissAt(a_at)) { detail::WarnNull(a_what, a_at); }
		return a_fallback;
	}

	namespace detail
	{
		// What amf::Call returns: holds the callee and the call site until the call, in the same full expression.
		template <class F>
		class Caller
		{
		public:
			Caller(const F& a_fn, const std::source_location& a_at) : _fn(a_fn), _at(a_at) {}

			template <class... A>
			auto operator()(A&&... a_args) const -> std::invoke_result_t<const F&, A...>
			{
				using R = std::invoke_result_t<const F&, A...>;
				if (!_fn)
				{
					if (FirstMissAt(_at)) { WarnNull("callback", _at); }
					if constexpr (std::is_void_v<R>) { return; }
					else { return R{}; }
				}
				return std::invoke(_fn, std::forward<A>(a_args)...);
			}

		private:
			const F&             _fn;
			std::source_location _at;
		};
	}

	// amf::Call(fn)(args...): calls fn only when it is set. Two calls, not amf::Call(fn, args...), because the call
	// site's location has to be a defaulted LAST parameter, which cannot follow a parameter pack. A FUNCTION, not a
	// class: `amf::Call(fn)(x);` as a class would parse as the declaration of a variable named fn (the 2.3.0 test of
	// this header caught exactly that). The callee is held by reference until the end of the full expression.
	template <class F>
	detail::Caller<F> Call(const F& a_fn, const std::source_location a_at = std::source_location::current())
	{
		return detail::Caller<F>(a_fn, a_at);
	}
}

// if (!p): warn once at this place, return retval
#define AMF_REQUIRE(p, retval)                                                                    \
	if (!(p))                                                                                     \
	{                                                                                             \
		static std::atomic_bool amf_nullWarned_{ false };                                         \
		if (!amf_nullWarned_.exchange(true)) { ::amf::detail::WarnNull(#p, std::source_location::current()); } \
		return retval;                                                                            \
	}

// if (!p): warn once at this place, return
#define AMF_REQUIRE_VOID(p)                                                                       \
	if (!(p))                                                                                     \
	{                                                                                             \
		static std::atomic_bool amf_nullWarned_{ false };                                         \
		if (!amf_nullWarned_.exchange(true)) { ::amf::detail::WarnNull(#p, std::source_location::current()); } \
		return;                                                                                   \
	}

// p, or the string literal fallback (warned once at this place)
#define AMF_OR(p, fallback) ::amf::Or((p), fallback, #p)
