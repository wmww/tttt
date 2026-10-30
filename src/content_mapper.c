#define _GNU_SOURCE // posix_spawn_file_actions_addchdir_np
#include "content_mapper.h"
#include "json.h"
#include "error.h"
#include "source_file.h"
#include "lib_names.h"
#include "string_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>

extern char** environ;
#ifdef __GLIBC__
// (glibc 2.29's: weak, so a binary built against an older glibc (the release build's 2.28 floor) binds it where the
// running glibc has it and spawns through sh's cd where it doesn't)
extern int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t*, char const*) __attribute__((weak));
#endif
#endif

void content_mapper_drop(ContentMapper* me) {
    c_drop(cstr, &my package, &my options_json, &my name, &my version, &my package_dir);
    str_list_drop(&my extensions);
    str_list_drop(&my exec);
    str_list_drop(&my compiler_options);
}

cstr content_mapper_identity(ContentMapper const* me) {
    return cstr_size(&my version) ? cstr_d(my name, "@", my version) : cstr_clone(my name);
}

static bool path_has_extension(csview path, csview extension) {
    // (tspath's ExtensionIsOneOf: case-sensitive)
    return path.size > extension.size && !memcmp(path.buf + path.size - extension.size, extension.buf, (size_t)extension.size);
}

ContentMapper const* content_mapper_for_path(CompilerOptions const* options, csview path) {
    for (u32 i = 0; i < options->content_mapper_count; i++) {
        ContentMapper const* mapper = &options->content_mappers[i];
        for (isize j = 0; j < mapper->extensions.count; j++) {
            if (path_has_extension(path, cstr_sv(&mapper->extensions.items[j]))) return mapper;
        }
    }
    return nullptr;
}

bool content_mapper_has_extension(CompilerOptions const* options, csview extension) {
    for (u32 i = 0; i < options->content_mapper_count; i++) {
        StrList const* extensions = &options->content_mappers[i].extensions;
        for (isize j = 0; j < extensions->count; j++) {
            if (csview_equals_sv(cstr_sv(&extensions->items[j]), extension)) return true;
        }
    }
    return false;
}

void content_mapper_output_drop(ContentMapperOutput* me) {
    c_drop(cstr, &my text, &my extension, &my diagnostic_source, &my failure, &my detail, &my global, &my global_detail);
    free(my segments);
    for (u32 i = 0; i < my diagnostic_count; i++) cstr_drop(&my diagnostics[i].message);
    free(my diagnostics);
    for (u32 i = 0; i < my directive_count; i++) cstr_drop(&my directives[i].unused_message);
    free(my directives);
    for (u32 i = 0; i < my supplemental_count; i++) {
        ContentMapperSupplemental* supplemental = &my supplemental[i];
        c_drop(cstr, &supplemental->text, &supplemental->extension);
        free(supplemental->segments);
        for (u32 j = 0; j < supplemental->directive_count; j++) cstr_drop(&supplemental->directives[j].unused_message);
        free(supplemental->directives);
    }
    free(my supplemental);
    cstr_drop(&my detail_sub);
    *me = (ContentMapperOutput){0};
}

static char const* launcher;

void content_mapper_set_launcher(char const* path) {
    launcher = path;
}

// ---- JSON text

// The value of tsgo's json.Marshal(CompilerOptions) for openProject: the options that are set, by their names (enums
// as tsgo's numbers). Not every option tttt has (paths, lib...): TODO(plan 56) if a mapper ever needs them
static void append_compiler_options(cstr* out, CompilerOptions const* options) {
    static char const* const bool_names[] = {
#define X(field, name, strict_family) name,
        COMPILER_BOOL_OPTIONS(X)
#undef X
    };
    cstr_append_lit(out, "{");
    bool first = true;
#define OPTION(name, ...) do { \
        if (!first) cstr_append_lit(out, ","); \
        first = false; \
        json_append_string(out, csview_from(name)); \
        cstr_append_lit(out, ":"); \
        __VA_ARGS__; \
    } while (0)
    for (u32 j = 0; j < c_arraylen(bool_names); j++) {
        if (!options->is_set[j]) continue;
        for (u32 k = 0; k < tsc_option_names_count; k++) {
            if (strcasecmp(bool_names[j], tsc_option_names[k])) continue;
            OPTION(tsc_option_names[k], cstr_append(out, ((bool const*)options)[j] ? "true" : "false"));
            break;
        }
    }
    if (options->target_set) {
        // (tsgo's ScriptTarget: ES5 1 ... ES2026 13, ESNext 99)
        OPTION("target", cstr_append_d(out, DEFAULT_DISPLAY, options->target == SCRIPT_TARGET_ESNEXT ? 99 : (int)options->target + 1));
    }
    if (options->module_set) {
        static int const kinds[] = { 0, 1, 2, 3, 4, 5, 6, 7, 99, 100, 101, 102, 199, 200 };
        OPTION("module", cstr_append_d(out, DEFAULT_DISPLAY, kinds[options->module]));
    }
    if (options->module_resolution_set) {
        static int const kinds[] = { 0, 1, 2, 3, 99, 100 };
        OPTION("moduleResolution", cstr_append_d(out, DEFAULT_DISPLAY, kinds[options->module_resolution]));
    }
    if (options->jsx != JSX_NONE) OPTION("jsx", cstr_append_d(out, DEFAULT_DISPLAY, (int)options->jsx));
    if (cstr_size(&options->jsx_factory)) OPTION("jsxFactory", json_append_string(out, cstr_sv(&options->jsx_factory)));
    if (cstr_size(&options->jsx_fragment_factory)) {
        OPTION("jsxFragmentFactory", json_append_string(out, cstr_sv(&options->jsx_fragment_factory)));
    }
    if (cstr_size(&options->jsx_import_source)) {
        OPTION("jsxImportSource", json_append_string(out, cstr_sv(&options->jsx_import_source)));
    }
#undef OPTION
    cstr_append_lit(out, "}");
}

// ---- A mapper process

static bool json_int(JsonValue const* value, i64* out) {
    if (!value || value->tag != JSON_NUMBER || value->number != (double)(i64)value->number) return false;
    *out = (i64)value->number;
    return true;
}


typedef struct MapperConn {
    cstr key; // Identity and exec
#ifdef _WIN32
    HANDLE process; // Null when there is none
    int write_fd; // A pipe that is the process's stdin; fd the one that is its stdout
#else
    pid_t pid;
#endif
    int fd; // A socket that is the process's stdin and stdout
    cstr read_buffer;
    isize read_at;
    bool failed; // Couldn't start or initialize: not tried again
    bool failure_reported; // Its TS18099 went to a program
    bool utf16; // It chose utf-16 positions
    struct timespec deadline; // While initializing (tsgo's 5 s): reads give up after it
    bool timed_out;
    u32 transform_failures;
    unsigned failure_code; // The reason chained under TS18099 (TS18100 and friends)
    cstr failure;
    cstr diagnostic_source;
    cstr project_handle; // Opened for the program's project, empty until then
    bool project_failed;
    unsigned project_failure_code; // Why (TS18090-18095), 0 for TS18091
    unsigned next_id;
} MapperConn;

// What tsgo's fileLoader keeps per mapper entry (its *Mapper): failures counted, initialization failure reported
typedef struct MapperState {
    ContentMapper const* mapper;
    u32 transform_failures;
    bool failure_reported;
} MapperState;

struct ContentMapperHost {
    pthread_mutex_t lock;
    MapperState* states;
    u32 state_count;
    ContentMapperOptionDiagnostic** option_diagnostics;
    u32 option_diagnostic_count;
    cstr config_file;
    CompilerOptions const* options;
    MapperConn* conns;
    u32 conn_count;
    u32 next_project;
};

ContentMapperHost* content_mapper_host_new(csview config_file, CompilerOptions const* options) {
    ContentMapperHost* me = c_new(ContentMapperHost, { .config_file = cstr_from_sv(config_file), .options = options });
    pthread_mutex_init(&my lock, nullptr);
    return me;
}

static bool write_all(int fd, char const* buf, isize size) {
    while (size > 0) {
#ifdef _WIN32
        ssize_t n = write(fd, buf, (unsigned)(size < INT32_MAX ? size : INT32_MAX));
#else
        ssize_t n = send(fd, buf, (size_t)size, MSG_NOSIGNAL);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buf += n;
        size -= n;
    }
    return true;
}

static bool send_message(MapperConn* conn, csview body) {
    cstr header = cstr_d("Content-Length: ", body.size, "\r\n\r\n");
#ifdef _WIN32
    int fd = conn->write_fd;
#else
    int fd = conn->fd;
#endif
    bool sent = write_all(fd, cstr_str(&header), cstr_size(&header)) && write_all(fd, body.buf, body.size);
    cstr_drop(&header);
    return sent;
}

// More bytes into the read buffer, false at the end of the stream (or past conn->deadline: timed_out)
static bool fill(MapperConn* conn) {
    char buf[65536];
    if (conn->deadline.tv_sec) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        i64 left = (conn->deadline.tv_sec - now.tv_sec) * 1000 + (conn->deadline.tv_nsec - now.tv_nsec) / 1000000;
#ifdef _WIN32
        // (A pipe can't be waited on for data: polled. Only the 5 s of the initialize request read this way)
        HANDLE pipe = (HANDLE)_get_osfhandle(conn->fd);
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available) break;
            if (left <= 0) {
                conn->timed_out = true;
                return false;
            }
            Sleep(1);
            left--;
        }
#else
        struct pollfd pfd = { .fd = conn->fd, .events = POLLIN };
        if (left <= 0 || poll(&pfd, 1, (int)left) == 0) {
            conn->timed_out = true;
            return false;
        }
#endif
    }
    for (;;) {
        ssize_t n = read(conn->fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        cstr_append_n(&conn->read_buffer, buf, n);
        return true;
    }
}

// The next message's body (jsonrpc.Reader: headers up to an empty line, Content-Length among them)
static bool read_message(MapperConn* conn, cstr* out) {
    isize length = -1;
    for (;;) {
        csview buffered = csview_slice(cstr_sv(&conn->read_buffer), conn->read_at, INTPTR_MAX);
        char const* eol = memchr(buffered.buf, '\n', (size_t)buffered.size);
        if (!eol) {
            if (!fill(conn)) return false;
            continue;
        }
        csview line = { buffered.buf, eol - buffered.buf };
        conn->read_at += line.size + 1;
        if (line.size && line.buf[line.size - 1] == '\r') line.size--;
        if (!line.size) break;
        if (line.size > 15 && !strncasecmp(line.buf, "Content-Length:", 15)) {
            length = strtol(line.buf + 15, nullptr, 10);
        }
    }
    if (length <= 0) return false;
    while (cstr_size(&conn->read_buffer) - conn->read_at < length) {
        if (!fill(conn)) return false;
    }
    *out = cstr_from_sv(csview_slice(cstr_sv(&conn->read_buffer), conn->read_at, conn->read_at + length));
    conn->read_at += length;
    if (conn->read_at == cstr_size(&conn->read_buffer)) {
        cstr_clear(&conn->read_buffer);
        conn->read_at = 0;
    }
    return true;
}

typedef enum CallResult { CALL_OK, CALL_FAILED, CALL_ERROR } CallResult;

// A request and its response (the mapper's own requests and notifications are skipped: tsgo's host rejects them).
// CALL_ERROR: the mapper answered with an error, CALL_FAILED: the connection broke or the response isn't JSON-RPC
static CallResult call(MapperConn* conn, char const* method, csview params, JsonValue* result, JsonValue* response) {
    unsigned id = ++conn->next_id;
    cstr body = cstr_d("{\"jsonrpc\":\"2.0\",\"id\":", id, ",\"method\":\"", method, "\",\"params\":", params, "}");
    bool sent = send_message(conn, cstr_sv(&body));
    cstr_drop(&body);
    if (!sent) return CALL_FAILED;
    for (;;) {
        cstr text;
        if (!read_message(conn, &text)) return CALL_FAILED;
        bool parsed = json_parse_strict(cstr_sv(&text), response);
        cstr_drop(&text);
        if (!parsed || response->tag != JSON_OBJECT) {
            json_drop(response);
            return CALL_FAILED;
        }
        JsonValue const* got = json_get_tagged(response, "id", JSON_NUMBER);
        if (!got || json_get(response, "method") || got->number != id) {
            json_drop(response);
            continue;
        }
        if (json_get(response, "error")) return CALL_ERROR;
        JsonValue const* value = json_get(response, "result");
        *result = value ? *value : (JsonValue){0};
        return CALL_OK;
    }
}

static void conn_fail(MapperConn* conn, unsigned code, cstr message) {
    conn->failed = true;
    conn->failure_code = code;
    cstr_take(&conn->failure, message);
}

static void conn_close(MapperConn* conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
#ifdef _WIN32
    if (conn->write_fd >= 0) {
        close(conn->write_fd);
        conn->write_fd = -1;
    }
    if (conn->process) {
        if (WaitForSingleObject(conn->process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(conn->process, 1);
            WaitForSingleObject(conn->process, INFINITE);
        }
        CloseHandle(conn->process);
        conn->process = nullptr;
    }
#else
    if (conn->pid > 0) {
        int status;
        if (waitpid(conn->pid, &status, WNOHANG) == 0) {
            kill(conn->pid, SIGTERM);
            waitpid(conn->pid, &status, 0);
        }
        conn->pid = 0;
    }
#endif
}

// tsgo's handshake: typescript, tsc and the native extensions without their dot (`ts`, `d.ts`, `json`)
static bool reserved_diagnostic_source(csview source) {
    static char const* const reserved[] = { "typescript", "tsc", "ts", "tsx", "d.ts", "cts", "d.cts", "mts", "d.mts",
        "js", "jsx", "cjs", "mjs", "json" };
    for (u32 i = 0; i < c_arraylen(reserved); i++) {
        if (source.size == (isize)strlen(reserved[i]) && !strncasecmp(source.buf, reserved[i], (size_t)source.size)) return true;
    }
    return false;
}

#ifdef _WIN32
// arg quoted for a command line as the C runtime's argv parser (and Go's syscall.EscapeArg) reads it back
static void append_command_arg(cstr* out, csview arg) {
    if (cstr_size(out)) cstr_append_lit(out, " ");
    bool plain = arg.size > 0;
    for (isize i = 0; i < arg.size && plain; i++) plain = !strchr(" \t\n\v\"", arg.buf[i]);
    if (plain) {
        cstr_append_sv(out, arg);
        return;
    }
    cstr_append_lit(out, "\"");
    for (isize i = 0; i < arg.size; i++) {
        isize backslashes = 0;
        while (i < arg.size && arg.buf[i] == '\\') i++, backslashes++;
        // (Backslashes are literal unless a quote or the closing quote follows: then doubled)
        isize repeat = i == arg.size ? backslashes * 2 : arg.buf[i] == '"' ? backslashes * 2 + 1 : backslashes;
        for (isize j = 0; j < repeat; j++) cstr_append_lit(out, "\\");
        if (i < arg.size) cstr_append_n(out, &arg.buf[i], 1);
    }
    cstr_append_lit(out, "\"");
}

static wchar_t* wide_string(char const* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    wchar_t* ret = calloc(n > 0 ? (size_t)n : 1, sizeof(wchar_t));
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, ret, n);
    return ret;
}

// The mapper's process with pipes for its stdin and stdout (conn->write_fd, conn->fd), its stderr discarded: 0, or
// what failed (ENOENT: no such program)
static int mapper_spawn(MapperConn* conn, char const* const* argv, isize argc, char const* dir) {
    SECURITY_ATTRIBUTES inherit = { .nLength = sizeof inherit, .bInheritHandle = TRUE };
    HANDLE child_in = nullptr, to_child = nullptr, from_child = nullptr, child_out = nullptr;
    if (!CreatePipe(&child_in, &to_child, &inherit, 0)) return EMFILE;
    if (!CreatePipe(&from_child, &child_out, &inherit, 0)) {
        CloseHandle(child_in), CloseHandle(to_child);
        return EMFILE;
    }
    // (Our ends aren't inherited)
    SetHandleInformation(to_child, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(from_child, HANDLE_FLAG_INHERIT, 0);
    HANDLE null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    cstr command = cstr_null;
    for (isize i = 0; i < argc; i++) append_command_arg(&command, c_sv(argv[i], (isize)strlen(argv[i])));
    wchar_t* wide_command = wide_string(cstr_str(&command));
    wchar_t* wide_dir = dir ? wide_string(dir) : nullptr;
    STARTUPINFOW startup = { .cb = sizeof startup, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = child_in,
        .hStdOutput = child_out, .hStdError = null };
    PROCESS_INFORMATION info;
    // (The executable is looked up like Go's exec.LookPath would: CreateProcess searches the PATH for argv[0])
    BOOL created = CreateProcessW(nullptr, wide_command, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, wide_dir,
        &startup, &info);
    DWORD error = created ? 0 : GetLastError();
    free(wide_command);
    free(wide_dir);
    cstr_drop(&command);
    CloseHandle(child_in);
    CloseHandle(child_out);
    if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
    if (!created) {
        CloseHandle(to_child), CloseHandle(from_child);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
    }
    CloseHandle(info.hThread);
    conn->process = info.hProcess;
    conn->write_fd = _open_osfhandle((intptr_t)to_child, _O_WRONLY | _O_BINARY);
    conn->fd = _open_osfhandle((intptr_t)from_child, _O_RDONLY | _O_BINARY);
    return 0;
}
#endif

// Waits for the mapper's process to exit: true with its exit code (-1 when killed by a signal) in *code
static bool mapper_exited(MapperConn* conn, int* code) {
#ifdef _WIN32
    DWORD status;
    if (!conn->process || WaitForSingleObject(conn->process, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(conn->process, &status)) return false;
    CloseHandle(conn->process);
    conn->process = nullptr;
    *code = (int)status;
    return true;
#else
    int status;
    if (waitpid(conn->pid, &status, 0) != conn->pid) return false;
    conn->pid = 0;
    *code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
#endif
}

// Spawns the mapper (exec in its package directory, the launcher in front of it) and initializes it
static void conn_start(MapperConn* conn, ContentMapper const* mapper) {
#ifdef _WIN32
    char const** argv = calloc((size_t)mapper->exec.count + 2, sizeof(char*));
    isize argc = 0;
    if (launcher) argv[argc++] = launcher;
    for (isize i = 0; i < mapper->exec.count; i++) argv[argc++] = cstr_str(&mapper->exec.items[i]);
    struct stat st;
    bool in_dir = cstr_size(&mapper->package_dir) && stat(cstr_str(&mapper->package_dir), &st) == 0 && S_ISDIR(st.st_mode);
    int spawned = mapper_spawn(conn, argv, argc, in_dir ? cstr_str(&mapper->package_dir) : nullptr);
    free(argv);
    if (spawned != 0) {
        csview command = cstr_sv(&mapper->exec.items[0]);
        cstr detail = !launcher && !csview_contains(command, "/") && !csview_contains(command, "\\") && spawned == ENOENT
            ? cstr_d("exec: \"", command, "\": executable file not found in %PATH%")
            : cstr_d("exec: \"", command, "\": ", spawned == ENOENT ? "file does not exist" : "access is denied");
        conn_fail(conn, 18100, cstr_d("The content mapper command '", command, "' could not be started: ", detail));
        cstr_drop(&detail);
        return;
    }
#else
    int fds[2];
#ifdef SOCK_CLOEXEC
    bool paired = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0;
#else
    // (macOS: no SOCK_CLOEXEC, a process spawned by another thread in between would inherit them)
    bool paired = socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0;
    if (paired) fcntl(fds[0], F_SETFD, FD_CLOEXEC), fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    if (!paired) {
        conn_fail(conn, 18084, cstr_from("The content mapper process could not be started or initialized."));
        return;
    }
    struct stat st;
    bool chdir_to = cstr_size(&mapper->package_dir) && stat(cstr_str(&mapper->package_dir), &st) == 0 && S_ISDIR(st.st_mode);
    bool via_shell = false;
#ifdef __GLIBC__
    via_shell = chdir_to && !posix_spawn_file_actions_addchdir_np;
#endif
    char** argv = calloc((size_t)mapper->exec.count + 6, sizeof(char*));
    isize argc = 0;
    if (via_shell) {
        argv[argc++] = (char*)"/bin/sh";
        argv[argc++] = (char*)"-c";
        argv[argc++] = (char*)"cd -- \"$0\" && exec \"$@\"";
        argv[argc++] = (char*)cstr_str(&mapper->package_dir);
    }
    if (launcher) argv[argc++] = (char*)launcher;
    for (isize i = 0; i < mapper->exec.count; i++) argv[argc++] = (char*)cstr_str(&mapper->exec.items[i]);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    // (tsgo discards a mapper's stderr unless it logs)
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (chdir_to && !via_shell) posix_spawn_file_actions_addchdir_np(&actions, cstr_str(&mapper->package_dir));
    pid_t pid;
    int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    free(argv);
    close(fds[1]);
    if (spawned != 0) {
        close(fds[0]);
        // (Go's exec.Cmd.Start errors, as tsgo's CLI spawner reports them)
        csview command = cstr_sv(&mapper->exec.items[0]);
        cstr detail = cstr_null;
        if (!launcher && !csview_contains(command, "/") && spawned == ENOENT) {
            detail = cstr_d("exec: \"", command, "\": executable file not found in $PATH");
        } else {
            char reason[128];
            snprintf(reason, sizeof(reason), "%s", strerror(spawned));
            if (reason[0] >= 'A' && reason[0] <= 'Z') reason[0] = (char)(reason[0] - 'A' + 'a');
            detail = cstr_d("fork/exec ", command, ": ", reason);
        }
        conn_fail(conn, 18100, cstr_d("The content mapper command '", command, "' could not be started: ", detail));
        cstr_drop(&detail);
        return;
    }
    conn->pid = pid;
    conn->fd = fds[0];
#endif

    JsonValue response, result;
    clock_gettime(CLOCK_MONOTONIC, &conn->deadline);
    conn->deadline.tv_sec += 5;
    CallResult called = call(conn, "initialize", c_sv("{\"positionEncodings\":[\"utf-8\",\"utf-16\"]}"), &result,
        &response);
    conn->deadline = (struct timespec){0};
    if (called != CALL_OK) {
        int status;
        if (conn->timed_out) {
            conn_fail(conn, 18102, cstr_from("The content mapper did not respond to the 'initialize' request within 5 seconds."));
        } else if (called == CALL_ERROR) {
            csview message = json_get_string(json_get(&response, "error"), "message");
            conn_fail(conn, 18104, cstr_d("The content mapper's 'initialize' request failed: ", message));
            json_drop(&response);
        } else if (mapper_exited(conn, &status)) {
            int code = status;
            conn_fail(conn, 18101, cstr_d("The content mapper process exited before responding to the 'initialize' "
                "request (exit code ", code, ")."));
        } else {
            conn_fail(conn, 18084, cstr_from("The content mapper process could not be started or initialized."));
        }
        return;
    }
    csview encoding = json_get_string(&result, "positionEncoding");
    csview source = json_get_string(&result, "diagnosticSource");
    conn->utf16 = csview_equals(encoding, "utf-16");
    if (!csview_equals(encoding, "utf-8") && !conn->utf16) {
        conn_fail(conn, 18087, cstr_d("The content mapper selected unsupported position encoding '", encoding, "'."));
    } else if (!csview_trim(source).size) {
        conn_fail(conn, 18088, cstr_from("The content mapper diagnostic source must not be empty."));
    } else if (reserved_diagnostic_source(source)) {
        conn_fail(conn, 18089, cstr_d("The content mapper diagnostic source '", source, "' is reserved by TypeScript."));
    } else {
        conn->diagnostic_source = cstr_from_sv(source);
    }
    json_drop(&response);
}

static MapperState* state_for(ContentMapperHost* me, ContentMapper const* mapper) {
    for (u32 i = 0; i < my state_count; i++) {
        if (my states[i].mapper == mapper) return &my states[i];
    }
    my states = realloc(my states, sizeof(MapperState) * (my state_count + 1));
    my states[my state_count] = (MapperState){ .mapper = mapper };
    return &my states[my state_count++];
}

// The connection for mapper, started (and its project opened) on first use; null after a failure (*conn has why)
static MapperConn* conn_for(ContentMapperHost* me, ContentMapper const* mapper) {
    cstr identity = content_mapper_identity(mapper);
    cstr key = cstr_clone(identity);
    for (isize i = 0; i < mapper->exec.count; i++) cstr_append_d(&key, DEFAULT_DISPLAY, "\n", mapper->exec.items[i]);
    MapperConn* conn = nullptr;
    for (u32 i = 0; i < my conn_count && !conn; i++) {
        if (cstr_eq(&my conns[i].key, &key)) conn = &my conns[i];
    }
    if (!conn) {
        my conns = realloc(my conns, sizeof(MapperConn) * (my conn_count + 1));
        conn = &my conns[my conn_count++];
        *conn = (MapperConn){ .key = key, .fd = -1 };
#ifdef _WIN32
        conn->write_fd = -1;
#endif
        key = cstr_null;
        conn_start(conn, mapper);
    }
    cstr_drop(&key);
    if (!conn->failed && !cstr_size(&conn->project_handle) && !conn->project_failed) {
        cstr handle = cstr_d(identity, ":", my next_project++);
        cstr params = cstr_null;
        cstr_append_lit(&params, "{\"configFileName\":");
        json_append_string(&params, cstr_sv(&my config_file));
        cstr_append_lit(&params, ",\"projectHandle\":");
        json_append_string(&params, cstr_sv(&handle));
        if (cstr_size(&mapper->options_json)) cstr_append_d(&params, DEFAULT_DISPLAY, ",\"options\":", mapper->options_json);
        cstr_append_lit(&params, ",\"compilerOptions\":");
        append_compiler_options(&params, my options);
        cstr_append_lit(&params, "}");
        JsonValue response, result;
        CallResult called = call(conn, "openProject", cstr_sv(&params), &result, &response);
        cstr_drop(&params);
        // (tsgo's openProjectLocked: what the response may hold depends on the manifest's dynamicConfig)
        JsonValue const* identity_value = called == CALL_OK ? json_get(&result, "configIdentity") : nullptr;
        JsonValue const* watched = called == CALL_OK ? json_get(&result, "watchedFiles") : nullptr;
        bool has_identity = identity_value && identity_value->tag == JSON_STRING && cstr_size(&identity_value->string);
        bool has_watched = watched && watched->tag == JSON_ARRAY && watched->count;
        if (called == CALL_OK && result.tag != JSON_OBJECT) {
            conn->project_failure_code = 18090;
        } else if (called == CALL_OK && mapper->dynamic_config && !has_identity) {
            conn->project_failure_code = 18092;
        } else if (called == CALL_OK && !mapper->dynamic_config && has_identity) {
            conn->project_failure_code = 18094;
        } else if (called == CALL_OK && !mapper->dynamic_config && has_watched) {
            conn->project_failure_code = 18095;
        } else if (called == CALL_OK && has_watched) {
            for (isize i = 0; i < watched->count; i++) {
                JsonValue const* file = &watched->items[i];
                if (file->tag != JSON_STRING || !cstr_size(&file->string) || !path_is_rooted(cstr_sv(&file->string))) {
                    conn->project_failure_code = 18093;
                }
            }
        }
        if (conn->project_failure_code) {
            conn->project_failed = true;
        } else if (called == CALL_OK) {
            JsonValue const* diagnostics = json_get_tagged(&result, "optionDiagnostics", JSON_ARRAY);
            for (isize i = 0; diagnostics && i < diagnostics->count; i++) {
                JsonValue const* d = &diagnostics->items[i];
                i64 code = 0;
                json_int(json_get(d, "code"), &code);
                cstr path = cstr_null;
                JsonValue const* segments = json_get_tagged(d, "path", JSON_ARRAY);
                if (segments) json_write(&path, segments);
                my option_diagnostics = realloc(my option_diagnostics,
                    sizeof(ContentMapperOptionDiagnostic*) * (my option_diagnostic_count + 1));
                my option_diagnostics[my option_diagnostic_count++] = c_new(ContentMapperOptionDiagnostic, {
                    .mapper = mapper, .path = path, .code = (unsigned)code,
                    .message = cstr_from_sv(json_get_string(d, "messageText")), .source = cstr_clone(conn->diagnostic_source) });
            }
            cstr_take(&conn->project_handle, handle);
            handle = cstr_null;
        } else {
            conn->project_failed = true;
        }
        if (called != CALL_FAILED) json_drop(&response);
        cstr_drop(&handle);
    }
    cstr_drop(&identity);
    return conn;
}

void content_mapper_host_drop(ContentMapperHost* me) {
    if (!me) return;
    for (u32 i = 0; i < my conn_count; i++) {
        MapperConn* conn = &my conns[i];
        if (conn->fd >= 0 && cstr_size(&conn->project_handle)) {
            cstr params = cstr_lit("{\"projectHandle\":");
            json_append_string(&params, cstr_sv(&conn->project_handle));
            cstr_append_lit(&params, "}");
            JsonValue response, result;
            if (call(conn, "closeProject", cstr_sv(&params), &result, &response) != CALL_FAILED) json_drop(&response);
            cstr_drop(&params);
        }
        conn_close(conn);
        c_drop(cstr, &conn->key, &conn->read_buffer, &conn->failure, &conn->diagnostic_source, &conn->project_handle);
    }
    free(my conns);
    free(my states);
    for (u32 i = 0; i < my option_diagnostic_count; i++) {
        ContentMapperOptionDiagnostic* d = my option_diagnostics[i];
        c_drop(cstr, &d->path, &d->message, &d->source);
        free(d);
    }
    free(my option_diagnostics);
    cstr_drop(&my config_file);
    pthread_mutex_destroy(&my lock);
    free(me);
}

ContentMapperOptionDiagnostic* const* content_mapper_host_option_diagnostics(ContentMapperHost* me, u32* count) {
    *count = me ? my option_diagnostic_count : 0;
    return me ? my option_diagnostics : nullptr;
}

// ---- Transform

static bool supported_virtual_extension(csview extension) {
    static char const* const supported[] = { ".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts", ".json" };
    for (u32 i = 0; i < c_arraylen(supported); i++) {
        if (csview_equals(extension, supported[i])) return true;
    }
    return false;
}

static int segment_cmp(void const* a, void const* b) {
    u32 x = ((ContentMapSegment const*)a)->virtual_start, y = ((ContentMapSegment const*)b)->virtual_start;
    return x < y ? -1 : x > y;
}

static void fail(ContentMapperOutput* out, unsigned code, cstr message) {
    out->failure_code = code;
    cstr_take(&out->failure, message);
}

static void fail_detail(ContentMapperOutput* out, cstr label, unsigned detail_code, cstr detail) {
    fail(out, 18069, cstr_d("The content mapper '", label, "' failed to transform this file."));
    out->detail_code = detail_code;
    cstr_take(&out->detail, detail);
    cstr_drop(&label);
}


// tsgo's positionNormalizer: a position in the mapper's encoding (utf-16 code units if it chose them) to a byte offset
// of text; false if it is negative, past the end or inside a character
static bool normalize_position(ContentMapperOutput const* out, i64 position, csview text, u32* at) {
    if (position < 0) return false;
    if (!out->utf16) {
        if (position > text.size) return false;
        if (position < text.size && ((unsigned char)text.buf[position] & 0xC0) == 0x80) return false;
        *at = (u32)position;
        return true;
    }
    i64 units = 0;
    isize i = 0;
    while (units < position && i < text.size) {
        unsigned char c = (unsigned char)text.buf[i];
        isize length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        units += length == 4 ? 2 : 1;
        i += length;
    }
    if (units != position || i > text.size) return false;
    *at = (u32)i;
    return true;
}

// tsgo's spanmap Unmarshal and Validate against the texts; false (and out failed) if the map is malformed
static bool read_mappings(ContentMapperOutput* out, ContentMapSegment** segments_out, u32* count_out,
    JsonValue const* mappings, csview virtual_text, csview original, csview label) {
    // (Absent or empty: fully synthesized text, decodeMappedOutput's empty map)
    if (!mappings || mappings->tag == JSON_NULL) return true;
    if (mappings->tag != JSON_ARRAY) {
        fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
        return false;
    }
    (*segments_out) = calloc((size_t)mappings->count + 1, sizeof(ContentMapSegment));
    for (isize i = 0; i < mappings->count; i++) {
        JsonValue const* tuple = &mappings->items[i];
        i64 v[6] = { 0 };
        bool valid = tuple->tag == JSON_ARRAY && (tuple->count == 5 || tuple->count == 6);
        for (isize j = 0; valid && j < tuple->count; j++) valid = json_int(&tuple->items[j], &v[j]);
        if (!valid) {
            fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
            return false;
        }
        // (normalizeMappings: every position in the mapper's encoding, within its text)
        ContentMapSegment segment = { .kind = v[4] >= 0 && v[4] <= 2 ? (uint8_t)v[4] : 3,
            .features = tuple->count == 6 ? v[5] : (1 << 20) - 1 };
        if (!normalize_position(out, v[0], virtual_text, &segment.virtual_start) ||
            !normalize_position(out, v[0] + v[1], virtual_text, &segment.virtual_end) ||
            !normalize_position(out, v[2], original, &segment.original_start) ||
            !normalize_position(out, v[2] + v[3], original, &segment.original_end)) {
            fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
            return false;
        }
        (*segments_out)[(*count_out)++] = segment;
    }
    qsort((*segments_out), (*count_out), sizeof(ContentMapSegment), segment_cmp);
    u32 previous_end = 0;
    for (u32 i = 0; i < (*count_out); i++) {
        ContentMapSegment const* s = &(*segments_out)[i];
        if (s->virtual_start < previous_end || s->virtual_end < s->virtual_start || s->virtual_end > virtual_text.size) {
            fail(out, 18081, cstr_d("The content mapper '", label,
                "' produced overlapping or out-of-order position mappings (near virtual offset ", s->virtual_start, ")."));
            return false;
        }
        previous_end = s->virtual_end;
        if (s->original_end < s->original_start || s->original_end > original.size) {
            fail(out, 18072, cstr_d("The content mapper '", label,
                "' produced a position mapping that points outside the original content (original offset ", s->original_end, ")."));
            return false;
        }
        if (s->kind > 2) {
            fail(out, 18083, cstr_d("The content mapper '", label,
                "' produced a position mapping with an invalid kind (near virtual offset ", s->virtual_start, ")."));
            return false;
        }
        if (s->kind == 0 && (s->virtual_end - s->virtual_start != s->original_end - s->original_start ||
            memcmp(virtual_text.buf + s->virtual_start, original.buf + s->original_start, s->virtual_end - s->virtual_start))) {
            fail(out, 18073, cstr_d("The content mapper '", label,
                "' produced a verbatim mapping that does not match the original content (virtual offset ", s->virtual_start,
                ", original offset ", s->original_start, ")."));
            return false;
        }
        if (s->features & ~(i64)((1 << 20) - 1)) {
            fail(out, 18082, cstr_d("The content mapper '", label, "' produced invalid mapping features near original offset ",
                s->original_start, "."));
            return false;
        }
    }
    return true;
}

static int directive_cmp(void const* a, void const* b) {
    u32 x = ((ContentMapDirective const*)a)->virtual_start, y = ((ContentMapDirective const*)b)->virtual_start;
    return x < y ? -1 : x > y;
}

// tsgo's normalizeDiagnosticDirectives: false (and out failed with the reason) if a directive is malformed
static bool read_directives(ContentMapperOutput* out, ContentMapDirective** directives_out, u32* count_out,
    JsonValue const* value, csview virtual_text, csview original, csview label, isize supplemental) {
    if (!value || value->tag == JSON_NULL) return true;
    JsonValue const* unused = json_get_tagged(value, "unusedExpectDirectiveDiagnostics", JSON_ARRAY);
    JsonValue const* directives = json_get_tagged(value, "directives", JSON_ARRAY);
    isize unused_count = unused ? unused->count : 0;
    if (!directives || !directives->count) return true;
    (*directives_out) = calloc((size_t)directives->count, sizeof(ContentMapDirective));
    bool valid = true;
    for (isize i = 0; i < directives->count && valid; i++) {
        JsonValue const* tuple = &directives->items[i];
        i64 v[6] = { 0 };
        bool decoded = tuple->tag == JSON_ARRAY && (tuple->count == 5 || tuple->count == 6);
        for (isize j = 0; decoded && j < tuple->count; j++) decoded = json_int(&tuple->items[j], &v[j]);
        if (!decoded) {
            fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
            valid = false;
            break;
        }
        ContentMapDirective directive = {0};
        if (v[4] == 1) {
            directive.expect = true;
            i64 index = tuple->count == 6 ? v[5] : 0;
            if (tuple->count != 6 && unused_count != 1) {
                fail_detail(out, cstr_from_sv(label), 18107, cstr_d("Diagnostic directive ", i, " returned by the content "
                    "mapper must specify 'unusedExpectDirectiveIndex' when there is not exactly one "
                    "'unusedExpectDirectiveDiagnostics' entry."));
                valid = false;
                break;
            }
            if (index < 0 || index >= unused_count) {
                fail_detail(out, cstr_from_sv(label), 18110, cstr_d("Diagnostic directive ", i,
                    " returned by the content mapper has an invalid 'unusedExpectDirectiveIndex'."));
                valid = false;
                break;
            }
            i64 code = 0;
            json_int(json_get(&unused->items[index], "code"), &code);
            directive.unused_code = (unsigned)code;
            directive.unused_message = cstr_from_sv(json_get_string(&unused->items[index], "messageText"));
        } else if (v[4] != 0) {
            fail_detail(out, cstr_from_sv(label), 18106, cstr_d("The content mapper returned a diagnostic directive with "
                "invalid policy '", v[4], "'."));
            valid = false;
            break;
        }
        u32 original_start = 0, original_end = 0;
        bool original_valid = v[0] >= 0 && v[1] >= 0 && normalize_position(out, v[0], original, &original_start) &&
            normalize_position(out, v[0] + v[1], original, &original_end);
        if (v[2] < 0 || v[3] < v[2] || !normalize_position(out, v[2], virtual_text, &directive.virtual_start) ||
            !normalize_position(out, v[3], virtual_text, &directive.virtual_end) || (directive.expect && !original_valid)) {
            cstr_drop(&directive.unused_message);
            fail_detail(out, cstr_from_sv(label), 18105, cstr_d("Diagnostic directive ", i,
                " returned by the content mapper has an invalid range."));
            valid = false;
            break;
        }
        if (original_valid) {
            directive.original_start = original_start;
            directive.original_end = original_end;
        }
        (*directives_out)[(*count_out)++] = directive;
    }
    if (!valid && supplemental >= 0 && out->detail_code != 18086) {
        out->detail_sub_code = 18109;
        out->detail_sub = cstr_d("The invalid diagnostic directive is in supplemental output ", supplemental,
            " returned by the content mapper.");
    }
    if (valid) {
        // (Ordered by virtual start, none may begin before the previous one ends)
        ContentMapDirective* sorted = malloc(sizeof(ContentMapDirective) * (*count_out));
        memcpy(sorted, (*directives_out), sizeof(ContentMapDirective) * (*count_out));
        qsort(sorted, (*count_out), sizeof(ContentMapDirective), directive_cmp);
        for (u32 i = 1; i < (*count_out) && valid; i++) {
            if (sorted[i].virtual_start < sorted[i - 1].virtual_end) {
                fail_detail(out, cstr_from_sv(label), 18108,
                    cstr_from("The content mapper returned diagnostic directives with overlapping virtual ranges."));
                valid = false;
                if (supplemental >= 0) {
                    out->detail_sub_code = 18109;
                    out->detail_sub = cstr_d("The invalid diagnostic directive is in supplemental output ", supplemental,
                        " returned by the content mapper.");
                }
            }
        }
        free(sorted);
    }
    return valid;
}

// tsgo's decodeMappedOutput of one output: its extension, then its directives
static bool decode_output(ContentMapperOutput* out, csview extension, JsonValue const* directives,
    ContentMapDirective** directives_out, u32* directive_count, csview text, csview content, csview label,
    isize supplemental) {
    if (!supported_virtual_extension(extension)) {
        fail_detail(out, cstr_from_sv(label), 18098,
            cstr_d("The content mapper returned an output with unsupported virtual extension '", extension, "'."));
        return false;
    }
    return read_directives(out, directives_out, directive_count, directives, text, content, label, supplemental);
}

// tsgo's recordContentMapperFailure: the fifth failed transform of a mapper adds TS18070, and it isn't used any more
void content_mapper_count_failure(ContentMapperHost* me, ContentMapper const* mapper, ContentMapperOutput* out) {
    pthread_mutex_lock(&my lock);
    if (++state_for(me, mapper)->transform_failures == 5) {
        out->global_code = 18070;
        out->global = cstr_d("The content mapper '", cstr_size(&mapper->name) ? cstr_sv(&mapper->name) :
            cstr_sv(&mapper->package), "' failed 5 times and will not be used.");
    }
    pthread_mutex_unlock(&my lock);
}

ContentMapperOutput content_mapper_host_transform(ContentMapperHost* me, ContentMapper const* mapper, csview file,
    csview content) {
    ContentMapperOutput out = {0};
    csview label = cstr_size(&mapper->name) ? cstr_sv(&mapper->name) : cstr_sv(&mapper->package);
    pthread_mutex_lock(&my lock);
    MapperConn* conn = conn_for(me, mapper);
    if (conn->failed) {
        // (tsgo's recordContentMapperInitializationFailure: one program diagnostic, then the mapper isn't used: its
        // files are empty)
        out.failure_code = 18069;
        out.silent = true;
        MapperState* state = state_for(me, mapper);
        if (!state->failure_reported) {
            state->failure_reported = true;
            out.global_code = 18099;
            out.global = cstr_d("The content mapper '", label, "' could not be initialized.");
            out.global_detail_code = conn->failure_code;
            out.global_detail = cstr_clone(conn->failure);
        }
        pthread_mutex_unlock(&my lock);
        return out;
    }
    if (state_for(me, mapper)->transform_failures >= 5) {
        out.failure_code = 18069;
        out.silent = true;
        pthread_mutex_unlock(&my lock);
        return out;
    }
    if (conn->project_failed) {
        // (tsgo's ContentMapperProjectErrorDiagnostic)
        static char const* const messages[] = {
            "The content mapper returned a project response that could not be decoded.",
            "The content mapper process failed while handling the project request.",
            "The content mapper did not return 'configIdentity', which is required when the content mapper has "
                "'\"dynamicConfig\": true' in its package.json.",
            "The content mapper returned a non-absolute path in 'watchedFiles'.",
            "The content mapper returned 'configIdentity', which is only allowed when it declares '\"dynamicConfig\": true' "
                "in its package.json.",
            "The content mapper returned 'watchedFiles', which is only allowed when it declares '\"dynamicConfig\": true' in "
                "its package.json.",
        };
        unsigned code = conn->project_failure_code ? conn->project_failure_code : 18091;
        fail_detail(&out, cstr_from_sv(label), code, cstr_from(messages[code - 18090]));
        pthread_mutex_unlock(&my lock);
        return out;
    }
    cstr params = cstr_lit("{\"fileName\":");
    json_append_string(&params, file);
    cstr_append_lit(&params, ",\"content\":");
    json_append_string(&params, content);
    cstr_append_lit(&params, ",\"projectHandle\":");
    json_append_string(&params, cstr_sv(&conn->project_handle));
    cstr_append_lit(&params, "}");
    JsonValue response, result;
    CallResult called = call(conn, "transform", cstr_sv(&params), &result, &response);
    cstr_drop(&params);
    out.diagnostic_source = cstr_clone(conn->diagnostic_source);
    out.utf16 = conn->utf16;
    pthread_mutex_unlock(&my lock);
    if (called != CALL_OK) {
        if (called == CALL_ERROR) json_drop(&response);
        fail_detail(&out, cstr_from_sv(label), 18085,
            cstr_from("The content mapper process failed while handling the transform request."));
        content_mapper_count_failure(me, mapper, &out);
        return out;
    }
    JsonValue const* text = json_get_tagged(&result, "text", JSON_STRING);
    JsonValue const* extension = json_get_tagged(&result, "extension", JSON_STRING);
    if (result.tag != JSON_OBJECT || !text || !extension) {
        fail_detail(&out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
        json_drop(&response);
        content_mapper_count_failure(me, mapper, &out);
        return out;
    }
    out.text = cstr_clone(text->string);
    out.extension = cstr_clone(extension->string);
    // tsgo's decodeTransformResult: each output's extension and directives (the canonical one's, then the
    // supplementals'), the diagnostics; then ParseResult validates each output's mappings against the texts
    JsonValue const* supplementals = json_get_tagged(&result, "supplemental", JSON_ARRAY);
    bool valid = decode_output(&out, cstr_sv(&out.extension), json_get(&result, "diagnosticDirectives"),
        &out.directives, &out.directive_count, cstr_sv(&out.text), content, label, -1);
    if (valid && supplementals && supplementals->count) {
        out.supplemental = calloc((size_t)supplementals->count, sizeof(ContentMapperSupplemental));
        for (isize i = 0; i < supplementals->count && valid; i++) {
            JsonValue const* item = &supplementals->items[i];
            ContentMapperSupplemental* supplemental = &out.supplemental[out.supplemental_count++];
            supplemental->text = cstr_from_sv(json_get_string(item, "text"));
            supplemental->extension = cstr_from_sv(json_get_string(item, "extension"));
            valid = decode_output(&out, cstr_sv(&supplemental->extension), json_get(item, "diagnosticDirectives"),
                &supplemental->directives, &supplemental->directive_count, cstr_sv(&supplemental->text), content, label, i);
        }
    }
    JsonValue const* diagnostics = json_get_tagged(&result, "diagnostics", JSON_ARRAY);
    if (valid && diagnostics && diagnostics->count) {
        out.diagnostics = calloc((size_t)diagnostics->count, sizeof(ContentMapperDiagnostic));
        for (isize i = 0; i < diagnostics->count && valid; i++) {
            JsonValue const* d = &diagnostics->items[i];
            i64 start = 0, length = 0, code = 0;
            json_int(json_get(d, "start"), &start);
            json_int(json_get(d, "length"), &length);
            json_int(json_get(d, "code"), &code);
            u32 from = 0, to = 0;
            if (!normalize_position(&out, start, content, &from) || length < 0 ||
                !normalize_position(&out, start + length, content, &to)) {
                fail_detail(&out, cstr_from_sv(label), 18086,
                    cstr_from("The content mapper returned an invalid transform response."));
                valid = false;
                break;
            }
            out.diagnostics[out.diagnostic_count++] = (ContentMapperDiagnostic){ .start = from,
                .length = to - from, .code = (unsigned)code, .message = cstr_from_sv(json_get_string(d, "messageText")) };
        }
    }
    valid = valid && read_mappings(&out, &out.segments, &out.segment_count, json_get(&result, "mappings"),
        cstr_sv(&out.text), content, label);
    for (isize i = 0; valid && supplementals && i < supplementals->count; i++) {
        ContentMapperSupplemental* supplemental = &out.supplemental[i];
        valid = read_mappings(&out, &supplemental->segments, &supplemental->segment_count,
            json_get(&supplementals->items[i], "mappings"), cstr_sv(&supplemental->text), content, label);
    }
    json_drop(&response);
    if (out.failure_code) content_mapper_count_failure(me, mapper, &out);
    return out;
}

// ---- Span maps

// tsgo's segmentIndexAt: the segment containing pos (true), else the one before it (-1 for none)
static i64 segment_index_at(ContentMapSegment const* segments, u32 count, u32 pos, bool* inside) {
    u32 lo = 0, hi = count;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (segments[mid].virtual_start < pos) lo = mid + 1;
        else hi = mid;
    }
    if (lo < count && segments[lo].virtual_start == pos) {
        *inside = true;
        return lo;
    }
    i64 prev = (i64)lo - 1;
    *inside = prev >= 0 && (pos < segments[prev].virtual_end || (prev == (i64)count - 1 && pos == segments[prev].virtual_end));
    return prev;
}

static u32 clamp_u32(i64 v, u32 lo, u32 hi) {
    return v < lo ? lo : v > hi ? hi : (u32)v;
}

static u32 map_boundary(ContentMapSegment const* segments, u32 pos, i64 index, bool inside, bool high) {
    if (!inside) return index < 0 ? 0 : segments[index].original_end;
    ContentMapSegment const* s = &segments[index];
    if (s->kind == 0) return clamp_u32((i64)s->original_start + pos - s->virtual_start, s->original_start, s->original_end);
    return high ? s->original_end : s->original_start;
}

bool content_map_virtual_to_original(ContentMapSegment const* segments, u32 count, u32 start, u32 end, u32* out_start,
    u32* out_end) {
    if (end < start) end = start;
    bool start_in, end_in;
    i64 start_index = segment_index_at(segments, count, start, &start_in);
    if (start == end) {
        *out_start = *out_end = map_boundary(segments, start, start_index, start_in, false);
        return start_in;
    }
    i64 end_index = segment_index_at(segments, count, end - 1, &end_in);
    if (start_index == end_index && start_in == end_in) {
        if (!start_in) {
            *out_start = *out_end = start_index < 0 ? 0 : segments[start_index].original_end;
            return false;
        }
        ContentMapSegment const* s = &segments[start_index];
        if (s->kind == 0) {
            *out_start = clamp_u32((i64)s->original_start + start - s->virtual_start, s->original_start, s->original_end);
            *out_end = clamp_u32((i64)s->original_start + end - s->virtual_start, *out_start, s->original_end);
        } else {
            *out_start = s->original_start;
            *out_end = s->original_end;
        }
        return true;
    }
    *out_start = map_boundary(segments, start, start_index, start_in, false);
    u32 high = map_boundary(segments, end, end_index, end_in, true);
    *out_end = high > *out_start ? high : *out_start;
    return true;
}

csview content_mapper_extension_of(CompilerOptions const* options, csview name) {
    csview ret = { name.buf + name.size, 0 };
    for (u32 i = 0; i < options->content_mapper_count; i++) {
        StrList const* extensions = &options->content_mappers[i].extensions;
        for (isize j = 0; j < extensions->count; j++) {
            csview extension = cstr_sv(&extensions->items[j]);
            if (extension.size > ret.size && path_has_extension(name, extension)) {
                ret = csview_slice(name, name.size - extension.size, name.size);
            }
        }
    }
    return ret;
}

// ---- Mapped files

void content_mapped_free(ContentMapped* me) {
    free(my supplementals);
    if (my original && !my canonical) {
        source_file_drop(my original);
        free(my original);
    }
    c_drop(cstr, &my identity, &my diagnostic_source, &my virtual_extension);
    free(my segments);
    for (u32 i = 0; i < my directive_count; i++) cstr_drop(&my directives[i].unused_message);
    free(my directives);
    free(me);
}

// tsgo's displayMessageArgs: a diagnostic exactly at an alias segment names the original text where its arguments are
// the virtual one. tttt's messages are formatted already: the quoted virtual name is replaced in the text
static void alias_original_names(ContentMapped const* mapped, Error* error, u32 start) {
    bool inside;
    i64 index = segment_index_at(mapped->segments, mapped->segment_count, start, &inside);
    if (!inside) return;
    ContentMapSegment const* s = &mapped->segments[index];
    if (s->kind != 2 || s->virtual_start != start || s->virtual_end != start + (u32)error->span.size) return;
    cstr from = cstr_d("'", span_sv(error->span), "'");
    csview original = csview_slice(cstr_sv(&mapped->original->contents), s->original_start, s->original_end);
    cstr to = cstr_d("'", original, "'");
    cstr_replace(&error->message, cstr_str(&from), cstr_str(&to));
    c_drop(cstr, &from, &to);
}

// tsgo's reportsUnnecessary diagnostics (unused declarations, unreachable code): filterAndSortDiagnostics drops them in
// synthesized text
static bool reports_unnecessary(unsigned code) {
    static unsigned const codes[] = { 2695, 6133, 6138, 6192, 6196, 6198, 6199, 7027, 7028 };
    for (u32 i = 0; i < c_arraylen(codes); i++) {
        if (codes[i] == code) return true;
    }
    return false;
}

bool content_mapped_map_error(Error* error) {
    for_each (sub, vec_error, &error->sub_errors) ignore content_mapped_map_error(sub);
    if (!span_valid(error->span)) return true;
    SourceFile const* file = span_source(error->span);
    ContentMapped const* mapped = file ? file->content_mapped : nullptr;
    if (!mapped || !mapped->has_map || error->source) return true;
    char const* text = cstr_str(&file->contents);
    u32 start = (u32)(error->span.buf - text);
    alias_original_names(mapped, error, start);
    u32 original_start, original_end;
    if (content_map_virtual_to_original(mapped->segments, mapped->segment_count, start, start + (u32)error->span.size,
        &original_start, &original_end)) {
        char const* original = cstr_str(&mapped->original->contents);
        error->span = span_make(mapped->original, original + original_start, original + original_end);
    } else {
        if (reports_unnecessary(error->ts_code)) return false;
        error_sub_error_d(error, span_null, 18074, "This location is in virtual code produced by the content mapper '",
            mapped->identity, "' and has no corresponding location in the original file.");
    }
    return true;
}
