// tools\null_guard_check - include\utils\NullGuard.h off-game (2.3.0): each helper returns what it promises for a null,
// passes a non-null through unchanged, and warns ONCE per place however often that place sees a null.
//     tools\null_guard_check.bat
#include "utils/NullGuard.h"

#include <cstdio>
#include <functional>
#include <string>

namespace
{
	int g_failed = 0;
	int g_passed = 0;
	void Check(bool a_ok, const char* a_what)
	{
		if (a_ok) { ++g_passed; }
		else { ++g_failed; std::printf("FAIL: %s\n", a_what); }
	}

	int  Require(const int* a_p) { AMF_REQUIRE(a_p, -1); return *a_p; }
	void RequireVoid(const int* a_p, int& a_out) { AMF_REQUIRE_VOID(a_p); a_out = *a_p; }
	int  Twice(int a_x) { return 2 * a_x; }
}

int main()
{
	int v = 5, out = 0;
	for (int i = 0; i < 3; ++i) { Check(Require(nullptr) == -1, "AMF_REQUIRE returns its value for a null"); }
	Check(Require(&v) == 5, "AMF_REQUIRE passes a non-null through");
	RequireVoid(nullptr, out);
	RequireVoid(nullptr, out);
	Check(out == 0, "AMF_REQUIRE_VOID returns before the read");
	RequireVoid(&v, out);
	Check(out == 5, "AMF_REQUIRE_VOID passes a non-null through");
	Check(g_warns == 2, "the two macros warned once each, not once per call");

	const int   before = g_warns;
	const char* none = nullptr;
	for (int i = 0; i < 3; ++i) { Check(std::string(amf::Str(none)).empty(), "amf::Str gives \"\" for a null"); }
	Check(std::string(amf::Str("text")) == "text", "amf::Str passes text through");
	Check(std::string(AMF_OR(none, "fallback")) == "fallback", "AMF_OR gives the fallback for a null");
	Check(std::string(AMF_OR("given", "fallback")) == "given", "AMF_OR passes text through");
	Check(g_warns == before + 2, "amf::Str and AMF_OR warned once per place");

	int (*fn)(int) = nullptr;
	for (int i = 0; i < 3; ++i) { Check(amf::Call(fn)(4) == 0, "amf::Call gives the result type's default for a null"); }
	fn = &Twice;
	Check(amf::Call(fn)(4) == 8, "amf::Call calls a set function pointer");
	std::function<bool(int)> empty;
	Check(amf::Call(empty)(1) == false, "amf::Call on an empty std::function gives false");
	std::function<bool(int)> set = [](int a_x) { return a_x == 1; };
	Check(amf::Call(set)(1), "amf::Call calls a set std::function");
	void (*voidFn)(int) = nullptr;
	amf::Call(voidFn)(1);   // a statement: must parse as a call, not as the declaration of a variable named voidFn
	Check(g_warns == before + 2 + 3, "amf::Call warned once per place");

	std::printf("null_guard_check: %d of %d passed\n", g_passed, g_passed + g_failed);
	return g_failed == 0 ? 0 : 1;
}
