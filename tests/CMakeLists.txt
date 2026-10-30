set(data ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(suite ${CMAKE_CURRENT_SOURCE_DIR}/gif-test-suite)

# Test images and the tolerance of their reference comparison
set(bmp_images rgb24.bmp rgb24_topdown.bmp pal8.bmp pal4.bmp os2_pal8.bmp mono.bmp
	rgb555.bmp rgb565.bmp xrgb32.bmp argb32_v4.bmp)
set(png_images rgb.png rgba.png gray.png palette_trns.png gray16.png rgba16.png interlaced.png)
set(jpeg_images baseline.jpg progressive.jpg gray.jpg cmyk.jpg)
set(gif_images anim_full.gif anim_disposal1.gif anim_disposal2.gif anim_disposal3.gif
	anim_interlaced.gif anim_once.gif still.gif)

set(images)
if(FBVNG_WITH_BMP)
	list(APPEND images ${bmp_images})
endif()
if(FBVNG_WITH_PNG)
	list(APPEND images ${png_images})
endif()
if(FBVNG_WITH_JPEG)
	list(APPEND images ${jpeg_images})
endif()
if(FBVNG_WITH_GIF)
	list(APPEND images ${gif_images})
endif()

add_executable(unit_test unit_test.cpp)
target_link_libraries(unit_test PRIVATE fbvng_core)
add_test(NAME unit COMMAND unit_test)

add_executable(decode_test decode_test.cpp)
target_link_libraries(decode_test PRIVATE fbvng_core)
foreach(image IN LISTS images)
	set(tolerance 0)
	if(image MATCHES "\\.jpg$")
		set(tolerance 2)
	endif()
	add_test(NAME decode/${image}
		COMMAND decode_test ${data}/${image} ${data}/${image}.ref ${tolerance})
endforeach()

list(TRANSFORM images PREPEND ${data}/ OUTPUT_VARIABLE image_paths)

add_executable(robustness_test robustness_test.cpp)
target_link_libraries(robustness_test PRIVATE fbvng_core)
add_test(NAME robustness COMMAND robustness_test ${image_paths})

# https://github.com/peterdn/gif-test-suite, expected frames are PNG
if(FBVNG_WITH_GIF AND FBVNG_WITH_PNG)
	add_executable(gif_suite_test gif_suite_test.cpp)
	target_link_libraries(gif_suite_test PRIVATE fbvng_core)

	set(second 1000,1000,1000,1000)
	set(suite_cases
		"static_nontransparent|0|static_nontransparent"
		"static_transparent|0|static_transparent"
		"animated_noloop|1000,1000,1000,0"
		"animated_loop|${second}"
		"animated_loop_interlaced|${second}"
		"animated_loop_variable_delay|1000,1000,100,1000"
		"animated_transparent_loop|${second}"
		"animated_transparent_restoreprev_loop|${second}"
		"animated_transparent_firstframerestoreprev_loop|${second}"
		"animated_transparent_frame_norestore_loop|${second}"
		"animated_transparent_frame_restorebackground_loop|${second}"
		"animated_transparent_frame_restoreprev_loop|${second}"
		"animated_transparent_loop_frames_out_of_bounds|${second}")
	foreach(case IN LISTS suite_cases)
		string(REPLACE "|" ";" fields "${case}")
		list(GET fields 0 name)
		list(GET fields 1 delays)
		list(LENGTH fields n)
		if(n GREATER 2)
			set(frames ${suite}/frames/${name}.png)
		else()
			set(frames ${suite}/frames/${name}-0.png ${suite}/frames/${name}-1.png
				${suite}/frames/${name}-2.png ${suite}/frames/${name}-3.png)
		endif()
		add_test(NAME gif-suite/${name}
			COMMAND gif_suite_test ${suite}/${name}.gif ${delays} ${frames})
	endforeach()
endif()

# End to end: the viewer on a fake framebuffer, native Linux builds only
if(TARGET fbv AND NOT CMAKE_CROSSCOMPILING)
	add_library(fakefb MODULE fakefb.cpp)
	add_executable(viewer_test viewer_test.cpp)
	target_link_libraries(viewer_test PRIVATE fbvng_core)

	set(viewer_cases)
	if(FBVNG_WITH_BMP)
		list(APPEND viewer_cases "32|rgb24.bmp" "24|rgb24.bmp" "16|rgb24.bmp" "32|pal4.bmp|-k")
	endif()
	if(FBVNG_WITH_PNG)
		list(APPEND viewer_cases "32|rgba.png|-a" "16|rgba.png|-a")
	endif()
	if(FBVNG_WITH_GIF)
		list(APPEND viewer_cases "32|anim_once.gif")
	endif()
	foreach(case IN LISTS viewer_cases)
		string(REPLACE "|" ";" fields "${case}")
		list(GET fields 0 bpp)
		list(GET fields 1 image)
		set(options)
		list(LENGTH fields n)
		if(n GREATER 2)
			list(SUBLIST fields 2 -1 options)
		endif()
		string(JOIN "" suffix ${options})
		add_test(NAME viewer/${bpp}bpp/${image}${suffix}
			COMMAND viewer_test $<TARGET_FILE:fbv> $<TARGET_FILE:fakefb> ${bpp} ${data}/${image} ${options})
	endforeach()
endif()
