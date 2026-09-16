-- Fixture: attaches cleanly, then fails in a GAME hook rather than during the
-- reload. This is the other half of the error story: the candidate was accepted
-- and committed, and the mistake only shows up on the next frame.
--
-- What must happen: the console notices the failed hook, stops on the frame
-- boundary and says so with an id-0 event. Then reloading a working chunk and
-- stepping again must simply work - no restart, and no permanent latch that
-- would stop the disc calling into lua for the rest of the run.
local M = {}

local state

function M.attach(s)
	state = s
	if state.cv_raw then
		state.cv = pdk.cast("rv_cv*", state.cv_raw)
	end
	state.frame_count = state.frame_count or 0
	return true
end

function M.frame_update(dt)
	state.frame_count = state.frame_count + 1
	error("update_throws: deliberate failure inside a game hook")
end

function M.frame_render()
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 200, 40, 40 }))
end

function M.disc_shutdown() end

return M
