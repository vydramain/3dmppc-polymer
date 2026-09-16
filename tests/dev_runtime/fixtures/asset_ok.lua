-- Fixture: a well-formed, reloadable entry chunk whose asset_changed(name)
-- simply reports success without touching any real video memory. Proves
-- the `asset <name>` request answers `ok` when the hook says so.
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

function M.asset_changed(name)
	return true
end

function M.frame_update(dt)
	return false
end

function M.frame_render()
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 60, 60, 60 }))
end

function M.disc_shutdown() end

return M
