cmake_minimum_required(VERSION 3.16)

project(fbv-ng VERSION 0.1.0 DESCRIPTION "Framebuffer image viewer" LANGUAGES CXX)

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
option(FBVNG_FUZZ "Build the libFuzzer target (clang)" OFF)

include(CheckCXXCompilerFlag)
include(CTest)
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

# An enabled image format needs its library. Each lookup is recorded for the recap
# printed at the end of the configuration, which then stops with an error if an
# enabled format misses its library (or has a too old one), so a single run shows
# everything that is missing.
# FindPNG/FindJPEG/FindGIF only set the Release location of their imported
# targets: use it for the other optimized build types too (CMP0111 warning).
set(fbvng_missing)
function(fbvng_require_library format package target what)
	find_package(${package} ${ARGN} QUIET)
	if(${package}_FOUND AND TARGET ${target})
		set_target_properties(${target} PROPERTIES
			MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;"
			MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;Release;")
		foreach(prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION IMPORTED_LOCATION_DEBUG)
			get_target_property(location ${target} ${prop})
			if(location)
				break()
			endif()
		endforeach()
		set(version "${${package}_VERSION_STRING}")
		if(NOT version)
			set(version "${${package}_VERSION}")
		endif()
		set(FBVNG_LIB_${format} "${what} ${version} (${location})" PARENT_SCOPE)
		set(FBVNG_FOUND_${format} TRUE PARENT_SCOPE)
	else()
		if(ARGN)
			set(what "${what} ${ARGN} or later")
		endif()
		set(FBVNG_LIB_${format} "MISSING: ${what}" PARENT_SCOPE)
		set(FBVNG_FOUND_${format} FALSE PARENT_SCOPE)
		set(fbvng_missing ${fbvng_missing} ${format} PARENT_SCOPE)
	endif()
endfunction()

if(FBVNG_WITH_BMP)
	target_sources(fbvng_core PRIVATE src/bmp.cpp)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_BMP)
endif()

if(FBVNG_WITH_PNG)
	fbvng_require_library(PNG PNG PNG::PNG "libpng" 1.6)
endif()
if(FBVNG_WITH_PNG AND FBVNG_FOUND_PNG)
	target_sources(fbvng_core PRIVATE src/png.cpp)
	target_link_libraries(fbvng_core PRIVATE PNG::PNG)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_PNG)
endif()

if(FBVNG_WITH_JPEG)
	fbvng_require_library(JPEG JPEG JPEG::JPEG "libjpeg")
endif()
if(FBVNG_WITH_JPEG AND FBVNG_FOUND_JPEG)
	target_sources(fbvng_core PRIVATE src/jpeg.cpp)
	target_link_libraries(fbvng_core PRIVATE JPEG::JPEG)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_JPEG)
endif()

if(FBVNG_WITH_GIF)
	fbvng_require_library(GIF GIF GIF::GIF "giflib" 5.1)
endif()
if(FBVNG_WITH_GIF AND FBVNG_FOUND_GIF)
	target_sources(fbvng_core PRIVATE src/gif.cpp)
	target_link_libraries(fbvng_core PRIVATE GIF::GIF)
	target_compile_definitions(fbvng_core PUBLIC FBVNG_HAVE_GIF)
endif()

# The viewer drives the Linux framebuffer and virtual terminals.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Same command line as fbv, so it installs under the same name
	add_executable(fbv
		src/control.cpp
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

# Test programs run through CMAKE_CROSSCOMPILING_EMULATOR when cross building.
if(BUILD_TESTING)
	add_subdirectory(tests)
endif()

if(FBVNG_FUZZ)
	add_executable(fbv-ng-fuzz fuzz/decode_fuzzer.cpp)
	target_compile_options(fbv-ng-fuzz PRIVATE -fsanitize=fuzzer)
	target_link_options(fbv-ng-fuzz PRIVATE -fsanitize=fuzzer)
	target_link_libraries(fbv-ng-fuzz PRIVATE fbvng_core)
endif()


# Recap of the configuration: options, and the library each image format uses
set(FBVNG_LIB_BMP "built in")
function(fbvng_recap_line name value detail)
	string(LENGTH "${name}" n)
	math(EXPR pad "18 - ${n}")
	string(REPEAT " " ${pad} spaces)
	string(LENGTH "${value}" n)
	math(EXPR pad "6 - ${n}")
	string(REPEAT " " ${pad} spaces2)
	message(STATUS "  ${name}${spaces}${value}${spaces2}${detail}")
endfunction()
message(STATUS "")
message(STATUS "fbv-ng ${PROJECT_VERSION} configuration")
fbvng_recap_line("Option" "Value" "Library")
foreach(format BMP PNG JPEG GIF)
	if(FBVNG_WITH_${format})
		fbvng_recap_line("FBVNG_WITH_${format}" "ON" "${FBVNG_LIB_${format}}")
	else()
		fbvng_recap_line("FBVNG_WITH_${format}" "OFF" "not enabled")
	endif()
endforeach()
foreach(option FBVNG_HARDENING FBVNG_SANITIZE FBVNG_FUZZ BUILD_TESTING)
	if(${option})
		fbvng_recap_line("${option}" "ON" "")
	else()
		fbvng_recap_line("${option}" "OFF" "")
	endif()
endforeach()
fbvng_recap_line("Build type" "" "${CMAKE_BUILD_TYPE}")
message(STATUS "")

if(fbvng_missing)
	set(options)
	foreach(format ${fbvng_missing})
		string(APPEND options " -DFBVNG_WITH_${format}=OFF")
	endforeach()
	list(JOIN fbvng_missing ", " formats)
	message(FATAL_ERROR "Enabled image formats without their library: ${formats} (see the recap above).\n"
		"Install the missing libraries (library and headers), or disable the formats with:${options}")
endif()
