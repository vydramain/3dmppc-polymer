#ifndef RV_PDK_CD_RV_CD_H
#define RV_PDK_CD_RV_CD_H

#include <stdint.h>

// Stream states: a sequence, not a mask.
#define RV_CD_STREAM_IDLE    0
#define RV_CD_STREAM_PLAYING 1
#define RV_CD_STREAM_ENDED   2 // entry without a loop played out
#define RV_CD_STREAM_ERROR   3

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cd rv_cd;

int64_t rv_cd_asset_open(rv_cd *cd, const char *resname);
int64_t rv_cd_asset_size(rv_cd *cd, int64_t handle);
int64_t rv_cd_asset_read(rv_cd *cd, int64_t handle, void *baddr, int64_t baddr_size);

// What a resource query names. A disc's resources are not only textures -
// the PR review that added this `kind` parameter asked for sound next, and
// model is still the obvious kind after that - so AUDIO is the second
// enumerator and MODEL the third, refused nowhere but the one gate named on
// rv_cd_resource_addr below, the same way TEXTURE alone was until now.
enum rv_cd_resource_kind {
    RV_CD_RESOURCE_TEXTURE = 1,
    RV_CD_RESOURCE_AUDIO = 2,
    RV_CD_RESOURCE_MODEL = 3,
};

/* Plain name: without a typedef, C demands `enum rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as
   an ordinary type. */
typedef enum rv_cd_resource_kind rv_cd_resource_kind;

// A disc never acquires or releases a resource - it only ever names one.
// Residency is the drive's business: the first of these five calls to name a
// resource of a given kind makes it resident (for the texture kind: reading
// the asset, decoding its container and uploading it to video RAM; for the
// audio kind: reading the asset's raw PCM bytes - there is no container to
// decode, the game supplies none and none is baked - and uploading them to
// sound RAM; for the model kind: reading the .mppcmod and handing it to the
// geometry controller's model memory, making its textures resident with it),
// and every later call for the same name and kind reuses that upload. The
// drive keeps a resource resident until the disc that named it is unloaded,
// at which point the drive frees everything it made resident - the disc never
// frees anything itself.
//
// Not every one of these five means something for every kind:
//   * rv_cd_resource_addr          - meaningful for TEXTURE, AUDIO and MODEL
//                                    alike: where the resident bytes now live.
//   * rv_cd_resource_size          - AUDIO only: its resident byte length,
//                                    the counterpart of a sample's rv_sample.size.
//                                    A texture's "size" is its width/height
//                                    below, not a byte count a game reads back.
//   * rv_cd_resource_palette_addr,
//     rv_cd_resource_width,
//     rv_cd_resource_height        - TEXTURE only: a sound or a model has no
//                                    palette and no pixel dimensions.
// Asking a kind for a query that does not describe it is exactly as
// malformed as asking with a `kind` this enum has never heard of, and
// answers the same RV_ERR_INVAL rather than a bogus 0 that would read back
// as a real, if unlikely, "no palette" or "0 bytes" - a caller must be able
// to trust that a non-negative answer is real. This is deliberately still
// ONE refusal every resource_* query routes through (see rv_pccd_fs's
// resource_resolve_, the one gate every one of the five reaches), not five
// separate copies of the same check.
// A palette is texture-specific in the other direction too: palette_addr
// answers 0, not an error, for a texture FORMAT that has no palette - that
// is a real fact about a resident texture, not a malformed query - and only
// the kind mismatch above (asking palette_addr of an AUDIO or MODEL name) is
// refused.
// A name that is not on the disc, or a resource that fails to become
// resident (a texture that fails to decode; AUDIO and MODEL have nothing to
// decode), also answers a negative rv_err from any of the five (RV_ERR_NOENT
// for the unknown name, distinct from the RV_ERR_INVAL a bad kind or a
// meaningless query gets).
// Addresses are valid until that resource is reloaded - ask again rather
// than caching across a reload.
int64_t rv_cd_resource_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_size(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_palette_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_width(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_height(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);

// Streamed music. A frame is one mono S16 sample (2 bytes). Loop 1 repeats the frames from loop_start
// up to loop_end (exclusive): after loop_end, loop_start plays next; frames past loop_end never sound.
// Loop 0 plays the entry once. For any loop, play answers RV_ERR_INVAL if loop_start or the effective
// loop_end (0 = entry end) lies outside the entry, or if loop_start >= loop_end.
struct rv_cd_stream_conf {
    int64_t loop;       // 0: play once, 1: repeat the loop; any other value is RV_ERR_INVAL
    int64_t loop_start; // first frame of the loop
    int64_t loop_end;   // frame after the loop's last frame; 0: end of the entry
};

typedef struct rv_cd_stream_conf rv_cd_stream_conf;

// Plays the entry from frame 0; replaces the current stream (the old one stops, the ring is cleared).
// A refusal changes nothing; RV_OK on success. Ring: fixed-size buffer from the drive reader to the mixer (the consumer).
// RV_ERR_INVAL: null cd/resname/conf; name: plain, no path separators; size 0 or odd; loop not 0 or 1; loop bounds (see conf).
// RV_ERR_NOENT: no entry. RV_ERR_IO: the entry cannot be measured; play does not read it, so a later read
// failure shows as state ERROR, error RV_ERR_IO. resname and conf are read only during the call.
int64_t rv_cd_stream_play(rv_cd *cd, const char *resname, const rv_cd_stream_conf *conf);

// Stops the stream and clears the ring; state IDLE, error 0, from any state (ERROR too). Idempotent. RV_OK.
int64_t rv_cd_stream_stop(rv_cd *cd);

// Stream state: IDLE before the first play. Without a loop the ring plays out, then ENDED and silence.
// Empty ring: silence, position stays (no skip, no repeat), starved_frames grows.
// Read error or changed entry: ERROR, ring cleared, silence.
// Volume: rv_ca_stream_volume.
struct rv_cd_stream_info {
    int64_t state;          // RV_CD_STREAM_*
    int64_t error;          // rv_err that made the stream ERROR, else 0
    int64_t position;       // next entry frame to sound, by the consumer's count
    int64_t loops;          // times the stream passed the loop boundary
    int64_t starved_frames; // silent frames for an empty ring since the last play
};

typedef struct rv_cd_stream_info rv_cd_stream_info;

// Writes the stream status to out. RV_OK; RV_ERR_INVAL if cd or out is null.
int64_t rv_cd_stream_status(rv_cd *cd, rv_cd_stream_info *out);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
