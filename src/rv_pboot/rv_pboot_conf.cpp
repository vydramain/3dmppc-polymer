#include "rv_pboot_conf.hpp"

namespace rv_3dmppc
{

void rv_pboot_conf_build(const rv_pdklib::rv_manifest_budget &budget, const rv_pboot_args &args,
    rv_pconsole_conf &conf)
{
    conf.ca.voice_count = budget.pcca.voice_count;
    conf.ca.sound_memory_size = budget.pcca.sound_memory_size;
    conf.cv.screen_width = budget.pccv.screen_width;
    conf.cv.screen_height = budget.pccv.screen_height;
    conf.cv.texture_max_width = budget.pccv.texture_max_width;
    conf.cv.texture_max_height = budget.pccv.texture_max_height;
    conf.cv.video_memory_size = budget.pccv.video_memory_size;
    conf.cv.frame_capacity = budget.pccv.frame_capacity;
    conf.cv.ot_bucket_count = budget.pccv.ot_bucket_count;
    conf.cio.iport_count = budget.pccio.iport_count;
    conf.cm.card_slots = budget.pccm.card_slots;
    conf.cm.card_slot_size = budget.pccm.card_slot_size;

    conf.params.headless = args.headless;
    conf.params.fixed_step = args.fixed_step;
    conf.params.scale = args.scale;
    conf.params.max_frames = args.max_frames;
    conf.params.dump_frame_path = args.dump_frame_path;
    conf.ca.mute = args.mute;
    conf.ca.no_audio = args.no_audio;
    conf.cd.medium_path = args.medium_path;
    conf.cm.image_path = args.memcard_path;
}

} // namespace rv_3dmppc
