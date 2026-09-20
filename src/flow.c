#include "flow.h"
#include "expr.h"
#include "scope.h"
#include "statement.h"
#include "function_impl.h"
#include "source_file.h"

FlowNode flow_unreachable = { .flags = FLOW_UNREACHABLE };
// Unreachable, and TS7027 was reported (or deliberately not) for the code that follows
FlowNode flow_reported_unreachable = { .flags = FLOW_UNREACHABLE };

bool flow_dump_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("TTTT_DUMP_FLOW") != nullptr;
    return enabled;
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
        .arena = &scope->binder->arena,
        .errs = errs,
        .function = function,
        .ambient = scope->ambient,
        .depth = outer ? outer->depth + 1 : 0,
        // A class field initializer is still inside the statements of the function around it
        .statements = outer && !function ? outer->statements : nullptr,
    };
    my next_id = outer ? outer->next_id : &my id_counter;
    my current = &flow_unreachable; // Until the container gets its start node
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
    // On first reference the node is referenced, from then on it is shared
    flow->flags |= flow->flags & FLOW_REFERENCED ? FLOW_SHARED : FLOW_REFERENCED;
}

FlowNode* flow_new_start(FlowBuilder* me, FunctionImpl const* function) {
    return new_node(me, FLOW_START, nullptr, function);
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
    return new_node(me, flag, antecedent, expr);
}

FlowNode* flow_new_switch_clause(FlowBuilder* me, FlowNode* antecedent, SwitchStatement const* switch_, u32 start, u32 end) {
    set_referenced(antecedent);
    FlowSwitchClause* clause = arena_new(my arena, FlowSwitchClause);
    *clause = (FlowSwitchClause){ switch_, start, end };
    return new_node(me, FLOW_SWITCH_CLAUSE, antecedent, clause);
}

FlowNode* flow_new_call(FlowBuilder* me, FlowNode* antecedent, Expr const* call) {
    set_referenced(antecedent);
    my has_flow_effects = true;
    return new_node(me, FLOW_CALL, antecedent, call);
}

FlowNode* flow_new_reduce_label(FlowBuilder* me, FlowNode* target, FlowList* antecedents, FlowNode* antecedent) {
    FlowReduceLabel* reduce = arena_new(my arena, FlowReduceLabel);
    *reduce = (FlowReduceLabel){ target, antecedents };
    return new_node(me, FLOW_REDUCE_LABEL, antecedent, reduce);
}

void flow_assign(FlowBuilder* me, FlowAssign assign) {
    if (my dead) return;
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

static bool binary_op_is_assignment(BinaryOp op) {
    return op >= BINARY_OP_EQUAL && op <= BINARY_OP_EH_EH_EQUAL;
}

static bool expr_is_entity_name(Expr const* me) {
    while (my tag == EXPR_PROPERTY && my property->form == PROPERTY_EXPR_DOT) me = &my property->expr;
    return my tag == EXPR_IDENTIFIER && !my identifier.keyword;
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
                    Expr const* key = &my property->prop;
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
    csview sv = span.sv;
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
    if (node->flags & (FLOW_CONDITION | FLOW_CALL)) {
        print("  `", dump_text(((Expr const*)node->node)->outer_span), "`");
    } else if (node->flags & FLOW_START) {
        if (node->node) print("  (continues outside)");
    } else if (node->flags & FLOW_SWITCH_CLAUSE) {
        FlowSwitchClause const* clause = node->node;
        print("  clauses [", clause->clause_start, ", ", clause->clause_end, ") of `switch (",
            dump_text(clause->switch_->needle.outer_span), ")`");
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
            case FLOW_ASSIGN_EXPR: print("  `", dump_text(assign->assignment->outer_span), "`"); break;
            case FLOW_ASSIGN_UPDATE: print("  update `", dump_text(assign->assignment->outer_span), "`"); break;
            case FLOW_ASSIGN_DELETE: print("  `", dump_text(assign->assignment->outer_span), "`"); break;
            case FLOW_ASSIGN_FOR_EACH: print("  for-each `", dump_text(assign->target->outer_span), "`"); break;
            case FLOW_ASSIGN_PATTERN: {
                print("  `", dump_text(assign->target->outer_span), "` = value");
                // Innermost first
                for (FlowPatternPath const* path = assign->path; path; path = path->parent) {
                    switch (path->kind) {
                        case FLOW_PATH_ELEMENT: print(" <- [", path->index, "]"); break;
                        case FLOW_PATH_PROPERTY: print(" <- .", dump_text(path->item->name_span)); break;
                        case FLOW_PATH_REST: print(" <- ...rest(", path->index, ")"); break;
                    }
                    if (path->default_value) print(" ?? `", dump_text(path->default_value->outer_span), "`");
                }
            }   break;
        }
    }
    println("");
}

void flow_builder_reference_dump(FlowBuilder* me, Span name) {
    if (flow_dump_enabled() && span_valid(name)) dump_record(me, my dead ? &flow_unreachable : my current, name);
}

void flow_builder_finish(FlowBuilder* me, csview name, Span span) {
    if (!flow_dump_enabled()) return;
    bool is_lib = span_valid(span) && span.source && span.source->is_stdlib;
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
