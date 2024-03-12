#include "type.h"
#include "generic.h"
#include "expr.h"
#include "as_type_expr.h"
#include "function_impl.h"
#include "statement.h"
#include "parser.h"
#include "block.h"
#include "name_stack.h"
#include "filesystem.h"
#include "error.h"
#include "engine.h"
#include "subprocess.h"
#include "string_helpers.h"

typedef struct TestCtx {
    const char* tests_dir;
} TestCtx;

bool compare_outputs(const char* test_name, const char* output_name, const char* expected_path, csview ours) {
    cstr expected = cstr_null;
    if (path_is_reg_file(expected_path)) {
        if (!read_entire_file(expected_path, true, &expected)) {
            return false;
        }
    }

    csview expected_sv = cstr_sv(&expected);
    if (csview_cmp(&ours, &expected_sv) == 0) {
        cstr_drop(&expected);
        return true;
    } else {
        printf("%s: %s did not match:\n", test_name, output_name);

        cstr result_display = cstr_from_fmt("tttt %s:\n", output_name);
        cstr_append_sv(&result_display, ours);
        cstr_append_sv(&result_display, c_sv("\n"));

        cstr expected_display = cstr_from_fmt("%s:\n", expected_path);
        cstr_append_sv(&expected_display, expected_sv);
        cstr_append_sv(&expected_display, c_sv("\n"));

        show_diff(cstr_sv(&result_display), cstr_sv(&expected_display));

        c_drop(cstr, &expected, &result_display, &expected_display);
        return false;
    }
}

typedef struct LineNumberedStr {
    int line;
    cstr str;
} LineNumberedStr;

void LineNumberedStr_drop(LineNumberedStr* self) {
    cstr_drop(&self->str);
}

#define i_key_class LineNumberedStr
#define i_no_clone
#include "stc/vec.h"

cstr render_lines_woven_with_file(const char* rel_path, csview file, vec_LineNumberedStr const* lines) {
    cstr ret = cstr_null;
    int line = 0;
    int index = 0;
    int vec_size = vec_LineNumberedStr_size(lines);
    cstr_append_fmt(&ret, "//// [tests/cases/%s] ////\r\n", rel_path);
    cstr_append(&ret, "\r\n");
    cstr_append_fmt(&ret, "=== %s ===\r\n", basename_of(rel_path));
    c_foreach(i, LinesSv, file) {
        cstr_append_sv(&ret, i.line);
        if (csview_size(i.line)) {
            cstr_append(&ret, "\r\n");
        }
        bool lines_added = false;
        while (index < vec_size && vec_LineNumberedStr_at(lines, index)->line <= line) {
            cstr_append_s(&ret, vec_LineNumberedStr_at(lines, index)->str);
            cstr_append(&ret, "\r\n");
            lines_added = true;
            index++;
        }
        if (lines_added) {
            cstr_append(&ret, "\r\n");
        }
        line++;
    }
    return ret;
}

typedef struct TestCaseCtx {
    const char* test_path;
    cstr contents;
    vec_LineNumberedStr symbols;
    vec_LineNumberedStr types;
} TestCaseCtx;

bool load_test_case(const char* test_path, const char* full_path, TestCaseCtx* ret) {
    cstr raw_contents_str = cstr_null;
    cstr contents = cstr_null;

    if (!read_entire_file(full_path, true, &raw_contents_str)) {
        return false;
    }

    csview raw_contents = cstr_sv(&raw_contents_str);
    if (csview_starts_with(raw_contents, "\xEF\xBB\xBF")) {
        // Byte order mark, check only needed
        raw_contents = csview_last(raw_contents, csview_size(raw_contents) - 3);
    }
    bool has_line = false;
    c_foreach(i, LinesSv, raw_contents) {
        if (!csview_starts_with(i.line, "// @")) {
            cstr_append_sv(&contents, i.line);
            if (has_line || csview_size(i.line)) {
                cstr_append(&contents, "\r\n");
                has_line = true;
            }
        }
    }

    cstr_drop(&raw_contents_str);

    *ret = (TestCaseCtx){
        .test_path = test_path,
        .contents = contents,
        .symbols = vec_LineNumberedStr_init(),
        .types = vec_LineNumberedStr_init(),
    };
    return true;
}

void TestCaseCtx_drop(TestCaseCtx* self) {
    cstr_drop(&self->contents);
    vec_LineNumberedStr_drop(&self->symbols);
    vec_LineNumberedStr_drop(&self->types);
}

void Statement_compile_test_info(Statement* self, TestCaseCtx* ctx);
void Block_compile_test_info(Block* self, TestCaseCtx* ctx);
void Expr_compile_test_info(Expr* self, TestCaseCtx* ctx);
void Type_compile_test_info(Type* self, TestCaseCtx* ctx);

void TestCaseCtx_add_symbol(TestCaseCtx* ctx, const char* name, Span const* span) {
    const char* line_start = span_line_start(span);
    unsigned line = span_line_index(span);
    const char* span_start = span_start_ptr(span);
    // The space before the symbol is included in the span. Why? I have no idea.
    while (span_start > line_start && *(span_start - 1) == ' ') span_start--;
    unsigned col = span_start - line_start + 1;
    vec_LineNumberedStr_push(&ctx->symbols, (LineNumberedStr){
        .line = line,
        .str = cstr_from_fmt(
            ">%s : Symbol(%s, Decl(%s, %d, %d))",
            name, name,
            basename_of(ctx->test_path),
            line, col - 1
        ),
    });
}

void TestCaseCtx_add_type(TestCaseCtx* ctx, Span const* span, Type const* type) {
    unsigned line = span_line_index(span);
    cstr str = cstr_from_fmt(">%.*s : ", c_SV(span_sv(span)));
    cstr_append_type_error_msg(&str, type);
    vec_LineNumberedStr_push(&ctx->types, (LineNumberedStr){
        .line = line,
        .str = str,
    });
}

void Statement_compile_test_info(Statement* self, TestCaseCtx* ctx) {
    switch (self->tag) {
        case STATEMENT_NOOP: break;
        case STATEMENT_INTERFACE: break;
        case STATEMENT_BLOCK:
            Block_compile_test_info(&self->block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_RETURN:
            Expr_compile_test_info(&self->expr, ctx);
            break;

        case STATEMENT_VAR_INIT:
            TestCaseCtx_add_symbol(ctx, cstr_str(&self->var_init.data.name), &self->var_init.data.name_span);
            Type_compile_test_info(&self->var_init.data.type, ctx);
            Expr_compile_test_info(&self->var_init.data.value, ctx);
            break;

        case STATEMENT_FUNCTION:
            Block_compile_test_info(&self->function.impl.body, ctx);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_Statement_size(&self->if_else.statements);
            int cond_size = vec_Expr_size(&self->if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    Expr_compile_test_info(vec_Expr_at_mut(&self->if_else.conditions, i), ctx);
                }
                Statement_compile_test_info(vec_Statement_at_mut(&self->if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_LOOP:
            Statement_compile_test_info(&self->loop->initialization, ctx);
            Expr_compile_test_info(&self->loop->condition, ctx);
            Expr_compile_test_info(&self->loop->afterthought, ctx);
            Statement_compile_test_info(&self->loop->body, ctx);
            break;

        case STATEMENT_BREAK: break;
    }
}

void Block_compile_test_info(Block* self, TestCaseCtx* ctx) {
    c_foreach(i, vec_Statement, self->statements) {
        Statement_compile_test_info(i.ref, ctx);
    }
}

void Expr_compile_test_info(Expr* self, TestCaseCtx* ctx) {
    Type t = Expr_get_type_and_typecheck(self, nullptr);
    TestCaseCtx_add_type(ctx, &self->span, &t);
    Type_drop(&t);

    switch (self->tag) {
        case EXPR_LITERAL:
            break;

        case EXPR_IDENTIFIER:
            TestCaseCtx_add_symbol(ctx, cstr_str(&self->identifier.name), &self->span);
            break;

        case EXPR_BINARY_OP:
            Expr_compile_test_info(&self->binary_op->left, ctx);
            Expr_compile_test_info(&self->binary_op->right, ctx);
            break;

        case EXPR_UNARY_OP:
            Expr_compile_test_info(&self->unary_op->expr, ctx);
            break;

        case EXPR_DOT_PROP:
            Expr_compile_test_info(&self->dot_prop->expr, ctx);
            break;

        case EXPR_AS_TYPE:
            Expr_compile_test_info(&self->as_type->expr, ctx);
            Type_compile_test_info(&self->as_type->type, ctx);
            break;

        case EXPR_CALL:
            Expr_compile_test_info(&self->call->callable, ctx);
            c_foreach(i, vec_Expr, self->call->args) {
                Expr_compile_test_info(i.ref, ctx);
            }
            break;

        case EXPR_FUNCTION:
            c_foreach(i, vec_FunctionArg, self->function->args) {
                TestCaseCtx_add_symbol(ctx, cstr_str(&i.ref->name), &i.ref->name_span);
            }
            Block_compile_test_info(&self->function->body, ctx);
            break;

        case EXPR_OBJECT:
            c_foreach(i, vec_ObjectExprItem, self->object) {
                Expr_compile_test_info(&i.ref->value, ctx);
            }
            break;
    }
}

void Type_compile_test_info(Type* self, TestCaseCtx* ctx) {
    switch (self->tag) {
        case TYPE_ANY:
        case TYPE_UNKOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_VALUE:
            break;

        case TYPE_IDENTIFIER:
            TestCaseCtx_add_symbol(ctx, cstr_str(&self->identifier.name), &self->identifier.span);
            break;

        case TYPE_GENERIC:
            Type_compile_test_info(&self->generic->inner, ctx);
            break;

        case TYPE_CALLABLE: break;
        case TYPE_INTERFACE: break;
        case TYPE_TERNARY: break;
    }
}

bool run_test_case(TestCtx* ctx, const char* test_name, const char* test_path) {
    bool ret = true;
    ErrorCtx errs = {0};
    Block ast = {0};
    cstr full_test_path = cstr_from_fmt("%s/cases/%s", ctx->tests_dir, test_path);
    cstr errors_path = cstr_from_fmt("%s/baselines/reference/%s.errors.txt", ctx->tests_dir, test_name);
    cstr symbols_path = cstr_from_fmt("%s/baselines/reference/%s.symbols", ctx->tests_dir, test_name);
    cstr types_path = cstr_from_fmt("%s/baselines/reference/%s.types", ctx->tests_dir, test_name);
    cstr js_path = cstr_from_fmt("%s/baselines/reference/%s.js", ctx->tests_dir, test_name);
    cstr errors_result = cstr_null;
    cstr symbols_results = cstr_null;
    cstr types_results = cstr_null;
    TestCaseCtx case_ctx = { 0 };

    printf("Testing %s\n", cstr_str(&full_test_path));

    if (!load_test_case(test_path, cstr_str(&full_test_path), &case_ctx)) {
        fprintf(stderr, "%s: Failed to load test case\n", test_name);
        goto cleanup;
    }

    ast = parse_file(cstr_str(&case_ctx.contents), &errs);

    NameStack names = NameStack_init();
    Block_resolve_names(&ast, &names, &errs);
    NameStack_drop(&names);

    StatementCtx statement_ctx = StatementCtx_toplevel();
    Block_typecheck(&ast, &statement_ctx, &errs);
    StatementCtx_drop(&statement_ctx);

    Block_compile_test_info(&ast, &case_ctx);

    symbols_results = render_lines_woven_with_file(test_path, cstr_sv(&case_ctx.contents), &case_ctx.symbols);
    types_results = render_lines_woven_with_file(test_path, cstr_sv(&case_ctx.contents), &case_ctx.types);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_append_errors_txt(&errs, &errors_result);
    }

    ret &= compare_outputs(test_name, "errors", cstr_str(&errors_path), cstr_sv(&errors_result));
    ret &= compare_outputs(test_name, "symbols", cstr_str(&symbols_path), cstr_sv(&symbols_results));
    ret &= compare_outputs(test_name, "types", cstr_str(&types_path), cstr_sv(&types_results));

cleanup:
    c_drop(cstr,
        &full_test_path,
        &errors_path,
        &symbols_path,
        &types_path,
        &js_path,
        &errors_result,
        &symbols_results,
        &types_results
    );
    TestCaseCtx_drop(&case_ctx);
    ErrorCtx_drop(&errs);
    Block_drop(&ast);
    return ret;
}

int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    TestCtx ctx = {
        .tests_dir = "/home/code/TypeScript/tests",
    };
    return run_test_case(
        &ctx,
        "emitArrowFunctionsAsIs",
        "conformance/es6/arrowFunction/emitArrowFunctionsAsIs.ts"
    ) ? 1 : 0;
}
