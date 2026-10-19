#include "flow.h"
#include "expr.h"
#include "scope.h"
#include "statement.h"
#include "source_file.h"
#include "function_impl.h"
#include "source_file.h"
#include "binding.h"
#include "lazy_type.h"

// (Referenced and shared from the start, never written: every walk of every program uses it, maybe on threads of their
// own)
FlowNode flow_unreachable = { .flags = FLOW_UNREACHABLE | FLOW_REFERENCED | FLOW_SHARED };
// Unreachable, and TS7027 was reported (or deliberately not) for the code that follows
FlowNode flow_reported_unreachable = { .flags = FLOW_UNREACHABLE };

bool flow_dump_enabled(void) {
    return debug_env.dump_flow;
}

static void dump_record(FlowBuilder* me, FlowNode const* node, Span reference) {
    if (my dump_count == my dump_cap) {
        my dump_cap = my dump_cap ? my dump_cap * 2 : 32;
        my dump = realloc(my dump, my dump_cap * sizeof(*my dump));
    }
    my dump[my dump_count++] = (FlowDumpEntry){ node, reference };
}

void flow_builder_init(FlowBuilder* me, FlowBuilder const* outer, Scope* scope, FunctionImpl const* function, ErrorCtx* errs) {
    *me = (FlowBuilder){
        .arena = binder_arena(scope->binder),
        .errs = errs,
        .function = function,
        .container = outer ? outer->container : (void const*)scope->file,
        .ambient = scope->ambient,
        .depth = outer ? outer->depth + 1 : 0,
        .contextual_depth = outer ? outer->contextual_depth : 0,
        // A class field initializer is still inside the statements of the function around it
        .statements = outer && !function ? outer->statements : nullptr,
        .container_start = outer ? outer->container_start : nullptr,
        .within_unreachable = outer && outer->within_unreachable,
        .file = outer ? outer->file : scope->file,
    };
    my next_id = outer ? outer->next_id : &my id_counter;
    my current = &flow_unreachable; // Until the container gets its start node
}

// An alias named as a whole by a narrowing node may be narrowed (Symbol.flow_narrowed; flow_narrowed_this as the object
// of a called method). A name not resolved yet could be one: the file's imports all may then
static void flow_names_add(FlowBuilder* me, Expr const* identifier, bool this_object) {
    Symbol* symbol = (Symbol*)identifier->identifier.symbol;
    if (symbol) {
        atomic_bool* flag = this_object ? &symbol->flow_narrowed_this : &symbol->flow_narrowed;
        if ((symbol->flags & SYMBOL_ALIAS) && !atomic_load_explicit(flag, memory_order_relaxed)) {
            atomic_store_explicit(flag, true, memory_order_relaxed);
        }
    } else if (my file) {
        atomic_store_explicit(&my file->flow_unresolved_names, true, memory_order_relaxed);
    }
}

// The identifiers a narrowing node's expression may narrow as a whole, the way an import (a class, function, enum or
// namespace: never a union, so no discriminant narrows it through a property) can be narrowed: an operand of a
// condition, `in`, `instanceof`, `typeof` or an equality, an argument of a call (a predicate or an assertion), the
// object of a called method (a `this` predicate). Not property chains' roots nor bare callees: `node.kind ===
// SyntaxKind.X` and `isIdentifier(node)` name neither `SyntaxKind` nor `isIdentifier` as a whole
static Expr const* skip_generic_params(Expr const* me);
static void flow_names_of(FlowBuilder* me, Expr const* expr, u32 depth, bool this_object) {
    if (depth > 64) return;
    switch (expr->tag) {
        case EXPR_IDENTIFIER:
            if (!expr->identifier.keyword) flow_names_add(me, expr, this_object);
            return;
        case EXPR_GENERIC_PARAMS: flow_names_of(me, &expr->generic_params->expr, depth + 1, this_object); return;
        case EXPR_AS_TYPE: flow_names_of(me, &expr->as_type->expr, depth + 1, this_object); return;
        case EXPR_UNARY_OP: flow_names_of(me, &expr->unary_op->expr, depth + 1, this_object); return;
        case EXPR_BINARY_OP:
            switch (expr->binary_op->op) {
                case BINARY_OP_INSTANCEOF:
                case BINARY_OP_EQUAL:
                case BINARY_OP_OR_OR_EQUAL:
                case BINARY_OP_AND_AND_EQUAL:
                case BINARY_OP_EH_EH_EQUAL:
                    flow_names_of(me, &expr->binary_op->left, depth + 1, this_object);
                    return;
                case BINARY_OP_IN:
                case BINARY_OP_COMMA:
                    flow_names_of(me, &expr->binary_op->right, depth + 1, this_object);
                    return;
                default:
                    flow_names_of(me, &expr->binary_op->left, depth + 1, this_object);
                    flow_names_of(me, &expr->binary_op->right, depth + 1, this_object);
                    return;
            }
        case EXPR_TERNARY:
            flow_names_of(me, &expr->ternary->condition, depth + 1, this_object);
            flow_names_of(me, &expr->ternary->yes, depth + 1, this_object);
            flow_names_of(me, &expr->ternary->no, depth + 1, this_object);
            return;
        case EXPR_CALL: {
            for_each (i, vec_expr, &expr->call->args) flow_names_of(me, i, depth + 1, false);
            Expr const* callee = skip_generic_params(&expr->call->callable);
            if (callee->tag == EXPR_PROPERTY) flow_names_of(me, &callee->property->expr, depth + 1, true);
            return;
        }
        default:
            return;
    }
}

static FlowNode* new_node(FlowBuilder* me, u32 flags, FlowNode* antecedent, void const* node) {
    FlowNode* ret = arena_new(my arena, FlowNode);
    ret->flags = flags;
    ret->id = ++*my next_id;
    ret->antecedent = antecedent;
    ret->node = node;
    if (flow_dump_enabled()) dump_record(me, ret, span_null);
    return ret;
}

static void set_referenced(FlowNode* flow) {
    if (flow == &flow_unreachable) return;
    // On first reference the node is referenced, from then on it is shared
    flow->flags |= flow->flags & FLOW_REFERENCED ? FLOW_SHARED : FLOW_REFERENCED;
}

FlowNode* flow_new_start(FlowBuilder* me, FunctionImpl const* function, bool continues_outside) {
    my start = new_node(me, FLOW_START | (continues_outside ? FLOW_CONTINUES_OUTSIDE : 0), nullptr, function);
    my container_start = my start;
    return my start;
}

FlowNode* flow_new_branch_label(FlowBuilder* me) {
    return new_node(me, FLOW_BRANCH_LABEL, nullptr, nullptr);
}

FlowNode* flow_new_loop_label(FlowBuilder* me) {
    return new_node(me, FLOW_LOOP_LABEL, nullptr, nullptr);
}

static void list_push(FlowBuilder* me, FlowList** list, FlowNode* item) {
    FlowList* old = *list;
    if (!old || old->count == old->cap) {
        u32 cap = old ? old->cap * 2 : 2;
        FlowList* grown = arena_alloc(my arena, sizeof(FlowList) + cap * sizeof(FlowNode*));
        grown->cap = cap;
        if (old) {
            grown->count = old->count;
            memcpy(grown->items, old->items, old->count * sizeof(FlowNode*));
        }
        *list = grown;
    }
    (*list)->items[(*list)->count++] = item;
}

void flow_add_antecedent(FlowBuilder* me, FlowNode* label, FlowNode* antecedent) {
    if (flow_is_unreachable(antecedent)) return;
    if (label->antecedents) {
        for (u32 i = 0; i < label->antecedents->count; i++) {
            if (label->antecedents->items[i] == antecedent) return;
        }
    }
    list_push(me, &label->antecedents, antecedent);
    set_referenced(antecedent);
}

FlowNode* flow_finish_label(FlowNode* label) {
    if (!label->antecedents) return &flow_unreachable;
    if (label->antecedents->count == 1) return label->antecedents->items[0];
    return label;
}

static bool expr_is_boolean_literal(Expr const* me, bool value) {
    return my tag == EXPR_LITERAL && my literal.tag == VALUE_BOOLEAN && my literal.boolean == value;
}

FlowNode* flow_new_condition(FlowBuilder* me, u32 flag, FlowNode* antecedent, Expr const* expr) {
    if (flow_is_unreachable(antecedent)) return antecedent;
    if (!expr || expr->tag == EXPR_IMPLICIT_ANY) { // `for (;;)`
        return flag & FLOW_TRUE_CONDITION ? antecedent : &flow_unreachable;
    }
    if (expr_is_boolean_literal(expr, flag & FLOW_TRUE_CONDITION ? false : true)) return &flow_unreachable;
    if (!expr_is_narrowing(expr)) return antecedent;
    set_referenced(antecedent);
    flow_names_of(me, expr, 0, false);
    return new_node(me, flag, antecedent, expr);
}

FlowNode* flow_new_switch_clause(FlowBuilder* me, FlowNode* antecedent, SwitchStatement const* switch_, u32 start, u32 end) {
    set_referenced(antecedent);
    FlowSwitchClause* clause = arena_new(my arena, FlowSwitchClause);
    *clause = (FlowSwitchClause){ switch_, start, end };
    flow_names_of(me, &switch_->needle, 0, false);
    for (u32 i = start; i < end && i < (u32)vec_switch_case_size(&switch_->cases); i++) {
        SwitchCase const* c = vec_switch_case_at(&switch_->cases, i);
        if (c->is_pattern) flow_names_of(me, &c->pattern, 0, false);
    }
    return new_node(me, FLOW_SWITCH_CLAUSE, antecedent, clause);
}

FlowNode* flow_new_call(FlowBuilder* me, FlowNode* antecedent, Expr const* call) {
    if (flow_is_unreachable(antecedent)) return antecedent;
    set_referenced(antecedent);
    my has_flow_effects = true;
    flow_names_of(me, call, 0, false);
    return new_node(me, FLOW_CALL, antecedent, call);
}

FlowNode* flow_new_reduce_label(FlowBuilder* me, FlowNode* target, FlowList* antecedents, FlowNode* antecedent) {
    FlowReduceLabel* reduce = arena_new(my arena, FlowReduceLabel);
    *reduce = (FlowReduceLabel){ target, antecedents };
    return new_node(me, FLOW_REDUCE_LABEL, antecedent, reduce);
}

void flow_assign(FlowBuilder* me, FlowAssign assign) {
    // (Unreachable code makes no flow nodes: tsc's bindChildren skips the flow binding while currentFlow is
    // unreachableFlow, so a loop's incrementor after a `break` adds nothing to the loop label)
    if (my dead || flow_is_unreachable(my current)) return;
    FlowAssign* node = arena_new(my arena, FlowAssign);
    *node = assign;
    set_referenced(my current);
    my has_flow_effects = true;
    my current = new_node(me, FLOW_ASSIGNMENT, my current, node);
    if (my exception_target) flow_add_antecedent(me, my exception_target, my current);
}

FlowList* flow_list_concat(FlowBuilder* me, FlowList const* a, FlowList const* b, FlowList const* c) {
    FlowList* ret = nullptr;
    FlowList const* lists[] = { a, b, c };
    for (u32 l = 0; l < 3; l++) {
        if (!lists[l]) continue;
        for (u32 i = 0; i < lists[l]->count; i++) {
            list_push(me, &ret, lists[l]->items[i]);
        }
    }
    return ret;
}

// Syntactic questions

static Expr const* skip_generic_params(Expr const* me) {
    while (my tag == EXPR_GENERIC_PARAMS) me = &my generic_params->expr;
    return me;
}

// The expression an optional chain continues from, null if `me` can't be part of one
static Expr const* chain_object(Expr const* me, bool* is_root) {
    switch (my tag) {
        case EXPR_PROPERTY:
            *is_root = my property->optional_chain;
            return &my property->expr;
        case EXPR_CALL:
            if (my call->uses_new) return nullptr;
            *is_root = my call->optional_chain;
            return skip_generic_params(&my call->callable);
        case EXPR_UNARY_OP:
            if (my unary_op->op != UNARY_OP_RIGHT_BANG) return nullptr;
            *is_root = false;
            return &my unary_op->expr;
        default:
            return nullptr;
    }
}

bool expr_is_optional_chain_root(Expr const* me) {
    bool is_root = false;
    return chain_object(me, &is_root) && is_root;
}

bool expr_is_optional_chain(Expr const* me) {
    me = skip_generic_params(me);
    while (true) {
        bool is_root = false;
        Expr const* object = chain_object(me, &is_root);
        if (!object) return false;
        if (is_root) return true;
        if (expr_is_perenthisized(object)) return false;
        me = object;
    }
}

bool expr_is_entity_name(Expr const* me) {
    // (`(a).b` isn't one either)
    while (my tag == EXPR_PROPERTY && my property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(me)) {
        // (`a.#x` isn't one: tsc's isPropertyAccessEntityNameExpression wants an identifier)
        Span name = property_name(my property);
        if (name.size && name.buf[0] == '#') return false;
        me = &my property->expr;
    }
    return my tag == EXPR_IDENTIFIER && !my identifier.keyword && !expr_is_perenthisized(me);
}

bool expr_is_dotted_name(Expr const* me) {
    while (my tag == EXPR_PROPERTY && my property->form == PROPERTY_EXPR_DOT && !my property->optional_chain) {
        me = &my property->expr;
    }
    return my tag == EXPR_IDENTIFIER; // `this`, `super` and `import.meta` included
}

bool expr_is_narrowable_reference(Expr const* me) {
    while (true) {
        switch (my tag) {
            case EXPR_IDENTIFIER:
                return true;
            case EXPR_PROPERTY:
                if (my property->optional_chain) return false; // Only as part of containsNarrowableReference
                if (my property->form == PROPERTY_EXPR_BRACKET) {
                    Expr const* key = &my property->index;
                    bool literal = key->tag == EXPR_LITERAL &&
                        (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER);
                    if (!literal && !expr_is_entity_name(key)) return false;
                }
                me = &my property->expr;
                break;
            case EXPR_UNARY_OP:
                if (my unary_op->op != UNARY_OP_RIGHT_BANG) return false;
                me = &my unary_op->expr;
                break;
            // (A JSDoc cast is tsgo's AsExpression or SatisfiesExpression too: not a narrowable reference)
            case EXPR_BINARY_OP:
                if (my binary_op->op == BINARY_OP_COMMA) {
                    me = &my binary_op->right;
                    break;
                }
                // TODO: tsc wants a left-hand side expression on the left
                return binary_op_is_assignment(my binary_op->op);
            default:
                return false;
        }
    }
}

static bool contains_narrowable_reference(Expr const* me) {
    while (true) {
        if (expr_is_narrowable_reference(me)) return true;
        if (!expr_is_optional_chain(me)) return false;
        bool is_root;
        me = chain_object(me, &is_root);
    }
}

static bool is_narrowable_operand(Expr const* me) {
    while (my tag == EXPR_BINARY_OP) {
        if (my binary_op->op == BINARY_OP_EQUAL) me = &my binary_op->left;
        else if (my binary_op->op == BINARY_OP_COMMA) me = &my binary_op->right;
        else break;
    }
    return contains_narrowable_reference(me);
}

// tsc's getReferenceRoot, from the top: the name `(x = [], x)` and `(x = y)` stand for, if they stand for one
static Expr* reference_root_name(Expr* me) {
    while (true) {
        if (my tag == EXPR_BINARY_OP && my binary_op->op == BINARY_OP_EQUAL) me = &my binary_op->left;
        else if (my tag == EXPR_BINARY_OP && my binary_op->op == BINARY_OP_COMMA) me = &my binary_op->right;
        else break;
    }
    return my tag == EXPR_IDENTIFIER && !my identifier.keyword ? me : nullptr;
}

// `this.x`, a member that may be auto-typed (tsc's autoArrayType of a constructor-declared property)
static bool is_this_property(Expr const* me) {
    return my tag == EXPR_PROPERTY && my property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(me) &&
        my property->expr.tag == EXPR_IDENTIFIER && my property->expr.identifier.keyword == KEYWORD_IDENTIFIER_THIS &&
        !expr_is_perenthisized(&my property->expr);
}

static void mark_evolving_array_target(Expr* object, EvolvingArrayOp op, Expr const* index) {
    if (op == EVOLVING_OP_ACCESS && is_this_property(object)) object->property->evolving_access = true;
    Expr* name = reference_root_name(object);
    if (!name) return;
    name->identifier.evolving_op = op;
    if (op == EVOLVING_OP_ELEMENT_ASSIGNMENT) name->identifier.evolving_index_ = index;
}

// Only a variable declared without a type and with no initializer, null, undefined or `[]` can be auto-typed (tsc's
// autoType, autoArrayType): no other reference needs array mutation nodes (tsc makes them for every narrowable one).
// The variable is the checker's reference candidate: `(x ||= []).push(v)` pushes to x too
static bool may_evolve(Expr const* object) {
    while (true) {
        // (ty/flow.c's reference_candidate(): `x!`, `x satisfies T`, JSDoc casts)
        if (object->tag == EXPR_UNARY_OP && object->unary_op->op == UNARY_OP_RIGHT_BANG) {
            object = &object->unary_op->expr;
            continue;
        }
        if (object->tag == EXPR_AS_TYPE && object->as_type->is_satisfies) {
            object = &object->as_type->expr;
            continue;
        }
        if (object->tag != EXPR_BINARY_OP) break;
        BinaryOp op = object->binary_op->op;
        if (op == BINARY_OP_EQUAL || op == BINARY_OP_OR_OR_EQUAL || op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_EH_EH_EQUAL) {
            object = &object->binary_op->left;
        } else if (op == BINARY_OP_COMMA) {
            object = &object->binary_op->right;
        } else {
            return false;
        }
    }
    if (is_this_property(object)) return true;
    Expr const* name = object->tag == EXPR_IDENTIFIER && !object->identifier.keyword ? object : nullptr;
    Symbol const* symbol = name ? name->identifier.target : nullptr;
    if (!symbol || !(symbol->flags & SYMBOL_VARIABLE) || symbol->is_parameter) return false;
    Binding const* binding = symbol_find_node(symbol, SYMBOL_DECL_VARIABLE);
    if (!binding || binding->has_explicit_type || binding->tag != BINDING_IDENTIFIER || !binding->source ||
        binding->source->tag != LAZY_TYPE_INITIALIZED_BINDING
    ) {
        return false;
    }
    Expr const* value = initialized_binding_value(c_container_of(binding->source, InitializedBinding, lazy_type));
    switch (value->tag) {
        case EXPR_IMPLICIT_ANY: return true;
        case EXPR_LITERAL: return value->literal.tag == VALUE_NULL || value->literal.tag == VALUE_UNDEFINED;
        case EXPR_ARRAY: return !vec_expr_size(&value->array);
        default: return false;
    }
}

static bool is_dot_name(Expr const* access, char const* name) {
    if (access->tag != EXPR_PROPERTY || access->property->form != PROPERTY_EXPR_DOT) return false;
    return property_is_dot_name(access->property, name);
}

// `x.push` and `x.unshift` as a call's arguments read x as an evolving array too (tsc's isEvolvingArrayOperationTarget:
// the property access's parent is a call expression, whichever its place), without mutating it
void flow_call_argument_targets(CallExpr* call) {
    if (call->uses_new) return;
    for_each (i, vec_expr, &call->args) {
        if (!expr_is_perenthisized(i) && (is_dot_name(i, "push") || is_dot_name(i, "unshift"))) {
            mark_evolving_array_target(&i->property->expr, EVOLVING_OP_ACCESS, nullptr);
        }
    }
}

void flow_array_mutation(FlowBuilder* me, Expr* expr) {
    Expr* object;
    if (expr->tag == EXPR_CALL) {
        flow_call_argument_targets(expr->call);
        Expr* callee = &expr->call->callable;
        while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
        if (expr->call->uses_new || !(is_dot_name(callee, "push") || is_dot_name(callee, "unshift"))) return;
        object = &callee->property->expr;
        mark_evolving_array_target(object, EVOLVING_OP_ACCESS, nullptr);
    } else {
        Expr* left = &expr->binary_op->left;
        if (expr->binary_op->op != BINARY_OP_EQUAL || left->tag != EXPR_PROPERTY || left->property->form != PROPERTY_EXPR_BRACKET) return;
        object = &left->property->expr;
        mark_evolving_array_target(object, EVOLVING_OP_ELEMENT_ASSIGNMENT, &left->property->index);
    }
    if (my dead || flow_is_unreachable(my current) || !is_narrowable_operand(object) || !may_evolve(object)) return;
    set_referenced(my current);
    my has_flow_effects = true;
    my current = new_node(me, FLOW_ARRAY_MUTATION, my current, expr);
    if (my exception_target) flow_add_antecedent(me, my exception_target, my current);
}

void flow_length_access(Expr* access) {
    if (is_dot_name(access, "length")) mark_evolving_array_target(&access->property->expr, EVOLVING_OP_ACCESS, nullptr);
}

static bool is_narrowing_typeof_operands(Expr const* a, Expr const* b) {
    return a->tag == EXPR_UNARY_OP && a->unary_op->op == UNARY_OP_TYPEOF && is_narrowable_operand(&a->unary_op->expr) &&
        b->tag == EXPR_LITERAL && b->literal.tag == VALUE_STRING;
}

static bool expr_is_any_boolean_literal(Expr const* me) {
    return my tag == EXPR_LITERAL && my literal.tag == VALUE_BOOLEAN;
}

bool expr_is_narrowing(Expr const* me) {
    switch (my tag) {
        case EXPR_IDENTIFIER:
            return !my identifier.keyword || my identifier.keyword == KEYWORD_IDENTIFIER_THIS;
        case EXPR_PROPERTY:
            return contains_narrowable_reference(me);
        case EXPR_CALL: {
            if (my call->uses_new) return false;
            for_each (i, vec_expr, &my call->args) {
                if (contains_narrowable_reference(i)) return true;
            }
            Expr const* callee = skip_generic_params(&my call->callable);
            return callee->tag == EXPR_PROPERTY && callee->property->form == PROPERTY_EXPR_DOT &&
                contains_narrowable_reference(&callee->property->expr);
        }
        case EXPR_UNARY_OP:
            switch (my unary_op->op) {
                case UNARY_OP_RIGHT_BANG:
                case UNARY_OP_LEFT_BANG:
                case UNARY_OP_TYPEOF:
                    return expr_is_narrowing(&my unary_op->expr);
                default:
                    return false;
            }
        case EXPR_BINARY_OP: {
            Expr const* left = &my binary_op->left;
            Expr const* right = &my binary_op->right;
            switch (my binary_op->op) {
                case BINARY_OP_EQUAL:
                case BINARY_OP_OR_OR_EQUAL:
                case BINARY_OP_AND_AND_EQUAL:
                case BINARY_OP_EH_EH_EQUAL:
                    return contains_narrowable_reference(left);
                case BINARY_OP_DOUBLE_EQUAL:
                case BINARY_OP_NOT_EQUAL:
                case BINARY_OP_TRIPLE_EQUAL:
                case BINARY_OP_TRIPLE_NOT_EQUAL:
                    return is_narrowable_operand(left) || is_narrowable_operand(right) ||
                        is_narrowing_typeof_operands(right, left) || is_narrowing_typeof_operands(left, right) ||
                        (expr_is_any_boolean_literal(right) && expr_is_narrowing(left)) ||
                        (expr_is_any_boolean_literal(left) && expr_is_narrowing(right));
                case BINARY_OP_INSTANCEOF:
                    return is_narrowable_operand(left);
                case BINARY_OP_IN:
                case BINARY_OP_COMMA:
                    return expr_is_narrowing(right);
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

// Dump

static void dump_flags(FlowNode const* node) {
    u32 flags = node->flags;
    char const* name =
        flags & FLOW_START ? "start" :
        flags & FLOW_BRANCH_LABEL ? "branch" :
        flags & FLOW_LOOP_LABEL ? "loop" :
        flags & FLOW_ASSIGNMENT ? "assign" :
        flags & FLOW_TRUE_CONDITION ? "true" :
        flags & FLOW_FALSE_CONDITION ? "false" :
        flags & FLOW_SWITCH_CLAUSE ? "clause" :
        flags & FLOW_CALL ? "call" :
        flags & FLOW_REDUCE_LABEL ? "reduce" : "?";
    print(name);
}

static void dump_id(FlowNode const* node) {
    if (node == &flow_unreachable || node == &flow_reported_unreachable) print("unreachable");
    else print("#", node->id);
}

static void dump_list(FlowList const* list) {
    for (u32 i = 0; list && i < list->count; i++) {
        print(i ? ", " : "");
        dump_id(list->items[i]);
    }
}

// One line of source at most
static csview dump_text(Span span) {
    csview sv = span_sv(span);
    for (isize_t i = 0; i < sv.size; i++) {
        if (sv.buf[i] == '\n' || i == 60) return (csview){ sv.buf, i };
    }
    return sv;
}

static void dump_node(FlowNode const* node) {
    dump_id(node);
    print(" ");
    dump_flags(node);
    if (node->flags & FLOW_LABEL) {
        print(" <- ");
        dump_list(node->antecedents);
    } else if (node->antecedent) {
        print(" <- ");
        dump_id(node->antecedent);
    }
    if (node->flags & FLOW_SHARED) print(" shared");
    if (node->flags & FLOW_NULLISH) print(" nullish");
    if (node->flags & (FLOW_CONDITION | FLOW_CALL)) {
        print("  `", dump_text(expr_outer_span((Expr const*)node->node)), "`");
    } else if (node->flags & FLOW_START) {
        if (node->flags & FLOW_CONTINUES_OUTSIDE) print("  (continues outside)");
    } else if (node->flags & FLOW_SWITCH_CLAUSE) {
        FlowSwitchClause const* clause = node->node;
        print("  clauses [", clause->clause_start, ", ", clause->clause_end, ") of `switch (",
            dump_text(expr_outer_span(&clause->switch_->needle)), ")`");
    } else if (node->flags & FLOW_REDUCE_LABEL) {
        FlowReduceLabel const* reduce = node->node;
        print("  ");
        dump_id(reduce->target);
        print(" reduced to ");
        dump_list(reduce->antecedents);
    } else if (node->flags & FLOW_ASSIGNMENT) {
        FlowAssign const* assign = node->node;
        switch (assign->kind) {
            case FLOW_ASSIGN_DECLARATION: print("  declaration `", dump_text(assign->binding->content_span), "`"); break;
            case FLOW_ASSIGN_EXPR: print("  `", dump_text(expr_outer_span(assign->assignment)), "`"); break;
            case FLOW_ASSIGN_UPDATE: print("  update `", dump_text(expr_outer_span(assign->assignment)), "`"); break;
            case FLOW_ASSIGN_DELETE: print("  `", dump_text(expr_outer_span(assign->assignment)), "`"); break;
            case FLOW_ASSIGN_FOR_EACH: print("  for-each `", dump_text(expr_outer_span(assign->target)), "`"); break;
            case FLOW_ASSIGN_PATTERN: {
                print("  `", dump_text(expr_outer_span(assign->target)), "` = value");
                // Innermost first
                for (FlowPatternPath const* path = assign->path; path; path = path->parent) {
                    switch (path->kind) {
                        case FLOW_PATH_ELEMENT: print(" <- [", path->index, "]"); break;
                        case FLOW_PATH_PROPERTY: print(" <- .", dump_text(path->item->name_span)); break;
                        case FLOW_PATH_REST: print(" <- ...rest(", path->index, ")"); break;
                    }
                    if (path->default_value) print(" ?? `", dump_text(expr_outer_span(path->default_value)), "`");
                }
            }   break;
        }
    }
    println("");
}

PropertyContext const* flow_builder_property_context(FlowBuilder* me, PropertyContext const* context) {
    PropertyContext const* last = my property_context;
    if (last && last->enclosing_class == context->enclosing_class && last->in_constructor_of == context->in_constructor_of &&
        last->this_parameter == context->this_parameter && last->this_function == context->this_function &&
        last->containing_function == context->containing_function && last->flow_container == context->flow_container &&
        last->initializer_field == context->initializer_field && last->initializer_context == context->initializer_context
    ) {
        return last;
    }
    PropertyContext* ret = arena_new(my arena, PropertyContext);
    *ret = *context;
    my property_context = ret;
    return ret;
}

void flow_builder_reference_dump(FlowBuilder* me, Span name) {
    if (flow_dump_enabled() && span_valid(name)) dump_record(me, my dead ? &flow_unreachable : my current, name);
}

void flow_builder_finish(FlowBuilder* me, csview name, Span span) {
    if (!flow_dump_enabled()) return;
    bool is_lib = span_valid(span) && span_source(span) && span_source(span)->is_stdlib;
    if (my dump_count <= 1 || is_lib) {
        free(my dump);
        my dump = nullptr;
        return;
    }
    println("flow of ", name, " `", dump_text(span), "`");
    for (u32 i = 0; i < my dump_count; i++) {
        print("  ");
        if (span_valid(my dump[i].reference)) {
            print("    `", dump_text(my dump[i].reference), "` at ");
            dump_id(my dump[i].node);
            println("");
        } else {
            dump_node(my dump[i].node);
        }
    }
    print("  end: ");
    dump_id(my current);
    println("");
    free(my dump);
    my dump = nullptr;
}

// Shared nodes already answered by flow_is_post_super() (a chain of `if`s is a chain of diamonds)
typedef struct PostSuperMemo {
    FlowNode const** nodes;
    bool* results;
    u32 len, cap;
    FlowReduceLabel const* reduce; // A reduce label in effect: its target has these antecedents instead
} PostSuperMemo;

static bool post_super(FlowNode const* flow, PostSuperMemo* memo, u32 depth) {
    if (depth > 1000) return true;
    while (true) {
        u32 flags = flow->flags;
        if (flags & FLOW_SHARED) {
            for (u32 i = 0; i < memo->len; i++) if (memo->nodes[i] == flow) return memo->results[i];
        }
        FlowNode const* at = flow;
        bool result;
        if (flags & (FLOW_ASSIGNMENT | FLOW_CONDITION | FLOW_SWITCH_CLAUSE | FLOW_ARRAY_MUTATION)) {
            flow = flow->antecedent;
            continue;
        } else if (flags & FLOW_CALL) {
            Expr const* call = flow->node;
            Expr const* callee = &call->call->callable;
            if (callee->tag == EXPR_IDENTIFIER && callee->identifier.keyword == KEYWORD_IDENTIFIER_SUPER) {
                result = true;
            } else {
                flow = flow->antecedent;
                continue;
            }
        } else if (flags & FLOW_BRANCH_LABEL) {
            FlowList const* antecedents = memo->reduce && memo->reduce->target == flow ? memo->reduce->antecedents : flow->antecedents;
            result = true;
            for (u32 i = 0; antecedents && i < antecedents->count && result; i++) {
                result = post_super(antecedents->items[i], memo, depth + 1);
            }
        } else if (flags & FLOW_LOOP_LABEL) {
            // The way into the loop decides
            if (!flow->antecedents || !flow->antecedents->count) return false;
            flow = flow->antecedents->items[0];
            continue;
        } else if (flags & FLOW_REDUCE_LABEL) {
            FlowReduceLabel const* saved = memo->reduce;
            memo->reduce = flow->node;
            result = post_super(flow->antecedent, memo, depth + 1);
            memo->reduce = saved;
        } else {
            // The start. Unreachable code counts as after the call, to keep quiet
            result = (flags & FLOW_UNREACHABLE) != 0;
        }
        if ((at->flags & FLOW_SHARED) && !memo->reduce) {
            if (memo->len == memo->cap) {
                memo->cap = memo->cap ? memo->cap * 2 : 16;
                memo->nodes = realloc(memo->nodes, sizeof(FlowNode const*) * memo->cap);
                memo->results = realloc(memo->results, sizeof(bool) * memo->cap);
                panic_if(!memo->nodes || !memo->results, "out of memory");
            }
            memo->nodes[memo->len] = at;
            memo->results[memo->len++] = result;
        }
        return result;
    }
}

bool flow_is_post_super(FlowNode const* flow) {
    PostSuperMemo memo = {0};
    bool ret = post_super(flow, &memo, 0);
    free(memo.nodes);
    free(memo.results);
    return ret;
}
