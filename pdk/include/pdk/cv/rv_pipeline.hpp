#pragma once

namespace rv_pdk
{

enum rv_pipeline_buffer_config_type {
	RV_PIPELINE_BUFFER_CONFIG_TYPE_Z = 1U << 0, // add per-pixel depth rejection
};

} // namespace rv_pdk
