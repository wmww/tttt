#include "statement.h"
#include "source_file.h"
#include "ty/ty.h"
#include "port_statement.h"
#include "error.h"
#include "scope.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "program.h"
#include "flow.h"

// Variable initialization

void variable_statement_drop(VariableStatement* me) {
    vec_initialized_binding_drop(&my initializers);
}

void variable_statement_add_names(VariableStatement* me, Scope* scope, bool exported, bool declared) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_add_names(i, scope, (BindingAddNamesProps){
            .abstract_literals = my keyword != VAR_INIT_CONST,
            .exported = exported,
            .function_scoped = my keyword == VAR_INIT_VAR,
            .is_const = my keyword == VAR_INIT_CONST,
            .is_ambient = declared,
        });
    }
}

void variable_statement_resolve_names(VariableStatement* me, Scope* names, ErrorCtx* errs) {
    for_each (i, vec_initialized_binding, &my initializers) {
        initialized_binding_lookup_names(i, names, errs);
    }
}

void variable_statement_typecheck(VariableStatement* me, bool declared, ErrorCtx* errs) {
    for_each (i, vec_initialized_binding, &my initializers) {
        if (my keyword == VAR_INIT_CONST &&
            i->value.tag == EXPR_IMPLICIT_ANY &&
            !declared
        ) {
            error_ctx_error_d(errs, i->binding.content_span, 1155,
                "'const' declarations must be initialized.");
        } else if (declared &&
            i->value.tag != EXPR_IMPLICIT_ANY
        ) {
            if (i->binding.has_explicit_type || my keyword != VAR_INIT_CONST) {
                error_ctx_error_d(errs, i->value.outer_span, 1039,
                    "Initializers are not allowed in ambient contexts.");
            } else {
                switch (i->value.tag) {
                    case EXPR_LITERAL:
                        switch (i->value.literal.tag) {
                            case VALUE_NUMBER:
                            case VALUE_STRING:
                            case VALUE_BOOLEAN:
                                break;

                            default: goto bad_initializer;
                        }
                        break;

                    case EXPR_IDENTIFIER:
                    case EXPR_PROPERTY: // TODO: check that it's actually a reference to a literal enum member
                        break;

                    default: bad_initializer:
                        error_ctx_error_d(errs, i->value.outer_span, 1254,
                            "A 'const' initializer in an ambient context must be a string or numeric literal or literal enum reference.");
                }
            }
        }
        initialized_binding_typecheck(i, errs);
    }
}

// Type declaration

void type_decl_drop(TypeDecl* me) {
    generic_drop(&my generic);
    type_drop(&my type);
}

void type_decl_add_names(TypeDecl* me, Scope* scope, DeclStatementProps props) {
    scope_init_declaration(scope, &my symbol, (DeclProps){
        .name = my name,
        .full = my full_span,
        .exported = props.exported,
        .kind = SYMBOL_DECL_TYPE_ALIAS,
        .node = me,
    });
}

void type_decl_typecheck(TypeDecl* me, ErrorCtx* errs) {
    generic_typecheck(&my generic, errs);
    type_typecheck(&my type, errs);
}

// Namespace declaration

void namespace_decl_drop(NamespaceDecl* me) {
    block_drop(&my contents);
}

// tsc's getModuleInstanceState(), syntactically: a namespace is a value if it contains more than types
// const_enums: whether a `const enum` counts (it does for the symbol, for unreachable code only when it is emitted)
static bool block_is_instantiated(Block const* me, bool const_enums) {
    for_each (i, vec_statement, &my statements) {
        switch (i->tag) {
            case STATEMENT_NOOP:
            case STATEMENT_DECL_TYPE:
            case STATEMENT_DECL_INTERFACE:
                break;

            case STATEMENT_DECL_ENUM:
                if (const_enums || !i->decl.enum_->is_const) return true;
                break;

            case STATEMENT_IMPORT_EXPORT:
                // TODO: an export declaration is as instantiated as what it exports
                if (port_item_is_export(&i->import_export->main) || i->import_export->main.export_import ||
                    span_equals(i->import_export->keyword_span, "export")
                ) {
                    return true;
                }
                break;

            case STATEMENT_DECL_NAMESPACE:
                if (i->decl.namespace.is_global || block_is_instantiated(&i->decl.namespace.contents, const_enums)) return true;
                break;

            default:
                return true;
        }
    }
    return false;
}

void namespace_decl_add_names(NamespaceDecl* me, Scope* scope, DeclStatementProps props) {
    if (my is_global && !props.declared && !scope->ambient) {
        error_ctx_error_d(scope->errs, my name, 2670,
            "Augmentations for the global scope should have 'declare' modifier unless they appear in already ambient context.");
    }

    if (my is_global) {
        my inner_scope = scope_new(scope, SCOPE_GLOBAL_AUGMENTATION);
    } else {
        my inner_scope = scope_new(scope, SCOPE_NAMESPACE);
        if (my is_ambient_module) {
            // In a module it augments the module it names, in a script it declares one. Either way every block goes
            // into the one module symbol
            Symbol* module = nullptr;
            bool augmentation = scope->kind == SCOPE_MODULE;
            if (augmentation && my module_ref->file) {
                module = my module_ref->file->scope ? my module_ref->file->scope->container : nullptr;
            }
            if (!module) {
                // TODO: TS2664 for an augmentation of a module nobody declares
                module = binder_ambient_module(scope->binder, cstr_sv(&my module_ref->path), my name);
            }
            if (my is_shorthand) module->is_shorthand_module = true;
            my symbol = module;
            my inner_scope->container = module;
            my is_augmentation = augmentation;
        } else {
            bool has_value = block_is_instantiated(&my contents, true);
            scope_init_declaration(scope, &my symbol, (DeclProps){
                .name = my name,
                .full = my full_span,
                .exported = props.exported,
                .kind = SYMBOL_DECL_NAMESPACE,
                .node = me,
                .is_instantiated = has_value,
            });
            my inner_scope->container = my symbol;
        }
    }
    my inner_scope->ambient = scope->ambient || props.declared;
    my inner_scope->export_context = my inner_scope->ambient && !block_has_export_declarations(&my contents);
    if (my is_ambient_module && my is_augmentation) my inner_scope->export_context = true;

    block_add_names(&my contents, my inner_scope);
}

void namespace_decl_resolve_names(NamespaceDecl* me, Scope* names, ErrorCtx* errs) {
    // The scope was made when the namespace was declared
    my inner_scope->flow = names->flow;
    block_lookup_names_container(&my contents, false, c_sv("namespace"), my name, my inner_scope, errs);
    my inner_scope->flow = nullptr;
}

void namespace_decl_typecheck(NamespaceDecl* me, bool declared, ErrorCtx* errs) {
    block_typecheck(&my contents, (BlockTypecheckCtx){ .ambient = declared }, errs);
}

// If statements

zsview for_each_loop_statement_keyword_str(ForEachLoopStatement const* me) {
    switch (my keyword) {
        case FOR_EACH_LOOP_OF: return c_zv("of");
        case FOR_EACH_LOOP_IN: return c_zv("in");
    }
    impossible;
}

Ty const* for_each_loop_statement_evaluate_var_type(ForEachLoopStatement* me) {
    Checker* checker = current_checker;
    Ty const* iterable = ty_expr_type(checker, &my iterable);
    if (iterable == ty_intrinsic(checker, TY_ERROR)) return iterable; // Self-referential
    switch (my keyword) {
        case FOR_EACH_LOOP_IN:
            // TODO: `Extract<keyof T, string>` for a type parameter
            return ty_intrinsic(checker, TY_STRING);

        case FOR_EACH_LOOP_OF:
            // TODO: iterables
            if (ty_extends_array(checker, iterable) ||
                ty_is_related(checker, iterable, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE)
            ) {
                Ty const* element = ty_access(checker, iterable, ty_intrinsic(checker, TY_NUMBER));
                if (element) return element;
            }
            return ty_intrinsic(checker, TY_ANY);
    }
    impossible;
}

// What `for (k in x)` takes: object types and type parameters (every member of a union, some member of an
// intersection: `T & object` is what `typeof t === "object"` makes of a T). Without strict null checks also null
static bool for_in_accepts(Checker* checker, Ty const* ty) {
    if (ty->flags & (TYF_ANY | TYF_OBJECT | TYF_NON_PRIMITIVE | TYF_TYPE_PARAM | TYF_DEFERRED)) return true;
    if (ty->flags & TYF_NULLABLE) return !compiler_options()->strict_null_checks;
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (!for_in_accepts(checker, ty->composite.types.data[i])) return false;
        }
        return ty->composite.types.len > 0;
    }
    if (ty->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (for_in_accepts(checker, ty->composite.types.data[i])) return true;
        }
    }
    return false;
}

// Statements

void statement_drop(Statement* me) {
    switch (my tag) {
        case STATEMENT_NOOP: break;

        case STATEMENT_SOURCE_DIRECTIVE:
            cstr_drop(&my source_directive->path);
            cstr_drop(&my source_directive->not_found);
            free(my source_directive);
            break;

        case STATEMENT_BLOCK:
            block_drop(&my block);
            break;

        case STATEMENT_IF_ELSE:
            vec_expr_drop(&my if_else.conditions);
            vec_statement_drop(&my if_else.statements);
            break;

        case STATEMENT_FOR_LOOP:
            statement_drop(&my for_loop->initialization);
            expr_drop(&my for_loop->condition);
            expr_drop(&my for_loop->afterthought);
            statement_drop(&my for_loop->body);
            free(my for_loop);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT: variable_statement_drop(&my for_each_loop->variable.statement); break;
                case FOR_EACH_LOOP_EXPR: expr_drop(&my for_each_loop->variable.expr); break;
            }
            lazy_type_drop(&my for_each_loop->var_type);
            expr_drop(&my for_each_loop->iterable);
            statement_drop(&my for_each_loop->body);
            free(my for_each_loop);
            break;

        case STATEMENT_SWITCH:
            expr_drop(&my switch_->needle);
            vec_switch_case_drop(&my switch_->cases);
            free(my switch_);
            break;

        case STATEMENT_TRY_CATCH:
            block_drop(&my try_catch->try_block);
            binding_drop(&my try_catch->catch_var);
            block_drop(&my try_catch->catch_block);
            block_drop(&my try_catch->finally_block);
            free(my try_catch);
            break;

        case STATEMENT_LABELED:
            statement_drop(my labeled.body);
            free(my labeled.body);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_CONTINUE: break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_drop(&my expr);
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_drop(my import_export);
            free(my import_export);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_drop(&my decl.variable);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_drop(my decl.function.impl);
            free(my decl.function.impl);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_drop(my decl.enum_);
            free(my decl.enum_);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_drop(my decl.class);
            free(my decl.class);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_drop(&my decl.type);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_drop(my decl.interface);
            free(my decl.interface);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_drop(&my decl.namespace);
            break;
    }
}

// The variables of `for (var/let x of y)`: their type comes from the iterable
static void for_each_loop_add_names(ForEachLoopStatement* me, Scope* scope) {
    if (my variable.tag != FOR_EACH_LOOP_STATEMENT) return;
    VecInitializedBinding* initializers = &my variable.statement.initializers;
    intptr_t var_size = vec_initialized_binding_size(initializers);
    if (var_size > 1) {
        error_ctx_error_d(
            scope->errs,
            vec_initialized_binding_at(initializers, 1)->binding.content_span,
            my keyword == FOR_EACH_LOOP_OF ? 1188 : 1091,
            "Only a single variable declaration is allowed in a 'for...",
            for_each_loop_statement_keyword_str(me).str, "' statement."
        );
    } else if (var_size == 0) {
        // Error added by parser
    } else {
        InitializedBinding* initializer = initializers->data;
        if (initializer->value.tag != EXPR_IMPLICIT_ANY) {
            error_ctx_error_d(
                scope->errs,
                initializer->binding.content_span,
                1189,
                "The variable declaration of a 'for...",
                for_each_loop_statement_keyword_str(me).str,
                "' statement cannot have an initializer."
            );
        } else if (initializer->binding.has_explicit_type) {
            error_ctx_error_d(
                scope->errs,
                initializer->binding.content_span,
                2404,
                "The left-hand side of a 'for...",
                for_each_loop_statement_keyword_str(me).str,
                "' statement cannot use a type annotation."
            );
        }
    }
    for_each (i, vec_initialized_binding, &my variable.statement.initializers) {
        // If we set has_explicit_type = false here but don't clear the type, the type will still show
        // up in test AST traversial (as required), but will not be typechecked (as required). This
        // is dumb but it works.
        i->binding.has_explicit_type = false;
        binding_add_names(&i->binding, &my var_type, scope, (BindingAddNamesProps){
            .function_scoped = my variable.statement.keyword == VAR_INIT_VAR,
        });
    }
}

static void block_hoist_names(Block* me, Scope* scope) {
    for_each (i, vec_statement, &my statements) {
        statement_hoist_names(i, scope);
    }
}

// Declares the `var`s of nested statements in the function, module or namespace scope they belong to. Never looks
// into functions. The blocks they're in skip them when their turn comes (scope_init_declaration())
void statement_hoist_names(Statement* me, Scope* scope) {
    switch (my tag) {
        case STATEMENT_BLOCK:
            block_hoist_names(&my block, scope);
            break;

        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) {
                statement_hoist_names(i, scope);
            }
            break;

        case STATEMENT_FOR_LOOP:
            statement_hoist_names(&my for_loop->initialization, scope);
            statement_hoist_names(&my for_loop->body, scope);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            if (my for_each_loop->variable.tag == FOR_EACH_LOOP_STATEMENT &&
                my for_each_loop->variable.statement.keyword == VAR_INIT_VAR
            ) {
                for_each_loop_add_names(my for_each_loop, scope);
            }
            statement_hoist_names(&my for_each_loop->body, scope);
            break;

        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) {
                    statement_hoist_names(j, scope);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_hoist_names(&my try_catch->try_block, scope);
            block_hoist_names(&my try_catch->catch_block, scope);
            block_hoist_names(&my try_catch->finally_block, scope);
            break;

        case STATEMENT_LABELED:
            statement_hoist_names(my labeled.body, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword == VAR_INIT_VAR) {
                variable_statement_add_names(&my decl.variable, scope, my decl.props.exported, my decl.props.declared);
            }
            break;

        default:
            break;
    }
}

// `export default function f() {}`: f locally, `default` in the exports
static void statement_export_default(Statement const* me, Scope* scope) {
    Symbol* symbol = nullptr;
    switch (my tag) {
        case STATEMENT_DECL_FUNCTION: symbol = my decl.function.impl->symbol; break;
        case STATEMENT_DECL_ENUM: symbol = my decl.enum_->symbol; break;
        case STATEMENT_DECL_CLASS: symbol = my decl.class->symbol; break;
        case STATEMENT_DECL_INTERFACE: symbol = my decl.interface->symbol; break;
        default: break;
    }
    if (symbol) scope_export_default(scope, symbol);
}

void statement_add_names(Statement* me, Scope* scope) {
    if (statement_declares_name(my tag) && my decl.props.exported == DECL_EXPORTED_YES_DEFAULT) {
        my decl.props.exported = DECL_EXPORTED_NO;
        statement_add_names(me, scope);
        my decl.props.exported = DECL_EXPORTED_YES_DEFAULT;
        statement_export_default(me, scope);
        return;
    }
    switch (my tag) {
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            scope_export_default_expr(scope, &my expr, my export_equals);
            break;

        case STATEMENT_IMPORT_EXPORT:
            port_statement_add_names(my import_export, scope);
            break;

        case STATEMENT_DECL_VARIABLE:
            // The `var`s of nested blocks were declared by statement_hoist_names()
            if (my decl.variable.keyword == VAR_INIT_VAR && !scope_is_var_container(scope)) break;
            variable_statement_add_names(&my decl.variable, scope, my decl.props.exported, my decl.props.declared);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_add_names(my decl.function.impl, scope, my decl.props.exported);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_add_names(my decl.enum_, scope, my decl.props.exported);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_add_names(my decl.class, scope, my decl.props.exported);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_add_names(&my decl.type, scope, my decl.props);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_add_names(my decl.interface, scope, my decl.props.exported);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_add_names(&my decl.namespace, scope, my decl.props);
            break;

        case STATEMENT_LABELED:
            statement_add_names(my labeled.body, scope);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_BLOCK:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            break;
    }
}

// Control flow (flow.h). These follow the functions of the same names in tsc's binder

// Statements that can validly be used before they appear: not part of an unreachable range
static bool statement_is_executable(Statement const* me) {
    switch (my tag) {
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
            return false;
        case STATEMENT_DECL_NAMESPACE:
            return block_is_instantiated(&my decl.namespace.contents, compiler_options()->preserve_const_enums);
        case STATEMENT_DECL_ENUM:
            return !my decl.enum_->is_const || compiler_options()->preserve_const_enums;
        case STATEMENT_DECL_VARIABLE:
            if (my decl.variable.keyword != VAR_INIT_VAR) return true;
            for_each (i, vec_initialized_binding, &my decl.variable.initializers) {
                if (i->value.tag == EXPR_IMPLICIT_ANY) return false; // `var x;` may declare a variable used above
            }
            return true;
        default:
            return true;
    }
}

// Whether the statement is unreachable. The first statement of unreachable code is where TS7027 goes, which is an
// error only when allowUnreachableCode is explicitly false (unset: a suggestion, which baselines don't have)
static bool statement_check_unreachable(Statement const* me, FlowBuilder* flow, VecStatement const* list) {
    if (!flow_is_unreachable(flow->current)) return false;
    if (flow->current != &flow_unreachable) return true;
    bool report;
    switch (my tag) {
        case STATEMENT_BLOCK: // The statements in it
        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_IMPORT_EXPORT:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            report = false;
            break;
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_NAMESPACE:
            report = statement_is_executable(me);
            break;
        default:
            report = true;
            break;
    }
    if (!report) return true;
    flow->current = &flow_reported_unreachable;

    CompilerOptions const* options = compiler_options();
    bool is_error = options->is_set[COMPILER_OPTION_allow_unreachable_code] && !options->allow_unreachable_code &&
        !flow->ambient && statement_is_executable(me);
    if (!is_error) return true;
    if (!list || !statement_is_executable(me)) {
        error_ctx_error_d(flow->errs, my span, 7027, "Unreachable code detected.");
        return true;
    }
    // In a block: every run of executable statements from here on
    isize_t count = vec_statement_size(list);
    for (isize_t i = me - vec_statement_at(list, 0); i < count; i++) {
        if (!statement_is_executable(vec_statement_at(list, i))) continue;
        isize_t last = i;
        while (last + 1 < count && statement_is_executable(vec_statement_at(list, last + 1))) last++;
        error_ctx_error_d(
            flow->errs, span_between(vec_statement_at(list, i)->span, vec_statement_at(list, last)->span),
            7027, "Unreachable code detected."
        );
        i = last;
    }
    return true;
}

static void statement_resolve_names_body(Statement* me, Scope* names, ErrorCtx* errs, FlowNode* break_target, FlowNode* continue_target) {
    FlowBuilder* flow = names->flow;
    FlowNode* saved_break = flow->break_target;
    FlowNode* saved_continue = flow->continue_target;
    flow->break_target = break_target;
    flow->continue_target = continue_target;
    statement_resolve_names(me, names, errs);
    flow->break_target = saved_break;
    flow->continue_target = saved_continue;
}

// `a: b: while (x) {}`: `continue a` goes where the loop continues. Takes the labels the statement was given
static FlowNode* flow_set_continue_target(FlowBuilder* flow, u32 label_count, FlowNode* target) {
    FlowActiveLabel* label = flow->active_labels;
    for (u32 i = 0; i < label_count && label; i++, label = label->next) {
        label->continue_target = target;
    }
    return target;
}

static void if_else_resolve_names(IfStatement* me, isize_t index, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    FlowNode* then_label = flow_new_branch_label(flow);
    FlowNode* else_label = flow_new_branch_label(flow);
    FlowNode* post = flow_new_branch_label(flow);
    expr_lookup_names_condition(vec_expr_at_mut(&my conditions, index), names, errs, then_label, else_label);
    flow_set_current(flow, flow_finish_label(then_label));
    {
        Statement* branch = vec_statement_at_mut(&my statements, index);
        Scope* branch_scope = scope_new(names, SCOPE_BLOCK);
        statement_add_names(branch, branch_scope);
        statement_resolve_names(branch, branch_scope, errs);
    }
    flow_add_antecedent(flow, post, flow->current);
    flow_set_current(flow, flow_finish_label(else_label));
    if (index + 1 < vec_expr_size(&my conditions)) {
        // `else if`: an if statement of its own, which can be where unreachable code starts
        // TODO: TS7027 for it (there is no Statement to hand to statement_check_unreachable())
        if_else_resolve_names(me, index + 1, names, errs);
    } else if (index + 1 < vec_statement_size(&my statements)) {
        Statement* branch = vec_statement_at_mut(&my statements, index + 1);
        Scope* branch_scope = scope_new(names, SCOPE_BLOCK);
        statement_add_names(branch, branch_scope);
        statement_resolve_names(branch, branch_scope, errs);
    }
    flow_add_antecedent(flow, post, flow->current);
    flow_set_current(flow, flow_finish_label(post));
}

static void switch_resolve_names(SwitchStatement* me, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    FlowNode* post = flow_new_branch_label(flow);
    expr_lookup_names(&my needle, names, errs);
    FlowNode* saved_break = flow->break_target;
    FlowNode* saved_pre_switch = flow->pre_switch_case_flow;
    flow->break_target = post;
    flow->pre_switch_case_flow = flow->current;

    Scope* switch_scope = scope_new(names, SCOPE_BLOCK);
    for_each (i, vec_switch_case, &my cases) {
        for_each (j, vec_statement, &i->statements) {
            statement_add_names(j, switch_scope);
        }
    }
    bool is_narrowing = (my needle.tag == EXPR_LITERAL && my needle.literal.tag == VALUE_BOOLEAN && my needle.literal.boolean) ||
        expr_is_narrowing(&my needle);
    FlowNode* fallthrough = &flow_unreachable;
    bool has_default = false;
    isize_t count = vec_switch_case_size(&my cases);
    for (isize_t i = 0; i < count; i++) {
        isize_t clause_start = i;
        // Clauses without statements share the next one's
        while (!vec_statement_size(&vec_switch_case_at(&my cases, i)->statements) && i + 1 < count) {
            if (fallthrough == &flow_unreachable) flow_set_current(flow, flow->pre_switch_case_flow);
            SwitchCase* empty = vec_switch_case_at_mut(&my cases, i);
            if (empty->is_pattern) {
                FlowNode* saved = flow->current;
                flow_set_current(flow, flow->pre_switch_case_flow);
                expr_lookup_names(&empty->pattern, switch_scope, errs);
                flow_set_current(flow, saved);
            } else {
                has_default = true;
            }
            i++;
        }
        FlowNode* pre_case = flow_new_branch_label(flow);
        flow_add_antecedent(flow, pre_case, is_narrowing && !flow_is_unreachable(flow->pre_switch_case_flow) ?
            flow_new_switch_clause(flow, flow->pre_switch_case_flow, me, (u32)clause_start, (u32)i + 1) :
            flow->pre_switch_case_flow);
        flow_add_antecedent(flow, pre_case, fallthrough);
        flow_set_current(flow, flow_finish_label(pre_case));
        SwitchCase* clause = vec_switch_case_at_mut(&my cases, i);
        if (clause->is_pattern) {
            FlowNode* saved = flow->current;
            flow_set_current(flow, flow->pre_switch_case_flow);
            expr_lookup_names(&clause->pattern, switch_scope, errs);
            flow_set_current(flow, saved);
        } else {
            has_default = true;
        }
        for_each (j, vec_statement, &clause->statements) {
            statement_resolve_names(j, switch_scope, errs);
        }
        fallthrough = flow->current;
    }
    flow_add_antecedent(flow, post, flow->current);
    if (!has_default && !flow_is_unreachable(flow->pre_switch_case_flow)) {
        // "No clause matched": the checker decides whether that can happen
        flow_add_antecedent(flow, post, flow_new_switch_clause(flow, flow->pre_switch_case_flow, me, 0, 0));
    }
    flow->break_target = saved_break;
    flow->pre_switch_case_flow = saved_pre_switch;
    flow_set_current(flow, flow_finish_label(post));
}

static void try_catch_resolve_names(TryCatchStatement* me, bool has_catch, bool has_finally, Scope* names, ErrorCtx* errs) {
    // Any code in the try block can throw, but only assignments widen what a variable may be: they are the
    // antecedents of the exception label, which is where the catch block (or the finally block) starts
    FlowBuilder* flow = names->flow;
    FlowNode* saved_return = flow->return_target;
    FlowNode* saved_exception = flow->exception_target;
    FlowNode* normal_exit = flow_new_branch_label(flow);
    FlowNode* return_label = flow_new_branch_label(flow);
    FlowNode* exception_label = flow_new_branch_label(flow);
    if (has_finally) flow->return_target = return_label;
    flow_add_antecedent(flow, exception_label, flow->current);
    flow->exception_target = exception_label;
    block_resolve_names(&my try_block, names, errs);
    flow_add_antecedent(flow, normal_exit, flow->current);
    if (has_catch) {
        flow_set_current(flow, flow_finish_label(exception_label));
        // The catch block is like a second try block
        exception_label = flow_new_branch_label(flow);
        flow_add_antecedent(flow, exception_label, flow->current);
        flow->exception_target = exception_label;

        Scope* catch_scope = scope_new(names, SCOPE_BLOCK);
        binding_add_names(&my catch_var, nullptr, catch_scope, (BindingAddNamesProps){});
        binding_lookup_names(&my catch_var, catch_scope, errs);
        block_resolve_names(&my catch_block, catch_scope, errs);
        flow_add_antecedent(flow, normal_exit, flow->current);
    }
    flow->return_target = saved_return;
    flow->exception_target = saved_exception;
    if (!has_finally) {
        flow_set_current(flow, flow_finish_label(normal_exit));
        return;
    }
    // The finally block is reached from everywhere. Code after it only from the normal exits, a `return` target only
    // from the returns, an outer exception target only from the exceptions: that's what the reduce labels say
    FlowNode* finally_label = flow_new_branch_label(flow);
    finally_label->antecedents =
        flow_list_concat(flow, normal_exit->antecedents, exception_label->antecedents, return_label->antecedents);
    flow_set_current(flow, finally_label);
    block_resolve_names(&my finally_block, names, errs);
    if (flow->dead) return;
    if (flow_is_unreachable(flow->current)) {
        flow->current = &flow_unreachable;
        return;
    }
    if (flow->return_target && return_label->antecedents) {
        flow_add_antecedent(flow, flow->return_target,
            flow_new_reduce_label(flow, finally_label, return_label->antecedents, flow->current));
    }
    if (flow->exception_target && exception_label->antecedents) {
        flow_add_antecedent(flow, flow->exception_target,
            flow_new_reduce_label(flow, finally_label, exception_label->antecedents, flow->current));
    }
    flow->current = normal_exit->antecedents ?
        flow_new_reduce_label(flow, finally_label, normal_exit->antecedents, flow->current) : &flow_unreachable;
}

static void statement_resolve_names_labeled(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs);

// label_count: how many labels the statement has
static void statement_resolve_names_reachable(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    switch (my tag) {
        case STATEMENT_LABELED: {
            FlowNode* post = flow_new_branch_label(flow);
            FlowActiveLabel label = { .next = flow->active_labels, .name = my labeled.name, .break_target = post };
            flow->active_labels = &label;
            // A label on a label on a loop is that loop's label too
            statement_resolve_names_labeled(my labeled.body, label_count + 1, names, errs);
            CompilerOptions const* options = compiler_options();
            if (!label.referenced && !flow->dead && options->is_set[COMPILER_OPTION_allow_unused_labels] &&
                !options->allow_unused_labels
            ) {
                error_ctx_error_d(errs, my labeled.name, 7028, "Unused label.");
            }
            flow->active_labels = label.next;
            flow_add_antecedent(flow, post, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_BLOCK:
            block_resolve_names(&my block, names, errs);
            break;

        case STATEMENT_IF_ELSE:
            if_else_resolve_names(&my if_else, 0, names, errs);
            break;

        case STATEMENT_FOR_LOOP: {
            ForLoopStatement* loop = my for_loop;
            Scope* loop_scope = scope_new(names, SCOPE_BLOCK);
            statement_add_names(&loop->initialization, loop_scope);
            statement_add_names(&loop->body, loop_scope);
            if (loop->is_do_while) {
                // (Nothing in these, but every expression wants its names looked up)
                statement_resolve_names(&loop->initialization, loop_scope, errs);
                expr_lookup_names(&loop->afterthought, loop_scope, errs);
                FlowNode* pre_do = flow_new_loop_label(flow);
                FlowNode* pre_condition = flow_set_continue_target(flow, label_count, flow_new_branch_label(flow));
                FlowNode* post = flow_new_branch_label(flow);
                flow_add_antecedent(flow, pre_do, flow->current);
                flow_set_current(flow, pre_do);
                statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_condition);
                flow_add_antecedent(flow, pre_condition, flow->current);
                flow_set_current(flow, flow_finish_label(pre_condition));
                expr_lookup_names_condition(&loop->condition, loop_scope, errs, pre_do, post);
                flow_set_current(flow, flow_finish_label(post));
                break;
            }
            // `while (c)` is `for (; c; )`
            FlowNode* pre_loop = flow_set_continue_target(flow, label_count, flow_new_loop_label(flow));
            FlowNode* pre_body = flow_new_branch_label(flow);
            FlowNode* pre_incrementor = flow_new_branch_label(flow);
            FlowNode* post = flow_new_branch_label(flow);
            statement_resolve_names(&loop->initialization, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, pre_loop);
            expr_lookup_names_condition(&loop->condition, loop_scope, errs, pre_body, post);
            flow_set_current(flow, flow_finish_label(pre_body));
            statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_incrementor);
            flow_add_antecedent(flow, pre_incrementor, flow->current);
            flow_set_current(flow, flow_finish_label(pre_incrementor));
            expr_lookup_names(&loop->afterthought, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_FOR_EACH_LOOP: {
            ForEachLoopStatement* loop = my for_each_loop;
            lazy_type_bind(&loop->var_type, LAZY_TYPE_FOR_EACH_LOOP_VAR);
            Scope* loop_scope = scope_new(names, SCOPE_BLOCK);
            bool is_declaration = loop->variable.tag == FOR_EACH_LOOP_STATEMENT;
            if (is_declaration && loop->variable.statement.keyword != VAR_INIT_VAR) {
                for_each_loop_add_names(loop, loop_scope);
            }
            FlowNode* pre_loop = flow_set_continue_target(flow, label_count, flow_new_loop_label(flow));
            FlowNode* post = flow_new_branch_label(flow);
            expr_lookup_names(&loop->iterable, loop_scope, errs);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, pre_loop);
            flow_add_antecedent(flow, post, flow->current);
            if (is_declaration) {
                for_each (i, vec_initialized_binding, &loop->variable.statement.initializers) {
                    binding_lookup_names(&i->binding, loop_scope, errs);
                    expr_lookup_names(&i->value, loop_scope, errs);
                    binding_flow_assign(&i->binding, loop_scope);
                }
            } else {
                flow->assignment_target = FLOW_TARGET_DEFINITE;
                expr_lookup_names(&loop->variable.expr, names, errs);
                expr_flow_assign_for_each(&loop->variable.expr, names, loop);
            }
            statement_add_names(&loop->body, loop_scope);
            statement_resolve_names_body(&loop->body, loop_scope, errs, post, pre_loop);
            flow_add_antecedent(flow, pre_loop, flow->current);
            flow_set_current(flow, flow_finish_label(post));
        }   break;

        case STATEMENT_SWITCH:
            switch_resolve_names(my switch_, names, errs);
            break;

        case STATEMENT_TRY_CATCH:
            try_catch_resolve_names(my try_catch, my try_catch->has_catch, my try_catch->has_finally, names, errs);
            break;

        case STATEMENT_EXPR:
            expr_lookup_names_statement(&my expr, names, errs);
            break;

        case STATEMENT_EXPORT_DEFAULT_EXPR:
            expr_lookup_names(&my expr, names, errs);
            break;

        case STATEMENT_THROW_EXPR:
            expr_lookup_names(&my expr, names, errs);
            flow_set_current(flow, &flow_unreachable);
            flow->has_flow_effects = true;
            break;

        case STATEMENT_RETURN_EXPR:
            if (!scope_has_function(names)) {
                error_ctx_error_d(
                    errs,
                    my expr.outer_span,
                    1108,
                    "A 'return' statement can only be used within a function body."
                );
            }
            expr_lookup_names(&my expr, names, errs);
            if (!flow->dead) {
                if (statement_is_bare_return(me)) flow->has_bare_return = true;
                flow->has_explicit_return = true;
                if (flow->return_target) flow_add_antecedent(flow, flow->return_target, flow->current);
                flow->current = &flow_unreachable;
                flow->has_flow_effects = true;
            }
            break;

        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE: {
            if (flow->dead) break;
            bool is_break = my tag == STATEMENT_BREAK;
            FlowNode* target = is_break ? flow->break_target : flow->continue_target;
            if (span_valid(my jump_label)) {
                target = nullptr;
                for (FlowActiveLabel* i = flow->active_labels; i; i = i->next) {
                    if (!csview_equals_sv(i->name.sv, my jump_label.sv)) continue;
                    i->referenced = true;
                    target = is_break ? i->break_target : i->continue_target;
                    break;
                }
            }
            if (target) {
                flow_add_antecedent(flow, target, flow->current);
                flow->current = &flow_unreachable;
                flow->has_flow_effects = true;
            }
        }   break;

        case STATEMENT_IMPORT_EXPORT: // Aliases are resolved by binder_link()
            port_statement_lookup_names(my import_export, names, errs);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_resolve_names(&my decl.variable, names, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_lookup_names(my decl.function.impl, true, names, errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_lookup_names(my decl.enum_, names, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_lookup_names(my decl.class, names, errs);
            break;

        case STATEMENT_DECL_TYPE: {
            Scope* generic_scope = scope_new_generic(names, &my decl.type.generic, errs);
            type_lookup_names(&my decl.type.type, generic_scope, errs);
        }   break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_lookup_names(my decl.interface, names, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_resolve_names(&my decl.namespace, names, errs);
            break;

        case STATEMENT_SOURCE_DIRECTIVE:
            if (cstr_size(&my source_directive->not_found)) {
                // Without the quotes
                Span span = my source_directive->path_span;
                if (span.sv.size >= 2 && (*span.sv.buf == '"' || *span.sv.buf == '\'')) {
                    span.sv = csview_slice(span.sv, 1, span.sv.size - 1);
                }
                if (my source_directive->tag == SOURCE_DIRECTIVE_REFERENCE_TYPES) {
                    error_ctx_error_d(errs, span, 2688,
                        "Cannot find type definition file for '", my source_directive->not_found, "'.");
                } else {
                    error_ctx_error_d(errs, span, 6053,
                        "File '", my source_directive->not_found, "' not found.");
                }
            }
            break;

        case STATEMENT_NOOP:
            break;
    }
}

void statement_resolve_names(Statement* me, Scope* names, ErrorCtx* errs) {
    statement_resolve_names_labeled(me, 0, names, errs);
}

static void statement_resolve_names_labeled(Statement* me, u32 label_count, Scope* names, ErrorCtx* errs) {
    FlowBuilder* flow = names->flow;
    VecStatement const* list = flow->statement_list;
    flow->statement_list = nullptr;
    if (statement_check_unreachable(me, flow, list)) {
        // Like tsc: nothing in unreachable code is part of the flow graph
        bool was_dead = flow->dead;
        flow->dead = true;
        statement_resolve_names_reachable(me, label_count, names, errs);
        flow->dead = was_dead;
        return;
    }
    switch (my tag) {
        case STATEMENT_NOOP:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
        case STATEMENT_DECL_VARIABLE:
            my flow = flow_builder_reference(flow, span_null);
            break;
        default:
            break;
    }
    // What "the statement an assignment is in" can be (tsc: extendAssignmentPosition())
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_EXPR:
        case STATEMENT_IF_ELSE:
        case STATEMENT_FOR_LOOP:
        case STATEMENT_FOR_EACH_LOOP:
        case STATEMENT_SWITCH:
        case STATEMENT_TRY_CATCH:
        case STATEMENT_DECL_CLASS: {
            FlowStatementFrame frame = { flow->statements, my span };
            flow->statements = &frame;
            statement_resolve_names_reachable(me, label_count, names, errs);
            flow->statements = frame.parent;
        }   break;

        default:
            statement_resolve_names_reachable(me, label_count, names, errs);
            break;
    }
}

void statement_collect_return_types(Statement const* me, TyBuf* out) {
    switch (my tag) {
        case STATEMENT_BLOCK: block_collect_return_types(&my block, out); break;

        case STATEMENT_IF_ELSE:
            for_each (i, vec_statement, &my if_else.statements) {
                statement_collect_return_types(i, out);
            }
            break;

        case STATEMENT_FOR_LOOP:
            statement_collect_return_types(&my for_loop->initialization, out);
            statement_collect_return_types(&my for_loop->body, out);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            statement_collect_return_types(&my for_each_loop->body, out);
            break;

        case STATEMENT_SWITCH:
            for_each (i, vec_switch_case, &my switch_->cases) {
                for_each (j, vec_statement, &i->statements) {
                    statement_collect_return_types(j, out);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_collect_return_types(&my try_catch->try_block, out);
            block_collect_return_types(&my try_catch->catch_block, out);
            block_collect_return_types(&my try_catch->finally_block, out);
            break;

        case STATEMENT_RETURN_EXPR:
            if (statement_is_bare_return(me)) break;
            ty_buf_push(out, ty_check_expr(current_checker, &my expr, current_checker->return_context));
            break;

        case STATEMENT_LABELED: statement_collect_return_types(my labeled.body, out); break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
        case STATEMENT_BREAK:
        case STATEMENT_CONTINUE:
        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
        case STATEMENT_IMPORT_EXPORT:
        case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_FUNCTION:
        case STATEMENT_DECL_ENUM:
        case STATEMENT_DECL_CLASS:
        case STATEMENT_DECL_TYPE:
        case STATEMENT_DECL_INTERFACE:
        case STATEMENT_DECL_NAMESPACE:
            break;
    }
}

void statement_typecheck(Statement* me, BlockTypecheckCtx ctx, ErrorCtx* errs) {
    // What the builder couldn't know: code after a call that never returns or an exhaustive switch (tsc's
    // checkSourceElement(); an error only when allowUnreachableCode is explicitly false)
    if (my flow && compiler_options()->is_set[COMPILER_OPTION_allow_unreachable_code] &&
        !compiler_options()->allow_unreachable_code && ty_flow_reachability(current_checker, my flow) == FLOW_REACH_NO
    ) {
        error_ctx_error_d(errs, my span, 7027, "Unreachable code detected.");
    }
    switch (my tag) {
        case STATEMENT_LABELED:
            ctx.inside_label = true;
            statement_typecheck(my labeled.body, ctx, errs);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
            break;

        case STATEMENT_BLOCK:
            block_typecheck(&my block, ctx, errs);
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            if (statement_size != cond_size && statement_size != cond_size + 1) {
                panic("invalid if statement has ", cond_size, " conditions and ", statement_size, " statements");
            }
            for_each (i, vec_expr, &my if_else.conditions) {
                // Conditions do not have to be booleans
                expr_typecheck(i, errs);
            }
            for_each (i, vec_statement, &my if_else.statements) {
                statement_typecheck(i, ctx, errs);
            }
            break;
        }

        case STATEMENT_FOR_LOOP: {
            statement_typecheck(&my for_loop->initialization, ctx, errs);
            expr_typecheck(&my for_loop->condition, errs);
            expr_typecheck(&my for_loop->afterthought, errs);
            ctx.break_allowed = true;
            ctx.continue_allowed = true;
            statement_typecheck(&my for_loop->body, ctx, errs);
            break;
        }

        case STATEMENT_FOR_EACH_LOOP: {
            Checker* checker = current_checker;
            Ty const* iterable_type = ty_expr_type(checker, &my for_each_loop->iterable);

            switch (my for_each_loop->keyword) {
                case FOR_EACH_LOOP_IN:
                    switch (my for_each_loop->variable.tag) {
                        case FOR_EACH_LOOP_STATEMENT:
                            if (!vec_initialized_binding_is_empty(&my for_each_loop->variable.statement.initializers) &&
                                my for_each_loop->variable.statement.initializers.data->binding.tag != BINDING_IDENTIFIER
                            ) {
                                error_ctx_error_d(
                                    errs,
                                    my for_each_loop->variable.statement.initializers.data->binding.content_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;

                        case FOR_EACH_LOOP_EXPR:
                            if (my for_each_loop->variable.expr.tag == EXPR_ARRAY ||
                                my for_each_loop->variable.expr.tag == EXPR_OBJECT
                            ) {
                                error_ctx_error_d(
                                    errs,
                                    my for_each_loop->variable.expr.outer_span,
                                    2491,
                                    "The left-hand side of a 'for...in' statement cannot be a destructuring pattern."
                                );
                            }
                            break;
                    }

                    // TODO: type parameters are fine too, the apparent type is what counts
                    if (!for_in_accepts(current_checker, iterable_type)) {
                        error_ctx_error_d(errs, my for_each_loop->iterable.outer_span, 2407,
                            "The right-hand side of a 'for...in' statement must be of type 'any', an object type "
                            "or a type parameter, but here has type '", iterable_type, "'.");
                    }
                    break;

                case FOR_EACH_LOOP_OF:
                    if (!ty_extends_array(checker, iterable_type) &&
                        !ty_is_related(checker, iterable_type, ty_intrinsic(checker, TY_STRING), TY_RELATION_ASSIGNABLE)
                    ) {
                        error_ctx_error_d(errs, my for_each_loop->iterable.outer_span, 2495,
                            "Type '", iterable_type, "' is not an array type or a string type.");
                    }
                    break;
            }

            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    for_each (i, vec_initialized_binding, &my for_each_loop->variable.statement.initializers) {
                        binding_typecheck(&i->binding, errs);
                    }
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_typecheck(&my for_each_loop->variable.expr, errs);
                    ty_check_assignable(
                        checker,
                        lazy_type_get(&my for_each_loop->var_type),
                        expr_get_type(&my for_each_loop->variable.expr),
                        my for_each_loop->variable.expr.outer_span,
                        errs
                    );
                    break;
            }

            ctx.break_allowed = true;
            ctx.continue_allowed = true;
            statement_typecheck(&my for_each_loop->body, ctx, errs);
        }   break;

        case STATEMENT_SWITCH: {
            expr_typecheck(&my switch_->needle, errs);
            Ty const* needle_type = expr_get_type(&my switch_->needle);
            intptr_t default_count = 0;
            ctx.break_allowed = true;
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (i->is_pattern) {
                    expr_typecheck(&i->pattern, errs);
                    Ty const* pattern_type = expr_get_type(&i->pattern);
                    if (!ty_is_comparable_either_way(current_checker, needle_type, pattern_type)) {
                        error_ctx_error_d(errs, i->pattern.outer_span, 2678,
                            "Type '", pattern_type, "' is not comparable to type '", needle_type, "'.");
                    }
                } else {
                    default_count++;
                    if (default_count == 2) {
                        error_ctx_error_d(
                            errs,
                            i->default_span,
                            1113,
                            "A 'default' clause cannot appear more than once in a 'switch' statement."
                        );
                    }
                }
                for_each (j, vec_statement, &i->statements) {
                    statement_typecheck(j, ctx, errs);
                }
            }
        }   break;

        case STATEMENT_TRY_CATCH:
            block_typecheck(&my try_catch->try_block, ctx, errs);
            binding_typecheck(&my try_catch->catch_var, errs);
            // No need to typecheck catch variable
            block_typecheck(&my try_catch->catch_block, ctx, errs);
            block_typecheck(&my try_catch->finally_block, ctx, errs);
            break;

        case STATEMENT_BREAK:
            // TODO: check the label actually exists (TS1116)
            if (!ctx.break_allowed && !(span_valid(my jump_label) && ctx.inside_label)) {
                error_ctx_error_d(errs, my keyword_span, 1105,
                    "A 'break' statement can only be used within an enclosing iteration or switch statement.");
            }
            break;

        case STATEMENT_CONTINUE:
            if (!ctx.continue_allowed) {
                error_ctx_error_d(errs, my keyword_span, 1104,
                    "A 'continue' statement can only be used within an enclosing iteration statement.");
            }
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
        case STATEMENT_THROW_EXPR:
            expr_typecheck(&my expr, errs);
            break;

        case STATEMENT_RETURN_EXPR:
            ty_check_expr(current_checker, &my expr, current_checker->return_context);
            expr_typecheck(&my expr, errs);
            if (ctx.specified_return) {
                ty_check_assignable(
                    current_checker, expr_get_type(&my expr), ty_of_node(ctx.specified_return), my expr.outer_span, errs
                );
            }
            break;

        case STATEMENT_IMPORT_EXPORT: break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_typecheck(&my decl.variable, my decl.props.declared || ctx.ambient, errs);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_typecheck(my decl.function.impl, errs);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_typecheck(my decl.enum_, errs);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_typecheck(my decl.class, errs);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_typecheck(&my decl.type, errs);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_typecheck(my decl.interface, errs);
            break;

        case STATEMENT_DECL_NAMESPACE:
            namespace_decl_typecheck(&my decl.namespace, my decl.props.declared || ctx.ambient, errs);
            break;
    }
}

void switch_case_drop(SwitchCase* me) {
    if (my is_pattern) {
        expr_drop(&my pattern);
    }
    vec_statement_drop(&my statements);
}
