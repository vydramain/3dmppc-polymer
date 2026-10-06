// ImGui keyboard to nvim_input notation: UTF-8 encoding and special key names.

#include <cstdint>
#include <string>

#include "imgui.h"

namespace rv_editor
{

namespace
{

// UTF-8 byte class limits, start masks, continuation mask: from RFC 3629.
constexpr uint32_t utf8_1byte_limit = 0x80;
constexpr uint32_t utf8_2byte_limit = 0x800;
constexpr uint32_t utf8_3byte_limit = 0x10000;
constexpr uint32_t utf8_2byte_start = 0xc0;
constexpr uint32_t utf8_3byte_start = 0xe0;
constexpr uint32_t utf8_4byte_start = 0xf0;
constexpr uint32_t utf8_cont = 0x80;
constexpr uint32_t utf8_cont_mask = 0x3f;
// UTF-8 encoding: bit shifts for extracting codepoint payload into continuation bytes (RFC 3629).
constexpr int utf8_payload_shift_1 = 6;  // Extract bits for 1st continuation byte
constexpr int utf8_payload_shift_2 = 12; // Extract bits for 2nd continuation byte
constexpr int utf8_payload_shift_3 = 18; // Extract bits for 3rd continuation byte

// nvim_input key names for special keys: :help keycodes.
constexpr const char *nvim_key_cr = "CR";
constexpr const char *nvim_key_bs = "BS";
constexpr const char *nvim_key_tab = "Tab";
constexpr const char *nvim_key_esc = "Esc";
constexpr const char *nvim_key_del = "Del";
constexpr const char *nvim_key_insert = "Insert";
constexpr const char *nvim_key_home = "Home";
constexpr const char *nvim_key_end = "End";
constexpr const char *nvim_key_pageup = "PageUp";
constexpr const char *nvim_key_pagedown = "PageDown";
constexpr const char *nvim_key_left = "Left";
constexpr const char *nvim_key_right = "Right";
constexpr const char *nvim_key_up = "Up";
constexpr const char *nvim_key_down = "Down";
constexpr const char *nvim_key_f2 = "F2";
constexpr const char *nvim_key_f3 = "F3";
constexpr const char *nvim_key_f4 = "F4";
constexpr const char *nvim_key_f8 = "F8";
constexpr const char *nvim_key_f9 = "F9";
constexpr const char *nvim_key_f10 = "F10";
constexpr const char *nvim_key_f11 = "F11";
constexpr const char *nvim_key_f12 = "F12";
constexpr const char *nvim_key_space = "Space";

// nvim_input modifier prefixes: :help <C-...>.
constexpr const char *nvim_mod_ctrl = "C-";
constexpr const char *nvim_mod_alt = "M-";
constexpr const char *nvim_mod_super = "D-";
constexpr const char *nvim_mod_shift = "S-";

// nvim_input literal angle bracket.
constexpr const char *nvim_angle_bracket = "<lt>";

// One code point as UTF-8 (ImGui's own encoder is internal API, 0001).
void rv_editor_utf8_append(std::string &out, uint32_t cp)
{
    if (cp < utf8_1byte_limit) {
        out += static_cast<char>(cp);
    } else if (cp < utf8_2byte_limit) {
        out += static_cast<char>(utf8_2byte_start | (cp >> utf8_payload_shift_1));
        out += static_cast<char>(utf8_cont | (cp & utf8_cont_mask));
    } else if (cp < utf8_3byte_limit) {
        out += static_cast<char>(utf8_3byte_start | (cp >> utf8_payload_shift_2));
        out += static_cast<char>(utf8_cont | ((cp >> utf8_payload_shift_1) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont | (cp & utf8_cont_mask));
    } else {
        out += static_cast<char>(utf8_4byte_start | (cp >> utf8_payload_shift_3));
        out += static_cast<char>(utf8_cont | ((cp >> utf8_payload_shift_2) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont | ((cp >> utf8_payload_shift_1) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont | (cp & utf8_cont_mask));
    }
}

// Key name in nvim_input notation, or nullptr for keys typed as text.
const char *rv_editor_nvim_key(ImGuiKey key)
{
    switch (key) {
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter:
        return nvim_key_cr;
    case ImGuiKey_Backspace:
        return nvim_key_bs;
    case ImGuiKey_Tab:
        return nvim_key_tab;
    case ImGuiKey_Escape:
        return nvim_key_esc;
    case ImGuiKey_Delete:
        return nvim_key_del;
    case ImGuiKey_Insert:
        return nvim_key_insert;
    case ImGuiKey_Home:
        return nvim_key_home;
    case ImGuiKey_End:
        return nvim_key_end;
    case ImGuiKey_PageUp:
        return nvim_key_pageup;
    case ImGuiKey_PageDown:
        return nvim_key_pagedown;
    case ImGuiKey_LeftArrow:
        return nvim_key_left;
    case ImGuiKey_RightArrow:
        return nvim_key_right;
    case ImGuiKey_UpArrow:
        return nvim_key_up;
    case ImGuiKey_DownArrow:
        return nvim_key_down;
    case ImGuiKey_F2:
        return nvim_key_f2;
    case ImGuiKey_F3:
        return nvim_key_f3;
    case ImGuiKey_F4:
        return nvim_key_f4;
    case ImGuiKey_F8:
        return nvim_key_f8;
    case ImGuiKey_F9:
        return nvim_key_f9;
    case ImGuiKey_F10:
        return nvim_key_f10;
    case ImGuiKey_F11:
        return nvim_key_f11;
    case ImGuiKey_F12:
        return nvim_key_f12;
    default:
        return nullptr;
    }
}

} // namespace

// This frame's keyboard as nvim keys. F1, F5, F6, F7 and Ctrl+B stay the editor's
// own (section 12); everything else typed into a focused code tile is nvim's.
std::string rv_editor_nvim_keys()
{
    const ImGuiIO &io = ImGui::GetIO();
    std::string keys;
    std::string mods;
    if (io.KeyCtrl) {
        mods += nvim_mod_ctrl;
    }
    if (io.KeyAlt) {
        mods += nvim_mod_alt;
    }
    if (io.KeySuper) {
        mods += nvim_mod_super;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!ImGui::IsKeyPressed(key, true)) {
            continue;
        }
        if (const char *name = rv_editor_nvim_key(key)) {
            keys += "<" + std::string(io.KeyShift ? nvim_mod_shift : "") + mods + name + ">";
            continue;
        }
        // Letters and digits with Ctrl or Alt arrive as keys, not as text.
        if ((io.KeyCtrl || io.KeyAlt) && !(io.KeyCtrl && key == ImGuiKey_B)) {
            char ch = 0;
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                ch = static_cast<char>('a' + (key - ImGuiKey_A));
            } else if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
                ch = static_cast<char>('0' + (key - ImGuiKey_0));
            } else if (key == ImGuiKey_Space) {
                keys += "<" + std::string(io.KeyShift ? nvim_mod_shift : "") + mods + nvim_key_space + ">";
                continue;
            }
            if (ch != 0) {
                keys += "<" + std::string(io.KeyShift ? nvim_mod_shift : "") + mods + std::string(1, ch) + ">";
            }
        }
    }
    // Typed text, Cyrillic included, as UTF-8; "<" is spelled out.
    if (!io.KeyCtrl && !io.KeyAlt) {
        for (const ImWchar ch : io.InputQueueCharacters) {
            if (ch == '<') {
                keys += nvim_angle_bracket;
                continue;
            }
            rv_editor_utf8_append(keys, ch);
        }
    }
    return keys;
}

} // namespace rv_editor
