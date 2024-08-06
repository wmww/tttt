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
    /*
    const char* args[] = {
        "meld",
        cstr_str(&a_arg),
        cstr_str(&b_arg),
        nullptr,
    };
    */
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

static bool read_fd_into_cstr(int fd, cstr* dest) {
// This is not a hard limit, if it's exceeded read is called multiple times
#define BUF_LEN 2048
    cstr ret = cstr_null;
    char buffer[BUF_LEN];
    while (true) {
        ssize_t bytes_read = read(fd, buffer, BUF_LEN);
        if (bytes_read == 0) {
            break;
        } else if (bytes_read > 0) {
            cstr_append_sv(&ret, c_sv(buffer, bytes_read));
        } else {
            fprintf(stderr, "failed to read from pipe: %s\n", strerror(errno));
            cstr_drop(&ret);
            return false;
        }
    }
    cstr_take(dest, ret);
    return true;
#undef BUF_LEN
}

int run_subprocess_get_output(const char* const* cmd, cstr* out, cstr* err) {
    // Create a pipe
    int out_pipe_fd[2] = {-1, -1};
    int err_pipe_fd[2] = {-1, -1};
    cstr stdout_tmp = cstr_null;
    cstr stderr_tmp = cstr_null;
    if (pipe(out_pipe_fd) < 0 || pipe(err_pipe_fd) < 0) {
        fprintf(stderr, "failed to open pipe: %s\n", strerror(errno));
        goto error;
    }

    if (pipe(err_pipe_fd) < 0) {
        fprintf(stderr, "failed to open pipe: %s\n", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "failed to fork: %s\n", strerror(errno));
        goto error;
    } else if (pid == 0) {
        // We're in the child process
        dup2(out_pipe_fd[1], STDOUT_FILENO);
        CLOSE_AND_CLEAR(out_pipe_fd[0]);
        CLOSE_AND_CLEAR(out_pipe_fd[1]);

        dup2(err_pipe_fd[1], STDERR_FILENO);
        CLOSE_AND_CLEAR(err_pipe_fd[0]);
        CLOSE_AND_CLEAR(err_pipe_fd[1]);

        execvp(cmd[0], (char* const*)cmd);
        // execvp never returns unless there's an error
        fprintf(stderr, "execvp failed: %s\n", strerror(errno));
        exit(1);
    } else {
        CLOSE_AND_CLEAR(out_pipe_fd[1]);
        CLOSE_AND_CLEAR(err_pipe_fd[1]);

        if (
            !read_fd_into_cstr(out_pipe_fd[0], &stdout_tmp) ||
            !read_fd_into_cstr(err_pipe_fd[0], &stderr_tmp)
        ) {
            goto error;
        }

        CLOSE_AND_CLEAR(out_pipe_fd[0]);
        CLOSE_AND_CLEAR(err_pipe_fd[0]);

        int child_status;
        if (waitpid(pid, &child_status, 0) < 0) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            goto error;
        }

        if (out) {
            cstr_take(out, stdout_tmp);
        } else {
            cstr_drop(&stdout_tmp);
        }
        if (err) {
            cstr_take(err, stderr_tmp);
        } else {
            cstr_drop(&stderr_tmp);
        }
        return WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 256;
    }

error:
    if (out_pipe_fd[0] >= 0) close(out_pipe_fd[0]);
    if (out_pipe_fd[1] >= 0) close(out_pipe_fd[1]);
    if (err_pipe_fd[0] >= 0) close(err_pipe_fd[0]);
    if (err_pipe_fd[1] >= 0) close(err_pipe_fd[1]);
    cstr_drop(&stdout_tmp);
    cstr_drop(&stderr_tmp);
    return -1;
}
