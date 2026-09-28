#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <cleat/subproc.h>

#include "subprocess.h"

// Each side goes into an unlinked temporary file (a pipe would deadlock once a side is bigger than its buffer): the
// subprocess inherits the descriptor and opens it through /dev/fd, which on Linux starts reading at the beginning
static_failable write_temp(csview sv, FILE** out) {
    *out = tmpfile();
    if (!*out) return err_d("failed to create a temporary file: ", strerror(errno));
    if (fwrite(sv.buf, 1, (size_t)csview_size(sv), *out) < (size_t)csview_size(sv) || fflush(*out) != 0) {
        return err_d("failed to write a temporary file: ", strerror(errno));
    }
    return ok;
}

// Does the actual work of run_diff_subprocess(). The files are owned by the caller, which closes whichever are open
// however this ends up returning
static_failable run_diff(csview a, csview b, FILE** a_file, FILE** b_file) {
    try (write_temp(a, a_file)) bubble;
    try (write_temp(b, b_file)) bubble;

    cstr a_arg = cstr_d("/dev/fd/", fileno(*a_file));
    cstr b_arg = cstr_d("/dev/fd/", fileno(*b_file));

    try (run_subproc((SubprocArgs){
        // Other commands that have been handy here:
        // .cmd = {"meld", cstr_str(&a_arg), cstr_str(&b_arg)},
        // .cmd = {"byteviewer", "-hex", "-ascii", "-utf8", "-width", "24", "-file", cstr_str(&b_arg)},
        .cmd = {"diff", "--color=always", "--unified=100", cstr_str(&a_arg), cstr_str(&b_arg)},
        .output_passthrough = true,
    })) cleanup {
        c_drop(cstr, &a_arg, &b_arg);
    }

    c_drop(cstr, &a_arg, &b_arg);
    return ok;
}

void run_diff_subprocess(csview a, csview b) {
    FILE* a_file = nullptr;
    FILE* b_file = nullptr;

    try (run_diff(a, b, &a_file, &b_file)) or_log(error);

    if (a_file) fclose(a_file);
    if (b_file) fclose(b_file);
}
