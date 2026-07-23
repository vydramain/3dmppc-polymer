#pragma once

namespace rv_3dmppc {

// Per-frame buffer configuration, passed as a bitmask to rv_cv::frame_configure().
// The console ALWAYS orders primitives back-to-front by rv_primitive::depth (the
// ordering table); these flags add optional behaviour on top of that ordering.
//
// New flags take the next free bit; existing bits are ABI.
enum rv_pipeline_buffer_config_type {
    RV_PIPELINE_BUFFER_CONFIG_TYPE_Z = 1U << 0,  // add per-pixel depth rejection
};

}  // namespace rv_3dmppc
