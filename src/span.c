#include "span.h"
#include "math_helpers.h"
#include "source_file.h"
#include <pthread.h>
#include <sys/mman.h>

SourceFile const** span_files[1 << SPAN_FILE_DIR_BITS];
static u32 span_file_next = 1;
static pthread_mutex_t span_files_lock = PTHREAD_MUTEX_INITIALIZER;

void source_file_register(SourceFile* file) {
    pthread_mutex_lock(&span_files_lock);
    u32 id = span_file_next++;
    panic_if(id >> SPAN_FILE_CHUNK_BITS >= 1u << SPAN_FILE_DIR_BITS, "too many source files");
    SourceFile const*** chunk = &span_files[id >> SPAN_FILE_CHUNK_BITS];
    // Never freed (indexes outlive programs): mmapped, so leak checks don't count it
    if (!*chunk) {
        void* mem = mmap(nullptr, sizeof(SourceFile const*) << SPAN_FILE_CHUNK_BITS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        panic_if(mem == MAP_FAILED, "mmap failed");
        *chunk = mem;
    }
    (*chunk)[id & ((1u << SPAN_FILE_CHUNK_BITS) - 1)] = file;
    file->id = id;
    pthread_mutex_unlock(&span_files_lock);
}

void source_file_unregister(SourceFile const* file) {
    if (!file->id) return;
    span_files[file->id >> SPAN_FILE_CHUNK_BITS][file->id & ((1u << SPAN_FILE_CHUNK_BITS) - 1)] = nullptr;
}

unsigned span_line_index(Span me) {
    if (!span_valid(me) || !span_source(me)) {
        return 1;
    }
    return source_file_line_index(span_source(me), span_start_ptr(me));
}

// In UTF-16 code units, like tsc's positions (utf8_byte_utf16_units)
unsigned span_col_index(Span me) {
    unsigned col = 0;
    for (const char* c = span_line_start(me); c < span_start_ptr(me); c++) col += utf8_byte_utf16_units(c);
    return col;
}

const char* span_line_start(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!span_source(me)) {
        return me.buf;
    } else {
        // A span that starts on a line break belongs to the line that line break ends
        return source_file_start(span_source(me)) + span_source(me)->line_starts[span_line_index(me)];
    }
}

// Where the span's first line's text ends (before its line break)
const char* span_line_end(Span me) {
    if (!span_valid(me)) {
        return nullptr;
    } else if (!span_source(me)) {
        return me.buf + me.size;
    } else {
        SourceFile const* source = span_source(me);
        unsigned line = span_line_index(me);
        const char* text = source_file_start(source);
        if (line + 1 >= source->line_count) return text + cstr_size(&source->contents);
        const char* end = text + source->line_starts[line + 1];
        if (end[-1] == '\n') end -= end - 1 > text && end[-2] == '\r' ? 2 : 1;
        else if (end[-1] == '\r') end--;
        else end -= 3; // U+2028/9
        return end;
    }
}

Span span_with_semicolon(Span statement) {
    if (!span_valid(statement) || !span_source(statement)) return statement;
    char const* end = span_end_ptr(statement);
    char const* file_end = cstr_str(&span_source(statement)->contents) + cstr_size(&span_source(statement)->contents);
    while (end < file_end && (*end == ' ' || *end == '\t')) end++;
    if (end < file_end && *end == ';') statement.size = end + 1 - statement.buf;
    return statement;
}

bool span_only_trivia_between(char const* from, char const* to, bool allow_semicolon) {
    while (from < to) {
        if (*from == ' ' || *from == '\t' || *from == '\n' || *from == '\r' || *from == '\f' || *from == '\v') {
            from++;
        } else if (from + 1 < to && from[0] == '/' && from[1] == '/') {
            while (from < to && *from != '\n') from++;
        } else if (from + 1 < to && from[0] == '/' && from[1] == '*') {
            from += 2;
            while (from + 1 < to && !(from[0] == '*' && from[1] == '/')) from++;
            from += 2;
        } else if (*from == ';' && allow_semicolon) {
            allow_semicolon = false;
            from++;
        } else {
            return false;
        }
    }
    return true;
}

Span span_with_member_separator(Span member) {
    if (!span_valid(member) || !span_source(member)) return member;
    char const* end = span_end_ptr(member);
    char const* file_end = cstr_str(&span_source(member)->contents) + cstr_size(&span_source(member)->contents);
    while (end < file_end && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end++;
    if (end < file_end && (*end == ';' || *end == ',')) member.size = end + 1 - member.buf;
    return member;
}

Span span_between(Span const a, Span const b) {
    if (!span_valid(a)) {
        return b;
    } else if (!span_valid(b)) {
        return a;
    }
    if (span_source(a) != span_source(b)) {
        panic("Tried to make a span between two different files");
    }
    return span_make(
        span_source(a),
        ccpmin(span_start_ptr(a), span_start_ptr(b)),
        ccpmax(  span_end_ptr(a),   span_end_ptr(b))
    );
}

int span_cmp(Span a, Span b) {
    switch (span_valid(a) | (span_valid(b) << 1)) {
        case 0: return 0; // Neither are valid
        case 1: return 1; // Only A is valid
        case 2: return -1; // Only b is valid
        default: // Both are valid
            break;
    }
    switch ((span_source(a) != nullptr) | ((span_source(b) != nullptr) << 1)) {
        case 1: return 1; // Only A is valid
        case 2: return -1; // Only b is valid
        default: // Both or neither are valid
            break;
    }
    if (span_source(a) != span_source(b)) {
        SourceFile const* fa = span_source(a);
        SourceFile const* fb = span_source(b);
        int cmp = cstr_cmp(&fa->canonical_path, &fb->canonical_path);
        if (cmp) return cmp;
        // A content-mapped file and its original text (ContentMapped.original): one file to tsgo, whose diagnostics
        // sort by their offsets, whichever text they are in
        panic_if(!fa->content_mapped && !fb->content_mapped, "Two different source files both have path ", fa->canonical_path);
        isize pa = a.buf - cstr_str(&fa->contents), pb = b.buf - cstr_str(&fb->contents);
        if (pa != pb) return pa > pb ? 1 : -1;
        if (a.size != b.size) return a.size > b.size ? 1 : -1;
        return fa->content_mapped ? 1 : -1;
    } else if (a.buf != b.buf) {
        return a.buf > b.buf ? 1 : -1;
    } else if (a.size != b.size) {
        return a.size > b.size ? 1 : -1;
    } else {
        return 0;
    }
}

void cstr_append_span(cstr* target, Span me) {
    cstr_append_sv(target, span_sv(me));
}

bool span_is_jsdoc_tag(Span me) {
    if (!span_is_written(me) || !span_source(me)->is_js || *me.buf != '@') return false;
    const char* file_start = source_file_start(span_source(me));
    const char* c = me.buf;
    while (c > file_start && (c[-1] == ' ' || c[-1] == '\t')) c--;
    return c > file_start && c[-1] == '*';
}

bool span_is_written(Span me) {
    SourceFile const* file = span_source(me);
    char const* start = file ? source_file_start(file) : nullptr;
    return start && me.buf >= start && me.buf < start + cstr_size(&file->contents);
}

// Escaped names. The files whose text has a `\u` (few), each with the names cooked so far. A cooked name is malloc'd
// once and kept until the file is dropped, so views of it are as stable as views of the file's text
typedef struct CookedName {
    const char* raw;
    isize raw_size;
    char* cooked;
    isize cooked_size;
} CookedName;

typedef struct EscapedSource {
    SourceFile const* file;
    const char* start;
    const char* end;
    CookedName* names;
    u32 count;
    u32 cap;
} EscapedSource;

_Atomic u32 names_escaped_files;
static pthread_mutex_t escaped_lock = PTHREAD_MUTEX_INITIALIZER;
static EscapedSource* escaped_sources;
static u32 escaped_source_count;
static u32 escaped_source_cap;

void names_register_source(SourceFile* file) {
    if (file->escaped_names) return;
    const char* start = source_file_start(file);
    const char* end = start + cstr_size(&file->contents);
    file->escaped_names = true;
    pthread_mutex_lock(&escaped_lock);
    atomic_fetch_add_explicit(&names_escaped_files, 1, memory_order_relaxed);
    if (escaped_source_count == escaped_source_cap) {
        escaped_source_cap = escaped_source_cap ? escaped_source_cap * 2 : 8;
        escaped_sources = realloc(escaped_sources, sizeof(EscapedSource) * escaped_source_cap);
    }
    escaped_sources[escaped_source_count++] = (EscapedSource){ .file = file, .start = start, .end = end };
    pthread_mutex_unlock(&escaped_lock);
}

void names_unregister_source(SourceFile* file) {
    if (!file->escaped_names) return;
    pthread_mutex_lock(&escaped_lock);
    for (u32 i = 0; i < escaped_source_count; i++) {
        EscapedSource* source = &escaped_sources[i];
        if (source->file != file) continue;
        for (u32 j = 0; j < source->count; j++) free(source->names[j].cooked);
        free(source->names);
        escaped_sources[i] = escaped_sources[--escaped_source_count];
        atomic_fetch_sub_explicit(&names_escaped_files, 1, memory_order_relaxed);
        if (!escaped_source_count) {
            free(escaped_sources);
            escaped_sources = nullptr;
            escaped_source_cap = 0;
        }
        break;
    }
    pthread_mutex_unlock(&escaped_lock);
    file->escaped_names = false;
}

static int name_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// `\uXXXX` or `\u{X...}` at c (before end): its length and value, 0 if there is none
static int name_escape(const char* c, const char* end, u32* code) {
    if (end - c < 3 || c[0] != '\\' || c[1] != 'u') return 0;
    *code = 0;
    if (c[2] == '{') {
        const char* d = c + 3;
        for (; d < end && name_hex_value(*d) >= 0; d++) {
            *code = *code * 16 + (u32)name_hex_value(*d);
            if (*code > 0x10FFFF) return 0;
        }
        return d > c + 3 && d < end && *d == '}' ? (int)(d + 1 - c) : 0;
    }
    if (end - c < 6) return 0;
    for (int i = 2; i < 6; i++) {
        if (name_hex_value(c[i]) < 0) return 0;
        *code = *code * 16 + (u32)name_hex_value(c[i]);
    }
    return 6;
}

csview name_cooked_slow(csview sv) {
    csview ret = sv;
    pthread_mutex_lock(&escaped_lock);
    for (u32 i = 0; i < escaped_source_count; i++) {
        EscapedSource* source = &escaped_sources[i];
        if (sv.buf < source->start || sv.buf + sv.size > source->end) continue;
        for (u32 j = 0; j < source->count; j++) {
            CookedName const* name = &source->names[j];
            if (name->raw == sv.buf && name->raw_size == sv.size) {
                ret = (csview){ name->cooked, name->cooked_size };
                goto done;
            }
        }
        // A quoted name (a string literal's text) isn't an identifier's
        if (*sv.buf == '"' || *sv.buf == '\'' || *sv.buf == '`') goto done;
        char* cooked = malloc((size_t)sv.size + 1);
        isize size = 0;
        const char* end = sv.buf + sv.size;
        for (const char* c = sv.buf; c < end;) {
            u32 code;
            int escape = name_escape(c, end, &code);
            if (!escape) {
                // (`\\u0061` is no escape, if a string's raw text ever gets here)
                if (*c == '\\' && c + 1 < end) cooked[size++] = *c++;
                cooked[size++] = *c++;
                continue;
            }
            // (A surrogate pair of escapes is one character, a lone surrogate has no UTF-8: U+FFFD)
            u32 low;
            if (code >= 0xD800 && code <= 0xDBFF && name_escape(c + escape, end, &low) == 6 && low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                escape += 6;
            } else if (code >= 0xD800 && code <= 0xDFFF) {
                code = 0xFFFD;
            }
            size += cutf8_encode(cooked + size, code);
            c += escape;
        }
        cooked[size] = '\0';
        if (source->count == source->cap) {
            source->cap = source->cap ? source->cap * 2 : 16;
            source->names = realloc(source->names, sizeof(CookedName) * source->cap);
        }
        source->names[source->count++] = (CookedName){ sv.buf, sv.size, cooked, size };
        ret = (csview){ cooked, size };
        break;
    }
done:
    pthread_mutex_unlock(&escaped_lock);
    return ret;
}

Span span_comma_after(Span after) {
    return span_token_after(after, ",");
}

Span span_token_after(Span after, char const* token) {
    if (!span_valid(after) || !span_source(after)) return span_null;
    char const* at = span_end_ptr(after);
    char const* file_end = cstr_str(&span_source(after)->contents) + cstr_size(&span_source(after)->contents);
    while (at < file_end) {
        if (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r' || *at == '\f' || *at == '\v') {
            at++;
        } else if (at + 1 < file_end && at[0] == '/' && at[1] == '/') {
            while (at < file_end && *at != '\n') at++;
        } else if (at + 1 < file_end && at[0] == '/' && at[1] == '*') {
            at += 2;
            while (at + 1 < file_end && !(at[0] == '*' && at[1] == '/')) at++;
            at += 2;
        } else {
            break;
        }
    }
    isize len = (isize)strlen(token);
    return at + len <= file_end && memcmp(at, token, len) == 0 ? span_make_in(after, at, at + len) : span_null;
}
