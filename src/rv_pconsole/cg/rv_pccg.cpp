#include "rv_pconsole/cg/rv_pccg.hpp"

// --- C contract (pdk/cg/rv_cg.h) ---------------------------------------------
// An rv_cg* handle and the address of an rv_pccg are the same address, reached
// through a virtual call, as in rv_pccv.cpp.

extern "C" int64_t rv_cg_model_memory_size(rv_cg *cg)
{
    return reinterpret_cast<rv_3dmppc::rv_pccg *>(cg)->model_memory_size();
}

extern "C" int64_t rv_cg_triangle_capacity(rv_cg *cg)
{
    return reinterpret_cast<rv_3dmppc::rv_pccg *>(cg)->triangle_capacity();
}

extern "C" int64_t rv_cg_frame_camera(rv_cg *cg, const rv_cg_camera *camera)
{
    return reinterpret_cast<rv_3dmppc::rv_pccg *>(cg)->frame_camera(camera);
}

extern "C" int64_t rv_cg_model_put(rv_cg *cg, int64_t model_addr, const rv_cg_put *put)
{
    return reinterpret_cast<rv_3dmppc::rv_pccg *>(cg)->model_put(model_addr, put);
}

extern "C" int64_t rv_cg_point(rv_cg *cg, const rv_cg_vec3 *world, rv_cg_screen *out)
{
    return reinterpret_cast<rv_3dmppc::rv_pccg *>(cg)->point(world, out);
}
