#pragma once

#include "string_helpers.h"
#include "failable_minimal.h"
#include "display_minimal.h"

// Target must be initialized, will be appended to, and must be dropped even in the error case
failable read_fd_into_cstr(int fd, cstr* target);

// Puts quotes around arguments with spaces and everything
DisplayCtx subproc_cmd_display(const char** cmd, size_t size, DisplayCtx d);

typedef struct ForkSubprocRet {
    int stdout_fd; // -1 if not captured or in child
    int stderr_fd; // -1 if not captured or in child
    int pid; // 0 if in child or on error
    bool in_child;
} ForkSubprocRet;
failable fork_subproc(bool capture_stdout, bool capture_stderr, returns ForkSubprocRet);

// Targets may be null, target strings must be initialized if non-null. Target strings may need to be
// dropped, even on the error case, target ints are -1 if not applicable
failable subproc_wait(
    ForkSubprocRet subproc,
    cstr* stdout_target,
    cstr* stderr_target,
    int* exit_code_target,
    int* exit_signal_target
);

// All can be 0 initialized
#ifdef _WIN32
// (stdout and stderr are macros there)
#pragma push_macro("stdout")
#pragma push_macro("stderr")
#undef stdout
#undef stderr
#endif
typedef struct SubprocArgs {
    // The command arguments, nulls are ignored and may appear in the middle of the command
    const char* cmd[16];
    // Alternative to cmd, cmd is ignored if initialized
    VecStr vec_cmd;
    // Destination to capture subprocess's stdout into. Target must be uninitialized. Always set, even on error.
    cstr* stdout;
    // Destination to capture subprocess's stderr into. Target must be uninitialized. Always set, even on error.
    cstr* stderr;
    // Destination for exit code, Always set, even on error, <0 indicates signal
    int* exit_code;
    // Display pass output on to our stdout/stderr. Only applicable for streams that are null above.
    bool output_passthrough;
    // If set, returns an error on nonzero exit code
    bool err_if_nonzero;
} SubprocArgs;
#ifdef _WIN32
#pragma pop_macro("stdout")
#pragma pop_macro("stderr")
#endif
failable run_subproc(SubprocArgs args);
