-- Fixture: attach() writes NOTHING and returns false with a reason, before
-- any state mutation. Proves error=attach_refused with effects=0.
local M = {}

function M.attach(state)
	return false, "attach_false: state layout mismatch, on purpose"
end

return M
