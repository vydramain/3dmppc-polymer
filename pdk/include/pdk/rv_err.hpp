#pragma once

namespace rv_pdk
{

enum rv_err : int {
	RV_OK = 0,
	RV_ERR_INVAL = -1, // malformed call: bad argument, unknown handle, short buffer
	RV_ERR_NOMEM = -2, // a pool the controller manages is exhausted
	RV_ERR_BUSY = -3,  // the resource is occupied; retrying later may succeed
	RV_ERR_NOENT = -4, // the named thing does not exist
	RV_ERR_IO = -5,    // the device failed to carry out a well-formed call
};

} // namespace rv_pdk
