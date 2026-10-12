cmake_minimum_required(VERSION 3.21)

########################################################################################################################
## AMF-Host-OAR - the bridge that hosts Open Animation Replacer's live menu inside Apocrypha Menu Framework's page
## (next version after 2.1.9; 4. plans\amf-imgui-hosting\FINDINGS.md, method b1 "snap-docking"). GPL-3.0-or-later.
##
## A SEPARATE DLL so AMF core holds no other Dear ImGui: this one is built against Dear ImGui 1.89.6 exactly as Open
## Animation Replacer builds it (vcpkg's imgui port at 1.89.6, feature dx11-binding, stock imconfig, with
## IMGUI_DEFINE_MATH_OPERATORS as OAR's PCH.h defines it - OAR's vcpkg.json and CMakeLists at its 3.0.2 and 3.2.1 tags),
## because it reads and moves a window in OAR's own ImGuiContext and the struct layouts must be OAR's.
##
## One build per game line, like AMF itself: CommonLibSSE-NG's SKSE::Init and plugin declaration differ between the
## SE/AE 1.6 line (NG 3.7 - cmake/ports) and the Skyrim 1.7 line (NG 7.2 - cmake/ports-17, AddressLibraryV5), so one
## DLL cannot serve both. The bridge itself uses no game address. build.bat builds both (preset se and preset 17).
########################################################################################################################

# The bridge ships inside AMF's package and carries AMF's version: read from AMF's own CMakeLists (the one place
# version-gate writes it), and the configure re-runs when that file changes.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt" AMF_VERSION_LINE REGEX "^[ \t]*VERSION[ \t]+[0-9]+[.][0-9]+[.][0-9]+[ \t]*$" LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" AMF_VERSION "${AMF_VERSION_LINE}")
if (NOT AMF_VERSION)
	message(FATAL_ERROR "AMF's version could not be read from ../../CMakeLists.txt")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt")

project(
	AMFHostOAR
	VERSION ${AMF_VERSION}
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(spdlog REQUIRED CONFIG)
# CommonLibSSE-NG's link interface names Microsoft::DirectXTK (as in AMF's CMakeLists)
find_package(directxtk CONFIG REQUIRED)
# Dear ImGui 1.89.6 - Open Animation Replacer's pin (vcpkg.json overrides), NOT AMF's 1.90.8
find_package(imgui CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME} NAME "AMF-Host-OAR" AUTHOR "ApocryphaRealm" SOURCES source/main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AMF-Host-OAR")
target_precompile_headers(${PROJECT_NAME} PRIVATE source/PCH.h)
# AMF's version resource template: FileVersion = AMF's version, so the release tools' version check reads the bridge as AMF's
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

set(AMF_RUNTIME_LINE "1" CACHE STRING "Build line: 1 (SE/AE 1.6) or 17 (Skyrim 1.7.x)")
target_compile_definitions(${PROJECT_NAME} PRIVATE AMF_RUNTIME_LINE=${AMF_RUNTIME_LINE} IMGUI_DEFINE_MATH_OPERATORS)

# AMF's public API (AMF/API.h - the host bridge surface) and Open Animation Replacer's vendored UI API header (include/OAR)
get_filename_component(AMF_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_include_directories(${PROJECT_NAME} PRIVATE "${AMF_REPO_DIR}/include")

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		spdlog::spdlog_header_only
		imgui::imgui
		Version.lib   # GetFileVersionInfoW - OAR's version
		Bcrypt.lib    # BCrypt SHA-256 - OAR's file hash for the allowlist
		Psapi.lib     # GetModuleInformation - OAR's image range
)

if (MSVC)
	# rule 43: a PDB beside the DLL, with Release-grade linking
	target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO)
	# rule 45: no build-machine paths - trim the whole repository (this folder AND ../../include) from __FILE__, and record
	# only the PDB's file name. No trailing separator on the prefix.
	file(TO_NATIVE_PATH "${AMF_REPO_DIR}" AMF_REPO_NATIVE_DIR)
	target_compile_options(${PROJECT_NAME} PRIVATE "/d1trimfile:${AMF_REPO_NATIVE_DIR}")
	target_link_options(${PROJECT_NAME} PRIVATE /PDBALTPATH:%_PDB%)
endif()

# The bridge goes beside AMF's DLL in AMF's own build tree of the same line: that is where the packaging (tools/build-fomod.py,
# and the pipeline's repack-fomod.ps1 through fomod/extra-line-dlls.txt) and the package gate's lines-match-build-tree row
# take each line's DLLs from.
if (AMF_RUNTIME_LINE STREQUAL "17")
	set(AMF_LINE_BUILD_DIR "${AMF_REPO_DIR}/build/relwithdebinfo-17")
else()
	set(AMF_LINE_BUILD_DIR "${AMF_REPO_DIR}/build/relwithdebinfo-se-only")
endif()
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${AMF_LINE_BUILD_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${AMF_LINE_BUILD_DIR}"
	COMMENT "AMF-Host-OAR -> ${AMF_LINE_BUILD_DIR}"
)
