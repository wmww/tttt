#include "scope_stack.h"
#include "scope.h"

typedef struct ScopeStack {
    VecScopeCPtr stack;
} ScopeStack;

static inline void scope_stack_push_frame_private(ScopeStack* me, Scope const* scope) {
    vec_scope_cptr_push(&my stack, scope);
}

ScopeStack* scope_stack_new() {
    return c_new(ScopeStack, {
        .stack = vec_scope_cptr_init()
    });
}

static void scope_stack_drop(ScopeStack* me) {
    PANIC_IF(
        vec_scope_cptr_size(&my stack),
        "Name stack dropped with %zd frames left on it",
        vec_scope_cptr_size(&my stack));
    vec_scope_cptr_drop(&my stack);
}

void scope_stack_destroy(ScopeStack* me) {
    scope_stack_drop(me);
    free(me);
}

// Declared and used exclusivly by scope.c
void scope_stack_push_scope(ScopeStack* me, Scope const* scope) {
    scope_stack_push_frame_private(me, scope);
}

// Declared and used exclusivly by scope.c
void scope_stack_pop_scope(ScopeStack* me, Scope const* scope) {
    PANIC_IF(vec_scope_cptr_is_empty(&my stack), "Tried to pop too many scopes");
    PANIC_IF(
        *vec_scope_cptr_back(&my stack) != scope,
        "Tried to pop incorrect scope, %zd scopes on the stack",
        vec_scope_cptr_size(&my stack));
    vec_scope_cptr_pop(&my stack);
}

bool scope_stack_has_function(ScopeStack* me) {
    for_each_reverse (i, vec_scope_cptr, &my stack) {
        if ((*i)->is_function) {
            return true;
        }
    }
    return false;
}

Declaration const* scope_stack_get_value(ScopeStack const* me, csview name) {
    for_each_reverse (i, vec_scope_cptr, &my stack) {
        auto result = hmap_arc_declaration_get(&(*i)->declarations, name);
        if (result && result->second.get->value_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_type(ScopeStack const* me, csview name) {
    for_each_reverse (i, vec_scope_cptr, &my stack) {
        auto result = hmap_arc_declaration_get(&(*i)->declarations, name);
        if (result && result->second.get->type_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_namespace(ScopeStack const* me, csview name) {
    for_each_reverse (i, vec_scope_cptr, &my stack) {
        auto result = hmap_arc_declaration_get(&(*i)->declarations, name);
        if (result && result->second.get->namespace_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_keyword(ScopeStack const* me, KeywordIdentifier keyword, bool type_else_value) {
    for_each_reverse (i, vec_scope_cptr, &my stack) {
        Declaration const* decl = nullptr;
        switch (keyword) {
            case KEYWORD_IDENTIFIER_THIS: decl = (*i)->this_declaration; break;
            case KEYWORD_IDENTIFIER_SUPER: decl = (*i)->super_declaration; break;
            case KEYWORD_IDENTIFIER_NONE: break;
        }
        if (decl) {
            if (type_else_value && decl->type_tag) {
                return decl;
            } else if (!type_else_value && decl->value_tag) {
                return decl;
            }
        }
    }
    return nullptr;
}
