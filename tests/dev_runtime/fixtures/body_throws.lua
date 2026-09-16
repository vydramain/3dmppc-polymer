-- Fixture: parses, but the top-level body raises before it ever returns a
-- table. Proves error=body.
error("body_throws: deliberate failure while the chunk body runs")

local M = {}

function M.attach(state)
	return true
end

return M
