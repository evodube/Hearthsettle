cmake_minimum_required(VERSION 3.24)

project(
	Hearthsettle
	VERSION 1.2.1
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# CommonLibSSE-NG (git submodule pinned to a release tag, so CI links its prebuilt
# library instead of compiling it). All runtimes stay on: one DLL for SE 1.5.97 and AE.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG commonlib EXCLUDE_FROM_ALL)

add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "Hearthsettle"
	AUTHOR "evodube"
	USE_ADDRESS_LIBRARY
	SOURCES
		src/main.cpp
		src/Settler.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
endif()

# Lay out a ready-to-install mod folder: dist/Hearthsettle/SKSE/Plugins/
set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/Hearthsettle/SKSE/Plugins")
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/dist-files/Hearthsettle.ini" "${DIST_DIR}"
	VERBATIM
)
