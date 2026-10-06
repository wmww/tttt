#include "unused.h"
#include "scope.h"
#include "statement.h"
#include "port_statement.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "several_types.h"
#include "source_file.h"
#include "error.h"
#include "compiler_options.h"
#include "interface_decl.h"
#include "ty/ty.h"

bool unused_enabled(void) {
    CompilerOptions const* options = compiler_options();
    return options->no_unused_locals || options->no_unused_parameters;
}

void unused_register(Scope const* scope, UnusedKind kind, void const* node, Span span) {
    if (!unused_enabled()) return;
    SourceFile* file = scope->file;
    // Ambient declarations are never unused (tsc's unusedIsError)
    if (!file || file->is_declaration || file->is_stdlib || scope->ambient) return;
    if (file->unused_count == file->unused_cap) {
        file->unused_cap = file->unused_cap ? file->unused_cap * 2 : 16;
        file->unused = realloc(file->unused, sizeof(UnusedCandidate) * file->unused_cap);
        panic_if(!file->unused, "out of memory");
    }
    file->unused[file->unused_count++] = (UnusedCandidate){ .kind = kind, .node = node, .span = span };
}

void unused_mark_referenced(Scope const* scope, Symbol* found, u32 meaning) {
    if (!found || found->binder != scope->binder || !unused_enabled()) return;
    // tsc's lastSelfReferenceLocation: the outermost declaration the use is in, below the scope that has the symbol
    Symbol const* self = nullptr;
    for (Scope const* i = scope; i; i = i->parent) {
        if (scope_declares_symbol(i, found)) break;
        if (i->self) self = i->self;
    }
    // (Atomic: files are resolved on several threads, and a global is anyone's, plan 45)
    if (self != found) __atomic_fetch_or(&found->referenced, meaning, __ATOMIC_RELAXED);
}

// Reporting

typedef struct Reporter {
    ErrorCtx* errs;
    bool locals; // noUnusedLocals
    bool parameters; // noUnusedParameters
} Reporter;

static bool starts_with_underscore(Span name) {
    return name.sv.size && name.sv.buf[0] == '_';
}

static bool is_unused(Symbol const* symbol) {
    return symbol && symbol->is_local && !symbol->referenced;
}

static void report(Reporter const* r, bool parameter, Span at, unsigned code, cstr message) {
    // (Not about a declaration whose parsing reported an error: tsc's checkSourceFileWorker skips a containing node
    // that containsParseError. The diagnostic's own node stands in for the containing one)
    if (!(parameter ? r->parameters : r->locals) || source_file_parse_error_within(at)) {
        cstr_drop(&message);
        return;
    }
    error_ctx_error_s(r->errs, at, code, message);
}

static void report_never_read(Reporter const* r, bool parameter, Span at, Span name) {
    report(r, parameter, at, 6133, cstr_d("'", name, "' is declared but its value is never read."));
}

// tsc's bindingNameText: the name of an identifier, the first name in a pattern
static Span binding_name(Binding const* me) {
    switch (my tag) {
        case BINDING_IDENTIFIER: return my content_span;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                if (i->tag != BINDING_NOOP) return binding_name(i);
            }
            break;
        case BINDING_OBJECT:
            if (vec_object_binding_item_size(&my object)) return binding_name(&vec_object_binding_item_front(&my object)->target);
            break;
        case BINDING_NOOP: break;
    }
    return my content_span;
}

// Where the declarations of a variable statement or parameter list that are unused altogether are collected
typedef struct UnusedDecls {
    Binding const* items[64];
    u32 count;
    bool overflow;
} UnusedDecls;

static void unused_decls_push(UnusedDecls* me, Binding const* binding) {
    if (my count == (u32)c_countof(my items)) {
        my overflow = true;
        return;
    }
    my items[my count++] = binding;
}

// The elements of a pattern: tsc groups the unused ones by pattern (unusedDestructures). `decls`: the pattern is a
// variable's, whose declaration counts as unused altogether if its only element is unused
static void report_pattern(Reporter const* r, Binding const* pattern, bool parameter, UnusedDecls* decls) {
    Binding const* unused[64];
    u32 unused_count = 0;
    u32 element_count = 0;
    bool overflow = false;
    if (pattern->tag == BINDING_OBJECT) {
        VecObjectBindingItem const* items = &pattern->object;
        bool rest_last = vec_object_binding_item_size(items) && vec_object_binding_item_back(items)->target.rest;
        for_each (i, vec_object_binding_item, items) {
            element_count++;
            Binding const* target = &i->target;
            if (target->tag == BINDING_ARRAY || target->tag == BINDING_OBJECT) {
                report_pattern(r, target, parameter, nullptr);
                continue;
            }
            if (target->tag != BINDING_IDENTIFIER || !is_unused(target->symbol)) continue;
            // `{ a: _a }` is fine (a renamed one only: `{ _a }` isn't)
            bool renamed = !span_eq(i->source_span, target->content_span);
            if (renamed && starts_with_underscore(target->content_span)) continue;
            // In `{ a, ...b }`, `a` is used: it takes a property out of `b`
            if (rest_last && !target->rest) continue;
            if (unused_count < (u32)c_countof(unused)) unused[unused_count++] = target; else overflow = true;
        }
    } else if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) {
            element_count++;
            if (i->tag == BINDING_ARRAY || i->tag == BINDING_OBJECT) {
                report_pattern(r, i, parameter, nullptr);
                continue;
            }
            if (i->tag != BINDING_IDENTIFIER || !is_unused(i->symbol) || starts_with_underscore(i->content_span)) continue;
            // A variable's array elements are reported one by one, a parameter's are grouped
            if (!parameter) {
                report_never_read(r, false, i->content_span, i->content_span);
                continue;
            }
            if (unused_count < (u32)c_countof(unused)) unused[unused_count++] = i; else overflow = true;
        }
    }
    if (!unused_count || overflow) return;
    if (unused_count == element_count) {
        if (unused_count == 1 && decls) {
            unused_decls_push(decls, pattern);
        } else if (unused_count == 1) {
            report_never_read(r, parameter, pattern->content_span, binding_name(unused[0]));
        } else {
            report(r, parameter, pattern->content_span, 6198, cstr_from("All destructured elements are unused."));
        }
        return;
    }
    for (u32 i = 0; i < unused_count; i++) {
        report_never_read(r, parameter, unused[i]->content_span, unused[i]->content_span);
    }
}

// tsc's unusedVariables: `list_span` is where "All variables are unused" goes
static void report_variables(Reporter const* r, VariableStatement const* me, Span list_span, bool for_in_of) {
    UnusedDecls decls = {0};
    bool is_using = my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING;
    for_each (i, vec_initialized_binding, &my initializers) {
        Binding const* binding = &i->binding;
        if (binding->tag == BINDING_IDENTIFIER) {
            if (!is_unused(binding->symbol)) continue;
            if ((for_in_of || is_using) && starts_with_underscore(binding->content_span)) continue;
            unused_decls_push(&decls, binding);
        } else {
            report_pattern(r, binding, false, &decls);
        }
    }
    if (!decls.count || decls.overflow) return;
    if (decls.count == (u32)vec_initialized_binding_size(&my initializers)) {
        if (decls.count == 1) {
            report_never_read(r, false, decls.items[0]->content_span, binding_name(decls.items[0]));
        } else {
            report(r, false, list_span, 6199, cstr_from("All variables are unused."));
        }
        return;
    }
    for (u32 i = 0; i < decls.count; i++) {
        report_never_read(r, false, decls.items[i]->content_span, binding_name(decls.items[i]));
    }
}

// The `<...>` of a type parameter list (tsc's rangeOfTypeParameters)
static Span type_parameters_range(Generic const* generic) {
    Span first = vec_generic_item_front(&generic->items)->full_span;
    Span last = vec_generic_item_back(&generic->items)->full_span;
    char const* start = span_start_ptr(first);
    char const* end = span_end_ptr(last);
    char const* file_start = cstr_str(&first.source->contents);
    while (start > file_start && *start != '<') start--;
    while (*end && *end != '>') end++;
    if (*end) end++;
    return span_make(first.source, start, end);
}

// The type parameters of a class or interface declaration
static Generic const* declaration_generic(SymbolDecl const* decl) {
    switch (decl->kind) {
        case SYMBOL_DECL_CLASS: return &((ClassDecl const*)decl->node)->generic;
        case SYMBOL_DECL_INTERFACE: return &((InterfaceDecl const*)decl->node)->generic;
        default: return nullptr;
    }
}

// `owner`: the class or interface whose declarations share type parameters (they are one symbol in tsc)
static bool type_parameter_is_unused(GenericItem const* me, Symbol const* owner, isize index) {
    if (!my owned_symbol || my owned_symbol->referenced & SYMBOL_TYPE_PARAMETER || starts_with_underscore(my name)) {
        return false;
    }
    for (u32 i = 0; owner && i < owner->decl_count; i++) {
        Generic const* generic = declaration_generic(&owner->decls[i]);
        if (!generic || index >= vec_generic_item_size(&generic->items)) continue;
        Symbol const* other = vec_generic_item_at(&generic->items, index)->owned_symbol;
        if (other && other->referenced & SYMBOL_TYPE_PARAMETER) return false;
    }
    return true;
}

// tsc's checkUnusedTypeParameters. Of a class or interface (`owner`) only the last declaration reports them: all of
// them can use them
static void report_type_parameters(Reporter const* r, Generic const* generic, Symbol const* owner, void const* node) {
    if (generic_empty(generic)) return;
    if (owner && owner->decls[owner->decl_count - 1].node != node) return;
    u32 unused_count = 0;
    for_each (i, vec_generic_item, &generic->items) {
        if (type_parameter_is_unused(i, owner, i_index)) unused_count++;
    }
    if (!unused_count) return;
    if (span_valid(vec_generic_item_front(&generic->items)->jsdoc_tag)) {
        // JSDoc: each `@template` tag is a list of its own, reported whole when none of its type parameters is used
        for_each (i, vec_generic_item, &generic->items) {
            if (!type_parameter_is_unused(i, owner, i_index)) continue;
            u32 in_tag = 0, unused_in_tag = 0;
            for_each (j, vec_generic_item, &generic->items) {
                if (!span_eq(j->jsdoc_tag, i->jsdoc_tag)) continue;
                in_tag++;
                if (type_parameter_is_unused(j, owner, j_index)) unused_in_tag++;
            }
            if (unused_in_tag < in_tag) {
                report_never_read(r, true, i->full_span, i->name);
                continue;
            }
            // (Once per tag: at its first type parameter)
            bool first = true;
            for (GenericItem const* j = generic->items.data; j < i && first; j++) first = !span_eq(j->jsdoc_tag, i->jsdoc_tag);
            if (!first) continue;
            if (in_tag == 1) report_never_read(r, true, i->jsdoc_tag, i->name);
            else report(r, true, i->jsdoc_tag, 6205, cstr_from("All type parameters are unused."));
        }
        return;
    }
    if (unused_count == (u32)vec_generic_item_size(&generic->items)) {
        Span range = type_parameters_range(generic);
        if (unused_count == 1) {
            report_never_read(r, true, range, vec_generic_item_front(&generic->items)->name);
        } else {
            report(r, true, range, 6205, cstr_from("All type parameters are unused."));
        }
        return;
    }
    for_each (i, vec_generic_item, &generic->items) {
        if (type_parameter_is_unused(i, owner, i_index)) {
            report_never_read(r, true, i->full_span, i->name);
        }
    }
}

static void report_function(Reporter const* r, FunctionImpl const* me) {
    // Of overloads only the last declaration reports its type parameters (a class method's overload signatures have an
    // implementation after them)
    bool last = my symbol ? my symbol->decls[my symbol->decl_count - 1].node == me : !function_impl_rare(me)->overload_implementation;
    if (last) report_type_parameters(r, &my generic, nullptr, nullptr);
    if (!my has_body || my is_type) return;
    for_each (i, vec_function_arg, &my args) {
        Binding const* binding = &i->initializer.binding;
        if (i->is_parameter_property) continue;
        if (binding->tag == BINDING_IDENTIFIER) {
            if (!is_unused(binding->symbol) || starts_with_underscore(binding->content_span) ||
                span_equals(binding->content_span, "this")
            ) {
                continue;
            }
            report_never_read(r, true, binding->content_span, binding->content_span);
        } else {
            report_pattern(r, binding, true, nullptr);
        }
    }
}

static void report_declaration(Reporter const* r, Symbol const* symbol, Span name, bool is_type) {
    if (!is_unused(symbol)) return;
    if (is_type) {
        report(r, false, name, 6196, cstr_d("'", name, "' is declared but never used."));
    } else {
        report_never_read(r, false, name, name);
    }
}

// tsc's unusedImports
static void report_imports(Reporter const* r, PortStatement const* me, Span statement_span) {
    PortItem const* unused[64];
    u32 unused_count = 0;
    u32 count = 0;
    PortItem const* main = &my main;
    for (isize i = -1; i < vec_port_item_size(&my items); i++) {
        PortItem const* item = i < 0 ? main : vec_port_item_at(&my items, i);
        if (item->tag == PORT_NOOP || port_item_is_export(item) || item->export_import) continue;
        count++;
        if (item->duplicate || !is_unused(item->symbol)) continue;
        // `import x = N.y` and `import x = require("m")` aren't grouped, and underscores don't excuse them
        if (item->tag == PORT_IMPORT_ALIAS || item->is_require) {
            report_never_read(r, false, item->decl_name, item->decl_name);
            count--;
            continue;
        }
        if (starts_with_underscore(item->decl_name)) continue;
        if (unused_count == (u32)c_countof(unused)) return;
        unused[unused_count++] = item;
    }
    if (!unused_count) return;
    if (unused_count == count) {
        if (unused_count == 1) {
            report_never_read(r, false, statement_span, unused[0]->decl_name);
        } else {
            report(r, false, statement_span, 6192, cstr_from("All imports in import declaration are unused."));
        }
        return;
    }
    for (u32 i = 0; i < unused_count; i++) {
        // A type-only import is a type declaration
        report_declaration(r, unused[i]->symbol, unused[i]->decl_name, main->type_only);
    }
}

static void report_statement(Reporter const* r, Statement const* me) {
    if (statement_declares_name(my tag) && my decl.props.declared) return; // Ambient
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE:
            report_variables(r, &my decl.variable, span_valid(my span) ? my span : my decl.variable.span, false);
            break;
        case STATEMENT_DECL_FUNCTION:
            report_declaration(r, my decl.function.impl->symbol, my decl.function.impl->name, false);
            break;
        case STATEMENT_DECL_CLASS:
            report_declaration(r, my decl.class->symbol, my decl.class->name, true);
            break;
        case STATEMENT_DECL_INTERFACE:
            report_declaration(r, my decl.interface->symbol, my decl.interface->name, true);
            break;
        case STATEMENT_DECL_TYPE:
            report_declaration(r, my decl.type->symbol, my decl.type->name, true);
            break;
        case STATEMENT_DECL_ENUM:
            report_declaration(r, my decl.enum_->symbol, my decl.enum_->name, true);
            break;
        case STATEMENT_DECL_NAMESPACE:
            if (!my decl.namespace->is_global && !my decl.namespace->is_ambient_module) {
                report_declaration(r, my decl.namespace->symbol, my decl.namespace->name, false);
            }
            break;
        case STATEMENT_IMPORT_EXPORT:
            report_imports(r, my import_export, my span);
            break;
        default:
            break;
    }
}

static bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
        (unsigned char)c >= 0x80;
}

// HACK: tsc resolves the names of JSDoc `{@link X}` tags, which makes them uses. tttt keeps no JSDoc tags (they are
// parsed on demand for their host, jsdoc_before()), so the source is scanned for the tags and their first names are looked up from the file's scope (not from where the
// comment is: a local only a link names still counts as unused)
void unused_mark_jsdoc_links(SourceFile const* file) {
    if (!unused_enabled()) return;
    char const* text = cstr_str(&file->contents);
    for (char const* at = strstr(text, "@link"); at; at = strstr(at + 1, "@link")) {
        char const* name = at + 5;
        if (!strncmp(name, "code", 4)) name += 4;
        else if (!strncmp(name, "plain", 5)) name += 5;
        if (*name != ' ' && *name != '\t') continue;
        while (*name == ' ' || *name == '\t') name++;
        char const* end = name;
        while (is_identifier_char(*end)) end++;
        if (end == name) continue;
        csview sv = csview_from_n(name, end - name);
        unused_mark_referenced(file->scope, scope_resolve(file->scope, sv, SYMBOL_ANY_MEANING), SYMBOL_ANY_MEANING);
    }
}

// tsc's checkUnusedClassMembers: private members nothing read (ty_mark_member_referenced())
static void report_class_members(Reporter const* r, ClassDecl const* me) {
    Checker* checker = current_checker;
    for_each (i, vec_class_item, &my items) {
        switch (i->tag) {
            case CLASS_ITEM_METHOD:
            case CLASS_ITEM_FIELD: {
                Span name = i->tag == CLASS_ITEM_METHOD ? i->method.name_span : i->field.name_span;
                bool private_name = span_valid(class_item_private_name(i));
                if (i->access != ACCESS_MODIFIER_PRIVATE && !private_name) break;
                // An accessor pair is reported on the getter
                if (i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_SET && i->method.accessor_getter) break;
                if (ty_member_is_referenced(checker, i->span)) break;
                report_never_read(r, false, name, name);
            }   break;
            case CLASS_ITEM_CONSTRUCTOR:
                for_each (arg, vec_function_arg, &i->constructor.func.args) {
                    if (!arg->is_parameter_property || arg->access != ACCESS_MODIFIER_PRIVATE) continue;
                    if (ty_member_is_referenced(checker, arg->span)) continue;
                    Span name = arg->initializer.binding.content_span;
                    report(r, false, name, 6138, cstr_d("Property '", name, "' is declared but its value is never read."));
                }
                break;
            default:
                break;
        }
    }
}

void unused_report(SourceFile const* file, ErrorCtx* errs) {
    CompilerOptions const* options = compiler_options();
    Reporter r = { .errs = errs, .locals = options->no_unused_locals, .parameters = options->no_unused_parameters };
    for (u32 i = 0; i < file->unused_count; i++) {
        UnusedCandidate const* candidate = &file->unused[i];
        switch (candidate->kind) {
            case UNUSED_STATEMENT: report_statement(&r, candidate->node); break;
            case UNUSED_VARIABLES: report_variables(&r, candidate->node, candidate->span, false); break;
            case UNUSED_FOR_IN_OF_VARIABLES: report_variables(&r, candidate->node, candidate->span, true); break;
            case UNUSED_FUNCTION: report_function(&r, candidate->node); break;
            case UNUSED_CLASS: {
                ClassDecl const* decl = candidate->node;
                report_class_members(&r, decl);
                report_type_parameters(&r, &decl->generic, decl->symbol, decl);
            }   break;
            case UNUSED_INTERFACE: {
                InterfaceDecl const* decl = candidate->node;
                report_type_parameters(&r, &decl->generic, decl->symbol, decl);
            }   break;
            case UNUSED_TYPE_ALIAS: report_type_parameters(&r, &((TypeDecl const*)candidate->node)->generic, nullptr, nullptr); break;
            case UNUSED_INFER: {
                InferredType const* infer = candidate->node;
                if (infer->owned_symbol && !(infer->owned_symbol->referenced & SYMBOL_TYPE_PARAMETER) &&
                    !starts_with_underscore(infer->name)
                ) {
                    report_never_read(&r, true, infer->full_span, infer->name);
                }
            }   break;
        }
    }
}
