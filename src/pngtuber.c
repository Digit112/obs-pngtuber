
/* https://docs.obsproject.com/reference-sources */

const char* eko_pngtuber_get_name(void* type_data) {
	return "Eko PNGTuber";
}

void* eko_pngtuber_create(obs_data_t* settings, obs_source_t* source) {
	return NULL;
}

void eko_pngtuber_destroy(void* data) {
	
}

void eko_pngtuber_update(void* data, obs_data_t* settings) {
	
}

void eko_pngtuber_video_render(void* data, gs_effect_t *effect) {
	
}

uint32_t eko_pngtuber_get_width(void* data) {
	return 400;
}

uint32_t eko_pngtuber_get_height(void* data) {
	return 400;
}

struct obs_source_info pngtuber {
	.id           = "Eko PNGtuber",
	.type         = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW, // Try removing OBS_SOURCE_CUSTOM_DRAW
	.get_name     = eko_pngtuber_get_name,
	.create       = eko_pngtuber_create,
	.destroy      = eko_pngtuber_destroy,
	.update       = eko_pngtuber_update,
	.video_render = eko_pngtuber_video_render,
	.get_width    = eko_pngtuber_get_width,
	.get_height   = eko_pngtuber_get_height
};