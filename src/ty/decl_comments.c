#include "decl_comments.h"
#include "../source_file.h"

typedef struct CommentRange {
    char const* start;
    char const* end;
    bool line; // `//` (else `/* */`)
    bool trailing_newline; // A line break follows it (before the next comment or token)
} CommentRange;

// (Comment lists are short: past this many, the rest are dropped)
#define MAX_COMMENTS 64

typedef struct Comments {
    CommentRange ranges[MAX_COMMENTS];
    u32 count;
} Comments;

// Where comments start: past a BOM and a shebang line (tsc's iterateCommentRanges skips that at 0)
static char const* text_start(SourceFile const* file) {
    char const* text = cstr_str(&file->contents);
    char const* end = text + cstr_size(&file->contents);
    if (end - text >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3;
    }
    if (end - text >= 2 && text[0] == '#' && text[1] == '!') {
        while (text < end && *text != '\n' && *text != '\r') text++;
    }
    return text;
}

static char const* text_end(SourceFile const* file) { return cstr_str(&file->contents) + cstr_size(&file->contents); }

// The byte length of the line break at c (U+2028 and U+2029 too), 0 if there is none
static u32 line_break_at(char const* c, char const* end) {
    if (c >= end) return 0;
    if (*c == '\n') return 1;
    if (*c == '\r') return c + 1 < end && c[1] == '\n' ? 2 : 1;
    if (end - c >= 3 && (unsigned char)c[0] == 0xE2 && (unsigned char)c[1] == 0x80 && ((unsigned char)c[2] == 0xA8 || (unsigned char)c[2] == 0xA9)) {
        return 3;
    }
    return 0;
}

// The byte length of the single-line whitespace at c (tsc's isWhiteSpaceSingleLine, the common ones), 0 if none
static u32 space_at(char const* c, char const* end) {
    if (c >= end) return 0;
    if (*c == ' ' || *c == '\t' || *c == '\v' || *c == '\f') return 1;
    if (end - c >= 2 && (unsigned char)c[0] == 0xC2 && (unsigned char)c[1] == 0xA0) return 2;
    if (end - c >= 3 && (unsigned char)c[0] == 0xEF && (unsigned char)c[1] == 0xBB && (unsigned char)c[2] == 0xBF) return 3;
    return 0;
}

// tsc's iterateCommentRanges: the comments from pos, those of its line too when `trailing` (and then only those) or
// at the start of the file
static void comment_ranges(SourceFile const* file, char const* pos, bool trailing, Comments* out) {
    out->count = 0;
    char const* end = text_end(file);
    bool collecting = trailing || pos == text_start(file);
    CommentRange pending = {0};
    bool has_pending = false;
    while (pos < end) {
        u32 n = line_break_at(pos, end);
        if (n) {
            pos += n;
            if (trailing) break;
            collecting = true;
            if (has_pending) pending.trailing_newline = true;
            continue;
        }
        n = space_at(pos, end);
        if (n) {
            pos += n;
            continue;
        }
        if (*pos != '/' || pos + 1 >= end || (pos[1] != '/' && pos[1] != '*')) break;
        CommentRange range = { .start = pos, .line = pos[1] == '/' };
        pos += 2;
        if (range.line) {
            while (pos < end && !line_break_at(pos, end)) pos++;
            range.trailing_newline = pos < end;
        } else {
            char const* close = nullptr;
            for (char const* c = pos; c + 1 < end; c++) {
                if (c[0] == '*' && c[1] == '/') {
                    close = c;
                    break;
                }
            }
            pos = close ? close + 2 : end;
        }
        range.end = pos;
        if (collecting) {
            if (has_pending && out->count < MAX_COMMENTS) out->ranges[out->count++] = pending;
            pending = range;
            has_pending = true;
        }
    }
    if (has_pending && out->count < MAX_COMMENTS) out->ranges[out->count++] = pending;
}

// tsc's isJSDocLikeText and isPinnedComment: what a declaration file keeps
static bool is_kept(CommentRange const* range) {
    if (range->line) return false;
    isize_t length = range->end - range->start;
    return (length >= 5 && range->start[2] == '*' && range->start[3] != '/') || (length > 5 && range->start[2] == '!');
}

static void filter_kept(Comments* comments) {
    u32 kept = 0;
    for (u32 i = 0; i < comments->count; i++) {
        if (is_kept(&comments->ranges[i])) comments->ranges[kept++] = comments->ranges[i];
    }
    comments->count = kept;
}

// Back from a node's first token over whitespace and comments: its full start (tsc's node.pos)
static char const* full_start(SourceFile const* file, char const* token) {
    char const* start = text_start(file);
    char const* c = token;
    while (true) {
        while (c > start && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r' || c[-1] == '\v' || c[-1] == '\f')) c--;
        char const* comment = c > start ? source_file_comment_ending_at(file, c) : nullptr;
        if (!comment) return c;
        c = comment;
    }
}

static bool same_line(char const* a, char const* b) {
    for (char const* c = a < b ? a : b, *end = a < b ? b : a; c < end; c++) {
        if (*c == '\n' || *c == '\r') return false;
    }
    return true;
}

static bool at_line_start(cstr const* out) {
    isize_t size = cstr_size(out);
    return !size || cstr_str(out)[size - 1] == '\n';
}

static void write_line(cstr* out) {
    if (!at_line_start(out)) cstr_append_sv(out, c_sv("\n"));
}

static void write_spaces(cstr* out, isize_t count) {
    for (isize_t i = 0; i < count; i++) cstr_append_sv(out, c_sv(" "));
}

// tsc's calculateIndent: the columns of the whitespace from c (tabs to the next multiple of 4)
static isize_t indent_of(char const* c, char const* end) {
    isize_t indent = 0;
    while (c < end) {
        u32 n = space_at(c, end);
        if (!n) break;
        indent = *c == '\t' ? indent + 4 - indent % 4 : indent + 1;
        c += n;
    }
    return indent;
}

static bool is_trimmed_space(char const* c, char const* end, u32* n) {
    *n = space_at(c, end);
    if (!*n) *n = line_break_at(c, end);
    return *n != 0;
}

// tsc's writeCommentRangeWorker: a block comment's lines re-indented relative to where its first line starts
static void write_comment(SourceFile const* file, cstr* out, u32 indent, CommentRange const* range) {
    if (at_line_start(out)) write_spaces(out, (isize_t)indent * 4);
    if (range->line) {
        cstr_append_sv(out, (csview){ range->start, range->end - range->start });
        return;
    }
    char const* text = text_start(file);
    char const* end = text_end(file);
    char const* first_line = range->start;
    while (first_line > text && first_line[-1] != '\n' && first_line[-1] != '\r') first_line--;
    isize_t first_indent = -1;
    for (char const* pos = range->start; pos < range->end;) {
        // (The line's end, and where the next one starts)
        char const* line_end = pos;
        while (line_end < end && !line_break_at(line_end, end)) line_end++;
        char const* next_line = line_end + line_break_at(line_end, end);
        if (line_end >= end) next_line = end + 1;
        bool line_start = at_line_start(out);
        if (pos != range->start) {
            if (first_indent < 0) first_indent = indent_of(first_line, range->start);
            isize_t spaces = (isize_t)indent * 4 - first_indent + indent_of(pos, line_end);
            write_spaces(out, spaces);
            line_start = false;
        }
        char const* stop = line_end < range->end ? line_end : range->end;
        char const* from = pos;
        u32 n;
        while (from < stop && is_trimmed_space(from, stop, &n)) from += n;
        char const* to = stop;
        while (to > from && (to[-1] == ' ' || to[-1] == '\t' || to[-1] == '\r' || to[-1] == '\n' || to[-1] == '\v' || to[-1] == '\f')) to--;
        if (to > from) {
            if (line_start) write_spaces(out, (isize_t)indent * 4);
            cstr_append_sv(out, (csview){ from, to - from });
            if (stop != range->end) write_line(out);
        } else {
            cstr_append_sv(out, c_sv("\n")); // (An empty line is one, even at the start of a line)
        }
        pos = next_line;
    }
}

// tsc's emitComments with a separator after each comment (leading comments) or before the first (trailing ones)
static void write_comments(SourceFile const* file, cstr* out, u32 indent, Comments const* comments, bool before) {
    if (!comments->count) return;
    if (before) cstr_append_sv(out, c_sv(" "));
    bool intervening = false;
    for (u32 i = 0; i < comments->count; i++) {
        CommentRange const* range = &comments->ranges[i];
        if (intervening) {
            cstr_append_sv(out, c_sv(" "));
            intervening = false;
        }
        write_comment(file, out, indent, range);
        if (range->line || range->trailing_newline) write_line(out);
        else intervening = true;
    }
    if (intervening && !before) cstr_append_sv(out, c_sv(" "));
}

char const* decl_comments_end(DeclComments const* me, char const* end, char const* separators) {
    if (!me || !decl_comments_in_file(me, end)) return end;
    end = full_start(me->file, end);
    if (!separators) return end;
    char const* c = end;
    char const* stop = text_end(me->file);
    while (c < stop) {
        u32 n = space_at(c, stop);
        if (!n) n = line_break_at(c, stop);
        if (n) {
            c += n;
            continue;
        }
        if (*c == '/' && c + 1 < stop && (c[1] == '/' || c[1] == '*')) {
            Comments one;
            comment_ranges(me->file, c, true, &one);
            if (!one.count) break;
            c = one.ranges[0].end;
            continue;
        }
        break;
    }
    return c < stop && *c && strchr(separators, *c) ? c + 1 : end;
}

bool decl_comments_last_contains(DeclComments const* me, char const* token, char const* needle) {
    if (!me || !decl_comments_in_file(me, token)) return false;
    char const* pos = full_start(me->file, token);
    char const* end = text_end(me->file);
    CommentRange last = {0};
    while (pos < token) {
        u32 n = line_break_at(pos, end);
        if (!n) n = space_at(pos, end);
        if (n) {
            pos += n;
            continue;
        }
        Comments one;
        comment_ranges(me->file, pos, true, &one);
        if (!one.count) break;
        last = one.ranges[0];
        pos = one.ranges[0].end;
    }
    if (!last.start) return false;
    size_t length = strlen(needle);
    for (char const* c = last.start; c + length <= last.end; c++) {
        if (memcmp(c, needle, length) == 0) return true;
    }
    return false;
}

bool decl_comments_spans_lines(DeclComments const* me, char const* token, char const* end) {
    if (!me || !decl_comments_in_file(me, token)) return false;
    return !same_line(full_start(me->file, token), end);
}

bool decl_comments_leading_contains(DeclComments const* me, char const* token, char const* needle) {
    if (!me || !decl_comments_in_file(me, token)) return false;
    Comments comments;
    comment_ranges(me->file, full_start(me->file, token), false, &comments);
    size_t length = strlen(needle);
    for (u32 i = 0; i < comments.count; i++) {
        for (char const* c = comments.ranges[i].start; c + length <= comments.ranges[i].end; c++) {
            if (memcmp(c, needle, length) == 0) return true;
        }
    }
    return false;
}

bool decl_comments_in_file(DeclComments const* me, char const* token) {
    if (!me || !me->file || !token) return false;
    char const* text = cstr_str(&me->file->contents);
    return token >= text && token <= text + cstr_size(&me->file->contents);
}

void decl_comments_detached(DeclComments* me, cstr* out) {
    SourceFile const* file = me->file;
    char const* start = text_start(file);
    Comments comments;
    comment_ranges(file, start, false, &comments);
    if (!comments.count) return;
    // (The first token: past the last of the file's leading comments)
    char const* node = comments.ranges[comments.count - 1].end;
    char const* end = text_end(file);
    for (u32 n; node < end && ((n = space_at(node, end)) || (n = line_break_at(node, end)));) node += n;
    if (me->off) {
        u32 kept = 0;
        for (u32 i = 0; i < comments.count; i++) {
            CommentRange const* range = &comments.ranges[i];
            if (!range->line && range->end - range->start > 5 && range->start[2] == '!') comments.ranges[kept++] = *range;
        }
        comments.count = kept;
    }
    if (!comments.count) return;
    // The comments up to the first blank line, if a blank line follows them too
    u32 detached = 1;
    while (detached < comments.count) {
        char const* gap_from = comments.ranges[detached - 1].end;
        u32 breaks = 0;
        for (char const* c = gap_from; c < comments.ranges[detached].start; c++) breaks += *c == '\n';
        if (breaks >= 2) break;
        detached++;
    }
    char const* last_end = comments.ranges[detached - 1].end;
    u32 lines = 0;
    for (char const* c = last_end; c < node; c++) lines += *c == '\n';
    if (lines < 2) return;
    comments.count = detached;
    filter_kept(&comments);
    write_comments(file, out, 0, &comments, false);
    me->detached_end = last_end;
}

void decl_comments_leading(DeclComments const* me, cstr* out, u32 indent, char const* token) {
    if (!me || me->off || !decl_comments_in_file(me, token)) return;
    char const* pos = full_start(me->file, token);
    if (pos == text_start(me->file) && me->detached_end) pos = me->detached_end;
    Comments comments;
    comment_ranges(me->file, pos, false, &comments);
    // (Only those before the token: a full start computed back from it has nothing else in between)
    filter_kept(&comments);
    if (!comments.count) return;
    if (!same_line(pos, comments.ranges[0].start)) write_line(out);
    write_comments(me->file, out, indent, &comments, false);
}

void decl_comments_file_end(DeclComments const* me, cstr* out, char const* end) {
    if (!me || me->off || !decl_comments_in_file(me, end)) return;
    char const* pos = full_start(me->file, end);
    Comments comments;
    comment_ranges(me->file, pos, false, &comments);
    filter_kept(&comments);
    if (!comments.count) return;
    if (!same_line(pos, comments.ranges[0].start)) write_line(out);
    write_comments(me->file, out, 0, &comments, false);
    write_line(out);
}

void decl_comments_trailing(DeclComments const* me, cstr* out, char const* end) {
    if (!me || me->off || !decl_comments_in_file(me, end)) return;
    end = full_start(me->file, end);
    Comments comments;
    comment_ranges(me->file, end, true, &comments);
    filter_kept(&comments);
    write_comments(me->file, out, 0, &comments, true);
}

void decl_comments_intervening(DeclComments const* me, cstr* out, u32 indent, char const* token) {
    if (!me || me->off || !decl_comments_in_file(me, token)) return;
    Comments comments;
    comment_ranges(me->file, full_start(me->file, token), true, &comments);
    for (u32 i = 0; i < comments.count; i++) {
        CommentRange const* range = &comments.ranges[i];
        write_comment(me->file, out, indent, range);
        if (range->trailing_newline) write_line(out);
        else cstr_append_sv(out, c_sv(" "));
    }
}

void decl_comments_after_delimiter(DeclComments const* me, cstr* out, u32 indent, char const* token, bool prefix_space) {
    if (!me || me->off || !decl_comments_in_file(me, token)) return;
    Comments comments;
    comment_ranges(me->file, full_start(me->file, token), true, &comments);
    for (u32 i = 0; i < comments.count; i++) {
        CommentRange const* range = &comments.ranges[i];
        if (prefix_space) {
            if (!is_kept(range)) continue;
            if (!at_line_start(out)) cstr_append_sv(out, c_sv(" "));
            write_comment(me->file, out, indent, range);
            if (range->trailing_newline) write_line(out);
            continue;
        }
        write_comment(me->file, out, indent, range);
        if (range->line) write_line(out);
    }
}
