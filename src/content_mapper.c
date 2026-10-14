#define _GNU_SOURCE // posix_spawn_file_actions_addchdir_np
#include "content_mapper.h"
#include "json.h"
#include "error.h"
#include "source_file.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

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
    c_drop(cstr, &my text, &my extension, &my diagnostic_source, &my failure, &my detail);
    free(my segments);
    for (u32 i = 0; i < my diagnostic_count; i++) cstr_drop(&my diagnostics[i].message);
    free(my diagnostics);
    *me = (ContentMapperOutput){0};
}

static char const* launcher;

void content_mapper_set_launcher(char const* path) {
    launcher = path;
}

// ---- JSON text

// tsgo's MarshalDeclaredOptions: the options the mapper declared, in its order, the ones that are set, as Go's JSON
// encoding of tsgo's CompilerOptions has them (enums as their numbers)
static void append_declared_options(cstr* out, ContentMapper const* mapper, CompilerOptions const* options) {
    static char const* const bool_names[] = {
#define X(field, name, strict_family) name,
        COMPILER_BOOL_OPTIONS(X)
#undef X
    };
    cstr_append_lit(out, "{");
    bool first = true;
    for (isize i = 0; i < mapper->compiler_options.count; i++) {
        cstr const* name = &mapper->compiler_options.items[i];
        char const* n = cstr_str(name);
        cstr value = cstr_null;
        if (!strcasecmp(n, "target") && options->target_set) {
            // (tsgo's ScriptTarget: ES5 1 ... ES2026 13, ESNext 99)
            int target = options->target == SCRIPT_TARGET_ESNEXT ? 99 : (int)options->target + 1;
            cstr_take(&value, cstr_d(target));
        } else if (!strcasecmp(n, "module") && options->module_set) {
            static int const kinds[] = { 0, 1, 2, 3, 4, 5, 6, 7, 99, 100, 101, 102, 199, 200 };
            cstr_take(&value, cstr_d(kinds[options->module]));
        } else if (!strcasecmp(n, "moduleResolution") && options->module_resolution_set) {
            static int const kinds[] = { 0, 1, 2, 3, 99, 100 };
            cstr_take(&value, cstr_d(kinds[options->module_resolution]));
        } else if (!strcasecmp(n, "jsx") && options->jsx != JSX_NONE) {
            cstr_take(&value, cstr_d((int)options->jsx));
        } else if (!strcasecmp(n, "jsxFactory") && cstr_size(&options->jsx_factory)) {
            json_append_string(&value, cstr_sv(&options->jsx_factory));
        } else if (!strcasecmp(n, "jsxFragmentFactory") && cstr_size(&options->jsx_fragment_factory)) {
            json_append_string(&value, cstr_sv(&options->jsx_fragment_factory));
        } else if (!strcasecmp(n, "jsxImportSource") && cstr_size(&options->jsx_import_source)) {
            json_append_string(&value, cstr_sv(&options->jsx_import_source));
        } else {
            for (u32 j = 0; j < c_arraylen(bool_names); j++) {
                if (!strcasecmp(n, bool_names[j]) && options->is_set[j]) {
                    bool on = ((bool const*)options)[j];
                    cstr_take(&value, cstr_from(on ? "true" : "false"));
                }
            }
        }
        if (!cstr_size(&value)) continue;
        if (!first) cstr_append_lit(out, ",");
        first = false;
        json_append_string(out, cstr_sv(name));
        cstr_append_lit(out, ":");
        cstr_append_s(out, value);
    }
    cstr_append_lit(out, "}");
}

// ---- A mapper process

typedef struct MapperConn {
    cstr key; // Identity and exec
    pid_t pid;
    int fd; // A socket that is the process's stdin and stdout
    cstr read_buffer;
    isize read_at;
    bool failed; // Couldn't start or initialize: not tried again
    unsigned failure_code; // Its TS18069 detail (TS18084 and friends)
    cstr failure;
    cstr diagnostic_source;
    cstr project_handle; // Opened for the program's project, empty until then
    bool project_failed;
    unsigned next_id;
} MapperConn;

struct ContentMapperHost {
    pthread_mutex_t lock;
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
        ssize_t n = send(fd, buf, (size_t)size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buf += n;
        size -= n;
    }
    return true;
}

static bool send_message(MapperConn* conn, csview body) {
    cstr header = cstr_d("Content-Length: ", body.size, "\r\n\r\n");
    bool sent = write_all(conn->fd, cstr_str(&header), cstr_size(&header)) && write_all(conn->fd, body.buf, body.size);
    cstr_drop(&header);
    return sent;
}

// More bytes into the read buffer, false at the end of the stream
static bool fill(MapperConn* conn) {
    char buf[65536];
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
    if (conn->pid > 0) {
        int status;
        if (waitpid(conn->pid, &status, WNOHANG) == 0) {
            kill(conn->pid, SIGTERM);
            waitpid(conn->pid, &status, 0);
        }
        conn->pid = 0;
    }
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

// Spawns the mapper (exec in its package directory, the launcher in front of it) and initializes it
static void conn_start(MapperConn* conn, ContentMapper const* mapper) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
        conn_fail(conn, 18084, cstr_from("The content mapper process could not be started or initialized."));
        return;
    }
    char** argv = calloc((size_t)mapper->exec.count + 2, sizeof(char*));
    isize argc = 0;
    if (launcher) argv[argc++] = (char*)launcher;
    for (isize i = 0; i < mapper->exec.count; i++) argv[argc++] = (char*)cstr_str(&mapper->exec.items[i]);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    struct stat st;
    if (cstr_size(&mapper->package_dir) && stat(cstr_str(&mapper->package_dir), &st) == 0 && S_ISDIR(st.st_mode)) {
        posix_spawn_file_actions_addchdir_np(&actions, cstr_str(&mapper->package_dir));
    }
    pid_t pid;
    int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    free(argv);
    close(fds[1]);
    if (spawned != 0) {
        close(fds[0]);
        conn_fail(conn, 18084, cstr_from("The content mapper process could not be started or initialized."));
        return;
    }
    conn->pid = pid;
    conn->fd = fds[0];

    JsonValue response, result;
    CallResult called = call(conn, "initialize", c_sv("{\"positionEncodings\":[\"utf-8\",\"utf-16\"]}"), &result,
        &response);
    if (called != CALL_OK) {
        if (called == CALL_ERROR) json_drop(&response);
        conn_fail(conn, 18084, cstr_from("The content mapper process could not be started or initialized."));
        return;
    }
    csview encoding = json_get_string(&result, "positionEncoding");
    csview source = json_get_string(&result, "diagnosticSource");
    if (!csview_equals(encoding, "utf-8")) {
        // TODO(plan 56 part 2): utf-16 positions
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
        append_declared_options(&params, mapper, my options);
        cstr_append_lit(&params, "}");
        JsonValue response, result;
        CallResult called = call(conn, "openProject", cstr_sv(&params), &result, &response);
        cstr_drop(&params);
        if (called == CALL_OK) {
            // TODO(plan 56 part 2): optionDiagnostics, configIdentity/watchedFiles against dynamicConfig
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
    cstr_drop(&my config_file);
    pthread_mutex_destroy(&my lock);
    free(me);
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

static bool json_int(JsonValue const* value, i64* out) {
    if (!value || value->tag != JSON_NUMBER || value->number != (double)(i64)value->number) return false;
    *out = (i64)value->number;
    return true;
}

// tsgo's spanmap Unmarshal and Validate against the texts; false (and out failed) if the map is malformed
static bool read_mappings(ContentMapperOutput* out, JsonValue const* mappings, csview virtual_text, csview original,
    csview label) {
    // (Absent or empty: fully synthesized text, decodeMappedOutput's empty map)
    if (!mappings || mappings->tag == JSON_NULL) return true;
    if (mappings->tag != JSON_ARRAY) {
        fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
        return false;
    }
    out->segments = calloc((size_t)mappings->count + 1, sizeof(ContentMapSegment));
    for (isize i = 0; i < mappings->count; i++) {
        JsonValue const* tuple = &mappings->items[i];
        i64 v[6] = { 0 };
        bool valid = tuple->tag == JSON_ARRAY && (tuple->count == 5 || tuple->count == 6);
        for (isize j = 0; valid && j < tuple->count; j++) valid = json_int(&tuple->items[j], &v[j]);
        if (!valid) {
            fail_detail(out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
            return false;
        }
        out->segments[out->segment_count++] = (ContentMapSegment){ .virtual_start = (u32)v[0],
            .virtual_end = (u32)(v[0] + v[1]), .original_start = (u32)v[2], .original_end = (u32)(v[2] + v[3]),
            .kind = (uint8_t)v[4] };
        // TODO(plan 56 part 2): features (the 6th element), checked against the mask
    }
    qsort(out->segments, out->segment_count, sizeof(ContentMapSegment), segment_cmp);
    u32 previous_end = 0;
    for (u32 i = 0; i < out->segment_count; i++) {
        ContentMapSegment const* s = &out->segments[i];
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
    }
    return true;
}

ContentMapperOutput content_mapper_host_transform(ContentMapperHost* me, ContentMapper const* mapper, csview file,
    csview content) {
    ContentMapperOutput out = {0};
    csview label = cstr_size(&mapper->name) ? cstr_sv(&mapper->name) : cstr_sv(&mapper->package);
    pthread_mutex_lock(&my lock);
    MapperConn* conn = conn_for(me, mapper);
    if (conn->failed) {
        fail_detail(&out, cstr_from_sv(label), conn->failure_code, cstr_clone(conn->failure));
        pthread_mutex_unlock(&my lock);
        return out;
    }
    if (conn->project_failed) {
        fail_detail(&out, cstr_from_sv(label), 18091,
            cstr_from("The content mapper process failed while handling the project request."));
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
    pthread_mutex_unlock(&my lock);
    if (called != CALL_OK) {
        if (called == CALL_ERROR) json_drop(&response);
        fail_detail(&out, cstr_from_sv(label), 18085,
            cstr_from("The content mapper process failed while handling the transform request."));
        return out;
    }
    JsonValue const* text = json_get_tagged(&result, "text", JSON_STRING);
    JsonValue const* extension = json_get_tagged(&result, "extension", JSON_STRING);
    if (result.tag != JSON_OBJECT || !text || !extension) {
        fail_detail(&out, cstr_from_sv(label), 18086, cstr_from("The content mapper returned an invalid transform response."));
        json_drop(&response);
        return out;
    }
    out.text = cstr_clone(text->string);
    out.extension = cstr_clone(extension->string);
    // (decodeMappedOutput: the extension, then the mappings; ParseResult validates them against the texts)
    if (!supported_virtual_extension(cstr_sv(&out.extension))) {
        fail_detail(&out, cstr_from_sv(label), 18098,
            cstr_d("The content mapper returned an output with unsupported virtual extension '", out.extension, "'."));
        json_drop(&response);
        return out;
    }
    if (!read_mappings(&out, json_get(&result, "mappings"), cstr_sv(&out.text), content, label)) {
        json_drop(&response);
        return out;
    }
    JsonValue const* diagnostics = json_get_tagged(&result, "diagnostics", JSON_ARRAY);
    if (diagnostics && diagnostics->count) {
        out.diagnostics = calloc((size_t)diagnostics->count, sizeof(ContentMapperDiagnostic));
        for (isize i = 0; i < diagnostics->count; i++) {
            JsonValue const* d = &diagnostics->items[i];
            i64 start = 0, length = 0, code = 0;
            json_int(json_get(d, "start"), &start);
            json_int(json_get(d, "length"), &length);
            json_int(json_get(d, "code"), &code);
            if (start < 0 || length < 0 || start + length > content.size) {
                fail_detail(&out, cstr_from_sv(label), 18086,
                    cstr_from("The content mapper returned an invalid transform response."));
                json_drop(&response);
                return out;
            }
            out.diagnostics[out.diagnostic_count++] = (ContentMapperDiagnostic){ .start = (u32)start,
                .length = (u32)length, .code = (unsigned)code, .message = cstr_from_sv(json_get_string(d, "messageText")) };
        }
    }
    // TODO(plan 56 part 2): diagnosticDirectives, supplemental outputs
    json_drop(&response);
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
    if (my original) {
        source_file_drop(my original);
        free(my original);
    }
    c_drop(cstr, &my identity, &my diagnostic_source, &my virtual_extension);
    free(my segments);
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
