#include <time.h>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/image-file.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <sys/stat.h>

#include <plugin-support.h>

/* https://docs.obsproject.com/reference-sources */

// TODO: Two double-destroys occur on shutdown, according to verbose logs. Try using refcounted objects correctly.
// TODO: Add UNUSED_PARAMETER() calls where appropriate.

/* ---- pngtuber_data ---- */

typedef struct pngtuber_data {
	obs_source_t* source;
	
	// Time since last blink is used to calculate when to 
	double last_blink;
	double next_blink;
	
	// Controls blinking.
	double blink_duration;
	double blink_gap;
	
	// The file paths of the four PNGtuber frames.
	const char* open_open_fn;
	const char* open_closed_fn;
	const char* closed_open_fn;
	const char* closed_closed_fn;
	
	// The four PNGtuber frames.
	gs_image_file4_t* open_open_img;
	gs_image_file4_t* open_closed_img;
	gs_image_file4_t* closed_open_img;
	gs_image_file4_t* closed_closed_img;
	
	// The currently visible PNGtuber frame.
	gs_image_file4_t* active_img;
	
	// The audio source that controls the mouth movement.
	obs_weak_source_t *audio_source;
	
	// Whether the user is counted as speaking currently.
	bool is_speaking;
} pngtuber_data;

pngtuber_data* eko_pngtuber_data_create() {
	pngtuber_data* ctx = bzalloc(sizeof(pngtuber_data));
	obs_log(LOG_INFO, "Creating pngtuber_data @ 0x%p", ctx);
	
	ctx->open_open_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->open_closed_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->closed_open_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->closed_closed_img = bzalloc(sizeof(gs_image_file4_t));
	
	ctx->active_img = ctx->open_closed_img;
	
	ctx->audio_source = NULL;
	ctx->is_speaking = false;
	
	return ctx;
}

void eko_pngtuber_data_destroy(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Destroying pngtuber_data @ %p", ctx);
	
	// Free image objects.
	obs_enter_graphics();
	if (ctx->open_open_img != NULL) {
		gs_image_file4_free(ctx->open_open_img);
		bfree(ctx->open_open_img);
	}
	if (ctx->open_closed_img != NULL) {
		gs_image_file4_free(ctx->open_closed_img);
		bfree(ctx->open_closed_img);
	}
	if (ctx->closed_open_img != NULL) {
		gs_image_file4_free(ctx->closed_open_img);
		bfree(ctx->closed_open_img);
	}
	if (ctx->closed_closed_img != NULL) {
		gs_image_file4_free(ctx->closed_closed_img);
		bfree(ctx->closed_closed_img);
	}
	obs_leave_graphics();
	
	// Release audio source.
	if (ctx->audio_source != NULL) {
		obs_weak_source_release(ctx->audio_source);
	}
	
	// Free context
	bfree(ctx);
	
	obs_log(LOG_INFO, "Destruction Complete.", ctx);
}

// Retrieves setting_name from settings and attempts to load it as an image, interpreting it as a file path.
// If successful, loads the image into the passed image object.
void eko_pngtuber_attempt_load(const char* setting_name, gs_image_file4_t* img, obs_data_t* settings) {
	const char* fn = obs_data_get_string(settings, setting_name);
	
	obs_log(LOG_INFO, "Considering loading \"%s\" to 0x%p...", fn, img);
	if (strcmp("", fn) != 0) {
		gs_image_file4_init(img, fn, GS_IMAGE_ALPHA_PREMULTIPLY_SRGB);
		if (img == NULL) {
			obs_log(LOG_INFO, "Init Failed.");
		}
		else {
			obs_log(LOG_INFO, "Init Successful");
			
			if (img->image3.image2.image.loaded) {
				obs_enter_graphics();
				gs_image_file4_init_texture(img);
				obs_leave_graphics();
				
				obs_log(LOG_INFO, "Load Successful to 0x%p", img->image3.image2.image.texture);
			}
			else {
				obs_log(LOG_INFO, "Load Failed");
			}
		}
	}
}

/* ---- source definition ---- */

static const char *image_filter =
#ifdef _WIN32
	"All formats (*.bmp *.tga *.png *.jpeg *.jpg *.jxr *.gif *.psd *.webp);;"
#else
	"All formats (*.bmp *.tga *.png *.jpeg *.jpg *.gif *.psd *.webp);;"
#endif
	"BMP Files (*.bmp);;"
	"Targa Files (*.tga);;"
	"PNG Files (*.png);;"
	"JPEG Files (*.jpeg *.jpg);;"
#ifdef _WIN32
	"JXR Files (*.jxr);;"
#endif
	"GIF Files (*.gif);;"
	"PSD Files (*.psd);;"
	"WebP Files (*.webp);;"
	"All Files (*.*)";

static const char* eko_pngtuber_get_name(void* type_data) {
	return "Eko's PNGTuber"; // TODO: Replace w/ localized string.
}

// Callback to ensure that the reference to audio sources are released as necessary.
static void on_audio_source_destroy(void *data, calldata_t *call_data) {
	UNUSED_PARAMETER(call_data);
	pngtuber_data* ctx = data;

	obs_weak_source_release(ctx->audio_source);
	ctx->audio_source = NULL;
}

// Callback to set flag when the user is speaking.
void on_audio_source_capture(void *data, obs_source_t *source, const struct audio_data *audio_block, bool muted) {
	pngtuber_data* ctx = data;
	ctx->is_speaking = false;
	
	if (muted) return;

	float* samples = (float*) audio_block->data[0];
	if (samples == NULL) return;
	
	// Compute the average of all audio samples provided and convert those values to decibels.
	float sum = 0.0;
	for (size_t i = 0; i < audio_block->frames; i++) {
		float sample = samples[i];
		sum += sample * sample;
	}
	
	double audio_level = (double) obs_mul_to_db(sqrtf(sum / audio_block->frames));
	
	if (audio_level > -40) {
		ctx->is_speaking = true;
	}
}

static void eko_pngtuber_update(void* data, obs_data_t* settings) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Update 0x%p w/ settings: %s", ctx, obs_data_get_json(settings));
	
	// Initialize an image file helper for each PNGtuber image.
	// TODO: Only reload if the filenames have changed?
	eko_pngtuber_attempt_load("open_open", ctx->open_open_img, settings);
	eko_pngtuber_attempt_load("open_closed", ctx->open_closed_img, settings);
	eko_pngtuber_attempt_load("closed_open", ctx->closed_open_img, settings);
	eko_pngtuber_attempt_load("closed_closed", ctx->closed_closed_img, settings);
	
	ctx->blink_duration = obs_data_get_double(settings, "blink_duration") / 1000;
	ctx->blink_gap = obs_data_get_double(settings, "blink_gap");
	
	// Update audio source.
	// TODO: If new source is NULL, replace settings value with ""?
	obs_source_t* new_audio_src = obs_get_source_by_name(obs_data_get_string(settings, "audio_src"));
	obs_source_t* current_audio_src = NULL;
	if (ctx->audio_source != NULL) {
		current_audio_src = obs_weak_source_get_source(ctx->audio_source);
	}
	
	obs_log(LOG_INFO, "Got audio src @ 0x%p, considering replacing 0x%p", new_audio_src, current_audio_src);
	if (new_audio_src != current_audio_src) {
		signal_handler_t *sig_handler;
		
		// Destroy callbacks ensure that our weak reference never refers to an invalid audio source.
		// Remove current audio source destroy & capture callbacks.
		if (current_audio_src != NULL) {
			sig_handler = obs_source_get_signal_handler(current_audio_src);
			signal_handler_disconnect(sig_handler, "destroy", on_audio_source_destroy, ctx);

			obs_source_remove_audio_capture_callback(current_audio_src, on_audio_source_capture, ctx);

			obs_weak_source_release(ctx->audio_source);
		}
		
		// Add new audio source destroy & capture callbacks.
		// TODO: Do we need to check whether new_audio_src is NULL?
		obs_log(LOG_INFO, "Setting callbacks...");
		sig_handler = obs_source_get_signal_handler(new_audio_src);
		signal_handler_connect(sig_handler, "destroy", on_audio_source_destroy, ctx);

		obs_source_add_audio_capture_callback(new_audio_src, on_audio_source_capture, ctx);

		ctx->audio_source = obs_source_get_weak_source(new_audio_src);
	}
	
	// Release strong references.
	obs_source_release(new_audio_src);
	if (current_audio_src != NULL) {
		obs_source_release(current_audio_src);
	}
}

static void eko_pngtuber_get_defaults(obs_data_t* settings) {
	obs_log(LOG_INFO, "Get defaults w/ settings: %s", obs_data_get_json(settings));
}

static void* eko_pngtuber_create(obs_data_t* settings, obs_source_t* source) {
	pngtuber_data* ctx = eko_pngtuber_data_create();
	obs_log(LOG_INFO, "Create w/ settings: %s", obs_data_get_json(settings));
	
	ctx->source = source;
	obs_data_set_default_double(settings, "blink_duration", 100);
	obs_data_set_default_double(settings, "blink_gap", 5);
	
	double cur_time = (double) clock() / CLOCKS_PER_SEC;
	ctx->last_blink = cur_time;
	ctx->next_blink = cur_time + ctx->blink_duration;
	
	eko_pngtuber_update((void*) ctx, settings);
	return ctx;
}
static void eko_pngtuber_destroy(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Destroy");
	
	eko_pngtuber_data_destroy(ctx);
}

static void eko_pngtuber_show(void *data)
{
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Show");
}

static void eko_pngtuber_hide(void *data)
{
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Hide");
}

static void eko_pngtuber_activate(void *data)
{
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Activate");
}

static uint32_t eko_pngtuber_get_width(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	if (ctx->active_img != NULL && ctx->active_img->image3.image2.image.texture != NULL) {
		return ctx->active_img->image3.image2.image.cx;
	}
	
	return 0;
}

static uint32_t eko_pngtuber_get_height(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	if (ctx->active_img != NULL && ctx->active_img->image3.image2.image.texture != NULL) {
		return ctx->active_img->image3.image2.image.cy;
	}
	
	return 0;
}

static void eko_pngtuber_video_render(void* data, gs_effect_t *effect) {
	// obs_log(LOG_INFO, "Render");
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	double cur_time = (double) clock() / CLOCKS_PER_SEC;
	
	if (cur_time > ctx->next_blink) {
		ctx->next_blink = cur_time + ctx->blink_gap;
		ctx->last_blink = cur_time;
	}
	
	bool is_blinking = false;
	if (cur_time < ctx->last_blink + ctx->blink_duration) {
		is_blinking = true;
	}
	
	if (is_blinking) {
		if (ctx->is_speaking) {
			ctx->active_img = ctx->closed_open_img;
		}
		else {
			ctx->active_img = ctx->closed_closed_img;
		}
	}
	else {
		if (ctx->is_speaking) {
			ctx->active_img = ctx->open_open_img;
		}
		else {
			ctx->active_img = ctx->open_closed_img;
		}
	}

	gs_image_file4_t *const img = ctx->active_img;
	if (img == NULL || img->image3.image2.image.texture == NULL)
		return;
	
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	gs_eparam_t *const param = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(param, img->image3.image2.image.texture);

	gs_draw_sprite(img->image3.image2.image.texture, 0, img->image3.image2.image.cx, img->image3.image2.image.cy);

	gs_blend_state_pop();

	gs_enable_framebuffer_srgb(previous);
}

// Callback used for getting a list of audio sources.
static bool enum_audio_sources(void *data, obs_source_t *source) {
	obs_property_t *sources_list = data;
	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_AUDIO) != 0) {
		const char *name = obs_source_get_name(source);
		obs_property_list_add_string(sources_list, name, name);
	}
	
	return true;
}

static obs_properties_t *eko_pngtuber_source_properties(void *data) {
	UNUSED_PARAMETER(data);
	obs_log(LOG_INFO, "Source Properties");

	obs_properties_t *props = obs_properties_create();
	// TODO: Replace display names with localized obs_module_text()
	obs_properties_add_path(props, "open_open", "Eyes Open Mouth Open", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "open_closed", "Eyes Open Mouth Closed", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "closed_open", "Eyes Closed Mouth Open", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "closed_closed", "Eyes Closed Mouth Closed", OBS_PATH_FILE, image_filter, NULL);
	
	obs_property_t* sources = obs_properties_add_list(props, "audio_src", "Audio Source", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, sources);
	
	obs_properties_add_float(props, "blink_duration", "Blink Duration (ms)", 10.0, 1000.0, 1);
	obs_properties_add_float(props, "blink_gap", "Avg Time Between Blinks (s)", 1.0, 30.0, 0.1);
	
	return props;
}

static enum gs_color_space eko_pngtuber_get_color_space(void* data, size_t count, const enum gs_color_space* preferred_spaces) {
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	return ctx->active_img->space;
}

struct obs_source_info eko_pngtuber = {
	.id             = "ekos_pngtuber",
	.type           = OBS_SOURCE_TYPE_INPUT,
	.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name       = eko_pngtuber_get_name,
	.create         = eko_pngtuber_create,
	.destroy        = eko_pngtuber_destroy,
	.update         = eko_pngtuber_update,
	.get_defaults   = eko_pngtuber_get_defaults,
	.show           = eko_pngtuber_show,
	.hide           = eko_pngtuber_hide,
	.get_width      = eko_pngtuber_get_width,
	.get_height     = eko_pngtuber_get_height,
	.video_render   = eko_pngtuber_video_render,
//	.missing_files  = image_source_missingfiles,
	.get_properties = eko_pngtuber_source_properties,
	.icon_type      = OBS_ICON_TYPE_IMAGE,
	.activate       = eko_pngtuber_activate,
	.video_get_color_space = eko_pngtuber_get_color_space
};