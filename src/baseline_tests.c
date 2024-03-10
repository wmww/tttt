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

typedef struct TestCaseCtx {
    const char* test_name;
    cstr symbols;
    cstr types;
} TestCaseCtx;

void Statement_compile_test_info(Statement* self, TestCaseCtx* ctx);
void Block_compile_test_info(Block* self, TestCaseCtx* ctx);
void Expr_compile_test_info(Expr* self, TestCaseCtx* ctx);
void Type_compile_test_info(Type* self, TestCaseCtx* ctx);

void TestCaseCtx_add_symbol(TestCaseCtx* ctx, const char* name, Span const* span) {
    const char* line_start = span_line_start(span);
    unsigned line = span_line_number(span) - 2;
    unsigned col = span_start_ptr(span) - line_start - 1;
    cstr_append_fmt(
        &ctx->symbols,
        ">%s : Symbol(%s, Decl(%s.ts, %d, %d))\r\n",
        name,
        name,
        ctx->test_name,
        line,
        col
    );
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
        cstr_append(&ctx->symbols, "\r\n");
        cstr_append(&ctx->types, "\r\n");
    }
}

void Expr_compile_test_info(Expr* self, TestCaseCtx* ctx) {
    cstr_append(&ctx->types, ">");
    cstr_append_sv(&ctx->types, span_sv(&self->span));
    cstr_append(&ctx->types, " : ");
    Type t = Expr_get_type_and_typecheck(self, nullptr);
    cstr_append_type_error_msg(&ctx->types, &t);
    Type_drop(&t);
    cstr_append(&ctx->types, "\r\n");

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
                TestCaseCtx_add_symbol(ctx, cstr_str(&i.ref->name), &self->span);
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

bool run_test_case(TestCtx* ctx, const char* test_name, const char* test_ts_path) {
    bool ret = true;
    ErrorCtx errs = {0};
    Block ast = {0};
    cstr full_ts_path = cstr_from_fmt("%s/cases/%s", ctx->tests_dir, test_ts_path);
    cstr errors_path = cstr_from_fmt("%s/baselines/reference/%s.errors.txt", ctx->tests_dir, test_name);
    cstr symbols_path = cstr_from_fmt("%s/baselines/reference/%s.symbols", ctx->tests_dir, test_name);
    cstr types_path = cstr_from_fmt("%s/baselines/reference/%s.types", ctx->tests_dir, test_name);
    cstr js_path = cstr_from_fmt("%s/baselines/reference/%s.js", ctx->tests_dir, test_name);
    cstr ts_source = cstr_null;
    cstr errors_result = cstr_null;
    TestCaseCtx case_ctx = {
        .test_name = test_name
    };

    printf("Testing %s\n", cstr_str(&full_ts_path));

    if (!read_entire_file(cstr_str(&full_ts_path), true, &ts_source)) {
        fprintf(stderr, "%s: Failed to read source file\n", test_name);
        goto cleanup;
    }

    ast = parse_file(cstr_str(&ts_source), &errs);

    NameStack names = NameStack_init();
    Block_resolve_names(&ast, &names, &errs);
    NameStack_drop(&names);

    Block_typecheck(&ast, &errs);

    Block_compile_test_info(&ast, &case_ctx);

    if (!ErrorCtx_empty(&errs)) {
        ErrorCtx_append_errors_txt(&errs, &errors_result);
    }

    ret &= compare_outputs(test_name, "errors", cstr_str(&errors_path), cstr_sv(&errors_result));
    ret &= compare_outputs(test_name, "symbols", cstr_str(&symbols_path), cstr_sv(&case_ctx.symbols));
    ret &= compare_outputs(test_name, "types", cstr_str(&types_path), cstr_sv(&case_ctx.types));

cleanup:
    c_drop(cstr,
        &full_ts_path,
        &errors_path,
        &symbols_path,
        &types_path,
        &js_path,
        &ts_source,
        &errors_result,
        &case_ctx.symbols,
        &case_ctx.types
    );
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
