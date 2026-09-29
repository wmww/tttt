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
#include "port_statement.h"
#include "class_decl.h"

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

static void out(TyDisplay d, csview sv) {
    isize_t start = cstr_size(d.target);
    cstr_append_sv(d.target, sv);
    underline_since(d, start);
}
#define out_lit(d, s) out(d, c_sv(s))

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
    TypeDecl const* type_decl = symbol_find_node(ty->alias, SYMBOL_DECL_TYPE_ALIAS);
    return !type_decl || !type_decl->local;
}

// Type arguments. The first one is parenthesized if it's a generic function type (tsc's parenthesizeTypeArguments:
// `X<<T>() => T>` would start with `<<`)
static void display_list(Checker* me, TyDisplay d, TyList list) {
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
        ty_display(me, d, arg);
        if (parens) out_lit(d, ")");
    }
}

static bool same_symbol(Checker* me, Symbol const* a, Symbol const* b) {
    Binder const* binder = program_get_binder(my program);
    return binder_redirect(binder, a) == binder_redirect(binder, b);
}

// With TyDisplay.enclosing, qualified as far as needed from there (symbol_chain.h), else bare or fully qualified (error
// messages)
static void out_symbol_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    csview js_name;
    if (ty_js_assigned_class_name(symbol, &js_name)) {
        out(d, js_name);
        return;
    }
    Symbol const* parent = symbol->parent;
    if ((!parent && !d.enclosing) || symbol->is_global_this || (!d.enclosing && !d.fully_qualified)) {
        out(d, ty_symbol_display_name(symbol).sv);
        return;
    }
    if (!d.enclosing) {
        bool quoted_parent = parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\'');
        // Modules and ambient modules (`declare module "m"`) are containers too, but not part of a name
        if (!parent->is_module && !quoted_parent) {
            out_symbol_name(me, d, parent, SYMBOL_NAMESPACE);
            out_lit(d, ".");
        }
        out(d, ty_symbol_display_name(symbol).sv);
        return;
    }
    // tsc's symbolToTypeNode: the chain accessible from here, `import("m")` when it starts at a module
    SymbolChain chain = symbol_chain_at(d.enclosing, symbol, meaning, true);
    u32 start = 0;
    if (chain.len && chain.items[0]->is_module) {
        cstr module_name = module_name_for_error(chain.items[0], nullptr, false);
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
        out_lit(d, ")");
        cstr_drop(&module_name);
        // (tsc's name of a file an import found is its absolute path, `/.src/` included where the harness writes the
        // underline: see SourceFile.loaded_by_import)
        SourceFile const* file = chain.items[0]->module ? chain.items[0]->module->file : nullptr;
        csview path = file ? source_file_display_path(file) : c_sv("");
        if (d.underline && file && file->loaded_by_import && path.size && path.buf[0] != '/') {
            cstr_append(d.underline, d.reused ? "      " : "^^^^^^");
        }
        start = 1;
    }
    cstr text = cstr_init();
    SymbolChain rest = { .len = chain.len - start };
    memcpy(rest.items, chain.items + start, rest.len * sizeof(*rest.items));
    if (start && rest.len) cstr_append_lit(&text, ".");
    symbol_chain_append_entity(&text, d.enclosing, &rest, true);
    out(d, cstr_sv(&text));
    cstr_drop(&text);
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

static void out_declared_name(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol) symbol = declared->declared.merged_value;
    if (!symbol && declared->declared.kind == TY_DECLARED_NAMESPACE) symbol = declared->declared.namespace_symbol;
    if (declared->kind == TY_DECLARED && declared->declared.commonjs_exports) {
        out(d, atom_sv(declared->declared.name));
        return;
    }
    // A module's object, also through `import * as ns` (whose symbol stands for the module): named from here like tsc
    if (d.enclosing && symbol && symbol_members(symbol)->is_module) {
        out_symbol_name(me, d, symbol_members(symbol), meaning);
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
    if (symbol && !symbol->is_module && (span_valid(symbol->name_span) ||
        (exported_class && exported_class->is_expression && exported_class->commonjs_module && d.enclosing))
    ) {
        out_symbol_name(me, d, symbol, meaning);
    } else {
        out(d, atom_sv(declared->declared.name));
    }
}

// tsc's escapeString() with a double quote: named escapes, other control characters and U+2028/2029/0085 as \uXXXX
static void display_string_literal(TyDisplay d, csview sv) {
    out_lit(d, "\"");
    for (isize_t i = 0; i < sv.size; i++) {
        unsigned char c = (unsigned char)sv.buf[i];
        switch (c) {
            case '"': out_lit(d, "\\\""); break;
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
                if (code) {
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
    out_lit(d, "\"");
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

static bool is_identifier_name(csview sv) {
    if (!sv.size || (sv.buf[0] >= '0' && sv.buf[0] <= '9')) return false;
    for (isize_t i = 0; i < sv.size; i++) {
        char c = sv.buf[i];
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
            c == '#' || (unsigned char)c >= 0x80;
        if (!valid) return false;
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

// A property declared with a string literal name prints quoted even if it reads as a number (tsc's isStringNamed)
static void display_member_name(Checker* me, TyDisplay d, Prop const* prop) {
    Prop const* root = prop;
    while (root->target) root = root->target;
    csview sv = atom_sv(prop->name);
    if (root->name != prop->name || !is_numeric_name(sv)) {
        display_prop_name(me, d, prop->name);
        return;
    }
    // (Every declaration: `{ 1: a, "1": b }` is a number)
    bool string_named = span_valid(root->decl->name_span) && !root->decl->numeric_redeclared;
    Span const* spans = root->decl->decl_span_count ? root->decl->decl_spans : &root->decl->name_span;
    u32 count = root->decl->decl_span_count ? root->decl->decl_span_count : 1;
    for (u32 i = 0; i < count && string_named; i++) {
        string_named = span_valid(spans[i]) && spans[i].sv.size >= 2 && (spans[i].sv.buf[0] == '"' || spans[i].sv.buf[0] == '\'');
    }
    if (string_named) display_string_literal(d, sv);
    else display_prop_name(me, d, prop->name);
}

static void display_prop_name(Checker* me, TyDisplay d, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (unique) {
        out_lit(d, "[");
        out_unique_symbol_name(me, d, unique->literal.unique);
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

// tsc's trackExistingEntityName(): the first name of an entity name means the same where the type is printed
static bool name_accessible(Checker* me, TyDisplay d, Symbol const* symbol, csview first, u32 meaning) {
    // Unresolved names are an error type; type parameters and parameters in scope where they're printed (tsc checks
    // that by resolving from the signature's fake scope)
    if (!symbol || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return true;
    if (symbol->decl_count && symbol->decls[0].kind == SYMBOL_DECL_BUILTIN) return true; // `object`
    // (A local of a function or block never is: tsc's isDeclarationVisible() is false for it, and `enclosing` only knows
    // file and namespace scopes anyway)
    Symbol const* found = scope_resolve(d.enclosing, first, meaning);
    return found && same_symbol(me, found, symbol);
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
static void display_node_fallback(Checker* me, TyDisplay d, TypeNode const* node) {
    bool element = d.array_element_node;
    d.array_element_node = false;
    Ty const* ty = node_type(me, d, node);
    bool parens = element && needs_parens_in_array(me, d, ty);
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
static bool jsdoc_reference_reusable(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->jsdoc_intrinsic || !identifier->target) return false;
    // A value used as a type (an expando `Outer.Inner = class {}`: tsc's getTypeFromJSDocValueReference) isn't found by
    // a type lookup, which reuse needs
    if (!(identifier->target->flags & (SYMBOL_TYPE | SYMBOL_ALIAS))) return false;
    Symbol const* first = vec_span_size(&identifier->namespaces) ?
        (vec_symbol_cptr_size(&identifier->namespace_symbols) ? *identifier->namespace_symbols.data : nullptr) : identifier->symbol;
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
    if (!vec_span_size(&identifier->namespaces)) {
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
    if (identifier->import_ref) {
        if (identifier->import_typeof) out_lit(d, "typeof ");
        out_lit(d, "import(");
        display_string_literal(d, cstr_sv(&identifier->import_ref->path));
        out_lit(d, ")");
        for_each (i, vec_span, &identifier->namespaces) {
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
    bool qualified = vec_span_size(&identifier->namespaces) > 0;
    Symbol const* symbol = identifier->symbol;
    if (qualified) {
        symbol = vec_symbol_cptr_size(&identifier->namespace_symbols) ?
            *vec_symbol_cptr_at(&identifier->namespace_symbols, 0) : nullptr;
    }
    Span first = qualified ? *vec_span_at(&identifier->namespaces, 0) : identifier->name;
    // (A type parameter that the instantiation maps: tsc's canReuseTypeNode)
    bool mapped = !qualified && d.mapped_by && symbol && (symbol->flags & SYMBOL_TYPE_PARAMETER) &&
        node_type(me, d, node) != node_type(me, (TyDisplay){0}, node);
    if (mapped || !name_accessible(me, d, symbol, first.sv, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE)) {
        isize_t start = cstr_size(d.target);
        // tsc names what it refers to anew (serializeTypeName) and keeps the type arguments, unless it's a local that
        // no name reaches (then the reference prints its type)
        Symbol const* target = identifier->target;
        if (!mapped && target && !target->is_local && !(target->flags & SYMBOL_TYPE_PARAMETER)) {
            out_symbol_name(me, reused_if(d, false), target, SYMBOL_TYPE);
            display_node_args(me, d, &identifier->generic_params);
        } else {
            display_node_fallback(me, d, node);
        }
        // tsc names it anew (serializeTypeName) in a node it marks as reused: of an `import("m").A` it made, the
        // `import(` and `).` are reused and the rest isn't
        char const* text = cstr_str(d.target) + start;
        if (!mapped && d.underline && strncmp(text, "import(\"", 8) == 0) {
            char const* close = strstr(text, "\").");
            char* underline = (char*)cstr_str(d.underline) + (cstr_size(d.underline) - (cstr_size(d.target) - start));
            if (close) {
                memset(underline, ' ', strlen("import("));
                memset(underline + (close + 1 - text), ' ', 2);
            }
        }
        return;
    }
    for_each (i, vec_span, &identifier->namespaces) {
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
        !name_accessible(me, d, leftmost->identifier.symbol, leftmost->identifier.name.sv, SYMBOL_VALUE)
    ) {
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
            display_node(me, d, &i->fallback);
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
    if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        !name_accessible(me, d, leftmost->identifier.symbol, leftmost->identifier.name.sv, SYMBOL_VALUE)
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
    out(d, expr->outer_span.sv);
    out_lit(d, "]");
}

// A computed name that isn't a literal or unique symbol isn't a member (tsc's shouldRemoveDeclaration)
static bool member_printed(Checker* me, InterfaceMember const* member) {
    return member->key.tag != INTERFACE_KEY_EXPR || !member->key.expr.ref ||
        ty_expr_type(me, member->key.expr.ref)->kind == TY_LITERAL;
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
        if (!member_printed(me, i)) continue;
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
    for_each (i, vec_interface_member, &interface->members) empty &= !member_printed(me, i);
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
        if (!member_printed(me, i)) continue;
        switch (i->key.tag) {
            case INTERFACE_KEY_MAPPED: {
                u32 saved = param_names_enter(d); // (A mapped type is a scope)
                if (i->key.mapped.readonly) out(d, i->key.mapped.readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
                out_lit(d, "[");
                if (d.param_names && i->key.mapped.symbol_ref) {
                    out_type_param_name(me, d, ty_type_param(me, i->key.mapped.symbol_ref), true);
                } else {
                    out(d, i->key.mapped.type_name.sv);
                }
                out_lit(d, " in ");
                display_node(me, d, &i->key.mapped.in_type);
                if (i->key.mapped.has_as_type) {
                    out_lit(d, " as ");
                    display_node(me, d, &i->key.mapped.as_type);
                }
                out_lit(d, "]");
                if (i->key.mapped.optional) out(d, i->key.mapped.optional > 0 ? c_sv("?") : c_sv("-?"));
                out_lit(d, ": ");
                if (i->type.parsed) display_node(me, d, &i->type);
                else out(reused_if(d, false), c_sv("any"));
                param_names_leave(d, saved);
            }   break;
            case INTERFACE_KEY_INDEX:
                if (i->readonly) out_lit(d, "readonly ");
                out_lit(d, "[");
                out(d, i->key.index.name.sv);
                out_lit(d, ": ");
                display_node(me, d, &i->key.index.type);
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
                if (i->key.tag == INTERFACE_KEY_NAME && i->key.name_is_member_function && i->type.tag == TYPE_CALLABLE) {
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
    bool union_list = separator[1] == '|';
    for_each (i, vec_type, list) {
        if (i->placeholder) continue;
        if (!first) out(d, csview_from(separator));
        first = false;
        // A member that needs parentheses without having them written: JSDoc's `?T`, `T=` of a union or function
        // type (tsc's printer parenthesizes what it transformed them into)
        bool parens = !i->parenthesized && (i->tag == TYPE_CALLABLE || i->tag == TYPE_UNION ||
            (!union_list && i->tag == TYPE_INTERSECTION));
        if (parens) out(reused_if(d, false), c_sv("("));
        display_node(me, d, i);
        if (parens) out(reused_if(d, false), c_sv(")"));
    }
}

// Prints an annotation as written, as reused
static void display_node(Checker* me, TyDisplay d, TypeNode const* node) {
    d.reused = !node->synthesized;
    // (Only a fallback of this very node, see display_node_identifier() and display_node_typeof(), is an element)
    bool array_element = d.array_element_node && !node->parenthesized;
    d.array_element_node = false;
    if (node->parenthesized) out_lit(d, "(");
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            TyDisplay element_d = d;
            element_d.array_element_node = array_element;
            display_node_identifier(me, element_d, node);
            break;
        }
        case TYPE_TYPEOF: {
            TyDisplay element_d = d;
            element_d.array_element_node = array_element;
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
        case TYPE_INDEXED:
            display_node(me, d, &node->indexed->interface);
            out_lit(d, "[");
            display_node(me, d, &node->indexed->index);
            out_lit(d, "]");
            break;
        case TYPE_KEYOF:
            out_lit(d, "keyof ");
            display_node(me, d, &node->keyof->interface);
            break;
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
        case TYPE_ANY: out_lit(d, "any"); break;
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
    if (!node->parsed || node->tag == TYPE_UNIQUE_SYMBOL) return false;
    Ty const* annotated = node_type(me, (TyDisplay){0}, node);
    return annotated == ty || (optional && annotated == ty_without_undefined(me, ty));
}

// Prints `ty` by reprinting its declaration's annotation when that's allowed and says the same, else as a type
static void display_annotated(Checker* me, TyDisplay d, TypeNode const* annotation, Ty const* ty, bool optional) {
    if (annotation && d.underline && d.enclosing && annotation_is(me, annotation, ty, optional)) {
        display_node(me, d, annotation);
    } else if (annotation && d.underline && d.enclosing && !annotation->parsed) {
        ty_display(me, reused_if(d, true), ty); // A missing type (`(a: ) => {}`), which tsc reuses as `any`
    } else {
        ty_display(me, d, ty);
    }
}

// Whether a type may mention one of the type parameters. Conservative: object types that aren't references might
static bool mentions(Checker* me, Ty const* ty, TyList params) {
    if (!params.len) return false;
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (mentions(me, ty->alias_args.data[i], params)) return true;
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
                if (mentions(me, args.data[i], params)) return true;
            }
            return false;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (mentions(me, ty->composite.types.data[i], params)) return true;
            }
            return false;
        case TY_KEYOF:
            return mentions(me, ty->keyof.operand, params);
        case TY_ANONYMOUS:
            // (What the lowering found it can mention, when it knows)
            if (ty->anonymous.free_params_unknown) return true;
            for (u32 i = 0; i < ty->anonymous.free_params.len; i++) {
                if (mentions(me, ty->anonymous.free_params.data[i], params)) return true;
            }
            return false;
        case TY_INDEXED:
            return mentions(me, ty->indexed.object, params) || mentions(me, ty->indexed.index, params);
        default:
            return true;
    }
}

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
// body (or an arrow function's expression body), null otherwise
static Expr const* single_return_expr(FunctionImpl const* function) {
    if (!function->has_body || function->is_generator) return nullptr;
    Expr const* found = nullptr;
    for_each (i, vec_statement, &function->body.statements) {
        if (i->tag == STATEMENT_RETURN_EXPR) {
            if (found || i->expr.tag == EXPR_IMPLICIT_ANY) return nullptr;
            found = &i->expr;
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
    AsTypeExpr const* as_type = expr->as_type;
    if (as_type->is_satisfies || as_type_is_const(as_type) || ty_expr_type(me, expr) != ret) return nullptr;
    return &as_type->type;
}

// A function without a return annotation whose only return is a function expression prints its return type
// syntactically (tsc's typeFromFunctionLikeExpression), unless something may type it contextually (isContextuallyTyped)
static FunctionImpl const* single_return_function(Checker* me, FunctionImpl const* function, Ty const* ret) {
    Expr const* expr = single_return_expr(function);
    if (!expr || expr->tag != EXPR_FUNCTION || expr->function->syntactically_contextual) return nullptr;
    return ty_expr_type(me, expr) == ret ? expr->function : nullptr;
}

static void display_type_params(Checker* me, TyDisplay d, TyList params, bool reusable) {
    if (!params.len) return;
    out_lit(d, "<");
    for (u32 i = 0; i < params.len; i++) {
        if (i) out_lit(d, ", ");
        // The name of a declared type parameter is always reused, the constraint like any annotation (tsc never reuses
        // the default)
        // (A clone made by instantiating the signature has its original's declaration)
        Ty const* declared = params.data[i];
        while (declared->kind == TY_TYPE_PARAM && declared->param.state->clone_of) declared = declared->param.state->clone_of;
        GenericItem const* item = declared->kind == TY_TYPE_PARAM ? declared->param.state->item : nullptr;
        if (item && item->is_const) out_lit(d, "const ");
        if (item && item->is_in) out_lit(d, "in ");
        if (item && item->is_out) out_lit(d, "out ");
        ty_display(me, reused_if(d, d.reused || item), params.data[i]);
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (constraint) {
            out_lit(d, " extends ");
            if (d.reused) ty_display(me, d, constraint);
            else display_annotated(me, d, reusable && item && item->extends_explicit ? &item->extends : nullptr, constraint, false);
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
    // (Unless its own type parameters were instantiated away: a call's candidate with what was inferred)
    bool own_instantiated = root->type_params.len && !sig->type_params.len;
    if (root != sig && !this_only && !class_default && !own_instantiated && !mentions_mapped(me, sig, root)) sig = root;
    bool instantiated = root != sig;
    if (instantiated && root->type_params.len && !this_only) d.fresh_params = root->type_params;
    // (A class's constructor has its parameters reused, but it returns the class)
    FunctionImpl const* function = root->function && root->class_instance && !root->function->is_constructor ? nullptr : root->function;
    // (A JSDoc `@this` makes a parameter of its own, which isn't reused)
    bool jsdoc_this = function && vec_function_arg_size(&function->args) && function->args.data[0].jsdoc_this;
    bool reusable = d.underline && function;
    bool syntactic = function && d.syntactic_function == function; // (Its parameter declarations are reused whole)
    d.syntactic_function = nullptr;
    display_type_params(me, d, sig->type_params, reusable);
    out_lit(d, "(");
    u32 param_count = sig->param_count;
    SigParam const* params = expanded_params(me, sig, &param_count);
    if (!params) params = sig->params;
    if (sig->this_type) {
        out(reused_if(d, d.reused || (function && !jsdoc_this)), c_sv("this"));
        out_lit(d, ": ");
        FunctionArg const* this_decl = function && vec_function_arg_size(&function->args) ? vec_function_arg_at(&function->args, 0) : nullptr;
        Binding const* binding = this_decl ? &this_decl->initializer.binding : nullptr;
        if (binding && !(binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this"))) binding = nullptr;
        bool reused = reusable && !jsdoc_this && binding && binding->has_explicit_type &&
            (!instantiated || (sig->this_type == root->this_type && !mentions(me, sig->this_type, d.fresh_params)));
        // A function expression's `this` from its contextual signature is that signature's parameter (tsc's
        // assignContextualParameterTypes: createSymbolWithType(context.thisParameter)), whose annotation is reused
        Signature const* contextual = function && !binding && reusable ? ty_function_assigned_contextual_signature(me, function) : nullptr;
        while (contextual && contextual->target && !contextual->function) contextual = contextual->target;
        FunctionImpl const* context_function = contextual ? contextual->function : nullptr;
        FunctionArg const* context_this = context_function && vec_function_arg_size(&context_function->args) ?
            vec_function_arg_at(&context_function->args, 0) : nullptr;
        if (context_this && context_this->initializer.binding.tag == BINDING_IDENTIFIER &&
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
        bool contextual_jsdoc = function && function->is_expression && param->decl && span_valid(param->decl->jsdoc_span);
        out(reused_if(d, d.reused || (syntactic && param->decl && !contextual_jsdoc)), c_sv(": "));
        Binding const* binding = param->decl ? &param->decl->initializer.binding : nullptr;
        bool reused = reusable && binding && binding->has_explicit_type && !contextual_jsdoc &&
            (!instantiated || (param->type == root->params[i].type && !mentions(me, param->type, d.fresh_params)));
        TyDisplay param_d = reused_if(d, false);
        // A parameter whose initializer is a function expression gets the function's type syntactically (tsc's
        // typeFromParameter -> typeFromExpression), unless something may type it contextually
        Expr const* initializer = param->decl ? &param->decl->initializer.value : nullptr;
        if (reusable && binding && !binding->has_explicit_type && binding->tag == BINDING_IDENTIFIER && initializer &&
            initializer->tag == EXPR_FUNCTION && !initializer->function->syntactically_contextual &&
            ty_without_undefined(me, param->type) == ty_expr_type(me, initializer)
        ) {
            param_d.syntactic_function = initializer->function;
        }
        display_annotated(me, param_d, reused ? &binding->explicit_type : nullptr, param->type, param->optional);
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TyPredicate const* predicate = signature_predicate(me, sig);
    Ty const* ret = signature_return_type(me, sig);
    TypeNode const* ret_annotation = !reusable || root->class_instance ? nullptr : function->ret_explicit ? &function->ret :
        function->jsdoc_tag_ret ? function->jsdoc_tag_ret : single_return_assertion(me, function, signature_return_type(me, root));
    bool reused = ret_annotation &&
        (!instantiated || (ret == signature_return_type(me, root) && !mentions(me, ret, d.fresh_params)));
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
            if (predicate_d.reused && function->predicate->has_type && d.underline && d.enclosing) {
                predicate_d.mapped_by = instantiated && !this_only ? sig : nullptr;
                display_node(me, predicate_d, &function->predicate->type);
            } else {
                ty_display(me, reused_if(predicate_d, false), predicate->type);
            }
        }
        return;
    }
    TyDisplay ret_d = reused_if(d, false);
    if (reusable && !ret_annotation && !function->ret_explicit && !root->class_instance) ret_d.syntactic_function = single_return_function(me, function, ret);
    display_annotated(me, ret_d, reused ? ret_annotation : nullptr, ret, false);
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
        cstr_append_sv(out, c_sv(" }"));
    } else {
        cstr_append_sv(out, binding->content_span.sv);
    }
}

static void display_mapped(Checker* me, TyDisplay d, MappedInfo const* mapped) {
    out_lit(d, "{ ");
    if (mapped->readonly) out(d, mapped->readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
    out_lit(d, "[");
    ty_display(me, reused_if(d, true), mapped->type_param); // (tsc reuses the name of the declared type parameter)
    out_lit(d, " in ");
    ty_display(me, d, mapped->constraint);
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
        Symbol const* found = scope_resolve(d.enclosing, leftmost->identifier.name.sv, SYMBOL_VALUE);
        if (!found || !same_symbol(me, found, leftmost->identifier.symbol)) return false;
    }
    return true;
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty) {
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
    for (u32 i = 0; i < members->call_count; i++) {
        display_signature(me, d, members->call[i], ": ", false);
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
                if (info->readonly) out_lit(reused, "readonly ");
                out_lit(reused, "[");
                out(reused, component->name->outer_span.sv);
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
        ty_display(me, d, info->value);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->prop_count; i++) {
        Prop const* prop = members->props[i];
        Ty const* type = prop_type(me, prop);
        Members const* type_members = (prop->flags & PROP_METHOD) ? ty_members(me, type) : nullptr;
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
        if (prop->flags & PROP_READONLY) out_lit(d, "readonly ");
        display_member_name(me, d, prop);
        if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
        out_lit(d, ": ");
        // The annotation of a property signature unless instantiated, or the type assertion of an object literal's
        // The annotation of the property it was declared as (an instantiation too: display_annotated() checks that it
        // still says the same), or the type assertion of an object literal's
        Prop const* origin = prop;
        while (origin->target) origin = origin->target;
        TypeNode const* annotation = prop->decl->asserted;
        if (origin->decl->node && origin->decl->node->parsed && !origin->decl->lazy && !origin->decl->function && !origin->decl->overload_count &&
            !origin->decl->symbol
        ) {
            annotation = origin->decl->node;
        }
        // An object literal's accessor: the getter's return annotation, else the setter's parameter's (tsc's
        // typeFromAccessor)
        if ((origin->flags & (PROP_GETTER | PROP_SETTER)) && origin->decl->function) {
            FunctionImpl const* accessor = origin->decl->function;
            Binding const* param = vec_function_arg_size(&accessor->args) ?
                &vec_function_arg_at(&accessor->args, 0)->initializer.binding : nullptr;
            if ((origin->flags & PROP_GETTER) && accessor->ret_explicit) annotation = &accessor->ret;
            else if (!(origin->flags & PROP_GETTER) && param && param->has_explicit_type) annotation = &param->explicit_type;
        }
        // (Instantiated in a signature that has fresh type parameters in tsc, not tttt: see TyDisplay.fresh_params)
        if (origin != prop && mentions(me, type, d.fresh_params)) annotation = nullptr;
        // (Printed, not reused: tsc doesn't reuse the `unique symbol` annotation that declares the symbol)
        // An optional property's type has undefined under strictNullChecks, unless exactOptionalPropertyTypes (tsc's
        // addOptionality in getTypeOfSymbol); a reused annotation doesn't say it
        Ty const* shown = type;
        if ((prop->flags & PROP_OPTIONAL) && compiler_options()->strict_null_checks &&
            !compiler_options()->exact_optional_property_types && ty_without_undefined(me, type) == type
        ) {
            shown = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
        }
        bool optional = (prop->flags & PROP_OPTIONAL) != 0;
        if (d.reused && annotation && annotation->tag != TYPE_UNIQUE_SYMBOL) ty_display(me, d, shown);
        else if (annotation && d.underline && d.enclosing && annotation_is(me, annotation, type, optional)) display_node(me, d, annotation);
        else display_annotated(me, d, annotation, shown, optional);
        out_lit(d, "; ");
    }
    out_lit(d, "}");
}

static bool is_function_like(Checker* me, TyDisplay d, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || shows_alias(d, ty)) return false;
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
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || ty->kind == TY_CONDITIONAL || ty->kind == TY_KEYOF ||
        type_query || readonly || is_function_like(me, d, ty);
}

static void display_tuple(Checker* me, TyDisplay d, TupleShape const* shape, TyList args) {
    if (shape->readonly) out_lit(d, "readonly ");
    out_lit(d, "[");
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
            ty_display(me, d, element);
        } else {
            display_parenthesized_if(me, d, element, optional && needs_parens_in_array(me, d, element));
            if (optional) out_lit(d, "?");
        }
    }
    out_lit(d, "]");
}

static void display_union(Checker* me, TyDisplay d, Ty const* ty) {
    // tsc's formatUnionTypes(): `true | false` prints as `boolean`, `null` and `undefined` go last
    TyList types = ty->composite.types;
    bool first = true;
    u32 nullable = 0;
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
        bool parens = is_function_like(me, d, t) || (!shows_alias(d, t) && (t->kind == TY_CONDITIONAL || t->kind == TY_INTERSECTION ||
            (t->kind == TY_UNION && t->composite.origin && t->composite.origin->kind == TY_INTERSECTION)));
        display_parenthesized_if(me, d, t, parens);
    }
    if (nullable & TYF_NULL) {
        out(d, first ? c_sv("null") : c_sv(" | null"));
        first = false;
    }
    if (nullable & TYF_UNDEFINED) {
        out(d, first ? c_sv("undefined") : c_sv(" | undefined"));
    }
}

void ty_display(Checker* me, TyDisplay d, Ty const* ty) {
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
                // The types baseline says `unique symbol` (tsc's AllowUniqueESSymbolType), messages name the symbol, and
                // so does an annotation that is reused (it says `typeof sym`: the one that says `unique symbol` declares
                // the symbol, and tsc doesn't reuse that one)
                if (d.unique_symbols && !d.reused) {
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
                if (member && !is_identifier_name(member_name)) {
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
                    out(d, member_name.size && member_name.buf[0] == '#' ? c_sv("__missing") : member_name);
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
            bool declared_here = false;
            for (u32 i = 0; i < d.infer_params.len; i++) declared_here |= d.infer_params.data[i] == ty;
            if (declared_here) out_lit(d, "infer ");
            out_type_param_name(me, d, ty, false);
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
                    if (!ty->declared.name->len || is_mixin_static_side(me, ty)) {
                        display_structure(me, d, ty); // A module nobody named
                        break;
                    }
                    out_lit(d, "typeof ");
                    out_declared_name(me, d, ty, SYMBOL_VALUE);
                    break;

                case TY_DECLARED_TUPLE_TARGET:
                    display_tuple(me, d, ty->declared.tuple, ty->declared.type_params);
                    break;

                default:
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
            // An anonymous type inside its own rendering prints as `any`, like tsc's elided information placeholder
            // (`function f() { return f }` is `() => any`)
            for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
                if (i->ty == ty) {
                    out_lit(d, "any");
                    return;
                }
            }
            // A JavaScript constructor function is a class
            Symbol const* js_class = ty->anonymous.function && !ty->anonymous.target ?
                ty_js_constructor_class(ty->anonymous.function) : nullptr;
            if (js_class) {
                out_lit(d, "typeof ");
                csview js_name;
                out(d, ty_js_assigned_class_name(js_class, &js_name) ? js_name : js_class->name);
                break;
            }
            // A function a CommonJS module exports with the module's other exports merged in (tsc's
            // getCommonJsExportEquals clone, a ValueModule too)
            Symbol const* exported = ty->anonymous.merged_value ? ty->anonymous.merged_value->merged_export_equals_of : nullptr;
            if (exported && (exported->flags & SYMBOL_FUNCTION) && span_valid(exported->name_span)) {
                out_lit(d, "typeof ");
                out(d, exported->name_span.sv);
                break;
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

        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                Ty const* t = ty->composite.types.data[i];
                if (i) out_lit(d, " & ");
                display_parenthesized_if(me, d, t, !shows_alias(d, t) && (t->kind == TY_UNION || t->kind == TY_CONDITIONAL ||
                    is_function_like(me, d, t) || displays_as_intersection(me, d, t)));
            }
            break;

        case TY_KEYOF:
            out_lit(d, "keyof ");
            display_parenthesized_if(me, d, ty->keyof.operand, needs_parens_in_array(me, d, ty->keyof.operand));
            break;

        case TY_INDEXED:
            display_parenthesized_if(me, d, ty->indexed.object, needs_parens_in_array(me, d, ty->indexed.object));
            out_lit(d, "[");
            ty_display(me, d, ty->indexed.index);
            out_lit(d, "]");
            break;

        case TY_SUBSTITUTION: impossible;

        case TY_CONDITIONAL:
            display_parenthesized_if(
                me, d, ty->conditional.check,
                is_function_like(me, d, ty->conditional.check) || ty->conditional.check->kind == TY_CONDITIONAL
            );
            out_lit(d, " extends ");
            TyDisplay extends_d = d;
            extends_d.infer_params = ty_conditional_own_infers(me, ty->conditional.root);
            display_parenthesized_if(me, extends_d, ty->conditional.extends, ty->conditional.extends->kind == TY_CONDITIONAL);
            out_lit(d, " ? ");
            ty_display(me, d, ty_conditional_branch(me, ty, true));
            out_lit(d, " : ");
            ty_display(me, d, ty_conditional_branch(me, ty, false));
            break;

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
