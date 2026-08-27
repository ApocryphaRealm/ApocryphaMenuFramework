cmake_minimum_required(VERSION 3.21)

########################################################################################################################
## Apocrypha Menu Framework
##
## An ORIGINAL SKSE menu framework that embeds Dear ImGui (MIT). It is deliberately NOT a fork of
## SKSE Menu Framework: that project's DLL is All Rights Reserved and its source is LGPL-2.1, and
## this codebase stays permissively licensed, so neither may inform the code here (see
## 4. plans\ApocryphaMenuFramework\plan.md for the full rails).
##
## The build shape below is copied from this project's proven repos (Dragon's Eye Minimap et al.),
## including the two hard-won blocks: stated debug info (rule 43) and stripped build paths
## (rule 45). Do not "clean up" either - each exists because its absence shipped a broken release.
########################################################################################################################

project(
	ApocryphaMenuFramework
	VERSION 1.1.1
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

# CONFIGURE_DEPENDS so that adding a source file re-runs the glob on the next build
# instead of silently leaving the new file out of the link.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "source/*.cpp")

source_group(
	TREE
		${CMAKE_CURRENT_SOURCE_DIR}
	FILES
		${headers}
		${sources}
)

########################################################################################################################
## Configure target DLL
########################################################################################################################

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(spdlog REQUIRED CONFIG)
# Not used directly by this project - CommonLibSSE-NG's own link INTERFACE names
# Microsoft::DirectXTK, so the consumer must resolve the target or configure fails at generate.
# (Removed once as "DEM-specific"; it is not.)
find_package(directxtk CONFIG REQUIRED)

# Dear ImGui via the vcpkg port with its binding features - the survey's recommendation, and the
# pattern Wheeler ships (per its own vcpkg.json). Configure imgui through vcpkg FEATURES, never
# via IMGUI_USER_CONFIG or consumer-side compile definitions: the port patches imconfig.h, and a
# consumer-side define like IMGUI_DISABLE_OBSOLETE_FUNCTIONS gates DATA MEMBERS - a silent ABI
# break against the library vcpkg already compiled (survey Section 5.2). MIT licence text ships
# verbatim with the package at release time.
find_package(imgui CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources})
add_library("${PROJECT_NAME}::${PROJECT_NAME}" ALIAS "${PROJECT_NAME}")

########################################################################################################################
## Debug symbols (CLAUDE.md rule 43) - stated on the target, never left to a default
########################################################################################################################
#
# The preset chain leaves CMAKE_CXX_FLAGS_RELWITHDEBINFO EMPTY and the link flags without /DEBUG,
# so RelWithDebInfo would produce a DLL with NO CodeView record and a stale PDB beside it. Caught
# on IshSoulsToPerks 2026-08-26, only by the packaging script's pairing check. /DEBUG implies
# /OPT:NOREF, so /OPT:REF and /OPT:ICF are restated to keep Release-grade linking.
if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)

	# Rule 45: never ship the build machine's absolute paths inside the DLL. /PDBALTPATH records
	# only the PDB filename; /d1trimfile strips the source prefix from __FILE__ at compile time.
	# The prefix is native-form with NO trailing separator (a trailing backslash would escape the
	# closing quote and mangle the argument).
	file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" PROJECT_NATIVE_DIR)
	target_compile_options(${PROJECT_NAME} PRIVATE "/d1trimfile:${PROJECT_NATIVE_DIR}")
	target_link_options(${PROJECT_NAME} PRIVATE /PDBALTPATH:%_PDB%)
endif()

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		spdlog::spdlog_header_only
		imgui::imgui
)

if (MSVC)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>"
	)
endif()

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		include/PCH.h
)
