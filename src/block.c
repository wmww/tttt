#include "block.h"
#include "statement.h"
#include "port_statement.h"
#include "scope.h"
#include "flow.h"
#include "error.h"
#include "ty/ty.h"

void block_drop(Block* me) {
    vec_statement_drop(&my statements);
}

void block_add_names(Block const* me, Scope* scope) {
    bool hoist = scope_is_var_container(scope);
    for_each (i, vec_statement, &my statements) {
        statement_add_names(i, scope);
        // The `var`s nested in it. A variable statement of this block (labeled or not) was just declared
        Statement const* inner = i;
        while (inner->tag == STATEMENT_LABELED) inner = inner->labeled.body;
        if (hoist && inner->tag != STATEMENT_DECL_VARIABLE) statement_hoist_names(i, scope);
    }
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
    flow.return_target = flow_new_branch_label(&flow);
    scope->flow = &flow;
    block_lookup_names_as(me, true, scope, errs);
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

void block_typecheck(Block* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    for_each (i, vec_statement, &my statements) {
        statement_typecheck(i, ctx, errs);
    }
}
