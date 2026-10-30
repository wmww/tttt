#define _GNU_SOURCE // memrchr
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
#include "unicode_identifiers.h"
#include "ty/ty.h"

// The last c in [s, s + n), or null: glibc's memrchr() (vectorized), a loop where libc has none (macOS)
static inline char const* last_byte(char const* s, char c, size_t n) {
#ifdef __APPLE__
    while (n) if (s[--n] == c) return s + n;
    return nullptr;
#else
    return memrchr(s, c, n);
#endif
}

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
    // (Nor in what tsc never checks, where its names aren't uses either: only what checking visits registers, tsc's
    // registerForUnusedIdentifiersCheck)
    if (unused_marking_suppressed) return;
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
    u64 hash = symbol_name_hash(found->name);
    for (Scope const* i = scope; i; i = i->parent) {
        if (scope_declares_symbol_hashed(i, found, hash)) break;
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

// `location`: tsgo's node reportUnused is given (the declaration, a variable's or parameter's for its binding
// elements, the owner of type parameters), skipped when a node the parser flagged ThisNodeHasError is inside it (a
// parse error right before it or inside it: source_file_error_node_within())
static Error* report(Reporter const* r, bool parameter, Span location, Span at, unsigned code, cstr message) {
    if (!(parameter ? r->parameters : r->locals) || (span_valid(location) &&
        source_file_error_node_within(span_source(location), span_start_ptr(location), span_end_ptr(location)))
    ) {
        cstr_drop(&message);
        return nullptr;
    }
    return error_ctx_error_s(r->errs, at, code, message);
}

static Error* report_never_read(Reporter const* r, bool parameter, Span location, Span at, Span name) {
    return report(r, parameter, location, at, 6133, cstr_d("'", name, "' is declared but its value is never read."));
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

static void report_binding(Reporter const* r, Binding const* me, bool excused, bool before_rest, bool parameter, Span root);

// tsc's reportUnusedBindingElements: all of a pattern's elements (of several) at once, else each
// `root`: the variable declaration or parameter the pattern is of (tsgo's reportUnusedVariable location)
static void report_pattern(Reporter const* r, Binding const* pattern, bool parameter, Span root) {
    u32 count = pattern->tag == BINDING_OBJECT ? (u32)vec_object_binding_item_size(&pattern->object) :
        (u32)vec_binding_size(&pattern->array);
    if (count > 1 && pattern_unreferenced(pattern)) {
        report(r, parameter, root, pattern->content_span, 6198, cstr_from("All destructured elements are unused."));
        return;
    }
    if (pattern->tag == BINDING_OBJECT) {
        VecObjectBindingItem const* items = &pattern->object;
        bool rest_last = vec_object_binding_item_size(items) && vec_object_binding_item_back(items)->target.rest;
        for_each (i, vec_object_binding_item, items) {
            bool renamed = !span_eq(i->source_span, i->target.content_span);
            report_binding(r, &i->target, renamed, rest_last && !i->target.rest, parameter, root);
        }
    } else {
        for_each (i, vec_binding, &pattern->array) report_binding(r, i, true, false, parameter, root);
    }
}

// tsc's reportUnusedVariableDeclarations of one declaration
static void report_binding(Reporter const* r, Binding const* me, bool excused, bool before_rest, bool parameter, Span root) {
    if (my tag == BINDING_ARRAY || my tag == BINDING_OBJECT) {
        report_pattern(r, me, parameter, root);
    } else if (my tag == BINDING_IDENTIFIER && binding_unreferenced(me, excused, before_rest)) {
        report_never_read(r, parameter, root, my content_span, my content_span);
    }
}

// A variable declaration's node: its name to the end of its type or initializer
static Span declaration_extent(InitializedBinding const* me) {
    Span span = my binding.content_span;
    if (!span_valid(span)) return span;
    char const* end = span_end_ptr(span);
    Span type = binding_explicit_type_span(&my binding);
    if (span_valid(type) && span_source(type) == span_source(span) && span_end_ptr(type) > end) end = span_end_ptr(type);
    Expr const* value = initialized_binding_value(me);
    Span value_span = value ? expr_outer_span(value) : span_null;
    if (span_valid(value_span) && span_source(value_span) == span_source(span) && span_end_ptr(value_span) > end) {
        end = span_end_ptr(value_span);
    }
    return span_make_in(span, span_start_ptr(span), end);
}

// tsc's reportUnusedVariables: `list_span` is where "All variables are unused" goes
static void report_variables(Reporter const* r, VariableStatement const* me, Span list_span, bool for_in_of) {
    bool is_using = my keyword == VAR_INIT_USING || my keyword == VAR_INIT_AWAIT_USING;
    bool excused = for_in_of || is_using;
    bool all = true;
    for_each (i, vec_initialized_binding, &my initializers) all &= binding_unreferenced(&i->binding, excused, false);
    if (all && vec_initialized_binding_size(&my initializers) > 1) {
        report(r, false, list_span, list_span, 6199, cstr_from("All variables are unused."));
        return;
    }
    for_each (i, vec_initialized_binding, &my initializers) {
        report_binding(r, &i->binding, excused, false, false, declaration_extent(i));
    }
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
// `location`: the declaration they are of
static void report_type_parameters(Reporter const* r, Generic const* generic, Symbol const* owner, Span location) {
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
        report(r, true, location, type_parameters_range(generic), 6205, cstr_from("All type parameters are unused."));
        return;
    }
    for_each (i, vec_generic_item, &generic->items) {
        if (type_parameter_is_unused(i, owner, i_index)) {
            report(r, true, location, i->name, 6196, cstr_d("'", i->name, "' is declared but never used."));
        }
    }
}

static void report_function(Reporter const* r, FunctionImpl const* me) {
    report_type_parameters(r, &my generic, nullptr, my full_span);
    if (!my has_body || my is_type) return;
    for_each (i, vec_function_arg, &my args) {
        Binding const* binding = &i->initializer.binding;
        // (A property only in a constructor: elsewhere it's a parameter, TS2369, tsc's isParameterPropertyDeclaration)
        if (i->is_parameter_property && my is_constructor) continue;
        if (binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
        report_binding(r, binding, true, false, true, i->span);
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

static void report_declaration(Reporter const* r, Symbol const* symbol, Span location, Span name, bool is_type) {
    if (!is_unused(symbol) || merged_with_export(symbol)) return;
    if (is_type) {
        report(r, false, location, name, 6196, cstr_d("'", name, "' is declared but never used."));
    } else {
        report_never_read(r, false, location, name, name);
    }
}

// tsc's unusedImports
static void report_imports(Reporter const* r, PortStatement const* me, Span statement_span) {
    PortItem const* unused[64];
    u32 unused_count = 0;
    u32 count = 0;
    PortItem const* main = &my main;
    // The import clause (tsgo's location of TS6192): its first name to its last, and the `}` after it
    char const* clause_start = nullptr;
    char const* clause_end = nullptr;
    for (isize i = -1; i < vec_port_item_size(&my items); i++) {
        PortItem const* item = i < 0 ? main : vec_port_item_at(&my items, i);
        if (item->tag == PORT_NOOP || port_item_is_export(item) || item->export_import) continue;
        // (The main item's full span is the statement's: its name is its node)
        Span extent = item == main ? item->decl_name : item->full_span;
        if (span_valid(extent) && span_source(extent) == span_source(statement_span)) {
            if (!clause_start || span_start_ptr(extent) < clause_start) clause_start = span_start_ptr(extent);
            if (!clause_end || span_end_ptr(extent) > clause_end) clause_end = span_end_ptr(extent);
        }
        count++;
        if (item->duplicate || !is_unused(item->symbol)) continue;
        // `import x = N.y` and `import x = require("m")` aren't grouped, and underscores don't excuse them
        if (item->tag == PORT_IMPORT_ALIAS || item->is_require) {
            report_never_read(r, false, statement_span, item->decl_name, item->decl_name);
            count--;
            continue;
        }
        if (starts_with_underscore(item->decl_name)) continue;
        if (unused_count == (u32)c_countof(unused)) return;
        unused[unused_count++] = item;
    }
    if (!unused_count) return;
    if (count > 1 && unused_count == count) {
        Span clause = span_null;
        if (clause_start) {
            char const* end = clause_end;
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r' || *end == ',') end++;
            if (*end == '}') clause_end = end + 1;
            char const* file_start = cstr_str(&span_source(statement_span)->contents);
            char const* start = clause_start;
            while (start > file_start && (start[-1] == ' ' || start[-1] == '\t' || start[-1] == '\n' || start[-1] == '\r')) start--;
            if (start > file_start && start[-1] == '{') clause_start = start - 1;
            clause = span_make_in(statement_span, clause_start, clause_end);
        }
        report(r, false, clause, statement_span, 6192, cstr_from("All imports in import declaration are unused."));
        return;
    }
    for (u32 i = 0; i < unused_count; i++) {
        // An `import type` clause's names are type declarations (tsc's isTypeDeclaration), not its namespace import:
        // `import type * as N` and an inline `{ type A }` are 6133
        report_declaration(r, unused[i]->symbol, unused[i]->full_span, unused[i]->decl_name,
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
            report_declaration(r, my decl.function.impl->symbol, my span, my decl.function.impl->name, false);
            break;
        case STATEMENT_DECL_CLASS:
            report_declaration(r, my decl.class->symbol, my span, my decl.class->name, true);
            break;
        case STATEMENT_DECL_INTERFACE:
            report_declaration(r, my decl.interface->symbol, my span, my decl.interface->name, true);
            break;
        case STATEMENT_DECL_TYPE:
            report_declaration(r, my decl.type->symbol, my span, my decl.type->name, true);
            break;
        case STATEMENT_DECL_ENUM:
            report_declaration(r, my decl.enum_->symbol, my span, my decl.enum_->name, true);
            break;
        case STATEMENT_DECL_NAMESPACE:
            if (!my decl.namespace->is_global && !my decl.namespace->is_ambient_module) {
                report_declaration(r, my decl.namespace->symbol, my span, my decl.namespace->name, false);
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

static bool is_identifier_start_char(char c) {
    return is_identifier_char(c) && !(c >= '0' && c <= '9');
}

bool unused_file_has_link_tag(SourceFile const* file) {
    SourceFile* f = (SourceFile*)file;
    // (Checkers ask concurrently: a relaxed atomic byte, every writer storing the same answer)
    uint8_t known = __atomic_load_n(&f->has_link_tag, __ATOMIC_RELAXED);
    if (!known) {
        // (memchr for the '@' at vector speed, then compare: far fewer instructions than strstr over a megabyte of text)
        char const* text = cstr_str(&f->contents);
        char const* end = text + cstr_size(&f->contents);
        uint8_t found = 1;
        for (char const* at = text; (at = memchr(at, '@', end - at)); at++) {
            if (end - at >= 5 && !memcmp(at, "@link", 5)) { found = 2; break; }
        }
        __atomic_store_n(&f->has_link_tag, found, __ATOMIC_RELAXED);
        known = found;
    }
    return known == 2;
}

// The links of one JSDoc comment that tsc's checkJSDocComments resolves: those in the main text and in the comments of
// its top-level tags, as tsgo's JSDoc parser splits them (parseJSDocCommentWorker, parseTag). Not a tag's type
// expression (`@returns {@link X}` is a type), a `@see` name reference, nor the tags nested under a `@typedef` or
// `@param` of an object type, or under a `@callback` or `@overload` (their @property/@param/@returns)
typedef struct LinkScan {
    Scope const* names;
    char const* p;
    char const* end;
    bool line_start; // tsgo's BeginningOfLine or SawAsterisk: only whitespace and one `*` so far on this line
    bool backticks; // inside `...` on this line
    bool fenced; // inside a ``` block
    // A JavaScript file's: its nested @property and @param tags are declarations of their own that tsc checks (the
    // reparsed typedef's members, the callback's parameters), so their links count; a nested @returns' still don't
    bool js;
} LinkScan;

static void scan_skip_spaces(LinkScan* s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\v' || *s->p == '\f')) s->p++;
}

// Whitespace, line breaks and each line's leading `*` (tsgo's skipWhitespaceOrAsterisk)
static void scan_skip_trivia(LinkScan* s) {
    while (s->p < s->end) {
        if (*s->p == ' ' || *s->p == '\t' || *s->p == '\r') {
            s->p++;
        } else if (*s->p == '\n') {
            s->p++;
            s->line_start = true;
            s->backticks = false;
        } else if (*s->p == '*' && s->line_start) {
            s->p++;
            s->line_start = false; // (one asterisk per line: the next is text)
            // (A tag may still start after it: tsgo's SawAsterisk)
            if (s->p < s->end && *s->p != '*') s->line_start = true;
        } else {
            break;
        }
    }
}

// `{...}` with nested braces (a type expression)
static void scan_skip_braces(LinkScan* s) {
    u32 depth = 0;
    while (s->p < s->end) {
        char c = *s->p++;
        if (c == '{') depth++;
        else if (c == '}' && --depth == 0) return;
    }
}

// `{@link`, `{@linkcode`, `{@linkplain` at s->p: the length of the prefix, 0 if there's none
static size_t link_prefix(char const* p, char const* end) {
    if (end - p < 6 || p[0] != '{' || p[1] != '@') return 0;
    char const* word = p + 2;
    char const* word_end = word;
    while (word_end < end && is_identifier_char(*word_end)) word_end++;
    csview w = csview_from_n(word, word_end - word);
    if (csview_equals(w, "link") || csview_equals(w, "linkcode") || csview_equals(w, "linkplain")) return (size_t)(word_end - p);
    return 0;
}

static void utf8_append(cstr* out, u32 code) {
    char buf[4];
    size_t n;
    if (code < 0x80) { buf[0] = (char)code; n = 1; }
    else if (code < 0x800) { buf[0] = (char)(0xC0 | (code >> 6)); buf[1] = (char)(0x80 | (code & 0x3F)); n = 2; }
    else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12)); buf[1] = (char)(0x80 | ((code >> 6) & 0x3F)); buf[2] = (char)(0x80 | (code & 0x3F)); n = 3;
    } else {
        buf[0] = (char)(0xF0 | (code >> 18)); buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F)); buf[3] = (char)(0x80 | (code & 0x3F)); n = 4;
    }
    cstr_append_n(out, buf, (isize)n);
}

// An identifier at p, its `\u` escapes decoded (the scanner's identifier value): its end, or p if there's none
static char const* read_identifier(char const* p, char const* end, cstr* out) {
    char const* at = p;
    while (at < end) {
        if ((unsigned char)*at >= 0x80) {
            // (A non-ASCII character: the scanner's IsIdentifierStart/Part of the code point)
            unsigned char c0 = (unsigned char)*at;
            u32 len = c0 >= 0xF0 ? 4 : c0 >= 0xE0 ? 3 : c0 >= 0xC0 ? 2 : 1;
            if (len == 1 || end - at < (isize)len) break;
            u32 code = c0 & (0x7F >> len);
            for (u32 k = 1; k < len; k++) code = (code << 6) | ((unsigned char)at[k] & 0x3F);
            if (at == p ? !unicode_is_identifier_start(code) : !unicode_is_identifier_part(code)) break;
            cstr_append_n(out, at, (isize)len);
            at += len;
            continue;
        }
        if (is_identifier_char(*at)) {
            if (at == p && !is_identifier_start_char(*at)) break;
            cstr_append_n(out, at, 1);
            at++;
            continue;
        }
        if (*at != '\\' || end - at < 3 || at[1] != 'u') break;
        char const* digits = at + 2;
        u32 code = 0;
        char const* after;
        if (*digits == '{') {
            after = digits + 1;
            while (after < end && isxdigit((unsigned char)*after) && code <= 0x10FFFF) {
                code = code * 16 + (u32)(isdigit((unsigned char)*after) ? *after - '0' : (tolower((unsigned char)*after) - 'a' + 10));
                after++;
            }
            if (after == digits + 1 || after >= end || *after != '}') break;
            after++;
        } else {
            if (end - digits < 4) break;
            bool valid = true;
            for (int k = 0; k < 4; k++) {
                char d = digits[k];
                if (!isxdigit((unsigned char)d)) { valid = false; break; }
                code = code * 16 + (u32)(isdigit((unsigned char)d) ? d - '0' : (tolower((unsigned char)d) - 'a' + 10));
            }
            if (!valid) break;
            after = digits + 4;
        }
        utf8_append(out, code);
        at = after;
    }
    return at;
}

// tsc's reserved words, which parseIdentifier won't take (a `#` member name: tsgo's parseJSDocLinkName)
static bool is_reserved_word_sv(csview w) {
    static char const* const words[] = { "break", "case", "catch", "class", "const", "continue", "debugger", "default",
        "delete", "do", "else", "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
        "instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void",
        "while", "with" };
    for (u32 i = 0; i < c_countof(words); i++) if (csview_equals(w, words[i])) return true;
    return false;
}

static void mark_link_name(LinkScan const* s, csview name, u32 meaning) {
    Symbol* found = scope_resolve(s->names, name, meaning);
    // (A class's type parameter isn't in scope in its static members: tsc's resolveName finds nothing there)
    if (found && (found->flags & SYMBOL_TYPE_PARAMETER)) {
        u64 hash = symbol_name_hash(found->name);
        bool in_static = false;
        for (Scope const* i = s->names; i && !scope_declares_symbol_hashed(i, found, hash); i = i->parent) {
            in_static |= scope_rare(i)->this_ref.kind == THIS_CLASS_STATIC;
        }
        if (in_static) found = nullptr;
    }
    unused_mark_referenced(s->names, found, meaning);
}

// `{@link Name rest}` at s->p (link_prefix() said so): tsc's resolveJSDocMemberName resolves the entity name with any
// meaning, a qualified one (`a.b`, `a#b`) through its left side as a namespace (resolveEntityName), and when that
// fails, the left side with any meaning. Its first identifier is what may be used here: the members aren't followed,
// so a qualified name falls back only when a `#` part is a reserved word (parseIdentifier makes it a missing name)
static void scan_link(LinkScan* s, size_t prefix, bool mark) {
    s->p += prefix;
    scan_skip_spaces(s);
    cstr name = cstr_init();
    char const* after = read_identifier(s->p, s->end, &name);
    if (after != s->p) {
        bool qualified = false;
        bool missing = false;
        while (after < s->end && (*after == '.' || *after == '#')) {
            bool hash = *after == '#';
            cstr part = cstr_init();
            char const* part_end = read_identifier(after + 1, s->end, &part);
            if (part_end == after + 1) {
                cstr_drop(&part);
                if (!hash) { qualified = true; missing = true; }
                break;
            }
            qualified = true;
            missing |= hash && is_reserved_word_sv(cstr_sv(&part));
            cstr_drop(&part);
            after = part_end;
        }
        if (mark && s->names) {
            if (!qualified) {
                mark_link_name(s, cstr_sv(&name), SYMBOL_ANY_MEANING);
            } else {
                Symbol const* left = scope_resolve(s->names, cstr_sv(&name), SYMBOL_NAMESPACE);
                if (left) mark_link_name(s, cstr_sv(&name), SYMBOL_NAMESPACE);
                if (!left || missing) mark_link_name(s, cstr_sv(&name), SYMBOL_ANY_MEANING);
            }
        }
    }
    cstr_drop(&name);
    s->p = after;
    while (s->p < s->end && *s->p != '}' && *s->p != '\n') s->p++;
    if (s->p < s->end && *s->p == '}') s->p++;
    s->line_start = false;
}

// Comment text up to the next tag (s->p at its `@`) or the end of the comment, its links marked if `mark`
static void scan_comment_text(LinkScan* s, bool mark) {
    while (s->p < s->end) {
        char c = *s->p;
        if (c == '\n') {
            s->p++;
            s->line_start = true;
            s->backticks = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            s->p++;
            continue;
        }
        if (c == '*' && s->line_start) {
            // (The first asterisk of a line is its margin)
            s->p++;
            if (s->p < s->end && *s->p == '*') s->line_start = false;
            continue;
        }
        if (c == '`') {
            u32 count = 0;
            while (s->p < s->end && *s->p == '`') { s->p++; count++; }
            // (Each backtick toggles tsgo's SavingBackticks state; three or more toggle a fenced block too)
            if (count >= 3) s->fenced = !s->fenced;
            if (count % 2) s->backticks = !s->backticks;
            s->line_start = false;
            continue;
        }
        if (c == '@' && !s->fenced) {
            char next = s->p + 1 < s->end ? s->p[1] : '\n';
            char prev = s->p[-1];
            bool tag = s->line_start ? (is_identifier_start_char(next) || next == ' ' || next == '\t' || next == '\n' || next == '\r') :
                !s->backticks && (prev == ' ' || prev == '\t') && is_identifier_start_char(next);
            if (tag) return;
        }
        if (c == '{' && !s->fenced && (s->line_start || !s->backticks)) {
            size_t prefix = link_prefix(s->p, s->end);
            if (prefix) {
                scan_link(s, prefix, mark);
                continue;
            }
        }
        s->p++;
        s->line_start = false;
    }
}

typedef struct TagHead {
    csview name;
    char const* after;
} TagHead;

// The tag at s->p (its `@`), not consumed
static TagHead peek_tag(LinkScan const* s) {
    char const* p = s->p + 1;
    char const* name_end = p;
    while (name_end < s->end && is_identifier_char(*name_end)) name_end++;
    return (TagHead){ .name = csview_from_n(p, name_end - p), .after = name_end };
}

static bool tag_is(TagHead const* tag, char const* a, char const* b, char const* c) {
    return csview_equals(tag->name, a) || (b && csview_equals(tag->name, b)) || (c && csview_equals(tag->name, c));
}

// tsc's isObjectOrObjectArrayTypeReference, by the type expression's text (`{Object}`, `{object[]}`)
static bool is_object_type_text(char const* open, char const* close) {
    char const* a = open + 1;
    char const* b = close - 1;
    while (a < b && (*a == ' ' || *a == '\t')) a++;
    while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
    csview t = csview_from_n(a, b - a);
    return csview_equals(t, "Object") || csview_equals(t, "object") || csview_equals(t, "Object[]") || csview_equals(t, "object[]");
}

// A @param or @property tag after its name (tsgo's parseParameterOrPropertyTag): its comment, marked if `mark`. The
// name is written to `name`; whether its type is an object type (its nested tags follow)
static bool scan_param_like(LinkScan* s, char const* after_tag, bool mark, csview* name) {
    s->p = after_tag;
    scan_skip_trivia(s);
    bool object = false;
    bool typed = false;
    if (s->p < s->end && *s->p == '{') {
        char const* open = s->p;
        scan_skip_braces(s);
        object = is_object_type_text(open, s->p);
        typed = true;
        scan_skip_trivia(s);
    }
    char const* name_start = s->p;
    if (s->p < s->end && *s->p == '[') {
        while (s->p < s->end && *s->p != ']' && *s->p != '\n') s->p++;
        if (s->p < s->end && *s->p == ']') s->p++;
        name_start++;
    } else {
        while (s->p < s->end && (is_identifier_char(*s->p) || *s->p == '.' || *s->p == '[' || *s->p == ']')) s->p++;
    }
    char const* name_end = s->p;
    while (name_end > name_start && (name_end[-1] == ']' || name_end[-1] == '[')) name_end--;
    for (char const* q = name_start; q < name_end; q++) if (*q == '=' || *q == ' ') { name_end = q; break; }
    *name = csview_from_n(name_start, name_end - name_start);
    if (!typed) {
        char const* save = s->p;
        scan_skip_spaces(s);
        if (s->p < s->end && *s->p == '{' && !link_prefix(s->p, s->end)) {
            char const* open = s->p;
            scan_skip_braces(s);
            object = is_object_type_text(open, s->p);
        } else {
            s->p = save;
        }
    }
    s->line_start = true;
    scan_comment_text(s, mark);
    return object;
}

// Nested @param/@property tags under a parent of an object type: those whose name starts with `parent.`
static void scan_nested_params(LinkScan* s, csview parent, bool property) {
    while (s->p < s->end) {
        TagHead tag = peek_tag(s);
        bool kind = property ? tag_is(&tag, "property", "prop", nullptr) : tag_is(&tag, "param", "arg", "argument");
        if (!kind) return;
        // (The name, to see whether it's the parent's member)
        LinkScan probe = *s;
        probe.p = tag.after;
        scan_skip_trivia(&probe);
        if (probe.p < probe.end && *probe.p == '{') { scan_skip_braces(&probe); scan_skip_trivia(&probe); }
        if (probe.p < probe.end && *probe.p == '[') probe.p++;
        if ((size_t)(probe.end - probe.p) <= (size_t)parent.size || memcmp(probe.p, parent.buf, (size_t)parent.size) ||
            probe.p[parent.size] != '.'
        ) {
            return;
        }
        csview name;
        bool object = scan_param_like(s, tag.after, s->js, &name);
        if (object) scan_nested_params(s, name, property);
    }
}

// A @callback's or @overload's signature tags: @param ones, then one @returns, none of them marked
static void scan_signature_children(LinkScan* s) {
    while (s->p < s->end) {
        TagHead tag = peek_tag(s);
        if (!tag_is(&tag, "param", "arg", "argument")) break;
        csview name;
        bool object = scan_param_like(s, tag.after, s->js, &name);
        if (object) scan_nested_params(s, name, false);
    }
    if (s->p < s->end) {
        TagHead tag = peek_tag(s);
        if (tag_is(&tag, "returns", "return", nullptr)) {
            s->p = tag.after;
            scan_skip_trivia(s);
            if (s->p < s->end && *s->p == '{') scan_skip_braces(s);
            s->line_start = true;
            scan_comment_text(s, false);
        }
    }
}

static void scan_jsdoc(Scope const* names, char const* body, char const* end, bool js) {
    LinkScan s = { .names = names, .p = body, .end = end, .line_start = true, .js = js };
    scan_comment_text(&s, true);
    while (s.p < s.end) {
        TagHead tag = peek_tag(&s);
        s.p = tag.after;
        if (tag_is(&tag, "returns", "return", "this") || tag_is(&tag, "enum", "type", "satisfies") ||
            tag_is(&tag, "throws", "exception", nullptr) || tag_is(&tag, "implements", "augments", "extends")
        ) {
            scan_skip_trivia(&s);
            if (s.p < s.end && *s.p == '{') scan_skip_braces(&s);
            s.line_start = true;
            scan_comment_text(&s, true);
        } else if (tag_is(&tag, "param", "arg", "argument") || tag_is(&tag, "property", "prop", nullptr)) {
            bool property = tag_is(&tag, "property", "prop", nullptr);
            csview name;
            if (scan_param_like(&s, tag.after, true, &name)) scan_nested_params(&s, name, property);
        } else if (tag_is(&tag, "template", nullptr, nullptr)) {
            scan_skip_trivia(&s);
            if (s.p < s.end && *s.p == '{') scan_skip_braces(&s);
            while (s.p < s.end && (is_identifier_char(*s.p) || *s.p == ',' || *s.p == ' ' || *s.p == '\t')) s.p++;
            s.line_start = true;
            scan_comment_text(&s, true);
        } else if (tag_is(&tag, "typedef", nullptr, nullptr)) {
            scan_skip_trivia(&s);
            bool object = true;
            if (s.p < s.end && *s.p == '{') {
                char const* open = s.p;
                scan_skip_braces(&s);
                object = is_object_type_text(open, s.p);
            }
            scan_skip_spaces(&s);
            while (s.p < s.end && (is_identifier_char(*s.p) || *s.p == '.')) s.p++;
            s.line_start = true;
            scan_comment_text(&s, true);
            while (object && s.p < s.end) {
                TagHead child = peek_tag(&s);
                if (!tag_is(&child, "property", "prop", nullptr)) break;
                csview name;
                if (scan_param_like(&s, child.after, s.js, &name)) scan_nested_params(&s, name, true);
            }
        } else if (tag_is(&tag, "callback", "overload", nullptr)) {
            scan_skip_spaces(&s);
            while (s.p < s.end && (is_identifier_char(*s.p) || *s.p == '.')) s.p++;
            s.line_start = true;
            scan_comment_text(&s, true);
            scan_signature_children(&s);
        } else if (tag_is(&tag, "see", nullptr, nullptr)) {
            // A name reference (`@see Foo`, `@see {Foo}`) isn't a link; a link or a markdown link is the comment
            scan_skip_trivia(&s);
            if (s.p < s.end && *s.p != '[' && !link_prefix(s.p, s.end)) {
                bool braced = *s.p == '{';
                if (braced) s.p++;
                while (s.p < s.end && (is_identifier_char(*s.p) || *s.p == '.' || *s.p == '#' || *s.p == '\\')) s.p++;
                if (braced && s.p < s.end && *s.p == '}') s.p++;
            }
            s.line_start = true;
            scan_comment_text(&s, true);
        } else if (tag_is(&tag, "import", nullptr, nullptr)) {
            while (s.p < s.end && *s.p != '\n') s.p++;
            scan_comment_text(&s, false);
        } else {
            s.line_start = true;
            scan_comment_text(&s, true);
        }
    }
}

// An earlier `/*` that opens the comment whose `/*` (one of its inner ones) is at `open`: on its line, and on the
// lines before it while the line after starts with `*` (a JSDoc's margin), up to a `*/`
static char const* comment_open_extended(char const* file_start, char const* open) {
    char const* line_end = open; // Search [line_start, line_end)
    while (true) {
        char const* newline_at = line_end > file_start ? last_byte(file_start, '\n', (size_t)(line_end - file_start)) : nullptr;
        char const* line_start = newline_at ? newline_at + 1 : file_start;
        char const* q = line_end;
        while (q > line_start) {
            char const* slash = last_byte(line_start, '/', (size_t)(q - line_start));
            if (!slash) break;
            if (slash > line_start && slash[-1] == '*') return open; // a `*/`: what's before is another comment
            if (slash + 1 < open && slash[1] == '*') open = slash;
            q = slash;
        }
        if (!newline_at) return open;
        // (The line just searched continues a comment from the line before only if it starts with `*`)
        char const* first = line_start;
        while (first < line_end && (*first == ' ' || *first == '\t')) first++;
        if (first >= line_end || *first != '*' || (first + 1 < line_end && first[1] == '/')) return open;
        line_end = newline_at;
    }
}

// The JSDoc comments tsc attaches to a node that starts at `node_start` (tsgo's GetJSDocCommentRanges): the comments
// between the previous token and the node that follow a line break, or are at the start of the file; for a parameter
// (`trailing`) also those on the previous token's line. Their links are uses, resolved from `names`
void unused_mark_jsdoc_links_before(Scope const* names, char const* node_start, SourceFile const* file, bool trailing) {
    if (!unused_enabled() || !node_start || !file || !unused_file_has_link_tag(file)) return;
    char const* file_start = cstr_str(&file->contents);
    // Back over whitespace and block comments to the previous token, remembering the comments (innermost first)
    enum { MAX_COMMENTS = 16 };
    char const* opens[MAX_COMMENTS];
    char const* closes[MAX_COMMENTS]; // At the `*/`
    u32 count = 0;
    char const* at = node_start;
    while (true) {
        while (at > file_start && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\n' || at[-1] == '\r')) at--;
        if (at - file_start < 4 || at[-1] != '/' || at[-2] != '*') break;
        char const* close = at - 2;
        char const* open = close;
        // (memrchr: a long comment before every statement of a file with links is scanned at vector speed)
        while (open >= file_start) {
            char const* slash = last_byte(file_start, '/', open + 1 - file_start);
            if (!slash) { open = file_start - 1; break; }
            open = slash;
            if (open[1] == '*' && open + 2 <= close) break;
            open--;
        }
        if (open < file_start) break;
        // (`/** a /** {@link X} */` is one comment: an earlier `/*` with no `*/` in between opens it. Looked for on the
        // line and on the lines before it that continue a comment, starting with `*`)
        open = comment_open_extended(file_start, open);
        if (count == MAX_COMMENTS) break;
        opens[count] = open;
        closes[count] = close;
        count++;
        at = open;
    }
    if (!count) return;
    // tsgo parses these comments' JSDoc only if one of the JSDoc comments before the token has a `@see` or `@link` tag
    // (scanJSDocCommentForTags: the name followed by whitespace, `}`, `*` or the end; else it isn't parsed eagerly and
    // checkJSDocComments sees nothing). A JavaScript file's JSDoc is always parsed
    bool eager = file->is_js;
    for (u32 i = 0; i < count && !eager; i++) {
        char const* open = opens[i];
        char const* close = closes[i];
        if (!(open + 2 < close && open[2] == '*' && open[3] != '/')) continue;
        for (char const* q = open + 2; q < close && !eager; q++) {
            q = memchr(q, '@', (size_t)(close - q));
            if (!q) break;
            static char const* const tags[] = { "see", "linkcode", "linkplain", "link" };
            for (u32 t = 0; t < c_countof(tags) && !eager; t++) {
                size_t n = strlen(tags[t]);
                if ((size_t)(close - (q + 1)) < n || memcmp(q + 1, tags[t], n)) continue;
                char c = q + 1 + n < close ? q[1 + n] : '\0';
                eager = c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '}' || c == '*';
            }
        }
    }
    if (!eager) return;
    // Which comments tsc collects: after a line break that follows the previous token (or at the file's start); for a
    // parameter or a declaration all of them
    bool collecting = trailing || at == file_start;
    char const* prev_end = at;
    for (u32 k = count; k-- > 0;) {
        char const* open = opens[k];
        char const* close = closes[k];
        if (!collecting && memchr(prev_end, '\n', (size_t)(open - prev_end))) collecting = true;
        bool jsdoc = open + 2 < close && open[2] == '*' && open[3] != '/';
        if (collecting && jsdoc && memmem(open, (size_t)(close - open), "@link", 5)) scan_jsdoc(names, open + 3, close, file->is_js);
        prev_end = close + 2;
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
                unread_member(r, report_never_read(r, false, i->span, name, name), i->span);
            }   break;
            case CLASS_ITEM_CONSTRUCTOR:
                for_each (arg, vec_function_arg, &i->constructor.func.args) {
                    if (!arg->is_parameter_property || arg->access != ACCESS_MODIFIER_PRIVATE) continue;
                    if (ty_member_referenced_order(checker, arg->span) <= r->order) continue;
                    Span name = arg->initializer.binding.content_span;
                    unread_member(r, report(r, false, arg->span, name, 6138, cstr_d("Property '", name, "' is declared but its value is never read.")), arg->span);
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
                report_type_parameters(&r, &decl->generic, decl->symbol, decl->full_span);
            }   break;
            case UNUSED_INTERFACE: {
                InterfaceDecl const* decl = candidate->node;
                report_type_parameters(&r, &decl->generic, decl->symbol, decl->full_span);
            }   break;
            case UNUSED_TYPE_ALIAS: {
                TypeDecl const* decl = candidate->node;
                report_type_parameters(&r, &decl->generic, nullptr, decl->full_span);
            }   break;
            case UNUSED_INFER: {
                InferredType const* infer = candidate->node;
                if (infer->owned_symbol && !(infer->owned_symbol->referenced & SYMBOL_TYPE_PARAMETER) &&
                    !starts_with_underscore(infer->name)
                ) {
                    report(&r, true, infer->full_span, infer->name, 6196, cstr_d("'", infer->name, "' is declared but never used."));
                }
            }   break;
        }
    }
}
