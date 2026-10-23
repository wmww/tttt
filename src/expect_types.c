// realpath(), which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include "expect_types.h"
#include "ast_walk.h"
#include "compiler_options.h"
#include "expr.h"
#include "program.h"
#include "source_file.h"
#include "statement.h"
#include "ty/ty.h"
#include <stdio.h>
#include <string.h>

// The byte offset of a 1-based line and 1-based column counted in UTF-16 units (tsc's), or -1 past the line's end
static i64 utf16_offset(SourceFile const* file, u32 line, u32 col) {
    if (line == 0 || line > file->line_count) return -1;
    char const* text = cstr_str(&file->contents);
    i64 size = cstr_size(&file->contents);
    i64 at = file->line_starts[line - 1];
    for (u32 units = 1; units < col; at++) {
        if (at >= size) return -1;
        unsigned char c = (unsigned char)text[at];
        if ((c & 0xC0) == 0x80) continue; // A continuation byte
        units += c >= 0xF0 ? 2 : 1; // Outside the BMP: a surrogate pair
    }
    return at;
}

typedef struct ExpectFind {
    char const* start;
    char const* end;
    Expr const* found;
} ExpectFind;

static AstWalk find_span(Expr const* expr, void* arg) {
    ExpectFind* find = arg;
    Span span = expr_outer_span(expr);
    char const* start = span_start_ptr(span);
    char const* end = span_end_ptr(span);
    // Pre-order: the outermost of the expressions with the span comes first
    if (start == find->start && end == find->end) {
        find->found = expr;
        return AST_WALK_STOP;
    }
    // The name of a property access is the access to tsc's getTypeAtLocation (getRegularTypeOfExpression)
    if (expr->tag == EXPR_PROPERTY && expr->property->form != PROPERTY_EXPR_BRACKET) {
        Span name = property_name(expr->property);
        if (span_start_ptr(name) == find->start && span_end_ptr(name) == find->end) {
            find->found = expr;
            return AST_WALK_STOP;
        }
    }
    return start > find->end || end < find->start ? AST_WALK_SKIP : AST_WALK_CONTINUE;
}

static SourceFile const* find_file(Program const* program, csview path) {
    SourceFile const* file = program_find_source(program, path);
    if (file) return file;
    // The program names files its own way (relative to the working directory, through symlinks or not): compare the
    // real paths
    cstr wanted_path = cstr_from_sv(path);
    char* wanted = realpath(cstr_str(&wanted_path), nullptr);
    cstr_drop(&wanted_path);
    if (!wanted) return nullptr;
    for (u32 i = 0; i < program_source_count(program) && !file; i++) {
        SourceFile const* candidate = program_source_at(program, i);
        if (candidate->is_stdlib) continue;
        char* real = realpath(cstr_str(&candidate->canonical_path), nullptr);
        if (real && strcmp(real, wanted) == 0) file = candidate;
        free(real);
    }
    free(wanted);
    return file;
}

void program_print_expect_types(Program* program, char const* spans_path) {
    FILE* in = fopen(spans_path, "r");
    if (!in) {
        fprintf(stderr, "Can't read %s\n", spans_path);
        return;
    }
    program_make_current(program);
    // (NoTruncation: checking is done, the option only matters to the printer from here on)
    ignore compiler_options_set(program_options(program), c_sv("noerrortruncation"), c_sv("true"));
    set_compiler_options(program_options(program));
    char path[4096];
    unsigned line, col, end_line, end_col;
    while (fscanf(in, "%4095[^\t]\t%u\t%u\t%u\t%u\n", path, &line, &col, &end_line, &end_col) == 5) {
        SourceFile const* file = find_file(program, csview_from(path));
        if (!file) continue;
        i64 start = utf16_offset(file, line, col), end = utf16_offset(file, end_line, end_col);
        if (start < 0 || end < start) continue;
        char const* text = cstr_str(&file->contents);
        ExpectFind find = { .start = text + start, .end = text + end };
        for_each (i, vec_statement, &file->ast.statements) {
            if (ast_walk_statement(i, find_span, &find)) break;
        }
        if (!find.found) continue;
        // (The file's own checker: its memos are the ones checking left)
        Checker* checker = program_checker_of(program, file);
        checker_make_current(checker);
        // What the `expect` lint rule prints: typeToString(getTypeAtLocation(node), undefined, NoTruncation), the bare
        // names of messages without an enclosing declaration, and without AllowUniqueESSymbolType
        cstr text_out = cstr_null;
        ty_display(checker, (TyDisplay){ .target = &text_out, .typeof_unique_symbols = true },
            ty_regular(checker, ty_expr_type(checker, find.found)));
        printf("%s:%u:%u\t%s\n", path, line, col, cstr_str(&text_out));
        cstr_drop(&text_out);
    }
    fclose(in);
    clear_compiler_options();
    checker_make_current(nullptr);
}
