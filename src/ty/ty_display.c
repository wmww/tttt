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

static bool is_function_like(Checker* me, Ty const* ty);

// Type arguments. The first one is parenthesized if it's a generic function type (tsc's parenthesizeTypeArguments:
// `X<<T>() => T>` would start with `<<`)
static void display_list(Checker* me, TyDisplay d, TyList list) {
    for (u32 i = 0; i < list.len; i++) {
        if (i) out_lit(d, ", ");
        Ty const* arg = list.data[i];
        bool parens = false;
        if (!i && is_function_like(me, arg)) {
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

// With TyDisplay.enclosing, qualified as far as needed from there (symbol_chain.h), else fully (error messages)
static void out_symbol_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    Symbol const* parent = symbol->parent;
    if ((!parent && !d.enclosing) || symbol->is_global_this) {
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
        start = 1;
    }
    cstr text = cstr_init();
    SymbolChain rest = { .len = chain.len - start };
    memcpy(rest.items, chain.items + start, rest.len * sizeof(*rest.items));
    if (start && rest.len) cstr_append_lit(&text, ".");
    symbol_chain_append(&text, d.enclosing, &rest);
    out(d, cstr_sv(&text));
    cstr_drop(&text);
}

// The name of a declared type: by its symbol when it has one
static void out_declared_name(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol) symbol = declared->declared.merged_value;
    if (!symbol && declared->declared.kind == TY_DECLARED_NAMESPACE) symbol = declared->declared.namespace_symbol;
    // A module's object, also through `import * as ns` (whose symbol stands for the module): named from here like tsc
    if (d.enclosing && symbol && symbol_members(symbol)->is_module) {
        out_symbol_name(me, d, symbol_members(symbol), meaning);
        return;
    }
    if (symbol && !symbol->is_module && span_valid(symbol->name_span)) {
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

static bool is_numeric_name(csview sv) {
    if (!sv.size) return false;
    for (isize_t i = 0; i < sv.size; i++) {
        if (sv.buf[i] < '0' || sv.buf[i] > '9') return false;
    }
    return sv.size == 1 || sv.buf[0] != '0';
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
static void display_node_fallback(Checker* me, TyDisplay d, TypeNode const* node) {
    ty_display(me, reused_if(d, false), node_type(me, d, node));
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

static void display_node_identifier(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->keyword) {
        out(d, identifier->name.sv);
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
        if (i->rest) out_lit(d, "...");
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
            cstr pattern = cstr_init();
            append_binding_pattern(&pattern, binding);
            out(d, cstr_sv(&pattern));
            cstr_drop(&pattern);
        } else {
            out(d, binding->content_span.sv);
        }
        if (i->explicit_optional) out_lit(d, "?");
        out_lit(d, ": ");
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

static void display_node_interface(Checker* me, TyDisplay d, Interface const* interface) {
    bool empty = true;
    for_each (i, vec_interface_member, &interface->members) empty &= !member_printed(me, i);
    if (empty) {
        out_lit(d, "{}");
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
    for_each (i, vec_type, list) {
        if (i->placeholder) continue;
        if (!first) out(d, csview_from(separator));
        first = false;
        display_node(me, d, i);
    }
}

// Prints an annotation as written, as reused
static void display_node(Checker* me, TyDisplay d, TypeNode const* node) {
    d.reused = true;
    if (node->parenthesized) out_lit(d, "(");
    switch (node->tag) {
        case TYPE_IDENTIFIER:
            display_node_identifier(me, d, node);
            break;
        case TYPE_TYPEOF:
            display_node_typeof(me, d, node);
            break;
        case TYPE_CALLABLE:
            display_node_signature(me, d, node->callable->function, " => ");
            break;
        case TYPE_INTERFACE:
            display_node_interface(me, d, &node->interface);
            break;
        case TYPE_ARRAY:
            if (node->array.readonly) out_lit(d, "readonly ");
            display_node(me, d, node->array.element);
            out_lit(d, "[]");
            break;
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

static Atom const* numbered_name(Checker* me, csview base, char const* suffix_format, u32 n) {
    char buf[256];
    int len = snprintf(buf, sizeof buf, "%.*s", (int)base.size, base.buf);
    if (len < 0 || len >= (int)sizeof buf) len = 0;
    int more = snprintf(buf + len, sizeof buf - (size_t)len, suffix_format, n);
    if (more > 0) len += more;
    if (len >= (int)sizeof buf) len = (int)sizeof buf - 1;
    return checker_atom(me, c_sv(buf, (isize_t)len));
}

// What tsc names an unlabeled element of a rest tuple (getTupleElementLabel): after the rest parameter
static Atom const* rest_element_label(Checker* me, SigParam const* rest, u32 index, unsigned char flags) {
    Binding const* binding = rest->decl ? &rest->decl->initializer.binding : nullptr;
    if (binding && binding->tag == BINDING_IDENTIFIER) {
        return flags & TUPLE_ELEMENT_REST ? rest->name : numbered_name(me, atom_sv(rest->name), "_%u", index);
    }
    if (binding) return numbered_name(me, c_sv("arg"), "_%u", index); // TODO: names out of an array pattern
    return numbered_name(me, atom_sv(rest->name), "_%u", index);
}

// tsc prints a rest parameter of tuple type as the parameters it stands for (getExpandedParameters), unless a rest
// element comes before the end. Null when there's nothing to expand
static SigParam const* expanded_params(Checker* me, Signature const* sig, u32* count_out) {
    if (!sig->param_count || !sig->params[sig->param_count - 1].rest) return nullptr;
    SigParam const* rest = &sig->params[sig->param_count - 1];
    if (!ty_is_tuple(rest->type)) return nullptr;
    TupleShape const* shape = rest->type->reference.target->declared.tuple;
    for (u32 i = 0; i + 1 < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return nullptr;
    }
    TyList elements = ty_reference_args(me, rest->type);
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
        if (sig->flags & SIGNATURE_ABSTRACT) out_lit(d, "abstract ");
        out_lit(d, "new ");
    }
    // tsc reuses the parameter names of any signature with declarations, and the annotations of its declaration where
    // they still say what is printed: an instantiated signature (members of `I<number>`) reuses what the mapper left
    // alone and doesn't mention its own type parameters (fresh ones in tsc). Inside a reused annotation everything is
    // reused but what has no annotation (`(x) => void` prints its `any` synthetically)
    Signature const* root = sig;
    bool this_only = true; // Instantiated only with what `this` is (every method of a class): no fresh type parameters
    for (; root->target; root = root->target) {
        for (u32 i = 0; root->mapper && i < root->mapper->len; i++) {
            Ty const* source = root->mapper->sources[i];
            this_only &= source->kind == TY_TYPE_PARAM && source->param.state->is_this;
        }
    }
    signature_resolve(me, root);
    if (root != sig && !this_only && !mentions_mapped(me, sig, root)) sig = root;
    bool instantiated = root != sig;
    if (instantiated && root->type_params.len && !this_only) d.fresh_params = root->type_params;
    FunctionImpl const* function = root->class_instance ? nullptr : root->function;
    bool reusable = d.underline && function;
    bool syntactic = function && d.syntactic_function == function; // (Its parameter declarations are reused whole)
    d.syntactic_function = nullptr;
    display_type_params(me, d, sig->type_params, reusable);
    out_lit(d, "(");
    u32 param_count = sig->param_count;
    SigParam const* params = expanded_params(me, sig, &param_count);
    if (!params) params = sig->params;
    if (sig->this_type) {
        out(reused_if(d, d.reused || function), c_sv("this"));
        out_lit(d, ": ");
        FunctionArg const* this_decl = function ? vec_function_arg_at(&function->args, 0) : nullptr;
        Binding const* binding = this_decl ? &this_decl->initializer.binding : nullptr;
        bool reused = reusable && binding && binding->has_explicit_type &&
            (!instantiated || (sig->this_type == root->this_type && !mentions(me, sig->this_type, d.fresh_params)));
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
            out(reused_if(d, d.reused || param->decl), atom_sv(param->name));
        }
        if (param->optional) out_lit(d, "?");
        out(reused_if(d, d.reused || (syntactic && param->decl)), c_sv(": "));
        Binding const* binding = param->decl ? &param->decl->initializer.binding : nullptr;
        bool reused = reusable && binding && binding->has_explicit_type &&
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
    TypeNode const* ret_annotation = !reusable ? nullptr : function->ret_explicit ? &function->ret :
        single_return_assertion(me, function, signature_return_type(me, root));
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
    if (reusable && !ret_annotation && !function->ret_explicit) ret_d.syntactic_function = single_return_function(me, function, ret);
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
    ty_display(me, d, mapped->template);
    out_lit(d, "; }");
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
    out_lit(d, "{ ");
    for (u32 i = 0; i < members->call_count; i++) {
        display_signature(me, d, members->call[i], ": ", false);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->construct_count; i++) {
        display_signature(me, d, members->construct[i], ": ", true);
        out_lit(d, "; ");
    }
    for (u32 i = 0; i < members->index_count; i++) {
        IndexInfo const* info = &members->index[i];
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
                display_prop_name(me, d, prop->name);
                if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
                display_signature(me, d, type_members->call[j], ": ", false);
                out_lit(d, "; ");
            }
            continue;
        }
        if (prop->flags & PROP_READONLY) out_lit(d, "readonly ");
        display_prop_name(me, d, prop->name);
        if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
        out_lit(d, ": ");
        // The annotation of a property signature unless instantiated, or the type assertion of an object literal's
        // The annotation of the property it was declared as (an instantiation too: display_annotated() checks that it
        // still says the same), or the type assertion of an object literal's
        Prop const* origin = prop;
        while (origin->target) origin = origin->target;
        TypeNode const* annotation = prop->asserted;
        if (origin->node && origin->node->parsed && !origin->lazy && !origin->function && !origin->overload_count &&
            !origin->symbol
        ) {
            annotation = origin->node;
        }
        // An object literal's accessor: the getter's return annotation, else the setter's parameter's (tsc's
        // typeFromAccessor)
        if ((origin->flags & (PROP_GETTER | PROP_SETTER)) && origin->function) {
            FunctionImpl const* accessor = origin->function;
            Binding const* param = vec_function_arg_size(&accessor->args) ?
                &vec_function_arg_at(&accessor->args, 0)->initializer.binding : nullptr;
            if ((origin->flags & PROP_GETTER) && accessor->ret_explicit) annotation = &accessor->ret;
            else if (!(origin->flags & PROP_GETTER) && param && param->has_explicit_type) annotation = &param->explicit_type;
        }
        // (Instantiated in a signature that has fresh type parameters in tsc, not tttt: see TyDisplay.fresh_params)
        if (origin != prop && mentions(me, type, d.fresh_params)) annotation = nullptr;
        // (Printed, not reused: tsc doesn't reuse the `unique symbol` annotation that declares the symbol)
        if (d.reused && annotation && annotation->tag != TYPE_UNIQUE_SYMBOL) ty_display(me, d, type);
        else display_annotated(me, d, annotation, type, (prop->flags & PROP_OPTIONAL) != 0);
        out_lit(d, "; ");
    }
    out_lit(d, "}");
}

static bool is_function_like(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || ty->alias) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->index_count && !members->mapped &&
        members->call_count + members->construct_count == 1;
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

static bool needs_parens_in_array(Checker* me, Ty const* ty) {
    if (ty->alias) return false;
    if ((ty->flags & TYF_BOOLEAN) || is_whole_enum(me, ty)) return false;
    // `(typeof C)[]`: a type query is parenthesized too
    bool type_query = ty->kind == TY_DECLARED &&
        (ty->declared.kind == TY_DECLARED_CLASS_STATIC || ty->declared.kind == TY_DECLARED_ENUM_OBJECT ||
            ty->declared.kind == TY_DECLARED_NAMESPACE) && ty->declared.name && ty->declared.name->len;
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || ty->kind == TY_CONDITIONAL || ty->kind == TY_KEYOF ||
        type_query || is_function_like(me, ty);
}

static void display_tuple(Checker* me, TyDisplay d, TupleShape const* shape, TyList args) {
    if (shape->readonly) out_lit(d, "readonly ");
    out_lit(d, "[");
    for (u32 i = 0; i < shape->arity; i++) {
        if (i) out_lit(d, ", ");
        bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
        bool optional = shape->flags[i] & TUPLE_ELEMENT_OPTIONAL;
        if (rest) out_lit(d, "...");
        if (shape->names && shape->names[i]) {
            out(d, atom_sv(shape->names[i]));
            if (optional) out_lit(d, "?");
            out_lit(d, ": ");
            ty_display(me, d, args.data[i]);
        } else {
            display_parenthesized_if(me, d, args.data[i], optional && needs_parens_in_array(me, args.data[i]));
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
        bool parens = is_function_like(me, t) || (!t->alias && (t->kind == TY_CONDITIONAL || t->kind == TY_INTERSECTION));
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
    d.depth++;
    d.mapped_by = nullptr; // (A type is printed as it is, annotations of other declarations inside it aren't instantiated)
    if (d.depth > MAX_DISPLAY_DEPTH * 4) {
        out_lit(d, "...");
        return;
    }

    // Widening only reaches through the structure of expression types (temporaries lowered from the legacy checker)
    if (d.widen_literals && !(ty->kind == TY_LITERAL || ty->kind == TY_UNION ||
        (ty->kind == TY_ANONYMOUS && !ty->anonymous.node && !ty->anonymous.overload_count && !ty->anonymous.target))
    ) {
        d.widen_literals = false;
    }

    if (ty_is_enum_union(ty)) {
        out_symbol_name(me, d, ty->alias, SYMBOL_TYPE);
        return;
    }
    if (ty->alias) {
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
                    out(d, member_name);
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
                    if (!ty->declared.name->len) {
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
                    if (ty->declared.type_params.len) {
                        out_lit(d, "<");
                        display_list(me, d, ty->declared.type_params);
                        out_lit(d, ">");
                    }
                    break;
            }
            break;

        case TY_REFERENCE: {
            Ty const* target = ty->reference.target;
            TyList args = ty_reference_args(me, ty);
            if (target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                display_tuple(me, d, target->declared.tuple, args);
            } else if (target == my array_target || target == my readonly_array_target) {
                if (target == my readonly_array_target) out_lit(d, "readonly ");
                display_parenthesized_if(me, d, args.data[0], needs_parens_in_array(me, args.data[0]));
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
            TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
            d.visiting = &visit;
            display_structure(me, d, ty);
        }   break;

        case TY_UNION:
            display_union(me, d, ty);
            break;

        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                Ty const* t = ty->composite.types.data[i];
                if (i) out_lit(d, " & ");
                display_parenthesized_if(me, d, t, !t->alias && (t->kind == TY_UNION || t->kind == TY_CONDITIONAL || is_function_like(me, t)));
            }
            break;

        case TY_KEYOF:
            out_lit(d, "keyof ");
            display_parenthesized_if(me, d, ty->keyof.operand, needs_parens_in_array(me, ty->keyof.operand));
            break;

        case TY_INDEXED:
            display_parenthesized_if(me, d, ty->indexed.object, needs_parens_in_array(me, ty->indexed.object));
            out_lit(d, "[");
            ty_display(me, d, ty->indexed.index);
            out_lit(d, "]");
            break;

        case TY_CONDITIONAL:
            display_parenthesized_if(
                me, d, ty->conditional.check,
                is_function_like(me, ty->conditional.check) || ty->conditional.check->kind == TY_CONDITIONAL
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
    }
}

cstr ty_to_cstr(Checker* me, Ty const* ty) {
    cstr ret = cstr_null;
    ty_display(me, (TyDisplay){ .target = &ret }, ty);
    return ret;
}
