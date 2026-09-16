-- Fixture: attach() never returns. Proves error=insn_ceiling fires during a
-- reload's attach phase, not only during its compile/body phase.
local M = {}

function M.attach(state)
	while true do
	end
end

return M
