project(eko_pngtuber)

set(
	eko_pngtuber_SOURCES
	plugin-support.h
	plugin-support.c.in
	plugin-main.c
	pngtuber.c
)

configure_file(plugin-support.c.in plugin-support.c)

add_library(eko_pngtuber MODULE)

target_sources(eko_pngtuber
	PRIVATE plugin-support.h
	PRIVATE plugin-support.c.in
	PRIVATE plugin-main.c
	PRIVATE pngtuber.c
)

target_link_libraries(eko_pngtuber libobs)

# cmake-format: off
#set_target_properties_obs(image-source PROPERTIES FOLDER plugins PREFIX "")