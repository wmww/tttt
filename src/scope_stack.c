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
void scope_stack_push_scope(ScopeStack* me, Scope* scope) {
    scope_stack_push_frame_private(me, scope);
}

// Declared and used exclusivly by scope.c
void scope_stack_pop_scope(ScopeStack* me, Scope const* scope) {
    PANIC_IF(vec_scope_cptr_empty(&my stack), "Tried to pop too many scopes");
    PANIC_IF(
        *vec_scope_cptr_back(&my stack) != scope,
        "Tried to pop incorrect scope, %zd scopes on the stack",
        vec_scope_cptr_size(&my stack));
    vec_scope_cptr_pop(&my stack);
}

bool scope_stack_has_function(ScopeStack* me) {
    c_foreach_rev(i, vec_scope_cptr, my stack) {
        if ((*i.ref)->tag == SCOPE_FUNCTION) {
            return true;
        }
    }
    return false;
}

Declaration const* scope_stack_get_value(ScopeStack const* me, csview name) {
    c_foreach_rev(i, vec_scope_cptr, my stack) {
        auto result = hmap_str_arc_declaration_get(&(*i.ref)->declarations, name);
        if (result && result->second.get->value_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_type(ScopeStack const* me, csview name) {
    c_foreach_rev(i, vec_scope_cptr, my stack) {
        auto result = hmap_str_arc_declaration_get(&(*i.ref)->declarations, name);
        if (result && result->second.get->type_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_namespace(ScopeStack const* me, csview name) {
    c_foreach_rev(i, vec_scope_cptr, my stack) {
        auto result = hmap_str_arc_declaration_get(&(*i.ref)->declarations, name);
        if (result && result->second.get->namespace) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* scope_stack_get_keyword(ScopeStack const* me, KeywordIdentifier keyword) {
    c_foreach_rev(i, vec_scope_cptr, my stack) {
        Declaration const* decl = nullptr;
        switch (keyword) {
            case KEYWORD_IDENTIFIER_THIS: decl = (*i.ref)->this_declaration; break;
            case KEYWORD_IDENTIFIER_SUPER: decl = (*i.ref)->super_declaration; break;
            case KEYWORD_IDENTIFIER_NONE: break;
        }
        if (decl) return decl;
    }
    return nullptr;
}
