cmake_minimum_required(VERSION 3.16)

project(fbv-ng VERSION 2.0.0 DESCRIPTION "Framebuffer image viewer" LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One switch per image format, to build only the decoders a system needs
option(FBVNG_WITH_BMP "BMP support (built in)" ON)
option(FBVNG_WITH_PNG "PNG support (libpng)" ON)
option(FBVNG_WITH_JPEG "JPEG support (libjpeg)" ON)
option(FBVNG_WITH_GIF "GIF support (giflib)" ON)
option(FBVNG_HARDENING "Compiler and linker hardening" ON)
option(FBVNG_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(CheckCXXCompilerFlag)
include(GNUInstallDirs)
if(NOT CMAKE_VERSION VERSION_LESS 3.18)
	include(CheckLinkerFlag)
endif()

# Flags shared by every target. Each one is probed, so any GCC or Clang
# toolchain, native or cross, gets what it supports.
add_library(fbvng_flags INTERFACE)

function(fbvng_add_flags)
	foreach(flag IN LISTS ARGN)
		string(MAKE_C_IDENTIFIER "FBVNG_HAVE${flag}" var)
		check_cxx_compiler_flag(${flag} ${var})
		if(${var})
			target_compile_options(fbvng_flags INTERFACE ${flag})
		endif()
	endforeach()
endfunction()

function(fbvng_add_link_flags)
	if(NOT COMMAND check_linker_flag)
		return()
	endif()
	foreach(flag IN LISTS ARGN)
		string(MAKE_C_IDENTIFIER "FBVNG_HAVE_LD${flag}" var)
		check_linker_flag(CXX "${flag}" ${var})
		if(${var})
			target_link_options(fbvng_flags INTERFACE ${flag})
		endif()
	endforeach()
endfunction()

if(MSVC)
	target_compile_options(fbvng_flags INTERFACE /W4 /permissive-)
else()
	fbvng_add_flags(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
		-Wformat=2 -Wnull-dereference -Wimplicit-fallthrough)
endif()

if(FBVNG_HARDENING AND NOT MSVC)
	include(CheckPIESupported)
	check_pie_supported(LANGUAGES CXX)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	target_compile_definitions(fbvng_flags INTERFACE _GLIBCXX_ASSERTIONS)
	if(NOT FBVNG_SANITIZE)
		# Replaces any level the toolchain sets, it needs optimization.
		target_compile_options(fbvng_flags INTERFACE
			$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2>)
	endif()
	# -fcf-protection is x86 only and drops out elsewhere
	fbvng_add_flags(-fstack-protector-strong -fstack-clash-protection -fcf-protection)
	if(NOT APPLE AND NOT WIN32)
		fbvng_add_link_flags(LINKER:-z,relro LINKER:-z,now LINKER:-z,noexecstack)
	endif()
endif()

if(FBVNG_SANITIZE)
	target_compile_options(fbvng_flags INTERFACE
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
	target_link_options(fbvng_flags INTERFACE -fsanitize=address,undefined)
endif()

if(NOT FBVNG_WITH_BMP AND NOT FBVNG_WITH_PNG AND NOT FBVNG_WITH_JPEG AND NOT FBVNG_WITH_GIF)
	message(FATAL_ERROR "fbv-ng: enable at least one FBVNG_WITH_<format> option")
endif()

# Decoders and transforms, used by the viewer, the tests and the fuzzer.
# They only need a POSIX C++20 toolchain.
add_library(fbvng_core STATIC
	src/decode.cpp
	src/image.cpp
	src/transform.cpp)
target_include_directories(fbvng_core PUBLIC src)
target_link_libraries(fbvng_core PUBLIC fbvng_flags)

if(FBVNG_WITH_BMP)
	target_sources(fbvng_core PRIVATE src/bmp.cpp)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_BMP)
endif()

if(FBVNG_WITH_PNG)
	find_package(PNG 1.6 REQUIRED)
	target_sources(fbvng_core PRIVATE src/png.cpp)
	target_link_libraries(fbvng_core PRIVATE PNG::PNG)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_PNG)
endif()

if(FBVNG_WITH_JPEG)
	find_package(JPEG REQUIRED)
	target_sources(fbvng_core PRIVATE src/jpeg.cpp)
	target_link_libraries(fbvng_core PRIVATE JPEG::JPEG)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_JPEG)
endif()

if(FBVNG_WITH_GIF)
	find_package(GIF 5.1 REQUIRED)
	target_sources(fbvng_core PRIVATE src/gif.cpp)
	target_link_libraries(fbvng_core PRIVATE GIF::GIF)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_GIF)
endif()

set(formats)
foreach(format BMP PNG JPEG GIF)
	if(FBVNG_WITH_${format})
		list(APPEND formats ${format})
	endif()
endforeach()
string(REPLACE ";" " " formats "${formats}")
message(STATUS "fbv-ng: image formats: ${formats}")

# The viewer drives the Linux framebuffer and virtual terminals.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Same command line as fbv, so it installs under the same name
	add_executable(fbv
		src/framebuffer.cpp
		src/main.cpp
		src/terminal.cpp
		src/viewer.cpp)
	target_link_libraries(fbv PRIVATE fbvng_core)

	install(TARGETS fbv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(FILES fbv.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
else()
	message(STATUS "fbv-ng: ${CMAKE_SYSTEM_NAME} has no Linux framebuffer, building the decoders only")
endif()
