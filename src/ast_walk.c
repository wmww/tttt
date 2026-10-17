#include "ast_walk.h"
#include "statement.h"
#include "class_decl.h"
#include "enum_decl.h"

// The children follow the ownership of the *_drop() functions: what a node owns is what is under it

static bool walk_block(Block const* me, AstExprVisit visit, void* arg);
static bool walk_function(FunctionImpl const* me, AstExprVisit visit, void* arg);
static bool walk_class(ClassDecl const* me, AstExprVisit visit, void* arg);

static bool walk_exprs(VecExpr const* exprs, AstExprVisit visit, void* arg) {
    for_each (i, vec_expr, exprs) if (ast_walk_expr(i, visit, arg)) return true;
    return false;
}

static bool walk_binding(Binding const* me, AstExprVisit visit, void* arg) {
    switch (my tag) {
        case BINDING_NOOP: case BINDING_IDENTIFIER: break;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) if (walk_binding(i, visit, arg)) return true;
            break;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key && ast_walk_expr(i->computed_key, visit, arg)) return true;
                if (walk_binding(&i->target, visit, arg)) return true;
            }
            break;
    }
    return my default_value && ast_walk_expr(my default_value, visit, arg);
}

static bool walk_initialized_binding(InitializedBinding const* me, AstExprVisit visit, void* arg) {
    return walk_binding(&my binding, visit, arg) || ast_walk_expr(initialized_binding_value(me), visit, arg);
}

static bool walk_variables(VariableStatement const* me, AstExprVisit visit, void* arg) {
    for_each (i, vec_initialized_binding, &my initializers) if (walk_initialized_binding(i, visit, arg)) return true;
    return false;
}

static bool walk_function(FunctionImpl const* me, AstExprVisit visit, void* arg) {
    for_each (i, vec_function_arg, &my args) {
        if (walk_exprs(function_arg_decorators(i), visit, arg) || walk_initialized_binding(&i->initializer, visit, arg)) return true;
    }
    return walk_block(function_impl_body(me), visit, arg);
}

static bool walk_class(ClassDecl const* me, AstExprVisit visit, void* arg) {
    if (walk_exprs(&my decorators, visit, arg) || ast_walk_expr(&my extends, visit, arg)) return true;
    for_each (i, vec_class_item, &my items) {
        if (walk_exprs(&i->decorators, visit, arg)) return true;
        switch (i->tag) {
            case CLASS_ITEM_CONSTRUCTOR:
                if (walk_function(&i->constructor.func, visit, arg)) return true;
                break;
            case CLASS_ITEM_METHOD:
                if (i->method.computed_key && ast_walk_expr(i->method.computed_key, visit, arg)) return true;
                if (walk_function(&i->method.func, visit, arg)) return true;
                break;
            case CLASS_ITEM_FIELD:
                if (i->field.computed_key && ast_walk_expr(i->field.computed_key, visit, arg)) return true;
                if (ast_walk_expr(&i->field.value, visit, arg)) return true;
                break;
            case CLASS_ITEM_INTERFACE_MEMBER: break;
            case CLASS_ITEM_STATIC_BLOCK:
                if (walk_block(&i->static_block, visit, arg)) return true;
                break;
        }
    }
    return false;
}

static bool walk_statements(VecStatement const* statements, AstExprVisit visit, void* arg) {
    for_each (i, vec_statement, statements) if (ast_walk_statement(i, visit, arg)) return true;
    return false;
}

static bool walk_block(Block const* me, AstExprVisit visit, void* arg) {
    return walk_statements(&my statements, visit, arg);
}

bool ast_walk_statement(Statement const* me, AstExprVisit visit, void* arg) {
    switch (my tag) {
        case STATEMENT_NOOP: case STATEMENT_SOURCE_DIRECTIVE: case STATEMENT_BREAK: case STATEMENT_CONTINUE:
        case STATEMENT_IMPORT_EXPORT: case STATEMENT_DECL_TYPE: case STATEMENT_DECL_INTERFACE:
            return false;
        case STATEMENT_BLOCK: return walk_block(&my block, visit, arg);
        case STATEMENT_IF_ELSE:
            return walk_exprs(&my if_else.conditions, visit, arg) || walk_statements(&my if_else.statements, visit, arg);
        case STATEMENT_FOR_LOOP:
            return ast_walk_statement(&my for_loop->initialization, visit, arg) ||
                ast_walk_expr(&my for_loop->condition, visit, arg) || ast_walk_expr(&my for_loop->afterthought, visit, arg) ||
                ast_walk_statement(&my for_loop->body, visit, arg);
        case STATEMENT_FOR_EACH_LOOP: {
            ForEachLoopStatement const* loop = my for_each_loop;
            bool stop = loop->variable.tag == FOR_EACH_LOOP_STATEMENT
                ? walk_variables(&loop->variable.statement, visit, arg)
                : ast_walk_expr(&loop->variable.expr, visit, arg);
            return stop || ast_walk_expr(&loop->iterable, visit, arg) || ast_walk_statement(&loop->body, visit, arg);
        }
        case STATEMENT_SWITCH:
            if (ast_walk_expr(&my switch_->needle, visit, arg)) return true;
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (i->is_pattern && ast_walk_expr(&i->pattern, visit, arg)) return true;
                if (walk_statements(&i->statements, visit, arg)) return true;
            }
            return false;
        case STATEMENT_TRY_CATCH:
            return walk_block(&my try_catch->try_block, visit, arg) || walk_binding(&my try_catch->catch_var, visit, arg) ||
                walk_block(&my try_catch->catch_block, visit, arg) || walk_block(&my try_catch->finally_block, visit, arg);
        case STATEMENT_LABELED: return ast_walk_statement(my labeled.body, visit, arg);
        case STATEMENT_WITH:
            return ast_walk_expr(&my with->object, visit, arg) || ast_walk_statement(&my with->body, visit, arg);
        case STATEMENT_EXPR: case STATEMENT_EXPORT_DEFAULT_EXPR: case STATEMENT_RETURN_EXPR: case STATEMENT_THROW_EXPR:
            return ast_walk_expr(&my expr, visit, arg);
        case STATEMENT_DECL_VARIABLE: return walk_variables(&my decl.variable, visit, arg);
        case STATEMENT_DECL_FUNCTION: return walk_function(my decl.function.impl, visit, arg);
        case STATEMENT_DECL_ENUM:
            for_each (i, vec_enum_case, &my decl.enum_->cases) if (ast_walk_expr(&i->value, visit, arg)) return true;
            return false;
        case STATEMENT_DECL_CLASS: return walk_class(my decl.class, visit, arg);
        case STATEMENT_DECL_NAMESPACE: return walk_block(&my decl.namespace->contents, visit, arg);
    }
    return false;
}

static bool walk_children(Expr const* me, AstExprVisit visit, void* arg) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY: case EXPR_LITERAL: case EXPR_IDENTIFIER: case EXPR_REGEX: case EXPR_PRIVATE_NAME:
            return false;
        case EXPR_BINARY_OP: {
            // Down the left spine with a loop (generated code has chains long enough to overflow the stack); the
            // order of the visits doesn't matter to anyone
            Expr const* e = me;
            while (true) {
                if (ast_walk_expr(&e->binary_op->right, visit, arg)) return true;
                e = &e->binary_op->left;
                if (e->tag != EXPR_BINARY_OP) return ast_walk_expr(e, visit, arg);
                AstWalk next = visit(e, arg);
                if (next == AST_WALK_STOP) return true;
                if (next == AST_WALK_SKIP) return false;
            }
        }
        case EXPR_UNARY_OP: return ast_walk_expr(&my unary_op->expr, visit, arg);
        case EXPR_PROPERTY:
            return ast_walk_expr(&my property->expr, visit, arg) ||
                (my property->form == PROPERTY_EXPR_BRACKET && ast_walk_expr(&my property->index, visit, arg));
        case EXPR_GENERIC_PARAMS: return ast_walk_expr(&my generic_params->expr, visit, arg);
        case EXPR_AS_TYPE: return ast_walk_expr(&my as_type->expr, visit, arg);
        case EXPR_TERNARY:
            return ast_walk_expr(&my ternary->condition, visit, arg) || ast_walk_expr(&my ternary->yes, visit, arg) ||
                ast_walk_expr(&my ternary->no, visit, arg);
        case EXPR_CALL: return ast_walk_expr(&my call->callable, visit, arg) || walk_exprs(&my call->args, visit, arg);
        case EXPR_FUNCTION: return walk_function(my function, visit, arg);
        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                if (i->computed_key && ast_walk_expr(i->computed_key, visit, arg)) return true;
                if (object_item_rare(i)->shorthand_default && ast_walk_expr(object_item_rare(i)->shorthand_default, visit, arg)) return true;
                if (ast_walk_expr(&i->value, visit, arg)) return true;
            }
            return false;
        case EXPR_ARRAY: return walk_exprs(&my array, visit, arg);
        case EXPR_CLASS_DECL: return walk_class(my class_decl, visit, arg);
        case EXPR_SPREAD: return ast_walk_expr(my spread, visit, arg);
        case EXPR_TEMPLATE:
            return (my template_->tag && ast_walk_expr(my template_->tag, visit, arg)) ||
                walk_exprs(&my template_->substitutions, visit, arg);
        case EXPR_JSX_ELEMENT: case EXPR_JSX_FRAGMENT: {
            JsxElement const* el = my jsx;
            if (ast_walk_expr(&el->tag_name, visit, arg) || ast_walk_expr(&el->attributes, visit, arg)) return true;
            for_each (i, vec_jsx_child, &el->children) if (ast_walk_expr(&i->expr, visit, arg)) return true;
            return false;
        }
        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) if (ast_walk_expr(&i->value, visit, arg)) return true;
            return false;
    }
    return false;
}

bool ast_walk_expr(Expr const* me, AstExprVisit visit, void* arg) {
    AstWalk next = visit(me, arg);
    return next == AST_WALK_STOP || (next == AST_WALK_CONTINUE && walk_children(me, visit, arg));
}
