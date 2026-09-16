-- Fixture: the top-level body never returns. Proves error=insn_ceiling fires
-- during a reload's compile-and-run-body phase.
while true do
end

local M = {}

function M.attach(state)
	return true
end

return M
