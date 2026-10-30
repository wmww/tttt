#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stc/cstr.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "subproc.h"
#include "string_helpers.h"
#include "common_macros.h"
#include "failable.h"
#include "display.h"
#include "log.h"

#define CLOSE_AND_CLEAR(fd) close(fd); fd = -1;

failable read_fd_into_cstr(int fd, cstr* target) {
// This is not a hard limit, if it's exceeded read is called multiple times
#define BUF_LEN 2048
    check(target);
    char buffer[BUF_LEN];
    while (true) {
        ssize_t bytes_read = read(fd, buffer, BUF_LEN);
        if (bytes_read == 0) {
            break;
        } else if (bytes_read > 0) {
            cstr_append_sv(target, c_sv(buffer, bytes_read));
        } else {
            return err_d("failed to read from pipe: ", strerror(errno));
        }
    }
    return ok;
#undef BUF_LEN
}

DisplayCtx subproc_cmd_display(const char** cmd, size_t size, DisplayCtx d) {
    bool has_items = false;
    for (size_t i = 0; i < size; i++) {
        if (!cmd[i]) continue;
        if (has_items) d = lit_display(" ", d);
        csview arg = csview_from(cmd[i]);
        bool has_whitespace = !arg.size;
        for_each (c, csview, &arg) {
            if (!csview_trim_whitespace(c).size) {
                has_whitespace = true;
                break;
            }
        }
        if (has_whitespace) {
            d = string_debug_display(arg, d);
        } else {
            d = sv_display(arg, d);
        }
        has_items = true;
    }
    return d;
}

#ifdef _WIN32
// (Windows: no fork(); subprocesses aren't supported)
failable fork_subproc(bool capture_stdout, bool capture_stderr, returns ForkSubprocRet) {
    ignore capture_stdout;
    ignore capture_stderr;
    return err(string_error_new(cstr_lit("fork_subproc: not supported on Windows")));
}

failable subproc_wait(ForkSubprocRet subproc, cstr* stdout_target, cstr* stderr_target, int* exit_code_target, int* exit_signal_target) {
    ignore subproc; ignore stdout_target; ignore stderr_target; ignore exit_code_target; ignore exit_signal_target;
    return err(string_error_new(cstr_lit("subproc_wait: not supported on Windows")));
}

failable run_subproc(SubprocArgs args) {
    ignore args;
    return err(string_error_new(cstr_lit("run_subproc: not supported on Windows")));
}
#else
failable fork_subproc(bool capture_stdout, bool capture_stderr, returns ForkSubprocRet) {
    int out_pipe_fd[2] = {-1, -1};
    int err_pipe_fd[2] = {-1, -1};
    FailableResult error_result = {0};

    if ((capture_stdout ? pipe(out_pipe_fd) < 0 : false) ||
        (capture_stderr ? pipe(err_pipe_fd) < 0 : false)
    ) {
        error_result = err_d("failed to open pipe: ", strerror(errno));
        goto error;
    }

    pid_t pid = fork();
    if (pid < 0) {
        error_result = err_d("failed to fork: ", strerror(errno));
        goto error;
    } else if (pid == 0) {
        // We're in the child process
        if (capture_stdout) {
            if (dup2(out_pipe_fd[1], STDOUT_FILENO) < 0) {
                perror("stdout dup2 failed");
                abort();
            }
        }
        CLOSE_AND_CLEAR(out_pipe_fd[0]);
        CLOSE_AND_CLEAR(out_pipe_fd[1]);

        if (capture_stderr) {
            if (dup2(err_pipe_fd[1], STDERR_FILENO) < 0) {
                perror("stderr dup2 failed");
                abort();
            }
        }
        CLOSE_AND_CLEAR(err_pipe_fd[0]);
        CLOSE_AND_CLEAR(err_pipe_fd[1]);

        return ok((ForkSubprocRet) {
            .stdout_fd = -1,
            .stderr_fd = -1,
            .in_child = true,
        });
    } else {
        CLOSE_AND_CLEAR(out_pipe_fd[1]);
        CLOSE_AND_CLEAR(err_pipe_fd[1]);

        return ok((ForkSubprocRet) {
            .stdout_fd = out_pipe_fd[0],
            .stderr_fd = err_pipe_fd[0],
            .pid = pid,
        });
    }

error:
    if (out_pipe_fd[0] >= 0) close(out_pipe_fd[0]);
    if (out_pipe_fd[1] >= 0) close(out_pipe_fd[1]);
    if (err_pipe_fd[0] >= 0) close(err_pipe_fd[0]);
    if (err_pipe_fd[1] >= 0) close(err_pipe_fd[1]);
    return error_result;
}

failable subproc_wait(
    ForkSubprocRet subproc,
    cstr* stdout_target,
    cstr* stderr_target,
    int* exit_code_target,
    int* exit_signal_target
) {
    int exit_code   = -1;
    int exit_signal = -1;
    FailableResult result = {0};
    if (!subproc.in_child) {
        int child_status;
        if (waitpid(subproc.pid, &child_status, 0) < 0) {
            add_error_to_collection(&result, err_d("waitpid failed: ", strerror(errno)).error);
        } else if (WIFEXITED(child_status)) {
            exit_code = WEXITSTATUS(child_status);
        } else if (WIFSIGNALED(child_status)) {
            exit_signal = WTERMSIG(child_status);
        } else {
            add_error_to_collection(&result, err_d("waitpid unexpected status: ", child_status).error);
        }

        if (subproc.stdout_fd >= 0 && stdout_target) {
            try (read_fd_into_cstr(subproc.stdout_fd, stdout_target)) or_collect_into(&result);
        }

        if (subproc.stderr_fd >= 0 && stderr_target) {
            try (read_fd_into_cstr(subproc.stderr_fd, stderr_target)) or_collect_into(&result);
        }
    }

    if (exit_code_target)   *exit_code_target   = exit_code;
    if (exit_signal_target) *exit_signal_target = exit_signal;
    if (subproc.stdout_fd >= 0) close(subproc.stdout_fd);
    if (subproc.stderr_fd >= 0) close(subproc.stderr_fd);
    return result;
}

failable run_subproc(SubprocArgs args) {
    cstr stdout_tmp = cstr_null;
    cstr stderr_tmp = cstr_null;
    bool capture_stdout = args.stdout || !args.output_passthrough;
    bool capture_stderr = args.stderr || !args.output_passthrough;
    int exit_code = -1, exit_signal = -1;
    FailableResult result = {0};
    VecStr cmd = vec_str_init();

    bool using_vec = vec_str_size(&args.vec_cmd);
    const char** cmd_start = using_vec ? args.vec_cmd.data : args.cmd;
    const char** cmd_end = cmd_start + (using_vec ? args.vec_cmd.size : c_arraylen(args.cmd));
    for (const char** i = cmd_start; i < cmd_end; i++) {
        if (*i) vec_str_push(&cmd, *i);
    }
    vec_str_drop(&args.vec_cmd);
    if (vec_str_is_empty(&cmd)) {
        result = err("subproc command empty");
        goto finish;
    }
    vec_str_push(&cmd, nullptr);

    // Arguments are only actually formatted if debug logging enabled
    log_display_ctx(
        &logger_config,
        LOG_LEVEL_DEBUG,
        subproc_cmd_display(
            cmd.data,
            cmd.size,
            lit_display("$ ", logger_config.display_ctx)
        )
    );

    ForkSubprocRet fork_ret;
    result = fork_subproc(capture_stdout, capture_stderr, &fork_ret);
    if (result.error) goto finish;

    if (fork_ret.in_child) {
        execvp(*cmd.data, (char* const*)cmd.data);
        // execvp never returns unless there's an error
        perror("execvp failed");
        abort();
    } else {
        result = subproc_wait(fork_ret, &stdout_tmp, &stderr_tmp, &exit_code, &exit_signal);
    }
    if (result.error) goto finish;

    if (args.err_if_nonzero && exit_code != 0) {
        DisplayCtx d = { .color = false };
        d = lit_display("`", d);
        d = subproc_cmd_display(cmd.data, cmd.size, d);
        d = lit_display("` exited with ", d);
        if (exit_code > 0) {
            d = display_sequence(d, "code ", exit_code);
        } else {
            d = display_sequence(d, "signal ", exit_signal);
        }
        if (cstr_size(&stdout_tmp)) {
            d = display_sequence(d, nl, "stdout:", nl, csview_trim_whitespace(cstr_sv(&stdout_tmp)));
        }
        if (cstr_size(&stderr_tmp)) {
            d = display_sequence(d, nl, "stderr:", nl, csview_trim_whitespace(cstr_sv(&stderr_tmp)));
        }
        result.error = string_error_new(display_into_cstr(d));
    }

finish:
    static const char* log_start = " '- subproc exited with ";
    if (result.error) {
        log_debug(log_start, "error");
    } else {
        log_debug(
            log_start,
            exit_code > 0 ? "code " : "signal ",
            exit_code > 0 ? exit_code : exit_signal
        );
    }
    if (args.stdout) *args.stdout = cstr_move(&stdout_tmp);
    if (args.stderr) *args.stderr = cstr_move(&stderr_tmp);
    if (args.exit_code) {
        if (exit_code >= 0) *args.exit_code = exit_code;
        else if (exit_signal > 0) *args.exit_code = -exit_signal;
        else *args.exit_code = INT_MIN;
    }
    vec_str_drop(&cmd);
    cstr_drop(&stderr_tmp);
    cstr_drop(&stdout_tmp);
    return result;
}
#endif
