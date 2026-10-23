// realpath(), which strict -std=c2x hides
#define _XOPEN_SOURCE 700
#include "expect_types.h"
#include "ast_walk.h"
#include "class_decl.h"
#include "enum_decl.h"
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

// --node-shapes: the syntactic shape at a position (scripts/projdiff.py --redact): the kinds of the nodes around it
// from the top-level statement down, and the part of each node it is in, from a fixed vocabulary (never source text)
typedef struct Shape {
    char const* pos;
    char const* labels[96];
    u32 depth;
} Shape;

static bool shape_expr(Shape* s, Expr const* e);
static bool shape_statement(Shape* s, Statement const* st);
static bool shape_function(Shape* s, FunctionImpl const* f);
static bool shape_class(Shape* s, ClassDecl const* c);

static void shape_push(Shape* s, char const* label) {
    if (s->depth < (u32)c_countof(s->labels)) s->labels[s->depth] = label;
    s->depth++;
}

static bool shape_contains(Shape const* s, Span span) {
    return span_start_ptr(span) && span_start_ptr(span) <= s->pos && s->pos < span_end_ptr(span);
}

// `role` (may be null) then what `e` is, if `e` has the position, else nothing
static bool shape_sub(Shape* s, char const* role, Expr const* e) {
    if (!e || !shape_contains(s, expr_outer_span(e))) return false;
    if (role) shape_push(s, role);
    return shape_expr(s, e);
}

static bool shape_label_if(Shape* s, char const* label, Span span) {
    if (!shape_contains(s, span)) return false;
    shape_push(s, label);
    return true;
}

static bool shape_binding(Shape* s, Binding const* b) {
    u32 mark = s->depth;
    switch (b->tag) {
        case BINDING_NOOP: break;
        case BINDING_IDENTIFIER:
            if (shape_label_if(s, "binding name", b->content_span)) return true;
            break;
        case BINDING_ARRAY:
            shape_push(s, "array pattern");
            for_each (i, vec_binding, &b->array) {
                u32 inner = s->depth;
                shape_push(s, "element");
                if (shape_binding(s, i)) return true;
                s->depth = inner;
            }
            if (shape_contains(s, b->content_span)) return true;
            s->depth = mark;
            break;
        case BINDING_OBJECT:
            shape_push(s, "object pattern");
            for_each (i, vec_object_binding_item, &b->object) {
                if (shape_sub(s, "computed key", i->computed_key)) return true;
                u32 inner = s->depth;
                shape_push(s, "property");
                if (shape_binding(s, &i->target)) return true;
                s->depth = inner;
                if (shape_label_if(s, "property name", i->source_span)) return true;
            }
            if (shape_contains(s, b->content_span)) return true;
            s->depth = mark;
            break;
    }
    if (shape_sub(s, "default value", b->default_value)) return true;
    if (b->has_explicit_type && shape_label_if(s, "type annotation", binding_explicit_type_span(b))) return true;
    return false;
}

static bool shape_initialized(Shape* s, InitializedBinding const* b) {
    if (shape_binding(s, &b->binding)) return true;
    return shape_sub(s, "initializer", b->value);
}

static bool shape_statements(Shape* s, VecStatement const* statements) {
    for_each (i, vec_statement, statements) if (shape_statement(s, i)) return true;
    return false;
}

static bool shape_function(Shape* s, FunctionImpl const* f) {
    u32 i = 0;
    for_each (arg, vec_function_arg, &f->args) {
        i++;
        if (!shape_contains(s, arg->span)) continue;
        shape_push(s, i == 1 ? "parameter 1" : i == 2 ? "parameter 2" : i == 3 ? "parameter 3" : "parameter 4+");
        if (function_arg_decorators(arg)) {
            for_each (d, vec_expr, function_arg_decorators(arg)) if (shape_sub(s, "decorator", d)) return true;
        }
        shape_initialized(s, &arg->initializer);
        return true;
    }
    Block const* body = function_impl_body(f);
    if (f->expression_body) {
        // A synthesized `return` of the expression
        for_each (st, vec_statement, &body->statements) {
            if (st->tag == STATEMENT_RETURN_EXPR && shape_sub(s, "expression body", &st->expr)) return true;
        }
        return false;
    }
    u32 mark = s->depth;
    shape_push(s, "body");
    if (shape_statements(s, &body->statements)) return true;
    s->depth = mark;
    return false;
}

static bool shape_class(Shape* s, ClassDecl const* c) {
    for_each (d, vec_expr, &c->decorators) if (shape_sub(s, "decorator", d)) return true;
    if (shape_sub(s, "extends", &c->extends)) return true;
    for_each (i, vec_class_item, &c->items) {
        if (!shape_contains(s, i->span)) continue;
        for_each (d, vec_expr, &i->decorators) if (shape_sub(s, "decorator", d)) return true;
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                shape_push(s, "constructor");
                shape_function(s, &i->constructor.func);
                return true;
            case CLASS_ITEM_METHOD:
                shape_push(s, i->method.accessor == CLASS_ACCESSOR_GET ? "getter" :
                    i->method.accessor == CLASS_ACCESSOR_SET ? "setter" : "method");
                if (shape_sub(s, "computed key", i->method.computed_key)) return true;
                if (shape_label_if(s, "member name", i->method.name_span)) return true;
                shape_function(s, &i->method.func);
                return true;
            case CLASS_ITEM_FIELD:
                shape_push(s, "property declaration");
                if (shape_sub(s, "computed key", i->field.computed_key)) return true;
                if (shape_label_if(s, "member name", i->field.name_span)) return true;
                if (i->field.has_explicit_type && shape_label_if(s, "type annotation", i->field.explicit_type_span)) return true;
                shape_sub(s, "initializer", &i->field.value);
                return true;
            case CLASS_ITEM_INTERFACE_MEMBER:
                shape_push(s, "signature member");
                return true;
            case CLASS_ITEM_STATIC_BLOCK:
                shape_push(s, "static block");
                shape_statements(s, &i->static_block.statements);
                return true;
        }
    }
    return false;
}

static char const* shape_binary_label(BinaryOp op) {
    switch (op) {
        case BINARY_OP_EQUAL: return "assignment";
        case BINARY_OP_AND_AND: return "&&";
        case BINARY_OP_OR_OR: return "||";
        case BINARY_OP_EH_EH: return "??";
        case BINARY_OP_COMMA: return "comma";
        case BINARY_OP_IN: return "in";
        case BINARY_OP_INSTANCEOF: return "instanceof";
        case BINARY_OP_DOUBLE_EQUAL: case BINARY_OP_NOT_EQUAL: case BINARY_OP_TRIPLE_EQUAL: case BINARY_OP_TRIPLE_NOT_EQUAL:
            return "equality";
        case BINARY_OP_LESS: case BINARY_OP_LESS_EQUAL: case BINARY_OP_GREATER: case BINARY_OP_GREATER_EQUAL:
            return "comparison";
        case BINARY_OP_PLUS: return "+";
        case BINARY_OP_PLUS_EQUAL: case BINARY_OP_MINUS_EQUAL: case BINARY_OP_STAR_STAR_EQUAL: case BINARY_OP_STAR_EQUAL:
        case BINARY_OP_SLASH_EQUAL: case BINARY_OP_MOD_EQUAL: case BINARY_OP_LESS_LESS_EQUAL:
        case BINARY_OP_GREATER_GREATER_EQUAL: case BINARY_OP_GREATER_GREATER_GREATER_EQUAL: case BINARY_OP_AND_EQUAL:
        case BINARY_OP_XOR_EQUAL: case BINARY_OP_OR_EQUAL:
            return "compound assignment";
        case BINARY_OP_AND_AND_EQUAL: case BINARY_OP_OR_OR_EQUAL: case BINARY_OP_EH_EH_EQUAL: return "logical assignment";
        default: return "arithmetic";
    }
}

static char const* const shape_argument_labels[] = { "argument 1", "argument 2", "argument 3", "argument 4", "argument 5+" };

static bool shape_expr(Shape* s, Expr const* e) {
    switch (e->tag) {
        case EXPR_IMPLICIT_ANY: shape_push(s, "missing expression"); return true;
        case EXPR_LITERAL: shape_push(s, "literal"); return true;
        case EXPR_IDENTIFIER: shape_push(s, "identifier"); return true;
        case EXPR_REGEX: shape_push(s, "regex"); return true;
        case EXPR_PRIVATE_NAME: shape_push(s, "private name"); return true;
        case EXPR_BINARY_OP: {
            // (Down the left spine with a loop, like ast_walk's)
            while (true) {
                BinaryOpExpr const* b = e->binary_op;
                bool pattern = b->op == BINARY_OP_EQUAL && (b->left.tag == EXPR_OBJECT || b->left.tag == EXPR_ARRAY);
                shape_push(s, pattern ? "destructuring assignment" : shape_binary_label(b->op));
                if (shape_sub(s, "right", &b->right)) return true;
                if (!shape_contains(s, expr_outer_span(&b->left))) return true;
                shape_push(s, "left");
                e = &b->left;
                if (e->tag != EXPR_BINARY_OP) return shape_expr(s, e);
            }
        }
        case EXPR_UNARY_OP: {
            UnaryOp op = e->unary_op->op;
            shape_push(s, op == UNARY_OP_AWAIT ? "await" : op == UNARY_OP_TYPEOF ? "typeof" : op == UNARY_OP_VOID ? "void" :
                op == UNARY_OP_DELETE ? "delete" : op == UNARY_OP_YIELD || op == UNARY_OP_YIELD_STAR ? "yield" :
                op == UNARY_OP_RIGHT_BANG ? "non-null assertion" : op == UNARY_OP_LEFT_BANG ? "!" : "unary");
            shape_sub(s, "operand", &e->unary_op->expr);
            return true;
        }
        case EXPR_PROPERTY: {
            PropertyExpr const* p = e->property;
            bool dot = p->form != PROPERTY_EXPR_BRACKET;
            shape_push(s, dot ? (p->optional_chain ? "optional property access" : "property access")
                              : (p->optional_chain ? "optional element access" : "element access"));
            if (shape_sub(s, "object", &p->expr)) return true;
            if (dot) shape_label_if(s, "name", property_name(p));
            else shape_sub(s, "index", &p->index);
            return true;
        }
        case EXPR_GENERIC_PARAMS:
            shape_push(s, "explicit type arguments");
            ignore (shape_sub(s, nullptr, &e->generic_params->expr) || (shape_push(s, "type argument"), true));
            return true;
        case EXPR_AS_TYPE: {
            AsTypeExpr const* a = e->as_type;
            shape_push(s, a->is_satisfies ? "satisfies" : a->angle_bracket_syntax ? "<T> assertion" : a->is_jsdoc ? "JSDoc cast" : "as");
            ignore (shape_sub(s, "operand", &a->expr) || (shape_push(s, "asserted type"), true));
            return true;
        }
        case EXPR_TERNARY:
            shape_push(s, "conditional");
            ignore (shape_sub(s, "condition", &e->ternary->condition) || shape_sub(s, "then", &e->ternary->yes) ||
                shape_sub(s, "else", &e->ternary->no));
            return true;
        case EXPR_CALL: {
            CallExpr const* c = e->call;
            shape_push(s, c->uses_new ? "new" : c->optional_chain ? "optional call" : "call");
            if (shape_sub(s, "callee", &c->callable)) return true;
            u32 n = 0;
            for_each (a, vec_expr, &c->args) {
                if (shape_sub(s, shape_argument_labels[n < 4 ? n : 4], a)) return true;
                n++;
            }
            return true;
        }
        case EXPR_FUNCTION:
            shape_push(s, function_impl_is_arrow(e->function) ? "arrow function" : "function expression");
            shape_function(s, e->function);
            return true;
        case EXPR_OBJECT:
            shape_push(s, "object literal");
            for_each (i, vec_object_expr_item, &e->object) {
                if (!shape_contains(s, i->span)) continue;
                if (shape_sub(s, "computed key", i->computed_key)) return true;
                switch (i->tag) {
                    case OBJECT_ITEM_PROPERTY:
                        if (shape_label_if(s, "property name", i->name_span)) return true;
                        shape_sub(s, "property value", &i->value);
                        return true;
                    case OBJECT_ITEM_SHORTHAND: shape_push(s, "shorthand property"); return true;
                    case OBJECT_ITEM_METHOD: shape_push(s, "method"); break;
                    case OBJECT_ITEM_GETTER: shape_push(s, "getter"); break;
                    case OBJECT_ITEM_SETTER: shape_push(s, "setter"); break;
                    case OBJECT_ITEM_SPREAD: shape_sub(s, "spread", &i->value); return true;
                }
                if (shape_label_if(s, "member name", i->name_span)) return true;
                if (i->value.tag == EXPR_FUNCTION) shape_function(s, i->value.function);
                return true;
            }
            return true;
        case EXPR_ARRAY:
            shape_push(s, "array literal");
            for_each (i, vec_expr, &e->array) if (shape_sub(s, "element", i)) return true;
            return true;
        case EXPR_CLASS_DECL:
            shape_push(s, "class expression");
            shape_class(s, e->class_decl);
            return true;
        case EXPR_SPREAD:
            shape_push(s, "spread");
            shape_sub(s, nullptr, e->spread);
            return true;
        case EXPR_TEMPLATE:
            shape_push(s, e->template_->tag ? "tagged template" : "template literal");
            if (shape_sub(s, "tag", e->template_->tag)) return true;
            for_each (i, vec_expr, &e->template_->substitutions) if (shape_sub(s, "substitution", i)) return true;
            return true;
        case EXPR_JSX_ELEMENT: case EXPR_JSX_FRAGMENT: {
            JsxElement const* el = e->jsx;
            shape_push(s, e->tag == EXPR_JSX_FRAGMENT ? "JSX fragment" : "JSX element");
            if (shape_sub(s, "tag name", &el->tag_name) || shape_sub(s, nullptr, &el->attributes)) return true;
            for_each (i, vec_jsx_child, &el->children) {
                if (!shape_contains(s, i->span)) continue;
                shape_push(s, i->spread ? "JSX spread child" : i->tag == JSX_CHILD_TEXT ? "JSX text" : "JSX child");
                shape_sub(s, nullptr, &i->expr);
                return true;
            }
            return true;
        }
        case EXPR_JSX_ATTRIBUTES:
            shape_push(s, "JSX attributes");
            for_each (i, vec_jsx_attribute, &e->jsx_attributes) {
                if (!shape_contains(s, i->span)) continue;
                if (i->tag == JSX_ATTRIBUTE_SPREAD) {
                    shape_push(s, "JSX spread attribute");
                    shape_sub(s, nullptr, &i->value);
                    return true;
                }
                shape_push(s, "JSX attribute");
                ignore (shape_label_if(s, "attribute name", i->name) || shape_sub(s, "attribute value", &i->value));
                return true;
            }
            return true;
    }
    return true;
}

static char const* shape_statement_label(Statement const* st) {
    switch (st->tag) {
        case STATEMENT_NOOP: return "empty statement";
        case STATEMENT_SOURCE_DIRECTIVE: return "triple-slash directive";
        case STATEMENT_BLOCK: return "block";
        case STATEMENT_IF_ELSE: return "if";
        case STATEMENT_FOR_LOOP: return "for";
        case STATEMENT_FOR_EACH_LOOP: return "for-in/of";
        case STATEMENT_SWITCH: return "switch";
        case STATEMENT_TRY_CATCH: return "try";
        case STATEMENT_LABELED: return "labeled statement";
        case STATEMENT_WITH: return "with";
        case STATEMENT_BREAK: return "break";
        case STATEMENT_CONTINUE: return "continue";
        case STATEMENT_EXPR: return "expression statement";
        case STATEMENT_EXPORT_DEFAULT_EXPR: return st->export_equals ? "export =" : "export default";
        case STATEMENT_RETURN_EXPR: return "return";
        case STATEMENT_THROW_EXPR: return "throw";
        case STATEMENT_IMPORT_EXPORT: return "import/export declaration";
        case STATEMENT_DECL_VARIABLE: {
            static char const* const keywords[] = { "var", "let", "const", "using", "await using" };
            return st->decl.variable.keyword < (u32)c_countof(keywords) ? keywords[st->decl.variable.keyword] : "variable";
        }
        case STATEMENT_DECL_FUNCTION: return "function declaration";
        case STATEMENT_DECL_ENUM: return "enum declaration";
        case STATEMENT_DECL_CLASS: return "class declaration";
        case STATEMENT_DECL_TYPE: return "type alias declaration";
        case STATEMENT_DECL_INTERFACE: return "interface declaration";
        case STATEMENT_DECL_NAMESPACE: return "namespace declaration";
    }
    return "statement";
}

static bool shape_statement(Shape* s, Statement const* st) {
    if (!shape_contains(s, st->span)) return false;
    shape_push(s, shape_statement_label(st));
    switch (st->tag) {
        case STATEMENT_BLOCK: shape_statements(s, &st->block.statements); break;
        case STATEMENT_IF_ELSE:
            for_each (i, vec_expr, &st->if_else.conditions) if (shape_sub(s, "condition", i)) return true;
            shape_statements(s, &st->if_else.statements);
            break;
        case STATEMENT_FOR_LOOP:
            ignore (shape_statement(s, &st->for_loop->initialization) || shape_sub(s, "condition", &st->for_loop->condition) ||
                shape_sub(s, "afterthought", &st->for_loop->afterthought) || shape_statement(s, &st->for_loop->body));
            break;
        case STATEMENT_FOR_EACH_LOOP: {
            ForEachLoopStatement const* loop = st->for_each_loop;
            if (loop->variable.tag == FOR_EACH_LOOP_STATEMENT) {
                for_each (i, vec_initialized_binding, &loop->variable.statement.initializers) {
                    if (shape_initialized(s, i)) return true;
                }
            } else if (shape_sub(s, "loop variable", &loop->variable.expr)) return true;
            ignore (shape_sub(s, "iterated", &loop->iterable) || shape_statement(s, &loop->body));
            break;
        }
        case STATEMENT_SWITCH:
            if (shape_sub(s, "discriminant", &st->switch_->needle)) return true;
            for_each (i, vec_switch_case, &st->switch_->cases) {
                if (i->is_pattern && shape_sub(s, "case label", &i->pattern)) return true;
                if (shape_statements(s, &i->statements)) return true;
            }
            break;
        case STATEMENT_TRY_CATCH: {
            TryCatchStatement const* t = st->try_catch;
            u32 mark = s->depth;
            shape_push(s, "try block");
            if (shape_statements(s, &t->try_block.statements)) return true;
            s->depth = mark;
            shape_push(s, "catch");
            if (shape_binding(s, &t->catch_var) || shape_statements(s, &t->catch_block.statements)) return true;
            s->depth = mark;
            shape_push(s, "finally");
            if (shape_statements(s, &t->finally_block.statements)) return true;
            s->depth = mark;
            break;
        }
        case STATEMENT_LABELED: shape_statement(s, st->labeled.body); break;
        case STATEMENT_WITH: ignore (shape_sub(s, "object", &st->with->object) || shape_statement(s, &st->with->body)); break;
        case STATEMENT_EXPR: case STATEMENT_EXPORT_DEFAULT_EXPR: case STATEMENT_RETURN_EXPR: case STATEMENT_THROW_EXPR:
            shape_sub(s, nullptr, &st->expr);
            break;
        case STATEMENT_DECL_VARIABLE:
            for_each (i, vec_initialized_binding, &st->decl.variable.initializers) if (shape_initialized(s, i)) return true;
            break;
        case STATEMENT_DECL_FUNCTION: shape_function(s, st->decl.function.impl); break;
        case STATEMENT_DECL_ENUM:
            for_each (i, vec_enum_case, &st->decl.enum_->cases) if (shape_sub(s, "member value", &i->value)) return true;
            break;
        case STATEMENT_DECL_CLASS: shape_class(s, st->decl.class); break;
        case STATEMENT_DECL_NAMESPACE: shape_statements(s, &st->decl.namespace->contents.statements); break;
        default: break;
    }
    return true;
}

void program_print_node_shapes(Program* program, char const* positions_path) {
    FILE* in = fopen(positions_path, "r");
    if (!in) {
        fprintf(stderr, "Can't read %s\n", positions_path);
        return;
    }
    char path[4096], last_path[4096] = "";
    unsigned line, col;
    SourceFile const* file = nullptr;
    while (fscanf(in, "%4095[^\t]\t%u\t%u\n", path, &line, &col) == 3) {
        // (The positions come sorted by file: find_file() compares real paths when the names differ)
        if (strcmp(path, last_path) != 0) {
            file = find_file(program, csview_from(path));
            strcpy(last_path, path);
        }
        printf("%s:%u:%u\t", path, line, col);
        i64 at = file ? utf16_offset(file, line, col) : -1;
        if (at < 0) {
            printf("%s\n", file ? "(past the line's end)" : "(not in the program)");
            continue;
        }
        Shape s = { .pos = cstr_str(&file->contents) + at };
        shape_statements(&s, &file->ast.statements);
        if (!s.depth) printf("(between statements)");
        for (u32 i = 0; i < s.depth && i < (u32)c_countof(s.labels); i++) printf("%s%s", i ? " > " : "", s.labels[i]);
        if (s.depth > (u32)c_countof(s.labels)) printf(" > ...");
        printf("\n");
    }
    fclose(in);
}
