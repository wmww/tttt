#include "ty/ty_internal.h"
#include "scope.h"
#include "program.h"
#include "function_impl.h"
#include "several_types.h"
#include "interface_member.h"
#include "statement.h"
#include "source_file.h"
#include "callable_type.h"
#include "symbol_chain.h"
#include "enum_decl.h"
#include "unicode_identifiers.h"
#include "port_statement.h"
#include "class_decl.h"
#include "module_resolution.h"

// Prints a Ty the way tsc's typeToString() does, in its order of preference: alias name, reference name + arguments
// (array and tuple syntax for those targets), declared name, structure.

#define MAX_DISPLAY_DEPTH 8

// One underline character per UTF-16 code unit of what was printed since `start`, like the harness' underlineFor()
static void underline_since(TyDisplay d, isize_t start) {
    if (!d.underline) return;
    char const* text = cstr_str(d.target);
    isize_t size = cstr_size(d.target);
    for (isize_t i = start; i < size; i++) {
        unsigned char c = (unsigned char)text[i];
        if ((c & 0xC0) == 0x80) continue;
        u32 units = c >= 0xF0 ? 2 : 1;
        for (u32 j = 0; j < units; j++) cstr_push(d.underline, d.reused ? " " : "^");
    }
}

// The length past which declaration emit doesn't serialize a type (tsc's noTruncationMaximumTruncationLength)
#define DECLARATION_MAX_LENGTH 1000000

static void out(TyDisplay d, csview sv) {
    if (d.tracker && d.tracker->truncated) return;
    isize_t start = cstr_size(d.target);
    cstr_append_sv(d.target, sv);
    underline_since(d, start);
    if (d.tracker && cstr_size(d.target) > DECLARATION_MAX_LENGTH) {
        d.tracker->truncated = true;
        d.tracker->report(d.tracker, DECL_REPORT_TRUNCATION, c_sv(""));
    }
}
#define out_lit(d, s) out(d, c_sv(s))

typedef struct PrintedRange {
    isize_t start, end, underline_start, underline_end;
} PrintedRange;
static PrintedRange printed_range_start(TyDisplay d) {
    return (PrintedRange){ .start = cstr_size(d.target), .underline_start = d.underline ? cstr_size(d.underline) : 0 };
}
static void printed_range_end(TyDisplay d, PrintedRange* range) {
    range->end = cstr_size(d.target);
    range->underline_end = d.underline ? cstr_size(d.underline) : 0;
}
// Prints again what was printed in `range` (tsc's serializedTypes cache: the same type printed twice is the same node,
// type parameter names and all, and isn't worked out twice)
static void reprint_range(TyDisplay d, PrintedRange range) {
    if (d.tracker && d.tracker->truncated) return;
    cstr text = cstr_from_sv(csview_slice(cstr_sv(d.target), range.start, range.end));
    cstr_append_sv(d.target, cstr_sv(&text));
    cstr_drop(&text);
    if (d.underline) {
        cstr underline = cstr_from_sv(csview_slice(cstr_sv(d.underline), range.underline_start, range.underline_end));
        cstr_append_sv(d.underline, cstr_sv(&underline));
        cstr_drop(&underline);
    }
}


typedef struct TyPrinted {
    Ty const* ty;
    cstr const* target;
    PrintedRange range;
} TyPrinted;

void ty_param_names_drop(TyParamNames* names) {
    free(names->given);
    free(names->printed);
}

static TyPrinted const* printed_before(TyDisplay d, Ty const* ty) {
    if (!d.param_names) return nullptr;
    for (u32 i = 0; i < d.param_names->printed_len; i++) {
        TyPrinted const* printed = &d.param_names->printed[i];
        if (printed->ty == ty && printed->target == d.target) return printed;
    }
    return nullptr;
}

static void remember_printed(TyDisplay d, Ty const* ty, PrintedRange range) {
    TyParamNames* names = d.param_names;
    if (!names) return;
    if (names->printed_len == names->printed_cap) {
        names->printed_cap = names->printed_cap ? names->printed_cap * 2 : 8;
        names->printed = realloc(names->printed, names->printed_cap * sizeof(TyPrinted));
    }
    names->printed[names->printed_len++] = (TyPrinted){ .ty = ty, .target = d.target, .range = range };
}

static TyDisplay reused_if(TyDisplay d, bool reused) {
    d.reused = reused;
    return d;
}

static bool is_function_like(Checker* me, TyDisplay d, Ty const* ty);

// Whether a type prints by its alias name: not when printed for a place (the types baseline) that can't name a local
// alias (tsc's isTypeSymbolAccessible: declarations in a block or function body aren't visible)
static bool shows_alias(TyDisplay d, Ty const* ty) {
    if (!ty->alias) return false;
    if (!d.enclosing) return true;
    // (The types baseline's typeToTypeNode and declaration emit don't have UseAliasDefinedOutsideCurrentScope, which
    // typeToString adds: an alias that isn't accessible from where it's printed prints its structure. A message's type
    // printed from its expression is typeToString's: the local alias shows)
    TypeDecl const* type_decl = symbol_find_node(ty->alias, SYMBOL_DECL_TYPE_ALIAS);
    if (type_decl && type_decl->local && !d.tracker && d.underline) return false;
    if (!d.tracker && !d.underline) return true;
    return symbol_type_accessible_at(d.enclosing, ty->alias);
}

static bool same_symbol(Checker* me, Symbol const* a, Symbol const* b);

// The symbol a type prints the name of as a plain type reference (`Foo`, `Foo<T>`), if it does
static Symbol const* reference_name_symbol(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty)) return ty->alias;
    Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    if (declared->kind != TY_DECLARED || declared == my array_target || declared == my readonly_array_target) return nullptr;
    if (declared->declared.kind != TY_DECLARED_INTERFACE && declared->declared.kind != TY_DECLARED_CLASS_INSTANCE) return nullptr;
    return declared->declared.symbol;
}

// tsc's mapToTypeNodes: members of a list (union, intersection, tuple, type arguments) that would print the same name
// for different types (`[Yep, Yep]` of two namespaces) are printed fully qualified. Null if none are; else which are
// (to free)
static bool* name_collisions(Checker* me, TyDisplay d, TyList list) {
    if (d.enclosing || d.fully_qualified || list.len < 2 || list.len > 256) return nullptr;
    Symbol const* stack_symbols[16];
    Symbol const** symbols = list.len <= c_arraylen(stack_symbols) ? stack_symbols : malloc(sizeof(Symbol const*) * list.len);
    u32 named = 0;
    for (u32 i = 0; i < list.len; i++) {
        symbols[i] = reference_name_symbol(me, d, list.data[i]);
        named += symbols[i] != nullptr;
    }
    bool* ret = nullptr;
    for (u32 i = 0; named >= 2 && i < list.len; i++) {
        if (!symbols[i]) continue;
        for (u32 j = i + 1; j < list.len; j++) {
            if (!symbols[j] || symbols[j] == symbols[i] || !csview_equals_sv(symbols[j]->name, symbols[i]->name)) continue;
            if (same_symbol(me, symbols[i], symbols[j])) continue;
            if (!ret) ret = calloc(list.len, sizeof(bool));
            ret[i] = ret[j] = true;
        }
    }
    if (symbols != stack_symbols) free((void*)symbols);
    return ret;
}

static TyDisplay qualified_if(TyDisplay d, bool const* collisions, u32 i) {
    if (collisions && collisions[i]) d.fully_qualified = true;
    return d;
}

// Type arguments. The first one is parenthesized if it's a generic function type (tsc's parenthesizeTypeArguments:
// `X<<T>() => T>` would start with `<<`)
static void display_list(Checker* me, TyDisplay d, TyList list) {
    bool* collisions = name_collisions(me, d, list);
    for (u32 i = 0; i < list.len; i++) {
        if (i) out_lit(d, ", ");
        Ty const* arg = list.data[i];
        bool parens = false;
        if (!i && is_function_like(me, d, arg)) {
            Members const* members = ty_members(me, arg);
            Signature const* sig = members->call_count ? members->call[0] : members->construct[0];
            signature_resolve(me, sig);
            parens = sig->type_params.len > 0;
        }
        if (parens) out_lit(d, "(");
        ty_display(me, qualified_if(d, collisions, i), arg);
        if (parens) out_lit(d, ")");
    }
    free(collisions);
}

static bool same_symbol(Checker* me, Symbol const* a, Symbol const* b) {
    Binder const* binder = program_get_binder(my program);
    return binder_redirect(binder, a) == binder_redirect(binder, b);
}

static bool name_taken_by_type_param(TyParamNames const* names, csview name) {
    for (u32 i = 0; i < names->in_scope.len; i++) {
        if (csview_equals_sv(atom_sv(names->in_scope.data[i]->param.name), name)) return true;
    }
    for (u32 i = 0; i < names->len; i++) {
        if (csview_equals_sv(atom_sv(names->given[i].name), name)) return true;
    }
    return false;
}

static bool global_shadowed_by_type_param(Checker* me, TyParamNames const* names, Symbol const* symbol) {
    if (!(symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_TYPE_ALIAS | SYMBOL_ENUM))) return false;
    Binder const* binder = program_get_binder(my program);
    csview name = symbol->name;
    if (binder_redirect(binder, symbol_table_get(&binder->globals, name)) != binder_redirect(binder, symbol)) return false;
    for (u32 i = 0; i < names->in_scope.len; i++) {
        if (csview_equals_sv(atom_sv(names->in_scope.data[i]->param.name), name)) return true;
    }
    for (u32 i = 0; i < names->len; i++) {
        if (csview_equals_sv(atom_sv(names->given[i].name), name)) return true;
    }
    return false;
}

// With TyDisplay.enclosing, qualified as far as needed from there (symbol_chain.h), else bare or fully qualified (error
// messages)
// tsc's isNonLocalFunctionSymbol: a function with a parent symbol or declared directly in a file or namespace block
static bool is_non_local_function(Symbol const* symbol) {
    if (!(symbol->flags & SYMBOL_FUNCTION)) return false;
    if (symbol->parent) return true;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        Scope const* block = symbol->decls[i].block;
        if (block && (block->kind == SCOPE_SCRIPT || block->kind == SCOPE_MODULE || block->kind == SCOPE_NAMESPACE ||
            block->kind == SCOPE_GLOBAL_AUGMENTATION)) {
            return true;
        }
    }
    return false;
}

// The function a module's `export =` assigns while the module has other exports (then tsc's getCommonJsExportEquals
// clones it into a ValueModule), else null
static Symbol const* export_equals_clone_of(Symbol const* function) {
    if (!function || !(function->flags & SYMBOL_FUNCTION) || !function->decl_count) return nullptr;
    Symbol* module = function->parent && function->parent->is_module ? function->parent : nullptr;
    Scope const* block = function->decls[0].block;
    if (!module && block && block->kind == SCOPE_MODULE) module = block->container;
    if (!module || !module->is_module || !module->exports || !symbol_table_get(module->exports, c_sv("export="))) return nullptr;
    Symbol const* assigned = module_export_equals(module);
    return assigned && symbol_rare(assigned)->merged_export_equals_of == function ? function : nullptr;
}

// A function with expando properties that is local (declared in a function or block): tsc prints a non-local one as
// `typeof f`, a local one's type like any function's (observed; the non-local rule is shouldWriteTypeOfFunctionSymbol's
// isNonLocalFunctionSymbol, why the expando members make it apply without recursion isn't pinned down)
static bool is_nested_expando_function(Ty const* ty) {
    Symbol const* symbol = ty->declared.kind == TY_DECLARED_NAMESPACE ? ty->declared.namespace_symbol : nullptr;
    if (!symbol) symbol = ty->declared.symbol;
    if (!symbol || !(symbol->flags & SYMBOL_FUNCTION) || symbol->parent) return false;
    bool expando = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_NAMESPACE || decl->kind == SYMBOL_DECL_CLASS) return false;
        if (decl->kind == SYMBOL_DECL_EXPANDO) expando = true;
    }
    return expando && !is_non_local_function(symbol);
}

// `import("m")` for a module at the root of a name (tsc's symbolToTypeNode: an ImportTypeNode), named from a place
// as program_module_display_name() says
static void out_module_import(Checker* me, TyDisplay d, Symbol const* module) {
    // An ES module named from a file of the other format under node16/nodenext resolution says how to resolve it
    // (tsc's symbolToTypeNode: the resolution-mode import attribute)
    ModuleResolutionKind resolution = compiler_options()->module_resolution;
    SourceFile const* target = module->module ? module->module->file : nullptr;
    SourceFile const* context = d.enclosing && span_valid(d.at) ? d.at.source : nullptr;
    bool resolution_mode = (resolution == MODULE_RESOLUTION_NODE16 || resolution == MODULE_RESOLUTION_NODENEXT) &&
        target && context && target->has_format && target->is_esm && !(context->has_format && context->is_esm);
    cstr module_name = program_module_display_name(my program, module, context,
        resolution_mode ? SPECIFIER_MODE_ESM : SPECIFIER_MODE_NONE, d.usage_ref);
    csview name = cstr_sv(&module_name);
    out_lit(d, "import(");
    if (name.size >= 2 && name.buf[0] == '\'') {
        // (a string literal the printer makes: double quotes)
        out_lit(d, "\"");
        out(d, csview_slice(name, 1, name.size - 1));
        out_lit(d, "\"");
    } else {
        out(d, name);
    }
    if (resolution_mode) out_lit(d, ", { with: { \"resolution-mode\": \"import\" } }");
    out_lit(d, ")");
    cstr_drop(&module_name);
}

static void out_symbol_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    if (d.tracker) d.tracker->track_symbol(d.tracker, symbol, meaning);
    csview js_name;
    if (ty_js_assigned_class_name(symbol, &js_name)) {
        out(d, js_name);
        return;
    }
    if (!d.enclosing && symbol_is_augmented_export_equals(symbol) && symbol->decl_count) {
        // What an augmented `export =` assigns is a module to tsc's printer (see below), in a message too
        SourceFile const* file = symbol->decls[0].full.source;
        out_lit(d, "import(\"");
        out(d, file ? source_file_display_path(file) : c_sv(""));
        out_lit(d, "\")");
        return;
    }
    Symbol const* parent = symbol->parent;
    if ((!parent && !d.enclosing) || (symbol->is_global_this && !d.enclosing) || (!d.enclosing && !d.fully_qualified)) {
        out(d, ty_symbol_display_name(symbol).sv);
        return;
    }
    if (!d.enclosing) {
        bool quoted_parent = parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\'');
        // A module or ambient module (`declare module "m"`) at the root is `import("m")` (tsc's getSymbolChain walks
        // up to it, symbolToTypeNode makes the import type)
        if (parent->is_module || quoted_parent) {
            out_module_import(me, d, parent);
        } else {
            out_symbol_name(me, d, parent, SYMBOL_NAMESPACE);
        }
        out_lit(d, ".");
        out(d, ty_symbol_display_name(symbol).sv);
        return;
    }
    // tsc's symbolToTypeNode: the chain accessible from here, `import("m")` when it starts at a module. Not in a
    // message (typeToString's UseAliasDefinedOutsideCurrentScope: no module symbol is yielded, what nothing reaches is
    // its bare name)
    SymbolChain chain = symbol_chain_at(d.enclosing, symbol, meaning, d.underline || d.tracker);
    u32 start = 0;
    // (What an augmented `export =` assigns is a module to tsc's printer, not named by its own local, see
    // is_external_module_symbol() in symbol_chain.c, and not as the module it is assigned to either)
    if (d.enclosing && symbol_is_augmented_export_equals(symbol) && chain.len == 1 && chain.items[0]->is_module) {
        chain.items[0] = symbol;
        chain.len = 1;
    }
    if (chain.len && d.enclosing && symbol_is_augmented_export_equals(chain.items[0])) {
        // What an augmented `export =` assigns is a module to tsc's printer (getSpecifierForModuleSymbol, which has a
        // resolver host since tsc 6 even for baselines): named by the first of its declarations that is an ambient
        // module not augmenting a relative name (tryGetModuleNameFromAmbientModule), else by a specifier from here
        // to the file of its first declaration
        Symbol const* module = chain.items[0];
        SourceFile const* file = module->decls[0].full.source;
        if (d.tracker && file && d.tracker->track_module_file) d.tracker->track_module_file(d.tracker, file, symbol);
        csview ambient = {0};
        for (u32 i = 0; i < module->decl_count && !ambient.size; i++) {
            if (module->decls[i].kind != SYMBOL_DECL_NAMESPACE) continue;
            NamespaceDecl const* ns = module->decls[i].node;
            if (!ns->is_ambient_module || ns->name.sv.size < 2) continue;
            csview name = csview_slice(ns->name.sv, 1, ns->name.sv.size - 1);
            if (!ns->is_augmentation || !module_name_is_relative(name)) ambient = name;
        }
        SourceFile const* context = span_valid(d.at) ? d.at.source : nullptr;
        out_lit(d, "import(\"");
        if (ambient.size) {
            out(d, ambient);
        } else if (file && context && !cstr_size(&compiler_options()->out_file)) {
            cstr specifier = program_module_specifier(my program, context, file, SPECIFIER_MODE_NONE);
            out(d, cstr_sv(&specifier));
            cstr_drop(&specifier);
        } else {
            out(d, file ? source_file_display_path(file) : c_sv(""));
        }
        out_lit(d, "\")");
        start = 1;
    } else if (chain.len && chain.items[0]->is_module) {
        out_module_import(me, d, chain.items[0]);
        SourceFile const* file = chain.items[0]->module ? chain.items[0]->module->file : nullptr;
        if (d.tracker && file && d.tracker->track_module_file) d.tracker->track_module_file(d.tracker, file, symbol);
        start = 1;
    }
    // A global whose name a type parameter in scope takes is reached through globalThis (tsc's getSymbolChain finds
    // the type parameter first in the declaration's locals, then the global in globalThis's exports)
    if (!start && chain.len && meaning == SYMBOL_TYPE && d.param_names && global_shadowed_by_type_param(me, d.param_names, chain.items[0])) {
        out_lit(d, "globalThis.");
    } else if (!start && chain.len == 1 && meaning == SYMBOL_TYPE && d.param_names && chain.items[0]->parent &&
        chain.items[0]->parent->is_module && !(chain.items[0]->flags & SYMBOL_TYPE_PARAMETER) &&
        name_taken_by_type_param(d.param_names, chain.items[0]->name)
    ) {
        // A module's export whose name a type parameter in scope takes (`type T<T> = ...` printed in itself) is named
        // from the module, like any inaccessible one (tsc's getSymbolChain finds the type parameter first). Only a
        // bare name: the first part of `E.Whatever` is looked up as a namespace, which no type parameter is
        out_module_import(me, d, chain.items[0]->parent);
        out_lit(d, ".");
    }
    cstr text = cstr_init();
    SymbolChain rest = { .len = chain.len - start };
    memcpy(rest.items, chain.items + start, rest.len * sizeof(*rest.items));
    if (start && rest.len) cstr_append_lit(&text, ".");
    symbol_chain_append_entity(&text, d.enclosing, &rest, true, start != 0);
    out(d, cstr_sv(&text));
    cstr_drop(&text);
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty);

// tsc's getTypeAliasForTypeLiteral: the type alias whose declared type is this type literal (written directly, perhaps
// in parentheses)
static Symbol const* type_alias_of_literal(Ty const* ty) {
    TypeNode const* node = ty->anonymous.node;
    Symbol const* alias = ty->alias;
    if (!node || node->tag != TYPE_INTERFACE || !alias || !(alias->flags & SYMBOL_TYPE_ALIAS)) return nullptr;
    for (u32 i = 0; i < alias->decl_count; i++) {
        if (alias->decls[i].kind != SYMBOL_DECL_TYPE_ALIAS) continue;
        TypeDecl const* decl = alias->decls[i].node;
        if (decl && &decl->type == node) return alias;
    }
    return nullptr;
}

// Declaration emit writes the types of a class expression, and of a class it can't name, as type literals (tsc's
// WriteClassExpressionAsTypeLiteral: createAnonymousTypeNode of a class's static side, typeReferenceToTypeNode of an
// instance)
static bool class_as_literal(TyDisplay d, Ty const* declared) {
    if (!d.tracker) return false;
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol || !(symbol->flags & SYMBOL_CLASS)) return false;
    ClassDecl const* cls = symbol_find_node(symbol, SYMBOL_DECL_CLASS);
    if (!cls) return false;
    if (cls->is_expression && declared->declared.kind == TY_DECLARED_CLASS_STATIC) return true;
    return !symbol_value_accessible_at(d.enclosing, symbol);
}

// The structure of a class written as a type literal, once: inside itself it is `any` (tsc's visitAndTransformType)
static void display_class_literal(Checker* me, TyDisplay d, Ty const* ty) {
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) {
            out_lit(d, "any");
            return;
        }
    }
    TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
    d.visiting = &visit;
    display_structure(me, d, ty);
}

// The name of a declared type: by its symbol when it has one
// The outer type arguments of a local class or interface (tsc's typeReferenceToTypeNode): `f<number>.` for each
// enclosing declaration whose type parameters aren't passed as themselves. `outer` is innermost first, the lists
// outermost first; returns how many parameters it covered
static u32 out_outer_type_args(Checker* me, TyDisplay d, OuterGenerics const* outer, TyList params, TyList args) {
    if (!outer) return 0;
    u32 start = out_outer_type_args(me, d, outer->parent, params, args);
    u32 len = ty_type_params_of(me, outer->generic).len;
    if (start + len > params.len) return start;
    bool same = true;
    for (u32 i = start; i < start + len; i++) same &= params.data[i] == args.data[i];
    if (!same) {
        if (span_valid(outer->owner_class)) {
            out(d, outer->owner_class.sv);
            out_lit(d, ".");
        }
        out(d, outer->owner.sv);
        out_lit(d, "<");
        display_list(me, d, (TyList){ .data = args.data + start, .len = len });
        out_lit(d, ">.");
    }
    return start + len;
}

// The type of a namespace import of an `export =` module (TyAnonymous.module_clone): tsc's cloneTypeAsModuleType
// makes an anonymous type whose symbol is a clone of the assigned one, `typeof x` where that is a class, enum or value
// module: named by the import that resolves to the clone where that is in scope, else by the assigned symbol's own name
static bool display_module_clone(Checker* me, TyDisplay d, Ty const* ty) {
    u64 key[2] = { KEY_MODULE_CLONE, (u64)(uintptr_t)ty };
    Symbol const* alias = intern_get(&my links, key, 2);
    if (!alias || !alias->alias || !alias->alias->item || !alias->alias->item->module_ref) return false;
    Symbol* module = binder_module_of_ref(alias->binder, alias->alias->item->module_ref);
    Symbol const* assigned = module ? module_export_equals(module) : nullptr;
    // (tsc's createAnonymousTypeNode: `typeof` for a class, enum or instantiated namespace, else the structure)
    if (!assigned || !(assigned->flags & (SYMBOL_CLASS | SYMBOL_ENUM | SYMBOL_VALUE_MODULE))) return false;
    out_lit(d, "typeof ");
    if (d.enclosing && scope_resolve(d.enclosing, alias->name, SYMBOL_VALUE) == alias) {
        if (d.tracker) d.tracker->track_symbol(d.tracker, alias, SYMBOL_VALUE);
        out(d, alias->name);
    } else {
        out(d, ty_symbol_display_name(assigned).sv);
    }
    return true;
}

static void out_declared_name(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol) symbol = declared->declared.merged_value;
    if (!symbol && declared->declared.kind == TY_DECLARED_NAMESPACE) symbol = declared->declared.namespace_symbol;
    if (declared->kind == TY_DECLARED && declared->declared.commonjs_exports) {
        // (Without a place to be named from, the `module.exports` symbol tsc copies from the file is a module named
        // by its file name, extension included: getSpecifierForModuleSymbol without a resolver host)
        Symbol const* module = declared->declared.namespace_symbol;
        SourceFile const* file = !d.enclosing && module && module->module ? module->module->file : nullptr;
        if (file) {
            out_lit(d, "import(\"");
            out(d, source_file_display_path(file));
            out_lit(d, "\")");
            return;
        }
        out(d, atom_sv(declared->declared.name));
        return;
    }
    // A module's object, also through `import * as ns` (whose symbol stands for the module): named from here like tsc
    if (d.enclosing && symbol && symbol != symbol_members(symbol) && namespace_import_is_clone(symbol)) {
        // The clone `import * as ns` makes is named by the import where that reaches it, else as the module
        SymbolChain chain = symbol_chain_at(d.enclosing, symbol, meaning, d.underline || d.tracker);
        out_symbol_name(me, d, chain.len ? symbol : symbol_members(symbol), meaning);
        return;
    }
    if (d.enclosing && symbol && symbol_members(symbol)->is_module) {
        out_symbol_name(me, d, symbol_is_augmented_export_equals(symbol) ? symbol : symbol_members(symbol), meaning);
        return;
    }
    // (without a place to be named from, as the module: messages say `typeof import("m")`)
    // (an ambient module by its name, `declare module "m"`)
    if (!d.enclosing && symbol && !symbol->is_module && symbol_members(symbol)->is_module && symbol_members(symbol)->module) {
        cstr module_name = module_name_for_error(symbol_members(symbol), nullptr, false);
        out_lit(d, "import(");
        out(d, cstr_sv(&module_name));
        out_lit(d, ")");
        cstr_drop(&module_name);
        return;
    }
    // (An anonymous class expression a CommonJS module exports is named through the module: `import("m")`)
    ClassDecl const* exported_class = symbol && !span_valid(symbol->name_span) && (symbol->flags & SYMBOL_CLASS) ?
        symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
    // (An anonymous `export default class {}` is named by what imports it: `import A from "./a"` is `typeof A`)
    if (symbol && !symbol->is_module && (span_valid(symbol->name_span) ||
        (exported_class && exported_class->is_expression && (exported_class->commonjs_module || exported_class->export_assigned) && d.enclosing) ||
        (symbol->is_default_export && d.enclosing))
    ) {
        out_symbol_name(me, d, symbol, meaning);
    } else if (d.enclosing && declared->declared.kind == TY_DECLARED_NAMESPACE && declared->declared.namespace_symbol &&
        declared->declared.namespace_symbol->is_module && declared->declared.namespace_symbol->module &&
        declared->declared.namespace_symbol->module->file
    ) {
        // (A module the expandos of an `export=` merged into, `module.exports.x = 1; module.exports = require("./y.js")`:
        // named from here, `import("./y.js")`)
        out_module_import(me, d, declared->declared.namespace_symbol);
    } else {
        out(d, atom_sv(declared->declared.name));
    }
}

// tsc's escapeString() with a double quote: named escapes, other control characters and U+2028/2029/0085 as \uXXXX
static void display_quoted_string(TyDisplay d, csview sv, bool single, bool ascii);
static void display_string_literal(TyDisplay d, csview sv) {
    display_quoted_string(d, sv, false, false);
}

static u32 utf8_next(csview sv, isize_t* i);

// `ascii`: everything else as `\uXXXX` escapes, like tsc's printer does a string literal node it made (a property name
// from getPropertyNameNodeForSymbol), unlike a literal type's (EmitFlags.NoAsciiEscaping)
static void display_quoted_string(TyDisplay d, csview sv, bool single, bool ascii) {
    if (single) out_lit(d, "'");
    else out_lit(d, "\"");
    for (isize_t i = 0; i < sv.size; i++) {
        unsigned char c = (unsigned char)sv.buf[i];
        switch (c) {
            case '"':
                if (single) out_lit(d, "\"");
                else out_lit(d, "\\\"");
                break;
            case '\'':
                if (single) out_lit(d, "\\'");
                else out_lit(d, "'");
                break;
            case '\\': out_lit(d, "\\\\"); break;
            case '\n': out_lit(d, "\\n"); break;
            case '\r': out_lit(d, "\\r"); break;
            case '\t': out_lit(d, "\\t"); break;
            case '\v': out_lit(d, "\\v"); break;
            case '\f': out_lit(d, "\\f"); break;
            case '\b': out_lit(d, "\\b"); break;
            case 0:
                if (i + 1 < sv.size && sv.buf[i + 1] >= '0' && sv.buf[i + 1] <= '9') out_lit(d, "\\x00");
                else out_lit(d, "\\0");
                break;
            default: {
                u32 code = 0;
                if (c < 0x20) {
                    code = c;
                } else if (c == 0xC2 && i + 1 < sv.size && (unsigned char)sv.buf[i + 1] == 0x85) {
                    code = 0x85;
                    i += 1;
                } else if (c == 0xE2 && i + 2 < sv.size && (unsigned char)sv.buf[i + 1] == 0x80 &&
                    ((unsigned char)sv.buf[i + 2] == 0xA8 || (unsigned char)sv.buf[i + 2] == 0xA9)
                ) {
                    code = (unsigned char)sv.buf[i + 2] == 0xA8 ? 0x2028 : 0x2029;
                    i += 2;
                }
                if (!code && ascii && c >= 0x80) {
                    code = utf8_next(sv, &i);
                    i--; // (The loop's increment)
                    char buf[16];
                    if (code >= 0x10000) {
                        code -= 0x10000;
                        snprintf(buf, sizeof buf, "\\u%04X\\u%04X", 0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF));
                    } else {
                        snprintf(buf, sizeof buf, "\\u%04X", code);
                    }
                    out(d, csview_from(buf));
                } else if (code) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04X", code);
                    out(d, csview_from(buf));
                } else {
                    out(d, csview_from_n(&sv.buf[i], 1));
                }
                break;
            }
        }
    }
    if (single) out_lit(d, "'");
    else out_lit(d, "\"");
}

// The text of a template literal as tsc's printer writes it: escapeString() with a backtick (newlines stay), and `${`
// escaped
static void display_template_text(TyDisplay d, csview sv) {
    for (isize_t i = 0; i < sv.size; i++) {
        unsigned char c = (unsigned char)sv.buf[i];
        switch (c) {
            case '`': out_lit(d, "\\`"); break;
            case '\\': out_lit(d, "\\\\"); break;
            case '\n': out_lit(d, "\n"); break;
            case '\r':
                // (A newline on its own stays, after a carriage return it's escaped too)
                if (i + 1 < sv.size && sv.buf[i + 1] == '\n') {
                    out_lit(d, "\\r\\n");
                    i++;
                } else {
                    out_lit(d, "\\r");
                }
                break;
            case '\t': out_lit(d, "\\t"); break;
            case '\v': out_lit(d, "\\v"); break;
            case '\f': out_lit(d, "\\f"); break;
            case '\b': out_lit(d, "\\b"); break;
            case '$':
                if (i + 1 < sv.size && sv.buf[i + 1] == '{') out_lit(d, "\\$");
                else out_lit(d, "$");
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04X", c);
                    out(d, csview_from(buf));
                } else {
                    out(d, csview_from_n(&sv.buf[i], 1));
                }
                break;
        }
    }
}

// One UTF-8 sequence at `*i` (advanced past it); malformed bytes are themselves
static u32 utf8_next(csview sv, isize_t* i) {
    unsigned char c = (unsigned char)sv.buf[*i];
    u32 len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (*i + len > sv.size) len = 1;
    u32 code = len == 1 ? c : c & (0xFF >> (len + 1));
    for (u32 k = 1; k < len; k++) code = code << 6 | ((unsigned char)sv.buf[*i + k] & 0x3F);
    *i += len;
    return code;
}

// tsc's isIdentifierText (non-ASCII by the Unicode ID_Start/ID_Continue tables: an emoji isn't one)
static bool is_identifier_name(csview sv) {
    if (!sv.size || (sv.buf[0] >= '0' && sv.buf[0] <= '9')) return false;
    for (isize_t i = 0; i < sv.size;) {
        char c = sv.buf[i];
        if ((unsigned char)c >= 0x80) {
            bool first = i == 0;
            u32 code = utf8_next(sv, &i);
            if (!(first ? unicode_is_identifier_start(code, true) : unicode_is_identifier_part(code, true))) return false;
            continue;
        }
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
            c == '#';
        if (!valid) return false;
        i++;
    }
    return true;
}

// Written as a number literal (tsc's createPropertyNameNodeForIdentifierOrLiteral: a non-negative numeric name)
static bool is_numeric_name(csview sv) {
    return sv.size && sv.buf[0] != '-' && value_is_numeric_literal_name(sv);
}

// How a unique symbol is written as an expression: `sym`, `C.key`, `Symbol.iterator`
static void out_unique_symbol_name(Checker* me, TyDisplay d, UniqueSymbolDecl const* decl) {
    Symbol const* owner = decl->owner;
    if (!span_valid(decl->member)) {
        if (owner) out_symbol_name(me, d, owner, SYMBOL_VALUE);
        return;
    }
    // Printed from nowhere, a member is its bare name (tsc's lookupSymbolChain without an enclosing declaration: the
    // chain is the symbol alone): `typeof VALID` for a static `SecurePassword.VALID`, `typeof toStringTag`
    if (d.typeof_unique_symbols && !d.enclosing && !d.fully_qualified && !d.tracker) {
        out(d, decl->member.sv);
        return;
    }
    if (owner && (owner->flags & SYMBOL_INTERFACE)) {
        // A member of `SymbolConstructor` is reached through the global variable of that type, `Symbol`. HACK: tsc
        // finds any variable whose type is the interface (getSymbolChain), this only knows the `XConstructor` pattern
        csview name = owner->name;
        isize_t suffix = (isize_t)strlen("Constructor");
        if (name.size > suffix && !owner->parent && csview_ends_with(name, "Constructor")) {
            csview value_name = csview_from_n(name.buf, name.size - suffix);
            if (program_get_global(my program, value_name, SYMBOL_VALUE)) {
                out(d, value_name);
                out_lit(d, ".");
                out(d, decl->member.sv);
                return;
            }
        }
    }
    if (owner) {
        out_symbol_name(me, d, owner, SYMBOL_VALUE);
        out_lit(d, ".");
    }
    out(d, decl->member.sv);
}

static void display_prop_name(Checker* me, TyDisplay d, Atom const* name);

cstr ty_prop_name_cstr(Checker* me, Atom const* name) {
    cstr ret = cstr_null;
    display_prop_name(me, (TyDisplay){ .target = &ret }, name);
    return ret;
}

cstr ty_prop_written_name(Checker* me, Prop const* prop) {
    Prop const* root = prop;
    while (root->target) root = root->target;
    // (a mapped type's properties are instantiations of its template, under other names)
    Span name = root->name == prop->name ? root->decl->name_span : span_null;
    // (tsc's getNameOfSymbolAsWritten: a name that isn't an identifier as written, `"a"`, `2.0`, `["a"]`)
    if (span_valid(name) && name.sv.size >= 2 && (name.sv.buf[0] == '"' || name.sv.buf[0] == '\'' || name.sv.buf[0] == '[')) {
        return cstr_from_sv(name.sv);
    }
    if (span_valid(name) && name.sv.size && name.sv.buf[0] >= '0' && name.sv.buf[0] <= '9') return cstr_from_sv(name.sv);
    return ty_prop_name_cstr(me, prop->name);
}

uint8_t ty_prop_name_form(csview written, Expr const* computed, Ty const* key_type, bool literal_key) {
    uint8_t form = PROP_NAME_KNOWN;
    if (computed) {
        if (!key_type) return form;
        if (key_type->flags & TYF_STRING_LIKE) form |= PROP_NAME_STRING;
        // (A literal written in a class or interface is the binder's name, tsc's getPropertyNameForPropertyNameNode)
        bool written_literal = computed->tag == EXPR_LITERAL || (computed->tag == EXPR_UNARY_OP &&
            (computed->unary_op->op == UNARY_OP_MINUS || computed->unary_op->op == UNARY_OP_PLUS) &&
            computed->unary_op->expr.tag == EXPR_LITERAL);
        if ((key_type->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) && (literal_key || !written_literal)) {
            form |= PROP_NAME_TYPED;
        }
        return form;
    }
    if (written.size && (written.buf[0] == '"' || written.buf[0] == '\'')) form |= PROP_NAME_STRING;
    if (written.size && written.buf[0] == '\'') form |= PROP_NAME_SINGLE_QUOTED;
    return form;
}

// Another declaration of the same property: tsc asks every declaration whether it's string named or single quoted, the
// name type is the last one's (checkObjectLiteral)
uint8_t ty_prop_name_form_merge(uint8_t a, uint8_t b) {
    return PROP_NAME_KNOWN | (a & b & (PROP_NAME_STRING | PROP_NAME_SINGLE_QUOTED)) | (b & PROP_NAME_TYPED);
}

uint8_t ty_prop_name_form_of(Prop const* prop) {
    // The declaration: the property's own (a mapped type's property has its key's), else the one it instantiates
    Prop const* root = prop;
    while (root->target) root = root->target;
    PropDecl const* decl = prop->decl;
    for (Prop const* p = prop->target; p && !(decl->name_form & PROP_NAME_KNOWN) && !span_valid(decl->name_span); p = p->target) {
        decl = p->decl;
    }
    uint8_t form = decl->name_form;
    if (!(form & PROP_NAME_KNOWN)) {
        form = span_valid(decl->name_span) && decl->name_span.sv.size && decl->name_span.sv.buf[0] != '[' ?
            ty_prop_name_form(decl->name_span.sv, nullptr, nullptr, false) : PROP_NAME_KNOWN;
    }
    // (A mapped type's property is named by its key type)
    if (root->name != prop->name) form |= PROP_NAME_TYPED;
    return form;
}

// tsc's getPropertyNameNodeForSymbol: an identifier, a number, a string literal quoted like every declaration wrote it,
// or `[-1]` for a negative number that came from a type
static void display_member_name(Checker* me, TyDisplay d, Prop const* prop) {
    csview sv = atom_sv(prop->name);
    if (ty_unique_symbol_of_name(me, prop->name)) {
        display_prop_name(me, d, prop->name);
        return;
    }
    uint8_t form = ty_prop_name_form_of(prop);
    bool string_named = form & PROP_NAME_STRING;
    bool single = form & PROP_NAME_SINGLE_QUOTED;
    bool identifier = is_identifier_name(sv);
    if ((form & PROP_NAME_TYPED) && !identifier && sv.size > 1 && sv.buf[0] == '-' && !string_named &&
        value_is_numeric_literal_name(sv)
    ) {
        out_lit(d, "[");
        out(d, sv);
        out_lit(d, "]");
        return;
    }
    if (identifier) out(d, sv);
    else if (!string_named && is_numeric_name(sv)) out(d, sv);
    else display_quoted_string(d, sv, single, true);
}

static void display_prop_name(Checker* me, TyDisplay d, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (unique) {
        out_lit(d, "[");
        // A computed name is an expression (tsc's symbolToExpression): a chain that would start at a module isn't
        // yielded, the variable prints by its bare name
        UniqueSymbolDecl const* decl = unique->literal.unique;
        if (d.enclosing && !d.tracker && decl->owner && !span_valid(decl->member)) {
            SymbolChain chain = symbol_chain_at(d.enclosing, decl->owner, SYMBOL_VALUE, true);
            if (chain.len && chain.items[0]->is_module) {
                out(d, ty_symbol_display_name(decl->owner).sv);
                out_lit(d, "]");
                return;
            }
        }
        // (Printed from nowhere, the name is still looked up from the property's declaration, never bare: tsc's
        // addPropertyToElementList makes it the enclosing declaration, `[Symbol.toStringTag]`)
        d.typeof_unique_symbols = false;
        out_unique_symbol_name(me, d, decl);
        out_lit(d, "]");
        return;
    }
    csview sv = atom_sv(name);
    if (is_identifier_name(sv) || is_numeric_name(sv)) {
        out(d, sv);
    } else {
        display_string_literal(d, sv);
    }
}

// Node reuse. tsc prints a type for a declaration by reusing the declaration's own annotation where it can
// (serializeTypeForDeclaration -> tryReuseExistingTypeNode): when the annotation's type is the type being printed, it
// prints *the node*, as written (`Array<string>`, `typeof a`, `(() => S)[]`, `Omit<O, "p">`). Only for the types
// baseline (TyDisplay.underline), where the reused parts are the spaces of the `^^^` line. A name in the annotation that
// doesn't mean the same from where the type is printed isn't reused: that part prints its type instead (tsc's
// visitExistingNodeTreeSymbols recovers per node)

static void display_node(Checker* me, TyDisplay d, TypeNode const* node);
static bool is_identifier_name(csview sv);
static void display_string_literal(TyDisplay d, csview sv);
static void display_template_text(TyDisplay d, csview sv);
static void append_binding_pattern(cstr* out, Binding const* binding);

// What a type parameter is called where it's printed, see TyParamNames
static Atom const* numbered_name(Checker* me, csview base, char const* suffix_format, u32 n);
static Atom const* type_param_name(Checker* me, TyDisplay d, Ty const* param) {
    TyParamNames* names = d.param_names;
    if (!names || param->param.state->is_this) return param->param.name;
    for (u32 i = names->len; i--;) {
        if (names->given[i].param == param) return names->given[i].name;
    }
    Ty const* declared = param; // (A clone made by instantiating a signature has its original's symbol)
    while (declared->param.state->clone_of) declared = declared->param.state->clone_of;
    Atom const* name = param->param.name;
    for (u32 n = 1;; n++) {
        bool taken = false;
        for (u32 i = 0; i < names->len && !taken; i++) taken = names->given[i].name == name;
        // (tsc's typeParameterShadowsOtherTypeParameterInScope)
        for (u32 i = names->in_scope.len; i-- && !taken;) {
            Ty const* other = names->in_scope.data[i];
            if (other->param.name != name) continue;
            taken = other != declared;
            break;
        }
        if (!taken) break;
        name = numbered_name(me, atom_sv(param->param.name), "_%u", n);
    }
    if (names->len == names->cap) {
        names->cap = names->cap ? names->cap * 2 : 8;
        names->given = realloc(names->given, sizeof(TyParamName) * names->cap);
    }
    names->given[names->len++] = (TyParamName){ .param = param, .name = name };
    return name;
}

// A renamed one is a new identifier, which isn't reused, except in a reused node (tsc's trackExistingEntityName gives
// it the range of the node it replaces)
static void out_type_param_name(Checker* me, TyDisplay d, Ty const* param, bool in_node) {
    Atom const* name = type_param_name(me, d, param);
    out(name == param->param.name || in_node ? d : reused_if(d, false), atom_sv(name));
}

// tsc's enterNewScope() for a signature: names given inside it don't count outside
static u32 param_names_enter(TyDisplay d) {
    return d.param_names ? d.param_names->len : 0;
}
static void param_names_leave(TyDisplay d, u32 saved) {
    if (d.param_names) d.param_names->len = saved;
}

static bool name_means_same(Checker* me, Symbol const* found, Symbol const* symbol);

// tsc's trackExistingEntityName(): the first name of an entity name means the same where the type is printed
static bool name_accessible(Checker* me, TyDisplay d, Symbol const* symbol, csview first, u32 meaning) {
    // Unresolved names are an error type; type parameters and parameters in scope where they're printed (tsc checks
    // that by resolving from the signature's fake scope)
    // (A name the signatures being printed declare as a parameter means that parameter there: a rest tuple's expanded
    // elements are none that can be referenced, tsc's unknownSymbol)
    if (meaning == SYMBOL_VALUE) {
        for (TyDisplayParams const* i = d.printed_params; i; i = i->outer) {
            for (u32 j = 0; j < i->count; j++) {
                SigParam const* param = &i->params[j];
                if (!csview_equals_sv(atom_sv(param->name), first)) continue;
                return symbol && param->decl && param->decl->initializer.binding.tag == BINDING_IDENTIFIER &&
                    param->decl->initializer.binding.symbol == symbol;
            }
        }
    }
    // (A parameter is only in scope inside its function: printed elsewhere it means nothing, tsc's symAtLocation)
    if (symbol && symbol->is_parameter && span_valid(d.at) && symbol->decl_count && symbol->decls[0].block) {
        FunctionImpl const* function = symbol->decls[0].block->function;
        bool printed = false;
        for (TyDisplayParams const* i = d.printed_params; i && !printed; i = i->outer) printed = i->function == function;
        if (!printed && function && span_valid(function->full_span) && function->full_span.source == d.at.source &&
            !span_contains_span(function->full_span, d.at)
        ) {
            return false;
        }
    }
    if (!symbol || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return true;
    // (A name that doesn't resolve with this meaning, a value used as a type (TS2749): tsc's resolveEntityName finds
    // nothing, here or there, and the node is reused, AllowUnresolvedNames)
    if (meaning != SYMBOL_VALUE && (symbol->is_placeholder || !symbol_for_meaning(symbol, meaning))) return true;
    if (symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN) return true; // `object`
    // (A local of a function or block never is: tsc's isDeclarationVisible() is false for it, and `enclosing` only knows
    // file and namespace scopes anyway)
    if (!d.enclosing) return true; // (A message's type: tsc has no enclosing declaration to check against)
    Symbol const* found = scope_resolve(d.enclosing, first, meaning);
    if (!found) return false;
    if (!name_means_same(me, found, symbol)) return false;
    // (and it must be accessible from there: a local of a namespace no visible declaration contains isn't, unless
    // declaration emit painted it)
    return symbol_accessibility_at(d.enclosing, found, meaning).kind == SYMBOL_ACCESSIBLE;
}

// Whether `found`, where the type is printed, is the `symbol` the written name resolved to
static bool name_means_same(Checker* me, Symbol const* found, Symbol const* symbol) {
    if (same_symbol(me, found, symbol)) return true;
    // (Another alias of the same thing: `m4.d` written under `import m4 = require("./m")` in one file is reused in
    // another that imports the module as m4 too; or the thing itself, where an imported name is printed in the file
    // that declares it)
    if (!(found->flags & SYMBOL_ALIAS) && !(symbol->flags & SYMBOL_ALIAS)) return false;
    Symbol const* target = symbol->flags & SYMBOL_ALIAS ? symbol_resolve_alias((Symbol*)symbol) : symbol;
    Symbol const* found_target = found->flags & SYMBOL_ALIAS ? symbol_resolve_alias((Symbol*)found) : found;
    // (Two `import * as`/`import = require` of one module are two gathering symbols of the same module)
    return target && found_target && same_symbol(me, symbol_members(found_target), symbol_members(target));
}

// A type of the uninstantiated signature at the root of `sig` as `sig` has it
static Ty const* instantiate_for(Checker* me, Signature const* sig, Ty const* ty) {
    if (!sig || !sig->target) return ty;
    ty = instantiate_for(me, sig->target, ty);
    return sig->mapper ? ty_instantiate(me, ty, sig->mapper) : ty;
}

// The type of an annotation, the way the checker lowered it (and instantiated, see TyDisplay.mapped_by)
static Ty const* node_type(Checker* me, TyDisplay d, TypeNode const* node) {
    return instantiate_for(me, d.mapped_by, ty_from_node(me, node, (LowerCtx){ .generic = true }));
}

// A part of an annotation that can't be reused prints its type
static bool needs_parens_in_array(Checker* me, TyDisplay d, Ty const* ty);
static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens);
static bool is_whole_enum(Checker* me, Ty const* ty);
static bool conditional_elided(TyDisplay d, Ty const* ty);

static bool needs_parens_as_constituent(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty) || (ty->flags & TYF_BOOLEAN) || is_whole_enum(me, ty)) return false;
    bool conditional = ty->kind == TY_CONDITIONAL && !conditional_elided(d, ty);
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || conditional || is_function_like(me, d, ty);
}

static void display_node_fallback(Checker* me, TyDisplay d, TypeNode const* node) {
    bool element = d.array_element_node;
    bool constituent = d.constituent_node;
    d.array_element_node = false;
    d.constituent_node = false;
    Ty const* ty = node_type(me, d, node);
    bool parens = (element && needs_parens_in_array(me, d, ty)) || (constituent && needs_parens_as_constituent(me, d, ty));
    if (parens) out(reused_if(d, false), c_sv("("));
    ty_display(me, reused_if(d, false), ty);
    if (parens) out(reused_if(d, false), c_sv(")"));
}

static void display_node_args(Checker* me, TyDisplay d, VecSpannedType const* args) {
    if (!vec_spanned_type_size(args)) return;
    out_lit(d, "<");
    for_each (i, vec_spanned_type, args) {
        if (i_index) out_lit(d, ", ");
        // (tsc's factory parenthesizes a generic function type first, the parentheses are new)
        bool parens = !i_index && i->type.tag == TYPE_CALLABLE && !i->type.parenthesized &&
            !generic_empty(&i->type.callable->function->generic);
        if (parens) out(reused_if(d, false), c_sv("("));
        display_node(me, d, &i->type);
        if (parens) out(reused_if(d, false), c_sv(")"));
    }
    out_lit(d, ">");
}

// A JSDoc reference tsc prints as its type instead of reusing it (visitExistingNodeTreeSymbols): a name with a JSDoc
// meaning (`String`, `Object`, `array`...), one that finds nothing, a generic type without enough type arguments, a
// name an `@import` declares (not a visible declaration to tsc)
static bool has_prototype_members(Symbol const* symbol) {
    SymbolTable const* members = symbol_rare(symbol)->members;
    if (!members) return false;
    for (u32 i = 0; i < members->count; i++) {
        Symbol const* member = members->entries[i].symbol;
        for (u32 j = 0; member && j < member->decl_count; j++) {
            if (member->decls[j].kind == SYMBOL_DECL_ASSIGNMENT && member->decls[j].assignment == ASSIGNMENT_DECL_PROTOTYPE_PROPERTY) {
                return true;
            }
        }
    }
    return false;
}

static bool jsdoc_reference_reusable(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->jsdoc_intrinsic) return false;
    // A JSDoc `typeof import("./m").x` is reused as written: tsc's tryVisitSimpleTypeNode only prints the type of an
    // import type that the JSDoc fallback resolved as a value without `typeof`, or whose type arguments it filled in
    if (identifier_type_import_typeof(identifier) && identifier->target && (identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier))) {
        return true;
    }
    // (An unresolved name is reused as written: tsc's errorType for it, unresolved names allowed)
    // (A qualified one too, `lf.schema.Table`)
    if (!identifier->target) return node_type(me, d, node)->flags & TYF_ERROR;
    // A value used as a type (an expando `Outer.Inner = class {}`: tsc's getTypeFromJSDocValueReference) isn't found by
    // a type lookup, which reuse needs
    if (!(identifier->target->flags & (SYMBOL_TYPE | SYMBOL_ALIAS))) return false;
    // A constructor function assigned to a variable or property (`var D = function () { this.x = 1 }`): the
    // instance type's symbol is the function expression's, not the name's, so tsc's reuse check fails
    // (With `D.prototype.x = v` the members are the name's own, which is then the instance type's symbol)
    if ((identifier->target->flags & SYMBOL_CLASS) && (identifier->target->flags & (SYMBOL_VARIABLE | SYMBOL_PROPERTY)) &&
        !has_prototype_members(identifier->target)
    ) {
        return false;
    }
    Symbol const* first = identifier_type_namespace_count(identifier) ?
        (vec_symbol_cptr_size(identifier_type_namespace_symbols(identifier)) ? *identifier->rare->namespace_symbols.data : nullptr) : identifier->symbol;
    if (first && first->flags & SYMBOL_ALIAS && first->decl_count && first->decls[0].kind == SYMBOL_DECL_ALIAS &&
        ((PortItem const*)first->decls[0].node)->jsdoc
    ) {
        return false;
    }
    Symbol const* target = identifier->target;
    if (target->decl_count && target->decls[0].kind == SYMBOL_DECL_TYPE_ALIAS &&
        ((TypeDecl const*)target->decls[0].node)->jsdoc_twice
    ) {
        return false;
    }
    if (!identifier_type_namespace_count(identifier)) {
        // (`array`, `promise` and `Object` mean something else only without type arguments and noImplicitAny)
        static char const* const intended[] = { "Function", "function", "array", "promise", "Object" };
        bool plain = !compiler_options()->no_implicit_any && !vec_spanned_type_size(&identifier->generic_params);
        for (u32 i = 0; i < c_arraylen(intended); i++) {
            if (csview_equals(identifier->name.sv, intended[i])) {
                if (i < 2 || plain) return false;
            }
        }
    }
    Ty const* type = node_type(me, d, node);
    if (type->kind != TY_REFERENCE) return true;
    TyList params = type->reference.target->declared.type_params;
    u32 min = 0;
    for (u32 i = 0; i < params.len; i++) {
        if (!ty_param_default(me, params.data[i])) min = i + 1;
    }
    return (u32)vec_spanned_type_size(&identifier->generic_params) >= min;
}

static Expr const* entity_leftmost(Expr const* expr);

// How tsc's tryVisitTypeReference reprints a (non-keyword, non-import) type reference: as written, by a name of its
// target from where it's printed (serializeTypeName), or not at all (the node prints its type)
typedef enum IdentifierReuse { IDENTIFIER_REUSED, IDENTIFIER_RENAMED, IDENTIFIER_MAPPED, IDENTIFIER_FAILS } IdentifierReuse;
static IdentifierReuse identifier_reuse(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    bool qualified = identifier_type_namespace_count(identifier) > 0;
    Symbol const* symbol = identifier->symbol;
    if (qualified) {
        symbol = vec_symbol_cptr_size(identifier_type_namespace_symbols(identifier)) ?
            *vec_symbol_cptr_at(identifier_type_namespace_symbols(identifier), 0) : nullptr;
    }
    Span first = qualified ? *vec_span_at(identifier_type_namespaces(identifier), 0) : identifier->name;
    // (A type parameter that the instantiation maps: tsc's canReuseTypeNode)
    // (Any reference whose type the instantiation changes: tsc's canReuseTypeNode with noMappedTypes, `State<T>` with
    // T mapped prints its type, `State<{ foo: number; }>`, as a new node)
    bool mapped = d.mapped_by && ((symbol && (symbol->flags & SYMBOL_TYPE_PARAMETER) && !qualified) ||
        vec_spanned_type_size(&identifier->generic_params)) &&
        node_type(me, d, node) != node_type(me, (TyDisplay){0}, node);
    if (mapped) return IDENTIFIER_MAPPED;
    if (name_accessible(me, d, symbol, first.sv, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE)) return IDENTIFIER_REUSED;
    Symbol const* target = identifier->target;
    if (target && !target->is_local && !(target->flags & SYMBOL_TYPE_PARAMETER) &&
        symbol_type_accessible_at(d.enclosing, target)
    ) {
        return IDENTIFIER_RENAMED;
    }
    return IDENTIFIER_FAILS;
}

// tsc's tryVisitSimpleTypeNode fails (the object of an indexed access, the operand of `keyof`): the whole indexed
// access or `keyof` prints its type then
static bool simple_node_fails(Checker* me, TyDisplay d, TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            if (identifier->keyword || identifier->jsdoc || identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier)) return false;
            return identifier_reuse(me, d, node) == IDENTIFIER_FAILS;
        }
        case TYPE_TYPEOF: {
            Expr const* leftmost = entity_leftmost(node->typeof_->expr);
            return leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
                !name_accessible(me, d, leftmost->identifier.symbol, leftmost->inner_span.sv, SYMBOL_VALUE);
        }
        case TYPE_INDEXED: return simple_node_fails(me, d, &node->indexed->interface);
        case TYPE_KEYOF: return simple_node_fails(me, d, &node->keyof->interface);
        default: return false;
    }
}

static void display_node_identifier(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->keyword) {
        out(d, identifier->name.sv);
        return;
    }
    if (identifier->jsdoc && !jsdoc_reference_reusable(me, d, node)) {
        display_node_fallback(me, d, node);
        return;
    }
    if (identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier)) {
        if (identifier_type_import_typeof(identifier)) out_lit(d, "typeof ");
        out_lit(d, "import(");
        ModuleRef const* ref = identifier_type_import_ref(identifier);
        // (Reused from another file, the specifier is made anew from here: tsc's rewriteModuleSpecifier)
        SourceFile const* here = d.enclosing && span_valid(d.at) ? d.at.source : nullptr;
        SourceFile const* written = span_valid(identifier->name) ? identifier->name.source : nullptr;
        if (ref && here && written && written != here && ref->module_state == ALIAS_DONE && ref->module &&
            ref->module->is_module && ref->module->module && ref->module->module->file
        ) {
            // (Named after the module the chain of what it refers to starts at, when there is one: a re-exporting
            // package entry rather than the declaring file)
            Symbol const* module = ref->module;
            Symbol const* target = identifier->target;
            if (target && span_valid(identifier->name)) {
                u32 meaning = identifier_type_import_typeof(identifier) ? SYMBOL_VALUE : SYMBOL_TYPE;
                SymbolChain chain = symbol_chain_at(d.enclosing, target, meaning, true);
                if (chain.len && chain.items[0]->is_module && chain.items[0]->module && chain.items[0]->module->file) {
                    module = chain.items[0];
                }
            }
            cstr name = program_module_display_name(my program, module, here, SPECIFIER_MODE_NONE, d.usage_ref);
            out(d, cstr_sv(&name));
            cstr_drop(&name);
        } else if (ref) {
            display_string_literal(d, cstr_sv(&ref->path));
        } else {
            display_node(me, d, identifier_type_import_argument(identifier));
        }
        out_lit(d, ")");
        for_each (i, vec_span, identifier_type_namespaces(identifier)) {
            out_lit(d, ".");
            out(d, i->sv);
        }
        if (span_valid(identifier->name)) {
            out_lit(d, ".");
            out(d, identifier->name.sv);
        }
        display_node_args(me, d, &identifier->generic_params);
        return;
    }
    bool qualified = identifier_type_namespace_count(identifier) > 0;
    Symbol const* symbol = identifier->symbol;
    IdentifierReuse reuse = identifier_reuse(me, d, node);
    if (reuse != IDENTIFIER_REUSED) {
        isize_t start = cstr_size(d.target);
        isize_t underline_start = d.underline ? cstr_size(d.underline) : 0;
        // tsc names what it refers to anew (serializeTypeName) and keeps the type arguments, unless no accessible
        // name reaches it (then the reference prints its type)
        Symbol const* target = identifier->target;
        bool mapped = reuse == IDENTIFIER_MAPPED;
        if (reuse == IDENTIFIER_RENAMED) {
            out_symbol_name(me, reused_if(d, false), target, SYMBOL_TYPE);
            display_node_args(me, d, &identifier->generic_params);
        } else {
            display_node_fallback(me, d, node);
        }
        // tsc names it anew (serializeTypeName) in a node it marks as reused: of an `import("m").A` it made, the
        // `import(` and `).` are reused and the rest isn't (of an `import("m")`, the `import(` and `)`)
        char const* text = cstr_str(d.target) + start;
        if (!mapped && d.underline && strncmp(text, "import(\"", 8) == 0) {
            char const* close = strstr(text, "\").");
            char* underline = (char*)cstr_str(d.underline) + underline_start;
            char const* quote = strchr(text + 8, '"');
            if (close) {
                memset(underline, ' ', strlen("import("));
                memset(underline + (close + 1 - text), ' ', 2);
            } else if (quote && quote[1] == ')' && !quote[2]) {
                memset(underline, ' ', strlen("import("));
                underline[quote + 1 - text] = ' ';
            }
        }
        return;
    }
    for_each (i, vec_span, identifier_type_namespaces(identifier)) {
        out(d, i->sv);
        out_lit(d, ".");
    }
    if (!qualified && d.param_names && symbol && (symbol->flags & SYMBOL_TYPE_PARAMETER)) {
        out_type_param_name(me, d, ty_type_param(me, symbol), true);
    } else {
        out(d, identifier->name.sv);
    }
    display_node_args(me, d, &identifier->generic_params);
}

// The entity name of `typeof a.b<T>`
static Expr const* entity_leftmost(Expr const* expr) {
    while (true) {
        if (expr->tag == EXPR_PROPERTY) expr = &expr->property->expr;
        else if (expr->tag == EXPR_GENERIC_PARAMS) expr = &expr->generic_params->expr;
        else return expr;
    }
}

static void display_entity_expr(Checker* me, TyDisplay d, Expr const* expr) {
    if (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) {
        display_entity_expr(me, d, &expr->property->expr);
        out_lit(d, ".");
        out(d, expr->property->prop.inner_span.sv);
    } else if (expr->tag == EXPR_GENERIC_PARAMS) {
        display_entity_expr(me, d, &expr->generic_params->expr);
        display_node_args(me, d, &expr->generic_params->params);
    } else {
        out(d, expr->inner_span.sv);
    }
}

static void display_node_typeof(Checker* me, TyDisplay d, TypeNode const* node) {
    Expr const* leftmost = entity_leftmost(node->typeof_->expr);
    if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        !name_accessible(me, d, leftmost->identifier.symbol, leftmost->inner_span.sv, SYMBOL_VALUE)
    ) {
        // tsc's tryVisitTypeQuery: a name that means something else here is named anew where its symbol is accessible
        // (serializeTypeName), in a node with the query's range: `typeof import("a").x` reuses `typeof import(` and
        // `).`, `typeof a.x` (through a namespace import) only `typeof `. Only for a plain name
        Symbol const* symbol = leftmost == node->typeof_->expr ? leftmost->identifier.symbol : nullptr;
        if (symbol && symbol->flags & SYMBOL_ALIAS) symbol = symbol_resolve_alias((Symbol*)symbol);
        if (symbol && !symbol->is_local && !symbol->is_parameter && !symbol->is_placeholder && symbol->parent &&
            d.enclosing && d.underline && symbol_value_accessible_at(d.enclosing, symbol)
        ) {
            out_lit(d, "typeof ");
            isize_t start = cstr_size(d.target);
            isize_t underline_start = cstr_size(d.underline);
            out_symbol_name(me, reused_if(d, false), symbol, SYMBOL_VALUE);
            char const* text = cstr_str(d.target) + start;
            char const* close = strncmp(text, "import(\"", 8) == 0 ? strstr(text, "\").") : nullptr;
            if (close && d.reused) {
                char* underline = (char*)cstr_str(d.underline) + underline_start;
                memset(underline, ' ', strlen("import("));
                memset(underline + (close + 1 - text), ' ', 2);
            }
            return;
        }
        display_node_fallback(me, d, node);
        return;
    }
    out_lit(d, "typeof ");
    display_entity_expr(me, d, node->typeof_->expr);
}

static void display_node_type_params(Checker* me, TyDisplay d, Generic const* generic) {
    if (generic_empty(generic)) return;
    out_lit(d, "<");
    for_each (i, vec_generic_item, &generic->items) {
        if (i_index) out_lit(d, ", ");
        if (i->is_const) out_lit(d, "const ");
        if (i->is_in) out_lit(d, "in ");
        if (i->is_out) out_lit(d, "out ");
        if (d.param_names && i->symbol_ref) out_type_param_name(me, d, ty_type_param(me, i->symbol_ref), true);
        else out(d, i->name.sv);
        if (i->extends_explicit) {
            out_lit(d, " extends ");
            display_node(me, d, &i->extends);
        }
        if (i->fallback_explicit) {
            out_lit(d, " = ");
            display_node(me, d, i->fallback);
        }
    }
    out_lit(d, ">");
}

// A signature written as a type: `<T>(x: T) => U` with `separator` " => ", a member's `(x: T): U` with ": "
static void display_node_signature_inner(Checker* me, TyDisplay d, FunctionImpl const* function, char const* separator);
static void display_node_signature(Checker* me, TyDisplay d, FunctionImpl const* function, char const* separator) {
    u32 saved = param_names_enter(d);
    display_node_signature_inner(me, d, function, separator);
    param_names_leave(d, saved);
}

static void display_node_signature_inner(Checker* me, TyDisplay d, FunctionImpl const* function, char const* separator) {
    if (function->is_abstract_constructor) out_lit(d, "abstract ");
    if (function->is_constructor || function->is_abstract_constructor) out_lit(d, "new ");
    display_node_type_params(me, d, &function->generic);
    out_lit(d, "(");
    for_each (i, vec_function_arg, &function->args) {
        if (i_index) out_lit(d, ", ");
        Binding const* binding = &i->initializer.binding;
        // (tsc's printer makes a JSDoc `...T` parameter `...args`, of which the name counts as reused)
        bool jsdoc_rest = function->jsdoc_type && i->rest && !span_is_written(binding->content_span);
        if (i->rest) out(jsdoc_rest ? reused_if(d, false) : d, c_sv("..."));
        if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
            cstr pattern = cstr_init();
            append_binding_pattern(&pattern, binding);
            out(d, cstr_sv(&pattern));
            cstr_drop(&pattern);
        } else if (jsdoc_rest) {
            out(d, c_sv("args"));
        } else {
            out(d, binding->content_span.sv);
        }
        if (i->explicit_optional) out_lit(d, "?");
        // (tsc reprints a JSDoc function type's parameters as new declarations around the written names and types)
        out(function->jsdoc_type ? reused_if(d, false) : d, c_sv(": "));
        if (binding->has_explicit_type) display_node(me, d, &binding->explicit_type);
        else out(reused_if(d, false), c_sv("any")); // tsc gives it a synthesized `any`
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TypePredicateNode const* predicate = function->predicate;
    if (predicate) {
        if (predicate->asserts) out_lit(d, "asserts ");
        out(d, predicate->name.sv);
        if (predicate->has_type) {
            out_lit(d, " is ");
            display_node(me, d, &predicate->type);
        }
    } else if (function->ret_explicit) {
        display_node(me, d, &function->ret);
    } else {
        out(reused_if(d, false), c_sv("any"));
    }
}

// A computed member name `[a.b]`. One whose name isn't accessible prints its value instead (tsc's
// visitExistingNodeTreeSymbolsWorker, ComputedPropertyName), as written if it has none (a unique symbol)
static void display_node_computed_name(Checker* me, TyDisplay d, Expr const* expr) {
    Expr const* leftmost = entity_leftmost(expr);
    // (declaration emit: a late bindable name in a reused node is tracked, visitExistingNodeTreeSymbols)
    if (d.tracker && leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        (ty_expr_type(me, expr)->flags & TYF_UNIQUE_ES_SYMBOL)
    ) {
        Span name = expr_outer_span(expr);
        d.tracker->track_computed_name(d.tracker, csview_slice(name.sv, 0, name.sv.size), name.source);
    }
    if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        !name_accessible(me, d, leftmost->identifier.symbol, leftmost->inner_span.sv, SYMBOL_VALUE)
    ) {
        Ty const* key = ty_expr_type(me, expr);
        if (key->kind == TY_LITERAL && key->literal.string && !(key->flags & TYF_UNIQUE_ES_SYMBOL)) {
            csview name = atom_sv(key->literal.string);
            if (is_identifier_name(name)) {
                out(d, name);
            } else {
                out_lit(d, "[");
                display_string_literal(d, name);
                out_lit(d, "]");
            }
            return;
        }
        if (key->kind == TY_LITERAL && key->literal.value.tag == VALUE_NUMBER && !(key->flags & TYF_BIGINT_LITERAL)) {
            bool negative = key->literal.value.number < 0;
            if (negative) out_lit(d, "[");
            isize_t start = cstr_size(d.target);
            cstr_append_value_js(d.target, &key->literal.value);
            underline_since(d, start);
            if (negative) out_lit(d, "]");
            return;
        }
    }
    out_lit(d, "[");
    out(d, expr_outer_span(expr).sv);
    out_lit(d, "]");
}

// A computed name that isn't a literal or unique symbol isn't a member (tsc's shouldRemoveDeclaration), except an
// unresolved entity name (`[Symbol.match]` without the lib that has it: the types baseline allows unresolved names,
// a message's typeToString doesn't)
static bool member_printed(Checker* me, TyDisplay d, InterfaceMember const* member) {
    if (member->key.tag != INTERFACE_KEY_EXPR || !member->key.expr.ref) return true;
    Ty const* key = ty_expr_type(me, member->key.expr.ref);
    return key->kind == TY_LITERAL ||
        ((d.underline || d.tracker) && (key->flags & TYF_ANY) && expr_is_entity_name(member->key.expr.ref));
}

// A JSDoc type literal the way tsc's printer reprints it, one member per line, which the types baseline flattens:
// `{    a: number;    b?: number;}` (the indentation, names, types and braces reused, the rest not). An optional
// member's type has undefined under strictNullChecks, which isn't the node's
static void display_node_jsdoc_literal(Checker* me, TyDisplay d, Interface const* interface) {
    uint8_t depth = (uint8_t)(d.jsdoc_literal_depth + 1);
    TyDisplay inner = d;
    inner.jsdoc_literal_depth = depth;
    out_lit(d, "{");
    for_each (i, vec_interface_member, &interface->members) {
        if (!member_printed(me, d, i)) continue;
        for (uint8_t k = 0; k < depth; k++) out_lit(d, "    ");
        out(d, i->key.span.sv);
        out(reused_if(d, false), i->optional ? c_sv("?: ") : c_sv(": "));
        if (i->optional && compiler_options()->strict_null_checks) {
            Ty const* type = ty_of_node(&i->type);
            ty_display(me, reused_if(inner, false), ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2,
                nullptr, (TyList){0}));
        } else if (i->type.parsed) {
            display_node(me, inner, &i->type);
        } else {
            out(reused_if(d, false), c_sv("any"));
        }
        out(reused_if(d, false), c_sv(";"));
    }
    for (uint8_t k = 0; k + 1 < depth; k++) out_lit(d, "    ");
    out_lit(d, "}");
}

static void display_node_interface(Checker* me, TyDisplay d, Interface const* interface) {
    bool empty = true;
    for_each (i, vec_interface_member, &interface->members) empty &= !member_printed(me, d, i);
    if (empty) {
        out_lit(d, "{}");
        return;
    }
    bool names_only = true;
    for_each (i, vec_interface_member, &interface->members) names_only &= i->key.tag == INTERFACE_KEY_NAME;
    if (interface->jsdoc_literal && names_only) {
        display_node_jsdoc_literal(me, d, interface);
        return;
    }
    out_lit(d, "{ ");
    for_each (i, vec_interface_member, &interface->members) {
        if (!member_printed(me, d, i)) continue;
        switch (i->key.tag) {
            case INTERFACE_KEY_MAPPED: {
                u32 saved = param_names_enter(d); // (A mapped type is a scope)
                if (i->key.mapped->readonly) out(d, i->key.mapped->readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
                out_lit(d, "[");
                if (d.param_names && i->key.mapped->symbol_ref) {
                    out_type_param_name(me, d, ty_type_param(me, i->key.mapped->symbol_ref), true);
                } else {
                    out(d, i->key.mapped->type_name.sv);
                }
                out_lit(d, " in ");
                display_node(me, d, &i->key.mapped->in_type);
                if (i->key.mapped->has_as_type) {
                    out_lit(d, " as ");
                    display_node(me, d, &i->key.mapped->as_type);
                }
                out_lit(d, "]");
                if (i->key.mapped->optional) out(d, i->key.mapped->optional > 0 ? c_sv("?") : c_sv("-?"));
                out_lit(d, ": ");
                if (i->type.parsed) display_node(me, d, &i->type);
                else out(reused_if(d, false), c_sv("any"));
                param_names_leave(d, saved);
            }   break;
            case INTERFACE_KEY_INDEX:
                if (i->readonly) out_lit(d, "readonly ");
                out_lit(d, "[");
                out(d, i->key.index->name.sv);
                out_lit(d, ": ");
                display_node(me, d, &i->key.index->type);
                out_lit(d, "]: ");
                display_node(me, d, &i->type);
                break;
            case INTERFACE_KEY_CALLABLE:
                display_node_signature(me, d, i->type.callable->function, ": ");
                break;
            case INTERFACE_KEY_EXPR:
            case INTERFACE_KEY_NAME:
                if (i->readonly) out_lit(d, "readonly ");
                if (i->key.tag == INTERFACE_KEY_EXPR) {
                    display_node_computed_name(me, d, i->key.expr.ref);
                } else {
                    out(d, i->key.span.sv);
                }
                if (i->optional) out_lit(d, "?");
                if ((i->key.tag == INTERFACE_KEY_NAME ? i->key.name_is_member_function : i->is_method) && i->type.tag == TYPE_CALLABLE) {
                    display_node_signature(me, d, i->type.callable->function, ": ");
                } else {
                    out_lit(d, ": ");
                    if (i->type.parsed) display_node(me, d, &i->type);
                    else out(reused_if(d, false), c_sv("any"));
                }
                break;
        }
        out_lit(d, "; ");
    }
    out_lit(d, "}");
}

static void display_node_list(Checker* me, TyDisplay d, VecTypeNode const* list, char const* separator) {
    bool first = true;
    for_each (i, vec_type, list) {
        if (i->placeholder) continue;
        if (!first) out(d, csview_from(separator));
        first = false;
        // A member that needs parentheses without having them written: JSDoc's `?T`, `T=` of a union or function
        // type, and a union or intersection in a union or intersection (tsc's visitor recreates the list node, and the
        // factory's parenthesizeConstituentTypeOfUnionType/IntersectionType wraps them)
        bool parens = !i->parenthesized && (i->tag == TYPE_CALLABLE || i->tag == TYPE_UNION ||
            i->tag == TYPE_INTERSECTION || i->tag == TYPE_TERNARY ||
            (i->tag == TYPE_JSDOC && !i->jsdoc->non_null && i->jsdoc->type.tag == TYPE_UNION));
        if (parens) out(reused_if(d, false), c_sv("("));
        TyDisplay member_d = d;
        member_d.constituent_node = !parens;
        display_node(me, member_d, i);
        if (parens) out(reused_if(d, false), c_sv(")"));
    }
}

// tsc's tryVisitSimpleTypeNode (the object of an indexed access, the operand of `keyof`) reprints a type reference,
// `typeof`, indexed access or `keyof` without the parentheses written around it
static TypeNode node_without_simple_parens(TypeNode const* node) {
    TypeNode copy = *node;
    if (copy.parenthesized && (copy.tag == TYPE_IDENTIFIER || copy.tag == TYPE_TYPEOF || copy.tag == TYPE_INDEXED ||
        copy.tag == TYPE_KEYOF)) {
        copy.parenthesized = false;
    }
    return copy;
}

// Prints an annotation as written, as reused
static void display_node(Checker* me, TyDisplay d, TypeNode const* node) {
    d.reused = !node->synthesized;
    // (Only a fallback of this very node, see display_node_identifier() and display_node_typeof(), is an element)
    bool array_element = d.array_element_node && !node->parenthesized;
    bool constituent = d.constituent_node && !node->parenthesized;
    d.array_element_node = false;
    d.constituent_node = false;
    if (node->parenthesized) out_lit(d, "(");
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            TyDisplay element_d = d;
            element_d.array_element_node = array_element;
            element_d.constituent_node = constituent;
            display_node_identifier(me, element_d, node);
            break;
        }
        case TYPE_TYPEOF: {
            TyDisplay element_d = d;
            element_d.array_element_node = array_element;
            element_d.constituent_node = constituent;
            display_node_typeof(me, element_d, node);
            break;
        }
        case TYPE_CALLABLE:
            display_node_signature(me, d, node->callable->function, " => ");
            break;
        case TYPE_INTERFACE:
            display_node_interface(me, d, &node->interface);
            break;
        case TYPE_ARRAY: {
            if (node->array.readonly) out_lit(d, "readonly ");
            TyDisplay element_d = d;
            element_d.array_element_node = true;
            display_node(me, element_d, node->array.element);
            out_lit(d, "[]");
            break;
        }
        case TYPE_TUPLE:
            if (node->tuple.readonly) out_lit(d, "readonly ");
            out_lit(d, "[");
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (i_index) out_lit(d, ", ");
                if (i->rest) out_lit(d, "...");
                if (span_valid(i->maybe_name)) {
                    out(d, i->maybe_name.sv);
                    if (i->optional) out_lit(d, "?");
                    out_lit(d, ": ");
                    display_node(me, d, &i->type);
                } else {
                    display_node(me, d, &i->type);
                    if (i->optional) out_lit(d, "?");
                }
            }
            out_lit(d, "]");
            break;
        case TYPE_INDEXED: {
            if (simple_node_fails(me, d, &node->indexed->interface)) {
                display_node_fallback(me, d, node);
                break;
            }
            // tsc's tryVisitIndexedAccess: the object without its parentheses if it's simple, then the factory's
            // parenthesizeNonArrayTypeOfPostfixType (`typeof a['a']` prints `(typeof a)["a"]`)
            TypeNode object = node_without_simple_parens(&node->indexed->interface);
            bool parens = !object.parenthesized && (object.tag == TYPE_TYPEOF || object.tag == TYPE_KEYOF);
            if (parens) out(reused_if(d, false), c_sv("("));
            display_node(me, d, &object);
            if (parens) out(reused_if(d, false), c_sv(")"));
            out_lit(d, "[");
            display_node(me, d, &node->indexed->index);
            out_lit(d, "]");
            break;
        }
        case TYPE_KEYOF: {
            if (simple_node_fails(me, d, &node->keyof->interface)) {
                display_node_fallback(me, d, node);
                break;
            }
            out_lit(d, "keyof ");
            TypeNode operand = node_without_simple_parens(&node->keyof->interface); // (tsc's tryVisitKeyOf)
            display_node(me, d, &operand);
            break;
        }
        case TYPE_TERNARY: {
            display_node(me, d, &node->ternary->maybe_sub);
            out_lit(d, " extends ");
            u32 saved = param_names_enter(d); // (Its `infer` type parameters are in scope in the true branch)
            display_node(me, d, &node->ternary->maybe_super);
            out_lit(d, " ? ");
            display_node(me, d, &node->ternary->if_true);
            param_names_leave(d, saved);
            out_lit(d, " : ");
            display_node(me, d, &node->ternary->if_false);
        }   break;
        case TYPE_INFERRED:
            out_lit(d, "infer ");
            if (d.param_names && node->inferred->symbol_ref) {
                out_type_param_name(me, d, ty_type_param(me, node->inferred->symbol_ref), true);
            } else {
                out(d, node->inferred->name.sv);
            }
            if (node->inferred->extends.parsed) {
                out_lit(d, " extends ");
                display_node(me, d, &node->inferred->extends);
            }
            break;
        case TYPE_UNION:
            if (node->jsdoc_variadic && vec_type_size(&node->composite) == 2) {
                TypeNode const* element = vec_type_at(&node->composite, 0);
                bool parens = !element->parenthesized && (element->tag == TYPE_UNION || element->tag == TYPE_INTERSECTION ||
                    element->tag == TYPE_CALLABLE);
                if (parens) out(reused_if(d, false), c_sv("("));
                display_node(me, d, element);
                if (parens) out(reused_if(d, false), c_sv(")"));
                out_lit(d, "[]");
                // (`...T=`: tsc's JSDocOptionalType around it, whose `undefined` is synthesized)
                if (node->jsdoc_optional) {
                    out_lit(d, " | ");
                    out(reused_if(d, false), c_sv("undefined"));
                }
                break;
            }
            display_node_list(me, d, &node->composite, " | ");
            break;
        case TYPE_INTERSECTION:
            display_node_list(me, d, &node->composite, " & ");
            break;
        case TYPE_TEMPLATE: {
            TemplateType const* template = node->template_;
            out_lit(d, "`");
            for (isize_t i = 0; i < vec_cstr_size(&template->texts); i++) {
                if (i) {
                    out_lit(d, "${");
                    display_node(me, d, vec_type_at(&template->types, i - 1));
                    out_lit(d, "}");
                }
                display_template_text(d, cstr_sv(vec_cstr_at(&template->texts, i)));
            }
            out_lit(d, "`");
        }   break;
        case TYPE_UNIQUE_SYMBOL:
            out_lit(d, "unique symbol");
            break;
        case TYPE_JSDOC: display_node(me, d, &node->jsdoc->type); break;
        case TYPE_PREDICATE:
            if (node->predicate->asserts) out_lit(d, "void");
            else out_lit(d, "boolean");
            break;
        case TYPE_ANY:
            if (node->jsdoc_unknown) out_lit(d, "unknown");
            else out_lit(d, "any");
            break;
        case TYPE_UNKNOWN: out_lit(d, "unknown"); break;
        case TYPE_NEVER: out_lit(d, "never"); break;
        case TYPE_VOID: out_lit(d, "void"); break;
        case TYPE_BOOLEAN: out_lit(d, "boolean"); break;
        case TYPE_NUMBER: out_lit(d, "number"); break;
        case TYPE_STRING: out_lit(d, "string"); break;
        case TYPE_SYMBOL: out_lit(d, "symbol"); break;
        case TYPE_BIGINT: out_lit(d, "bigint"); break;
        case TYPE_VALUE: {
            // Literals print normalized, like tsc's printer does a cloned literal node (`'a'` is `"a"`, `0x10` is 16)
            Ty const* literal = ty_literal(me, &node->value);
            if (literal->kind == TY_LITERAL && !(literal->flags & (TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL)) &&
                !literal->literal.string && node->value.tag == VALUE_NUMBER && node->value.number < 0
            ) {
                out_lit(d, "-");
                ty_display(me, d, ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = -node->value.number }));
            } else {
                ty_display(me, d, literal);
            }
        }   break;
    }
    if (node->parenthesized) out_lit(d, ")");
}

// Whether an annotation says what is printed (tsc's typeNodeIsEquivalentToType: an optional parameter or property
// without its `undefined`), so that it can be reprinted
static bool annotation_is(Checker* me, TypeNode const* node, Ty const* ty, bool optional) {
    // (A `unique symbol` where it can't be one is plain symbol, which it says as well as any annotation: tsc reuses it)
    if (!node->parsed || (node->tag == TYPE_UNIQUE_SYMBOL && node->unique_symbol.key)) return false;
    Ty const* annotated = node_type(me, (TyDisplay){0}, node);
    // (An annotation that is the error type is reused whatever the type is: tsc's canReuseTypeNodeAnnotation)
    return annotated == ty || (optional && annotated == ty_without_undefined(me, ty)) ||
        (annotated->flags & TYF_ERROR);
}

// Prints `ty` by reprinting its declaration's annotation when that's allowed and says the same, else as a type
// Whether `ty` is what the annotation says with undefined added, the annotation being one that a union can take as it
// is (no function, constructor or conditional type, which would need new parentheses)
static bool annotation_plus_undefined(Checker* me, TypeNode const* node, Ty const* ty) {
    if (!node->parsed || ty->kind != TY_UNION || !compiler_options()->strict_null_checks) return false;
    if ((node->tag == TYPE_CALLABLE && !node->parenthesized) || (node->tag == TYPE_TERNARY && !node->parenthesized) ||
        node->tag == TYPE_UNIQUE_SYMBOL || node->tag == TYPE_JSDOC) return false;
    Ty const* annotated = node_type(me, (TyDisplay){0}, node);
    if (annotated == ty || ty_without_undefined(me, annotated) != annotated) return false;
    return ty_without_undefined(me, ty) == annotated;
}

// tsc's canAddUndefined (expressionToTypeNode.ts): an annotation made only of keywords, literals, function, array,
// tuple, object literal and template literal types takes `undefined` syntactically
static bool can_add_undefined(TypeNode const* node, u32 depth) {
    if (depth > 8) return false;
    switch (node->tag) {
        case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_NEVER: case TYPE_VOID: case TYPE_BOOLEAN: case TYPE_NUMBER:
        case TYPE_STRING: case TYPE_SYMBOL: case TYPE_BIGINT: case TYPE_VALUE: case TYPE_CALLABLE: case TYPE_ARRAY:
        case TYPE_TUPLE: case TYPE_TEMPLATE:
            return true;
        case TYPE_INTERFACE: // (A type literal, not a mapped type)
            return vec_interface_member_size(&node->interface.members) != 1 ||
                vec_interface_member_at(&node->interface.members, 0)->key.tag != INTERFACE_KEY_MAPPED;
        case TYPE_IDENTIFIER:
            return node->identifier->keyword && !vec_spanned_type_size(&node->identifier->generic_params);
        case TYPE_UNION: case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (!i->placeholder && !can_add_undefined(i, depth + 1)) return false;
            }
            return true;
        default:
            return false;
    }
}

// An annotation in a new union with `undefined` (tsc's addUndefined): a union annotation's members are reused in a new
// union node, whose ` | ` aren't. One that can't take it syntactically (it names a type, can_add_undefined()) is the
// checker's whole clone in a new union (serializeExistingTypeNode), which a union or intersection is parenthesized in
static void display_node_plus_undefined(Checker* me, TyDisplay d, TypeNode const* node) {
    if ((node->tag == TYPE_UNION || node->tag == TYPE_INTERSECTION) && !node->parenthesized && !can_add_undefined(node, 0)) {
        out(reused_if(d, false), c_sv("("));
        display_node(me, d, node);
        out(reused_if(d, false), c_sv(")"));
    } else if (node->tag == TYPE_UNION && !node->parenthesized && !node->jsdoc_variadic && vec_type_size(&node->composite)) {
        for_each (i, vec_type, &node->composite) {
            if (i_index) out(reused_if(d, false), c_sv(" | "));
            display_node(me, d, i);
        }
    } else {
        display_node(me, d, node);
    }
    out(reused_if(d, false), c_sv(" | undefined"));
}

// Whether the printer reprints annotations (tsc's node reuse, which needs an enclosing declaration): the types
// baseline, which marks them in the underline, and a message's type with `reuse_nodes`
static bool reuses_nodes(TyDisplay d) {
    return (d.underline || d.reuse_nodes) && d.enclosing;
}

static void display_annotated(Checker* me, TyDisplay d, TypeNode const* annotation, Ty const* ty, bool optional) {
    if (annotation && reuses_nodes(d) && annotation_is(me, annotation, ty, optional)) {
        display_node(me, d, annotation);
    } else if (annotation && reuses_nodes(d) && !annotation->parsed && !annotation->synthesized) {
        ty_display(me, reused_if(d, true), ty); // A missing type (`(a: ) => {}`), which tsc reuses as `any`
    } else {
        ty_display(me, d, ty);
    }
}

// Whether a type may mention one of the type parameters. Conservative: object types that aren't references might
static bool mentions_at(Checker* me, Ty const* ty, TyList params, u32 depth) {
    if (!params.len) return false;
    // (A deferred reference's arguments can lead back to it: `type RD<T> = ReadonlyArray<RD<T>>`)
    if (depth > 32) return true;
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (mentions_at(me, ty->alias_args.data[i], params, depth + 1)) return true;
    }
    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
        case TY_LITERAL:
        case TY_DECLARED:
            return false;
        case TY_TYPE_PARAM:
            for (u32 i = 0; i < params.len; i++) {
                if (params.data[i] == ty) return true;
            }
            return false;
        case TY_REFERENCE: {
            TyList args = ty_reference_args(me, ty);
            for (u32 i = 0; i < args.len; i++) {
                if (mentions_at(me, args.data[i], params, depth + 1)) return true;
            }
            return false;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (mentions_at(me, ty->composite.types.data[i], params, depth + 1)) return true;
            }
            return false;
        case TY_KEYOF:
            return mentions_at(me, ty->keyof.operand, params, depth + 1);
        case TY_ANONYMOUS:
            // (What the lowering found it can mention, when it knows)
            if (ty->anonymous.free_params_unknown) return true;
            for (u32 i = 0; i < ty->anonymous.free_params.len; i++) {
                if (mentions_at(me, ty->anonymous.free_params.data[i], params, depth + 1)) return true;
            }
            return false;
        case TY_INDEXED:
            return mentions_at(me, ty->indexed.object, params, depth + 1) || mentions_at(me, ty->indexed.index, params, depth + 1);
        default:
            return true;
    }
}

static bool mentions(Checker* me, Ty const* ty, TyList params) { return mentions_at(me, ty, params, 0); }

// Whether a statement has a `return` in it (not counting nested functions)
static bool block_has_return(Block const* block);
static bool statements_have_return(VecStatement const* statements) {
    for_each (i, vec_statement, statements) {
        switch (i->tag) {
            case STATEMENT_RETURN_EXPR: return true;
            case STATEMENT_BLOCK: if (block_has_return(&i->block)) return true; break;
            case STATEMENT_IF_ELSE: if (statements_have_return(&i->if_else.statements)) return true; break;
            case STATEMENT_LABELED:
                if (statements_have_return(&(VecStatement){ .data = i->labeled.body, .size = 1 })) return true;
                break;
            case STATEMENT_FOR_LOOP:
                if (statements_have_return(&(VecStatement){ .data = &i->for_loop->body, .size = 1 })) return true;
                break;
            case STATEMENT_FOR_EACH_LOOP:
                if (statements_have_return(&(VecStatement){ .data = &i->for_each_loop->body, .size = 1 })) return true;
                break;
            case STATEMENT_WITH:
                if (statements_have_return(&(VecStatement){ .data = &i->with->body, .size = 1 })) return true;
                break;
            case STATEMENT_SWITCH:
                for_each (c, vec_switch_case, &i->switch_->cases) {
                    if (statements_have_return(&c->statements)) return true;
                }
                break;
            case STATEMENT_TRY_CATCH:
                if (block_has_return(&i->try_catch->try_block) || block_has_return(&i->try_catch->catch_block) ||
                    block_has_return(&i->try_catch->finally_block)
                ) {
                    return true;
                }
                break;
            default: break;
        }
    }
    return false;
}
static bool block_has_return(Block const* block) { return statements_have_return(&block->statements); }

// tsc's typeFromSingleReturnExpression(): the expression of a function's only `return`, when that is directly in its
// body (or an arrow function's expression body), null otherwise. A bare `return;` before it doesn't count (tsc keeps
// looking while it has no candidate: `return; return 1;` is `1`'s), after it there's none
static Expr const* single_return_expr(FunctionImpl const* function) {
    if (!function->has_body || function->is_generator) return nullptr;
    Expr const* found = nullptr;
    for_each (i, vec_statement, &function->body.statements) {
        if (i->tag == STATEMENT_RETURN_EXPR) {
            if (found) return nullptr;
            if (!statement_is_bare_return(i)) found = &i->expr;
        } else if (statements_have_return(&(VecStatement){ .data = (Statement*)i, .size = 1 })) {
            return nullptr;
        }
    }
    return found;
}

// A function without a return annotation whose only return is `return x as T` (or an arrow `=> <T>x`) prints its
// return type by reusing `T`
static TypeNode const* single_return_assertion(Checker* me, FunctionImpl const* function, Ty const* ret) {
    Expr const* expr = single_return_expr(function);
    if (!expr || expr->tag != EXPR_AS_TYPE) return nullptr;
    // (Where something may type it contextually only the assertion itself, `=> (<T>x)` isn't one: tsc's
    // typeFromFunctionLikeExpression with isContextuallyTyped, else typeFromExpression looks through parentheses)
    if (function->syntactically_contextual && expr_is_perenthisized(expr)) return nullptr;
    AsTypeExpr const* as_type = expr->as_type;
    if (as_type->is_satisfies || as_type_is_const(as_type) || ty_expr_type(me, expr) != ret) return nullptr;
    return &as_type->type;
}

// A type node that names nothing (keywords and literals, in unions, intersections, arrays and parentheses): reusing it
// asks no name whether it is accessible, so it is reused wherever the type is printed from
static bool type_node_names_nothing(TypeNode const* node, u32 depth) {
    if (depth > 8) return false;
    switch (node->tag) {
        case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_NEVER: case TYPE_VOID: case TYPE_BOOLEAN: case TYPE_NUMBER:
        case TYPE_STRING: case TYPE_SYMBOL: case TYPE_BIGINT: case TYPE_VALUE:
            return node->parsed && !node->synthesized && !node->from_jsdoc;
        case TYPE_IDENTIFIER:
            return node->identifier->keyword && !vec_spanned_type_size(&node->identifier->generic_params) &&
                !node->from_jsdoc;
        case TYPE_ARRAY:
            return type_node_names_nothing(node->array.element, depth + 1);
        case TYPE_UNION: case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (i->placeholder || !type_node_names_nothing(i, depth + 1)) return false;
            }
            return vec_type_size(&node->composite) > 0;
        default:
            return false;
    }
}

// A function without a return annotation whose only return is a function expression prints its return type
// syntactically (tsc's typeFromFunctionLikeExpression), unless something may type it contextually (isContextuallyTyped)
static FunctionImpl const* single_return_function(Checker* me, FunctionImpl const* function, Ty const* ret) {
    Expr const* expr = single_return_expr(function);
    if (!expr || expr->tag != EXPR_FUNCTION || expr->function->syntactically_contextual) return nullptr;
    return ty_expr_type(me, expr) == ret ? expr->function : nullptr;
}

// A function without a return annotation whose only return is a number, string, boolean or bigint literal prints its
// keyword (tsc's typeFromExpression of a primitive literal). Only observable where the checker's type is another:
// `return; return 1;` under strictNullChecks is `1 | undefined` to the checker, `number` to the printer
static Ty const* single_return_keyword(Checker* me, FunctionImpl const* function, Ty const* ret) {
    // (tsc's FunctionFlags.AsyncGenerator test is a mask; in a call or an annotated initializer it's isContextuallyTyped)
    if (function->is_async || function->syntactically_contextual) return nullptr;
    Expr const* expr = single_return_expr(function);
    if (!expr || expr->tag != EXPR_LITERAL) return nullptr;
    Ty const* keyword = nullptr;
    switch (expr->literal.tag) {
        case VALUE_NUMBER: keyword = ty_intrinsic(me, TY_NUMBER); break;
        case VALUE_STRING: keyword = ty_intrinsic(me, TY_STRING); break;
        case VALUE_BOOLEAN: keyword = ty_intrinsic(me, TY_BOOLEAN); break;
        case VALUE_BIGINT: keyword = ty_intrinsic(me, TY_BIGINT); break;
        case VALUE_NULL:
            if (!compiler_options()->strict_null_checks) return nullptr;
            keyword = ty_intrinsic(me, TY_NULL);
            break;
        default: return nullptr;
    }
    // (the checker's type is that literal, widened or not, maybe with undefined)
    TyList parts = ret->kind == TY_UNION ? ret->composite.types : (TyList){ .data = &ret, .len = 1 };
    for (u32 i = 0; i < parts.len; i++) {
        Ty const* part = parts.data[i];
        if (part->flags & TYF_UNDEFINED) continue;
        if (ty_base_type_of_literal(me, part) != keyword) return nullptr;
    }
    return keyword;
}

static void display_type_params(Checker* me, TyDisplay d, TyList params, bool reusable, bool syntactic) {
    if (!params.len) return;
    out_lit(d, "<");
    for (u32 i = 0; i < params.len; i++) {
        if (i) out_lit(d, ", ");
        // (A function printed syntactically reuses its type parameter declarations whole)
        Ty const* declared_param = params.data[i];
        GenericItem const* own = syntactic && declared_param->kind == TY_TYPE_PARAM ? declared_param->param.state->item : nullptr;
        if (own && !declared_param->param.state->clone_of && d.underline) {
            TyDisplay whole = reused_if(d, true);
            if (own->is_const) out_lit(whole, "const ");
            if (own->is_in) out_lit(whole, "in ");
            if (own->is_out) out_lit(whole, "out ");
            // (Renamed for shadowing too: the reused node keeps its range, tsc's trackExistingEntityName)
            out_type_param_name(me, whole, declared_param, true);
            if (own->extends_explicit) {
                out_lit(whole, " extends ");
                display_node(me, whole, &own->extends);
            }
            if (own->fallback) {
                out_lit(whole, " = ");
                display_node(me, whole, own->fallback);
            }
            continue;
        }
        // The name of a declared type parameter is always reused, the constraint like any annotation (tsc never reuses
        // the default)
        // (A clone made by instantiating the signature has its original's declaration)
        Ty const* declared = params.data[i];
        while (declared->kind == TY_TYPE_PARAM && declared->param.state->clone_of) declared = declared->param.state->clone_of;
        GenericItem const* item = declared->kind == TY_TYPE_PARAM ? declared->param.state->item : nullptr;
        // (A copy renamed by inference, `T1`, is a new symbol without declarations: tsc's getUniqueTypeParameterName)
        if (declared->kind == TY_TYPE_PARAM && declared->param.name != params.data[i]->param.name) item = nullptr;
        if (item && item->is_const) out_lit(d, "const ");
        if (item && item->is_in) out_lit(d, "in ");
        if (item && item->is_out) out_lit(d, "out ");
        ty_display(me, reused_if(d, d.reused || item), params.data[i]);
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (constraint) {
            out_lit(d, " extends ");
            if (d.reused) {
                ty_display(me, d, constraint);
            } else if (!d.underline && item && item->extends_explicit && annotation_is(me, &item->extends, constraint, false)) {
                // (tsc's typeParameterToDeclaration reuses the constraint as written in messages too:
                // typeToTypeNodeHelperWithPossibleReusableTypeNode)
                display_node(me, d, &item->extends);
            } else {
                display_annotated(me, d, reusable && item && item->extends_explicit ? &item->extends : nullptr, constraint, false);
            }
        }
        Ty const* fallback = ty_param_default(me, params.data[i]);
        if (fallback) {
            out_lit(d, " = ");
            ty_display(me, d, fallback);
        }
    }
    out_lit(d, ">");
}

void ty_display_predicate(Checker* me, TyDisplay d, TyPredicate const* predicate) {
    if (predicate->asserts) out_lit(d, "asserts ");
    out(d, atom_sv(predicate->param_name));
    if (predicate->type) {
        out_lit(d, " is ");
        ty_display(me, d, predicate->type);
    }
}

// `(a: A, b?: B, ...rest: C[])` followed by `separator` and the return type
static void append_binding_pattern(cstr* out, Binding const* binding);
static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new);

void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature(me, d, sig, ": ", (sig->flags & SIGNATURE_CONSTRUCT) != 0);
}

// tsc's signatureToString with WriteArrowStyleSignature: `new (x: number) => C`
void ty_display_signature_arrow(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature(me, d, sig, " => ", (sig->flags & SIGNATURE_CONSTRUCT) != 0);
}

// tsc's signatureToString without a kind: a construct signature prints like a call signature
void ty_display_call_signature(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature(me, d, sig, ": ", false);
}

static Atom const* numbered_name(Checker* me, csview base, char const* suffix_format, u32 n) {
    char buf[256];
    int len = snprintf(buf, sizeof buf, "%.*s", (int)base.size, base.buf);
    if (len < 0 || len >= (int)sizeof buf) len = 0;
    int more = snprintf(buf + len, sizeof buf - (size_t)len, suffix_format, n);
    if (more > 0) len += more;
    if (len >= (int)sizeof buf) len = (int)sizeof buf - 1;
    return checker_atom(me, c_sv(buf, (isize_t)len));
}

// tsc's getTupleElementLabelFromBindingElement: `binding` is a rest parameter's or an element of its array pattern
static Atom const* binding_element_label(Checker* me, Binding const* binding, bool rest, u32 index, unsigned char flags) {
    if (binding->tag == BINDING_IDENTIFIER) {
        Atom const* name = checker_atom(me, binding->content_span.sv);
        if (rest) return flags & TUPLE_ELEMENT_REST ? name : numbered_name(me, atom_sv(name), "_%u", index);
        if (!(flags & TUPLE_ELEMENT_REST)) return name;
        cstr numbered = cstr_d(atom_sv(name), "_n"); // (tsc's `${name}_n`, literally)
        Atom const* ret = checker_atom(me, cstr_sv(&numbered));
        cstr_drop(&numbered);
        return ret;
    }
    if (binding->tag == BINDING_ARRAY && rest) {
        u32 len = (u32)vec_binding_size(&binding->array);
        Binding const* last = len ? vec_binding_at(&binding->array, len - 1) : nullptr;
        u32 count = len - (last && last->rest ? 1 : 0);
        if (index < count) {
            Binding const* element = vec_binding_at(&binding->array, index);
            if (element->tag != BINDING_NOOP) return binding_element_label(me, element, element->rest, index, flags);
        } else if (last && last->rest) {
            return binding_element_label(me, last, true, index - count, flags);
        }
    }
    return numbered_name(me, c_sv("arg"), "_%u", index);
}

// What tsc names an unlabeled element of a rest tuple (getTupleElementLabel): after the rest parameter
static Atom const* rest_element_label(Checker* me, SigParam const* rest, u32 index, unsigned char flags) {
    if (rest->decl) return binding_element_label(me, &rest->decl->initializer.binding, true, index, flags);
    return numbered_name(me, atom_sv(rest->name), "_%u", index);
}

Atom const* ty_rest_element_label(Checker* me, SigParam const* rest, u32 index, unsigned char flags) {
    return rest_element_label(me, rest, index, flags);
}

// tsc prints a rest parameter of tuple type as the parameters it stands for (getExpandedParameters), unless a rest
// element comes before the end. Null when there's nothing to expand
static SigParam const* expanded_params(Checker* me, Signature const* sig, u32* count_out) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return nullptr;
    SigParam const* rest = &sig->params[sig->param_count - 1];
    TupleShape const* shape = ty_tuple_shape(rest->type);
    if (!shape) return nullptr;
    bool empty = !ty_is_tuple(rest->type);
    for (u32 i = 0; i + 1 < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return nullptr;
    }
    TyList elements = empty ? (TyList){0} : ty_reference_args(me, rest->type);
    u32 count = sig->param_count - 1 + shape->arity;
    SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
    memcpy(params, sig->params, sizeof(SigParam) * (sig->param_count - 1));
    for (u32 i = 0; i < shape->arity; i++) {
        Atom const* name = shape->names && shape->names[i] ? shape->names[i] : rest_element_label(me, rest, i, shape->flags[i]);
        // Labels are made unique: `[a: A, a: B]` prints `a: A, a_1: B`
        Atom const* base = name;
        for (u32 counter = 1, j = sig->param_count - 1; j < sig->param_count - 1 + i; j++) {
            if (params[j].name != name) continue;
            name = numbered_name(me, atom_sv(base), "_%u", counter++);
            j = sig->param_count - 2;
        }
        params[sig->param_count - 1 + i] = (SigParam){
            .name = name,
            .type = elements.data[i],
            .optional = (shape->flags[i] & TUPLE_ELEMENT_OPTIONAL) != 0,
            .rest = (shape->flags[i] & TUPLE_ELEMENT_REST) != 0,
        };
    }
    *count_out = count;
    return params;
}

static void display_signature_inner(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new);
static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
    u32 saved = param_names_enter(d);
    display_signature_inner(me, d, sig, separator, with_new);
    param_names_leave(d, saved);
}

// Whether the declared signature at the root of an instantiated one mentions what the instantiations map (other than
// its own type parameters). tsc doesn't instantiate a signature that doesn't (getObjectTypeInstantiation keeps only the
// outer type parameters the declaration may reference): it prints as declared, fresh type parameters and all
static bool mentions_mapped(Checker* me, Signature const* sig, Signature const* root) {
    u32 count = 0;
    for (Signature const* i = sig; i != root; i = i->target) count += i->mapper ? i->mapper->len : 0;
    Ty const** sources = arena_new_n(&my arena, Ty const*, count ? count : 1);
    u32 len = 0;
    for (Signature const* i = sig; i != root; i = i->target) {
        for (u32 j = 0; i->mapper && j < i->mapper->len; j++) {
            Ty const* source = i->mapper->sources[j];
            bool own = false;
            for (u32 k = 0; k < root->type_params.len; k++) own |= root->type_params.data[k] == source;
            if (!own) sources[len++] = source;
        }
    }
    TyList mapped = { .data = sources, .len = len };
    if (!len) return false;
    // (tsc's isTypeParameterPossiblyReferenced is syntactic: a body without a return type annotation may reference anything)
    FunctionImpl const* function = root->function;
    if (function && function->has_body && !function->ret_explicit) return true;
    for (u32 i = 0; i < root->param_count; i++) {
        if (mentions(me, root->params[i].type, mapped)) return true;
    }
    for (u32 i = 0; i < root->type_params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, root->type_params.data[i]);
        if (constraint && mentions(me, constraint, mapped)) return true;
    }
    TyPredicate const* predicate = signature_predicate(me, root);
    if (predicate && predicate->type && mentions(me, predicate->type, mapped)) return true;
    return (root->this_type && mentions(me, root->this_type, mapped)) ||
        mentions(me, signature_return_type(me, root), mapped);
}

// Whether an instantiated signature's mapper maps a type parameter of a merged declaration (an interface declared twice):
// tsc's isTypeParameterPossiblyReferenced says yes for those whatever the node, so every anonymous type of the signature
// is a new instantiation, which no annotation says
static bool maps_merged_type_param(Checker* me, Signature const* sig, Signature const* root) {
    Binder const* binder = program_get_binder(my program);
    for (Signature const* i = sig; i && i != root; i = i->target) {
        for (u32 j = 0; i->mapper && j < i->mapper->len; j++) {
            Ty const* source = i->mapper->sources[j];
            if (source->kind == TY_TYPE_PARAM && !source->param.state->is_this && source->param.symbol &&
                binder_redirect(binder, source->param.symbol)->decl_count > 1) {
                return true;
            }
        }
    }
    return false;
}

static void display_signature_inner(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
    d.widen_literals = false;
    signature_resolve(me, sig);
    if (with_new) {
        // (Only a constructor type is: tsc's signatureToSignatureDeclarationHelper gives a construct signature member,
        // which is what `: ` prints, no modifiers)
        if ((sig->flags & SIGNATURE_ABSTRACT) && strcmp(separator, ": ") != 0) out_lit(d, "abstract ");
        out_lit(d, "new ");
    }
    // tsc reuses the parameter names of any signature with declarations, and the annotations of its declaration where
    // they still say what is printed: an instantiated signature (members of `I<number>`) reuses what the mapper left
    // alone and doesn't mention its own type parameters (fresh ones in tsc). Inside a reused annotation everything is
    // reused but what has no annotation (`(x) => void` prints its `any` synthetically)
    Signature const* root = sig;
    bool this_only = true; // Instantiated only with what `this` is (every method of a class): no fresh type parameters
    bool class_default = false; // (A class's default constructor returns the class, whatever its base signature returns)
    for (; root->target; root = root->target) {
        class_default |= root->class_instance != nullptr;
        for (u32 i = 0; root->mapper && i < root->mapper->len; i++) {
            Ty const* source = root->mapper->sources[i];
            this_only &= source->kind == TY_TYPE_PARAM && source->param.state->is_this;
        }
    }
    signature_resolve(me, root);
    bool sibling_mapped = d.sibling_mentions_mapped;
    d.sibling_mentions_mapped = false;
    // (Unless its own type parameters were instantiated away: a call's candidate with what was inferred)
    bool own_instantiated = root->type_params.len && !sig->type_params.len;
    // (A merged declaration's type parameter may be referenced anywhere: tsc instantiates the whole member)
    bool merged_mapped = root != sig && maps_merged_type_param(me, sig, root);
    if (root != sig && !this_only && !class_default && !own_instantiated && !sibling_mapped && !merged_mapped &&
        !mentions_mapped(me, sig, root)
    ) {
        sig = root;
    }
    bool instantiated = root != sig;
    bool fresh_anonymous = instantiated && merged_mapped;
    // (Added to an enclosing signature's: a function type in a parameter of `then<U>` of `P<number>` doesn't reuse a
    // `U` it mentions either)
    // (Instantiated for `this` only, it's the same: tsc instantiates a member that mentions `this` (a class or interface
    // reference's members are instantiated with `this` as the reference), with fresh type parameters)
    if (instantiated && root->type_params.len && (!this_only || sibling_mapped || mentions_mapped(me, sig, root))) {
        if (d.fresh_params.len) {
            Ty const** both = arena_new_n(&my arena, Ty const*, d.fresh_params.len + root->type_params.len);
            memcpy(both, d.fresh_params.data, sizeof(Ty const*) * d.fresh_params.len);
            memcpy(both + d.fresh_params.len, root->type_params.data, sizeof(Ty const*) * root->type_params.len);
            d.fresh_params = (TyList){ .data = both, .len = d.fresh_params.len + root->type_params.len };
        } else {
            d.fresh_params = root->type_params;
        }
    }
    // (A class's constructor has its parameters reused, but it returns the class)
    FunctionImpl const* function = root->function && root->class_instance && !root->function->is_constructor ? nullptr : root->function;
    // (A JSDoc `@this` makes a parameter of its own, which isn't reused)
    bool jsdoc_this = function && vec_function_arg_size(&function->args) && function->args.data[0].jsdoc_this;
    bool reusable = (d.underline || d.reuse_nodes) && function;
    bool syntactic = function && d.syntactic_function == function; // (Its parameter declarations are reused whole)
    d.syntactic_function = nullptr;
    display_type_params(me, d, sig->type_params, reusable, syntactic);
    out_lit(d, "(");
    u32 param_count = sig->param_count;
    SigParam const* params = expanded_params(me, sig, &param_count);
    if (!params) params = sig->params;
    TyDisplayParams printed = { .params = params, .count = param_count, .function = function, .outer = d.printed_params };
    if (d.underline || d.reuse_nodes) d.printed_params = &printed;
    if (sig->this_type) {
        // (A `this` from a contextual signature whose own is a JSDoc `@this` has no declaration: tsc's
        // getSignatureFromDeclaration makes the symbol, assignContextualParameterTypes copies it)
        bool contextual_jsdoc_this = false;
        if (function && reusable && !jsdoc_this && !(vec_function_arg_size(&function->args) &&
            function->args.data[0].initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(function->args.data[0].initializer.binding.content_span, "this"))
        ) {
            Signature const* contextual = ty_function_assigned_contextual_signature(me, function);
            while (contextual && !contextual->function && (contextual->target || contextual->declared_by)) contextual = contextual->target ? contextual->target : contextual->declared_by;
            FunctionImpl const* context_function = contextual ? contextual->function : nullptr;
            contextual_jsdoc_this = context_function && vec_function_arg_size(&context_function->args) &&
                context_function->args.data[0].jsdoc_this;
        }
        out(reused_if(d, d.reused || (function && !jsdoc_this && !contextual_jsdoc_this)), c_sv("this"));
        out_lit(d, ": ");
        FunctionArg const* this_decl = function && vec_function_arg_size(&function->args) ? vec_function_arg_at(&function->args, 0) : nullptr;
        Binding const* binding = this_decl ? &this_decl->initializer.binding : nullptr;
        if (binding && !(binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this"))) binding = nullptr;
        bool reused = reusable && !jsdoc_this && binding && binding->has_explicit_type &&
            !mentions(me, sig->this_type, d.fresh_params) && (!instantiated || sig->this_type == root->this_type);
        // A function expression's `this` from its contextual signature is that signature's parameter (tsc's
        // assignContextualParameterTypes: createSymbolWithType(context.thisParameter)), whose annotation is reused
        Signature const* contextual = function && !binding && reusable ? ty_function_assigned_contextual_signature(me, function) : nullptr;
        while (contextual && !contextual->function && (contextual->target || contextual->declared_by)) contextual = contextual->target ? contextual->target : contextual->declared_by;
        FunctionImpl const* context_function = contextual ? contextual->function : nullptr;
        FunctionArg const* context_this = context_function && vec_function_arg_size(&context_function->args) ?
            vec_function_arg_at(&context_function->args, 0) : nullptr;
        if (context_this && !context_this->jsdoc_this && context_this->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(context_this->initializer.binding.content_span, "this") &&
            context_this->initializer.binding.has_explicit_type && contextual->this_type == sig->this_type
        ) {
            binding = &context_this->initializer.binding;
            reused = true;
        }
        display_annotated(me, reused_if(d, false), reused ? &binding->explicit_type : nullptr, sig->this_type, false);
        if (param_count) out_lit(d, ", ");
    }
    for (u32 i = 0; i < param_count; i++) {
        SigParam const* param = &params[i];
        if (i) out_lit(d, ", ");
        if (param->rest) out(reused_if(d, d.reused || (syntactic && param->decl)), c_sv("..."));
        Binding const* name_binding = param->decl ? &param->decl->initializer.binding : nullptr;
        if (name_binding && (name_binding->tag == BINDING_ARRAY || name_binding->tag == BINDING_OBJECT)) {
            cstr pattern = cstr_init();
            append_binding_pattern(&pattern, name_binding);
            out(reused_if(d, true), cstr_sv(&pattern));
            cstr_drop(&pattern);
        } else {
            bool written = name_binding && span_is_written(name_binding->content_span);
            out(reused_if(d, (d.reused && (!name_binding || written)) || (param->decl && written)), atom_sv(param->name));
        }
        if (param->optional) out_lit(d, "?");
        // (A function expression's parameter typed by a JSDoc `@type` signature: tsc types it contextually)
        bool contextual_jsdoc = function && function->is_expression && param->decl && param->decl->jsdoc_span;
        out(reused_if(d, d.reused || (syntactic && param->decl && !contextual_jsdoc)), c_sv(": "));
        Binding const* binding = param->decl ? &param->decl->initializer.binding : nullptr;
        bool reused = reusable && binding && binding->has_explicit_type && !contextual_jsdoc &&
            !mentions(me, param->type, d.fresh_params) && (!instantiated || param->type == root->params[i].type) &&
            !(fresh_anonymous && param->type->kind == TY_ANONYMOUS) &&
            // (A `@param {T} [x]` tag of a JSDoc signature (`@overload`, `@callback`) declares x without a `?` that
            // would say the undefined its type has: tsc doesn't reuse `T` there)
            !(function && function->jsdoc_callback && param->decl->explicit_optional && !binding->explicit_type.jsdoc_optional &&
                compiler_options()->strict_null_checks);
        TyDisplay param_d = reused_if(d, false);
        // A parameter whose initializer is a function expression gets the function's type syntactically (tsc's
        // typeFromParameter -> typeFromExpression), unless something may type it contextually
        Expr const* initializer = param->decl ? initialized_binding_value(&param->decl->initializer) : nullptr;
        if (reusable && binding && !binding->has_explicit_type && binding->tag == BINDING_IDENTIFIER && initializer &&
            initializer->tag == EXPR_FUNCTION && !initializer->function->syntactically_contextual &&
            ty_without_undefined(me, param->type) == ty_expr_type(me, initializer)
        ) {
            param_d.syntactic_function = initializer->function;
        }
        // An optional parameter's type has undefined under strictNullChecks (tsc's getOptionalType in
        // serializeTypeForDeclaration), which a reused annotation doesn't say
        Ty const* shown = param->type;
        // (Not a binding pattern's without annotation or initializer: that type is the pattern's as it is, tsc's
        // getTypeForVariableLikeDeclaration returns getTypeFromBindingPattern without addOptionality)
        bool bare_pattern = binding && binding->tag != BINDING_IDENTIFIER && !binding->has_explicit_type &&
            initializer && initializer->tag == EXPR_IMPLICIT_ANY;
        if (param->optional && (!param->rest || (binding && binding->optional_parameter)) && param->decl &&
            (param->decl->explicit_optional || param->decl->jsdoc_optional_initialized) &&
            compiler_options()->strict_null_checks && ty_without_undefined(me, shown) == shown && !bare_pattern
        ) {
            shown = ty_union(me, (Ty const*[]){ shown, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
        }
        if (reused && reuses_nodes(param_d) && !param->optional &&
            annotation_plus_undefined(me, &binding->explicit_type, shown)
        ) {
            // The annotation with undefined added (a parameter with an initializer before a required one): tsc reuses
            // the annotation in a new union with `undefined` (serializeTypeForDeclaration's addUndefined)
            display_node_plus_undefined(me, param_d, &binding->explicit_type);
        } else {
            display_annotated(me, param_d, reused ? &binding->explicit_type : nullptr, shown, param->optional);
        }
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TyPredicate const* predicate = signature_predicate(me, sig);
    Ty const* ret = signature_return_type(me, sig);
    TypeNode const* ret_annotation = !reusable || root->class_instance ? nullptr : function->ret_explicit ? &function->ret :
        function_impl_rare(function)->jsdoc_tag_ret ? function_impl_rare(function)->jsdoc_tag_ret : single_return_assertion(me, function, signature_return_type(me, root));
    bool reused = ret_annotation &&
        !mentions(me, ret, d.fresh_params) && (!instantiated || ret == signature_return_type(me, root)) &&
        !(fresh_anonymous && ret->kind == TY_ANONYMOUS);
    if (predicate) {
        // In an instantiated signature, a parameter it names isn't the one of the instantiated signature (tsc resolves
        // it to the declaration's own), so the whole predicate is printed
        bool names_param = function && function->predicate && !function->predicate->is_this;
        TyDisplay predicate_d = reused_if(d, reusable && function->predicate && !(instantiated && !this_only && names_param));
        if (predicate->asserts) out_lit(predicate_d, "asserts ");
        out(predicate_d, atom_sv(predicate->param_name));
        if (predicate->type) {
            out_lit(predicate_d, " is ");
            // Reused like any annotation, but whatever type it has (tsc compares the return type, boolean): in an
            // instantiated signature, what the instantiation maps prints what it is mapped to
            if (predicate_d.reused && function->predicate->has_type && reuses_nodes(d)) {
                predicate_d.mapped_by = instantiated && !this_only ? sig : nullptr;
                display_node(me, predicate_d, &function->predicate->type);
            } else {
                ty_display(me, reused_if(predicate_d, false), predicate->type);
            }
        }
        return;
    }
    TyDisplay ret_d = reused_if(d, false);
    if (reusable && !ret_annotation && !function->ret_explicit && !root->class_instance) {
        ret_d.syntactic_function = single_return_function(me, function, ret);
        Ty const* keyword = single_return_keyword(me, function, ret);
        if (keyword) ret = keyword;
    }
    // tsc's serializeReturnTypeForSignature reuses `return x as T` whether or not there is an enclosing declaration:
    // a message's type printed from nowhere (a method's) reuses such a T when it names nothing
    if (!reusable && !d.underline && function && !function->ret_explicit && !root->class_instance && !instantiated &&
        !function_impl_rare(function)->jsdoc_tag_ret
    ) {
        TypeNode const* node = single_return_assertion(me, function, ret);
        if (node && type_node_names_nothing(node, 0) && annotation_is(me, node, ret, false)) {
            ret_d.reuse_nodes = true;
            display_node(me, ret_d, node);
            return;
        }
    }
    display_annotated(me, ret_d, reused ? ret_annotation : nullptr, ret, false);
}

// The pattern ends with a comma before its `]` or `}` (tsc's NodeArray.hasTrailingComma, which the printer keeps)
static bool pattern_has_trailing_comma(Binding const* binding) {
    csview text = binding->content_span.sv;
    isize_t i = text.size - 1; // On the closing bracket
    while (i-- > 0) {
        char c = text.buf[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        return c == ',';
    }
    return false;
}

// A destructured parameter's name the way tsc prints it in a signature: the pattern without its defaults
static void append_binding_pattern(cstr* out, Binding const* binding) {
    if (binding->rest) cstr_append_sv(out, c_sv("..."));
    if (binding->tag == BINDING_ARRAY) {
        cstr_append_sv(out, c_sv("["));
        for (isize_t i = 0; i < binding->array.size; i++) {
            if (i) cstr_append_sv(out, c_sv(", "));
            append_binding_pattern(out, &binding->array.data[i]);
        }
        if (binding->array.size && pattern_has_trailing_comma(binding)) cstr_append_sv(out, c_sv(","));
        cstr_append_sv(out, c_sv("]"));
    } else if (binding->tag == BINDING_OBJECT) {
        if (!binding->object.size) {
            cstr_append_sv(out, c_sv("{}"));
            return;
        }
        cstr_append_sv(out, c_sv("{ "));
        for (isize_t i = 0; i < binding->object.size; i++) {
            ObjectBindingItem const* item = &binding->object.data[i];
            if (i) cstr_append_sv(out, c_sv(", "));
            bool shorthand = item->target.rest || (item->target.tag == BINDING_IDENTIFIER &&
                item->target.content_span.sv.buf == item->source_span.sv.buf);
            if (!shorthand) {
                cstr_append_sv(out, item->source_span.sv);
                cstr_append_sv(out, c_sv(": "));
            }
            append_binding_pattern(out, &item->target);
        }
        if (pattern_has_trailing_comma(binding)) cstr_append_sv(out, c_sv(","));
        cstr_append_sv(out, c_sv(" }"));
    } else {
        cstr_append_sv(out, binding->content_span.sv);
    }
}

static MappedInfo const* mapped_root(MappedInfo const* mapped);
static void display_mapped_body(Checker* me, TyDisplay d, MappedInfo const* mapped, Ty const* wrapper, bool keyof_wrapper);

// How an instantiated mapped type's root was instantiated to make it (tsc's type.mapper)
static TyMapper const* mapped_mapper_from_root(Checker* me, MappedInfo const* mapped) {
    if (!mapped->template_from) return nullptr;
    TyMapper const* inner = mapped_mapper_from_root(me, mapped->template_from);
    return inner ? ty_compose_mappers(me, inner, mapped->template_mapper) : mapped->template_mapper;
}

static void display_mapped(Checker* me, TyDisplay d, MappedInfo const* mapped) {
    // With GenerateNamesForShadowedTypeParams (the types baseline), tsc's createMappedTypeNodeFromType keeps what makes
    // the mapped type homomorphic or modifier preserving when inlining wouldn't: a homomorphic `[K in keyof T]` whose T
    // is no longer a type parameter is written over a new T_1 inferred from it (`X extends infer T_1 ? { [K in keyof
    // T_1]: ... } : never`), a `[P in K]` with `K extends keyof T` whose K is no longer a type parameter over a new T_1
    // inferred from it and constrained to `keyof T`
    Ty const* wrapper = nullptr;
    bool homomorphic_lost = false;
    if (d.param_names && mapped->modifiers_type && mapped->modifiers_type != ty_intrinsic(me, TY_UNKNOWN_)) {
        MappedInfo const* root = mapped_root(mapped);
        if (mapped->keyof_constraint) {
            homomorphic_lost = root != mapped && root->modifiers_type && root->modifiers_type->kind == TY_TYPE_PARAM &&
                mapped->modifiers_type->kind != TY_TYPE_PARAM;
        }
        bool preserving = !mapped->keyof_constraint && !(mapped->constraint->kind == TY_TYPE_PARAM &&
            ty_param_constraint(me, mapped->constraint) && ty_param_constraint(me, mapped->constraint)->kind == TY_KEYOF);
        if (homomorphic_lost || preserving) {
            wrapper = ty_synthetic_type_param(me, c_sv("T"));
            type_param_name(me, d, wrapper);
        }
    }
    TyDisplay fresh = reused_if(d, false);
    if (wrapper) {
        // (Written after the mapped type, but named before it)
        cstr inner = cstr_null;
        cstr inner_underline = cstr_null;
        TyDisplay inner_d = d;
        inner_d.target = &inner;
        inner_d.underline = d.underline ? &inner_underline : nullptr;
        display_mapped_body(me, inner_d, mapped, wrapper, homomorphic_lost);
        if (homomorphic_lost) ty_display(me, d, mapped->modifiers_type);
        else ty_display(me, d, mapped->constraint);
        out(fresh, c_sv(" extends infer "));
        out_type_param_name(me, fresh, wrapper, false);
        if (homomorphic_lost) {
            MappedInfo const* root = mapped_root(mapped);
            Ty const* constraint = ty_param_constraint(me, root->modifiers_type);
            if (constraint) constraint = ty_instantiate(me, constraint, mapped_mapper_from_root(me, mapped));
            if (constraint && constraint != ty_intrinsic(me, TY_UNKNOWN_)) {
                out(fresh, c_sv(" extends "));
                ty_display(me, d, constraint);
            }
        } else {
            out(fresh, c_sv(" extends keyof "));
            ty_display(me, d, mapped->modifiers_type);
        }
        out(fresh, c_sv(" ? "));
        cstr_append_sv(d.target, cstr_sv(&inner));
        if (d.underline) cstr_append_sv(d.underline, cstr_sv(&inner_underline));
        out(fresh, c_sv(" : never"));
        c_drop(cstr, &inner, &inner_underline);
        return;
    }
    display_mapped_body(me, d, mapped, nullptr, false);
}

static void display_mapped_body(Checker* me, TyDisplay d, MappedInfo const* mapped, Ty const* wrapper, bool keyof_wrapper) {
    out_lit(d, "{ ");
    if (mapped->readonly) out(d, mapped->readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
    out_lit(d, "[");
    ty_display(me, reused_if(d, true), mapped->type_param); // (tsc reuses the name of the declared type parameter)
    out_lit(d, " in ");
    if (wrapper) {
        TyDisplay fresh = reused_if(d, false);
        if (keyof_wrapper) out(fresh, c_sv("keyof "));
        out_type_param_name(me, fresh, wrapper, false);
    } else if (mapped->keyof_constraint && mapped->modifiers_type) {
        // (`keyof` and the modifiers type: the constraint itself may have distributed `keyof` over an intersection)
        out_lit(d, "keyof ");
        display_parenthesized_if(me, d, mapped->modifiers_type, needs_parens_in_array(me, d, mapped->modifiers_type));
    } else {
        ty_display(me, d, mapped->constraint);
    }
    if (mapped->name_type) {
        out_lit(d, " as ");
        ty_display(me, d, mapped->name_type);
    }
    out_lit(d, "]");
    if (mapped->optional) out(d, mapped->optional > 0 ? c_sv("?") : c_sv("-?"));
    out_lit(d, ": ");
    // `?` adds undefined to the template (tsc's getTemplateTypeFromMappedType; the missing type under
    // exactOptionalPropertyTypes isn't printed)
    Ty const* template = ty_mapped_template(me, mapped);
    if (keyof_wrapper) {
        // tsc 6 writes the root's template over the new T_1 too (`{ [K in keyof T_1]: T_1[K]; }`, not the instantiated
        // modifiers type indexed): its type parameter is this one's, its modifiers type the wrapper
        MappedInfo const* root = mapped_root(mapped);
        Ty const* sources[2] = { root->type_param, root->modifiers_type };
        Ty const* targets[2] = { mapped->type_param, wrapper };
        template = ty_instantiate(me, ty_mapped_template(me, root),
            ty_mapper_new(me, (TyList){ .data = sources, .len = 2 }, (TyList){ .data = targets, .len = 2 }));
    }
    if (mapped->optional > 0 && compiler_options()->strict_null_checks && !compiler_options()->exact_optional_property_types) {
        Ty const* parts[2] = { template, ty_intrinsic(me, TY_UNDEFINED) };
        template = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    ty_display(me, d, template);
    out_lit(d, "; }");
}

// An index signature an object literal's computed names made prints as those members when each is an entity name
// accessible from where it's printed (tsc's indexInfoToObjectComputedNamesOrSignatureDeclaration)
static bool index_components_printed(Checker* me, TyDisplay d, IndexInfo const* info) {
    if (!info->component_count || !d.enclosing) return false;
    for (u32 i = 0; i < info->component_count; i++) {
        Expr const* name = info->components[i].name;
        if (!expr_is_entity_name(name)) return false;
        Expr const* leftmost = entity_leftmost(name);
        if (leftmost->tag != EXPR_IDENTIFIER || leftmost->identifier.keyword || !leftmost->identifier.symbol) return false;
        // (A visible declaration, tsc's isEntityNameVisible: not a local of a function body, not a parameter of a
        // function expression or arrow. HACK: a parameter of a function declaration nested in another function
        // counts as visible, and TyDisplay.enclosing, the scope of the statement, doesn't see the parameters of the
        // function around it: those aren't checked for shadowing, only that the type is printed inside the function)
        Symbol const* symbol = leftmost->identifier.symbol;
        if (symbol->is_local && symbol->function && (symbol->flags & SYMBOL_VARIABLE) &&
            (!symbol->is_parameter || symbol->function->is_expression)
        ) {
            return false;
        }
        if (symbol->is_parameter && symbol->function) {
            if (span_valid(d.at) && !span_contains_span(symbol->function->full_span, d.at)) return false;
            continue;
        }
        Symbol const* found = scope_resolve(d.enclosing, leftmost->inner_span.sv, SYMBOL_VALUE);
        if (!found || !same_symbol(me, found, leftmost->identifier.symbol)) return false;
    }
    return true;
}

// Declaration emit's checks of a property it writes (createTypeNodesFromResolvedType, addPropertyToElementList).
// Whether it is skipped
static bool track_property(Checker* me, TyDisplay d, Prop const* prop) {
    // A class's `prototype` isn't written; its private and protected members can't be in a type literal
    if (prop->flags & PROP_PROTOTYPE) return true;
    if (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) {
        d.tracker->report(d.tracker, DECL_REPORT_PRIVATE_IN_CLASS_LITERAL, atom_sv(prop->name));
    }
    if (!ty_unique_symbol_of_name(me, prop->name)) return false;
    // A late bound name: the declaration's computed name, else it can't be written
    // (A mapped type's property is declared where the property it comes from is, see mapped_members())
    Prop const* declared = prop;
    while (declared->target && !span_valid(declared->decl->name_span) && !declared->decl->decl_span_count) {
        declared = declared->target;
    }
    Span name = declared->decl->name_span;
    if (span_valid(name) && name.sv.size && name.sv.buf[0] == '[') {
        d.tracker->track_computed_name(d.tracker, name.sv, name.source);
    } else if (!span_valid(name) && !declared->decl->decl_span_count) {
        cstr text = ty_prop_name_cstr(me, prop->name);
        d.tracker->report(d.tracker, DECL_REPORT_NON_SERIALIZABLE, cstr_sv(&text));
        cstr_drop(&text);
    }
    return false;
}

static MappedInfo const* mapped_root(MappedInfo const* mapped) {
    while (mapped->template_from) mapped = mapped->template_from;
    return mapped;
}

// tsc's shouldUsePlaceholderForProperty: a reverse mapped property prints as `any` (the elided information placeholder)
// inside itself, inside one whose source property's type isn't an anonymous object type, and three deep in the same
// mapped type
static bool reverse_placeholder(TyDisplay d, Prop const* prop) {
    if (!d.reverse_stack) return false;
    for (TyDisplayReverse const* i = d.reverse_stack; i; i = i->outer) {
        if (i->prop == prop) return true;
    }
    if (prop_decl_rare(d.reverse_stack->prop->decl)->reverse_source->kind != TY_ANONYMOUS) return true;
    MappedInfo const* root = mapped_root(prop_decl_rare(prop->decl)->reverse_of->mapped);
    u32 depth = 0;
    for (TyDisplayReverse const* i = d.reverse_stack; i && depth < 3; i = i->outer, depth++) {
        if (mapped_root(prop_decl_rare(i->prop->decl)->reverse_of->mapped) != root) return false;
    }
    return depth == 3;
}

// Whether a type literal is the annotation of the declaration whose name is at `at`: only `?` and `:` between them
static bool annotates_name_at(Span at, Span literal) {
    if (!span_valid(at) || !span_valid(literal) || at.source != literal.source) return false;
    char const* i = span_end_ptr(at);
    char const* end = span_start_ptr(literal);
    if (i > end) return false;
    bool colon = false;
    for (; i < end; i++) {
        if (*i == ':' && !colon) colon = true;
        else if (*i != ' ' && *i != '\t' && *i != '\r' && *i != '\n' && *i != '?') return false;
    }
    return colon;
}


// tsc's getSetAccessorValueParameter: the parameter after a `this` one
static Binding const* setter_value_parameter(FunctionImpl const* setter) {
    isize count = vec_function_arg_size(&setter->args);
    if (!count) return nullptr;
    Binding const* first = &vec_function_arg_at(&setter->args, 0)->initializer.binding;
    bool first_this = first->tag == BINDING_IDENTIFIER && span_equals(first->content_span, "this");
    return count == 2 && first_this ? &vec_function_arg_at(&setter->args, 1)->initializer.binding : first;
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty) {
    if (reuses_nodes(d) && !span_valid(d.decl_literal) && ty->kind == TY_ANONYMOUS && ty->anonymous.node &&
        ty->anonymous.node->tag == TYPE_INTERFACE && annotates_name_at(d.at, ty->anonymous.node->interface.span)
    ) {
        d.decl_literal = ty->anonymous.node->interface.span;
    }
    Members const* members = ty_members(me, ty);
    if (members->mapped) {
        display_mapped(me, d, members->mapped);
        return;
    }
    bool only_signatures = !members->prop_count && !members->index_count;
    if (only_signatures && members->call_count == 1 && !members->construct_count) {
        display_signature(me, d, members->call[0], " => ", false);
        return;
    }
    if (only_signatures && members->construct_count == 1 && !members->call_count) {
        display_signature(me, d, members->construct[0], " => ", true);
        return;
    }
    if (only_signatures && !members->call_count && !members->construct_count) {
        out_lit(d, "{}");
        return;
    }
    if (d.depth > MAX_DISPLAY_DEPTH) {
        out_lit(d, "...");
        return;
    }
    // Abstract construct signatures can't be members of a type literal: `(abstract new () => C) & { rest }` (tsc's
    // createTypeNodeFromObjectType)
    u32 abstract_count = 0;
    for (u32 i = 0; i < members->construct_count; i++) abstract_count += (members->construct[i]->flags & SIGNATURE_ABSTRACT) != 0;
    if (abstract_count) {
        for (u32 i = 0, printed = 0; i < members->construct_count; i++) {
            if (!(members->construct[i]->flags & SIGNATURE_ABSTRACT)) continue;
            if (printed++) out_lit(d, " & ");
            out_lit(d, "(");
            display_signature(me, d, members->construct[i], " => ", true);
            out_lit(d, ")");
        }
        if (members->call_count + members->construct_count - abstract_count + members->index_count + members->prop_count == 0) {
            return;
        }
        out_lit(d, " & ");
    }
    out_lit(d, "{ ");
    d.in_object_literal = true;
    TyDisplay sig_d = d;
    for (u32 i = 0; i < members->call_count && members->call_count > 1 && (d.underline || d.reuse_nodes) && !sig_d.sibling_mentions_mapped; i++) {
        Signature const* root = members->call[i];
        while (root->target) root = root->target;
        sig_d.sibling_mentions_mapped = root != members->call[i] && mentions_mapped(me, members->call[i], root);
    }
    for (u32 i = 0; i < members->call_count; i++) {
        display_signature(me, sig_d, members->call[i], ": ", false);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->construct_count; i++) {
        if (members->construct[i]->flags & SIGNATURE_ABSTRACT) continue;
        display_signature(me, d, members->construct[i], ": ", true);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
        if (index_components_printed(me, d, info)) {
            for (u32 j = 0; j < info->component_count; j++) {
                IndexComponent const* component = &info->components[j];
                // (Those with a literal or unique symbol name are properties too)
                if (ty_expr_type(me, component->name)->kind == TY_LITERAL) continue;
                // (The member is its declaration reused, the type is made: the baseline's `^` line)
                TyDisplay reused = reused_if(d, true);
                if (d.tracker) {
                    Span name = expr_outer_span(component->name);
                    d.tracker->track_computed_name(d.tracker, name.sv, name.source);
                }
                if (info->readonly) out_lit(reused, "readonly ");
                out_lit(reused, "[");
                out(reused, expr_outer_span(component->name).sv);
                out_lit(reused, "]: ");
                ty_display(me, d, component->type);
                out_lit(reused, ";");
                out_lit(d, " ");
            }
            continue;
        }
        if (info->readonly) out_lit(d, "readonly ");
        out_lit(d, "[");
        out(d, info->param_name ? atom_sv(info->param_name) : c_sv("x"));
        out_lit(d, ": ");
        ty_display(me, d, info->key);
        out_lit(d, "]: ");
        // (A reverse mapped type's is the elided information placeholder: tsc's createTypeNodeFromObjectType)
        if (ty_reverse_of(ty)) out_lit(d, "any");
        else ty_display(me, d, info->value);
        out_lit(d, "; ");
    }
    TyDisplay outer_d = d;
    TyDisplayReverse reverse_frame;
    for (u32 i = 0; i < members->prop_count; i++) {
        d = outer_d;
        Prop const* prop = members->props[i];
        if (d.tracker) {
            if (d.tracker->truncated) break;
            if (track_property(me, d, prop)) continue;
        }
        bool reverse_prop = prop_decl_rare(prop->decl)->reverse_of != nullptr;
        bool placeholder = reverse_prop && reverse_placeholder(d, prop);
        Ty const* type = placeholder ? ty_intrinsic(me, TY_ANY) : prop_type(me, prop);
        if (reverse_prop && !placeholder) {
            reverse_frame = (TyDisplayReverse){ .prop = prop, .outer = d.reverse_stack };
            d.reverse_stack = &reverse_frame;
        }
        // (A property that is a function of a namespace, as a spread copies it: tsc prints a property whose symbol is a
        // Function or Method as methods)
        Prop const* declared = prop;
        while (declared->target) declared = declared->target;
        Symbol const* member_symbol = declared->decl->symbol ? declared->decl->symbol : prop_decl_rare(declared->decl)->spread_variable;
        bool function_symbol = member_symbol && (member_symbol->flags & SYMBOL_FUNCTION) && member_symbol->parent &&
            !(prop->flags & PROP_READONLY) && (type->flags & TYF_OBJECT);
        // (Not a readonly one, `as const`: tsc's addPropertyToElementList prints it as a property)
        Members const* type_members = ((prop->flags & PROP_METHOD) && !(prop->flags & PROP_READONLY)) || function_symbol ?
            ty_members(me, type) : nullptr;
        if (type_members && type_members->call_count && !type_members->prop_count) {
            // Methods print once per signature
            for (u32 j = 0; j < type_members->call_count; j++) {
                // (A method named `new` is quoted: it would read as a construct signature)
                if (csview_equals(atom_sv(prop->name), "new")) out_lit(d, "\"new\"");
                else display_member_name(me, d, prop);
                if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
                display_signature(me, d, type_members->call[j], ": ", false);
                out_lit(d, "; ");
            }
            continue;
        }
        // An accessor pair whose setter takes another type than the getter gives prints as the pair (tsc's
        // addPropertyToElementList)
        if (((prop_decl_rare(declared->decl)->setter && vec_function_arg_size(&prop_decl_rare(declared->decl)->setter->args)) || prop_decl_rare(declared->decl)->write_node) &&
            !(declared->flags & PROP_OPTIONAL)
        ) {
            Ty const* write = prop_write_type(me, prop);
            if (write && write != type && type != ty_intrinsic(me, TY_ERROR) && write != ty_intrinsic(me, TY_ERROR)) {
                out_lit(d, "get ");
                display_member_name(me, d, prop);
                out_lit(d, "(): ");
                // (The annotations as written where they still say it: tsc's signatureToSignatureDeclarationHelper)
                FunctionImpl const* getter = prop_decl_rare(declared->decl)->setter ? declared->decl->function : nullptr;
                TypeNode const* read_node = getter ? (getter->ret_explicit ? &getter->ret : nullptr) : declared->decl->node;
                display_annotated(me, d, prop == declared ? read_node : nullptr, type, false);
                out_lit(d, "; set ");
                display_member_name(me, d, prop);
                out_lit(d, "(");
                // (The parameter's name is its declaration's, reused)
                if (prop_decl_rare(declared->decl)->setter) {
                    cstr param = cstr_null;
                    append_binding_pattern(&param, &vec_function_arg_at(&prop_decl_rare(declared->decl)->setter->args, 0)->initializer.binding);
                    out(reused_if(d, true), cstr_sv(&param));
                    cstr_drop(&param);
                } else {
                    out(reused_if(d, true), prop_decl_rare(declared->decl)->setter_param.sv);
                }
                out_lit(d, ": ");
                TypeNode const* write_node = prop_decl_rare(declared->decl)->setter ?
                    &vec_function_arg_at(&prop_decl_rare(declared->decl)->setter->args, 0)->initializer.binding.explicit_type : prop_decl_rare(declared->decl)->write_node;
                display_annotated(me, d, prop == declared ? write_node : nullptr, write, false);
                out_lit(d, "); ");
                continue;
            }
        }
        if (prop->flags & PROP_READONLY) out_lit(d, "readonly ");
        TyDisplay name_d = d;
        name_d.tracker = nullptr; // (named from its own declaration, see track_property())
        display_member_name(me, name_d, prop);
        if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
        out_lit(d, ": ");
        // The annotation of a property signature unless instantiated, or the type assertion of an object literal's
        // The annotation of the property it was declared as (an instantiation too: display_annotated() checks that it
        // still says the same), or the type assertion of an object literal's
        Prop const* origin = prop;
        while (origin->target) origin = origin->target;
        bool mapped_from = prop_decl_rare(origin->decl)->template_of && prop_decl_rare(prop->decl)->mapped_from;
        if (mapped_from) {
            origin = prop_decl_rare(prop->decl)->mapped_from;
            // (Through a modifiers type that is mapped itself, `keyof (Partial<A> & B)`: the declarations tsc's mapped
            // property copies are its modifiers property's, all the way down)
            for (int depth = 0; depth < 16 && origin->mapped && prop_decl_rare(origin->decl)->mapped_from; depth++) {
                origin = prop_decl_rare(origin->decl)->mapped_from;
            }
            while (origin->target) origin = origin->target;
        }
        TypeNode const* annotation = placeholder ? nullptr : prop->decl->asserted;
        if (origin->decl->node && origin->decl->node->parsed && !origin->decl->lazy && !origin->decl->function && !origin->decl->overload_count &&
            !origin->decl->symbol
        ) {
            annotation = origin->decl->node;
        }
        // A module's variable (a namespace's member, spread or not): its declaration's annotation, or a `const`'s
        // literal initializer (tsc's serializeTypeOfDeclaration through the property's declarations)
        Expr const* literal_initializer = nullptr;
        FunctionImpl const* initializer_function = nullptr;
        Symbol const* variable = origin->decl->symbol ? origin->decl->symbol : prop_decl_rare(origin->decl)->spread_variable;
        if (!annotation && !placeholder && variable && reuses_nodes(d)) {
            Binding const* binding = variable->decl_count == 1 && variable->decls[0].kind == SYMBOL_DECL_VARIABLE ?
                variable->decls[0].node : nullptr;
            if (binding && !binding->parent && binding->tag == BINDING_IDENTIFIER && binding->symbol == variable) {
                if (binding->has_explicit_type) {
                    annotation = &binding->explicit_type;
                } else {
                    Expr const* value = initialized_binding_value((InitializedBinding const*)binding);
                    // (A function expression: its parameter declarations reused, tsc's typeFromExpression)
                    if (value && value->tag == EXPR_FUNCTION && !value->function->syntactically_contextual &&
                        ty_expr_type(me, value) == type
                    ) {
                        initializer_function = value->function;
                    }
                    if (!variable->is_const) value = nullptr;
                    bool literal = value && value->tag == EXPR_LITERAL && (value->literal.tag == VALUE_BOOLEAN ||
                        value->literal.tag == VALUE_NUMBER || value->literal.tag == VALUE_BIGINT);
                    if (literal && (type->flags & TYF_LITERAL)) literal_initializer = value;
                }
            }
        }
        // An object literal's accessor: the getter's return annotation, else the setter's parameter's (tsc's
        // typeFromAccessor)
        FunctionImpl const* syntactic_getter = nullptr;
        if ((origin->flags & (PROP_GETTER | PROP_SETTER)) && origin->decl->function) {
            FunctionImpl const* accessor = origin->decl->function;
            Binding const* param = setter_value_parameter(accessor);
            Binding const* setter_param = prop_decl_rare(origin->decl)->setter ? setter_value_parameter(prop_decl_rare(origin->decl)->setter) : nullptr;
            if (setter_param && !accessor->ret_explicit) {
                param = setter_param;
                if (param->has_explicit_type) annotation = &param->explicit_type;
                else if (origin->flags & PROP_GETTER) syntactic_getter = accessor;
            } else if ((origin->flags & PROP_GETTER) && accessor->ret_explicit) {
                annotation = &accessor->ret;
            } else if (origin->flags & PROP_GETTER) {
                syntactic_getter = accessor;
            } else if (param && param->has_explicit_type) {
                annotation = &param->explicit_type;
            }
        }
        // A getter returning a function expression has its type syntactically, parameter declarations reused (tsc's
        // typeFromAccessor -> typeFromSingleReturnExpression)
        TyDisplay type_d = d;
        if (syntactic_getter && reuses_nodes(d)) type_d.syntactic_function = single_return_function(me, syntactic_getter, type);
        if (initializer_function) type_d.syntactic_function = initializer_function;
        // (Instantiated in a signature that has fresh type parameters in tsc, not tttt: see TyDisplay.fresh_params)
        if (origin != prop && mentions(me, type, d.fresh_params)) annotation = nullptr;
        // (Printed, not reused: tsc doesn't reuse the `unique symbol` annotation that declares the symbol)
        // An optional property's type has undefined under strictNullChecks, unless exactOptionalPropertyTypes (tsc's
        // addOptionality in getTypeOfSymbol); a reused annotation doesn't say it
        Ty const* shown = type;
        if ((prop->flags & PROP_OPTIONAL) && !(prop->flags & PROP_PATTERN_OPTIONAL) && compiler_options()->strict_null_checks &&
            !compiler_options()->exact_optional_property_types && ty_without_undefined(me, type) == type
        ) {
            shown = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
        }
        bool optional = (prop->flags & PROP_OPTIONAL) != 0;
        // (A mapped property whose template added `| undefined` to what the annotation says: the annotation in a new
        // union, like a parameter's, see annotation_plus_undefined())
        bool reverse = prop_decl_rare(prop->decl)->reverse_of != nullptr;
        if ((mapped_from || reverse) && annotation && !d.reused && reuses_nodes(d) && (origin->flags & PROP_OPTIONAL) &&
            annotation_plus_undefined(me, annotation, shown)
        ) {
            display_node_plus_undefined(me, d, annotation);
        }
        else if (literal_initializer && !d.reused) out(reused_if(d, true), literal_initializer->inner_span.sv);
        else if (mapped_from && annotation && !annotation_is(me, annotation, shown, false)) ty_display(me, d, shown);
        else if (d.reused && annotation && annotation->tag != TYPE_UNIQUE_SYMBOL) ty_display(me, d, shown);
        else if (annotation && annotation->tag == TYPE_UNIQUE_SYMBOL && reuses_nodes(d) && span_valid(d.decl_literal) &&
            span_contains_span(d.decl_literal, annotation->unique_symbol.member) && (type->flags & TYF_UNIQUE_ES_SYMBOL)
        ) {
            // (tsc's canReuseTypeNode: a `unique symbol` inside the enclosing declaration)
            display_node(me, d, annotation);
        }
        else if (annotation && reuses_nodes(d) && annotation_is(me, annotation, type, optional)) display_node(me, d, annotation);
        else display_annotated(me, type_d, annotation, shown, optional);
        out_lit(d, "; ");
    }
    out_lit(d, "}");
}

static bool is_function_like(Checker* me, TyDisplay d, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || shows_alias(d, ty)) return false;
    // (Inside its own rendering it is `any` (or `typeof f`): no function type node to parenthesize)
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) return false;
    }
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->index_count && !members->mapped &&
        members->call_count + members->construct_count == 1;
}

// The static side of a mixin class, which tsc prints as a type literal rather than `typeof C` (createAnonymousTypeNode:
// only a class without getBaseTypeVariableOfClass is a type query)
static bool is_mixin_static_side(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_CLASS_STATIC && ty->declared.symbol &&
        !ty->declared.merged_value && ty_class_base_type_variable(me, ty->declared.symbol);
}

// A type literal that prints as an intersection: its abstract construct signatures are split off (display_structure())
static bool displays_as_intersection(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty)) return false;
    if (ty->kind != TY_ANONYMOUS && !is_mixin_static_side(me, ty)) return false;
    Members const* members = ty_members(me, ty);
    if (members->mapped) return false;
    u32 abstract_count = 0;
    for (u32 i = 0; i < members->construct_count; i++) abstract_count += (members->construct[i]->flags & SIGNATURE_ABSTRACT) != 0;
    return abstract_count && (abstract_count > 1 || members->prop_count || members->index_count || members->call_count ||
        members->construct_count > abstract_count);
}

static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens) {
    if (parens) out_lit(d, "(");
    ty_display(me, d, ty);
    if (parens) out_lit(d, ")");
}

// A distributive conditional type whose check type is no longer a type parameter, after ` extends `: written as is it
// wouldn't distribute, so tsc's conditionalTypeToTypeNode (GenerateNamesForShadowedTypeParams: the types baseline and
// declaration emit) makes the check type a new type parameter T first: `C extends infer T ? T extends C ? T extends
// E<T> ? X<T> : Y<T> : never : never`
static void display_distributive_conditional(Checker* me, TyDisplay d, Ty const* ty, PrintedRange check_range) {
    Ty const* root = ty->conditional.root;
    Ty const* param = ty_synthetic_type_param(me, c_sv("T"));
    // (prependTypeMapping(root.checkType, T, type.mapper))
    TyMapper const* outer = ty->conditional.mapper;
    u32 len = 1 + (outer ? outer->len : 0);
    Ty const** sources = malloc(len * sizeof(Ty const*));
    Ty const** targets = malloc(len * sizeof(Ty const*));
    sources[0] = root->conditional.check;
    targets[0] = param;
    for (u32 i = 1; i < len; i++) {
        sources[i] = outer->sources[i - 1];
        targets[i] = outer->targets[i - 1];
    }
    TyMapper const* mapper = ty_mapper_new(me, (TyList){ .data = sources, .len = len }, (TyList){ .data = targets, .len = len });
    free(sources);
    free(targets);
    Ty const* extends = ty_instantiate(me, root->conditional.extends, mapper);
    Ty const* when_true = ty_instantiate(me, ty_conditional_branch(me, root, true), mapper);
    Ty const* when_false = ty_instantiate(me, ty_conditional_branch(me, root, false), mapper);
    TyDisplay fresh = reused_if(d, false);
    out(fresh, c_sv("infer "));
    out_type_param_name(me, fresh, param, false);
    out(fresh, c_sv(" ? "));
    out_type_param_name(me, fresh, param, false);
    out(fresh, c_sv(" extends "));
    reprint_range(d, check_range);
    out(fresh, c_sv(" ? "));
    out_type_param_name(me, fresh, param, false);
    out(fresh, c_sv(" extends "));
    TyDisplay extends_d = d;
    extends_d.infer_params = ty_conditional_own_infers(me, root);
    display_parenthesized_if(me, extends_d, extends, extends->kind == TY_CONDITIONAL && !shows_alias(d, extends));
    out(fresh, c_sv(" ? "));
    ty_display(me, d, when_true);
    out(fresh, c_sv(" : "));
    ty_display(me, d, when_false);
    out(fresh, c_sv(" : never : never"));
}

static bool conditional_elided(TyDisplay d, Ty const* ty) {
    Ty const* root = ty->conditional.root ? ty->conditional.root : ty;
    u32 depth = 0;
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        Ty const* other = i->ty;
        depth += other->kind == TY_CONDITIONAL && (other->conditional.root ? other->conditional.root : other) == root;
    }
    return depth > 10;
}

static void display_conditional(Checker* me, TyDisplay d, Ty const* ty) {
    // Eleven instantiations of one conditional type deep, the next is the elided information placeholder (tsc's
    // visitAndTransformType counts conditional types by their root's node); `any` without truncation (the
    // types baseline, declaration emit), `...` in messages
    if (conditional_elided(d, ty)) {
        out(d, d.enclosing ? c_sv("any") : c_sv("..."));
        return;
    }
    TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
    d.visiting = &visit;
    PrintedRange check_range = printed_range_start(d);
    display_parenthesized_if(
        me, d, ty->conditional.check,
        is_function_like(me, d, ty->conditional.check) ||
            (ty->conditional.check->kind == TY_CONDITIONAL && !shows_alias(d, ty->conditional.check))
    );
    printed_range_end(d, &check_range);
    out_lit(d, " extends ");
    if (d.param_names && ty->conditional.root && ty->conditional.root->conditional.distributive &&
        ty->conditional.check->kind != TY_TYPE_PARAM
    ) {
        display_distributive_conditional(me, d, ty, check_range);
        return;
    }
    TyDisplay extends_d = d;
    extends_d.infer_params = ty_conditional_own_infers(me, ty->conditional.root);
    display_parenthesized_if(me, extends_d, ty->conditional.extends, ty->conditional.extends->kind == TY_CONDITIONAL &&
        !shows_alias(d, ty->conditional.extends));
    out_lit(d, " ? ");
    ty_display(me, d, ty_conditional_branch(me, ty, true));
    out_lit(d, " : ");
    ty_display(me, d, ty_conditional_branch(me, ty, false));
}


// A union of every member of one enum (and nothing else) prints as the enum, see display_union()
static bool is_whole_enum(Checker* me, Ty const* ty) {
    if (ty->kind != TY_UNION) return false;
    Ty const* first = ty->composite.types.data[0];
    if (first->kind != TY_LITERAL || !first->literal.enum_decl) return false;
    Ty const* enum_type = ty_enum_of_member(me, first);
    if (enum_type->kind != TY_UNION || enum_type->composite.types.len != ty->composite.types.len) return false;
    for (u32 i = 0; i < ty->composite.types.len; i++) {
        if (ty_regular(me, ty->composite.types.data[i]) != enum_type->composite.types.data[i]) return false;
    }
    return true;
}

static bool needs_parens_in_array(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty)) return false;
    if ((ty->flags & TYF_BOOLEAN) || is_whole_enum(me, ty)) return false;
    // `(typeof C)[]`: a type query is parenthesized too
    bool type_query = ty->kind == TY_DECLARED &&
        (ty->declared.kind == TY_DECLARED_CLASS_STATIC || ty->declared.kind == TY_DECLARED_ENUM_OBJECT ||
            ty->declared.kind == TY_DECLARED_NAMESPACE) && ty->declared.name && ty->declared.name->len;
    // `(readonly T[])[]`, `(readonly [K, V])[]`: a type operator
    bool readonly = ty->kind == TY_REFERENCE && (ty->reference.target == my readonly_array_target ||
        (ty_is_tuple(ty) && ty->reference.target->declared.tuple->readonly));
    // (`(unique symbol)[]`, `(typeof sym)[]`: a type operator or query)
    bool unique_symbol = ty->kind == TY_LITERAL && (ty->flags & TYF_UNIQUE_ES_SYMBOL);
    // (A conditional type printed as the elided information placeholder is `any`)
    bool conditional = ty->kind == TY_CONDITIONAL && !conditional_elided(d, ty);
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || conditional || ty->kind == TY_KEYOF ||
        type_query || readonly || unique_symbol || is_function_like(me, d, ty);
}

static void display_tuple(Checker* me, TyDisplay d, TupleShape const* shape, TyList args) {
    if (shape->readonly) out_lit(d, "readonly ");
    out_lit(d, "[");
    bool* collisions = name_collisions(me, d, (TyList){ .data = args.data, .len = shape->arity });
    for (u32 i = 0; i < shape->arity; i++) {
        if (i) out_lit(d, ", ");
        bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
        bool optional = shape->flags[i] & TUPLE_ELEMENT_OPTIONAL;
        Ty const* element = ty_remove_missing(me, args.data[i], optional); // (tsc's typeReferenceToTypeNode)
        if (rest) out_lit(d, "...");
        if (shape->names && shape->names[i]) {
            out(d, atom_sv(shape->names[i]));
            if (optional) out_lit(d, "?");
            out_lit(d, ": ");
            ty_display(me, qualified_if(d, collisions, i), element);
        } else {
            display_parenthesized_if(me, qualified_if(d, collisions, i), element, optional && needs_parens_in_array(me, d, element));
            if (optional) out_lit(d, "?");
        }
    }
    free(collisions);
    out_lit(d, "]");
}

static void display_union(Checker* me, TyDisplay d, Ty const* ty) {
    // tsc's formatUnionTypes(): `true | false` prints as `boolean`, `null` and `undefined` go last
    TyList types = ty->composite.types;
    bool first = true;
    u32 nullable = 0;
    bool* collisions = name_collisions(me, d, types);
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        if (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE)) {
            nullable |= t->flags;
            continue;
        }
        if (!first) out_lit(d, " | ");
        first = false;
        if (ty_regular(me, t) == ty_intrinsic(me, TY_FALSE) && i + 1 < types.len &&
            ty_regular(me, types.data[i + 1]) == ty_intrinsic(me, TY_TRUE)
        ) {
            out_lit(d, "boolean");
            i++;
            continue;
        }
        // A run of all the members of an enum prints as the enum
        if (t->kind == TY_LITERAL && t->literal.enum_decl) {
            Ty const* enum_type = ty_enum_of_member(me, t);
            if (enum_type->kind == TY_UNION) {
                u32 count = enum_type->composite.types.len;
                if (i + count <= types.len &&
                    ty_regular(me, types.data[i + count - 1]) == enum_type->composite.types.data[count - 1]
                ) {
                    ty_display(me, d, enum_type);
                    i += count - 1;
                    continue;
                }
            }
        }
        // (A member that is itself a union is one in an origin, `(string | false) | (string | false)[]` of a hidden
        // local alias: tsc's printer parenthesizes a union constituent of a union)
        bool union_member = t->kind == TY_UNION && !(t->flags & TYF_BOOLEAN) && !ty_is_enum_union(t) &&
            !(t->composite.origin && t->composite.origin->kind == TY_KEYOF);
        bool parens = is_function_like(me, d, t) || (!shows_alias(d, t) && (t->kind == TY_CONDITIONAL || t->kind == TY_INTERSECTION ||
            union_member));
        display_parenthesized_if(me, qualified_if(d, collisions, i), t, parens);
    }
    free(collisions);
    if (nullable & TYF_NULL) {
        out(d, first ? c_sv("null") : c_sv(" | null"));
        first = false;
    }
    if (nullable & TYF_UNDEFINED) {
        out(d, first ? c_sv("undefined") : c_sv(" | undefined"));
    }
}

void ty_display(Checker* me, TyDisplay d, Ty const* ty) {
    if (d.tracker && d.tracker->truncated) return;
    if (ty == my display_of) ty = my display_as;
    if (ty->kind == TY_SUBSTITUTION) {
        // Its base (tsc's typeToTypeNodeHelper)
        if (!ty_is_no_infer(ty)) {
            ty_display(me, d, ty->substitution.base);
            return;
        }
        out_lit(d, "NoInfer<");
        ty_display(me, d, ty->substitution.base);
        out_lit(d, ">");
        return;
    }
    // (A union or intersection prints reduced: tsc's typeToTypeNodeHelper without NoTypeReduction)
    if (!d.no_reduction && (ty->kind == TY_INTERSECTION || ty->kind == TY_UNION)) ty = ty_reduced(me, ty);
    d.depth++;
    d.mapped_by = nullptr; // (A type is printed as it is, annotations of other declarations inside it aren't instantiated)
    if (d.depth > MAX_DISPLAY_DEPTH * 4) {
        out_lit(d, "...");
        return;
    }

    // Widening only reaches through the structure of expression types (temporaries lowered from the legacy checker)
    // (JSX attributes are typed like tsc already: widened values, and `true` for an attribute without one)
    if (d.widen_literals && !(ty->kind == TY_LITERAL || ty->kind == TY_UNION)) {
        d.widen_literals = false;
    }

    if (ty_is_enum_union(ty)) {
        out_symbol_name(me, d, ty->alias, SYMBOL_TYPE);
        return;
    }
    if (shows_alias(d, ty)) {
        d.widen_literals = false; // (A union shown by its alias: its type arguments are as written)
        out_symbol_name(me, d, ty->alias, SYMBOL_TYPE);
        if (ty->alias_args.len) {
            out_lit(d, "<");
            display_list(me, d, ty->alias_args);
            out_lit(d, ">");
        }
        return;
    }

    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
            out(d, ty->intrinsic.name);
            break;

        case TY_LITERAL:
            if (d.widen_literals && !ty->literal.enum_decl) {
                TyDisplay widened = d;
                widened.widen_literals = false;
                ty_display(me, widened, ty_base_type_of_literal(me, ty));
                break;
            }
            if (ty->flags & TYF_UNIQUE_ES_SYMBOL) {
                // `unique symbol` (tsc's AllowUniqueESSymbolType, in typeToString's default flags) unless asked to name
                // the symbol, as an annotation that is reused does (it says `typeof sym`: the one that says `unique
                // symbol` declares the symbol, and tsc doesn't reuse that one)
                if (d.tracker && !d.reused) {
                    // (declaration emit names it if it can, tsc's typeToTypeNodeHelper without AllowUniqueESSymbolType)
                    UniqueSymbolDecl const* unique = ty->literal.unique;
                    Symbol const* owner = unique->owner;
                    // (A member's symbol is reached through its class, or a variable of its interface's type)
                    bool named = owner && ((owner->flags & SYMBOL_INTERFACE) && span_valid(unique->member) ?
                        symbol_type_accessible_at(d.enclosing, owner) : symbol_value_accessible_at(d.enclosing, owner));
                    if (named) {
                        out_lit(d, "typeof ");
                        out_unique_symbol_name(me, d, unique);
                    } else {
                        d.tracker->report(d.tracker, DECL_REPORT_UNIQUE_SYMBOL, c_sv(""));
                        out_lit(d, "unique symbol");
                    }
                } else if (!d.typeof_unique_symbols && !d.reused) {
                    out_lit(d, "unique symbol");
                } else {
                    out_lit(d, "typeof ");
                    out_unique_symbol_name(me, d, ty->literal.unique);
                }
            } else if (ty->literal.enum_decl) {
                // The only member of an enum is the enum. TODO(fresh literals): tsc prints the *fresh* type of the
                // expression `E.x` as `E.x` even then (error messages); `.types` baselines show the widened `E`
                bool member = ty->literal.enum_member != ty->literal.enum_decl && ty_enum_of_member(me, ty) != ty;
                csview member_name = ty->literal.enum_member->name;
                // (`#x` written as a private name is tsc's __missing below, `"#x"` written as a string is a name)
                EnumCase const* member_case = symbol_find_node(ty->literal.enum_member, SYMBOL_DECL_ENUM_MEMBER);
                bool private_name = member_name.size && member_name.buf[0] == '#' && !(member_case && member_case->name_is_value);
                if (member && (private_name ? false : !is_identifier_name(member_name) || (member_name.size && member_name.buf[0] == '#'))) {
                    // `(typeof E)["a b"]`: a member whose name isn't an identifier
                    out_lit(d, "(typeof ");
                    out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_VALUE);
                    out_lit(d, ")[");
                    display_string_literal(d, member_name);
                    out_lit(d, "]");
                    break;
                }
                out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_TYPE);
                if (member) {
                    out_lit(d, ".");
                    // `enum E { #x }`: tsc's binder names a private name outside a class InternalSymbolName.Missing
                    out(d, private_name ? c_sv("__missing") : member_name);
                }
            } else if (ty->literal.string) {
                display_string_literal(d, atom_sv(ty->literal.string));
            } else if (ty->flags & TYF_BOOLEAN_LITERAL) {
                out(d, ty->literal.value.boolean ? c_sv("true") : c_sv("false"));
            } else {
                isize_t start = cstr_size(d.target);
                cstr_append_value_js(d.target, &ty->literal.value);
                underline_since(d, start);
                if (ty->flags & TYF_BIGINT_LITERAL) out_lit(d, "n");
            }
            break;

        case TY_TYPE_PARAM: {
            if (d.tracker && d.in_object_literal && ty->param.state->is_this) {
                d.tracker->report(d.tracker, DECL_REPORT_THIS, c_sv(""));
            }
            bool declared_here = false;
            for (u32 i = 0; i < d.infer_params.len; i++) declared_here |= d.infer_params.data[i] == ty;
            if (declared_here) out_lit(d, "infer ");
            // (Declared here, the name gets its declaration's range: tsc's typeParameterToName, reused)
            out_type_param_name(me, declared_here ? reused_if(d, true) : d, ty, false);
            if (declared_here && ty->param.state->constraint_node) {
                out_lit(d, " extends ");
                TyDisplay inner = d;
                inner.infer_params = (TyList){0};
                ty_display(me, inner, ty_param_constraint(me, ty));
            }
        }   break;

        case TY_DECLARED:
            switch (ty->declared.kind) {
                case TY_DECLARED_CLASS_STATIC:
                case TY_DECLARED_ENUM_OBJECT:
                case TY_DECLARED_NAMESPACE:
                    if (!ty->declared.name->len || is_mixin_static_side(me, ty) || is_nested_expando_function(ty)) {
                        display_structure(me, d, ty); // A module nobody named
                        break;
                    }
                    if (class_as_literal(d, ty)) {
                        display_class_literal(me, d, ty);
                        break;
                    }
                    out_lit(d, "typeof ");
                    out_declared_name(me, d, ty, SYMBOL_VALUE);
                    break;

                case TY_DECLARED_TUPLE_TARGET:
                    display_tuple(me, d, ty->declared.tuple, ty->declared.type_params);
                    break;

                default:
                    if (class_as_literal(d, ty)) {
                        display_class_literal(me, d, ty);
                        break;
                    }
                    out_declared_name(me, d, ty, SYMBOL_TYPE);
                    if (ty_local_type_params(ty).len) {
                        out_lit(d, "<");
                        display_list(me, d, ty_local_type_params(ty));
                        out_lit(d, ">");
                    }
                    break;
            }
            break;

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyList args = ty_reference_args(me, ty);
            if (target->declared.kind == TY_DECLARED_CLASS_STATIC && is_mixin_static_side(me, target)) {
                display_structure(me, d, ty);
            } else if (target->declared.kind != TY_DECLARED_TUPLE_TARGET && class_as_literal(d, target)) {
                display_class_literal(me, d, ty);
            } else if (target->declared.kind == TY_DECLARED_CLASS_STATIC) {
                // The static side of a local class, applied to its outer type parameters (tsc: an anonymous type)
                out_lit(d, "typeof ");
                out_declared_name(me, d, target, SYMBOL_VALUE);
            } else if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                display_tuple(me, d, target->declared.tuple, args);
            } else if (target == my array_target || target == my readonly_array_target) {
                if (target == my readonly_array_target) out_lit(d, "readonly ");
                // (An `infer U` declared here is parenthesized too: `readonly (infer U)[]`)
                bool infer_here = false;
                for (u32 i = 0; i < d.infer_params.len; i++) infer_here |= d.infer_params.data[i] == args.data[0];
                display_parenthesized_if(me, d, args.data[0], infer_here || needs_parens_in_array(me, d, args.data[0]));
                out_lit(d, "[]");
            } else {
                if (ty_elides_default_type_args(me, target)) {
                    // `Iterable<T>` for `Iterable<T, any, any>` (tsc does this for four lib types only)
                    while (args.len) {
                        Ty const* fallback = ty_param_default(me, target->declared.type_params.data[args.len - 1]);
                        if (fallback != args.data[args.len - 1]) break;
                        args.len--;
                    }
                }
                u32 outer = target->declared.outer_count;
                if (outer) {
                    out_outer_type_args(me, d, target->declared.outer,
                        (TyList){ .data = target->declared.type_params.data, .len = outer }, (TyList){ .data = args.data, .len = outer });
                    args.data += outer;
                    args.len -= outer;
                }
                out_declared_name(me, d, target, SYMBOL_TYPE);
                if (args.len) { // (None: a class or interface with only a this-argument)
                    out_lit(d, "<");
                    display_list(me, d, args);
                    out_lit(d, ">");
                }
            }
        }   break;

        case TY_ANONYMOUS: {
            if (ty->anonymous.module_clone && display_module_clone(me, d, ty)) break;
            if (ty->anonymous.commonjs_module) {
                out_lit(d, "typeof module");
                break;
            }
            // An anonymous type inside its own rendering prints as `any`, like tsc's elided information placeholder
            // (`function f() { return f }` is `() => any`)
            // (Not a reverse mapped type, which has no symbol: its properties have placeholders of their own, see
            // reverse_placeholder())
            for (TyDisplayVisit const* i = ty_reverse_of(ty) ? nullptr : d.visiting; i; i = i->outer) {
                if (i->ty != ty) continue;
                // (A function declared in a file or namespace prints as `typeof f` instead: tsc's
                // shouldWriteTypeOfFunctionSymbol)
                Symbol const* function = ty->anonymous.function && !ty->anonymous.target ? ty->anonymous.function->symbol : nullptr;
                // (An overloaded function's type is its symbol's)
                if (!function && !ty->anonymous.target && !ty->anonymous.is_reverse && ty->anonymous.merged_value &&
                    (ty->anonymous.merged_value->flags & SYMBOL_FUNCTION)
                ) {
                    function = ty->anonymous.merged_value;
                }
                if (function && is_non_local_function(function)) {
                    out_lit(d, "typeof ");
                    out_symbol_name(me, d, function, SYMBOL_VALUE);
                    return;
                }
                // (and a static method as `typeof C.m`)
                FunctionImpl const* method = ty->anonymous.function && !ty->anonymous.target ? ty->anonymous.function : nullptr;
                Symbol const* owner = method ? function_impl_rare(method)->static_method_of : nullptr;
                if (owner && span_valid(owner->name_span) && span_valid(function_impl_member_name(method))) {
                    out_lit(d, "typeof ");
                    // (Qualified by the class only where printed for a location: tsc's symbolToTypeNode without an
                    // enclosing declaration has the symbol alone)
                    if (d.enclosing) {
                        out_symbol_name(me, d, owner, SYMBOL_VALUE);
                        out_lit(d, ".");
                    }
                    out(d, function_impl_member_name(method).sv);
                    return;
                }
                // (and a type alias's type literal by the alias: tsc's getTypeAliasForTypeLiteral)
                Symbol const* alias = type_alias_of_literal(ty);
                if (alias) {
                    out_symbol_name(me, d, alias, SYMBOL_TYPE);
                    return;
                }
                out_lit(d, "any");
                return;
            }
            // A JavaScript constructor function is a class
            Symbol const* js_class = ty->anonymous.function && !ty->anonymous.target ?
                ty_js_constructor_class(ty->anonymous.function) : nullptr;
            if (js_class) {
                out_lit(d, "typeof ");
                csview js_name;
                // (A declared function by its symbol chain from here: `typeof x.F` through `const x = require()`)
                if (ty_js_assigned_class_name(js_class, &js_name)) out(d, js_name);
                else if (js_class->flags & SYMBOL_FUNCTION) out_symbol_name(me, d, js_class, SYMBOL_VALUE);
                else out(d, js_class->name);
                break;
            }
            // A function a CommonJS module exports with the module's other exports merged in (tsc's
            // getCommonJsExportEquals clone, a ValueModule too)
            Symbol const* exported = ty->anonymous.merged_value && !ty->anonymous.is_reverse ? symbol_rare(ty->anonymous.merged_value)->merged_export_equals_of : nullptr;
            // (the function's own type too: tsc's clone is its merged symbol, recordMergedSymbol)
            if (!exported && ty->anonymous.function && !ty->anonymous.target) exported = export_equals_clone_of(ty->anonymous.function->symbol);
            if (exported && (exported->flags & SYMBOL_FUNCTION) && span_valid(exported->name_span)) {
                out_lit(d, "typeof ");
                out(d, exported->name_span.sv);
                break;
            }
            // An instantiation expression made by `typeof f<T>` reprints that node (tsc's createAnonymousTypeNode with an
            // InstantiationExpressionType whose node is a type query)
            if (ty->anonymous.applies_type_args) {
                TypeNode const* query = intern_get(&my links, (u64[]){ KEY_INSTANTIATION_QUERY, ty->id }, 2);
                if (query && !simple_node_fails(me, d, query)) {
                    TyDisplay node_d = d;
                    node_d.reused = true;
                    display_node(me, node_d, query);
                    break;
                }
            }
            TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
            d.visiting = &visit;
            display_structure(me, d, ty);
        }   break;

        case TY_UNION:
            // What it was made from (tsc's typeToTypeNodeHelper prints a union's origin)
            if (ty->composite.origin) {
                Ty const* origin = ty->composite.origin;
                if (origin->kind == TY_UNION) display_union(me, d, origin);
                else ty_display(me, d, origin);
                break;
            }
            display_union(me, d, ty);
            break;

        case TY_INTERSECTION: {
            bool* collisions = name_collisions(me, d, ty->composite.types);
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                Ty const* t = ty->composite.types.data[i];
                if (i) out_lit(d, " & ");
                // (A union that prints as the `keyof X` it was made from is a type operator, which needs none)
                bool keyof_origin = t->kind == TY_UNION && t->composite.origin && t->composite.origin->kind == TY_KEYOF;
                display_parenthesized_if(me, qualified_if(d, collisions, i), t, !shows_alias(d, t) &&
                    ((t->kind == TY_UNION && !keyof_origin) || t->kind == TY_CONDITIONAL || is_function_like(me, d, t) ||
                    displays_as_intersection(me, d, t)));
            }
            free(collisions);
        }   break;

        case TY_KEYOF: {
            // (tsc's parenthesizeOperandOfTypeOperator: unions, intersections, conditional and function types, not
            // another type operator or query: `keyof readonly any[]`, `keyof keyof T`)
            Ty const* operand = ty->keyof.operand;
            bool parens = needs_parens_in_array(me, d, operand) && !shows_alias(d, operand) &&
                (operand->kind == TY_UNION || operand->kind == TY_INTERSECTION ||
                    (operand->kind == TY_CONDITIONAL && !conditional_elided(d, operand)) || is_function_like(me, d, operand));
            out_lit(d, "keyof ");
            display_parenthesized_if(me, d, operand, parens);
        }   break;

        case TY_INDEXED:
            display_parenthesized_if(me, d, ty->indexed.object, needs_parens_in_array(me, d, ty->indexed.object));
            out_lit(d, "[");
            ty_display(me, d, ty->indexed.index);
            out_lit(d, "]");
            break;

        case TY_SUBSTITUTION: impossible;

        case TY_CONDITIONAL: {
            TyPrinted const* before = printed_before(d, ty);
            if (before) {
                reprint_range(d, before->range);
                break;
            }
            PrintedRange whole = printed_range_start(d);
            display_conditional(me, d, ty);
            printed_range_end(d, &whole);
            remember_printed(d, ty, whole);
        }   break;

        case TY_TEMPLATE_LITERAL:
            out_lit(d, "`");
            for (u32 i = 0; i <= ty->template_literal.types.len; i++) {
                if (i) {
                    out_lit(d, "${");
                    ty_display(me, d, ty->template_literal.types.data[i - 1]);
                    out_lit(d, "}");
                }
                display_template_text(d, atom_sv(ty->template_literal.texts[i]));
            }
            out_lit(d, "`");
            break;

        case TY_STRING_MAPPING: {
            static char const* const names[] = { "Uppercase", "Lowercase", "Capitalize", "Uncapitalize" };
            out(d, csview_from(names[ty->string_mapping.mapping]));
            out_lit(d, "<");
            ty_display(me, d, ty->string_mapping.type);
            out_lit(d, ">");
        }   break;

        case TY_EVOLVING_ARRAY: // (Never leaves a flow query: only debug output sees one)
            out_lit(d, "evolving ");
            ty_display(me, d, ty->evolving_array.element);
            out_lit(d, "[]");
            break;
    }
}

cstr ty_to_cstr(Checker* me, Ty const* ty) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret }, ty);
    return ret;
}

// Whether an object literal's type is of a context sensitive literal (tsc's isContextSensitive of an object literal: a
// method or function value that is), as far as its properties tell
static bool object_literal_is_context_sensitive(Checker* me, Ty const* ty, u32 depth) {
    if (depth > 4) return false;
    Members const* members = ty_members(me, ty);
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        if (prop->decl->function && ty_function_is_context_sensitive(prop->decl->function)) return true;
        Ty const* type = prop->type_state == LAZY_STATE_DONE ? prop->type : nullptr;
        if (!type || type->kind != TY_ANONYMOUS || type->anonymous.target) continue;
        if (type->anonymous.function && type->anonymous.function->is_expression &&
            ty_function_is_context_sensitive(type->anonymous.function)
        ) {
            return true;
        }
        if (type->anonymous.object_literal && object_literal_is_context_sensitive(me, type, depth + 1)) return true;
    }
    return false;
}

// The innermost scope of the statements around `at` that declare something: a namespace's, a function declaration's
// body, a block's (expressions aren't entered: a function expression's own scope isn't found)
static Scope const* statement_scope_at(Block const* block, Scope const* scope, Span at, u32 depth) {
    if (depth > 64) return scope;
    for_each (i, vec_statement, &block->statements) {
        if (!span_contains_span(i->span, at)) continue;
        Block const* inner = nullptr;
        Scope const* inner_scope = nullptr;
        switch (i->tag) {
            case STATEMENT_DECL_NAMESPACE:
                inner = &i->decl.namespace->contents;
                inner_scope = i->decl.namespace->inner_scope;
                break;
            case STATEMENT_DECL_FUNCTION:
                if (!i->decl.function.impl) return scope;
                inner = &i->decl.function.impl->body;
                inner_scope = inner->scope;
                break;
            case STATEMENT_BLOCK:
                inner = &i->block;
                inner_scope = inner->scope;
                break;
            default:
                return scope;
        }
        return statement_scope_at(inner, inner_scope ? inner_scope : scope, at, depth + 1);
    }
    return scope;
}

Scope const* ty_report_enclosing(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->anonymous.target || ty->anonymous.is_reverse) return nullptr;
    FunctionImpl const* function = ty->anonymous.function;
    if (function) {
        // (A function expression or arrow function, not an object literal method: a declaration of its own)
        if (!function->is_expression || span_valid(function_impl_member_name(function)) ||
            ty_function_is_context_sensitive(function)
        ) {
            return nullptr;
        }
        // Inside it, where its parameters are declared
        for_each (i, vec_function_arg, &function->args) {
            Binding const* binding = &i->initializer.binding;
            if (binding->tag == BINDING_IDENTIFIER && binding->symbol && binding->symbol->decl_count &&
                binding->symbol->decls[0].block
            ) {
                return binding->symbol->decls[0].block;
            }
        }
        SourceFile const* file = function->span_source ? function->span_source : function->full_span.source;
        return file ? file->scope : nullptr;
    }
    if (!ty->anonymous.object_literal || ty->anonymous.jsx_attributes) return nullptr;
    Members const* members = ty_members(me, ty);
    // (Where a property written in the literal is: one spread from elsewhere was written elsewhere)
    Span at = span_null;
    for (u32 i = 0; i < members->prop_count && !span_valid(at); i++) {
        if (members->props[i]->decl->literal_own) at = members->props[i]->decl->name_span;
    }
    // (Else where the literal is: one of spread properties only)
    if (!span_valid(at) && ty->anonymous.literal_expr) at = ty->anonymous.literal_expr->inner_span;
    SourceFile const* file = span_valid(at) ? at.source : nullptr;
    if (!file || !file->scope || object_literal_is_context_sensitive(me, ty, 0)) return nullptr;
    // HACK: tsc prints it from the literal (its valueDeclaration), tttt from the innermost statement-level scope around
    // it, which doesn't see into function expressions
    return statement_scope_at(&file->ast, file->scope, at, 0);
}

cstr ty_type_node_to_cstr(Checker* me, TypeNode const* node) {
    cstr ret = cstr_null;
    display_node(me, (TyDisplay){ .target = &ret }, node);
    return ret;
}
