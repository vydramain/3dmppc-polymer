-- The factory board's lettering "ОСТАЛОСЬ: 5" (example-cpp-board.cpp): one glyph model per
-- cell, lit by a flicker table. Each lit cell draws its halo first, then the letters.
local mathx = require("example-lua-math")
local draw = require("example-lua-draw")
local M = {}

local HALO_MODEL = "halo.mppcmod"
-- One model per cell; false is the space between the colon and the digit.
local TEXT = {
    "glyph_o.mppcmod",
    "glyph_s.mppcmod",
    "glyph_t.mppcmod",
    "glyph_a.mppcmod",
    "glyph_l.mppcmod",
    "glyph_o.mppcmod",
    "glyph_s.mppcmod",
    "glyph_soft.mppcmod",
    "glyph_colon.mppcmod",
    false,
    "glyph_5.mppcmod",
}
local CELL_COUNT = 11
M.CELL_COUNT = CELL_COUNT
-- Cell pitch along the panel, metres; the text is centred on the panel.
local CELL_WIDTH = 0.24
-- Forward offsets from the panel along its normal, metres: the halo and the letter stand off it.
local HALO_FORWARD = 0.005
local LETTER_FORWARD = 0.010
-- Flicker: cell i is lit when TABLE[(frame / STEP + i * SHIFT) % 256] > LIT_ABOVE.
-- The shift spreads the letters over the table so they do not blink in unison.
local FLICKER_STEP = 6 -- frames per table step
local FLICKER_SHIFT = 37
local FLICKER_SIZE = 256
local LIT_ABOVE = 128
-- The matrix is row-major 4x4: 16 numbers.
local MATRIX_LEN = mathx.MATRIX_LEN
-- The text is centred on the panel: half its width, and the centre of a cell is half a cell in.
local HALF_FACTOR = 0.5
local CELL_CENTRE = 0.5

-- Own values, of the same kind as Doom's rndtable (256 bytes of pseudo-random data read by
-- frame index), not its numbers. Same table as example-cpp-board.cpp.
local FLICKER_TABLE = {
    37, 201, 88, 14, 166, 243, 59, 117, 9, 180, 128, 72, 223, 45, 154, 96, 231, 6, 112, 190, 53, 147, 25, 210, 84, 169, 13, 238, 101, 66, 197, 31,
    142, 77, 255, 20, 178, 98, 230, 4, 133, 61, 215, 110, 39, 186, 92, 249, 17, 164, 121, 57, 208, 83, 2, 141, 226, 70, 105, 189, 44, 153, 27, 219,
    91, 198, 36, 170, 129, 252, 65, 12, 183, 97, 240, 58, 146, 107, 200, 8, 174, 119, 48, 233, 80, 161, 22, 205, 137, 74, 254, 33, 118, 194, 63, 211,
    3, 149, 90, 181, 226, 116, 43, 167, 102, 239, 21, 156, 85, 209, 130, 49, 192, 28, 224, 75, 158, 127, 62, 245, 16, 187, 99, 136, 233, 71, 172, 38,
    109, 207, 52, 143, 3, 218, 124, 89, 177, 40, 196, 103, 60, 230, 150, 15, 221, 86, 163, 29, 115, 201, 47, 185, 138, 253, 94, 176, 26, 123, 214, 68,
    134, 1, 199, 106, 244, 56, 160, 19, 228, 81, 145, 212, 35, 191, 113, 250, 54, 171, 79, 236, 148, 11, 195, 126, 100, 168, 30, 223, 73, 139, 5, 182,
    232, 111, 42, 157, 206, 87, 246, 64, 179, 23, 132, 108, 216, 50, 202, 93, 155, 24, 241, 67, 188, 135, 10, 225, 76, 162, 46, 247, 120, 193, 78, 144,
    41, 220, 104, 175, 32, 237, 122, 88, 213, 151, 59, 18, 203, 140, 69, 234, 165, 95, 248, 57, 198, 4, 159, 210, 131, 83, 242, 34, 184, 101, 229, 114,
}
assert(#FLICKER_TABLE == FLICKER_SIZE, "the flicker table is 256 bytes")

-- True when cell i (0-based) is lit at frame `frame`; the space cell never lights.
function M.cell_lit(i, frame)
    local index = (math.floor(frame / FLICKER_STEP) + i * FLICKER_SHIFT) % FLICKER_SIZE
    return TEXT[i + 1] ~= false and FLICKER_TABLE[index + 1] > LIT_ABOVE
end

-- Offset of cell i along the panel: centred on the panel, as example-cpp-board.cpp.
local function along_of(half, i, cell)
    return mathx.f(-half + mathx.f(mathx.f(i + CELL_CENTRE) * cell))
end

-- board is the board object's 1-based world table. Returns the panel world of a model
-- shifted by `forward` along the panel normal (the board's local +x) and `along` along its local +z.
local function panel_world(board, forward, along)
    local out = {}
    for k = 1, MATRIX_LEN do
        out[k] = board[k]
    end
    out[4] = mathx.f(mathx.f(board[4] + mathx.f(board[1] * forward)) + mathx.f(board[3] * along))
    out[8] = mathx.f(mathx.f(board[8] + mathx.f(board[5] * forward)) + mathx.f(board[7] * along))
    out[12] = mathx.f(mathx.f(board[12] + mathx.f(board[9] * forward)) + mathx.f(board[11] * along))
    return out
end

-- Draws the lit cells at frame `frame`: all halos first, then all letters, so a halo never
-- lands over its neighbour's letter. Returns 0 or the first drive or cg error.
function M.draw(cg, cd, board, frame)
    local cell = mathx.f(CELL_WIDTH)
    local half = mathx.f(mathx.f(CELL_COUNT * cell) * HALF_FACTOR)
    local halo_forward = mathx.f(HALO_FORWARD)
    local letter_forward = mathx.f(LETTER_FORWARD)
    for i = 0, CELL_COUNT - 1 do
        if M.cell_lit(i, frame) then
            local code = draw.decal(cg, cd, HALO_MODEL, panel_world(board, halo_forward, along_of(half, i, cell)), draw.WHITE)
            if code < 0 then
                return code
            end
        end
    end
    for i = 0, CELL_COUNT - 1 do
        if M.cell_lit(i, frame) then
            local world = panel_world(board, letter_forward, along_of(half, i, cell))
            local code = draw.decal(cg, cd, TEXT[i + 1], world, draw.WHITE)
            if code < 0 then
                return code
            end
        end
    end
    return 0
end

return M
