#include "ty/ty_internal.h"
#include "scope.h"
#include "program.h"
#include "function_impl.h"
#include "several_types.h"
#include "interface_member.h"
#include "statement.h"

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

static void display_list(Checker* me, TyDisplay d, TyList list) {
    for (u32 i = 0; i < list.len; i++) {
        if (i) out_lit(d, ", ");
        ty_display(me, d, list.data[i]);
    }
}

// Symbol accessibility, a simplified getAccessibleSymbolChain(): how to name a symbol from a scope

static bool same_symbol(Checker* me, Symbol const* a, Symbol const* b) {
    Binder const* binder = program_get_binder(my program);
    return binder_redirect(binder, a) == binder_redirect(binder, b);
}

// An alias among the entries of a table that names the symbol
static bool find_alias_in_table(Checker* me, SymbolTable const* table, Symbol const* symbol, u32 meaning, csview* name) {
    if (!table) return false;
    for (u32 i = 0; i < table->count; i++) {
        Symbol const* candidate = table->entries[i].symbol;
        if (!(candidate->flags & SYMBOL_ALIAS) || candidate->alias->state != ALIAS_DONE) continue;
        Symbol const* target = symbol_for_meaning(candidate, meaning);
        if (target && same_symbol(me, target, symbol)) {
            *name = table->entries[i].name;
            return true;
        }
    }
    return false;
}

static void out_symbol_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    Symbol const* parent = symbol->parent;
    if (!parent || symbol->is_global_this) {
        out(d, symbol->name_span.sv);
        return;
    }
    bool quoted_parent = parent->name.size && (*parent->name.buf == '"' || *parent->name.buf == '\'');
    if (!d.enclosing) {
        // Modules and ambient modules (`declare module "m"`) are containers too, but not part of a name
        if (!parent->is_module && !quoted_parent) {
            out_symbol_name(me, d, parent, SYMBOL_NAMESPACE);
            out_lit(d, ".");
        }
        out(d, symbol->name_span.sv);
        return;
    }
    // Visible under its own name?
    Symbol const* found = scope_resolve(d.enclosing, symbol->name, meaning);
    if (found && same_symbol(me, symbol_for_meaning(found, meaning), symbol)) {
        out(d, symbol->name_span.sv);
        return;
    }
    // Under an import's? The globals aren't searched: nothing but `export as namespace` puts an alias there
    for (Scope const* scope = d.enclosing; scope && scope->kind != SCOPE_GLOBAL; scope = scope->parent) {
        csview alias_name;
        if (find_alias_in_table(me, &scope->locals, symbol, meaning, &alias_name) ||
            (scope->container && find_alias_in_table(me, scope->container->exports, symbol, meaning, &alias_name))
        ) {
            out(d, alias_name);
            return;
        }
    }
    if (parent->is_module) {
        // Of another module that nothing here names: `import("a").T`
        Scope const* file_scope = d.enclosing;
        while (file_scope->parent && file_scope->kind != SCOPE_MODULE && file_scope->kind != SCOPE_SCRIPT) {
            file_scope = file_scope->parent;
        }
        if (file_scope->container != parent) {
            cstr module_name = module_name_for_error(parent, nullptr, false);
            out_lit(d, "import(");
            out(d, cstr_sv(&module_name));
            out_lit(d, ").");
            cstr_drop(&module_name);
        }
    } else if (!quoted_parent) {
        out_symbol_name(me, d, parent, SYMBOL_NAMESPACE);
        out_lit(d, ".");
    }
    out(d, symbol->name_span.sv);
}

// The name of a declared type: by its symbol when it has one
static void out_declared_name(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol) symbol = declared->declared.merged_value;
    if (!symbol && declared->declared.kind == TY_DECLARED_NAMESPACE) symbol = declared->declared.namespace_symbol;
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
    if (ty_unique_symbol_of_name(me, name)) {
        display_prop_name(me, (TyDisplay){ .target = &ret }, name);
    } else {
        cstr_append_sv(&ret, atom_sv(name));
    }
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

// Node reuse, for the underlines of the types baseline (see TyDisplay.underline). tsc prints a type for a declaration
// by reusing the declaration's own annotation where it can (serializeTypeForDeclaration -> tryReuseExistingTypeNode).
// We print the Ty as usual and only mark those parts as reused. HACK: tsc re-prints the annotation itself, so where
// the Ty prints differently from what was written (`Array<string>`) the text is wrong too (hacks.md)

// Whether every name in an annotation still means the same thing from where the type is printed (tsc bails out of
// reusing a node whose names aren't accessible there, `q: Q` printed outside of namespace N is `q: N.Q`)
static bool annotation_accessible(Checker* me, TyDisplay d, TypeNode const* node);

static bool function_annotations_accessible(Checker* me, TyDisplay d, FunctionImpl const* function) {
    for_each (i, vec_generic_item, &function->generic.items) {
        if (i->extends_explicit && !annotation_accessible(me, d, &i->extends)) return false;
    }
    for_each (i, vec_function_arg, &function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->has_explicit_type && !annotation_accessible(me, d, &binding->explicit_type)) return false;
    }
    return !function->ret_explicit || annotation_accessible(me, d, &function->ret);
}

static bool annotation_accessible(Checker* me, TyDisplay d, TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                if (!annotation_accessible(me, d, &i->type)) return false;
            }
            if (identifier->keyword || identifier->import_ref) return true;
            bool qualified = vec_span_size(&identifier->namespaces) > 0;
            Symbol const* symbol = identifier->symbol;
            if (qualified) {
                symbol = vec_symbol_cptr_size(&identifier->namespace_symbols) ?
                    *vec_symbol_cptr_at(&identifier->namespace_symbols, 0) : nullptr;
            }
            // Unresolved names are an error type, and the type parameters of what is printed are in scope for it
            if (!symbol || (symbol->flags & SYMBOL_TYPE_PARAMETER)) return true;
            Span first = qualified ? *vec_span_at(&identifier->namespaces, 0) : identifier->name;
            Symbol const* found = scope_resolve(d.enclosing, first.sv, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE);
            return found && same_symbol(me, found, symbol);
        }
        case TYPE_CALLABLE:
            return function_annotations_accessible(me, d, node->callable->function);
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                if (i->key.tag == INTERFACE_KEY_INDEX && !annotation_accessible(me, d, &i->key.index.type)) return false;
                if (i->key.tag == INTERFACE_KEY_MAPPED) {
                    if (!annotation_accessible(me, d, &i->key.mapped.in_type)) return false;
                    if (i->key.mapped.has_as_type && !annotation_accessible(me, d, &i->key.mapped.as_type)) return false;
                }
                if (!annotation_accessible(me, d, &i->type)) return false;
            }
            return true;
        case TYPE_ARRAY:
            return annotation_accessible(me, d, node->array.element);
        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (!annotation_accessible(me, d, &i->type)) return false;
            }
            return true;
        case TYPE_INDEXED:
            return annotation_accessible(me, d, &node->indexed->interface) &&
                annotation_accessible(me, d, &node->indexed->index);
        case TYPE_KEYOF:
            return annotation_accessible(me, d, &node->keyof->interface);
        case TYPE_TERNARY:
            return annotation_accessible(me, d, &node->ternary->maybe_sub) &&
                annotation_accessible(me, d, &node->ternary->maybe_super) &&
                annotation_accessible(me, d, &node->ternary->if_true) &&
                annotation_accessible(me, d, &node->ternary->if_false);
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (!annotation_accessible(me, d, i)) return false;
            }
            return true;
        case TYPE_INFERRED:
            return !node->inferred->extends.parsed || annotation_accessible(me, d, &node->inferred->extends);
        default:
            return true; // Keywords, literals, `typeof x` (TODO: the entity name of a typeof)
    }
}

// Whether to print a declaration's annotation as reused. Only when underlining: `enclosing` is what accessibility is
// judged from
static bool reuses(Checker* me, TyDisplay d, TypeNode const* annotation) {
    return d.underline && d.enclosing && annotation_accessible(me, d, annotation);
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
        case TY_INDEXED:
            return mentions(me, ty->indexed.object, params) || mentions(me, ty->indexed.index, params);
        default:
            return true;
    }
}

// tsc's typeFromSingleReturnExpression(): a function without a return annotation whose body is just `return x as T`
// (or an arrow `=> <T>x`) prints its return type by reusing `T`. HACK: only a body of that one statement, tsc allows
// other statements before it
static TypeNode const* single_return_assertion(Checker* me, FunctionImpl const* function, Ty const* ret) {
    if (!function->has_body || function->is_generator || vec_statement_size(&function->body.statements) != 1) return nullptr;
    Statement const* statement = vec_statement_at(&function->body.statements, 0);
    if (statement->tag != STATEMENT_RETURN_EXPR || statement->expr.tag != EXPR_AS_TYPE) return nullptr;
    AsTypeExpr const* as_type = statement->expr.as_type;
    if (as_type->is_satisfies || as_type_is_const(as_type) || ty_expr_type(me, &statement->expr) != ret) return nullptr;
    return &as_type->type;
}

static TyDisplay reused_if(TyDisplay d, bool reused) {
    d.reused = reused;
    return d;
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
            bool reused = d.reused || (reusable && item && item->extends_explicit && reuses(me, d, &item->extends));
            ty_display(me, reused_if(d, reused), constraint);
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

static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
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
    bool instantiated = root != sig;
    if (instantiated && root->type_params.len && !this_only) d.fresh_params = root->type_params;
    FunctionImpl const* function = root->class_instance ? nullptr : root->function;
    bool reusable = d.underline && function;
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
            (!instantiated || (sig->this_type == root->this_type && !mentions(me, sig->this_type, d.fresh_params))) &&
            reuses(me, d, &binding->explicit_type);
        ty_display(me, reused_if(d, reused), sig->this_type);
        if (param_count) out_lit(d, ", ");
    }
    for (u32 i = 0; i < param_count; i++) {
        SigParam const* param = &params[i];
        if (i) out_lit(d, ", ");
        if (param->rest) out_lit(d, "...");
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
        out_lit(d, ": ");
        Binding const* binding = param->decl ? &param->decl->initializer.binding : nullptr;
        bool reused = reusable && binding && binding->has_explicit_type &&
            (!instantiated || (param->type == root->params[i].type && !mentions(me, param->type, d.fresh_params))) &&
            reuses(me, d, &binding->explicit_type);
        ty_display(me, reused_if(d, reused), param->type);
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TyPredicate const* predicate = signature_predicate(me, sig);
    Ty const* ret = signature_return_type(me, sig);
    TypeNode const* ret_annotation = !reusable ? nullptr : function->ret_explicit ? &function->ret :
        single_return_assertion(me, function, signature_return_type(me, root));
    bool reused = ret_annotation &&
        (!instantiated || (ret == signature_return_type(me, root) && !mentions(me, ret, d.fresh_params))) &&
        reuses(me, d, ret_annotation);
    if (predicate) {
        ty_display_predicate(me, reused_if(d, reusable && function->predicate), predicate);
        return;
    }
    ty_display(me, reused_if(d, reused), ret);
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
    ty_display(me, d, mapped->type_param);
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
        TypeNode const* annotation = prop->asserted;
        if (prop->node && prop->node->parsed && !prop->target && !prop->lazy && !prop->function && !prop->overload_count &&
            !prop->symbol
        ) {
            annotation = prop->node;
        }
        TyDisplay prop_d = reused_if(d, annotation && (d.reused || reuses(me, d, annotation)));
        // Printed, not reused: tsc doesn't reuse the `unique symbol` annotation that declares the symbol
        if (annotation && annotation->tag == TYPE_UNIQUE_SYMBOL) prop_d.reused = d.reused;
        ty_display(me, prop_d, type);
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
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || ty->kind == TY_CONDITIONAL || ty->kind == TY_KEYOF ||
        is_function_like(me, ty);
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
                out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_TYPE);
                if (ty->literal.enum_member != ty->literal.enum_decl && ty_enum_of_member(me, ty) != ty) {
                    out_lit(d, ".");
                    out(d, ty->literal.enum_member->name_span.sv);
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
            out(d, atom_sv(ty->param.name));
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
