#include <time.h>

#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <sys/stat.h>

#include <plugin-support.h>

/* https://docs.obsproject.com/reference-sources */

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
} pngtuber_data;

pngtuber_data* eko_pngtuber_data_create() {
	pngtuber_data* ctx = bzalloc(sizeof(pngtuber_data));
	obs_log(LOG_INFO, "Creating pngtuber_data @ 0x%p", ctx);
	
	ctx->open_open_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->open_closed_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->closed_open_img = bzalloc(sizeof(gs_image_file4_t));
	ctx->closed_closed_img = bzalloc(sizeof(gs_image_file4_t));
	
	ctx->active_img = ctx->open_closed_img;
	
	return ctx;
}

void eko_pngtuber_data_destroy(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Destroying pngtuber_data @ %p", ctx);
	
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
	
	// Free context
	bfree(ctx);
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

static time_t get_modified_timestamp(const char *filename) {
	struct stat stats;
	if (os_stat(filename, &stats) != 0)
		return -1;
	return stats.st_mtime;
}

static const char* eko_pngtuber_get_name(void* type_data) {
	return "Eko's PNGTuber"; // TODO: Replace with localized obs_module_text()
}

static void eko_pngtuber_update(void* data, obs_data_t* settings) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	obs_log(LOG_INFO, "Update 0x%p w/ settings: %s", ctx, obs_data_get_json(settings));
	
	// Initialize an image file helper for each PNGtuber image.
	eko_pngtuber_attempt_load("open_open", ctx->open_open_img, settings);
	eko_pngtuber_attempt_load("open_closed", ctx->open_closed_img, settings);
	eko_pngtuber_attempt_load("closed_open", ctx->closed_open_img, settings);
	eko_pngtuber_attempt_load("closed_closed", ctx->closed_closed_img, settings);
	
	ctx->blink_duration = obs_data_get_double(settings, "blink_duration") / 1000;
	ctx->blink_gap = obs_data_get_double(settings, "blink_gap");
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
	
	if (ctx->open_open_img != NULL && ctx->open_open_img->image3.image2.image.texture != NULL) {
		return ctx->open_open_img->image3.image2.image.cx;
	}
	
	return 0;
}

static uint32_t eko_pngtuber_get_height(void* data) {
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	if (ctx->open_open_img != NULL && ctx->open_open_img->image3.image2.image.texture != NULL) {
		return ctx->open_open_img->image3.image2.image.cy;
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
		ctx->active_img = ctx->closed_closed_img;
	}
	else {
		ctx->active_img = ctx->open_closed_img;
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

static obs_properties_t *eko_pngtuber_source_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_log(LOG_INFO, "Source Properties");

	obs_properties_t *props = obs_properties_create();
	// TODO: Replace display names with localized obs_module_text()
	obs_properties_add_path(props, "open_open", "Eyes Open Mouth Open", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "open_closed", "Eyes Open Mouth Closed", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "closed_open", "Eyes Closed Mouth Open", OBS_PATH_FILE, image_filter, NULL);
	obs_properties_add_path(props, "closed_closed", "Eyes Closed Mouth Closed", OBS_PATH_FILE, image_filter, NULL);
	
	obs_properties_add_float(props, "blink_duration", "Blink Duration (ms)", 10.0, 500.0, 1.0);
	obs_properties_add_float(props, "blink_gap", "Avg Time Between Blinks (s)", 1.0, 30.0, 1.0);
	
	return props;
}

static enum gs_color_space eko_pngtuber_get_color_space(void* data, size_t count, const enum gs_color_space* preferred_spaces) {
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	pngtuber_data* ctx = (pngtuber_data*) data;
	
	return ctx->open_open_img->space;
}

struct obs_source_info eko_pngtuber = {
	.id             = "Eko's PNGtuber",
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