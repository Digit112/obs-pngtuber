/*
OBS PNGtuber
Copyright (C) 2024 Ekobadd ekobaddish@gmail.com

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#define EKO_PNGTUBER_VERSION "0.0"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("OBS PNGtuber", "en-US")

extern struct obs_source_info pngtuber;

bool obs_module_load(void)
{
	obs_register_source(&pngtuber);
	
	obs_log(LOG_INFO, "OBS PNGtuber loaded successfully (version %s)", EKO_PNGTUBER_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "OBS PNGtuber unloaded");
}
