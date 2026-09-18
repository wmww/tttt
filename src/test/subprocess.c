#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <cleat/subproc.h>

#include "subprocess.h"

static bool write_sv(int fd, csview sv) {
    if (write(fd, sv.buf, csview_size(sv)) < csview_size(sv)) {
        fprintf(stderr, "failed to write to pipe: %s\n", strerror(errno));
        return false;
    }
    return true;
}

void run_diff_subprocess(csview a, csview b) {
    int a_pipe[2] = {-1, -1};
    int b_pipe[2] = {-1, -1};
    cstr a_arg = cstr_null;
    cstr b_arg = cstr_null;

    if (pipe(a_pipe) < 0 || pipe(b_pipe) < 0) {
        fprintf(stderr, "failed to open pipe: %s\n", strerror(errno));
        goto finish;
    }

    // NOTE: this will deadlock if either string is bigger than the pipe buffer (64KB by default). Test outputs have
    // stayed well under that so far.
    if (!write_sv(a_pipe[1], a) || !write_sv(b_pipe[1], b)) {
        goto finish;
    }

    close(a_pipe[1]);
    a_pipe[1] = -1;
    close(b_pipe[1]);
    b_pipe[1] = -1;

    // The read ends of the pipes are inherited by the subprocess, which reads them through /dev/fd
    a_arg = cstr_from_fmt("/dev/fd/%d", a_pipe[0]);
    b_arg = cstr_from_fmt("/dev/fd/%d", b_pipe[0]);

    try (run_subproc((SubprocArgs){
        // Other commands that have been handy here:
        // .cmd = {"meld", cstr_str(&a_arg), cstr_str(&b_arg)},
        // .cmd = {"byteviewer", "-hex", "-ascii", "-utf8", "-width", "24", "-file", cstr_str(&b_arg)},
        .cmd = {"diff", "--color=always", "--unified=100", cstr_str(&a_arg), cstr_str(&b_arg)},
        .output_passthrough = true,
    })) or_log(error);

finish:
    if (a_pipe[0] >= 0) close(a_pipe[0]);
    if (a_pipe[1] >= 0) close(a_pipe[1]);
    if (b_pipe[0] >= 0) close(b_pipe[0]);
    if (b_pipe[1] >= 0) close(b_pipe[1]);
    cstr_drop(&a_arg);
    cstr_drop(&b_arg);
}
