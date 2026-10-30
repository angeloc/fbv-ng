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
