#include "decl_map.h"
#include "../source_file.h"
#include "../span.h"
#include "../type.h"
#include "../several_types.h"
#include "../interface.h"
#include "../callable_type.h"
#include "../function_impl.h"
#include "../expr.h"


void decl_map_mark(cstr* out, u32 source_offset) {
    char mark[DECL_MAP_MARK_SIZE];
    mark[0] = DECL_MAP_MARK;
    for (int i = 0; i < 8; i++) mark[1 + i] = (char)(0x10 + ((source_offset >> (28 - 4 * i)) & 0xF));
    cstr_append_n(out, mark, DECL_MAP_MARK_SIZE);
}

isize_t decl_map_text_end(csview text) {
    isize_t end = text.size;
    while (end >= DECL_MAP_MARK_SIZE && text.buf[end - DECL_MAP_MARK_SIZE] == DECL_MAP_MARK) end -= DECL_MAP_MARK_SIZE;
    return end;
}

char decl_map_last_char(csview text) {
    isize_t end = decl_map_text_end(text);
    return end ? text.buf[end - 1] : 0;
}

void decl_map_strip(cstr* text) {
    char* buf = cstr_data(text);
    isize_t size = cstr_size(text), to = 0;
    if (!memchr(buf, DECL_MAP_MARK, (size_t)size)) return;
    for (isize_t from = 0; from < size;) {
        if (buf[from] == DECL_MAP_MARK && from + DECL_MAP_MARK_SIZE <= size) {
            from += DECL_MAP_MARK_SIZE;
            continue;
        }
        buf[to++] = buf[from++];
    }
    cstr_resize(text, to, '\0');
}

// tsc's sourcemap.Generator, whose output depends on the order and repetition of what it is given: a mapping at the
// position of the one pending replaces it, unless that one has no source or this one goes back in the source
typedef struct Generator {
    cstr mappings;
    i64 last_line, last_col, last_source_line, last_source_col;
    bool has_last;
    i64 pending_line, pending_col, pending_source_line, pending_source_col;
    bool has_pending;
} Generator;

static void append_vlq(cstr* out, i64 value) {
    static char const digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u64 v = value < 0 ? ((u64)(-value) << 1) + 1 : (u64)value << 1;
    do {
        u64 digit = v & 31;
        v >>= 5;
        if (v) digit |= 32;
        cstr_append_n(out, &digits[digit], 1);
    } while (v);
}

static void commit(Generator* me) {
    if (!my has_pending) return;
    if (my has_last && my last_line == my pending_line && my last_col == my pending_col &&
        my last_source_line == my pending_source_line && my last_source_col == my pending_source_col) {
        return;
    }
    if (my last_line < my pending_line) {
        while (my last_line < my pending_line) {
            cstr_append(&my mappings, ";");
            my last_line++;
        }
        my last_col = 0;
    } else if (my has_last) {
        cstr_append(&my mappings, ",");
    }
    append_vlq(&my mappings, my pending_col - my last_col);
    my last_col = my pending_col;
    append_vlq(&my mappings, 0); // (One source)
    append_vlq(&my mappings, my pending_source_line - my last_source_line);
    my last_source_line = my pending_source_line;
    append_vlq(&my mappings, my pending_source_col - my last_source_col);
    my last_source_col = my pending_source_col;
    my has_last = true;
}

static void add(Generator* me, i64 line, i64 col, i64 source_line, i64 source_col) {
    bool new_position = !my has_pending || my pending_line != line || my pending_col != col;
    bool backtracking = my has_pending && (my pending_source_line > source_line ||
        (my pending_source_line == source_line && my pending_source_col > source_col));
    if (new_position || backtracking) {
        commit(me);
        my pending_line = line;
        my pending_col = col;
        my has_pending = true;
    }
    my pending_source_line = source_line;
    my pending_source_col = source_col;
}

cstr decl_map_take(cstr* text, SourceFile const* source) {
    Generator gen = { .mappings = cstr_init() };
    char const* src = cstr_str(&source->contents);
    u32 src_size = (u32)cstr_size(&source->contents);
    // (tsc's text has no byte order mark)
    u32 bom = src_size >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF ? 3 : 0;
    char* buf = cstr_data(text);
    isize_t size = cstr_size(text), to = 0;
    i64 line = 0, col = 0;
    // (Source positions come mostly in order: the line search goes on from the last one, and on the same line so does
    // the column count, from col_offset: a long line's marks would rescan it from its start each)
    u32 source_line = 0;
    u32 col_line = UINT32_MAX, col_offset = 0;
    i64 col_units = 0;
    for (isize_t from = 0; from < size;) {
        char c = buf[from];
        if (c == DECL_MAP_MARK && from + DECL_MAP_MARK_SIZE <= size) {
            u32 offset = 0;
            for (int i = 0; i < 8; i++) offset = (offset << 4) | (u32)(buf[from + 1 + i] - 0x10);
            from += DECL_MAP_MARK_SIZE;
            if (offset > src_size) continue;
            if (source_line >= source->line_count || source->line_starts[source_line] > offset) {
                // (Back: the last line starting at or before offset)
                u32 lo = 0, hi = source->line_count;
                while (hi - lo > 1) {
                    u32 mid = lo + (hi - lo) / 2;
                    if (source->line_starts[mid] <= offset) lo = mid;
                    else hi = mid;
                }
                source_line = lo;
            }
            while (source_line + 1 < source->line_count && source->line_starts[source_line + 1] <= offset) source_line++;
            u32 line_start = source->line_starts[source_line];
            if (source_line == 0 && offset >= bom) line_start = bom;
            if (col_line != source_line || col_offset > offset || col_offset < line_start) {
                col_line = source_line;
                col_offset = line_start;
                col_units = 0;
            }
            for (char const* p = src + col_offset; p < src + offset; p++) col_units += utf8_byte_utf16_units(p, src + line_start);
            col_offset = offset;
            add(&gen, line, col, source_line, col_units);
            continue;
        }
        buf[to++] = c;
        from++;
        if (c == '\n') {
            line++;
            col = 0;
        } else {
            // (By lead bytes: the text is being moved over itself, a continuation byte's lead may be gone)
            unsigned char b = (unsigned char)c;
            col += (b & 0xC0) == 0x80 ? 0 : b >= 0xF0 ? 2 : 1;
        }
    }
    cstr_resize(text, to, '\0');
    commit(&gen);
    return gen.mappings;
}

// JSON string contents as Go's encoding/json writes them (HTML characters escaped)
static void append_json_string(cstr* out, csview s) {
    cstr_append(out, "\"");
    for (isize_t i = 0; i < s.size; i++) {
        unsigned char c = (unsigned char)s.buf[i];
        if (c == '"' || c == '\\') {
            char esc[3] = { '\\', (char)c, 0 };
            cstr_append(out, esc);
        } else if (c < 0x20 || c == '<' || c == '>' || c == '&') {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            cstr_append(out, esc);
        } else {
            cstr_append_n(out, (char const*)&s.buf[i], 1);
        }
    }
    cstr_append(out, "\"");
}

cstr decl_map_json(csview file, csview source_root, csview source, csview mappings) {
    cstr out = cstr_from("{\"version\":3,\"file\":");
    append_json_string(&out, file);
    if (source_root.size) {
        cstr_append(&out, ",\"sourceRoot\":");
        append_json_string(&out, source_root);
    }
    cstr_append(&out, ",\"sources\":[");
    append_json_string(&out, source);
    cstr_append(&out, "],\"names\":[],\"mappings\":");
    append_json_string(&out, mappings);
    cstr_append(&out, "}");
    return out;
}

char const* decl_map_skip_trivia(char const* at, char const* end) {
    while (at < end) {
        unsigned char c = (unsigned char)*at;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            at++;
        } else if (c == 0xC2 && at + 1 < end && (unsigned char)at[1] == 0xA0) {
            at += 2;
        } else if (c == 0xE2 && at + 2 < end && (unsigned char)at[1] == 0x80 && ((unsigned char)at[2] == 0xA8 || (unsigned char)at[2] == 0xA9)) {
            at += 3;
        } else if (c == 0xEF && at + 2 < end && (unsigned char)at[1] == 0xBB && (unsigned char)at[2] == 0xBF) {
            at += 3;
        } else if (c == '/' && at + 1 < end && at[1] == '/') {
            while (at < end && *at != '\n' && *at != '\r') at++;
        } else if (c == '/' && at + 1 < end && at[1] == '*') {
            char const* close = at + 2;
            while (close + 1 < end && !(close[0] == '*' && close[1] == '/')) close++;
            at = close + 1 < end ? close + 2 : end;
        } else {
            break;
        }
    }
    return at;
}

char const* decl_map_back_trivia(char const* at, char const* start) {
    while (at > start) {
        unsigned char c = (unsigned char)at[-1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            at--;
        } else if (c == '/' && at - 1 > start && at[-2] == '*') {
            char const* open = at - 2;
            while (open - 1 > start && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open - 1 <= start) break;
            at = open - 2;
        } else {
            // A line comment ending here: its `//` on this line, after whitespace or at the line's start (not in a
            // string, approximately)
            char const* line = at;
            while (line > start && line[-1] != '\n' && line[-1] != '\r') line--;
            char const* comment = nullptr;
            for (char const* p = line; p + 1 < at; p++) {
                if (*p == '"' || *p == '\'' || *p == '`') break;
                if (p[0] == '/' && p[1] == '/' && (p == line || p[-1] == ' ' || p[-1] == '\t')) {
                    comment = p;
                    break;
                }
            }
            if (!comment) break;
            at = comment;
        }
    }
    return at;
}

static bool in_file(SourceFile const* file, char const* at) {
    char const* text = cstr_str(&file->contents);
    return at && at >= text && at <= text + cstr_size(&file->contents);
}

static char const* file_end(SourceFile const* file) { return cstr_str(&file->contents) + cstr_size(&file->contents); }

static bool word_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$' || (unsigned char)c >= 0x80; }

// (A span may take in the whitespace after it: a node ends at its last token)
static char const* span_end_in(SourceFile const* file, Span span) {
    if (!span_valid(span) || !in_file(file, span.buf)) return nullptr;
    char const* end = span_end_ptr(span);
    while (end > span.buf && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    return end;
}

char const* decl_map_type_start(TypeNode const* node, SourceFile const* file) {
    if (!file || !node->pos || node->pos - 1 > (u32)cstr_size(&file->contents)) return nullptr;
    return cstr_str(&file->contents) + node->pos - 1;
}

char const* decl_map_type_inner_start(TypeNode const* node, SourceFile const* file) {
    char const* start = decl_map_type_start(node, file);
    if (!start || !node->parenthesized) return start;
    char const* end = file_end(file);
    // (As many `(` as written, and no more: what is inside may start with one, `(() => T)`)
    for (u32 i = 0; i <= node->paren_extra && start < end && *start == '('; i++) start = decl_map_skip_trivia(start + 1, end);
    return start;
}

char const* decl_map_type_inner_end(TypeNode const* node, SourceFile const* file) {
    char const* start = decl_map_type_inner_start(node, file);
    if (!start) return nullptr;
    switch (node->tag) {
        case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_NEVER: case TYPE_VOID: case TYPE_BOOLEAN: case TYPE_NUMBER:
        case TYPE_STRING: case TYPE_SYMBOL: case TYPE_BIGINT: {
            char const* end = file_end(file);
            char const* c = start;
            if (node->readonly_invalid && c + 8 <= end && !memcmp(c, "readonly", 8)) c = decl_map_skip_trivia(c + 8, end);
            while (c < end && word_char(*c)) c++;
            return c;
        }
        case TYPE_VALUE: return span_end_in(file, node->value_span);
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            if (!identifier_type_import_ref(identifier) && !identifier_type_import_argument(identifier)) {
                return span_end_in(file, identifier_type_span(identifier));
            }
            // `import("m").A.B<T>`: past its qualifier and type arguments (`import("m")`: its `)`)
            char const* end = span_end_in(file, identifier->name);
            ModuleRef const* ref = identifier_type_import_ref(identifier);
            if (!end && ref && span_valid(ref->span) && in_file(file, ref->span.buf)) {
                char const* stop = file_end(file);
                char const* c = span_end_ptr(ref->span);
                int depth = 0;
                for (; c < stop && !(depth == 0 && *c == ')'); c++) {
                    if (*c == '{' || *c == '(' || *c == '[') depth++;
                    else if (*c == '}' || *c == ')' || *c == ']') depth--;
                }
                end = c < stop ? c + 1 : nullptr;
            }
            u32 args = (u32)vec_spanned_type_size(&identifier->generic_params);
            if (args) {
                char const* last = span_end_in(file, vec_spanned_type_at(&identifier->generic_params, args - 1)->span);
                char const* stop = file_end(file);
                if (last) {
                    end = decl_map_skip_trivia(last, stop);
                    if (end < stop && *end == ',') end = decl_map_skip_trivia(end + 1, stop);
                    if (end < stop && *end == '>') end++;
                }
            }
            return end;
        }
        case TYPE_CALLABLE: return span_end_in(file, node->callable->function->full_span);
        case TYPE_INTERFACE: return span_end_in(file, node->interface.span);
        case TYPE_ARRAY: return span_end_in(file, node->array.span);
        case TYPE_TUPLE: return span_end_in(file, node->tuple.span);
        case TYPE_INDEXED: return span_end_in(file, node->indexed->span);
        case TYPE_KEYOF: return decl_map_type_end(&node->keyof->interface, file);
        case TYPE_TYPEOF: return span_end_in(file, expr_outer_span(node->typeof_->expr));
        case TYPE_TERNARY: return span_end_in(file, node->ternary->span);
        case TYPE_INFERRED: return span_end_in(file, node->inferred->full_span);
        case TYPE_UNION: case TYPE_INTERSECTION: {
            for (isize_t i = vec_type_size(&node->composite); i-- > 0;) {
                TypeNode const* member = vec_type_at(&node->composite, i);
                if (!member->placeholder) return decl_map_type_end(member, file);
            }
            return nullptr;
        }
        case TYPE_UNIQUE_SYMBOL: {
            char const* end = file_end(file);
            char const* c = start;
            while (c < end && word_char(*c)) c++;
            c = decl_map_skip_trivia(c, end);
            while (c < end && word_char(*c)) c++;
            return c;
        }
        case TYPE_TEMPLATE: return span_end_in(file, node->template_->span);
        case TYPE_JSDOC: return span_end_in(file, node->jsdoc->span);
        case TYPE_PREDICATE: return span_end_in(file, node->predicate->span);
    }
    return nullptr;
}

char const* decl_map_type_end(TypeNode const* node, SourceFile const* file) {
    char const* end = decl_map_type_inner_end(node, file);
    if (!end || !node->parenthesized) return end;
    char const* stop = file_end(file);
    end = decl_map_skip_trivia(end, stop);
    if (end >= stop || *end != ')') return end;
    end++;
    // (`((T))`: past each `)`)
    for (u32 i = 0; i < node->paren_extra; i++) {
        char const* close = decl_map_skip_trivia(end, stop);
        if (close >= stop || *close != ')') break;
        end = close + 1;
    }
    return end;
}
