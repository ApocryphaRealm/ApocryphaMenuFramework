// Off-game check of the Mod Function Menu entries (include/MfmCore.h - the same code the page runs): the TOML reading,
// the folder tree (exact lower-case .toml, sorted by path), the resolution (GetModuleHandleW + GetProcAddress only) and
// the guarded call (a function that faults or throws is reported, not fatal).
//   tools\mfm_check.bat [<a folder holding ccld_ModFunctionMenu\{Mod,Config}>]
// With a folder, its real entries are read and listed too (the released MFM's own "Config\Mod Function Menu.toml").
// Prints one line per check and "mfm_check: N of N passed"; exits 1 on any failure.
#include "../include/MfmCore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Exports of this exe, so the resolver and the call can be proven on a module that is really loaded.
extern "C" __declspec(dllexport) void TestVoid() { std::puts("    (TestVoid ran)"); }
extern "C" __declspec(dllexport) void TestMessage(char* a_buf, std::size_t a_len)
{
	std::snprintf(a_buf, a_len, "[info] reloaded %d settings", 7);
}
extern "C" __declspec(dllexport) void TestFill(char* a_buf, std::size_t a_len)
{
	std::memset(a_buf, 'x', a_len);   // no terminator at all: the caller must cut it
}
extern "C" __declspec(dllexport) void TestFault()
{
	volatile int* p = nullptr;
	*p = 1;
}
extern "C" __declspec(dllexport) void TestThrow() { throw std::runtime_error("boom"); }

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	void Write(const std::filesystem::path& a_p, const std::string& a_text)
	{
		std::filesystem::create_directories(a_p.parent_path());
		std::ofstream(a_p, std::ios::binary) << a_text;
	}

	// the condition is evaluated BEFORE the message, so a message can show what the condition read
#define CHECK(a_cond, a_msg) do { const bool ok_ = (a_cond); Check(ok_, (a_msg)); } while (0)

	const mfm::Node* Find(const mfm::Node& a_n, const std::string& a_rel)
	{
		if (a_n.rel == a_rel) { return &a_n; }
		for (const mfm::Node& c : a_n.children) { if (const mfm::Node* f = Find(c, a_rel)) { return f; } }
		return nullptr;
	}
}

int main(int argc, char** argv)
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "amf-mfm-check" / "ccld_ModFunctionMenu";
	std::error_code ec;
	fs::remove_all(root.parent_path(), ec);

	// ---- the fixtures: three made-up entries the brief names, plus the shapes the reader must handle ----
	const std::string exe = "mfm_check.exe";
	Write(root / "Mod" / "Test" / "Good Void.toml", "dll = \"" + exe + "\"\napi = \"TestVoid\"\n");
	Write(root / "Mod" / "Test" / "Good Message.toml",
		"# a comment line\ndll = '" + exe + "'   # a literal string, then a comment\napi = \"TestMessage\"\ntype = \"MessageBox\"\n"
		"preAction = \"CloseMenu\"\npostAction = \"CloseMenuAndResetPath\"\n");
	Write(root / "Mod" / "Test" / "Wrong api.toml", "dll = \"" + exe + "\"\napi = \"NoSuchExport\"\ntype = \"Void\"\n");
	Write(root / "Mod" / "Test" / "Unknown type.toml",
		"dll = \"" + exe + "\"\napi = \"TestVoid\"\ntype = \"messagebox\"\npreAction = \"closemenu\"\npostAction = \"Bogus\"\n");
	Write(root / "Mod" / "Test" / "Upper case.TOML", "dll = \"" + exe + "\"\napi = \"TestVoid\"\n");
	Write(root / "Mod" / "Test" / "Not an entry.txt", "dll = \"x\"\n");
	Write(root / "Mod" / "Test" / "Faults.toml", "dll = \"" + exe + "\"\napi = \"TestFault\"\n");
	Write(root / "Mod" / "Test" / "Throws.toml", "dll = \"" + exe + "\"\napi = \"TestThrow\"\n");
	Write(root / "Mod" / "Test" / "Fills.toml", "dll = \"" + exe + "\"\napi = \"TestFill\"\ntype = \"Message\"\n");
	Write(root / "Mod" / "Toggle UI" / "Toggle HUD - Compass.toml", "dll = \"ccld_ToggleUI.dll\"\napi = \"ToggleHUD_Compass\"\n");
	Write(root / "Mod" / "A first.toml", "\xEF\xBB\xBF" "dll = \"" + exe + "\"\r\napi = \"TestVoid\"\r\n");   // BOM, CRLF
	Write(root / "Mod" / "Escapes.toml",
		"dll = \"mfm\\u005Fcheck.exe\"\n\"api\" = \"\"\"\nTestVoid\"\"\"\n[extra]\ntype = \"MessageBox\"\nother.dll = \"no\"\n");
	Write(root / "Config" / "No api.toml", "dll = \"" + exe + "\"\n");
	Write(root / "Config" / "Broken.toml", "dll = \"" + exe + "\napi = \"TestVoid\"\n");
	Write(root / "Config" / "Twice.toml", "dll = \"a.dll\"\ndll = \"b.dll\"\napi = \"X\"\n");
	Write(root / "Config" / "Not a string.toml", "dll = \"" + exe + "\"\napi = 12\ntype = [\"a\",\n \"b\"]\n");
	// the real one, if given
	if (argc > 1)
	{
		const fs::path real = fs::path(argv[1]) / "ccld_ModFunctionMenu" / "Config" / "Mod Function Menu.toml";
		fs::copy_file(real, root / "Config" / "Mod Function Menu.toml", ec);
		CHECK(!ec, "the released MFM's Config\\Mod Function Menu.toml copied in (" + real.string() + ")");
	}

	// ---- the tree ----
	std::vector<std::string> problems;
	const mfm::Node mod = mfm::BuildGroup(root, "Mod", problems);
	const mfm::Node config = mfm::BuildGroup(root, "Config", problems);
	const mfm::Node none = mfm::BuildGroup(root, "Missing", problems);
	CHECK(problems.empty(), "the tree is read with no problems");
	CHECK(none.children.empty(), "a missing group folder is an empty group");
	CHECK(Find(mod, "Mod/Test/Upper case.TOML") == nullptr, "an upper-case .TOML is not an entry");
	CHECK(Find(mod, "Mod/Test/Not an entry.txt") == nullptr, "a .txt is not an entry");
	CHECK(mod.children.size() == 4 && mod.children[0].name == "A first" && mod.children[1].name == "Escapes" &&
			  mod.children[2].name == "Test" && mod.children[2].folder && mod.children[3].name == "Toggle UI",
		"Mod's top level sorted by path, folders and files mixed: A first, Escapes, Test/, Toggle UI/");
	if (const mfm::Node* t = Find(mod, "Mod/Test"))
	{
		std::string order;
		for (const mfm::Node& c : t->children) { order += c.name + "|"; }
		CHECK(order == "Faults|Fills|Good Message|Good Void|Throws|Unknown type|Wrong api|", "Mod/Test sorted: " + order);
	}
	CHECK(mfm::CountFiles(mod) == 10, "Mod holds 10 entries (" + std::to_string(mfm::CountFiles(mod)) + ")");
	CHECK(mfm::CountFiles(config) == (argc > 1 ? 5u : 4u), "Config holds " + std::to_string(mfm::CountFiles(config)) + " entries");

	auto read = [&](const std::string& a_rel, mfm::Function& a_f, std::string& a_err) {
		const mfm::Node* n = Find(a_rel.rfind("Mod", 0) == 0 ? mod : config, a_rel);
		if (!n) { a_err = "not in the tree"; return false; }
		return mfm::ReadFunction(n->path, a_f, a_err);
	};

	// ---- reading ----
	{
		mfm::Function f; std::string err;
		CHECK(read("Mod/Test/Good Message.toml", f, err) && f.dll == exe && f.api == "TestMessage" && f.type == mfm::Type::kMessageBox &&
				  f.pre == mfm::Action::kCloseMenu && f.post == mfm::Action::kCloseMenuAndResetPath,
			"Good Message: literal string + comments, MessageBox, CloseMenu / CloseMenuAndResetPath " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(read("Mod/Test/Unknown type.toml", f, err) && f.type == mfm::Type::kVoid && f.pre == mfm::Action::kNone &&
				  f.post == mfm::Action::kNone && f.typeText == "messagebox",
			"Unknown type: \"messagebox\" (wrong case) -> Void, \"closemenu\" -> None, \"Bogus\" -> None");
	}
	{
		mfm::Function f; std::string err;
		CHECK(read("Mod/A first.toml", f, err) && f.api == "TestVoid", "a byte-order mark and CRLF lines are read " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(read("Mod/Escapes.toml", f, err) && f.dll == exe && f.api == "TestVoid" && f.type == mfm::Type::kVoid,
			"\\u escape, quoted key, multi-line string; a key under [extra] is not top-level " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(!read("Config/No api.toml", f, err) && err.find("api") != std::string::npos, "no api -> refused: " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(!read("Config/Broken.toml", f, err), "an unclosed string -> refused: " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(!read("Config/Twice.toml", f, err), "a key set twice -> refused: " + err);
	}
	{
		mfm::Function f; std::string err;
		CHECK(!read("Config/Not a string.toml", f, err) && err.find("api") != std::string::npos, "api = 12 counts as missing: " + err);
	}
	if (argc > 1)
	{
		mfm::Function f; std::string err;
		CHECK(read("Config/Mod Function Menu.toml", f, err) && f.dll == "ccld_ModFunctionMenu.dll" && f.api == "ReloadConfig" &&
				  f.type == mfm::Type::kMessageBox && f.pre == mfm::Action::kNone,
			"the real MFM entry: ccld_ModFunctionMenu.dll ! ReloadConfig, MessageBox " + err);
		std::string missing;
		CHECK(mfm::Resolve(f, missing) == nullptr && missing == "ccld_ModFunctionMenu.dll",
			"MFM is not loaded here -> unresolved, naming the DLL (" + missing + ")");
	}

	// ---- resolving (never LoadLibrary) ----
	{
		mfm::Function f; std::string err, missing;
		read("Mod/Toggle UI/Toggle HUD - Compass.toml", f, err);
		CHECK(mfm::Resolve(f, missing) == nullptr && missing == "ccld_ToggleUI.dll", "a DLL that is not loaded -> names it: " + missing);
		CHECK(::GetModuleHandleW(L"ccld_ToggleUI.dll") == nullptr, "and it was not loaded by the attempt");
	}
	{
		mfm::Function f; std::string err, missing;
		read("Mod/Test/Wrong api.toml", f, err);
		CHECK(mfm::Resolve(f, missing) == nullptr && missing == exe + " ! NoSuchExport", "a wrong api -> names the function: " + missing);
	}

	// ---- calling ----
	auto run = [&](const std::string& a_rel) {
		mfm::Function f; std::string err, missing;
		read(a_rel, f, err);
		void* p = mfm::Resolve(f, missing);
		return p ? mfm::Call(p, f.type) : mfm::CallResult{ false, {}, "unresolved: " + missing };
	};
	{
		const auto r = run("Mod/Test/Good Void.toml");
		CHECK(r.ok && r.message.empty(), "Void called");
	}
	{
		const auto r = run("Mod/Test/Good Message.toml");
		CHECK(r.ok && r.message == "[info] reloaded 7 settings", "MessageBox returned its text: " + r.message);
	}
	{
		const auto r = run("Mod/Test/Fills.toml");
		CHECK(r.ok && r.message.size() == mfm::kMessageSize - 1, "a function that fills the whole buffer is cut at 0x3FFF (" +
			std::to_string(r.message.size()) + " chars)");
	}
	{
		const auto r = run("Mod/Test/Unknown type.toml");
		CHECK(r.ok && r.message.empty(), "an unknown type is called as Void");
	}
	{
		const auto r = run("Mod/Test/Faults.toml");
		CHECK(!r.ok && r.error.find("access violation") != std::string::npos, "a faulting function is caught: " + r.error);
	}
	{
		const auto r = run("Mod/Test/Throws.toml");
		CHECK(!r.ok && r.error.find("boom") != std::string::npos, "a throwing function is caught: " + r.error);
	}
	{
		const auto r = run("Mod/Test/Good Void.toml");
		CHECK(r.ok, "and calls still work after both");
	}

	fs::remove_all(root.parent_path(), ec);
	std::cout << "mfm_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
