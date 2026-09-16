-- Fixture: a valid module table with no attach() at all. Proves
-- error=no_attach on reload, and reloadable=false when raised as an entry.
local M = {}

function M.frame_update(dt)
	return false
end

return M
