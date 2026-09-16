-- Fixture: a valid, reloadable entry chunk with NO asset_changed at all.
-- Proves error=no_asset_hook, the same shape as no_attach.lua proving
-- error=no_attach for a code reload.
local M = {}

local state

function M.attach(s)
	state = s
	if state.cv_raw then
		state.cv = pdk.cast("rv_cv*", state.cv_raw)
	end
	return true
end

function M.disc_initialize(cv_, ca_, cio_)
	state.cv_raw = cv_
	state.cv = pdk.cast("rv_cv*", cv_)
end

function M.frame_update(dt)
	return false
end

function M.frame_render()
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 60, 60, 60 }))
end

function M.disc_shutdown() end

return M
