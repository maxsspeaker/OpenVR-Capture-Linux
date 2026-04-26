//
// OpenVR Capture (Linux/OpenGL)
// Linux port of the OBS OpenVR Capture plugin
//

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/dstr.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <GL/gl.h>
#include <openvr.h>

static bool init_inprog = false;

static std::chrono::steady_clock::time_point last_init_time =
	std::chrono::steady_clock::now();
static std::chrono::steady_clock::time_point last_init_timeBUFFER =
	std::chrono::steady_clock::now();

static constexpr std::chrono::milliseconds retry_delay{8};
static constexpr std::chrono::milliseconds retry_delayBUFFER{500};

#define blog(log_level, message, ...) \
	blog(log_level, "[linux_openvr] " message, ##__VA_ARGS__)

#define debug(message, ...)                                                    \
	blog(LOG_DEBUG, "[%s] " message, obs_source_get_name(context->source), \
	     ##__VA_ARGS__)
#define info(message, ...)                                                     \
	blog(LOG_INFO, "[%s] " message, obs_source_get_name(context->source), \
	     ##__VA_ARGS__)
#define warn(message, ...)                                                     \
	blog(LOG_WARNING, "[%s] " message, obs_source_get_name(context->source), \
	     ##__VA_ARGS__)

struct linux_openvr {
	obs_source_t *source = nullptr;

	bool righteye = true;
	double active_aspect_ratio = 16.0 / 9.0;
	bool ar_crop = false;

	uint32_t lastFrame = 0;

	gs_texture_t *texture = nullptr;

	unsigned int device_width = 0;
	unsigned int device_height = 0;

	unsigned int x = 0;
	unsigned int y = 0;
	unsigned int width = 100;
	unsigned int height = 100;

	double scale_factor = 1.0;
	int x_offset = 0;
	int y_offset = 0;

	bool initialized = false;
	bool active = false;

	vr::glUInt_t mirrorTex = 0;
	vr::glSharedTextureHandle_t mirrorSharedHandle = 0;
	bool mirror_acquired = false;

	std::vector<uint8_t> full_buffer;
	std::vector<uint8_t> crop_buffer;
};

static void destroy_obs_texture(gs_texture_t **texture)
{
	if (texture && *texture) {
		obs_enter_graphics();
		gs_texture_destroy(*texture);
		obs_leave_graphics();
		*texture = nullptr;
	}
}

static void release_mirror_texture(linux_openvr *context)
{
	if (context->mirror_acquired && vr::VRCompositor()) {
		vr::VRCompositor()->ReleaseSharedGLTexture(context->mirrorTex,
							  context->mirrorSharedHandle);
	}

	context->mirrorTex = 0;
	context->mirrorSharedHandle = 0;
	context->mirror_acquired = false;
}

static void copy_crop_rgba(const uint8_t *src, unsigned int src_w,
			   unsigned int src_h, unsigned int x,
			   unsigned int y, unsigned int w,
			   unsigned int h, std::vector<uint8_t> &dst)
{
	(void)src_h;

	dst.resize(static_cast<size_t>(w) * h * 4);

	for (unsigned int row = 0; row < h; ++row) {
		const uint8_t *src_row =
			src + (static_cast<size_t>(y + row) * src_w + x) * 4;
		uint8_t *dst_row = dst.data() + static_cast<size_t>(row) * w * 4;
		std::memcpy(dst_row, src_row, static_cast<size_t>(w) * 4);
	}
}

static bool linux_openvr_init(void *data, bool forced = true)
{
	UNUSED_PARAMETER(forced);

	linux_openvr *context = (linux_openvr *)data;

	if (context->initialized || init_inprog) {
		return false;
	}

	auto now = std::chrono::steady_clock::now();
	if (now - last_init_time < retry_delay) {
		return false;
	}
	last_init_time = now;

	init_inprog = true;

	vr::EVRInitError err = vr::VRInitError_None;
	vr::VR_Init(&err, vr::VRApplication_Background);
	if (err != vr::VRInitError_None) {
		warn("VR_Init failed: %s",
		     vr::VR_GetVRInitErrorAsEnglishDescription(err));
		init_inprog = false;
		return false;
	}

	if (!vr::VRCompositor()) {
		warn("VRCompositor not found");
		vr::VR_Shutdown();
		init_inprog = false;
		return false;
	}

	context->mirrorTex = 0;
	context->mirrorSharedHandle = 0;
	context->mirror_acquired = false;

	bool success = false;

	obs_enter_graphics();
	do {
		vr::EVRCompositorError composError =
			vr::VRCompositor()->GetMirrorTextureGL(
				context->righteye ? vr::Eye_Right : vr::Eye_Left,
				&context->mirrorTex,
				&context->mirrorSharedHandle);

		if (composError != vr::VRCompositorError_None ||
		    !context->mirrorTex) {
			warn("GetMirrorTextureGL failed: %d", (int)composError);
			break;
		}

		context->mirror_acquired = true;

		glBindTexture(GL_TEXTURE_2D, context->mirrorTex);

		GLint w = 0;
		GLint h = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

		if (w <= 0 || h <= 0) {
			warn("mirror texture size is invalid");
			break;
		}

		context->device_width = (unsigned int)w;
		context->device_height = (unsigned int)h;

		double scale_factor = context->scale_factor < 1.0 ? 1.0
								   : context->scale_factor;
		unsigned int scaled_width =
			static_cast<unsigned int>(context->device_width / scale_factor);
		unsigned int scaled_height =
			static_cast<unsigned int>(context->device_height / scale_factor);

		context->width = scaled_width;
		context->height = scaled_height;

		if (context->ar_crop) {
			double input_aspect_ratio =
				static_cast<double>(context->width) / context->height;
			double active_aspect_ratio = context->active_aspect_ratio;

			if (input_aspect_ratio > active_aspect_ratio) {
				context->width = static_cast<unsigned int>(
					context->height * active_aspect_ratio);
			} else if (input_aspect_ratio < active_aspect_ratio) {
				context->height = static_cast<unsigned int>(
					context->width / active_aspect_ratio);
			}
		}

		int x = 0;
		int y = 0;

		int x_offset = context->x_offset;
		int y_offset = context->y_offset;

		if (!context->righteye) {
			x_offset = -x_offset;
			x = (int)context->device_width - (int)scaled_width;
		}

		x += x_offset;
		y += y_offset;

		if (x + (int)context->width > (int)context->device_width)
			x = (int)context->device_width - (int)context->width;
		if (y + (int)context->height > (int)context->device_height)
			y = (int)context->device_height - (int)context->height;

		x = std::max(0, x);
		y = std::max(0, y);

		context->x = (unsigned int)x;
		context->y = (unsigned int)y;

		context->full_buffer.resize(static_cast<size_t>(context->device_width) *
					    context->device_height * 4);
		context->crop_buffer.resize(static_cast<size_t>(context->width) *
					    context->height * 4);

		if (context->texture) {
			gs_texture_destroy(context->texture);
			context->texture = nullptr;
		}

		context->texture = gs_texture_create(context->width, context->height,
						     GS_RGBA, 1, nullptr,
						     GS_DYNAMIC);
		if (!context->texture) {
			warn("gs_texture_create failed");
			break;
		}

		success = true;
	} while (false);
	obs_leave_graphics();

	if (!success) {
		release_mirror_texture(context);
		vr::VR_Shutdown();
		init_inprog = false;
		return false;
	}

	context->initialized = true;
	context->lastFrame = 0;
	init_inprog = false;
	return true;
}

static bool linux_openvr_init1(void *data, bool forced = true)
{
	linux_openvr *context = (linux_openvr *)data;

	if (context->initialized || init_inprog) {
		return false;
	}

	auto now = std::chrono::steady_clock::now();
	if (now - last_init_timeBUFFER < retry_delayBUFFER) {
		return false;
	}
	last_init_timeBUFFER = now;

	return linux_openvr_init(data, forced);
}

static void linux_openvr_deinit(void *data)
{
	linux_openvr *context = (linux_openvr *)data;

	if (context->texture) {
		destroy_obs_texture(&context->texture);
	}

	release_mirror_texture(context);

	vr::VR_Shutdown();

	context->initialized = false;
	init_inprog = false;
}

static const char *linux_openvr_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "OpenVR Capture";
}

static void linux_openvr_update(void *data, obs_data_t *settings)
{
	linux_openvr *context = (linux_openvr *)data;

	context->righteye = obs_data_get_bool(settings, "righteye");
	context->scale_factor = obs_data_get_double(settings, "scale_factor");
	context->x_offset = (int)obs_data_get_int(settings, "x_offset");
	context->y_offset = (int)obs_data_get_int(settings, "y_offset");
	context->active_aspect_ratio = obs_data_get_double(settings, "aspect_ratio");

	if (context->active_aspect_ratio == -1.0) {
		context->ar_crop = false;
	} else {
		context->ar_crop = true;

		if (context->active_aspect_ratio == 0.0) {
			int custom_width =
				(int)obs_data_get_int(settings, "custom_aspect_width");
			int custom_height =
				(int)obs_data_get_int(settings, "custom_aspect_height");
			if (custom_width > 0 && custom_height > 0) {
				context->active_aspect_ratio =
					static_cast<double>(custom_width) /
					(double)custom_height;
			} else {
				context->active_aspect_ratio = 16.0 / 9.0;
			}
		}
	}

	if (context->initialized) {
		linux_openvr_deinit(data);
		linux_openvr_init(data);
	}
}

static void linux_openvr_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "righteye", true);
	obs_data_set_default_double(settings, "aspect_ratio", -1.0);
	obs_data_set_default_int(settings, "custom_aspect_width", 16);
	obs_data_set_default_int(settings, "custom_aspect_height", 9);
	obs_data_set_default_double(settings, "scale_factor", 1.0);
	obs_data_set_default_int(settings, "x_offset", 0);
	obs_data_set_default_int(settings, "y_offset", 0);
}

static uint32_t linux_openvr_getwidth(void *data)
{
	linux_openvr *context = (linux_openvr *)data;
	return context->width;
}

static uint32_t linux_openvr_getheight(void *data)
{
	linux_openvr *context = (linux_openvr *)data;
	return context->height;
}

static void linux_openvr_show(void *data)
{
	linux_openvr_init1(data, true);
}

static void linux_openvr_hide(void *data)
{
	linux_openvr_deinit(data);
}

static void *linux_openvr_create(obs_data_t *settings, obs_source_t *source)
{
	auto *context = new linux_openvr();
	context->source = source;
	context->width = 100;
	context->height = 100;
	context->active_aspect_ratio = 16.0 / 9.0;
	context->initialized = false;

	linux_openvr_update(context, settings);
	return context;
}

static void linux_openvr_destroy(void *data)
{
	auto *context = (linux_openvr *)data;
	linux_openvr_deinit(data);
	delete context;
}

static void linux_openvr_upload_frame(linux_openvr *context)
{
	if (!context->mirror_acquired || !context->mirrorTex)
		return;

	glBindTexture(GL_TEXTURE_2D, context->mirrorTex);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		      context->full_buffer.data());

	copy_crop_rgba(context->full_buffer.data(), context->device_width,
		       context->device_height, context->x, context->y,
		       context->width, context->height, context->crop_buffer);

	if (context->texture) {
		gs_texture_set_image(context->texture, context->crop_buffer.data(),
				     context->width * 4, false);
	}
}

static void linux_openvr_render(void *data, gs_effect_t *effect)
{
	linux_openvr *context = (linux_openvr *)data;

	if (!context->active) {
		return;
	}

	if (!context->initialized) {
		linux_openvr_init1(data);
	}

	if (vr::VRCompositor()) {
		vr::Compositor_FrameTiming frameTiming = {};
		frameTiming.m_nSize = sizeof(vr::Compositor_FrameTiming);

		if (vr::VRCompositor()->GetFrameTiming(&frameTiming, 0)) {
			if (frameTiming.m_nFrameIndex != context->lastFrame) {
				if (context->texture && context->mirror_acquired &&
				    context->mirrorTex) {
					linux_openvr_upload_frame(context);
					context->lastFrame = frameTiming.m_nFrameIndex;
				}
			}
		}
	}

	effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);

	if (context->texture) {
		while (gs_effect_loop(effect, "Draw")) {
			obs_source_draw(context->texture, 0, 0, 0, 0, false);
		}
	}
}

static void linux_openvr_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	linux_openvr *context = (linux_openvr *)data;

	context->active = obs_source_showing(context->source);

	vr::VREvent_t e;
	if (vr::VRSystem() != NULL) {
		if (vr::VRSystem()->PollNextEvent(&e, sizeof(vr::VREvent_t))) {
			if (e.eventType == vr::VREvent_Quit) {
				linux_openvr_deinit(data);
			}
		}
	}

	if (!context->initialized && context->active) {
		linux_openvr_init1(data);
	}
}

static bool ar_modd(obs_properties_t *props, obs_property_t *property,
		    obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	double aspect_ratio = obs_data_get_double(settings, "aspect_ratio");
	bool custom_active = (aspect_ratio == 0.0);

	obs_property_t *custom_width =
		obs_properties_get(props, "custom_aspect_width");
	obs_property_t *custom_height =
		obs_properties_get(props, "custom_aspect_height");

	obs_property_set_visible(custom_width, custom_active);
	obs_property_set_visible(custom_height, custom_active);

	return true;
}

static obs_properties_t *linux_openvr_properties(void *data)
{
	linux_openvr *context = (linux_openvr *)data;

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_bool(props, "righteye", obs_module_text("Right Eye"));

	p = obs_properties_add_list(props, "aspect_ratio",
				    obs_module_text("Aspect Ratio"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
	obs_property_list_add_float(p, "Native", -1.0);
	obs_property_list_add_float(p, "16:9", 16.0 / 9.0);
	obs_property_list_add_float(p, "4:3", 4.0 / 3.0);
	obs_property_list_add_float(p, "Custom", 0.0);

	obs_property_set_modified_callback(p, ar_modd);

	p = obs_properties_add_int(props, "custom_aspect_width",
				   obs_module_text("Ratio Width"), 1, 100, 1);
	obs_property_set_visible(p, false);

	p = obs_properties_add_int(props, "custom_aspect_height",
				   obs_module_text("Ratio Height"), 1, 100, 1);
	obs_property_set_visible(p, false);

	p = obs_properties_add_float_slider(props, "scale_factor",
					    obs_module_text("Zoom"), 1.0, 5.0,
					    0.01);
	p = obs_properties_add_int(props, "x_offset",
				   obs_module_text("Horizontal Offset"),
				   -10000, 10000, 1);
	p = obs_properties_add_int(props, "y_offset",
				   obs_module_text("Vertical Offset"),
				   -10000, 10000, 1);

	obs_data_t *settings = obs_source_get_settings(context->source);
	ar_modd(props, nullptr, settings);
	obs_data_release(settings);

	return props;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("linux-openvr", "en-US")

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = "openvr_capture_linux";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = linux_openvr_get_name;
	info.create = linux_openvr_create;
	info.destroy = linux_openvr_destroy;
	info.update = linux_openvr_update;
	info.get_defaults = linux_openvr_defaults;
	info.show = linux_openvr_show;
	info.hide = linux_openvr_hide;
	info.get_width = linux_openvr_getwidth;
	info.get_height = linux_openvr_getheight;
	info.video_render = linux_openvr_render;
	info.video_tick = linux_openvr_tick;
	info.get_properties = linux_openvr_properties;

	obs_register_source(&info);
	return true;
}