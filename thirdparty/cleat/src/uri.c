#include "uri.h"
#include "filesystem.h"
#include "failable.h"
#include <stc/csview.h>

const csview terminal_link_begin_uri = c_sv_const_init("\x1b]8;;");
const csview terminal_link_uri_to_text = c_sv_const_init("\x1b\\");
const csview terminal_link_end_text = c_sv_const_init("\x1b]8;;\x1b\\");

// Checks if the pointed-at character matches the regex [A-Za-z0-9\-_.~/:]
// To regenerate run cleat/src/gen_chartest.py is_file_uri_safe 'A-Za-z0-9\-_.~/:'
static inline bool is_file_uri_safe(char const* char_ptr) {
    static const u64 magic[] = {0x7ffe00000000000, 0x47fffffe87fffffe, 0x0, 0x0};
    unsigned char c = *(unsigned char*)char_ptr;
    return (magic[(c >> 6) & 0x3] >> (c & 0x3f)) & 0x1;
}

void uri_encode_append(csview scheme, csview data, cstr* target) {
    cstr_append_sv(target, scheme);
    for (i64 i = 0; i < data.size; i++) {
        char c = data.buf[i];
        if (is_file_uri_safe(&c)) {
            cstr_append_sv(target, c_sv(&c, 1));
        } else {
            cstr_append_fmt(target, "%%%02X", (unsigned)(unsigned char)c);
        }
    }
}

failable existing_file_uri_encode_append(csview path, i64 line, cstr* target) {
    try (cstr absolute, fs_absolute_path_of(fs, path, &absolute)) bubble;
    if (!fs_is_reg_file(fs, cstr_sv(&absolute))) {
        cstr_drop(&absolute);
        try (cstr cwd, fs_get_cwd(fs, &cwd)) bubble;
        // May work even if dir is not build/, because path is likely a ../ relative path
        cstr_append_lit(&cwd, "/build");
        absolute = absolute_path_of(cstr_sv(&cwd), path);
        cstr_drop(&cwd);
    }
    if (!fs_is_reg_file(fs, cstr_sv(&absolute))) {
        // give up
        cstr_drop(&absolute);
        return err_fmt("could not find %.*s", c_SV(path));
    }
    if (line >= 0) cstr_append_fmt(&absolute, ":%lld", (long long)line);
    uri_encode_append(c_sv("file://"), cstr_sv(&absolute), target);
    cstr_drop(&absolute);
    return ok;
}
