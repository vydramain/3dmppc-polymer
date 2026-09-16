-- Fixture: asset_changed() that returns nothing at all.
--
-- The gate protocol demands a boolean: true, or false and a reason. Anything
-- else is a contract violation and not a refusal, because "forgot to return"
-- must not read as "accepted". This fixture exists to prove the two hooks that
-- share one implementation of that protocol still report their OWN token - an
-- asset request answering `attach_contract` was a real bug once.
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

function M.asset_changed(name)
	-- No return. Deliberately.
end

function M.frame_update(dt)
	state.frame_count = state.frame_count + 1
	return false
end

function M.frame_render()
	pdk.cv_frame_configure(state.cv, 0, pdk.new("rv_color", { 20, 24, 40 }))
end

function M.disc_shutdown() end

return M
