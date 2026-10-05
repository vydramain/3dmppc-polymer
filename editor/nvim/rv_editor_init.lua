-- 3dmppc-editor's nvim: an ordinary editor by default.
-- Loaded with -u, so the user's own init.lua and plugins stay out.
-- F2 switches between this and plain Vim; the editor starts in insert mode.

local o = vim.opt

o.number = true
o.termguicolors = true
-- The owner's everyday settings (~/.config/nvim/lua/options.lua), without plugins.
o.relativenumber = true
o.cursorline = true
o.signcolumn = "yes"
o.scrolloff = 8
o.sidescrolloff = 8
o.wrap = false
o.splitbelow = true
o.splitright = true
o.ignorecase = true
o.smartcase = true
o.hlsearch = false
o.updatetime = 250
o.timeoutlen = 500
o.expandtab = true
o.tabstop = 2
o.shiftwidth = 2
o.softtabstop = 2
o.smartindent = true
o.hidden = true
o.autoread = true
o.clipboard = "unnamedplus"
o.mouse = "a" -- the editor sends clicks and drags in a code tile (nvim_input_mouse)
-- Each tile draws its own status line; nvim would put one in grid 1, outside the tile.
o.laststatus = 0
o.showmode = true
o.cmdheight = 1
o.shortmess:append("I")
o.undofile = false
o.swapfile = true
-- Shift+arrows select, typing replaces the selection.
o.keymodel = { "startsel", "stopsel" }
o.selectmode = { "key" }
o.whichwrap = "b,s,<,>,[,]"

-- E325 "ATTENTION" when file has swap (crash or concurrent edit). RPC (msgpack to nvim --embed)
-- cannot press keys to answer. SwapExists autocommand chooses instead and notifies the editor.
local rv_swap_answers = {}  -- pending answers keyed by full path; value "r" or "d"
local rv_swap_sources = {}  -- old swap file paths keyed by full path (for cleanup after recover)

vim.api.nvim_create_autocmd("SwapExists", {
    callback = function()
        local file = vim.fn.expand("<afile>:p")
        local swapname = vim.v.swapname

        if rv_swap_answers[file] then
            vim.v.swapchoice = rv_swap_answers[file]
            rv_swap_answers[file] = nil
            return
        end

        local info = vim.fn.swapinfo(swapname)

        if info.pid ~= 0 and info.pid ~= vim.fn.getpid() then
            local success, result = pcall(vim.uv.kill, info.pid, 0)
            if success and result == 0 then
                vim.v.swapchoice = "o"
                vim.rpcnotify(0, "rv_swap", { file = file, swap = swapname, state = "in_use", pid = info.pid })
                return
            end
        end

        if info.dirty == 0 then
            vim.v.swapchoice = "d"
            return
        end

        -- Swap holds unsaved text from a dead nvim: open read-only and notify.
        vim.v.swapchoice = "o"
        rv_swap_sources[file] = swapname
        vim.rpcnotify(0, "rv_swap", { file = file, swap = swapname, state = "recoverable" })
    end,
})

function _G.rv_swap_resolve(win, path, choice)
    local fullpath = vim.fn.fnamemodify(path, ":p")

    rv_swap_answers[fullpath] = (choice == "recover") and "r" or "d"

    -- Find and wipe the loaded buffer so the next edit reads the file and swap fresh.
    for _, b in ipairs(vim.api.nvim_list_bufs()) do
        if vim.api.nvim_buf_is_loaded(b) and vim.api.nvim_buf_get_name(b) == fullpath then
            vim.api.nvim_buf_delete(b, { force = true })
            break
        end
    end

    local success, err = pcall(function()
        if vim.api.nvim_win_is_valid(win) then
            vim.api.nvim_set_current_win(win)
        end
        -- silent: recovery messages would open a Press-ENTER prompt RPC cannot answer.
        vim.cmd.edit({ args = { fullpath }, mods = { silent = true } })
    end)

    if not success then
        vim.rpcnotify(0, "rv_open_error", { file = path, msg = tostring(err) })
        return
    end

    -- After successful recover: delete the old swap if it is not the buffer's current swap.
    if choice == "recover" then
        local old_swap = rv_swap_sources[fullpath]
        if old_swap then
            for _, b in ipairs(vim.api.nvim_list_bufs()) do
                if vim.api.nvim_buf_is_loaded(b) and vim.api.nvim_buf_get_name(b) == fullpath then
                    local current_swap = vim.fn.swapname(b)
                    if old_swap ~= current_swap and vim.fn.filereadable(old_swap) == 1 then
                        vim.fn.delete(old_swap)
                    end
                    break
                end
            end
            rv_swap_sources[fullpath] = nil
        end
    end

    vim.rpcnotify(0, "rv_swap", { file = fullpath, state = "resolved" })
end

-- Colours: Catppuccin Mocha, independent of the editor's olive chrome.
local c = {
    base = "#1e1e2e", mantle = "#181825", crust = "#11111b", surface0 = "#313244", surface1 = "#45475a",
    overlay0 = "#6c7086", text = "#cdd6f4", subtext = "#a6adc8", red = "#f38ba8", peach = "#fab387",
    yellow = "#f9e2af", green = "#a6e3a1", teal = "#94e2d5", blue = "#89b4fa", mauve = "#cba6f7",
    lavender = "#b4befe",
}
local function hl(group, spec) vim.api.nvim_set_hl(0, group, spec) end
vim.cmd("highlight clear")
vim.g.colors_name = "rv_mocha"
hl("Normal", { fg = c.text, bg = c.base })
hl("NormalNC", { fg = c.text, bg = c.base })
hl("LineNr", { fg = c.overlay0, bg = c.base })
hl("CursorLineNr", { fg = c.lavender, bg = c.base })
hl("CursorLine", { bg = "#2a2b3c" })
hl("SignColumn", { bg = c.base })
hl("StatusLine", { fg = c.text, bg = c.surface1 })
hl("StatusLineNC", { fg = c.subtext, bg = c.surface0 })
hl("WinSeparator", { fg = c.surface0 })
hl("Visual", { bg = c.surface1 })
hl("Search", { fg = c.base, bg = c.yellow })
hl("IncSearch", { fg = c.base, bg = c.peach })
hl("ColorColumn", { bg = c.mantle })
hl("Pmenu", { fg = c.text, bg = c.surface0 })
hl("PmenuSel", { fg = c.base, bg = c.blue })
hl("ErrorMsg", { fg = c.red })
hl("WarningMsg", { fg = c.yellow })
hl("MsgArea", { fg = c.text, bg = c.crust })
hl("Comment", { fg = c.overlay0, italic = true })
hl("String", { fg = c.green })
hl("Character", { fg = c.teal })
hl("Number", { fg = c.peach })
hl("Boolean", { fg = c.peach })
hl("Constant", { fg = c.peach })
hl("Identifier", { fg = c.text })
hl("Function", { fg = c.blue })
hl("Statement", { fg = c.mauve })
hl("Keyword", { fg = c.mauve })
hl("Operator", { fg = c.teal })
hl("Type", { fg = c.yellow })
hl("PreProc", { fg = c.red })
hl("Special", { fg = c.lavender })
hl("Delimiter", { fg = c.subtext })
hl("Todo", { fg = c.base, bg = c.yellow })
hl("@variable", { fg = c.text })

-- Per language: two spaces everywhere, four for C and C++ with the ruler
-- at 129, the first column past 128 (the owner's options.lua).
local profiles = {
    c = { expandtab = true, tabstop = 4, shiftwidth = 4, softtabstop = 4, colorcolumn = "129" },
    cpp = { expandtab = true, tabstop = 4, shiftwidth = 4, softtabstop = 4, colorcolumn = "129" },
}
vim.api.nvim_create_autocmd("FileType", {
    callback = function(ev)
        local p = profiles[ev.match]
        if p then
            for k, v in pairs(p) do vim.opt_local[k] = v end
        end
        -- The bundled parsers highlight what they know; the rest keeps syntax.
        pcall(vim.treesitter.start, ev.buf)
    end,
})

-- Language servers: clangd for C/C++, lua-language-server (LuaJIT) for Lua.
-- Root is the directory holding disc.toml; nvim's cwd already is it, but a
-- buffer under src/ or scripts/ still resolves the same root via vim.fs.root.
hl("DiagnosticError", { fg = c.red })
hl("DiagnosticWarn", { fg = c.yellow })
hl("DiagnosticInfo", { fg = c.blue })
hl("DiagnosticHint", { fg = c.teal })
hl("DiagnosticUnderlineError", { sp = c.red, underline = true })
hl("DiagnosticUnderlineWarn", { sp = c.yellow, underline = true })
hl("DiagnosticUnderlineInfo", { sp = c.blue, underline = true })
hl("DiagnosticUnderlineHint", { sp = c.teal, underline = true })
vim.diagnostic.config({
    signs = { text = { [vim.diagnostic.severity.ERROR] = "E", [vim.diagnostic.severity.WARN] = "W",
        [vim.diagnostic.severity.INFO] = "I", [vim.diagnostic.severity.HINT] = "H" } },
    underline = true,
    virtual_text = { spacing = 2 },
})

local function rv_project_root(bufnr)
    return vim.fs.root(bufnr, "disc.toml") or vim.fn.getcwd()
end
-- vim.lsp.Config wants root_dir as fun(bufnr, on_dir), not a plain getter.
local function rv_root_dir(bufnr, on_dir) on_dir(rv_project_root(bufnr)) end

-- The editor learns server state on the same channel as rv_mode/rv_buffers.
-- Kept locally too, so a test (or a later editor build) can read it directly.
_G.rv_lsp_status = {}
local function rv_lsp_report(server, state, reason)
    _G.rv_lsp_status[server] = { state = state, reason = reason }
    vim.rpcnotify(0, "rv_lsp", { server = server, state = state, reason = reason })
end

local function rv_send_diagnostics(bufnr)
    local items = {}
    for _, d in ipairs(vim.diagnostic.get(bufnr)) do
        items[#items + 1] = {
            line = d.lnum + 1,
            col = d.col + 1,
            severity = vim.diagnostic.severity[d.severity]:lower(),
            source = d.source,
            message = d.message,
        }
    end
    vim.rpcnotify(0, "rv_diagnostics", { file = vim.api.nvim_buf_get_name(bufnr), items = items })
end
vim.api.nvim_create_autocmd("DiagnosticChanged", {
    callback = function(ev) rv_send_diagnostics(ev.buf) end,
})

local rv_servers = {
    clangd = {
        cmd = { "clangd", "--compile-commands-dir=" .. vim.fn.getcwd() .. "/.mppcburn" },
        filetypes = { "c", "cpp" },
        root_dir = rv_root_dir,
    },
    luals = {
        cmd = { "lua-language-server" },
        filetypes = { "lua" },
        root_dir = rv_root_dir,
        -- The console gives every script `pdk` and its persistent `state`.
        settings = { Lua = { runtime = { version = "LuaJIT" }, diagnostics = { globals = { "state", "pdk" } } } },
    },
}
for name, cfg in pairs(rv_servers) do
    cfg.on_error = function(_, err) rv_lsp_report(name, "stopped", tostring(err)) end
    cfg.on_exit = function(code, signal) rv_lsp_report(name, "stopped", "exit " .. code .. " signal " .. signal) end
    vim.lsp.config(name, cfg)
    -- A missing executable never breaks editing: skip enabling that server
    -- instead of letting it fail to spawn on every matching buffer.
    if vim.fn.executable(cfg.cmd[1]) == 1 then
        vim.lsp.enable(name)
    else
        rv_lsp_report(name, "missing", cfg.cmd[1] .. " not found on PATH")
    end
end
vim.api.nvim_create_autocmd("LspAttach", {
    callback = function(ev)
        local client = vim.lsp.get_client_by_id(ev.data.client_id)
        if not client or not rv_servers[client.name] then
            return
        end
        rv_lsp_report(client.name, "running", nil)
    end,
})

-- Hover and go-to-definition, in both editing modes.
vim.keymap.set({ "n", "i" }, "<C-k>", function() vim.lsp.buf.hover() end)
vim.keymap.set({ "n", "i" }, "<F12>", function() vim.lsp.buf.definition() end)

-- Editor mode or Vim mode. In editor mode a buffer is always typed into.
vim.g.rv_vim_mode = false
local function stay_inserting()
    if not vim.g.rv_vim_mode and vim.bo.buftype == "" and vim.bo.modifiable then
        vim.cmd("startinsert")
    end
end
vim.api.nvim_create_autocmd({ "BufEnter", "WinEnter", "BufWinEnter" }, { callback = stay_inserting })
vim.api.nvim_create_autocmd("InsertLeave", {
    callback = function() vim.schedule(stay_inserting) end,
})
local function toggle_vim_mode()
    vim.g.rv_vim_mode = not vim.g.rv_vim_mode
    if vim.g.rv_vim_mode then
        vim.cmd("stopinsert")
        vim.notify("Vim mode")
    else
        vim.notify("Editor mode")
        stay_inserting()
    end
    -- The editor's Vim toggle shows what F2 did.
    vim.rpcnotify(0, "rv_mode", vim.g.rv_vim_mode)
end
vim.keymap.set({ "n", "i", "v", "s" }, "<F2>", toggle_vim_mode)
-- The same switch for the editor's Vim toggle.
_G.rv_toggle_vim_mode = toggle_vim_mode

-- The usual keys, in every mode.
local map = vim.keymap.set
map({ "n", "i", "v", "s" }, "<C-s>", "<Cmd>update<CR>")
map({ "n", "i", "v", "s" }, "<C-S-s>", "<Cmd>wall<CR>")
map("i", "<C-z>", "<C-o>u")
map("n", "<C-z>", "u")
map("i", "<C-S-z>", "<C-o><C-r>")
map("n", "<C-S-z>", "<C-r>")
map("i", "<C-y>", "<C-o><C-r>")
map("v", "<C-c>", '"+y')
map("s", "<C-c>", '<C-o>"+y')
map("v", "<C-x>", '"+d')
map("s", "<C-x>", '<C-o>"+d')
map("i", "<C-v>", "<C-r><C-o>+")
map("v", "<C-v>", '"+P')
map("s", "<C-v>", '<C-o>"+P')
map("i", "<C-a>", "<C-o>gg<C-o>VG")
map("i", "<C-f>", "<C-o>/")
map("i", "<C-h>", "<C-o>:%s/")
map("i", "<C-g>", "<C-o>:")

-- The editor keeps a list of buffers and whether they are modified, so it can
-- ask before a code tile or the editor closes. Sent on every change.
local function report()
    local out = {}
    for _, b in ipairs(vim.api.nvim_list_bufs()) do
        if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].buftype == "" then
            out[#out + 1] = {
                id = b,
                name = vim.api.nvim_buf_get_name(b),
                modified = vim.bo[b].modified,
                windows = vim.fn.win_findbuf(b),
            }
        end
    end
    vim.rpcnotify(0, "rv_buffers", out)
end
vim.api.nvim_create_autocmd({ "BufModifiedSet", "BufWritePost", "BufDelete", "BufWipeout", "BufWinEnter",
    "BufWinLeave", "BufEnter", "WinNew", "WinClosed", "VimEnter" }, {
    callback = function() vim.schedule(report) end,
})

-- Files changed outside are re-read when clean; nvim asks when they are not.
vim.api.nvim_create_autocmd({ "FocusGained", "BufEnter" }, { command = "silent! checktime" })
