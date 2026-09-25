-- 3dmppc-editor's nvim: an ordinary editor by default (editor/docs/adr/0005-code-editor-nvim.md).
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

-- Colours: Catppuccin Mocha, independent of the editor's olive chrome (TXT-02).
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

-- Per language (TXT-04): two spaces everywhere, four for C and C++ with the ruler
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
end
vim.keymap.set({ "n", "i", "v", "s" }, "<F2>", toggle_vim_mode)

-- The usual keys, in every mode (TXT-01).
local map = vim.keymap.set
map({ "n", "i", "v", "s" }, "<C-s>", "<Cmd>update<CR>")
map({ "n", "i", "v", "s" }, "<C-S-s>", "<Cmd>wall<CR>")
map("i", "<C-z>", "<C-o>u")
map("n", "<C-z>", "u")
map("i", "<C-S-z>", "<C-o><C-r>")
map("n", "<C-S-z>", "<C-r>")
map("i", "<C-y>", "<C-o><C-r>")
map({ "v", "s" }, "<C-c>", '"+y')
map({ "v", "s" }, "<C-x>", '"+d')
map("i", "<C-v>", "<C-r><C-o>+")
map({ "v", "s" }, "<C-v>", '"+P')
map("i", "<C-a>", "<C-o>gg<C-o>VG")
map("i", "<C-f>", "<C-o>/")
map("i", "<C-h>", "<C-o>:%s/")
map("i", "<C-g>", "<C-o>:")

-- The editor keeps a list of buffers and whether they are modified, so it can
-- ask before a code tile or the editor closes (TXT-07). Sent on every change.
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
    "BufWinLeave", "BufEnter", "WinClosed", "VimEnter" }, {
    callback = function() vim.schedule(report) end,
})

-- Files changed outside are re-read when clean; nvim asks when they are not.
vim.api.nvim_create_autocmd({ "FocusGained", "BufEnter" }, { command = "silent! checktime" })
