#include "block.h"
#include "unused.h"
#include "unicode_identifiers.h"
#include "statement.h"
#include "port_statement.h"
#include "scope.h"
#include "flow.h"
#include "error.h"
#include "ty/ty.h"
#include "source_file.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void block_add_names(Block const* me, Scope* scope) {
    if (!scope->block) scope->block = me;
    if (scope->kind == SCOPE_BLOCK || scope->kind == SCOPE_FUNCTION || scope->kind == SCOPE_STATIC_BLOCK) {
        ((Block*)me)->scope = scope;
    }
    bool hoist = scope_is_var_container(scope);
    // Function declarations first, like tsc's bindEachFunctionsFirst: that decides which of two conflicting
    // declarations is the first one
    for_each (i, vec_statement, &my statements) {
        if (i->tag == STATEMENT_DECL_FUNCTION) statement_add_names(i, scope);
    }
    for_each (i, vec_statement, &my statements) {
        if (i->tag != STATEMENT_DECL_FUNCTION) statement_add_names(i, scope);
        // The `var`s nested in it. A variable statement of this block (labeled or not) was just declared
        Statement const* inner = i;
        while (inner->tag == STATEMENT_LABELED) inner = inner->labeled.body;
        if (hoist && inner->tag != STATEMENT_DECL_VARIABLE) {
            scope->hoisting = true;
            statement_hoist_names(i, scope);
            scope->hoisting = false;
        }
    }
    scope_add_expandos(scope, me);
}

bool block_has_export_declarations(Block const* me) {
    for_each (i, vec_statement, &my statements) {
        if (i->tag == STATEMENT_EXPORT_DEFAULT_EXPR) return true;
        if (i->tag == STATEMENT_IMPORT_EXPORT && span_equals(i->import_export->keyword_span, "export") &&
            !i->import_export->main.export_import &&
            // (Not `export as namespace X`: tsc's hasExportDeclarations counts export declarations and assignments)
            i->import_export->main.tag != PORT_EXPORT_AS_NAMESPACE
        ) {
            return true;
        }
    }
    return false;
}

void block_lookup_names_as(Block const* me, Scope* names, ErrorCtx* errs) {
    block_lookup_names_range(me, 0, (u32)vec_statement_size(&my statements), names, errs);
}

void block_lookup_names_range(Block const* me, u32 from, u32 to, Scope* names, ErrorCtx* errs) {
    if (from >= to) return;
    FlowBuilder* flow = scope_flow(names);
    for (Statement* i = (Statement*)vec_statement_at(&my statements, from), *end = i + (to - from); i < end; i++) {
        flow->statement_list = &my statements;
        flow->lookup_statements = &my statements;
        // (A JSDoc `{@link x}` before it names x from here: see unused_mark_leading_jsdoc_links())
        if (names->kind != SCOPE_MODULE && names->kind != SCOPE_SCRIPT && span_valid(i->span) && span_source(i->span) &&
            unused_file_has_link_tag(span_source(i->span))
        ) {
            unused_mark_leading_jsdoc_links(names, span_start_ptr(i->span), cstr_str(&span_source(i->span)->contents));
        }
        statement_resolve_names(i, names, errs);
    }
}

// A `static {}` block of a class runs where the class is evaluated: part of the flow around it, like an IIFE
FlowNode const* block_lookup_names_inline_container(Block const* me, Scope* scope, ErrorCtx* errs) {
    FlowBuilder* outer = scope_flow(scope);
    FlowBuilder flow;
    flow_builder_init(&flow, outer, scope, nullptr, errs);
    flow.start = outer->start;
    flow.current = outer->current;
    flow.dead = outer->dead;
    flow.has_flow_effects = outer->has_flow_effects;
    // Runs with the class: in a parameter's initializer it's still there (tsc's getIsDeferredContext), and in the
    // initializer of a variable it is (TS2448)
    flow.initializing = outer->initializing;
    flow.parameter_initializing = outer->parameter_initializing;
    flow.parameter_function_scope = outer->parameter_function_scope;
    flow.return_target = flow_new_branch_label(&flow);
    scope->flow_ = &flow;
    for_each (i, vec_statement, &my statements) {
        flow.statement_list = &my statements;
        flow.lookup_statements = &my statements;
        // It runs with the class (TS2729), nested blocks too, up to a function (tsc 6.0's
        // isInPropertyInitializerOrClassStaticBlock; 5.x only counted its expression statements)
        flow.initializer_context = INITIALIZER_STATIC_BLOCK;
        statement_resolve_names(i, scope, errs);
    }
    flow.initializer_context = INITIALIZER_NONE;
    flow_add_antecedent(&flow, flow.return_target, flow.current);
    flow.current = flow_finish_label(flow.return_target);
    flow_builder_finish(&flow, c_sv("static block"), span_null);
    scope->flow_ = outer;
    if (!outer->dead) {
        outer->current = flow.current;
        outer->has_flow_effects |= flow.has_flow_effects;
    }
    return flow.current;
}

void block_lookup_names_container(Block const* me, csview kind, Span name, Scope* scope, ErrorCtx* errs) {
    FlowBuilder* outer = scope_flow(scope);
    FlowBuilder flow;
    flow_builder_init(&flow, outer, scope, nullptr, errs);
    flow.current = flow_new_start(&flow, nullptr, false);
    flow.container = me;
    scope->flow_ = &flow;
    block_lookup_names_as(me, scope, errs);
    flow_builder_finish(&flow, kind, name);
    scope->flow_ = outer; // The scope may outlive the builder
}

void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs) {
    ScopePendingMark pending_mark = scope_pending_mark(names->binder);
    Scope* scope = scope_new(names, SCOPE_BLOCK);
    block_add_names(me, scope);
    block_lookup_names_as(me, scope, errs);
    if (scope_release(scope, pending_mark) && me->scope == scope) ((Block*)me)->scope = nullptr;
}

void block_collect_return_types(Block const* me, TyBuf* out) {
    for_each (i, vec_statement, &my statements) {
        statement_collect_return_types(i, out);
    }
}

Span block_span(Block const* me) {
    isize count = vec_statement_size(&my statements);
    if (!count) return span_null;
    Span first = vec_statement_at(&my statements, 0)->span;
    Span last = vec_statement_at(&my statements, count - 1)->span;
    if (!span_valid(first) || !span_valid(last) || span_source(first) != span_source(last)) return span_null;
    return span_make_in(first, span_start_ptr(first), span_end_ptr(last));
}

// The length of the non-ASCII identifier character at c (UTF-8), 0 if it's something else (a unicode space: U+0085)
static int unicode_identifier_part_size(char const* c, char const* limit) {
    unsigned char b = (unsigned char)*c;
    int size = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    if (size == 1 || limit - c < size) return 1; // (A stray continuation byte: part of whatever it continues)
    u32 code = size == 2 ? (u32)(b & 0x1F) << 6 | ((unsigned char)c[1] & 0x3F) :
        size == 3 ? (u32)(b & 0x0F) << 12 | (u32)((unsigned char)c[1] & 0x3F) << 6 | ((unsigned char)c[2] & 0x3F) :
        (u32)(b & 0x07) << 18 | (u32)((unsigned char)c[1] & 0x3F) << 12 | (u32)((unsigned char)c[2] & 0x3F) << 6 |
            ((unsigned char)c[3] & 0x3F);
    return unicode_is_identifier_part(code) ? size : 0;
}

Span block_first_token(Span at) {
    if (!span_valid(at) || !at.size) return span_null;
    char const* start = at.buf;
    char const* limit = start + at.size;
    char const* end = start + 1;
    unsigned char c = (unsigned char)*start;
    if (isalnum(c) || c == '_' || c == '$' || c >= 0x80) {
        while (end < limit) {
            unsigned char d = (unsigned char)*end;
            if (isalnum(d) || d == '_' || d == '$') end++;
            else if (d >= 0x80 && unicode_identifier_part_size(end, limit)) end += unicode_identifier_part_size(end, limit);
            else break;
        }
    } else if (c == '"' || c == '\'' || c == '`') {
        while (end < limit && *end != (char)c) end += *end == '\\' ? 2 : 1;
        if (end < limit) end++;
    } else if ((c == '+' || c == '-') && end < limit && *end == (char)c) {
        end++;
    } else if (c == '.' && limit - start >= 3 && start[1] == '.' && start[2] == '.') {
        end += 2;
    }
    return span_make_in(at, start, end < limit ? end : limit);
}

// tsc's checkGrammarStatementInAmbientContext: in an ambient context only declarations can be statements, which is
// said once per block
bool block_statement_is_ambient_disallowed(Statement const* me) {
    switch (my tag) {
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_LABELED:
        case STATEMENT_WITH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            return true;
        case STATEMENT_NOOP: // (`;` or `debugger`, not what the parser made of an error)
            return span_valid(my span) && my span.size && (my span.buf[0] == ';' || my span.buf[0] == 'd');
        default:
            return false;
    }
}

// tsc's checkGrammarTopLevelElementsForRequiredDeclareModifier(): the first top-level declaration of a declaration file
// without `declare`, `export` or `default` (interfaces, type aliases, imports and exports need none)
void block_check_required_declare(Block const* me, ErrorCtx* errs) {
    for_each (i, vec_statement, &my statements) {
        if (!statement_declares_name(i->tag) || i->tag == STATEMENT_DECL_INTERFACE || i->tag == STATEMENT_DECL_TYPE) continue;
        if (span_valid(i->decl.props.declare_keyword) || i->decl.props.exported != DECL_EXPORTED_NO) continue;
        error_ctx_grammar_error_d(errs, block_first_token(i->span), 1046,
            "Top-level declarations in .d.ts files must start with either a 'declare' or 'export' modifier.");
        return;
    }
}

void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    if (ctx.ambient && !ctx.ambient_reported) {
        for_each (i, vec_statement, &my statements) {
            // (A grammar error: not in a file with parse errors, like tsc's grammarErrorOnFirstToken)
            if (!block_statement_is_ambient_disallowed(i) || !span_source(i->span) || span_source(i->span)->has_parse_errors) continue;
            error_ctx_error_d(errs, block_first_token(i->span), 1036, "Statements are not allowed in ambient contexts.");
            ctx.ambient_error_statement = i;
            break;
        }
    }
    ctx.ambient_reported = false;
    statements_typecheck(&my statements, ctx, errs);
    if (!ctx.ambient) statements_check_overloads(&my statements, errs);
}
