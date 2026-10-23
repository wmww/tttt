#include "unused.h"
#include <stdatomic.h>
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

// Lists unused_register() grew out of: a lock-free stack threaded through their first bytes (each holds 16
// candidates at least), freed by unused_free_retired()
static _Atomic(void*) unused_retired;

static void unused_retire(void* block) {
    void* head = atomic_load_explicit(&unused_retired, memory_order_relaxed);
    do *(void**)block = head;
    while (!atomic_compare_exchange_weak_explicit(&unused_retired, &head, block, memory_order_release, memory_order_relaxed));
}

void unused_free_retired(void) {
    void* block = atomic_exchange_explicit(&unused_retired, nullptr, memory_order_acquire);
    while (block) {
        void* next = *(void**)block;
        free(block);
        block = next;
    }
}

void unused_register(Scope const* scope, UnusedKind kind, void const* node) {
    if (!unused_enabled()) return;
    SourceFile* file = scope->file;
    // Ambient declarations are never unused (tsc's unusedIsError)
    if (!file || file->is_declaration || file->is_stdlib || scope->ambient) return;
    if (file->unused_count == file->unused_cap) {
        // (Not realloc() nor free(): the list was allocated by the thread that walked the file, and freeing it takes that
        // thread's malloc arena lock, which another thread may hold for long (growing its heap, an mprotect under the
        // lock): the checkers materializing deferred bodies queued on it, vscode: 14-17% of the check phase's blocked
        // samples. A fresh block is the calling thread's; the old one is retired, freed once checking is over)
        u32 cap = file->unused_cap ? file->unused_cap * 2 : 16;
        UnusedCandidate* grown = malloc(sizeof(UnusedCandidate) * cap);
        panic_if(!grown, "out of memory");
        if (file->unused_count) memcpy(grown, file->unused, sizeof(UnusedCandidate) * file->unused_count);
        if (file->unused) unused_retire(file->unused);
        file->unused = grown;
        file->unused_cap = cap;
    }
    file->unused[file->unused_count++] = (UnusedCandidate){ .kind = kind, .node = node };
}

thread_local u32 unused_marking_suppressed;

void unused_mark_referenced(Scope const* scope, Symbol* found, u32 meaning) {
    if (!found || found->binder != scope->binder || !unused_enabled() || unused_marking_suppressed) return;
    // (Marked already: the walk could only set what is set. Most uses of a name come after its first)
    if ((__atomic_load_n(&found->referenced, __ATOMIC_RELAXED) & meaning) == meaning) return;
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
    u32 order; // The file's index among the checked files (unused_report())
    bool locals; // noUnusedLocals
    bool parameters; // noUnusedParameters
} Reporter;

static bool starts_with_underscore(Span name) {
    return name.size && name.buf[0] == '_';
}

static bool is_unused(Symbol const* symbol) {
    return symbol && symbol->is_local && !symbol->referenced;
}

static Error* report(Reporter const* r, bool parameter, Span at, unsigned code, cstr message) {
    // (Not about a declaration whose parsing reported an error: tsc's checkSourceFileWorker skips a containing node
    // that containsParseError. The diagnostic's own node stands in for the containing one)
    if (!(parameter ? r->parameters : r->locals) || source_file_parse_error_within(at)) {
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_error_s(r->errs, at, code, message);
}

static Error* report_never_read(Reporter const* r, bool parameter, Span at, Span name) {
    return report(r, parameter, at, 6133, cstr_d("'", name, "' is declared but its value is never read."));
}

// A private member this checker saw no read of in a file checked no later than this one: reported, unless another
// checker saw one (program_typecheck() drops it then: Error.unread_member)
static void unread_member(Reporter const* r, Error* error, Span member) {
    if (!error) return;
    error->unread_member = member.buf;
    error->unread_order = r->order;
}

// tsc's isUnreferencedVariableDeclaration: a pattern whose elements all are, a name never read (`excused`: one whose
// underscore excuses it, a parameter's, a binding element's but an object shorthand's; `before_rest`: an object
// element before a rest one, which takes a property out of it)
static bool binding_unreferenced(Binding const* me, bool excused, bool before_rest);

static bool pattern_unreferenced(Binding const* pattern) {
    if (pattern->tag == BINDING_OBJECT) {
        VecObjectBindingItem const* items = &pattern->object;
        bool rest_last = vec_object_binding_item_size(items) && vec_object_binding_item_back(items)->target.rest;
        for_each (i, vec_object_binding_item, items) {
            bool renamed = !span_eq(i->source_span, i->target.content_span);
            if (!binding_unreferenced(&i->target, renamed, rest_last && !i->target.rest)) return false;
        }
    } else {
        for_each (i, vec_binding, &pattern->array) {
            if (!binding_unreferenced(i, true, false)) return false;
        }
    }
    return true;
}

static bool binding_unreferenced(Binding const* me, bool excused, bool before_rest) {
    switch (my tag) {
        case BINDING_ARRAY:
        case BINDING_OBJECT: return pattern_unreferenced(me);
        case BINDING_NOOP: return true;
        case BINDING_IDENTIFIER: break;
    }
    return is_unused(my symbol) && !before_rest && !(excused && starts_with_underscore(my content_span));
}

static void report_binding(Reporter const* r, Binding const* me, bool excused, bool before_rest, bool parameter);

// tsc's reportUnusedBindingElements: all of a pattern's elements (of several) at once, else each
static void report_pattern(Reporter const* r, Binding const* pattern, bool parameter) {
    u32 count = pattern->tag == BINDING_OBJECT ? (u32)vec_object_binding_item_size(&pattern->object) :
        (u32)vec_binding_size(&pattern->array);
    if (count > 1 && pattern_unreferenced(pattern)) {
        report(r, parameter, pattern->content_span, 6198, cstr_from("All destructured elements are unused."));
        return;
    }
    if (pattern->tag == BINDING_OBJECT) {
        VecObjectBindingItem const* items = &pattern->object;
        bool rest_last = vec_object_binding_item_size(items) && vec_object_binding_item_back(items)->target.rest;
        for_each (i, vec_object_binding_item, items) {
            bool renamed = !span_eq(i->source_span, i->target.content_span);
            report_binding(r, &i->target, renamed, rest_last && !i->target.rest, parameter);
        }
    } else {
        for_each (i, vec_binding, &pattern->array) report_binding(r, i, true, false, parameter);
    }
}

// tsc's reportUnusedVariableDeclarations of one declaration
static void report_binding(Reporter const* r, Binding const* me, bool excused, bool before_rest, bool parameter) {
    if (my tag == BINDING_ARRAY || my tag == BINDING_OBJECT) {
        report_pattern(r, me, parameter);
    } else if (my tag == BINDING_IDENTIFIER && binding_unreferenced(me, excused, before_rest)) {
        report_never_read(r, parameter, my content_span, my content_span);
    }
}

// tsc's reportUnusedVariables: `list_span` is where "All variables are unused" goes
static void report_variables(Reporter const* r, VariableStatement const* me, Span list_span, bool for_in_of) {
    bool is_using = my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING;
    bool excused = for_in_of || is_using;
    bool all = true;
    for_each (i, vec_initialized_binding, &my initializers) all &= binding_unreferenced(&i->binding, excused, false);
    if (all && vec_initialized_binding_size(&my initializers) > 1) {
        report(r, false, list_span, 6199, cstr_from("All variables are unused."));
        return;
    }
    for_each (i, vec_initialized_binding, &my initializers) report_binding(r, &i->binding, excused, false, false);
}

// tsc's rangeOfTypeParameters: from the character before the list (`<`, the space before a JSDoc list's first
// `@template`) to one past the first non-whitespace after it (`>`, the `*` of a JSDoc comment's `*/`)
static Span type_parameters_range(Generic const* generic) {
    GenericItem const* first = vec_generic_item_front(&generic->items);
    Span last = vec_generic_item_back(&generic->items)->full_span;
    char const* file_start = cstr_str(&span_source(first->full_span)->contents);
    char const* start = span_start_ptr(first->full_span);
    if (span_valid(first->jsdoc_tag)) {
        start = span_start_ptr(first->jsdoc_tag);
        if (start > file_start) start--;
    } else {
        while (start > file_start && *start != '<') start--;
    }
    char const* end = span_end_ptr(last);
    if (span_valid(first->jsdoc_tag)) {
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
    } else {
        while (*end && *end != '>') end++;
    }
    if (*end) end++;
    return span_make_in(first->full_span, start, end);
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

// tsc's checkUnusedTypeParameters: of every declaration (a class or interface's merged declarations, `owner`, share
// their type parameters: one used in any of them is used), the whole list when it has several and none is used
static void report_type_parameters(Reporter const* r, Generic const* generic, Symbol const* owner) {
    if (generic_empty(generic)) return;
    // (Not when the declarations are in several files: tsc's allDeclarationsInSameSourceFile)
    for (u32 i = 1; owner && i < owner->decl_count; i++) {
        if (span_source(owner->decls[i].full) != span_source(owner->decls[0].full)) return;
    }
    u32 unused_count = 0;
    for_each (i, vec_generic_item, &generic->items) {
        if (type_parameter_is_unused(i, owner, i_index)) unused_count++;
    }
    if (!unused_count) return;
    isize count = vec_generic_item_size(&generic->items);
    if (count > 1 && unused_count == (u32)count) {
        report(r, true, type_parameters_range(generic), 6205, cstr_from("All type parameters are unused."));
        return;
    }
    for_each (i, vec_generic_item, &generic->items) {
        if (type_parameter_is_unused(i, owner, i_index)) {
            report(r, true, i->name, 6196, cstr_d("'", i->name, "' is declared but never used."));
        }
    }
}

static void report_function(Reporter const* r, FunctionImpl const* me) {
    report_type_parameters(r, &my generic, nullptr);
    if (!my has_body || my is_type) return;
    for_each (i, vec_function_arg, &my args) {
        Binding const* binding = &i->initializer.binding;
        if (i->is_parameter_property) continue;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        report_binding(r, binding, true, false, true);
    }
}

// A local declaration with an exported one of the same name in its block: one symbol to tsc (the export's local
// symbol, which the local declaration merges into), never unused (checkUnusedLocalsAndParameters skips a local with an
// exportSymbol): `export const K = {...}; type K = (typeof K)[keyof typeof K]`
static bool merged_with_export(Symbol const* symbol) {
    if (!symbol->decl_count) return false;
    Scope const* block = symbol->decls[0].block;
    Symbol const* container = block ? block->container : nullptr;
    if (!container || !container->exports) return false;
    Symbol const* exported = symbol_table_get(container->exports, symbol->name);
    return exported && exported != symbol;
}

static void report_declaration(Reporter const* r, Symbol const* symbol, Span name, bool is_type) {
    if (!is_unused(symbol) || merged_with_export(symbol)) return;
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
    if (count > 1 && unused_count == count) {
        report(r, false, statement_span, 6192, cstr_from("All imports in import declaration are unused."));
        return;
    }
    for (u32 i = 0; i < unused_count; i++) {
        // An `import type` clause's names are type declarations (tsc's isTypeDeclaration), not its namespace import:
        // `import type * as N` and an inline `{ type A }` are 6133
        report_declaration(r, unused[i]->symbol, unused[i]->decl_name,
            main->type_only && unused[i]->tag != PORT_IMPORT_GATHER);
    }
}

static void report_statement(Reporter const* r, Statement const* me) {
    if (statement_declares_name(my tag) && my decl.props.declared) return; // Ambient
    switch (my tag) {
        case STATEMENT_DECL_VARIABLE: {
            // (At the declaration list, tsgo's reportUnusedVariables: the statement without its semicolon)
            Span list = span_valid(my span) ? my span : my decl.variable.span;
            if (span_valid(list) && list.size && list.buf[list.size - 1] == ';') {
                list.size--;
                while (list.size && (list.buf[list.size - 1] == ' ' || list.buf[list.size - 1] == '\t')) list.size--;
            }
            report_variables(r, &my decl.variable, list, false);
            break;
        }
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

// The `{@link X}` tags of the JSDoc comments right before a statement in a block, resolved where the statement is: a
// local only a link names is used (tsc resolves the link names of the node the JSDoc belongs to, from there). The
// file's own statements' links are unused_mark_jsdoc_links()'s, which looks from the file's scope
void unused_mark_leading_jsdoc_links(Scope const* names, char const* statement_start, char const* file_start) {
    if (!unused_enabled() || !statement_start) return;
    char const* at = statement_start;
    while (true) {
        while (at > file_start && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\n' || at[-1] == '\r')) at--;
        if (at - file_start < 5 || at[-1] != '/' || at[-2] != '*') return;
        char const* open = at - 2;
        while (open > file_start && !(open[0] == '/' && open[1] == '*')) open--;
        if (open[0] != '/' || open[1] != '*') return;
        bool jsdoc = open[2] == '*' && open[3] != '/';
        for (char const* p = open; jsdoc && p + 5 < at; p++) {
            if (memcmp(p, "@link", 5)) continue;
            char const* name = p + 5;
            if (!strncmp(name, "code", 4)) name += 4;
            else if (!strncmp(name, "plain", 5)) name += 5;
            if (*name != ' ' && *name != '\t') continue;
            while (*name == ' ' || *name == '\t') name++;
            char const* end = name;
            while (end < at && is_identifier_char(*end)) end++;
            if (end == name) continue;
            csview sv = csview_from_n(name, end - name);
            unused_mark_referenced(names, scope_resolve(names, sv, SYMBOL_ANY_MEANING), SYMBOL_ANY_MEANING);
        }
        at = open;
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
                // (Not a `declare` member: tsc's parser gives it NodeFlags.Ambient, which checkUnusedClassMembers skips)
                if (i->tag == CLASS_ITEM_METHOD ? i->method.is_declare : i->field.is_declare) break;
                // An accessor pair is reported on the getter
                if (i->tag == CLASS_ITEM_METHOD && i->method.accessor == CLASS_ACCESSOR_SET && ty_class_accessor_getter(checker, i)) break;
                if (ty_member_referenced_order(checker, i->span) <= r->order) break;
                unread_member(r, report_never_read(r, false, name, name), i->span);
            }   break;
            case CLASS_ITEM_CONSTRUCTOR:
                for_each (arg, vec_function_arg, &i->constructor.func.args) {
                    if (!arg->is_parameter_property || arg->access != ACCESS_MODIFIER_PRIVATE) continue;
                    if (ty_member_referenced_order(checker, arg->span) <= r->order) continue;
                    Span name = arg->initializer.binding.content_span;
                    unread_member(r, report(r, false, name, 6138, cstr_d("Property '", name, "' is declared but its value is never read.")), arg->span);
                }
                break;
            default:
                break;
        }
    }
}

void unused_report(SourceFile const* file, ErrorCtx* errs, u32 order) {
    CompilerOptions const* options = compiler_options();
    Reporter r = { .errs = errs, .order = order, .locals = options->no_unused_locals, .parameters = options->no_unused_parameters };
    for (u32 i = 0; i < file->unused_count; i++) {
        UnusedCandidate const* candidate = &file->unused[i];
        switch (candidate->kind) {
            case UNUSED_STATEMENT: report_statement(&r, candidate->node); break;
            case UNUSED_VARIABLES: report_variables(&r, candidate->node, span_null, false); break;
            // (The span of the loop's variable statement)
            case UNUSED_FOR_IN_OF_VARIABLES:
                report_variables(&r, candidate->node, ((Statement const*)candidate->node)->span, true);
                break;
            case UNUSED_FUNCTION: report_function(&r, candidate->node); break;
            case UNUSED_CLASS: {
                ClassDecl const* decl = candidate->node;
                report_class_members(&r, decl);
                report_type_parameters(&r, &decl->generic, decl->symbol);
            }   break;
            case UNUSED_INTERFACE: {
                InterfaceDecl const* decl = candidate->node;
                report_type_parameters(&r, &decl->generic, decl->symbol);
            }   break;
            case UNUSED_TYPE_ALIAS: report_type_parameters(&r, &((TypeDecl const*)candidate->node)->generic, nullptr); break;
            case UNUSED_INFER: {
                InferredType const* infer = candidate->node;
                if (infer->owned_symbol && !(infer->owned_symbol->referenced & SYMBOL_TYPE_PARAMETER) &&
                    !starts_with_underscore(infer->name)
                ) {
                    report(&r, true, infer->name, 6196, cstr_d("'", infer->name, "' is declared but never used."));
                }
            }   break;
        }
    }
}
