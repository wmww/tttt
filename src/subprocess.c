#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include "subprocess.h"

#define CLOSE_AND_CLEAR(fd) close(fd); fd = -1;

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
        goto cleanup;
    }

    if (!write_sv(a_pipe[1], a) ||
        !write_sv(b_pipe[1], b)
    ) {
        goto cleanup;
    }

    CLOSE_AND_CLEAR(a_pipe[1]);
    CLOSE_AND_CLEAR(b_pipe[1]);

    a_arg = cstr_from_fmt("/dev/fd/%d", a_pipe[0]);
    b_arg = cstr_from_fmt("/dev/fd/%d", b_pipe[0]);
    const char* args[] = {
        "diff",
        "--color=always",
        "--unified=100",
        cstr_str(&a_arg),
        cstr_str(&b_arg),
        nullptr,
    };
    /*
    const char* args[] = {
        "byteviewer",
        "-hex",
        "-ascii",
        "-utf8",
        "-width",
        "24",
        "-file",
        cstr_str(&b_arg),
        nullptr,
    };
    */

    run_subprocess(args);

cleanup:
    if (a_pipe[0] >= 0) close(a_pipe[0]);
    if (a_pipe[1] >= 0) close(a_pipe[1]);
    if (b_pipe[0] >= 0) close(b_pipe[0]);
    if (b_pipe[1] >= 0) close(b_pipe[1]);
    cstr_drop(&a_arg);
    cstr_drop(&b_arg);
}

int run_subprocess(const char* const* cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "failed to fork: %s\n", strerror(errno));
        return -1;
    } else if (pid == 0) {
        execvp(cmd[0], (char* const*)cmd);
        // execvp never returns unless there's an error
        fprintf(stderr, "execvp failed: %s\n", strerror(errno));
        exit(1);
    } else {
        int child_status;
        if (waitpid(pid, &child_status, 0) < 0) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            return -1;
        }
        return WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 256;
    }
}
