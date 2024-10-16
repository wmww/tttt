#include "name_stack.h"

typedef struct StackFrame {
    Scope* scope;
} StackFrame;

StackFrame stack_frame_init(Scope* scope);
void stack_frame_drop(StackFrame* me);
StackFrame stack_frame_clone(StackFrame me);

#define i_key_class_names StackFrame, stack_frame
#include "wrap_stc/vec.h"

typedef struct NameStack {
    VecStackFrame stack;
} NameStack;

static void name_stack_drop(NameStack* me);

StackFrame stack_frame_init(Scope* scope) {
    return (StackFrame){
        .scope = scope,
    };
}

void stack_frame_drop(StackFrame* me) {
    ignore me;
}

StackFrame stack_frame_clone(StackFrame me) {
    return me;
}

static inline void name_stack_push_frame_private(NameStack* me, Scope* scope) {
    vec_stack_frame_push(&my stack, stack_frame_init(scope));
}

NameStack* name_stack_new() {
    return c_new(NameStack, {
        .stack = vec_stack_frame_init()
    });
}

static void name_stack_drop(NameStack* me) {
    PANIC_IF(
        vec_stack_frame_size(&my stack),
        "Name stack dropped with %zd frames left on it",
        vec_stack_frame_size(&my stack));
    vec_stack_frame_drop(&my stack);
}

void name_stack_destroy(NameStack* me) {
    name_stack_drop(me);
    free(me);
}

// Declared and used exclusivly by scope.c
void name_stack_push_scope(NameStack* me, Scope* scope) {
    name_stack_push_frame_private(me, scope);
}

// Declared and used exclusivly by scope.c
void name_stack_pop_scope(NameStack* me, Scope* scope) {
    PANIC_IF(vec_stack_frame_empty(&my stack), "Tried to pop too many scopes");
    PANIC_IF(
        vec_stack_frame_back(&my stack)->scope != scope,
        "Tried to pop incorrect scope, %zd scopes on the stack",
        vec_stack_frame_size(&my stack));
    vec_stack_frame_pop(&my stack);
}

bool name_stack_has_function(NameStack* me) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (i.ref->scope->tag == SCOPE_FUNCTION) {
            return true;
        }
    }
    return false;
}

Declaration const* name_stack_get_value(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_arc_declaration_get(&i.ref->scope->declarations, name);
        if (result && result->second.get->value_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Declaration const* name_stack_get_type(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_arc_declaration_get(&i.ref->scope->declarations, name);
        if (result && result->second.get->type_tag) {
            return result->second.get;
        }
    }
    return nullptr;
}

Scope const* name_stack_get_namespace(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_scope_cptr_get(&i.ref->scope->namespaces, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

Declaration const* name_stack_get_keyword(NameStack const* me, KeywordIdentifier keyword) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        Declaration const* decl = nullptr;
        switch (keyword) {
            case KEYWORD_IDENTIFIER_THIS: decl = i.ref->scope->this_declaration; break;
            case KEYWORD_IDENTIFIER_SUPER: decl = i.ref->scope->super_declaration; break;
            case KEYWORD_IDENTIFIER_NONE: break;
        }
        if (decl) return decl;
    }
    return nullptr;
}
