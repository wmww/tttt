#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <cleat/subproc.h>

#include "subprocess.h"

static_failable write_sv(int fd, csview sv) {
    if (write(fd, sv.buf, csview_size(sv)) < csview_size(sv)) {
        return err_d("failed to write to pipe: ", strerror(errno));
    }
    return ok;
}

static void close_fd(int* fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

// Does the actual work of run_diff_subprocess(). The pipes are owned by the caller, which closes whichever ends are
// still open however this ends up returning
static_failable run_diff(csview a, csview b, int a_pipe[2], int b_pipe[2]) {
    if (pipe(a_pipe) < 0 || pipe(b_pipe) < 0) {
        return err_d("failed to open pipe: ", strerror(errno));
    }

    // NOTE: this will deadlock if either string is bigger than the pipe buffer (64KB by default). Test outputs have
    // stayed well under that so far.
    try (write_sv(a_pipe[1], a)) bubble;
    try (write_sv(b_pipe[1], b)) bubble;

    close_fd(&a_pipe[1]);
    close_fd(&b_pipe[1]);

    // The read ends of the pipes are inherited by the subprocess, which reads them through /dev/fd
    cstr a_arg = cstr_d("/dev/fd/", a_pipe[0]);
    cstr b_arg = cstr_d("/dev/fd/", b_pipe[0]);

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
    int a_pipe[2] = {-1, -1};
    int b_pipe[2] = {-1, -1};

    try (run_diff(a, b, a_pipe, b_pipe)) or_log(error);

    close_fd(&a_pipe[0]);
    close_fd(&a_pipe[1]);
    close_fd(&b_pipe[0]);
    close_fd(&b_pipe[1]);
}
