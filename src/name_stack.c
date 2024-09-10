#include "name_stack.h"
#include "generic.h"
#include "several_types.h"
#include "error.h"
#include "interface_member.h"
#include "source_file.h"

typedef NamedType* NamedTypePtr;
typedef NamedVariable* NamedVariablePtr;

NamedVariable* make_named_variable(Span name, Span declaration_full, Type type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedVariable, {
        .name = name,
        .full = declaration_full,
        .type = type,
        .generic = generic,
    });
}

void named_variable_destroy(NamedVariable* me) {
    if (!me) return;
    type_drop(&my type);
    free(me);
}

NamedType* make_named_type(Span name, Span declaration_full, Type type, Generic const* generic) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    return c_new(NamedType, {
        .name = name,
        .declarations = vec_span_single(declaration_full),
        .type = type,
        .generic = generic,
    });
}

static void named_type_drop(NamedType* me) {
    vec_span_drop(&my declarations);
    type_drop(&my type);
    vec_type_drop(&my merged_types);
    lazy_type_drop(&my lazy_type);
}

void named_type_destroy(NamedType* me) {
    if (!me) return;
    my ref_count_minus_1--;
    if (my ref_count_minus_1 < 0) {
        named_type_drop(me);
        free(me);
    }
}

Type named_type_evaluate_type(NamedType* me) {
    // This should only be called when evaluating our LazyType, and our
    // LazyType should only be used when there are multiple types to merge
    ASSERT(my is_merged && vec_type_size(&my merged_types) > 1);
    Type ret = interface_from_merged(&my merged_types);
    vec_type_drop(&my merged_types);
    my merged_types = vec_type_init();
    return ret;
}

typedef NameStack const* NameStackCPtr;
wrap_forward_hmap_str(NameStackCPtr, name_stack_cptr);

#define i_key_ssv
#define i_val_names NamedTypePtr, named_type_ptr
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names NamedVariablePtr, named_variable_ptr
#include "wrap_stc/hmap.h"

#define i_key_ssv
#define i_val_names NamedImport, named_import
#include "wrap_stc/hmap.h"

typedef enum StackFrameTag {
    STACK_FRAME_BLOCK,
    STACK_FRAME_FUNCTION,
    STACK_FRAME_GENERIC,
} StackFrameTag;

typedef struct StackFrame {
    StackFrameTag tag;
    csview default_export;
    HmapStrNamedTypePtr types;
    HmapStrNamedVariablePtr variables;
    HmapStrNameStackCPtr namespaces;
    HmapStrNamedImport imports;
} StackFrame;

StackFrame stack_frame_init(StackFrameTag tag);
void stack_frame_drop(StackFrame* me);
StackFrame stack_frame_clone(StackFrame me);

#define i_key_class_names StackFrame, stack_frame
#include "wrap_stc/vec.h"

typedef struct NameStack {
    VecStackFrame stack;
    ErrorCtx* errs;
} NameStack;

static void name_stack_drop(NameStack* me);

#define i_key_ssv
#define i_val_names NameStackCPtr, name_stack_cptr
#define i_is_forward
#include "wrap_stc/hmap.h"

StackFrame stack_frame_init(StackFrameTag tag) {
    return (StackFrame){
        .tag = tag,
    };
}

void stack_frame_drop(StackFrame* me) {
    hmap_str_named_type_ptr_drop(&my types);
    hmap_str_named_variable_ptr_drop(&my variables);
    hmap_str_name_stack_cptr_drop(&my namespaces);
    if (hmap_str_named_import_size(&my imports)) {
        NONFATAL("Stack frame dropped with imports not empty");
    }
    hmap_str_named_import_drop(&my imports);
}

StackFrame stack_frame_clone(StackFrame me) {
    me.types = hmap_str_named_type_ptr_clone(me.types);
    me.variables = hmap_str_named_variable_ptr_clone(me.variables);
    me.namespaces = hmap_str_name_stack_cptr_clone(me.namespaces);
    me.imports = hmap_str_named_import_clone(me.imports);
    return me;
}

static inline void name_stack_push_frame_private(NameStack* me, StackFrameTag tag) {
    vec_stack_frame_push(&my stack, stack_frame_init(tag));
}

NameStack* name_stack_new(ErrorCtx* errs) {
    NameStack* ret = c_new(NameStack, {
        .stack = vec_stack_frame_init(),
        .errs = errs,
    });
    name_stack_push_frame_private(ret, STACK_FRAME_BLOCK);

#define BUILTIN_VAR(name, t) \
    static NamedVariable name = { 0 }; \
    name.type = t;

#define BUILTIN_TYPE(name, t) \
    static NamedType name = { 0 }; \
    name.type = t

    BUILTIN_VAR(builtin_var_any, type_any);
    BUILTIN_VAR(builtin_var_undefined, type_undefined);

    BUILTIN_TYPE(builtin_type_any, type_any);
    BUILTIN_TYPE(builtin_type_unkown, type_unknown);
    BUILTIN_TYPE(builtin_type_never, type_never);
    BUILTIN_TYPE(builtin_type_void, type_void);
    BUILTIN_TYPE(builtin_type_object, type_object);
    BUILTIN_TYPE(builtin_type_boolean, type_boolean);
    BUILTIN_TYPE(builtin_type_number, type_number);
    BUILTIN_TYPE(builtin_type_string, type_string);

#undef BUILTIN_VAR
#undef BUILTIN_TYPE

    name_stack_add_variable_s(ret, cstr_lit("console"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("String"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Number"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Boolean"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("NaN"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Date"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Error"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Set"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Array"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("Object"), &builtin_var_any);
    name_stack_add_variable_s(ret, cstr_lit("this"), &builtin_var_undefined);

    name_stack_add_type_s(ret, cstr_lit("any"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("unknown"), &builtin_type_unkown);
    name_stack_add_type_s(ret, cstr_lit("never"), &builtin_type_never);
    name_stack_add_type_s(ret, cstr_lit("void"), &builtin_type_void);
    name_stack_add_type_s(ret, cstr_lit("object"), &builtin_type_object);
    name_stack_add_type_s(ret, cstr_lit("boolean"), &builtin_type_boolean);
    name_stack_add_type_s(ret, cstr_lit("number"), &builtin_type_number);
    name_stack_add_type_s(ret, cstr_lit("string"), &builtin_type_string);
    name_stack_add_type_s(ret, cstr_lit("String"), &builtin_type_string);
    name_stack_add_type_s(ret, cstr_lit("Number"), &builtin_type_number);
    name_stack_add_type_s(ret, cstr_lit("Boolean"), &builtin_type_boolean);
    name_stack_add_type_s(ret, cstr_lit("Date"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("bigint"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("symbol"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("intrinsic"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Error"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Set"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Array"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("Object"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("BooleanConstructor"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("NumberConstructor"), &builtin_type_any);
    name_stack_add_type_s(ret, cstr_lit("StringConstructor"), &builtin_type_any);

    return ret;
}

void name_stack_push_block_frame(NameStack* me) {
    name_stack_push_frame_private(me, STACK_FRAME_BLOCK);
}

void name_stack_push_function_frame(NameStack* me) {
    name_stack_push_frame_private(me, STACK_FRAME_FUNCTION);
}

static void name_stack_drop(NameStack* me) {
    /*
    if (vec_stack_frame_size(&my stack) != 1) {
        PANIC("stack size %ld on drop, 1 expected", vec_stack_frame_size(&my stack));
    }
    */
    vec_stack_frame_drop(&my stack);
}

void name_stack_destroy(NameStack* me) {
    name_stack_drop(me);
    free(me);
}

void name_stack_push_generic_frame(NameStack* me, Generic const* generic) {
    if (generic_empty(generic)) {
        // Frame is only needed so a frame can be unconditionally popped later
        name_stack_push_frame_private(me, STACK_FRAME_BLOCK);
    } else {
        name_stack_push_frame_private(me, STACK_FRAME_GENERIC);
        generic_resolve_names(generic, me, my errs);
    }
}

void name_stack_push_name_stack_frame(NameStack* me, NameStack const* ref) {
    ASSERT(vec_stack_frame_size(&ref->stack) == 1);
    vec_stack_frame_push(&my stack, stack_frame_clone(*ref->stack.data));
}

void name_stack_pop_frame(NameStack* me) {
    if (vec_stack_frame_size(&my stack) < 2) {
        PANIC("More stack frames popped than pushed");
    }
    vec_stack_frame_pop(&my stack);
}

NameStack* name_stack_pull_frame_into_new_stack(NameStack* me) {
    if (vec_stack_frame_size(&my stack) < 2) {
        PANIC("More stack frames popped than pushed");
    }
    StackFrame frame = vec_stack_frame_pull(&my stack);
    NameStack* ret = c_new(NameStack, {
        .stack = vec_stack_frame_init(),
        .errs = my errs,
    });
    vec_stack_frame_push(&ret->stack, frame);
    return ret;
}

Type name_stack_make_interface_from(NameStack const* ref) {
    ASSERT(vec_stack_frame_size(&ref->stack) > 0);
    VecInterfaceMember members = vec_interface_member_init();
    c_foreach(i, hmap_str_named_variable_ptr, vec_stack_frame_back(&ref->stack)->variables) {
        vec_interface_member_push(&members, interface_member_make(
            i.ref->second->full,
            span_null, // Interface name
            false, // readonly
            false, // optional
            cstr_from_span(i.ref->second->name),
            i.ref->second->name,
            false, // name is value
            type_clone(i.ref->second->type)
        ));
    }
    return type_interface((Interface){
        members
    });
}

void name_stack_add_variable(NameStack* me, NamedVariable* var) {
    name_stack_add_variable_s(me, cstr_from_span(var->name), var);
}

void name_stack_add_type(NameStack* me, NamedType* type) {
    name_stack_add_type_s(me, cstr_from_span(type->name), type);
}

void name_stack_add_variable_s(NameStack* me, cstr name_string, NamedVariable* var) {
    Span name_span = var->name;
    auto result = hmap_str_named_variable_ptr_insert(&vec_stack_frame_back(&my stack)->variables, name_string, var);
    if (!result.inserted) {
        switch (vec_stack_frame_back(&my stack)->tag) {
            case STACK_FRAME_BLOCK:
                error_ctx_error(
                    my errs,
                    name_span,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SPAN(name_span)
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2451,
                    "Cannot redeclare block-scoped variable '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                break;

            case STACK_FRAME_FUNCTION:
            case STACK_FRAME_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(name_span)
                );
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                break;
        }
    }
}

void name_stack_add_type_s(NameStack* me, cstr name_string, NamedType* type) {
    Span name_span = type->name;
    auto result = hmap_str_named_type_ptr_insert(&vec_stack_frame_back(&my stack)->types, name_string, type);
    if (!result.inserted) {
        switch (vec_stack_frame_back(&my stack)->tag) {
            case STACK_FRAME_BLOCK:
            case STACK_FRAME_FUNCTION:
                error_ctx_error(
                    my errs,
                    result.ref->second->name,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(result.ref->second->name)
                );
                // Fallthrough

            case STACK_FRAME_GENERIC:
                error_ctx_error(
                    my errs,
                    name_span,
                    2300,
                    "Duplicate identifier '%.*s'.",
                    c_SPAN(name_span)
                );
                break;
        }
    }
}

NamedType* name_stack_add_mergeable_type(
    NameStack* me,
    Span name,
    Span declaration_full,
    Type type,
    Generic const* generic,
    bool is_class
) {
    if (generic_empty(generic)) {
        generic = nullptr;
    }
    auto existing = hmap_str_named_type_ptr_get(&vec_stack_frame_back(&my stack)->types, name.sv);
    if (!existing || !existing->second->is_mergeable || (is_class && existing->second->is_class)) {
        // If there is no existing type by this name the normal add type process is fine
        // Two classes can't be merged, so if we try fall back to normal duplicate name behavior
        NamedType* ret = c_new(NamedType, {
            .name = name,
            .declarations = vec_span_single(declaration_full),
            .type = type,
            .generic = generic,
            .is_class = is_class,
            .is_mergeable = true,
        });
        // This might error and not add ret to the name stack. Even if that happens we want to return ret (the caller
        // will own it either way)
        name_stack_add_type_s(me, cstr_from_span(name), ret);
        return ret;
    } else {
        // Merge declarations
        NamedType* merged = existing->second;
        // TODO: check generics are equal
        if (!merged->is_merged) {
            // Previously this was not a merged declaration, so we have some setup to do
            // 1. move the previous type into the merged vec
            // 2. set up our lazy type
            // 3. make our type refer to our lazy type
            // Resolving our type later (in the typecheck phase) will then result in named_type_evaluate_type() being
            // called, which will do the actual merging
            vec_type_push(&merged->merged_types, merged->type);
            merged->lazy_type.tag = LAZY_TYPE_NAMED_TYPE;
            merged->type = type_lazy(&merged->lazy_type);
            merged->is_merged = true;
        }
        vec_span_push(&merged->declarations, declaration_full);
        vec_type_push(&merged->merged_types, type);
        merged->ref_count_minus_1++;
        return merged;
    }
}

void name_stack_add_namespace(NameStack* me, Span name, NameStack const* ns) {
    // PERF: I don't think cloning into a box is needed? The reference we get should simply live long enough
    // NameStacks may not need to be clonable if that's the case
    cstr name_string = cstr_from_span(name);
    auto result = hmap_str_name_stack_cptr_insert(&vec_stack_frame_back(&my stack)->namespaces, name_string, ns);
    if (!result.inserted) {
        // TODO: declaration merging
        error_ctx_error(
            my errs,
            name,
            TS_CODE_NONE,
            "Duplicate namespace '%.*s'.",
            c_SPAN(name)
        );
    }
}

void name_stack_add_import(NameStack* me, NamedImport import) {
    auto result = hmap_str_named_import_insert(
        &vec_stack_frame_back(&my stack)->imports,
        cstr_from_span(import.decl_name),
        import
    );
    if (!result.inserted) {
        error_ctx_error(
            my errs,
            import.decl_name,
            2300,
            "Duplicate identifier '%.*s'.",
            c_SPAN(import.decl_name)
        );
    }
}

static void add_import(NameStack* me, Span name, NamedImport const* import) {
    NameStack const* name_stack = import->source_file->name_stack;
    csview source_name = span_valid(import->source_name) ?
        import->source_name.sv :
        vec_stack_frame_back(&name_stack->stack)->default_export;
    bool found = false;
    {
        NamedVariable* var = name_stack_get_variable(name_stack, source_name);
        if (var) {
            name_stack_add_variable_s(me, cstr_from_span(import->decl_name), var);
            found = true;
        }
    }{
        NamedType* type = name_stack_get_type(name_stack, source_name);
        if (type) {
            name_stack_add_type_s(me, cstr_from_span(import->decl_name), type);
            found = true;
        }
    }{
        NameStack const* namespace = name_stack_get_namespace(name_stack, source_name);
        if (namespace) {
            name_stack_add_namespace(me, import->decl_name, namespace);
            found = true;
        }
    }
    if (!found) {
        auto result = hmap_str_named_import_get(
            &vec_stack_frame_back(&name_stack->stack)->imports,
            source_name
        );
        if (result) {
            add_import(me, name, &result->second);
            found = true;
        }
    }
    if (!found) {
        error_ctx_error(
            my errs,
            import->source_name,
            2305,
            "Module '%s' has no exported member '%.*s'.",
            cstr_str(&import->source_file->absolute_path),
            c_SPAN(import->source_name)
        );

#define BUILTIN_VAR(name, t) \
    static NamedVariable name = { 0 }; \
    name.type = t;

#define BUILTIN_TYPE(name, t) \
    static NamedType name = { 0 }; \
    name.type = t

    BUILTIN_VAR(builtin_var_any, type_any);
    BUILTIN_TYPE(builtin_type_any, type_any);

#undef BUILTIN_VAR
#undef BUILTIN_TYPE

        name_stack_add_variable_s(me, cstr_from_span(import->decl_name), &builtin_var_any);
        name_stack_add_type_s(me, cstr_from_span(import->decl_name), &builtin_type_any);
    }
}

void name_stack_process_imports(NameStack* me) {
    HmapStrNamedImport* imports = &vec_stack_frame_back(&my stack)->imports;
    c_foreach(i, hmap_str_named_import, *imports) {
        add_import(me, i.ref->second.decl_name, &i.ref->second);
    }
    hmap_str_named_import_drop(imports);
    *imports = hmap_str_named_import_init();
}

bool name_stack_has_function(NameStack* me) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        if (i.ref->tag == STACK_FRAME_FUNCTION) {
            return true;
        }
    }
    return false;
}

NamedVariable* name_stack_get_variable(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_named_variable_ptr_get(&i.ref->variables, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NamedType* name_stack_get_type(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_named_type_ptr_get(&i.ref->types, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}

NameStack const* name_stack_get_namespace(NameStack const* me, csview name) {
    c_foreach_rev(i, vec_stack_frame, my stack) {
        auto result = hmap_str_name_stack_cptr_get(&i.ref->namespaces, name);
        if (result) {
            return result->second;
        }
    }
    return nullptr;
}
