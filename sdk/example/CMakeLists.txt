cmake_minimum_required(VERSION 3.21)

# AMF Example - a Skyrim SKSE plugin with two pages in the Apocrypha Menu Framework, built against Dear ImGui
# 1.90.8 docking (the framework's own) and drawn through AMF.h. With VCPKG_ROOT set:
#     cmake --preset build-relwithdebinfo-se-only
#     cmake --build build/relwithdebinfo-se-only
project(
	AMFExample
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(spdlog REQUIRED CONFIG)
find_package(directxtk CONFIG REQUIRED)  # CommonLibSSE-NG links Microsoft::DirectXTK
find_package(imgui CONFIG REQUIRED)      # the vcpkg port with docking-experimental - see vcpkg.json

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES main.cpp)

# AMF.h and PreciseSlider.h
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		spdlog::spdlog_header_only
		imgui::imgui
)

target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
	file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" PROJECT_NATIVE_DIR)
	target_compile_options(${PROJECT_NAME} PRIVATE "/d1trimfile:${PROJECT_NATIVE_DIR}")
	target_link_options(${PROJECT_NAME} PRIVATE /PDBALTPATH:%_PDB%)
endif()

target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
