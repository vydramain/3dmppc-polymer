-- Fixture: attach() writes into state FIRST, then raises. Demonstrates
-- effects=1 on error=attach: the write already happened and cannot be
-- undone by the failed reload.
local M = {}

function M.attach(state)
	state.attach_throws_dirty_touched = true
	error("attach_throws_dirty: deliberate failure after a state write")
end

return M
