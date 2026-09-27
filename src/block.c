#include "block.h"
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
        if (hoist && inner->tag != STATEMENT_DECL_VARIABLE) statement_hoist_names(i, scope);
    }
    scope_add_expandos(scope, me);
}

bool block_has_export_declarations(Block const* me) {
    for_each (i, vec_statement, &my statements) {
        if (i->tag == STATEMENT_EXPORT_DEFAULT_EXPR) return true;
        if (i->tag == STATEMENT_IMPORT_EXPORT && span_equals(i->import_export->keyword_span, "export") &&
            !i->import_export->main.export_import
        ) {
            return true;
        }
    }
    return false;
}

void block_lookup_names_as(Block const* me, bool is_block, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    for_each (i, vec_statement, &my statements) {
        flow->statement_list = is_block ? &my statements : nullptr;
        statement_resolve_names(i, names, errs);
    }
}

// A `static {}` block of a class runs where the class is evaluated: part of the flow around it, like an IIFE
void block_lookup_names_inline_container(Block const* me, Scope* scope, ErrorCtx* errs) {
    FlowBuilder* outer = scope->flow;
    FlowBuilder flow;
    flow_builder_init(&flow, outer, scope, nullptr, errs);
    flow.start = outer->start;
    flow.current = outer->current;
    flow.dead = outer->dead;
    flow.has_flow_effects = outer->has_flow_effects;
    // Runs with the class: in a parameter's initializer it's still there (tsc's getIsDeferredContext)
    flow.parameter_initializing = outer->parameter_initializing;
    flow.parameter_function_scope = outer->parameter_function_scope;
    flow.return_target = flow_new_branch_label(&flow);
    scope->flow = &flow;
    for_each (i, vec_statement, &my statements) {
        flow.statement_list = &my statements;
        // Its expression statements run with the class (TS2729), the rest is like any other code
        flow.initializer_context = i->tag == STATEMENT_EXPR ? INITIALIZER_STATIC_BLOCK : INITIALIZER_NONE;
        statement_resolve_names(i, scope, errs);
    }
    flow.initializer_context = INITIALIZER_NONE;
    flow_add_antecedent(&flow, flow.return_target, flow.current);
    flow.current = flow_finish_label(flow.return_target);
    flow_builder_finish(&flow, c_sv("static block"), span_null);
    scope->flow = outer;
    if (!outer->dead) {
        outer->current = flow.current;
        outer->has_flow_effects |= flow.has_flow_effects;
    }
}

void block_lookup_names_container(Block const* me, bool is_block, csview kind, Span name, Scope* scope, ErrorCtx* errs) {
    FlowBuilder* outer = scope->flow;
    FlowBuilder flow;
    flow_builder_init(&flow, outer, scope, nullptr, errs);
    flow.current = flow_new_start(&flow, nullptr, false);
    scope->flow = &flow;
    block_lookup_names_as(me, is_block, scope, errs);
    flow_builder_finish(&flow, kind, name);
    scope->flow = outer; // The scope may outlive the builder
}

void block_resolve_names(Block const* me, Scope* names, ErrorCtx* errs) {
    Scope* scope = scope_new(names, SCOPE_BLOCK);
    block_add_names(me, scope);
    block_lookup_names_as(me, true, scope, errs);
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
    if (!span_valid(first) || !span_valid(last) || first.source != last.source) return span_null;
    return span_make(first.source, span_start_ptr(first), span_end_ptr(last));
}

Span block_first_token(Span at) {
    if (!span_valid(at) || !at.sv.size) return span_null;
    char const* start = at.sv.buf;
    char const* limit = start + at.sv.size;
    char const* end = start + 1;
    unsigned char c = (unsigned char)*start;
    if (isalnum(c) || c == '_' || c == '$' || c >= 0x80) {
        while (end < limit && (isalnum((unsigned char)*end) || *end == '_' || *end == '$' || (unsigned char)*end >= 0x80)) end++;
    } else if (c == '"' || c == '\'' || c == '`') {
        while (end < limit && *end != (char)c) end += *end == '\\' ? 2 : 1;
        if (end < limit) end++;
    } else if ((c == '+' || c == '-') && end < limit && *end == (char)c) {
        end++;
    } else if (c == '.' && limit - start >= 3 && start[1] == '.' && start[2] == '.') {
        end += 2;
    }
    return span_make(at.source, start, end < limit ? end : limit);
}

// tsc's checkGrammarStatementInAmbientContext: in an ambient context only declarations can be statements, which is
// said once per block
static bool is_ambient_disallowed(Statement const* me) {
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
            return span_valid(my span) && my span.sv.size && (my span.sv.buf[0] == ';' || my span.sv.buf[0] == 'd');
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
            if (!is_ambient_disallowed(i) || !i->span.source || i->span.source->has_parse_errors) continue;
            error_ctx_error_d(errs, block_first_token(i->span), 1036, "Statements are not allowed in ambient contexts.");
            ctx.ambient_error_statement = i;
            break;
        }
    }
    ctx.ambient_reported = false;
    for_each (i, vec_statement, &my statements) {
        statement_typecheck(i, ctx, errs);
    }
    if (!ctx.ambient) statements_check_overloads(&my statements, errs);
}
