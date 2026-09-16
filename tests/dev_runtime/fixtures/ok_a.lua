-- Fixture: a well-formed entry chunk, clear colour A. Pairs with ok_b.lua so
-- an acceptance run can reload A -> B and tell the frame apart, and can show
-- that state.frame_count survives the swap.
--
-- Only `state` (the console's persistent table) is used across hooks, never
-- a chunk local: a reload runs attach() with no disc_initialize call in
-- between, so a chunk local set only in disc_initialize would be nil in a
-- chunk that got here by reload instead of by boot.
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

function M.disc_initialize(cv_, ca_, cio_)
	state.cv_raw = cv_
	state.cv = pdk.cast("rv_cv*", cv_)
end

function M.frame_update(dt)
	state.frame_count = state.frame_count + 1
	return false
end

function M.frame_render()
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 200, 40, 40 })) -- red-ish: A
end

function M.disc_shutdown() end

return M
