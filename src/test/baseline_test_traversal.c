#include "baseline_test_traversal.h"
#include "baseline_test_case.h"
#include "type.h"
#include "interface_member.h"
#include "generic.h"
#include "expr.h"
#include "several_types.h"
#include "function_impl.h"
#include "port_statement.h"
#include "statement.h"
#include "block.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "function_impl.h"
#include "callable_type.h"
#include "ty/ty.h"
#include "ty/ty_internal.h"
#include "symbol_chain.h"

static void callee_compile_test_info(Expr* me, BaselineTestCase* ctx);
// Past whitespace and comments: where the harness shows a node that starts at a missing node's full start (its
// skipTrivia)
static const char* skip_trivia_forward(const char* at) {
    while (true) {
        if (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' || *at == '\f' || *at == '\v') {
            at++;
        } else if (at[0] == '/' && at[1] == '/') {
            while (*at && *at != '\n' && *at != '\r') at++;
        } else if (at[0] == '/' && at[1] == '*') {
            const char* end = strstr(at + 2, "*/");
            if (!end) return at;
            at = end + 2;
        } else {
            return at;
        }
    }
}

static VecSpan class_prop_declarations(Symbol const* class_symbol, bool is_static, csview name, Span own, u32 min_count);

// What the symbols baseline shows for a reference: the declarations of the symbol the name found (an alias shows its
// import, not what it resolves to), or of the declaration when there is no symbol (keywords, synthetic nodes)
static inline Symbol const* shown_declaration(Symbol const* symbol, Symbol const* decl) {
    return symbol && symbol->decl_count ? symbol : decl;
}

// (Not inlined: the expression walk recurses deeply, binderBinaryExpressionStress)
__attribute__((noinline)) static VecSpan declaration_get_spans(Symbol const* me) {
    // A file's module that declarations merged into (a script's merging into a UMD global, `export as namespace`):
    // declared by the file first, like tsc's clone of the module symbol
    if (me && me->is_module && me->module && me->module->file && me->decl_count) {
        VecSpan ret = vec_span_init();
        vec_span_push(&ret, span_from_sv(me->module->file, csview_from_n(source_file_start(me->module->file), 0)));
        VecSpan own = baseline_symbol_declaration_spans(me);
        for_each (i, vec_span, &own) vec_span_push(&ret, *i);
        vec_span_drop(&own);
        return ret;
    }
    // A type a class's namespace exports is one symbol with the class's static member of its name (see
    // builder_add_class_static_side)
    if (me && me->parent && (me->parent->flags & SYMBOL_CLASS) && !(me->flags & SYMBOL_VALUE) && me->decl_count) {
        VecSpan merged = class_prop_declarations(me->parent, true, me->name, me->decls[0].full, 2);
        if (!vec_span_is_empty(&merged)) return merged;
    }
    return baseline_symbol_declaration_spans(me);
}

// The declarations of a module's `default` export are all named like its first one (`export default function f` and
// `export default class C` are both `f`)
static Span default_export_name(Symbol const* symbol) {
    return symbol && symbol->is_default_export ? symbol->name_span : span_null;
}

// tsc's symbolToString(symbol, node.parent): the name of a symbol as the symbols baseline prints it from here
static cstr symbol_text(BaselineTestCase* ctx, Symbol const* symbol) {
    SymbolChain chain = symbol_chain_at(ctx->enclosing, symbol, 0, false);
    cstr ret = cstr_init();
    symbol_chain_append(&ret, ctx->enclosing, &chain);
    return ret;
}

// A module symbol's name from here (tsc's symbolToString with the node's parent as the enclosing declaration)
static cstr module_text(BaselineTestCase* ctx, Symbol const* module) {
    return program_module_display_name(ctx->program, module, ctx->enclosing ? ctx->enclosing->file : nullptr,
        SPECIFIER_MODE_NONE, ctx->usage_ref);
}

// The name of a file's module from here: its module specifier in quotes, unless an alias in scope names it (`var m =
// require("./m")`, `import * as m`: tsc's getAccessibleSymbolChain)
static cstr file_module_text(BaselineTestCase* ctx, Symbol const* module) {
    SymbolChain chain = symbol_chain_at(ctx->enclosing, module, 0, false);
    Symbol const* first = chain.len ? chain.items[0] : nullptr;
    PortItem const* item = first && first != module && (first->flags & SYMBOL_ALIAS) && first->alias ?
        first->alias->item : nullptr;
    // (A namespace import can be a clone of the module, tsc's resolveESModuleSymbol, which the chain doesn't know
    // about: with esModuleInterop when the module's type has a `default` or signatures. An ES module's import of a
    // CommonJS one is a symbol of its own here already)
    bool plain_namespace = item && item->tag == PORT_IMPORT_GATHER && !symbol_get_export(module, c_sv("default")) &&
        !symbol_get_export(module, c_sv("export="));
    // (A default import, or `{ default as d }`, that is the module itself, a synthetic default, names it too)
    if (item && (item->is_require || plain_namespace || item->tag == PORT_IMPORT_DEFAULT || item->tag == PORT_IMPORT_SPECIFIC)) {
        cstr ret = cstr_init();
        symbol_chain_append(&ret, ctx->enclosing, &chain);
        return ret;
    }
    return module_text(ctx, module);
}

static bool has_symbol_override(BaselineTestCase const* ctx, Span span) {
    for (u32 i = 0; i < ctx->symbol_override_count; i++) {
        if (span_eq(ctx->symbol_overrides[i].span, span)) return true;
    }
    return false;
}

static void add_symbol_override(BaselineTestCase* ctx, Span span, Symbol const* symbol) {
    if (!symbol || ctx->symbol_override_count >= c_arraylen(ctx->symbol_overrides)) return;
    ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
        .span = span,
        .text = symbol_text(ctx, symbol),
        .declarations = declaration_get_spans(symbol),
    };
}

// `exports.x = undefined` is an alias of undefined to tsc, whose `undefined` shows the file's first such export
// (getSymbolAtLocation of the name in an alias declaration)
static void commonjs_undefined_override(BinaryOpExpr const* assignment, BaselineTestCase* ctx) {
    Expr const* value = &assignment->right;
    if (value->tag != EXPR_LITERAL || value->literal.tag != VALUE_UNDEFINED || expr_is_perenthisized(value)) return;
    SourceFile const* file = span_source(value->inner_span);
    Symbol const* module = file && file->scope ? file->scope->container : nullptr;
    if (!module || !module->exports) return;
    Symbol const* first = nullptr;
    char const* first_at = nullptr;
    for (u32 i = 0; i < module->exports->count; i++) {
        // (One whose last declaration assigns undefined: an alias resolves by its last declaration)
        Symbol const* symbol = module->exports->entries[i].symbol;
        SymbolDecl const* last = symbol->decl_count ? &symbol->decls[symbol->decl_count - 1] : nullptr;
        Expr const* node = last && last->kind == SYMBOL_DECL_ASSIGNMENT ? last->node : nullptr;
        if (!node || node->tag != EXPR_LITERAL || node->literal.tag != VALUE_UNDEFINED) continue;
        if (!first_at || span_start_ptr(node->inner_span) < first_at) {
            first = symbol;
            first_at = span_start_ptr(node->inner_span);
        }
    }
    // (named like the export, with no declarations)
    if (!first || ctx->symbol_override_count >= c_arraylen(ctx->symbol_overrides)) return;
    ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
        .span = value->inner_span, .text = symbol_text(ctx, first), .declarations = vec_span_init(),
    };
}

// JavaScript `this.x = v` that declares x: the name `x` is the member it declares, whatever the access finds (a
// base class's member: tsc's getSymbolOfDeclaration for the name of a declaration)
static void this_property_symbol_overrides(Expr* left, BaselineTestCase* ctx) {
    Span name = expr_static_property_name(left);
    Expr const* object = span_valid(name) ? &left->property->expr : nullptr;
    if (!object || object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS) return;
    IdentifierExpr const* this = &object->identifier;
    SymbolTable const* table = nullptr;
    if (this->this_kind == THIS_CLASS_INSTANCE && this->target) table = symbol_rare(this->target)->members;
    if (this->this_kind == THIS_CLASS_STATIC && this->target) table = this->target->exports;
    Symbol const* member = table ? symbol_table_get(table, span_sv(name)) : nullptr;
    // (Not when the class body declares it)
    ClassDecl const* class_decl = this->target ? symbol_find_node(this->target, SYMBOL_DECL_CLASS) : nullptr;
    ClassItem const* item = class_decl ? class_decl_find_member(class_decl, span_sv(name), this->this_kind == THIS_CLASS_STATIC) : nullptr;
    if (item) return;
    bool declares = false;
    for (u32 i = 0; member && i < member->decl_count; i++) declares |= span_eq(member->decls[i].full, expr_outer_span(left));
    if (!declares) return;
    add_symbol_override(ctx, property_key_span(left->property), member);
}

// Whether an alias resolves to nothing (tsc's unknownSymbol: an import of a module that doesn't resolve)
static bool alias_of_unknown(Symbol const* symbol) {
    return symbol && (symbol->flags & SYMBOL_ALIAS) && symbol->alias && symbol->alias->state == ALIAS_DONE &&
        (!symbol->alias->target || symbol->alias->target->is_placeholder);
}

// How tsc's symbolToString names its unknownSymbol (what a name after an unresolved import resolves to) from here:
// by the first alias of it in the innermost symbol table that has one (getAccessibleSymbolChain matches aliases by
// what they resolve to; a namespace's table is its exports; the globals last), else not at all
static cstr unknown_symbol_text(BaselineTestCase const* ctx) {
    for (Scope const* scope = ctx->enclosing; scope; scope = scope->parent) {
        SymbolTable const* tables[2] = { scope_locals(scope), nullptr };
        if (scope->kind == SCOPE_NAMESPACE && scope->container) tables[1] = symbol_members(scope->container)->exports;
        if (scope->kind == SCOPE_GLOBAL) tables[1] = &scope->binder->globals;
        for (u32 t = 0; t < 2; t++) {
            for (u32 i = 0; tables[t] && i < tables[t]->count; i++) {
                Symbol const* entry = tables[t]->entries[i].symbol;
                if (alias_of_unknown(entry)) return cstr_from_sv(entry->name);
            }
        }
    }
    return cstr_init();
}

// `A.B.C` as spans
// What the symbols baseline says about a property access, gathered over the members of a union or intersection
typedef struct PropSymbolInfo {
    bool found;
    u32 member_count; // Members of the object type that have the property
    bool missing_in_member; // Some member of a union has no such property: no symbol at all, like tsc
    bool strict_union; // A member that isn't an object (`undefined`) has no such property either
    bool qualified_name; // `typeof ns.A`: a namespace's type-only export too (ty_namespace_type_only_prop())
    Symbol const* parent;
    Symbol const* member_symbol; // A namespace's or module's export: named by its symbol
    Symbol const* namespace; // The object is a namespace's or module's value: the property is its export
    Span name_span;
    bool name_is_value;
    VecSpan declarations;
    // tsc's synthetic union/intersection property keeps the parent only when every member's property has the same
    // declaration (hasNonUniformValueDeclaration)
    Span first_declaration;
    bool non_uniform;
    // In an intersection: the members' own properties, not Object's or Function's (tsc's getPropertyOfType with
    // skipObjectFunctionPropertyAugment, unless no member has one)
    bool skip_augment;
    // The member properties found so far: tsc's synthetic property lists the declarations of each distinct one (the
    // same declaration again when two instantiations have it: `(A[] | B[]).filter`)
    // (tsc's symbol: the declared one, or an instantiation of it, told apart by type: instantiateSymbol keeps the
    // declared symbol when its type has no type variables, the same type here)
    struct { Span decl; Ty const* type; TyMapper const* mapper; Ty const* tuple; } seen[16];
    bool non_public; // A member property is private or protected
    bool readonly; // The (first) member property is readonly
    // The object is an ES module's `import * as ns` of a CommonJS module (make_gather_target()'s synthetic default)
    bool synthetic_namespace;
    u32 seen_count;
} PropSymbolInfo;

// tsc's createUnionOrIntersectionProperty lists each declaration once (AppendIfUnique)
static void push_unique_declaration(VecSpan* declarations, Span span) {
    for (isize q = 0; q < vec_span_size(declarations); q++) {
        if (span_eq(*vec_span_at(declarations, q), span)) return;
    }
    vec_span_push(declarations, span);
}

static void collect_found_prop(Checker* checker, Ty const* object, Prop const* prop, PropSymbolInfo* info);

// The property symbol a union's or intersection's members' properties (collected into `inner`) make, added to `info`
static void merge_composite_info(PropSymbolInfo* info, PropSymbolInfo const* inner_info) {
    PropSymbolInfo const inner = *inner_info;
    // A member's synthetic property of distinct declarations has no value declaration of its own: it doesn't count
    // for uniformity, the next member's declaration is the first (tsc's createUnionOrIntersectionProperty skips
    // members without a valueDeclaration in hasNonUniformValueDeclaration)
    Span first = inner.non_uniform ? span_null : inner.first_declaration;
    if (!info->found) {
        info->found = true;
        info->parent = inner.non_uniform ? nullptr : inner.parent;
        info->namespace = inner.namespace;
        info->member_symbol = inner.member_count == 1 ? inner.member_symbol : nullptr;
        info->name_span = inner.name_span;
        info->name_is_value = inner.name_is_value;
        info->first_declaration = first;
    } else if (span_valid(first) && !span_valid(info->first_declaration)) {
        info->first_declaration = first;
        info->parent = inner.parent;
    } else if (span_valid(first) && !span_eq(info->first_declaration, first)) {
        info->non_uniform = true;
    }
    info->member_count += inner.member_count;
    // (A composite member with one property that has the name is that property's symbol, tsc's
    // createUnionOrIntersectionProperty's singleProp: it may already be there. More are a symbol of their own)
    bool repeated = false;
    if (inner.seen_count == 1) {
        for (u32 k = 0; k < info->seen_count; k++) {
            repeated |= span_eq(info->seen[k].decl, inner.seen[0].decl) && info->seen[k].type == inner.seen[0].type && info->seen[k].mapper == inner.seen[0].mapper &&
                info->seen[k].tuple == inner.seen[0].tuple;
        }
        if (!repeated && info->seen_count < c_arraylen(info->seen)) info->seen[info->seen_count++] = inner.seen[0];
    }
    if (!repeated) {
        for_each (k, vec_span, &inner.declarations) push_unique_declaration(&info->declarations, *k);
    }
}

static void collect_prop_symbol(Checker* checker, Ty const* object, Atom const* name, PropSymbolInfo* info, u32 depth) {
    if (depth > 8) return;
    if (object->flags & (TYF_TYPE_PARAM | TYF_DEFERRED)) {
        Ty const* constraint = ty_base_constraint(checker, object);
        if (constraint) object = constraint;
    }
    // (tsc's getPropertyOfType reduces: an intersection that is never has none)
    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) object = ty_reduced(checker, object);
    if (object->kind == TY_UNION || object->kind == TY_INTERSECTION) {
        bool intersection = object->kind == TY_INTERSECTION;
        PropSymbolInfo inner = { .declarations = vec_span_init(), .strict_union = info->strict_union,
            .skip_augment = intersection || info->skip_augment };
        for (u32 i = 0; i < object->composite.types.len; i++) {
            collect_prop_symbol(checker, object->composite.types.data[i], name, &inner, depth + 1);
        }
        if (intersection && !inner.found && !info->skip_augment) {
            vec_span_drop(&inner.declarations);
            inner = (PropSymbolInfo){ .declarations = vec_span_init(), .strict_union = info->strict_union };
            for (u32 i = 0; i < object->composite.types.len; i++) {
                collect_prop_symbol(checker, object->composite.types.data[i], name, &inner, depth + 1);
            }
        }
        bool keep = inner.found && (object->kind == TY_INTERSECTION || !inner.missing_in_member);
        // (A union of distinct private or protected properties with no declaration in common has no property: tsc's
        // createUnionOrIntersectionProperty, getCommonDeclarationsOfSymbols)
        bool inaccessible = !intersection && inner.non_public && inner.seen_count > 1 && inner.non_uniform;
        if (inaccessible) keep = false;
        if (keep) {
            merge_composite_info(info, &inner);
        } else if ((object->kind == TY_UNION && !inaccessible) || !inner.found) {
            // (An intersection without the property is a member of an outer union that lacks it)
            info->missing_in_member = true;
        }
        vec_span_drop(&inner.declarations);
        return;
    }
    object = ty_apparent_type(checker, object);
    if (!(object->flags & TYF_OBJECT)) {
        // (`undefined` in a union has no properties either: the union's property is partial. Only said for binding
        // patterns: the object of a property access is sometimes not narrowed as far as tsc's, hacks.md)
        // A type parameter without a constraint is apparently unknown under strictNullChecks, which has none either
        if ((info->strict_union || (object->flags & TYF_UNKNOWN)) && !(object->flags & (TYF_NEVER | TYF_ANY))) info->missing_in_member = true;
        return;
    }
    Prop const* prop = info->skip_augment ? ty_get_prop(checker, object, name) : ty_get_object_prop(checker, object, name);
    if (!prop && info->qualified_name) prop = ty_namespace_type_only_prop(checker, object, name);
    if (!prop) {
        // (A member with an index signature for the name still has it: tsc's createUnionOrIntersectionProperty
        // counts it as WritePartial, no declarations, `(T[] | [T]).0`)
        Members const* members = ty_members(checker, object);
        bool indexed = false;
        for (u32 i = 0; i < members->index_count && !indexed; i++) {
            Ty const* key = members->index[i].key;
            indexed = (key->flags & TYF_STRING) || ((key->flags & TYF_NUMBER) && ty_is_numeric_name(name));
        }
        // (An object literal's type without it too, unless widened: isObjectLiteralType, `(o || {}).a`)
        if (ty_is_unwidened_object_literal(object)) indexed = true;
        if (!indexed) info->missing_in_member = true;
        return;
    }
    // A class's member inherited from an intersection base is the intersection's synthetic property (tsc's
    // getPropertiesOfType of the base in resolveObjectTypeMembers): declared by every member's
    Prop const* const* parts = nullptr;
    u32 part_count = ty_synthetic_prop_parts(checker, prop, &parts);
    if (part_count > 1) {
        PropSymbolInfo inner = { .declarations = vec_span_init(), .strict_union = info->strict_union, .skip_augment = true };
        for (u32 i = 0; i < part_count; i++) collect_found_prop(checker, object, parts[i], &inner);
        if (inner.found) merge_composite_info(info, &inner);
        vec_span_drop(&inner.declarations);
        return;
    }
    collect_found_prop(checker, object, prop, info);
}

static void collect_found_prop(Checker* checker, Ty const* object, Prop const* prop, PropSymbolInfo* info) {
    Prop const* root = prop;
    while (root->target && !root->decl->decl_span_count) root = root->target;
    Span first_declaration = root->decl->decl_span_count ? root->decl->decl_spans[0] : span_null;
    if (!info->found) {
        info->found = true;
        info->parent = prop->parent;
        if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE) {
            info->namespace = object->declared.namespace_symbol;
        }
        // (A namespace member spread into an object literal is still that member's symbol: tsc's getSpreadSymbol
        // keeps it, see PropDeclRare.spread_variable)
        // (Unless the spread made a fresh symbol, a readonly one's copy: no parent, a bare name)
        bool fresh = !root->decl->symbol && prop_decl_rare(root->decl)->spread_fresh;
        info->member_symbol = root->decl->symbol ? root->decl->symbol : fresh ? nullptr : prop_decl_rare(root->decl)->spread_variable;
        if (!root->decl->symbol && !fresh && prop_decl_rare(root->decl)->spread_namespace) info->namespace = prop_decl_rare(root->decl)->spread_namespace;
        info->name_span = root->decl->name_span;
        info->name_is_value = root->decl->name_is_value;
        info->first_declaration = first_declaration;
        info->readonly = (prop->flags & PROP_READONLY) != 0;
    } else if (span_valid(first_declaration) && !span_valid(info->first_declaration)) {
        info->first_declaration = first_declaration;
        info->parent = prop->parent;
    } else if (span_valid(first_declaration) && !span_eq(info->first_declaration, first_declaration)) {
        info->non_uniform = true;
    }
    info->member_count++;
    info->non_public |= (prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) != 0;
    // (tsc's instantiateSymbol keeps the declared symbol when its type has no type variables: the same type here)
    Ty const* type = prop_type(checker, prop);
    TyMapper const* mapper = prop->mapper && (prop == root || type != prop_type(checker, root)) ? prop->mapper : nullptr;
    // (Each tuple type has an Array base of its own in tsc, whose instantiated members are distinct symbols; tttt's
    // tuples of one element union share it)
    Ty const* instantiated_by = mapper && ty_is_tuple(object) ? object : nullptr;
    for (u32 k = 0; k < info->seen_count; k++) {
        if (span_eq(info->seen[k].decl, first_declaration) && info->seen[k].type == type && info->seen[k].mapper == mapper &&
            info->seen[k].tuple == instantiated_by
        ) {
            return;
        }
    }
    if (info->seen_count < c_arraylen(info->seen)) {
        info->seen[info->seen_count].decl = first_declaration;
        info->seen[info->seen_count].mapper = mapper;
        info->seen[info->seen_count].tuple = instantiated_by;
        info->seen[info->seen_count++].type = type;
    }
    // (A declaration once per property: some members' PropDecl repeat one, an enum merged with a namespace)
    for (u32 j = 0; j < root->decl->decl_span_count; j++) push_unique_declaration(&info->declarations, root->decl->decl_spans[j]);
    // A mapped type over an intersection: where each member's property is declared too (tsc's intersection property)
    Ty const* intersection = root->decl->rare ? prop_decl_rare(root->decl)->mapped_intersection : nullptr;
    for (u32 m = 0; intersection && m < intersection->composite.types.len; m++) {
        Ty const* part = ty_apparent_type(checker, intersection->composite.types.data[m]);
        Prop const* other = (part->flags & TYF_OBJECT) ? ty_get_prop(checker, part, prop->name) : nullptr;
        while (other && other->target && !other->decl->decl_span_count) other = other->target;
        for (u32 k = 0; other && k < other->decl->decl_span_count; k++) push_unique_declaration(&info->declarations, other->decl->decl_spans[k]);
    }
}

// The name of a member's declaration from here: its container's chain (see prop_text) and its name as written (a
// computed name `[x]` too). Without a container symbol, the container's name (`container_span`) or nothing
static cstr member_text(BaselineTestCase* ctx, Symbol const* container, Span container_span, csview name, bool enum_member) {
    cstr ret = cstr_init();
    SymbolChain chain = {0};
    if (container) chain = symbol_member_container_chain_at(ctx->enclosing, container);
    if (chain.len) {
        symbol_chain_append(&ret, ctx->enclosing, &chain);
        symbol_chain_append_member(&ret, name, enum_member);
    } else if (!container && span_valid(container_span)) {
        cstr_append_span(&ret, container_span);
        symbol_chain_append_member(&ret, name, enum_member);
    } else {
        cstr_append_sv(&ret, name);
    }
    return ret;
}

// What an import resolves to when it's a property of what a module's `export =` names (the binder's stand-in symbol)
static Symbol const* export_equals_property_of(Symbol const* alias) {
    Symbol const* target = symbol_resolve_alias((Symbol*)alias);
    return target && target->decl_count && target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY ? target : nullptr;
}

// An import in scope of the property `name` of what a module's `export =` names, when that is the found property
// (tsgo's getAccessibleSymbolChain: an alias resolving to the symbol names it, `import { a as a2 }` makes `x.a` print
// as `a2`). The innermost scope's
static Symbol const* export_equals_property_alias(BaselineTestCase* ctx, PropSymbolInfo const* info, csview name) {
    if (info->member_count != 1 || !span_valid(info->first_declaration)) return nullptr;
    Checker* checker = current_checker;
    for (Scope const* scope = ctx->enclosing; scope; scope = scope->parent) {
        if (!scope_locals(scope)->count) continue;
        u32 count;
        SymbolEntry* entries = symbol_table_collect(scope_locals(scope), &count);
        Symbol const* found = nullptr;
        for (u32 i = 0; i < count && !found; i++) {
            Symbol* alias = (Symbol*)entries[i].symbol;
            if (!(alias->flags & SYMBOL_ALIAS) || !alias->alias || !alias->alias->item) continue;
            PortItem const* item = alias->alias->item;
            if (item->tag != PORT_IMPORT_SPECIFIC) continue;
            Symbol const* target = export_equals_property_of(alias);
            if (!target) continue;
            if (!csview_equals_sv(target->name, name)) continue;
            Ty const* assigned = ty_type_of_symbol(checker, target->decls[0].node);
            if (!(assigned->flags & TYF_OBJECT)) continue;
            Prop const* prop = ty_get_prop(checker, assigned, checker_atom(checker, name));
            while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
            if (prop && prop->decl->decl_span_count && span_eq(prop->decl->decl_spans[0], info->first_declaration)) found = alias;
        }
        free(entries);
        if (found) return found;
    }
    return nullptr;
}

// An alias the enclosing CommonJS module exports of this very property, `exports.fn2 = Math.min` (tsgo's
// getAccessibleSymbolChain finds the alias in the module's exports: `Math.min` is named `fn2` there)
static Symbol const* commonjs_property_alias(BaselineTestCase* ctx, PropSymbolInfo const* info, csview name) {
    if (info->member_count != 1 || !span_valid(info->first_declaration) || !ctx->enclosing) return nullptr;
    SourceFile const* file = ctx->enclosing->file;
    Symbol const* module = file && file->is_js && file->scope ? file->scope->container : nullptr;
    // (The export of that name itself is found first, trySymbolTable's direct lookup)
    if (!module || !module->exports || symbol_table_get(module->exports, name)) return nullptr;
    Checker* checker = current_checker;
    Atom const* atom = nullptr;
    for (u32 i = 0; i < module->exports->count; i++) {
        Symbol const* alias = module->exports->entries[i].symbol;
        if (!(alias->flags & SYMBOL_ALIAS) || !alias->alias) continue;
        csview alias_name = symbol_entry_name(&module->exports->entries[i]);
        if (csview_equals(alias_name, "export=") || csview_equals(alias_name, "default")) continue;
        Expr const* entity = alias->alias->entity;
        if (!entity || entity->tag != EXPR_PROPERTY || entity->property->form != PROPERTY_EXPR_DOT ||
            !csview_equals_sv(span_sv(property_name(entity->property)), name)
        ) {
            continue;
        }
        if (!atom) atom = checker_atom(checker, name);
        Ty const* object = ty_expr_type(checker, &entity->property->expr);
        Prop const* prop = object ? ty_get_prop(checker, ty_apparent_type(checker, object), atom) : nullptr;
        while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
        if (prop && prop->decl->decl_span_count && span_eq(prop->decl->decl_spans[0], info->first_declaration)) return alias;
    }
    return nullptr;
}

// The name of a property's symbol from here: a namespace's or module's export by its own chain, a member by the chain
// of its container (tsc's getContainersOfSymbol of a member), else bare
static cstr prop_text(PropSymbolInfo const* info, BaselineTestCase* ctx, Span fallback) {
    csview name = span_valid(info->name_span) ? span_sv(info->name_span) : span_sv(fallback);
    // (A property no declaration names, `x["a"]` of what `"a" in x` synthesized, is named by its symbol's name)
    if (!span_valid(info->name_span) && name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'') &&
        name.buf[name.size - 1] == name.buf[0] && !memchr(name.buf, '\\', (size_t)name.size)
    ) {
        name = csview_slice(name, 1, name.size - 1);
    }
    cstr ret = cstr_init();
    Symbol const* member = info->member_symbol;
    // (An expando of a `{}` container is a member of the object literal in tsgo, whose symbol getSymbolChain skips:
    // bare, even where the container is reached through another name, `module.exports = o`)
    // (also one merged with a dotted typedef's namespace, SymbolRare.literal_member)
    if (info->member_count == 1 && member && member->decl_count &&
        ((!member->parent && member->decls[0].kind == SYMBOL_DECL_ASSIGNMENT) || symbol_rare(member)->literal_member)
    ) {
        cstr_append_sv(&ret, name);
        return ret;
    }
    // A member of a namespace's or module's object is its export (maybe an alias: getSymbolAtLocation doesn't resolve)
    // (by the name accessed: the export's own name may differ, `export default class C`)
    csview accessed = span_sv(fallback);
    if (accessed.size >= 2 && (accessed.buf[0] == '"' || accessed.buf[0] == '\'')) accessed = csview_slice(accessed, 1, accessed.size - 1);
    Symbol const* import_alias = export_equals_property_alias(ctx, info, accessed);
    if (!import_alias) import_alias = commonjs_property_alias(ctx, info, accessed);
    if (import_alias) {
        cstr_append_sv(&ret, import_alias->name);
        return ret;
    }
    Symbol const* exported = info->member_count == 1 && info->namespace ?
        symbol_get_export(info->namespace, accessed) : nullptr;
    // (An ES module's `import * as ns` of a CommonJS one: tsc's getSpreadSymbol copies a readonly export into the
    // synthetic namespace type, a symbol without a parent)
    if (exported && info->readonly && info->synthetic_namespace) {
        cstr_append_sv(&ret, name);
        return ret;
    }
    if (exported) {
        cstr_drop(&ret);
        return symbol_text(ctx, exported);
    }
    // (A class expression a CommonJS module exports, through an alias of the module: `wat.D`)
    if (info->member_count == 1 && member && !member->parent && (member->flags & SYMBOL_CLASS)) {
        cstr aliased = baseline_test_case_module_alias_text(ctx, member);
        if (cstr_size(&aliased)) {
            cstr_drop(&ret);
            return aliased;
        }
        cstr_drop(&aliased);
    }
    if (info->member_count == 1 && member && member->parent && !info->name_is_value &&
        (!info->parent || info->parent->is_module || member->parent == info->parent)
    ) {
        cstr_drop(&ret);
        return symbol_text(ctx, member);
    }
    SymbolChain chain = {0};
    if (info->parent && !(info->member_count > 1 && info->non_uniform)) {
        chain = symbol_member_container_chain_at(ctx->enclosing, info->parent);
    }
    if (!chain.len) {
        cstr_append_sv(&ret, name);
        return ret;
    }
    symbol_chain_append(&ret, ctx->enclosing, &chain);
    symbol_chain_append_member(&ret, name, false);
    return ret;
}

void generic_compile_test_info(Generic const* me, BaselineTestCase* ctx) {
    for_each (i, vec_generic_item, &my items) {
        if (i->jsdoc) continue;
        // (A type parameter of merged declarations is declared by all of them)
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .span = i->name,
            .declarations = i->owned_symbol ? baseline_symbol_declaration_spans(i->owned_symbol) : vec_span_single(i->full_span),
        });
        type_compile_test_info(&i->extends, ctx);
        if (i->fallback_explicit) {
            type_compile_test_info(i->fallback, ctx);
        }
    }
}

void binding_content_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case BINDING_NOOP:
            break; // An omitted element, `[, x]`: tsc 6's typeWriter skips OmittedExpression

        case BINDING_IDENTIFIER:
            if (my parent) ctx->any_keeps_rendering = true; // A binding element
            if (!my parent && span_valid(ctx->parameter_property_class) && my symbol) {
                // A parameter property's name declares the property (tsc's parameter.symbol is the property's)
                // (One symbol with a field of its name: the class's)
                VecSpan declarations = class_prop_declarations(ctx->parameter_property_symbol, false, span_sv(my content_span),
                    ctx->parameter_property_arg, 1);
                // (Not its first declaration, an overload's parameter or a field's: the type of the property)
                bool secondary = vec_span_size(&declarations) > 1 && !span_eq(declarations.data[0], ctx->parameter_property_arg);
                if (vec_span_is_empty(&declarations)) declarations = baseline_symbol_declaration_spans(my symbol);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declarations,
                    .namespaces = vec_span_single(ctx->parameter_property_class),
                    .span = my content_span,
                });
                Checker* checker = current_checker;
                Prop const* prop = secondary ?
                    ty_get_prop(checker, ty_declared_type_of(checker, ctx->parameter_property_symbol), checker_atom(checker, span_sv(my content_span))) :
                    nullptr;
                if (prop) baseline_test_case_add_type(ctx, my content_span, prop_type(checker, prop));
                else baseline_test_case_add_symbol_value(ctx, my content_span, my symbol);
                break;
            }
            baseline_test_case_add_declaration(ctx, my content_span, my symbol);
            break;

        case BINDING_ARRAY:
            for_each (i, vec_binding, &my array) {
                binding_content_compile_test_info(i, ctx);
            }
            break;

        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &my object) {
                if (i->computed_key) {
                    // A literal computed name `{ ["a"]: x }` is the binding element's own symbol to tsc's
                    // getSymbolAtLocation: the variable it declares
                    Expr const* key = i->computed_key;
                    bool literal = (key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)) ||
                        (key->tag == EXPR_TEMPLATE && !key->template_->tag && vec_expr_is_empty(&key->template_->substitutions));
                    if (literal && i->target.tag == BINDING_IDENTIFIER && i->target.symbol) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = baseline_symbol_declaration_spans(i->target.symbol),
                            .span = expr_outer_span(key),
                            .name = i->target.symbol->name_span,
                        });
                    }
                    expr_compile_test_info(i->computed_key, ctx);
                } else if (!i->target.rest && !i->private_name && !span_eq(i->source_span, i->target.content_span) &&
                    i->source_span.size && i->source_span.buf[0] != '"' && i->source_span.buf[0] != '\'' && !isdigit((unsigned char)i->source_span.buf[0])
                ) {
                    // `{ a: x }`: the property name is the property of the pattern's type (tsc's getSymbolAtLocation
                    // of a binding element's propertyName). A string or numeric name isn't an expression to tsc's walker
                    Checker* checker = current_checker;
                    Ty const* object = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
                    // (An optional parameter's pattern is of the optional type: tsc's getTypeOfNode of a pattern is
                    // getTypeForVariableLikeDeclaration with includeOptionality, `@param {T} [x]`)
                    if (!my parent && my optional_parameter) object = ty_optional_param_type(checker, object);
                    Atom const* name = ty_literal_name(checker, ty_literal(checker, &i->source));
                    PropSymbolInfo info = { .strict_union = true };
                    if (name && !ty_contextual_parameter_is_pattern(checker, me)) collect_prop_symbol(checker, object, name, &info, 0);
                    if (info.found) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = vec_span_clone(info.declarations),
                            .allow_declaration_empty = true,
                            .span = i->source_span,
                            .text = prop_text(&info, ctx, i->source_span),
                        });
                    }
                    vec_span_drop(&info.declarations);
                    // (Its type is any: tsc's getTypeOfNode of a property name that isn't an expression)
                    ctx->any_keeps_rendering = true;
                    baseline_test_case_add_type_node(ctx, i->source_span, &type_any);
                } else if (i->target.rest_property_name) {
                    // `...a: b` (TS2566): the property name is any too
                    ctx->any_keeps_rendering = true;
                    baseline_test_case_add_type_node(ctx, i->source_span, &type_any);
                }
                binding_content_compile_test_info(&i->target, ctx);
            }
            break;
    }
    if (my default_value) {
        if (my parent) ctx->any_keeps_rendering = true; // (A binding element's initializer: its parent is one too)
        expr_compile_test_info(my default_value, ctx);
    }
}

void binding_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    binding_content_compile_test_info(me, ctx);
    // (the parameters of a parameter property's type are no members)
    Span parameter_property_class = ctx->parameter_property_class;
    ctx->parameter_property_class = span_null;
    if (my has_explicit_type) {
        type_compile_test_info(binding_type(me), ctx);
    }
    ctx->parameter_property_class = parameter_property_class;
}

void initialized_binding_compile_test_info(
    InitializedBinding* me,
    BaselineTestCase* ctx
) {
    binding_compile_test_info(&my binding, ctx);
    if (initialized_binding_value(me)->tag != EXPR_IMPLICIT_ANY || initialized_binding_value(me)->missing || initialized_binding_value(me)->missing_decorators) {
        Expr const* literal = ctx->variable_literal;
        Span literal_name = ctx->variable_literal_name;
        if (initialized_binding_value(me)->tag == EXPR_OBJECT && my binding.tag == BINDING_IDENTIFIER) {
            ctx->variable_literal = initialized_binding_value(me);
            ctx->variable_literal_name = my binding.content_span;
        }
        expr_compile_test_info(initialized_binding_value(me), ctx);
        ctx->variable_literal = literal;
        ctx->variable_literal_name = literal_name;
    }
}

// The type of an optional property includes undefined, like tsc's type of its symbol
static Ty const* optional_member_type(Ty const* type, bool optional) {
    if (!optional || !compiler_options()->strict_null_checks) return type;
    Checker* checker = current_checker;
    Ty const* parts[2] = { type, ty_intrinsic(checker, TY_UNDEFINED) };
    return ty_union(checker, parts, 2, nullptr, (TyList){0});
}

// The declarations of a class's property symbol as the class's type has them, when they include `own` (a field and the
// parameter property or interface member of its name are one symbol) and are at least `min_count`, else empty
static VecSpan class_prop_declarations(Symbol const* class_symbol, bool is_static, csview name, Span own, u32 min_count) {
    VecSpan ret = vec_span_init();
    if (!class_symbol) return ret;
    Checker* checker = current_checker;
    Ty const* owner = is_static ? ty_static_type_of(checker, class_symbol) : ty_declared_type_of(checker, class_symbol);
    if (!owner || !(owner->flags & TYF_OBJECT)) return ret;
    Prop const* prop = ty_get_prop(checker, owner, checker_atom(checker, name));
    while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
    if (!prop || prop->decl->decl_span_count < min_count) return ret;
    bool has_own = false;
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) has_own |= span_eq(prop->decl->decl_spans[i], own);
    if (!has_own) return ret;
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(&ret, prop->decl->decl_spans[i]);
    return ret;
}

// A member's symbol line; a computed name that is a literal (`["a"]`, `[0]`) names the member too
static void add_member_symbol(BaselineTestCase* ctx, BaselineSym sym, Expr const* computed_key) {
    bool literal = computed_key && (
        (computed_key->tag == EXPR_LITERAL && (computed_key->literal.tag == VALUE_STRING || computed_key->literal.tag == VALUE_NUMBER)) ||
        (computed_key->tag == EXPR_TEMPLATE && !computed_key->template_->tag && vec_expr_is_empty(&computed_key->template_->substitutions))
    );
    BaselineSym key = literal ? (BaselineSym){
        .declarations = vec_span_clone(sym.declarations),
        .span = computed_key->inner_span,
        .text = cstr_clone(sym.text),
    } : (BaselineSym){0};
    baseline_test_case_add_symbol(ctx, sym);
    if (literal) baseline_test_case_add_symbol(ctx, key);
}

// The type parameters of the class or interface aren't in scope in a computed member name (tsc's resolveName)
static void computed_member_name_compile_test_info(Expr* key, BaselineTestCase* ctx) {
    u32 inside = ctx->scope_params_len;
    bool in_class = ctx->class_params_len && ctx->class_params_outside + ctx->class_params_len <= inside;
    if (in_class) baseline_test_case_leave_generic(ctx, ctx->class_params_outside);
    expr_compile_test_info(key, ctx);
    if (in_class) {
        baseline_test_case_enter_type_params(ctx, (TyList){ ctx->class_params, ctx->class_params_len });
        check(ctx->scope_params_len == inside);
    }
}

// The initializers of an index signature's parameter from `from` to `to` (null: the end)
static void index_initializers_compile_test_info(InterfaceIndexKey const* key, const char* from, const char* to, BaselineTestCase* ctx) {
    if (!from) return;
    for (isize i = 0; i < vec_span_size(&key->initialized); i++) {
        const char* at = span_start_ptr(*vec_span_at(&key->initialized, i));
        if (at >= from && (!to || at < to)) expr_compile_test_info(key->initializer_values[i], ctx);
    }
}

void function_impl_compile_test_info(FunctionImpl* me, BaselineTestCase* ctx);
void interface_member_compile_test_info(InterfaceMember const* me, Span scope, BaselineTestCase* ctx) {
    switch (my key.tag) {
        case INTERFACE_KEY_NAME: {
            // Overloaded method signatures are one member, declared by all of them (of every declaration of an
            // interface), whose type has all the signatures
            Prop const* merged = nullptr;
            if (my key.name_is_member_function && ctx->member_owner) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                if (prop && (prop->flags & PROP_METHOD)) {
                    // (Also with the declarations of an interface of another program, the lib's: `interface Array<T>`)
                    Ty const* type = prop_type(checker, prop);
                    if (prop->decl->decl_span_count > 1 || ((type->flags & TYF_OBJECT) && ty_members(checker, type)->call_count > 1)) {
                        merged = prop;
                    }
                }
            }
            // A property declared again (TS2717) is one symbol, of the first declaration's type (tsc's getTypeOfSymbol)
            Prop const* redeclared = nullptr;
            if (!my key.name_is_member_function && ctx->member_owner && !my has_write_type) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                if (prop && !(prop->flags & PROP_METHOD) && prop->decl->decl_span_count > 1 && prop->decl->node &&
                    !prop_decl_rare(prop->decl)->write_node
                ) {
                    redeclared = prop;
                }
            }
            // A property after an accessor of its name joined its symbol (see builder_add_interface_member)
            Prop const* joined = nullptr;
            if (!my key.name_is_member_function && ctx->member_owner && !redeclared && span_valid(my declaration)) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
                bool mine = false;
                for (u32 i = 0; prop && (prop->flags & (PROP_METHOD | PROP_ACCESSOR)) && i < prop->decl->decl_span_count; i++) {
                    mine |= span_eq(prop->decl->decl_spans[i], my declaration);
                }
                if (mine) joined = prop;
            }
            // A property and an accessor pair of one name in declarations of an interface are one symbol (tsgo 7.1)
            Prop const* shared = nullptr;
            if (!my key.name_is_member_function && ctx->member_owner && !redeclared && !joined && span_valid(my declaration)) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
                bool mine = false;
                for (u32 i = 0; prop && !(prop->flags & PROP_METHOD) && i < prop->decl->decl_span_count; i++) {
                    mine |= span_eq(prop->decl->decl_spans[i], my declaration);
                }
                u32 own;
                interface_member_declarations(me, &own);
                if (mine && prop->decl->decl_span_count > own) shared = prop;
            }
            VecSpan declarations = vec_span_init();
            // (A symbol is named as its first declaration writes it: `1; 1.0;` is `I[1]` twice)
            csview written = span_sv(my key.span);
            // (An instantiation's declarations are its target's: `interface Array<T>` merged with the lib's)
            Prop const* merged_root = merged;
            while (merged_root && !merged_root->decl->decl_span_count && merged_root->target) merged_root = merged_root->target;
            if ((merged_root && merged_root->decl->decl_span_count) || redeclared || joined || shared) {
                Prop const* shown = merged ? merged_root : redeclared ? redeclared : joined ? joined : shared;
                for (u32 i = 0; i < shown->decl->decl_span_count; i++) vec_span_push(&declarations, shown->decl->decl_spans[i]);
                if (span_valid(shown->decl->name_span)) written = span_sv(shown->decl->name_span);
            } else {
                declarations = interface_member_declaration_vec(me);
            }
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my key.span,
                .text = member_text(ctx, ctx->member_container, scope, written, false),
            });
            // (The declared member, `this` not instantiated: tsc's type of the merged symbol)
            Prop const* declared_member = merged;
            while (declared_member && declared_member->target) declared_member = declared_member->target;
            // (Optional if any declaration is: the symbol's flags)
            Ty const* type = merged ? optional_member_type(prop_type(current_checker, declared_member), declared_member->flags & PROP_OPTIONAL) :
                optional_member_type(ty_of_node(&my type), my optional);
            // (A getter with a body and no annotation, TS1183: what it returns, tsc's getTypeOfAccessors)
            if (!merged && my accessor && my accessor->getter_body && my accessor->getter_body_infers) {
                type = optional_member_type(ty_object_literal_accessor_type(current_checker, my accessor->getter_body->function, true), my optional);
            }
            // (Optional as its first declaration is: tsc's addOptionality of the symbol's valueDeclaration)
            if (redeclared) type = optional_member_type(prop_type(current_checker, redeclared), redeclared->flags & PROP_OPTIONAL);
            // A property declared after an accessor of its name is that accessor's symbol (after a method it conflicts:
            // a symbol of its own)
            if (joined) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                Prop const* root = prop;
                while (root && root->target && !root->decl->decl_span_count) root = root->target;
                if (prop && root->decl->decl_span_count && !span_eq(root->decl->decl_spans[0], my declaration)) {
                    type = prop_type(checker, prop);
                }
            }
            bool method = my key.name_is_member_function && my type.tag == TYPE_CALLABLE;
            u32 saved = method ? baseline_test_case_enter_generic(ctx, &my type.callable->function->generic) : 0;
            baseline_test_case_add_type(ctx, my key.span, type);
            if (method) baseline_test_case_leave_generic(ctx, saved);
            // `get x(): T; set x(v: U)`: the second accessor names the same member, the setter declares its parameter.
            // In source order: the first accessor's types come before the second's name
            Span second_name = interface_member_second_name(me), setter_param = interface_member_setter_param(me);
            bool pair = span_valid(second_name);
            // (A setter's pattern parameter: its names, then its annotation, see InterfaceAccessor.setter_function)
            FunctionImpl* setter_function = my accessor && my accessor->setter_function ? my accessor->setter_function->function : nullptr;
            Span setter_at = span_valid(setter_param) ? setter_param : setter_function ? my accessor->setter_pattern : span_null;
            bool setter_first = pair && span_valid(setter_at) && setter_at.buf < second_name.buf;
            // (A getter's body, TS1183: what's in it, after the getter's type)
            FunctionImpl* getter_body = my accessor && my accessor->getter_body ? my accessor->getter_body->function : nullptr;
            if (pair && !setter_first) type_compile_test_info(&my type, ctx);
            if (pair && !setter_first && getter_body) function_impl_compile_test_info(getter_body, ctx);
            if (span_valid(setter_param) && setter_first) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = setter_param });
                baseline_test_case_add_type(ctx, setter_param, my has_write_type ? ty_of_node(&my accessor->write_type) : type);
                if (my has_write_type) type_compile_test_info(&my accessor->write_type, ctx);
            }
            if (setter_function && setter_first) function_impl_compile_test_info(setter_function, ctx);
            if (pair) {
                VecSpan second_decls = vec_span_init();
                if (shared) {
                    for (u32 i = 0; i < shared->decl->decl_span_count; i++) vec_span_push(&second_decls, shared->decl->decl_spans[i]);
                } else {
                    second_decls = interface_member_declaration_vec(me);
                }
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = second_decls,
                    .span = second_name,
                    .text = member_text(ctx, ctx->member_container, scope, span_sv(second_name), false),
                });
                baseline_test_case_add_type(ctx, second_name, type);
            }
            if (span_valid(setter_param) && !setter_first) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = setter_param });
                baseline_test_case_add_type(ctx, setter_param, my has_write_type ? ty_of_node(&my accessor->write_type) : type);
                if (pair && my has_write_type) type_compile_test_info(&my accessor->write_type, ctx);
            }
            if (setter_function && pair && !setter_first) function_impl_compile_test_info(setter_function, ctx);
            if (pair) {
                if (setter_first) type_compile_test_info(&my type, ctx);
                if (setter_first && getter_body) function_impl_compile_test_info(getter_body, ctx);
                return;
            }
        }   break;

        case INTERFACE_KEY_INDEX:
            if (span_valid(my key.index->name)) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .span = my key.index->name, .declarations = vec_span_single(my key.index->decl),
                });
                baseline_test_case_add_type(ctx, my key.index->name,
                    optional_member_type(ty_of_node(&my key.index->type), my key.index->optional_params & 1));
            }
            type_compile_test_info(&my key.index->type, ctx);
            index_initializers_compile_test_info(my key.index, span_valid(my key.index->decl) ? span_start_ptr(my key.index->decl) : nullptr,
                vec_span_size(&my key.index->more_decls) ? span_start_ptr(*vec_span_at(&my key.index->more_decls, 0)) : nullptr, ctx);
            for (u32 k = 0; k < vec_span_size(&my key.index->more_names); k++) {
                Span name = *vec_span_at(&my key.index->more_names, k);
                TypeNode const* type = vec_type_at(&my key.index->more_types, k);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .span = name, .declarations = vec_span_single(*vec_span_at(&my key.index->more_decls, k)),
                });
                baseline_test_case_add_type(ctx, name, optional_member_type(ty_of_node(type), k + 1 < 32 && (my key.index->optional_params >> (k + 1) & 1)));
                type_compile_test_info(type, ctx);
                index_initializers_compile_test_info(my key.index, span_start_ptr(*vec_span_at(&my key.index->more_decls, k)),
                    k + 1 < vec_span_size(&my key.index->more_decls) ? span_start_ptr(*vec_span_at(&my key.index->more_decls, k + 1)) : nullptr, ctx);
            }
            break;

        case INTERFACE_KEY_EXPR: {
            // A computed name that names a member merges like a written one (the checker's member has the
            // declarations of its symbol, see builder_add_interface_member)
            VecSpan declarations = vec_span_init();
            Prop const* overloads = nullptr; // (Overloaded methods are one member, of all their signatures)
            Prop const* accessor_pair = nullptr; // (`get [k]()` and `set [k](v)`: one member, of the getter's type)
            if (ctx->member_owner) {
                Checker* checker = current_checker;
                bool is_number = false;
                Atom const* name = ty_property_name_of_key(checker, ty_expr_type(checker, my key.expr), &is_number);
                Prop const* prop = name ? ty_get_prop(checker, ctx->member_owner, name) : nullptr;
                bool mine = false;
                Span own = span_valid(my declaration) ? my declaration : my key.span;
                for (u32 i = 0; prop && i < prop->decl->decl_span_count; i++) mine |= span_eq(prop->decl->decl_spans[i], own);
                if (mine && prop->decl->decl_span_count > 1) {
                    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(&declarations, prop->decl->decl_spans[i]);
                    // (Overloads or a duplicate property: one symbol, its first declaration's type)
                    overloads = prop;
                    if (my computed_accessor) accessor_pair = prop;
                }
            }
            if (vec_span_is_empty(&declarations)) vec_span_push(&declarations, span_valid(my declaration) ? my declaration : my key.span);
            // (Named as its symbol's first declaration writes it: a late-bound member merged into an early one is that one)
            Span written = overloads && span_valid(overloads->decl->name_span) ? overloads->decl->name_span : my key.span;
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my key.span,
                .text = member_text(ctx, ctx->member_container, scope, span_sv(written), false),
            }, my key.expr);
            // (Overloads: optional if any of them is, the symbol's flags)
            baseline_test_case_add_type(ctx, my key.span, overloads ? optional_member_type(prop_type(current_checker, overloads), overloads->flags & PROP_OPTIONAL) :
                accessor_pair ? prop_type(current_checker, accessor_pair) : optional_member_type(ty_of_node(&my type), my optional));
            computed_member_name_compile_test_info(my key.expr, ctx);
            // (A lone setter `set [k](v: T)` declares its parameter)
            Span setter_param = interface_member_setter_param(me);
            if (span_valid(setter_param) && my computed_accessor == 2) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = setter_param });
                baseline_test_case_add_type(ctx, setter_param, ty_of_node(&my type));
            }
        }   break;

        case INTERFACE_KEY_MAPPED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my key.mapped->type_name });
            type_compile_test_info(&my key.mapped->in_type, ctx);
            type_compile_test_info(&my key.mapped->as_type, ctx);
            break;

        case INTERFACE_KEY_CALLABLE: break;
    }
    if (!my accessor || !my accessor->type_borrowed) type_compile_test_info(&my type, ctx);
    if (my has_write_type && !my accessor->write_type_borrowed) type_compile_test_info(&my accessor->write_type, ctx);
    // (A setter's pattern parameter: its names, then its annotation, see InterfaceAccessor.setter_function)
    if (my accessor && my accessor->setter_function) function_impl_compile_test_info(my accessor->setter_function->function, ctx);
    if (my accessor && my accessor->initializer) expr_compile_test_info(my accessor->initializer, ctx);
    // (A getter's body, TS1183: what's in it, not the function)
    if (my accessor && my accessor->getter_body) function_impl_compile_test_info(my accessor->getter_body->function, ctx);
}

void interface_compile_test_info(Interface const* me, Span scope, BaselineTestCase* ctx) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_compile_test_info(i, scope, ctx);
    }
}

static void type_predicate_compile_test_info(TypePredicateNode* me, BaselineTestCase* ctx);

// The type parameter of `infer X`: from the end of `infer` (its leading trivia) to the end of the name
static Span inferred_type_parameter_span(InferredType const* inferred) {
    csview full = span_sv(inferred->full_span);
    isize at = csview_find_sv(full, c_sv("infer"));
    char const* start = at == c_NPOS ? inferred->name.buf : full.buf + at + 5;
    if (start > inferred->name.buf) start = inferred->name.buf;
    return span_make_in(inferred->name, start, span_end_ptr(inferred->name));
}

void type_compile_test_info(TypeNode const* me, BaselineTestCase* ctx) {
    if (my from_jsdoc) return; // tsc's walk doesn't go into JSDoc
    switch (my tag) {
        case TYPE_UNKNOWN:
        case TYPE_NEVER:
        case TYPE_VOID:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
        case TYPE_SYMBOL:
        case TYPE_UNIQUE_SYMBOL:
        case TYPE_BIGINT:
            break;

        case TYPE_VALUE: {
            // tsc's walk sees `true`, `false` and `-1` (and its `1`) as expressions, not other literal types
            if (!span_valid(my value_span)) break;
            bool negative = (my value.tag == VALUE_NUMBER || my value.tag == VALUE_BIGINT) && my value_span.size && my value_span.buf[0] == '-';
            if (my value.tag != VALUE_BOOLEAN && !negative) break;
            baseline_test_case_add_type_node(ctx, my value_span, me);
            if (negative) {
                Span operand = my value_span;
                operand.buf++;
                operand.size--;
                while (operand.size && (operand.buf[0] == ' ' || operand.buf[0] == '\t')) {
                    operand.buf++;
                    operand.size--;
                }
                Value positive = my value.tag == VALUE_BIGINT ? value_bigint_negated(&my value)
                                                              : value_number(-my value.number);
                baseline_test_case_add_type(ctx, operand, ty_literal(current_checker, &positive));
            }
        }   break;

        case TYPE_ANY:
            if (span_valid(my any.unkown_symbol_span)) {
                // (Named by an alias of it in scope if there is one, see unknown_symbol_text())
                cstr text = unknown_symbol_text(ctx);
                if (!cstr_size(&text)) cstr_assign(&text, "unknown");
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .text = text,
                    .span = my any.unkown_symbol_span,
                });
            }
            break;

        case TYPE_IDENTIFIER:
            // `import("./a", { with: { type: "json" } })`: the keys written as identifiers are errorType, like an import
            // declaration's (printed `error`, or `any` where the test has errors: tsgo's TypeWriterWalker prints an any's
            // intrinsic name only without an error baseline)
            if (my identifier->rare && my identifier->rare->import_attributes) {
                for_each (i, vec_import_attribute, &my identifier->rare->import_attributes->elements) {
                    if (span_valid(i->name) && i->name.buf[0] != '"' && i->name.buf[0] != '\'') {
                        baseline_test_case_add_type(ctx, i->name, ty_intrinsic(current_checker, TY_ERROR));
                    }
                }
            }
            if (identifier_type_import_argument(my identifier)) { // (Nothing after an invalid argument is resolved)
                type_compile_test_info(identifier_type_import_argument(my identifier), ctx);
                for_each (i, vec_spanned_type, &my identifier->generic_params) type_compile_test_info(&i->type, ctx);
                break;
            }
            if (span_equals(my identifier->name, "object")) {
                break;
            }
            if (my identifier->keyword == KEYWORD_IDENTIFIER_THIS) break; // A `this` type isn't an identifier to tsc
            if (my identifier->intrinsic_keyword) break; // (Nor `type T = intrinsic`: a keyword)
            // `typeof import("./m").x` of a module that is `export =` an object: x is a property of the module's type
            // (tsc's getTypeFromImportTypeNode: symbolFromVariable)
            if (identifier_type_import_ref(my identifier) && identifier_type_import_typeof(my identifier) && span_valid(my identifier->name) &&
                !identifier_type_namespace_count(my identifier) && my identifier->target && !span_valid(my identifier->target->name_span) &&
                !my identifier->target->is_module
            ) {
                // (The target is what the module's `export =` assigns)
                Checker* checker = current_checker;
                Ty const* module_type = ty_type_of_symbol(checker, my identifier->target);
                Prop const* prop = module_type && (module_type->flags & TYF_OBJECT) ?
                    ty_get_prop(checker, module_type, checker_atom(checker, span_sv(my identifier->name))) : nullptr;
                while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
                if (prop && prop->decl->decl_span_count) {
                    VecSpan spans = vec_span_init();
                    for (u32 k = 0; k < prop->decl->decl_span_count; k++) vec_span_push(&spans, prop->decl->decl_spans[k]);
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = spans,
                        .allow_declaration_empty = true,
                        .span = my identifier->name,
                        .text = cstr_from_sv(span_sv(my identifier->name)),
                    });
                }
            }
            // (An alias of an anonymous class, `import C = require()` of `export = class {}`, is shown)
            if (my identifier->target && !span_valid(my identifier->target->name_span) &&
                !(my identifier->target->is_module && my identifier->target->module->file && my identifier->target->decl_count) &&
                !(my identifier->symbol && my identifier->symbol != my identifier->target &&
                    my identifier->symbol->flags & SYMBOL_ALIAS && span_valid(my identifier->symbol->name_span))
            ) {
                // (Its qualifier still has a type: see below)
                if (identifier_type_import_typeof(my identifier) && span_valid(my identifier->name) && !identifier_type_namespace_count(my identifier)) {
                    baseline_test_case_add_type(ctx, my identifier->name, ty_intrinsic(current_checker, TY_ERROR));
                }
                break;
            }
            bool heritage = ctx->heritage_type;
            ctx->heritage_type = false;
            if (heritage) {
                // A heritage clause's name is a property access to tsc's writer: `a.b.C` visits `a.b.C` (symbols only:
                // the whole is part of the type) and `a.b` before `a`
                isize_t count = identifier_type_namespace_count(my identifier);
                isize_t known = vec_symbol_cptr_size(identifier_type_namespace_symbols(my identifier));
                Symbol const* named = shown_declaration(my identifier->symbol, my identifier->target);
                if (count && span_valid(my identifier->name) && named && !named->is_placeholder) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(named),
                        .allow_declaration_empty = true,
                        .span = span_between(*vec_span_at(identifier_type_namespaces(my identifier), 0), my identifier->name),
                        .text = symbol_text(ctx, named),
                    });
                }
                // (Unresolved ones too: `any`)
                for (isize_t k = count - 1; k >= 1; k--) {
                    Symbol const* decl = k < known ? *vec_symbol_cptr_at(identifier_type_namespace_symbols(my identifier), k) : nullptr;
                    Span prefix = span_between(*vec_span_at(identifier_type_namespaces(my identifier), 0), *vec_span_at(identifier_type_namespaces(my identifier), k));
                    if (decl && !decl->is_placeholder) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = declaration_get_spans(decl),
                            .allow_declaration_empty = true,
                            .span = prefix,
                            .text = symbol_text(ctx, decl),
                        });
                    }
                    // (Of type any, whatever it names: tsgo's getTypeAtLocation of an `implements` or interface
                    // `extends` clause's qualifier, where TS 6.0 had its value's type)
                    ctx->any_keeps_rendering = true;
                    baseline_test_case_add_type_node(ctx, prefix, &type_any);
                }
            }
            VecSpan so_far = vec_span_init();
            bool heritage_unresolved = false;
            for_each (i, vec_span, identifier_type_namespaces(my identifier)) {
                intptr_t index = i_index;
                Symbol const* decl = index < vec_symbol_cptr_size(identifier_type_namespace_symbols(my identifier)) ?
                    *vec_symbol_cptr_at(identifier_type_namespace_symbols(my identifier), index) :
                    nullptr;
                ctx->any_keeps_rendering = true; // Part of a qualified name (any in a heritage clause too, see above)
                baseline_test_case_add_type_node(ctx, *i, &type_any);
                // (After an unresolved name of a heritage clause, the rest are expressions of no symbol)
                if (heritage && (!decl || heritage_unresolved)) {
                    heritage_unresolved = true;
                    continue;
                }
                // (Nothing for a name an import type can't find: see below)
                if (identifier_type_import_ref(my identifier) && (!decl || decl->is_placeholder)) {
                    vec_span_push(&so_far, *i);
                    continue;
                }
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declaration_get_spans(decl),
                    .allow_declaration_empty = true,
                    .namespaces = vec_span_clone(so_far),
                    .span = *i,
                    // (A missing name resolves to tsc's unknownSymbol)
                    .text = decl && !decl->is_placeholder ? symbol_text(ctx, decl) : i->size ? cstr_init() : cstr_lit("unknown"),
                });
                vec_span_push(&so_far, *i);
            }
            vec_span_drop(&so_far);
            if (!span_valid(my identifier->name)) { // A bare `import("./a")`, maybe with type arguments
                for_each (i, vec_spanned_type, &my identifier->generic_params) type_compile_test_info(&i->type, ctx);
                break;
            }
            if (!my identifier->name.size && !identifier_type_import_ref(my identifier)) {
                // A missing name (`a.` at the end of a line): tsc's unknownSymbol, shown where the next token starts
                const char* at = my identifier->name.buf;
                at = skip_trivia_forward(at);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .span = span_make_in(my identifier->name, at, at),
                    .text = cstr_lit("unknown"),
                });
                break;
            }
            Symbol const* shown = shown_declaration(my identifier->symbol, my identifier->target);
            // A value used as a type (TS2749) is tsc's unresolved symbol, which has no declarations
            if (!heritage && !my identifier->jsdoc && !my identifier->keyword && !span_valid(identifier_type_import_span(my identifier)) &&
                my identifier->target &&
                !my identifier->target->is_placeholder && !(my identifier->target->flags & SYMBOL_TYPE)
            ) {
                shown = nullptr;
            }
            Symbol const* export_equals_shown = my identifier->jsdoc ? nullptr : ty_export_equals_property_shown(current_checker, my identifier);
            if (export_equals_shown) shown = export_equals_shown;
            // (A name an import type can't find, in a module that doesn't resolve or doesn't export it, is nothing: tsc's
            // getSymbolAtLocation of an ImportTypeNode's qualifier resolves through the module)
            bool unresolved_import = identifier_type_import_ref(my identifier) && (!shown || shown->is_placeholder);
            // `typeof import("./a").A.foo` with A a value that doesn't export foo: foo is a property of A's type (tsc's
            // getTypeFromImportTypeNode; the checker reads it the same way, lower.c)
            int prop_start = my identifier->rare ? my identifier->rare->import_prop_start : -1;
            if (!unresolved_import && identifier_type_import_typeof(my identifier) && prop_start > 0 &&
                prop_start == identifier_type_namespace_count(my identifier)
            ) {
                Checker* checker = current_checker;
                Ty const* type = ty_type_of_symbol(checker, my identifier->target);
                Atom const* name = checker_atom(checker, span_sv(my identifier->name));
                if ((type->flags & TYF_OBJECT) && ty_get_prop(checker, type, name)) {
                    PropSymbolInfo info = { .declarations = vec_span_init() };
                    collect_prop_symbol(checker, type, name, &info, 0);
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = info.declarations,
                        .allow_declaration_empty = true,
                        .span = my identifier->name,
                        .text = prop_text(&info, ctx, my identifier->name),
                    });
                    unresolved_import = true; // (shown above)
                }
            }
            if (!(heritage && !shown) && !unresolved_import) baseline_test_case_add_symbol(ctx, (BaselineSym){
                .namespaces = vec_span_clone(*identifier_type_namespaces(my identifier)),
                .declarations = declaration_get_spans(shown),
                .allow_declaration_empty = true,
                .span = my identifier->name,
                .text = shown && !shown->is_placeholder ? symbol_text(ctx, shown) : cstr_init(),
            });
            // `typeof import("a").x`'s qualifier isn't part of a type node to tsc's writer (isPartOfTypeNode: an
            // import type with isTypeOf), and getTypeAtLocation has nothing for it: errorType, `any` as the right of
            // a qualified name
            if (identifier_type_import_typeof(my identifier)) {
                if (identifier_type_namespace_count(my identifier)) {
                    ctx->any_keeps_rendering = true;
                    baseline_test_case_add_type_node(ctx, my identifier->name, &type_any);
                } else {
                    baseline_test_case_add_type(ctx, my identifier->name, ty_intrinsic(current_checker, TY_ERROR));
                }
            }
            for_each (i, vec_spanned_type, &my identifier->generic_params) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case TYPE_INFERRED:
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                // (An `infer X` outside a conditional's extends clause, TS1338, is an anonymous type parameter
                // declared by itself: tsc's bindAnonymousDeclaration)
                .declarations = my inferred->owned_symbol ? declaration_get_spans(my inferred->owned_symbol) :
                    vec_span_single(inferred_type_parameter_span(my inferred)),
                .allow_declaration_empty = true,
                .span = my inferred->name,
            });
            type_compile_test_info(&my inferred->extends, ctx);
            break;

        case TYPE_CALLABLE: {
            Span parameter_property_class = ctx->parameter_property_class;
            ctx->parameter_property_class = span_null;
            u32 saved = baseline_test_case_enter_generic(ctx, &my callable->function->generic);
            generic_compile_test_info(&my callable->function->generic, ctx);
            for_each (i, vec_function_arg, &my callable->function->args) {
                binding_content_compile_test_info(&i->initializer.binding, ctx);
                if (i->initializer.binding.has_explicit_type) {
                    type_compile_test_info(binding_type(&i->initializer.binding), ctx);
                }
                // (An initializer is an error here, TS2371, but still an expression)
                if (initialized_binding_value(&i->initializer)->tag != EXPR_IMPLICIT_ANY) expr_compile_test_info(initialized_binding_value(&i->initializer), ctx);
            }
            if (my callable->function->predicate) {
                type_predicate_compile_test_info(my callable->function->predicate, ctx);
            } else {
                type_compile_test_info(&my callable->function->ret, ctx);
            }
            baseline_test_case_leave_generic(ctx, saved);
            ctx->parameter_property_class = parameter_property_class;
        }   break;

        case TYPE_INTERFACE: {
            Ty const* outer_owner = ctx->member_owner;
            ctx->member_owner = ty_of_node(me);
            Symbol const* outer_container = ctx->member_container;
            ctx->member_container = nullptr;
            u32 outer_class_params_len = ctx->class_params_len;
            ctx->class_params_len = 0; // (A type literal's computed names see them)
            // (A mapped type's key is a type parameter in scope in it: tsc's MappedType has locals)
            u32 saved_generic = 0;
            bool mapped = vec_interface_member_size(&my interface.members) == 1 &&
                vec_interface_member_at(&my interface.members, 0)->key.tag == INTERFACE_KEY_MAPPED;
            MappedInfo const* info = mapped && ctx->member_owner ? ty_mapped_info_unexpanded(current_checker, ctx->member_owner) : nullptr;
            if (info && info->type_param) {
                saved_generic = baseline_test_case_enter_type_params(ctx, (TyList){ .data = &info->type_param, .len = 1 });
            }
            interface_compile_test_info(&my interface, span_null, ctx);
            if (info && info->type_param) baseline_test_case_leave_generic(ctx, saved_generic);
            ctx->class_params_len = outer_class_params_len;
            ctx->member_container = outer_container;
            ctx->member_owner = outer_owner;
        }   break;

        case TYPE_ARRAY:
            type_compile_test_info(my array.element, ctx);
            break;

        case TYPE_TUPLE:
            for_each (i, vec_tuple_element, &my tuple.elements) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case TYPE_INDEXED:
            type_compile_test_info(&my indexed->interface, ctx);
            type_compile_test_info(&my indexed->index, ctx);
            break;

        case TYPE_KEYOF:
            type_compile_test_info(&my keyof->interface, ctx);
            break;

        case TYPE_TYPEOF:
            // (`typeof f<T>`'s type arguments are the type query's)
            bool outer_in_type_query = ctx->in_type_query;
            ctx->in_type_query = true;
            callee_compile_test_info(my typeof_->expr, ctx);
            ctx->in_type_query = outer_in_type_query;
            break;

        case TYPE_TERNARY:
            type_compile_test_info(&my ternary->maybe_sub, ctx);
            type_compile_test_info(&my ternary->maybe_super, ctx);
            type_compile_test_info(&my ternary->if_true, ctx);
            type_compile_test_info(&my ternary->if_false, ctx);
            break;

        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &my composite) {
                type_compile_test_info(i, ctx);
            }
            break;

        case TYPE_TEMPLATE:
            for_each (i, vec_type, &my template_->types) {
                type_compile_test_info(i, ctx);
            }
            break;

        case TYPE_JSDOC: type_compile_test_info(&my jsdoc->type, ctx); break;
        case TYPE_PREDICATE: if (my predicate->has_type) type_compile_test_info(&my predicate->type, ctx); break;
    }
}

// `x is T`: the symbols baseline has the parameter, the types baseline nothing
static void type_predicate_compile_test_info(TypePredicateNode* me, BaselineTestCase* ctx) {
    if (my jsdoc) return;
    if (my symbol) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = baseline_symbol_declaration_spans(my symbol),
            .span = my name,
            .name = my name,
        });
    }
    if (my has_type) type_compile_test_info(&my type, ctx);
}

void function_impl_compile_test_info(FunctionImpl* me, BaselineTestCase* ctx) {
    Span outer_parameter_property_class = ctx->parameter_property_class;
    ctx->parameter_property_class = span_null;
    u32 saved = baseline_test_case_enter_generic(ctx, &my generic);
    if (span_valid(my name)) {
        baseline_test_case_add_declaration(ctx, my name, my symbol);
    }
    generic_compile_test_info(&my generic, ctx);
    for_each (i, vec_function_arg, &my args) {
        if (i->jsdoc_this) continue;
        for_each (decorator, vec_expr, function_arg_decorators(i)) {
            expr_compile_test_info(decorator, ctx);
        }
        Span saved = ctx->parameter_property_class;
        if (i->is_parameter_property && function_impl_rare(me)->constructed_class && span_valid(function_impl_rare(me)->constructed_class->name_span)) {
            ctx->parameter_property_class = function_impl_rare(me)->constructed_class->name_span;
            ctx->parameter_property_symbol = function_impl_rare(me)->constructed_class;
            ctx->parameter_property_arg = i->span;
        }
        initialized_binding_compile_test_info(&i->initializer, ctx);
        ctx->parameter_property_class = saved;
    }
    if (my predicate) {
        type_predicate_compile_test_info(my predicate, ctx);
    } else if (my ret_explicit && !my is_constructor) {
        type_compile_test_info(&my ret, ctx);
    } else if (span_valid(function_impl_rare(me)->constructor_missing_type)) {
        // (A constructor's annotation is an error, TS1093, but still a type node: a missing one is unknownSymbol)
        TypeNode missing = { .tag = TYPE_ANY, .any.unkown_symbol_span = function_impl_rare(me)->constructor_missing_type };
        type_compile_test_info(&missing, ctx);
    }
    block_compile_test_info(&my body, ctx);
    baseline_test_case_leave_generic(ctx, saved);
    ctx->parameter_property_class = outer_parameter_property_class;
}

// An alias shows its own declaration as the symbol and the value of what it resolves to as the type. In a type-only
// `import type`/`export type` (not `* as ns`) its name is a type declaration's to tsc's getTypeOfNode: the declared
// type of what it resolves to, errorType if that isn't a type
static void port_item_add_alias_inner(PortItem const* me, BaselineTestCase* ctx, bool type_declaration);
static void port_item_add_alias(PortItem const* me, BaselineTestCase* ctx, bool type_declaration) {
    ctx->usage_ref = my module_ref;
    port_item_add_alias_inner(me, ctx, type_declaration);
    ctx->usage_ref = nullptr;
}

static void port_item_add_alias_inner(PortItem const* me, BaselineTestCase* ctx, bool type_declaration) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = baseline_symbol_declaration_spans(my symbol),
        .span = my decl_name,
        .text = symbol_text(ctx, my symbol),
    });
    // tsc shows aliases of pure types as any
    // An import/export name (the harness's isImportStatementName, isExportStatementName: not `* as ns`, whose errorType
    // is `error`)
    ctx->any_keeps_rendering = !((my tag == PORT_IMPORT_GATHER && !my is_require) || my tag == PORT_REEXPORT_GATHER);
    // (Only an identifier name: a string one, `export type { foo as "a b" }`, isn't a type declaration name to tsc's
    // getTypeOfNode, and is the alias's value)
    // (An import's string name, TS1003, is a declaration name all the same)
    bool string_name = !my decl_name_missing && my decl_name.size && (my decl_name.buf[0] == '"' || my decl_name.buf[0] == '\'');
    if (type_declaration && !string_name && my tag != PORT_IMPORT_GATHER && my tag != PORT_REEXPORT_GATHER) {
        Symbol const* type_symbol = symbol_for_meaning(my symbol, SYMBOL_TYPE);
        bool declared = type_symbol && !type_symbol->is_placeholder &&
            (type_symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM | SYMBOL_TYPE_ALIAS));
        if (declared) {
            baseline_test_case_add_type(ctx, my decl_name, ty_declared_type_of(current_checker, type_symbol));
        } else {
            baseline_test_case_add_type_node(ctx, my decl_name, &type_any);
        }
        return;
    }
    Symbol const* value = symbol_for_meaning(my symbol, SYMBOL_VALUE);
    if (my tag == PORT_EXPORT_AS_NAMESPACE && (!value || value->is_placeholder)) {
        // Not an import or export statement name to tsc's writer: an alias of no value is errorType (getTypeOfAlias)
        ctx->any_keeps_rendering = false;
        baseline_test_case_add_type(ctx, my decl_name, ty_intrinsic(current_checker, TY_ERROR));
        return;
    }
    baseline_test_case_add_symbol_value(ctx, my decl_name, my symbol);
}

// See BaselineTestCase.shorthand_module
static Symbol const* shorthand_module_of(PortItem const* item, BaselineTestCase* ctx) {
    if (!item->module_ref || !item->symbol) return nullptr;
    Symbol const* module = binder_module_of_ref(item->symbol->binder, item->module_ref);
    if (!module || !module->is_shorthand_module) return nullptr;
    bool local_import = item->tag == PORT_IMPORT_DEFAULT || item->tag == PORT_IMPORT_GATHER || item->tag == PORT_IMPORT_SPECIFIC;
    bool same_file = ctx->shorthand_module == module && span_valid(ctx->shorthand_alias) &&
        span_source(ctx->shorthand_alias) == span_source(item->decl_name);
    if (local_import && !same_file) {
        ctx->shorthand_module = module;
        ctx->shorthand_alias = item->decl_name;
    }
    return module;
}

static void port_statement_compile_test_info_inner(PortStatement const* me, BaselineTestCase* ctx);
void port_statement_compile_test_info(PortStatement const* me, BaselineTestCase* ctx) {
    if (my jsdoc) return;
    ctx->usage_attributes = my attributes;
    port_statement_compile_test_info_inner(me, ctx);
    ctx->usage_attributes = nullptr;
}

static void port_statement_compile_test_info_inner(PortStatement const* me, BaselineTestCase* ctx) {
    shorthand_module_of(&my main, ctx);
    switch (my main.tag) {
        case PORT_IMPORT_DEFAULT:
        case PORT_IMPORT_GATHER:
        case PORT_REEXPORT_GATHER:
        case PORT_EXPORT_AS_NAMESPACE: // (the global alias of the module)
            // Null when the scope refused the import, eg `import a = require("b")` inside an ambient module
            if (my main.symbol) {
                port_item_add_alias(&my main, ctx, my main.type_only);
            } else if (my main.tag == PORT_EXPORT_AS_NAMESPACE) {
                // (Not in a module, TS1314/TS1316: tsc's binder doesn't declare it, its name is errorType)
                baseline_test_case_add_type(ctx, my main.decl_name, ty_intrinsic(current_checker, TY_ERROR));
            }
            break;

        case PORT_IMPORT_ALIAS:
            if (!my main.symbol) {
                ctx->any_keeps_rendering = true;
                baseline_test_case_add_type_node(ctx, my main.decl_name, &type_any);
            } else {
                port_item_add_alias(&my main, ctx, false);
            }
            for_each (i, vec_span, &my main.alias_path) {
                // Part of a qualified name: any is written like any other type, else it's the intrinsic name
                ctx->any_keeps_rendering = vec_span_size(&my main.alias_path) > 1;
                if (i_index >= vec_symbol_cptr_size(&my main.alias_path_symbols)) {
                    // (After an import of an unresolved module: tsc's unknownSymbol, no declarations, and so is every
                    // name after it: resolveEntityName of a qualified name on unknownSymbol)
                    u32 resolved = (u32)vec_symbol_cptr_size(&my main.alias_path_symbols);
                    if (resolved && alias_of_unknown(*vec_symbol_cptr_at(&my main.alias_path_symbols, resolved - 1))) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .allow_declaration_empty = true, .span = *i, .text = unknown_symbol_text(ctx),
                        });
                    }
                    baseline_test_case_add_type_node(ctx, *i, &type_any);
                    continue;
                }
                Symbol const* symbol = *vec_symbol_cptr_at(&my main.alias_path_symbols, i_index);
                Symbol const* decl = symbol_for_meaning(symbol, SYMBOL_ANY_MEANING);
                if (!decl) decl = symbol;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    // (globalThis has no declarations: `Symbol(mod)` named through the alias being declared)
                    .declarations = decl->is_global_this ? vec_span_init() :
                        symbol_declaration_spans(decl->is_placeholder ? decl : shown_declaration(symbol, decl)),
                    .allow_declaration_empty = decl->is_global_this,
                    .span = *i,
                    .text = decl->is_placeholder ? cstr_from_sv(span_sv(decl->name_span)) : symbol_text(ctx, shown_declaration(symbol, decl)),
                });
                // (An alias merged with a namespace, `export import A = a.A; export namespace A {}`: its declared type
                // is the alias's, getDeclaredTypeOfSymbol's getDeclaredTypeOfAlias)
                Symbol const* alias_type = (decl->flags & SYMBOL_ALIAS) && !(decl->flags & SYMBOL_TYPE) && !decl->is_placeholder ?
                    symbol_for_meaning(decl, SYMBOL_TYPE) : nullptr;
                if (alias_type && alias_type != decl && (alias_type->flags & SYMBOL_TYPE) && !alias_type->is_placeholder) {
                    baseline_test_case_add_type(ctx, *i, ty_declared_type_of(current_checker, alias_type));
                } else if ((decl->flags & SYMBOL_ENUM_MEMBER) && !decl->is_placeholder) {
                    // (An enum member's literal type, `E.B`, not a name to reuse)
                    baseline_test_case_add_type(ctx, *i, ty_declared_type_of(current_checker, decl));
                } else if (decl->flags & SYMBOL_TYPE && !decl->is_placeholder) {
                    TypeNode t = type_identifier(c_new(IdentifierType, {
                        .name = decl->name_span,
                        .target = decl,
                    }));
                    baseline_test_case_add_type_node(ctx, *i, &t);
                    type_drop(&t);
                } else if (!decl->is_placeholder && !symbol_for_meaning(decl, SYMBOL_VALUE)) {
                    // Neither a type nor a value (an uninstantiated namespace): errorType (isInRightSideOfImportOrExportAssignment)
                    baseline_test_case_add_type(ctx, *i, ty_intrinsic(current_checker, TY_ERROR));
                } else {
                    baseline_test_case_add_symbol_value(ctx, *i, decl);
                }
            }
            break;

        default: break;
    }
    for_each (i, vec_port_item, &my items) {
        if (!i->symbol) continue;
        Symbol const* shorthand = shorthand_module_of(i, ctx);
        if (i->tag == PORT_IMPORT_GATHER) { // `import d, * as ns`
            port_item_add_alias(i, ctx, false);
            continue;
        }
        if (i->decl_name.buf != i->source_name.buf) {
            // (a string literal name, `{ "<X>" as x }`, is no expression to tsc's walker: no line)
            bool quoted = i->source_name.size && (i->source_name.buf[0] == '"' || i->source_name.buf[0] == '\'');
            if (i->source_symbol && !quoted) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(i->source_symbol),
                    .allow_declaration_empty = i->source_symbol->is_global_this, // (`Symbol(globalThis)`)
                    .span = i->source_name,
                    .text = i->source_symbol->is_placeholder ? cstr_init() : symbol_text(ctx, i->source_symbol),
                });
            } else if (!quoted && csview_equals(span_sv(i->source_name), "default") && i->symbol->alias &&
                i->symbol->alias->target && i->symbol->alias->target->is_module && !shorthand
            ) {
                // A synthetic default (a module without one, allowSyntheticDefaultImports or a CommonJS module from an
                // ES module): the module itself (tsc's resolveExternalModuleSymbol of the `default` it can't find)
                Symbol const* module = i->symbol->alias->target;
                SourceFile const* file = module->module->file;
                VecSpan declarations = vec_span_init();
                if (file) vec_span_push(&declarations, span_from_sv(file, csview_from_n(source_file_start(file), 0)));
                VecSpan own = symbol_declaration_spans(module);
                for_each (k, vec_span, &own) vec_span_push(&declarations, *k);
                vec_span_drop(&own);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declarations,
                    .span = i->source_name,
                    .text = file ? file_module_text(ctx, module) : symbol_text(ctx, module),
                });
            } else if (!quoted && !shorthand && export_equals_property_of(i->symbol)) {
                // A property of what the module's `export =` names: that property's symbol (named by an import of it in
                // scope, see export_equals_property_alias())
                Symbol const* target = export_equals_property_of(i->symbol);
                Checker* checker = current_checker;
                Ty const* assigned = ty_type_of_symbol(checker, target->decls[0].node);
                Prop const* prop = (assigned->flags & TYF_OBJECT) ? ty_get_prop(checker, assigned, checker_atom(checker, target->name)) : nullptr;
                while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
                if (prop && prop->decl->decl_span_count) {
                    PropSymbolInfo info = { .member_count = 1, .first_declaration = prop->decl->decl_spans[0] };
                    Symbol const* alias = export_equals_property_alias(ctx, &info, target->name);
                    VecSpan declarations = vec_span_init();
                    for (u32 k = 0; k < prop->decl->decl_span_count; k++) vec_span_push(&declarations, prop->decl->decl_spans[k]);
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declarations,
                        .span = i->source_name,
                        .text = cstr_from_sv(alias ? alias->name : target->name),
                    });
                }
            } else if (shorthand && !quoted) {
                // (tsc's getExternalModuleMember: any member of a shorthand ambient module is the module)
                bool aliased = ctx->shorthand_module == shorthand && span_valid(ctx->shorthand_alias) &&
                    span_source(ctx->shorthand_alias) == span_source(i->source_name);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(shorthand),
                    .span = i->source_name,
                    .text = aliased ? cstr_from_sv(span_sv(ctx->shorthand_alias)) : symbol_text(ctx, shorthand),
                });
            }
            ctx->any_keeps_rendering = true;
            // (Printed for the specifier, like its alias: tsgo's originalModuleSpecifier of the enclosing ExportSpecifier)
            ctx->usage_ref = i->module_ref;
            if (!quoted) baseline_test_case_add_symbol_value(ctx, i->source_name, i->symbol);
            else ctx->any_keeps_rendering = false;
            ctx->usage_ref = nullptr;
        }
        port_item_add_alias(i, ctx, my main.type_only);
    }
    if (span_valid(port_statement_rare(me)->missing_specifier)) baseline_test_case_add_type(ctx, port_statement_rare(me)->missing_specifier, ty_intrinsic(current_checker, TY_ANY));
    // (A literal there isn't an expression to tsc's writer: isInExpressionContext is false under an import)
    Expr const* bad = port_statement_rare(me)->bad_specifier;
    bool bad_literal = bad && bad->tag == EXPR_LITERAL && *bad->inner_span.buf != '-' &&
        (bad->literal.tag == VALUE_NUMBER || bad->literal.tag == VALUE_BIGINT);
    if (bad && !bad_literal) expr_compile_test_info(port_statement_rare(me)->bad_specifier, ctx);
    // `with { type: "json" }`: the keys written as identifiers are errorType (tsc resolves them as nothing)
    if (my attributes) {
        for_each (i, vec_import_attribute, &my attributes->elements) {
            if (span_valid(i->name) && i->name.buf[0] != '"' && i->name.buf[0] != '\'') {
                baseline_test_case_add_type(ctx, i->name, ty_intrinsic(current_checker, TY_ERROR));
            }
            // (a string, number or bigint literal there is no expression to tsc's isExpressionNode: no line)
            Expr* value = &i->value;
            bool literal = value->tag == EXPR_LITERAL && (value->literal.tag == VALUE_STRING ||
                value->literal.tag == VALUE_NUMBER || value->literal.tag == VALUE_BIGINT);
            if (!literal) expr_compile_test_info(value, ctx);
        }
    }
}

void variable_statement_compile_test_info(VariableStatement const* me, BaselineTestCase* ctx) {
    for_each (i, vec_initialized_binding, &my initializers) {
        TypeNode const* type_literal = ctx->variable_type_literal;
        Span type_literal_name = ctx->variable_type_literal_name;
        if (i->binding.tag == BINDING_IDENTIFIER && i->binding.has_explicit_type &&
            binding_type(&i->binding)->tag == TYPE_INTERFACE && !binding_type(&i->binding)->parenthesized) {
            ctx->variable_type_literal = binding_type(&i->binding);
            ctx->variable_type_literal_name = i->binding.content_span;
        }
        initialized_binding_compile_test_info(i, ctx);
        ctx->variable_type_literal = type_literal;
        ctx->variable_type_literal_name = type_literal_name;
    }
}

void enum_decl_compile_test_info(EnumDecl const* me, BaselineTestCase* ctx) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        // Every declaration of a merged enum
        .declarations = my symbol ? symbol_declaration_spans(my symbol) : vec_span_single(my full_span),
        .span = my name,
    });
    baseline_test_case_add_type(
        ctx, my name,
        my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_unresolved(current_checker, span_sv(my name), (TyList){0})
    );
    for_each (i, vec_enum_case, &my cases) {
        //baseline_test_case_add_declaration(ctx, i->name, i->symbol);
        Span span = i->full_name;
        Symbol const* declaration = i->symbol;
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = symbol_declaration_spans(declaration),
            .span = span,
            // (tsc's symbolToString of the member: an alias of the enum in scope names it, `import b = a.E` gives
            // `b.x`)
            .text = !i->name_is_value && declaration && declaration->parent ? symbol_text(ctx, declaration) :
                member_text(ctx, my symbol, my name, span_sv((i->name_is_value ? i->full_name : declaration->name_span)), true),
        });
        baseline_test_case_add_symbol_value(ctx, span, declaration);
        // `[2] = 2`, `["4"] = 4`: the literal in the computed name names the member too
        if (!i->computed_name && span_valid(i->full_name) && i->full_name.size && i->full_name.buf[0] == '[' && declaration) {
            Span literal = i->name;
            if (i->name_is_value) literal = span_make_in(literal, span_start_ptr(literal) - 1, span_end_ptr(literal) + 1);
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = symbol_declaration_spans(declaration),
                .span = literal,
                .text = !i->name_is_value && declaration->parent ? symbol_text(ctx, declaration) :
                    member_text(ctx, my symbol, my name, span_sv((i->name_is_value ? i->full_name : declaration->name_span)), true),
            });
            // ...and is an expression of its own (isInExpressionContext of a ComputedPropertyName)
            {
                Value value;
                if (i->name_is_value) {
                    value = value_string(cstr_from_sv(span_sv(i->name)));
                } else {
                    cstr text = cstr_from_sv(span_sv(i->name));
                    value = value_number(strtod(cstr_str(&text), nullptr));
                    cstr_drop(&text);
                }
                baseline_test_case_add_type(ctx, literal, ty_literal(current_checker, &value));
                value_drop(&value);
            }
        }
        // (A computed name's expression is checked: tsgo's checkEnumMember)
        if (i->computed_key) expr_compile_test_info(i->computed_key, ctx);
        if (i->value.tag != EXPR_IMPLICIT_ANY) {
            expr_compile_test_info(&i->value, ctx);
        }
    }
}

// The members of a class merge when the first of a name (and staticness) is a method: then its overloads are one
// symbol (a property or accessor first conflicts with the methods, which then stand alone)
static bool overloads_merge(ClassItem const* me, VecClassItem const* items, u32* count) {
    *count = 0;
    bool first_seen = false, merge = false;
    for_each (other, vec_class_item, items) {
        bool is_method = other->tag == CLASS_ITEM_METHOD;
        if (!is_method && other->tag != CLASS_ITEM_FIELD) continue;
        if (is_method ? other->method.computed_key != nullptr : other->field.computed_key != nullptr) continue;
        if (class_item_is_static((ClassItem*)other) != my method.is_static) continue;
        if (!cstr_eq(is_method ? &other->method.name_string : &other->field.name_string, &my method.name_string)) continue;
        if (!first_seen) {
            first_seen = true;
            merge = is_method && !other->method.accessor;
        }
        if (is_method && !other->method.accessor) (*count)++;
    }
    return merge;
}

// A class member's binder name and flags (1 property, 2 method, 4 get, 8 set) for the computed-name grouping below:
// `late` for a computed name that isn't a literal (tsc's late bound names). Null without one
static Atom const* class_member_name(ClassItem const* item, bool* late, u32* flags) {
    Checker* checker = current_checker;
    Expr const* key;
    cstr const* name_string;
    if (item->tag == CLASS_ITEM_FIELD) {
        key = item->field.computed_key;
        name_string = &item->field.name_string;
        *flags = item->field.is_auto_accessor ? 4 | 8 : 1;
    } else if (item->tag == CLASS_ITEM_METHOD) {
        key = item->method.computed_key;
        name_string = &item->method.name_string;
        *flags = item->method.accessor == CLASS_ACCESSOR_GET ? 4 : item->method.accessor == CLASS_ACCESSOR_SET ? 8 : 2;
    } else {
        return nullptr;
    }
    *late = false;
    if (!key) {
        if (span_valid(class_item_private_name(item))) return nullptr;
        return checker_atom(checker, cstr_sv(name_string));
    }
    if (expr_is_perenthisized(key)) return nullptr;
    bool literal = (key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)) ||
        (key->tag == EXPR_TEMPLATE && !key->template_->tag && vec_expr_is_empty(&key->template_->substitutions));
    if (!literal && !expr_is_entity_name(key)) return nullptr;
    *late = !literal;
    bool is_number = false;
    return ty_property_name_of_key(checker, ty_expr_type(checker, key), &is_number);
}

// The members whose declarations one with a computed name shares a symbol with. A literal name is the binder's (like
// a written name: joins the first member of the name unless that one has a meaning it excludes); any other is late
// bound (tsgo's lateBindMember), which does the same among the late bound members of the name: a conflicting one is
// a symbol of its own, and an accessor conflicting with what isn't its other half makes the first symbol both
static u32 computed_member_symbol_items(ClassItem const* me, VecClassItem const* items, ClassItem const** out, u32 cap) {
    bool late;
    u32 flags;
    Atom const* name = class_member_name(me, &late, &flags);
    if (!name) return 0;
    bool is_static = class_item_is_static((ClassItem*)me);
    u32 len = 0;
    // (In order: each member's symbol is the first of its name or itself)
    ClassItem const* firsts[64];
    u32 count = 0;
    ClassItem const* first = nullptr;
    u32 existing = 0;
    ClassItem const* mine = nullptr;
    for (ClassItem const* j = items->data; j < items->data + items->size && count < c_arraylen(firsts); j++) {
        bool other_late;
        u32 other_flags;
        if (class_item_is_static((ClassItem*)j) != is_static) continue;
        if (class_member_name(j, &other_late, &other_flags) != name || other_late != late) continue;
        // (PropertyExcludes, MethodExcludes, Get/SetAccessorExcludes, AccessorExcludes)
        u32 excludes = other_flags == 1 ? 2u : other_flags == 2 ? 1u | 4u | 8u : other_flags == 4 ? 2u | 4u :
            other_flags == 8 ? 2u | 8u : 2u | 4u | 8u;
        ClassItem const* symbol = j;
        if (!first) {
            first = j;
            existing = other_flags;
        } else if (!(existing & excludes)) {
            symbol = first;
            existing |= other_flags;
        } else if ((existing & 12u) && (existing & 12u) != (other_flags & 12u)) {
            existing |= 12u;
        }
        firsts[count] = symbol;
        out[count < cap ? count : cap - 1] = j;
        if (j == me) mine = symbol;
        count++;
    }
    for (u32 k = 0; k < count && k < cap; k++) {
        if (firsts[k] == mine) out[len++] = out[k];
    }
    return len;
}

// A member with a computed name merged with others: a late-bound one (an entity name key) into the early member of its
// name or another late-bound one, a literal one with late-bound ones: the declarations of the checker's member, which
// has them in tsc's order (the early ones first, see resolve_declared_members()), named as its first declaration
// writes it. False when it has no more than `declarations`
static bool late_member_declarations(ClassItem const* me, BaselineTestCase* ctx, VecSpan* declarations, Span* written_name) {
    Expr const* key = me->tag == CLASS_ITEM_METHOD ? me->method.computed_key : me->field.computed_key;
    bool early = key && computed_key_early_name(key, nullptr);
    if (!key || (!early && !expr_is_entity_name(key)) || !ctx->class_symbol) return false;
    Checker* checker = current_checker;
    bool is_number = false;
    Atom const* name = ty_property_name_of_key(checker, ty_expr_type(checker, key), &is_number);
    Ty const* owner = class_item_is_static((ClassItem*)me) ? ty_static_type_of(checker, ctx->class_symbol) :
        ty_declared_type_of(checker, ctx->class_symbol);
    Prop const* prop = name ? ty_get_prop(checker, owner, name) : nullptr;
    while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target; // (Of the instantiated class)
    if (!prop || prop->decl->decl_span_count < 2 || (early && prop->decl->decl_span_count <= (u32)vec_span_size(declarations))) return false;
    bool mine = false;
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) mine |= span_eq(prop->decl->decl_spans[i], me->span);
    if (!mine) return false;
    vec_span_clear(declarations);
    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(declarations, prop->decl->decl_spans[i]);
    if (span_valid(prop->decl->name_span)) *written_name = prop->decl->name_span;
    return true;
}

void class_item_compile_test_info(ClassItem* me, VecClassItem const* items, Span class_name, BaselineTestCase* ctx) {
    for_each (i, vec_expr, &my decorators) {
        expr_compile_test_info(i, ctx);
    }
    switch(my tag) {
        case CLASS_ITEM_CONSTRUCTOR:
            function_impl_compile_test_info(&my constructor.func, ctx);
            break;

        case CLASS_ITEM_METHOD: {
            u32 saved_generic = baseline_test_case_enter_generic(ctx, &my method.func.generic);
            VecSpan declarations = {0};
            Span written_name = my method.name_span;
            ClassItem const* symbol_items[64];
            u32 symbol_count = my method.computed_key ?
                computed_member_symbol_items(me, items, symbol_items, c_arraylen(symbol_items)) :
                class_item_symbol_items(me, items, symbol_items, c_arraylen(symbol_items));
            if (symbol_count > 1 || (symbol_count && !my method.computed_key)) {
                // The declarations of its symbol (tsc's binder: see class_item_symbol_items())
                for (u32 k = 0; k < symbol_count; k++) vec_span_push(&declarations, symbol_items[k]->span);
                ClassItem const* first = symbol_items[0];
                written_name = first->tag == CLASS_ITEM_METHOD ? first->method.name_span : first->field.name_span;
            } else if (my method.accessor && my method.accessor_first && !my method.computed_key) {
                // Both halves of an accessor pair declare the same property. Items live in one vec, so walk forward
                // from the first
                ClassItem* first = my method.accessor_first;
                written_name = first->method.name_span;
                vec_span_push(&declarations, first->span);
                for_each (other, vec_class_item, items) {
                    if (other != first && other->tag == CLASS_ITEM_METHOD && other->method.accessor_first == first) {
                        vec_span_push(&declarations, other->span);
                    }
                }
            } else if (!my method.computed_key && overloads_merge(me, items, &(u32){0})) {
                // Overloads are declarations of one symbol
                for_each (other, vec_class_item, items) {
                    if (other->tag == CLASS_ITEM_METHOD && !other->method.accessor && !other->method.computed_key &&
                        other->method.is_static == my method.is_static && cstr_eq(&other->method.name_string, &my method.name_string)
                    ) {
                        if (vec_span_is_empty(&declarations)) written_name = other->method.name_span;
                        vec_span_push(&declarations, other->span);
                    }
                }
            } else {
                vec_span_push(&declarations, my span);
            }
            // A method or accessor of a class merged with an interface: the member's declarations are both's (the
            // checker's member has them, see builder_add_interface_member)
            if (!my method.computed_key && ctx->class_symbol && !span_valid(class_item_private_name(me))) {
                Checker* checker = current_checker;
                Ty const* owner = my method.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, checker_atom(checker, cstr_sv(&my method.name_string)));
                while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target; // (Of the instantiated class)
                bool mine = false;
                for (u32 i = 0; prop && i < prop->decl->decl_span_count; i++) mine |= span_eq(prop->decl->decl_spans[i], my span);
                if (mine && prop->decl->decl_span_count > (u32)vec_span_size(&declarations)) {
                    vec_span_clear(&declarations);
                    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(&declarations, prop->decl->decl_spans[i]);
                }
            }
            late_member_declarations(me, ctx, &declarations, &written_name);
            // (named as its first declaration writes it)
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my method.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, span_sv(written_name), false),
            }, my method.computed_key);
            // A method is the member's type: all of its overloads, and those of the declarations it merges with
            // (an interface's, overloads with a computed name)
            Ty const* type = class_item_get_type(me);
            if (!my method.accessor && ctx->class_symbol) {
                Checker* checker = current_checker;
                Atom const* name = nullptr;
                if (my method.computed_key) {
                    bool is_number = false;
                    name = ty_property_name_of_key(checker, ty_expr_type(checker, my method.computed_key), &is_number);
                } else {
                    name = checker_atom(checker, cstr_sv(&my method.name_string));
                }
                Ty const* owner = my method.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = name ? ty_get_prop(checker, owner, name) : nullptr;
                // (A single signature prints as declared by the declaration it is: its annotations are reused)
                Ty const* merged = prop && (prop->flags & PROP_METHOD) ? prop_type(checker, prop) : nullptr;
                Prop const* origin = prop;
                while (origin && origin->target) origin = origin->target;
                if (merged && (merged->flags & TYF_OBJECT) &&
                    (ty_members(checker, merged)->call_count > 1 || origin->decl->function != &my method.func)
                ) {
                    type = merged;
                }
            }
            baseline_test_case_add_type(ctx, my method.name_span, optional_member_type(type, my method.is_optional));
            // (Not in scope in its computed name)
            baseline_test_case_leave_generic(ctx, saved_generic);
            if (my method.computed_key) computed_member_name_compile_test_info(my method.computed_key, ctx);
            function_impl_compile_test_info(&my method.func, ctx);
        }   break;

        case CLASS_ITEM_FIELD: {
            ClassItem const* symbol_items[64];
            u32 symbol_count = my field.computed_key ?
                computed_member_symbol_items(me, items, symbol_items, c_arraylen(symbol_items)) :
                class_item_symbol_items(me, items, symbol_items, c_arraylen(symbol_items));
            VecSpan declarations = my field.computed_key || span_valid(class_item_private_name(me)) ? vec_span_init() :
                class_prop_declarations(ctx->class_symbol, my field.is_static, cstr_sv(&my field.name_string), my span, 2);
            // (Not those of the class's members of the name that are symbols of their own: they conflicted, TS2300)
            for (u32 k = 0; !my field.computed_key && k < (u32)vec_span_size(&declarations);) {
                bool other = false;
                for_each (j, vec_class_item, items) {
                    other |= j->symbol_first && j->symbol_first != my symbol_first && span_eq(j->span, declarations.data[k]);
                }
                if (other) vec_span_erase_n(&declarations, k, 1);
                else k++;
            }
            if (vec_span_size(&declarations) == 1) vec_span_clear(&declarations);
            if (vec_span_is_empty(&declarations)) {
                for (u32 k = 0; k < symbol_count; k++) vec_span_push(&declarations, symbol_items[k]->span);
                if (!symbol_count) vec_span_push(&declarations, my span);
            }
            ClassItem const* first = symbol_count ? symbol_items[0] : me;
            Span written_name = first->tag == CLASS_ITEM_METHOD ? first->method.name_span : first->field.name_span;
            late_member_declarations(me, ctx, &declarations, &written_name);
            // (add_member_symbol() takes the declarations)
            bool declared_first_elsewhere = vec_span_size(&declarations) > 1 && !span_eq(declarations.data[0], my span);
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my field.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, span_sv(written_name), false),
            }, my field.computed_key);
            Ty const* type = optional_member_type(class_item_get_type(me), my field.is_optional);
            // (Joined into a symbol something else made first: that symbol's type. A static `prototype` property joins
            // the class's own prototype symbol, which the binder makes first: the instance type)
            // (A computed `[k]` whose literal type is "prototype" too)
            Atom const* name = checker_atom(current_checker, cstr_sv(&my field.name_string));
            // (A `#foo` is the class's own private name)
            if (span_valid(class_item_private_name(me)) && ctx->class_symbol) {
                name = ty_private_name(current_checker, ctx->class_symbol, span_sv(class_item_private_name(me)));
            }
            if (my field.computed_key) {
                bool is_number = false;
                name = ty_property_name_of_key(current_checker, ty_expr_type(current_checker, my field.computed_key), &is_number);
            }
            bool prototype = my field.is_static && name && csview_equals(atom_sv(name), "prototype");
            if ((first != me || prototype) && ctx->class_symbol && name) {
                Checker* checker = current_checker;
                Ty const* owner = my field.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, name);
                if (prop) type = prop_type(checker, prop);
            } else if (declared_first_elsewhere && ctx->class_symbol && name) {
                // (Joined into a symbol an interface merged with the class declared first: that symbol's type)
                Checker* checker = current_checker;
                Ty const* owner = my field.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, name);
                if (prop) type = prop_type(checker, prop);
            }
            baseline_test_case_add_type(ctx, my field.name_span, type);
            if (my field.computed_key) computed_member_name_compile_test_info(my field.computed_key, ctx);

            if (my field.has_explicit_type) {
                type_compile_test_info(&my field.explicit_type, ctx);
            }
            if (my field.value.tag != EXPR_IMPLICIT_ANY) {
                expr_compile_test_info(&my field.value, ctx);
            }
        }   break;

        case CLASS_ITEM_INTERFACE_MEMBER:
            interface_member_compile_test_info(&my interface_memeber, class_name, ctx);
            break;

        case CLASS_ITEM_STATIC_BLOCK:
            block_compile_test_info(&my static_block, ctx);
            break;
    }
}

// An expression where a heritage clause means a type (`implements A?.B`, `interface I extends f()`), which the
// checker rejected: tsc's walk takes the property accesses on top of it for type nodes (isPartOfTypeNode), which
// get no types line, what's under them is an expression like any other
static void heritage_invalid_compile_test_info(Expr* expr, BaselineTestCase* ctx) {
    Expr* e = expr->tag == EXPR_GENERIC_PARAMS ? &expr->generic_params->expr : expr;
    u32 saved = ctx->hidden_type_span_count;
    while (e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT && ctx->hidden_type_span_count + 2 <= c_arraylen(ctx->hidden_type_spans)) {
        ctx->hidden_type_spans[ctx->hidden_type_span_count++] = e->inner_span;
        ctx->hidden_type_spans[ctx->hidden_type_span_count++] = property_key_span(e->property);
        e = &e->property->expr;
    }
    expr_compile_test_info(expr, ctx);
    ctx->hidden_type_span_count = saved;
}

static void heritage_type_compile_test_info(TypeNode const* type, BaselineTestCase* ctx) {
    ctx->heritage_type = type->tag == TYPE_IDENTIFIER;
    type_compile_test_info(type, ctx);
    ctx->heritage_type = false;
}

// An entity name `A.B.C` a class extends that the checker couldn't make a base of: tsc's getSymbolAtLocation resolves
// it as an entity name (isHeritageClauseElementIdentifier): every name but the last with a namespace meaning (`M1` of
// `M1.A` is the namespace even where a variable M1 shadows it), the whole with a value meaning. Overrides the lines
// of what resolves, see add_symbol_override()
static void heritage_entity_overrides(Expr* base, BaselineTestCase* ctx) {
    Expr* nodes[16];
    u32 n = 0;
    Expr* e = base;
    while (e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT && !e->property->optional_chain &&
        !expr_is_perenthisized(e) && n < c_arraylen(nodes)
    ) {
        nodes[n++] = e;
        e = &e->property->expr;
    }
    if (!n || e->tag != EXPR_IDENTIFIER || expr_is_perenthisized(e) || e->identifier.keyword || !ctx->enclosing) return;
    Symbol const* found = scope_resolve(ctx->enclosing, span_sv(e->inner_span), SYMBOL_NAMESPACE);
    if (alias_of_unknown(found)) {
        // (An import of an unresolved module: the names after it are tsc's unknownSymbol, see unknown_symbol_text())
        for (u32 k = n; k-- > 0 && ctx->symbol_override_count + 2 <= c_arraylen(ctx->symbol_overrides);) {
            Span spans[2] = { nodes[k]->inner_span, property_key_span(nodes[k]->property) };
            for (u32 j = 0; j < 2; j++) {
                ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
                    .span = spans[j], .text = unknown_symbol_text(ctx), .declarations = vec_span_init(),
                };
            }
        }
        return;
    }
    if (!found || found->is_placeholder || !symbol_for_meaning(found, SYMBOL_NAMESPACE)) return;
    if (!e->identifier.target && !e->identifier.namespace_as_value && !e->identifier.invalid_initializer_reference) {
        // (Not a value: the identifier shows nothing of its own, `M` of `extends M.C` with an uninstantiated M)
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(found),
            .span = e->inner_span,
            .text = symbol_text(ctx, found),
        });
    } else if (found != e->identifier.symbol) {
        add_symbol_override(ctx, e->inner_span, found);
    }
    for (u32 k = n; k-- > 0;) {
        Symbol const* container = symbol_for_meaning(found, SYMBOL_NAMESPACE);
        if (!container || container->is_placeholder) return;
        csview name = span_sv(property_key_span(nodes[k]->property));
        Symbol const* next = symbol_get_export(container, name);
        if (!next || !symbol_for_meaning(next, k ? SYMBOL_NAMESPACE : SYMBOL_VALUE)) return;
        add_symbol_override(ctx, nodes[k]->inner_span, next);
        add_symbol_override(ctx, property_key_span(nodes[k]->property), next);
        found = next;
    }
}

// An element of a class's `extends` clause: the base type (instance) if there is one, else the expression's
// `outside`: the type parameters in scope outside the class, `class_params` its own: they aren't in scope in the base
// class expression (tsc's resolveNameHelper at an ExpressionWithTypeArguments' expression), only in its type arguments
static void class_extends_compile_test_info(Expr* extends, Ty const* instance, u32 outside, TyList class_params,
    BaselineTestCase* ctx) {
    bool generic = extends->tag == EXPR_GENERIC_PARAMS;
    Expr* base = generic ? &extends->generic_params->expr : extends;
    u32 overrides = ctx->symbol_override_count;
    if (!instance || (instance->flags & TYF_ANY)) heritage_entity_overrides(base, ctx);
    if (instance && !(instance->flags & TYF_ANY)) {
        // (Only the clause's expression itself is the base type: what's parenthesized in it is an expression like
        // any other, `class D extends (await p)` has `await p : typeof C`)
        baseline_test_case_add_type_recursive(ctx, expr_outer_span(base), expr_outer_span(base), instance);
        if (!span_eq(expr_outer_span(base), base->inner_span)) {
            const char* start = span_start_ptr(expr_outer_span(base));
            const char* end = span_end_ptr(expr_outer_span(base));
            while (start < end && *start != '(') start++;
            while (end > start && end[-1] != ')') end--;
            if (end - start >= 2) {
                Span inside = span_make_in(expr_outer_span(base), start + 1, end - 1);
                baseline_test_case_add_type_recursive(ctx, inside, base->inner_span, ty_regular(current_checker, expr_get_type(base)));
            }
        }
        baseline_test_case_leave_generic(ctx, outside);
        expr_compile_test_info_inner(base, ctx);
    } else {
        baseline_test_case_leave_generic(ctx, outside);
        expr_compile_test_info(base, ctx);
    }
    baseline_test_case_enter_type_params(ctx, class_params);
    while (ctx->symbol_override_count > overrides) {
        ctx->symbol_override_count--;
        cstr_drop(&ctx->symbol_overrides[ctx->symbol_override_count].text);
        vec_span_drop(&ctx->symbol_overrides[ctx->symbol_override_count].declarations);
    }
    if (generic) {
        for_each (i, vec_spanned_type, &extends->generic_params->params) type_compile_test_info(&i->type, ctx);
    }
}

// A type parameter of a class or interface and its instance member of the same name are one symbol in tsc (both in
// the members table), named as the type parameter: see BaselineTestCase.merged_declarations
static void register_type_parameter_member_merges(Symbol const* owner, Generic const* generic, BaselineTestCase* ctx) {
    if (!owner) return;
    Ty const* declared = nullptr;
    for_each (i, vec_generic_item, &generic->items) {
        if (i->jsdoc || !i->owned_symbol || ctx->merged_declaration_count >= c_arraylen(ctx->merged_declarations)) continue;
        Checker* checker = current_checker;
        if (!declared) declared = ty_declared_type_of(checker, owner);
        if (!declared || !(declared->flags & TYF_OBJECT)) return;
        Prop const* prop = ty_get_prop(checker, declared, checker_atom(checker, span_sv(i->name)));
        while (prop && prop->target && !prop->decl->decl_span_count) prop = prop->target;
        if (!prop || prop->parent != owner || !prop->decl->decl_span_count) continue;
        VecSpan spans = baseline_symbol_declaration_spans(i->owned_symbol);
        bool seen = false;
        for_each (j, vec_span, &spans) seen |= span_eq(*j, prop->decl->decl_spans[0]);
        if (seen) {
            vec_span_drop(&spans);
            continue;
        }
        for (u32 j = 0; j < prop->decl->decl_span_count; j++) vec_span_push(&spans, prop->decl->decl_spans[j]);
        ctx->merged_declarations[ctx->merged_declaration_count].name = span_sv(i->name);
        ctx->merged_declarations[ctx->merged_declaration_count++].declarations = spans;
    }
}

void class_decl_compile_test_info(ClassDecl* me, bool is_expr, BaselineTestCase* ctx) {
    for_each (i, vec_expr, &my decorators) {
        expr_compile_test_info(i, ctx);
    }
    symbol_chain_enter_class(me);
    // (Those of the merged declarations: one symbol each, like tsc's)
    TyList class_params = my symbol ? ty_declared_type_of(current_checker, my symbol)->declared.type_params :
        ty_type_params_of(current_checker, &my generic);
    u32 saved_generic = baseline_test_case_enter_type_params(ctx, class_params);
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
            .name = default_export_name(my symbol),
            .text = baseline_test_case_module_alias_text(ctx, my symbol),
        });
        if (is_expr) {
            baseline_test_case_add_symbol_value(ctx, my name, my symbol);
        } else {
            baseline_test_case_add_type(ctx, my name, ty_class_instance_type(current_checker, my symbol));
        }
    }
    register_type_parameter_member_merges(my symbol, &my generic, ctx);
    generic_compile_test_info(&my generic, ctx);
    // tsc shows the base type (getBaseTypes, with its arguments) at the base expression, not `typeof Base`, unless
    // there is none or it's `any`; then the name's parts. At every element of every `extends` clause
    Ty const* instance = my extends.tag == EXPR_IMPLICIT_ANY ? nullptr : ty_class_base_instance(current_checker, me);
    // (A class that is its own base has none: then it's the expression's type, like any other)
    if (instance && my symbol && ty_has_base_type(current_checker, instance, ty_declared_type_of(current_checker, my symbol))) {
        instance = nullptr;
    }
    // (With the wrong number of type arguments the reference is errorType and no base: the first base of an interface
    // merged into the class is, else the harness replaces errorType with the expression's type. tsc's getBaseTypes)
    if (my extends.tag != EXPR_IMPLICIT_ANY && !class_decl_base_arity_fits(me)) instance = class_decl_merged_interface_base(me);
    // (A reference is the base with the class's `this` as its this-argument to tsc's type writer, a new reference
    // without the alias it was written through: getTypeWithThisArgument. `extends Constructor<T3>()` with `type T3 =
    // number[]` shows number[])
    if (instance && instance->kind == TY_REFERENCE && instance->alias) {
        instance = ty_reference(current_checker, instance->reference.target, ty_reference_args(current_checker, instance));
    }
    if (my extends.tag != EXPR_IMPLICIT_ANY) class_extends_compile_test_info(&my extends, instance, saved_generic, class_params, ctx);
    for_each (i, vec_spanned_type, &my implements) heritage_type_compile_test_info(&i->type, ctx);
    for_each (i, vec_expr, &my unchecked_extends) class_extends_compile_test_info(i, instance, saved_generic, class_params, ctx);
    for_each (i, vec_type, &my unchecked_implements) heritage_type_compile_test_info(i, ctx);
    for_each (i, vec_expr, &my invalid_implements) heritage_invalid_compile_test_info(i, ctx);
    Symbol const* outer_class = ctx->class_symbol;
    Symbol const* outer_container = ctx->member_container;
    ctx->class_symbol = my symbol;
    ctx->member_container = my symbol;
    TyList outer_class_params = { ctx->class_params, ctx->class_params_len };
    u32 outer_class_params_outside = ctx->class_params_outside;
    ctx->class_params = class_params.data;
    ctx->class_params_len = class_params.len;
    ctx->class_params_outside = saved_generic;
    for_each (i, vec_class_item, &my items) {
        // (The class's type parameters aren't in scope in a static member)
        bool is_static = class_item_is_static(i);
        if (is_static) baseline_test_case_leave_generic(ctx, saved_generic);
        class_item_compile_test_info(i, &my items, is_expr ? class_decl_display_name(me) : my name, ctx);
        if (is_static) baseline_test_case_enter_type_params(ctx, class_params);
    }
    ctx->class_symbol = outer_class;
    ctx->member_container = outer_container;
    ctx->class_params = outer_class_params.data;
    ctx->class_params_len = outer_class_params.len;
    ctx->class_params_outside = outer_class_params_outside;
    baseline_test_case_leave_generic(ctx, saved_generic);
    symbol_chain_leave_class();
}

void type_decl_compile_test_info(TypeDecl const* me, BaselineTestCase* ctx) {
    if (my jsdoc) return;
    u32 saved_generic = baseline_test_case_enter_generic(ctx, &my generic);
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declaration_get_spans(my symbol),
        .span = my name,
        .text = baseline_test_case_module_alias_text(ctx, my symbol),
    });
    // The declared type as the new core knows it, which carries the alias name where tsc would
    Ty const* declared = my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_intrinsic(current_checker, TY_ANY);
    if (my type.tag == TYPE_IDENTIFIER && my type.identifier->intrinsic_keyword && (declared->flags & TYF_ANY)) {
        // `type X = intrinsic` that isn't one of the lib's: tsc's intrinsicMarkerType, an any named `intrinsic`
        baseline_test_case_add_type_text(ctx, my name, c_sv("intrinsic"));
    } else {
        baseline_test_case_add_type(ctx, my name, declared);
    }
    generic_compile_test_info(&my generic, ctx);
    type_compile_test_info(&my type, ctx);
    baseline_test_case_leave_generic(ctx, saved_generic);
}

void interface_decl_compile_test_info(InterfaceDecl const* me, BaselineTestCase* ctx) {
    TyList params = my symbol ? ty_declared_type_of(current_checker, my symbol)->declared.type_params :
        ty_type_params_of(current_checker, &my generic);
    u32 saved_generic = baseline_test_case_enter_type_params(ctx, params);
    TyList outer_class_params = { ctx->class_params, ctx->class_params_len };
    u32 outer_class_params_outside = ctx->class_params_outside;
    ctx->class_params = params.data;
    ctx->class_params_len = params.len;
    ctx->class_params_outside = saved_generic;
    if (span_valid(my name)) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(my symbol),
            .span = my name,
            .name = default_export_name(my symbol),
            .text = baseline_test_case_module_alias_text(ctx, my symbol),
        });
    }
    register_type_parameter_member_merges(my symbol, &my generic, ctx);
    generic_compile_test_info(&my generic, ctx);
    for_each (i, vec_spanned_type, &my extends) heritage_type_compile_test_info(&i->type, ctx);
    for_each (i, vec_type, &my unchecked_extends) heritage_type_compile_test_info(i, ctx);
    for_each (i, vec_expr, &my invalid_extends) heritage_invalid_compile_test_info(i, ctx);
    Ty const* outer_owner = ctx->member_owner;
    ctx->member_owner = my symbol ? ty_declared_type_of(current_checker, my symbol) : nullptr;
    Symbol const* outer_container = ctx->member_container;
    ctx->member_container = my symbol;
    interface_compile_test_info(&my type.interface, my name, ctx);
    ctx->member_container = outer_container;
    ctx->member_owner = outer_owner;
    ctx->class_params = outer_class_params.data;
    ctx->class_params_len = outer_class_params.len;
    ctx->class_params_outside = outer_class_params_outside;
    baseline_test_case_leave_generic(ctx, saved_generic);
}

void namespace_decl_compile_test_info(NamespaceDecl const* me, BaselineTestCase* ctx) {
    if (my is_global && span_valid(my name)) {
        // `declare global`: a namespace named `global` of its own in tsc, its value `typeof global` if it has one
        // (Every `declare global` at the top of the file: one symbol in its locals)
        VecSpan declarations = vec_span_init();
        SourceFile const* file = span_source(my name);
        bool top = false;
        for_each (i, vec_statement, &file->ast.statements) {
            if (i->tag != STATEMENT_DECL_NAMESPACE || !i->decl.namespace->is_global) continue;
            vec_span_push(&declarations, i->decl.namespace->full_span);
            top |= i->decl.namespace == me;
        }
        if (!top) {
            vec_span_clear(&declarations);
            vec_span_push(&declarations, my full_span);
        }
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = declarations, .span = my name });
        if (namespace_decl_is_instantiated(me)) {
            baseline_test_case_add_type_text(ctx, my name, c_sv("typeof global"));
        } else {
            ctx->any_keeps_rendering = true;
            baseline_test_case_add_type_node(ctx, my name, &type_any);
        }
    }
    // (tsc names the declaration from itself: its name's parent. A namespace shadowing it inside is `globalThis.M`)
    Scope const* outer = ctx->enclosing;
    if (my inner_scope) ctx->enclosing = my inner_scope;
    if (span_valid(my name) && my symbol) {
        // An augmentation of a file is that file's module symbol: named by its path, declared by the whole file
        // first
        SourceFile const* file = my symbol->is_module ? my symbol->module->file : nullptr;
        VecSpan decls = vec_span_init();
        cstr file_name = cstr_init();
        if (file) {
            vec_span_push(&decls, span_from_sv(file, csview_from_n(source_file_start(file), 0)));
            file_name = file_module_text(ctx, my symbol);
        }
        VecSpan own = file ? baseline_symbol_declaration_spans(my symbol) : declaration_get_spans(my symbol);
        for_each (i, vec_span, &own) vec_span_push(&decls, *i);
        vec_span_drop(&own);
        // An ambient module's name is its string in double quotes
        // (an augmentation of an `export =` module declares into the exported symbol: named like any symbol)
        if (!file && my is_ambient_module && my module_ref) {
            // (An alias of it in scope names it, tsc's getAccessibleSymbolChain: `import * as Foo` in the same block)
            if (my symbol->is_module) {
                Scope const* inner = ctx->enclosing;
                ctx->enclosing = outer;
                file_name = symbol_text(ctx, my symbol);
                ctx->enclosing = inner;
                if (cstr_size(&file_name) && strchr("\"'", cstr_str(&file_name)[0])) cstr_clear(&file_name);
            }
            if (cstr_size(&file_name)) {
            } else if (my symbol->is_module) {
                // (its value, escaped like a string literal: `".\\m"`)
                file_name = cstr_lit("\"");
                csview path = cstr_sv(&my module_ref->path);
                for (isize k = 0; k < path.size; k++) {
                    if (path.buf[k] == '\\' || path.buf[k] == '"') cstr_push(&file_name, "\\");
                    cstr_append_n(&file_name, &path.buf[k], 1);
                }
                cstr_push(&file_name, "\"");
            } else {
                Scope const* inner = ctx->enclosing;
                ctx->enclosing = outer;
                file_name = symbol_text(ctx, my symbol);
                ctx->enclosing = inner;
            }
        }
        // (A namespace's own chain: an alias of its container in scope names it, `import ma = a` in `a.b` gives
        // `ma.b`)
        if (!cstr_size(&file_name) && !file && !my is_ambient_module && !my symbol->is_module) {
            file_name = symbol_text(ctx, my symbol);
        }
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = decls,
            .span = my name,
            .name = cstr_size(&file_name) ? sv_make_span(cstr_sv(&file_name)) : span_null,
        });
        cstr_drop(&file_name);
        // (Only if this declaration is a value: tsc's type writer asks getMeaningFromDeclaration)
        if (my is_invalid_global && my is_shorthand) {
            // (`global x`: a shorthand ambient module to tsc, any)
            ctx->any_keeps_rendering = true;
            baseline_test_case_add_type_node(ctx, my name, &type_any);
        } else if (my is_augmentation && my module_ref && !binder_module_of_ref(my symbol->binder, my module_ref) &&
            !namespace_decl_is_instantiated(me)
        ) {
            // (An augmentation is a namespace to tsc's binder, a value only if instantiated: unmerged, its symbol has no
            // type)
            baseline_test_case_add_type(ctx, my name, ty_intrinsic(current_checker, TY_ERROR));
        } else if ((my symbol->flags & SYMBOL_VALUE) && namespace_decl_declares_value(me)) {
            baseline_test_case_add_symbol_value(ctx, my name, my symbol);
        } else if (my is_augmentation && !(my symbol->flags & SYMBOL_VALUE)) {
            // (An augmentation merged into an `export =` namespace without a value: getTypeOfSymbol of a namespace
            // is errorType)
            baseline_test_case_add_type(ctx, my name, ty_intrinsic(current_checker, TY_ERROR));
        }
    }
    if (my attributes) {
        Scope const* inner = ctx->enclosing;
        ctx->enclosing = outer;
        type_compile_test_info(my attributes, ctx);
        ctx->enclosing = inner;
    }
    block_compile_test_info(&my contents, ctx);
    ctx->enclosing = outer;
}

// tsc's getSymbolAtLocation of `this`: the `this` parameter of the function (declared, or of the contextual signature
// it was typed by), else the symbol of the type of `this` (globalThis at the top of a script, the class)
static void this_compile_test_info(IdentifierExpr* me, BaselineTestCase* ctx) {
    Checker* checker = current_checker;
    if (my this_kind == THIS_GLOBAL) {
        // (Declared by the JavaScript `globalThis.x = v` that give it expandos)
        Symbol const* global_this = checker->binder->global_this;
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = global_this ? declaration_get_spans(global_this) : vec_span_init(), .span = identifier_expr_name(me),
            .allow_declaration_empty = true, .text = cstr_lit("globalThis"),
        });
        return;
    }
    Symbol const* symbol = my target;
    if (!symbol && my this_kind == THIS_ANY && identifier_this_function(me) && function_impl_rare(identifier_this_function(me))->paired_this) {
        symbol = function_impl_rare(identifier_this_function(me))->paired_this->initializer.binding.symbol;
    }
    if (!symbol && my this_kind == THIS_ANY && identifier_this_function(me)) {
        Signature const* sig = ty_function_assigned_contextual_signature(checker, identifier_this_function(me));
        // (A union's contextual signature: the `this` of the member it was made from, declared_by)
        while (sig && (sig->target || (!sig->function && sig->declared_by))) sig = sig->target ? sig->target : sig->declared_by;
        FunctionImpl const* function = sig ? sig->function : nullptr;
        // (Only a function expression's: a declaration's signature is its own, getSignatureFromDeclaration, which a
        // JSDoc `@type` gives no `this` parameter; its `this` is the symbol of its type)
        FunctionArg const* first = function && identifier_this_function(me)->is_expression && vec_function_arg_size(&function->args) ?
            vec_function_arg_at(&function->args, 0) : nullptr;
        if (first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
            span_equals(first->initializer.binding.content_span, "this")
        ) {
            symbol = first->initializer.binding.symbol;
        }
        Ty const* type = symbol ? nullptr : ty_contextual_this_type(checker, identifier_this_function(me));
        Expr const* literal = function_impl_rare(identifier_this_function(me))->this_literal;
        // (`o.m = function () { this }` of an object literal o: that literal's)
        if (!literal && function_impl_rare(identifier_this_function(me))->this_assigned_object && type &&
            type->kind == TY_ANONYMOUS && type->anonymous.object_literal
        ) {
            literal = type->anonymous.literal_expr;
        }
        if (type && type->kind == TY_ANONYMOUS && type->anonymous.object_literal && literal) {
            // The object literal's own type (noImplicitThis): its symbol, `__object` or the variable's name
            bool named = literal == ctx->variable_literal;
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(literal->inner_span),
                .span = identifier_expr_name(me),
                .text = named ? cstr_from_sv(span_sv(ctx->variable_literal_name)) : cstr_lit("__object"),
            });
            return;
        }
        // A type literal (`let p: Point = { m() { this } }` with `type Point = {...}`): its anonymous symbol `__type`
        Ty const* literal_type = type;
        while (literal_type && literal_type->kind == TY_ANONYMOUS && !literal_type->anonymous.node && literal_type->anonymous.target) {
            literal_type = literal_type->anonymous.target;
        }
        if (!symbol && literal_type && literal_type->kind == TY_ANONYMOUS && literal_type->anonymous.node &&
            literal_type->anonymous.node->tag == TYPE_INTERFACE && !literal_type->anonymous.object_literal &&
            span_valid(literal_type->anonymous.node->interface.span)
        ) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(literal_type->anonymous.node->interface.span),
                .span = identifier_expr_name(me),
                .text = literal_type->anonymous.node == ctx->variable_type_literal ?
                    cstr_from_sv(span_sv(ctx->variable_type_literal_name)) : cstr_lit("__type"),
            });
            return;
        }
        if (!symbol) {
            // (`this.m = function () { this }`: the `this` of the constructor, the class)
            if (type && type->kind == TY_TYPE_PARAM && type->param.state->is_this) type = ty_apparent_type(checker, type);
            if (type && type->kind == TY_REFERENCE) type = type->reference.target;
            if (type && type->kind == TY_DECLARED) symbol = type->declared.symbol;
            // (`o.m = function () { this }` where o is a namespace value, a function with expandos: its symbol)
            if (type && type->kind == TY_DECLARED && !symbol && type->declared.kind == TY_DECLARED_NAMESPACE) {
                symbol = type->declared.namespace_symbol;
                // (A `{}` container's: the object literal's symbol in tsgo, named by its variable)
                SymbolDecl const* decl = symbol && symbol->decl_count ? &symbol->decls[0] : nullptr;
                Binding const* binding = decl && decl->kind == SYMBOL_DECL_VARIABLE ? decl->node : nullptr;
                Expr const* init = binding && !binding->parent && binding->tag == BINDING_IDENTIFIER ?
                    initialized_binding_value((InitializedBinding*)binding) : nullptr;
                if (init && init->tag == EXPR_OBJECT && init->expando_container) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = vec_span_single(init->inner_span), .span = identifier_expr_name(me),
                        .text = cstr_from_sv(span_sv(decl->name)),
                    });
                    return;
                }
            }
        }
    }
    // (A function expression's type, `this` of `module.exports.F = function () { this }` when module.exports is one:
    // its symbol, `__function` named as tsc's getNameOfSymbolAsWritten names it, by getAssignedName)
    if (!symbol && my this_kind == THIS_ANY && identifier_this_function(me)) {
        Ty const* type = ty_contextual_this_type(checker, identifier_this_function(me));
        FunctionImpl const* function = type && type->kind == TY_ANONYMOUS && !type->anonymous.target ? type->anonymous.function : nullptr;
        if (function && function->is_expression && !function_impl_is_arrow(function) && span_valid(function->full_span)) {
            Span name = span_valid(function->name) ? function->name : function_impl_rare(function)->assigned_name;
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(function->full_span), .span = identifier_expr_name(me),
                .text = span_valid(name) ? cstr_from_sv(span_sv(name)) : cstr_lit("__function"),
            });
            return;
        }
    }
    // (A file's module, `this` of `module.exports.F = function () { this }`: declared by the file, at its start)
    if (symbol && !symbol->decl_count && symbol->is_module && symbol->module && symbol->module->file) {
        SourceFile const* file = symbol->module->file;
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_single(span_from_sv(file, csview_from_n(source_file_start(file), 0))),
            .span = identifier_expr_name(me), .text = module_text(ctx, symbol),
        });
        return;
    }
    if (!symbol || !symbol->decl_count) return;
    // The `this` parameter of a JSDoc `@this {T}` (its name is the tag's): tsgo's reparsed parameter, a missing name
    // at the tag's
    Span first = symbol->decls[0].name;
    if (symbol->is_parameter && span_valid(first) && span_source(first) && first.buf > source_file_start(span_source(first)) &&
        first.buf[-1] == '@'
    ) {
        // (A `@callback`'s `@this` is the parameter's name node itself in tsgo: named by the tag's text, from its `@`
        // through the line's end)
        char const* file_start = source_file_start(span_source(first));
        char const* comment = first.buf - 1;
        while (comment > file_start + 1 && !(comment[-1] == '*' && comment[-2] == '/')) comment--;
        if (csview_contains(csview_from_range(comment, first.buf), "@callback")) {
            char const* end = first.buf;
            while (*end && *end != '\n' && !(end[0] == '*' && end[1] == '/')) end++;
            if (*end == '\n') end++;
            Span tag = span_make(span_source(first), first.buf - 1, end);
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(tag), .span = identifier_expr_name(me), .text = cstr_from_sv(span_sv(tag)),
            });
            return;
        }
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_single(first), .span = identifier_expr_name(me), .text = cstr_lit("(Missing)"),
        });
        return;
    }
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declaration_get_spans(symbol),
        .span = identifier_expr_name(me),
        .text = symbol_text(ctx, symbol),
    });
}

// The declarations of an object literal member's symbol (the checker's binding, ty_object_literal_member_symbol()),
// *first its first member (whose value types it)
static VecSpan object_member_symbol_decls(Expr const* object, ObjectExprItem const* item, ObjectExprItem const** first) {
    return ty_object_literal_member_symbol(current_checker, object, item, first);
}

// `new.target` in a function expression: the function's symbol (named like tsc's getNameOfSymbolAsWritten: the
// variable it initializes, else `(Anonymous function)`), declared by the expression
static void new_target_function_symbol(IdentifierExpr const* me, Span span, BaselineTestCase* ctx) {
    FunctionImpl const* function = identifier_this_function(me);
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = vec_span_single(function->full_span),
        .span = span,
        .text = span_valid(function_impl_variable_name(function)) ? cstr_from_sv(span_sv(function_impl_variable_name(function))) : cstr_lit("(Anonymous function)"),
    });
}

void identifier_expr_compile_test_info(IdentifierExpr* me, BaselineTestCase* ctx) {
    if (my keyword == KEYWORD_IDENTIFIER_NEW_TARGET && !my target && identifier_this_function(me)) {
        new_target_function_symbol(me, identifier_expr_name(me), ctx);
    }
    if (my keyword == KEYWORD_IDENTIFIER_THIS) {
        this_compile_test_info(me, ctx);
        return;
    }
    if (my keyword == KEYWORD_IDENTIFIER_SUPER) {
        // The base class (target is the class `super` is in), in a static member the base constructor's type
        ClassDecl const* class_decl = my target ? symbol_find_node(my target, SYMBOL_DECL_CLASS) : nullptr;
        Ty const* base = !class_decl || class_decl->extends.tag == EXPR_IMPLICIT_ANY ? nullptr :
            my this_kind == THIS_CLASS_STATIC ? ty_expr_type(current_checker, &class_decl->extends) :
            ty_class_base_instance(current_checker, class_decl);
        // (For a class that is its base constructor's type that's the same symbol, not for a mixin's `T`)
        // (Any base expression: `extends getBase()`, `extends getBase()<number>`)
        Expr const* base_expr = class_decl && class_decl->extends.tag == EXPR_GENERIC_PARAMS ?
            &class_decl->extends.generic_params->expr : class_decl ? &class_decl->extends : nullptr;
        if (base && my super_call && base_expr) {
            Ty const* constructor = ty_expr_type(current_checker, base_expr);
            if (constructor->flags & TYF_TYPE_PARAM) base = constructor;
            // A base constructor declared as an interface's value (`declare var Set: SetConstructor`): that interface
            // (tsc's getSymbolAtLocation of `super()`, the symbol of the base constructor type)
            Ty const* declared = constructor->kind == TY_REFERENCE ? constructor->reference.target : constructor;
            if (declared->kind == TY_DECLARED && declared->declared.symbol && (declared->declared.symbol->flags & SYMBOL_INTERFACE) &&
                !(declared->declared.symbol->flags & SYMBOL_CLASS)
            ) {
                base = declared;
            }
        }
        // A base constructor typed by a type literal (`class {} as new () => T`): that literal's `__type`
        if (base && my super_call) {
            Ty const* constructor = ty_expr_type(current_checker, &class_decl->extends);
            while (constructor->kind == TY_ANONYMOUS && !constructor->anonymous.node && constructor->anonymous.target) {
                constructor = constructor->anonymous.target;
            }
            TypeNode const* node = constructor->kind == TY_ANONYMOUS ? constructor->anonymous.node : nullptr;
            Span declared_at = !node ? span_null : node->tag == TYPE_INTERFACE ? node->interface.span :
                node->tag == TYPE_CALLABLE ? node->callable->function->full_span : span_null;
            if (span_valid(declared_at)) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = vec_span_single(declared_at),
                    .span = identifier_expr_name(me),
                    .text = cstr_lit("__type"),
                });
                return;
            }
        }
        // (A base reference with the wrong number of type arguments isn't a base, but an interface merged into the
        // class may still give it one: then `super()` is the base constructor's, tsc's checkSuperExpression)
        if (!base && my super_call && base_expr && base_expr->tag == EXPR_IDENTIFIER && my this_kind != THIS_CLASS_STATIC &&
            !class_decl_base_arity_fits(class_decl) && class_decl_merged_interface_base(class_decl) &&
            base_expr->identifier.target && (base_expr->identifier.target->flags & SYMBOL_CLASS)
        ) {
            base = ty_declared_type_of(current_checker, base_expr->identifier.target);
        }
        if (base && base->kind == TY_REFERENCE) base = base->reference.target;
        Symbol const* symbol = base && base->kind == TY_DECLARED ? base->declared.symbol :
            base && base->kind == TY_TYPE_PARAM ? base->param.symbol : nullptr; // (A mixin's `T`)
        // (A mixin, a class expression that extends a type parameter's value, is an intersection to tsc: no symbol)
        ClassDecl const* base_decl = symbol ? symbol_find_node(symbol, SYMBOL_DECL_CLASS) : nullptr;
        if (base_decl && base_decl->is_expression && base_decl->extends.tag != EXPR_IMPLICIT_ANY &&
            ty_expr_type(current_checker, &base_decl->extends)->flags & TYF_TYPE_PARAM
        ) {
            symbol = nullptr;
        }
        if (symbol && symbol->decl_count) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(symbol),
                .span = identifier_expr_name(me),
                .text = symbol_text(ctx, symbol),
            });
        }
        return;
    }
    if (my require_call && my target && csview_equals(my target->name, "require") && !span_valid(my target->name_span)) {
        // tsc's requireSymbol, which has no declaration
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = identifier_expr_name(me), .allow_declaration_empty = true });
        return;
    }
    // (A name an instance field can't use still shows what it finds)
    Symbol const* target = my target ? my target :
        my invalid_initializer_reference ? symbol_for_meaning(my symbol, SYMBOL_VALUE) :
        my namespace_as_value ? my symbol : nullptr;
    Symbol const* shown = target ? shown_declaration(my symbol, target) : nullptr;
    // `export default Foo` of an exported value Foo and a local type Foo: tsc's local symbol, both (resolveEntityName
    // with every meaning finds the locals' symbol, a type with the value's exportSymbol)
    VecSpan declarations = {0};
    bool merged_local = false;
    if (shown && ctx->export_name && &ctx->export_name->identifier == me && shown->exported && (shown->flags & SYMBOL_VALUE) &&
        ctx->enclosing
    ) {
        Symbol const* local = symbol_table_get(scope_locals(ctx->enclosing), span_sv(identifier_expr_name(me)));
        if (local && local != shown && !(local->flags & SYMBOL_VALUE) && (local->flags & SYMBOL_TYPE)) {
            declarations = declaration_get_spans(local);
            VecSpan own = declaration_get_spans(shown);
            for_each (i, vec_span, &own) {
                bool seen = false;
                for_each (j, vec_span, &declarations) seen |= span_eq(*i, *j);
                if (!seen) vec_span_push(&declarations, *i);
            }
            vec_span_drop(&own);
            for (isize_t a = 1; a < vec_span_size(&declarations); a++) {
                for (isize_t b = a; b > 0 && span_source(declarations.data[b]) == span_source(declarations.data[b - 1]) &&
                    span_start_ptr(declarations.data[b]) < span_start_ptr(declarations.data[b - 1]); b--
                ) {
                    Span t = declarations.data[b];
                    declarations.data[b] = declarations.data[b - 1];
                    declarations.data[b - 1] = t;
                }
            }
            merged_local = true;
        }
    }
    // (an alias of an anonymous declaration, `export default class {}`, has a name)
    // (and a module that declarations merged into, the UMD global's)
    // (and the `module` and `exports` of a CommonJS file, declared by the file)
    bool commonjs_variable = shown && shown->decl_count &&
        (shown->decls[0].kind == SYMBOL_DECL_COMMONJS_MODULE || shown->decls[0].kind == SYMBOL_DECL_COMMONJS_EXPORTS);
    if (shown && (span_valid(target->name_span) || span_valid(shown->name_span) || (shown->is_module && shown->decl_count) ||
        commonjs_variable)
    ) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = merged_local ? declarations : declaration_get_spans(shown),
            .span = identifier_expr_name(me),
            .text = symbol_text(ctx, shown),
            .allow_declaration_empty = shown->is_global_this, // (It has no declarations: `Symbol(globalThis)`)
        });
    }
}

// `#x in obj`: the member `#x` names, from the class that declares it
static void private_name_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (!ctx->private_name_in_left) return; // (elsewhere it's nothing to tsc's getSymbolAtLocation either)
    Checker* checker = current_checker;
    Atom const* name = nullptr;
    Ty const* object = ty_private_name_owner(checker, me, &name);
    if (!object) return;
    PropSymbolInfo info = {0};
    collect_prop_symbol(checker, object, name, &info, 0);
    if (info.found) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = vec_span_clone(info.declarations),
            .allow_declaration_empty = true,
            .span = my inner_span,
            .text = prop_text(&info, ctx, my inner_span),
        });
    }
    vec_span_drop(&info.declarations);
}

// A namespaced JSX name `a:b` (tsc's JsxNamespacedName): its two identifiers, which are the error type
static void jsx_namespaced_name_compile_test_info(Span name, BaselineTestCase* ctx) {
    isize colon = csview_find(span_sv(name), ":");
    if (colon == c_NPOS) return;
    Span parts[2] = { name, name };
    span_set_sv(&parts[0], csview_slice(span_sv(name), 0, colon));
    span_set_sv(&parts[1], csview_slice(span_sv(name), colon + 1, name.size));
    for (u32 i = 0; i < 2; i++) {
        while (parts[i].size && isspace((unsigned char)parts[i].buf[0])) span_set_sv(&parts[i], csview_slice(span_sv(parts[i]), 1, parts[i].size));
        while (parts[i].size && isspace((unsigned char)parts[i].buf[parts[i].size - 1])) parts[i].size--;
        // (A missing name after the colon, `attr:={x}`, is tsc's missing identifier)
        if (parts[i].size || i == 1) baseline_test_case_add_type(ctx, parts[i], ty_intrinsic(current_checker, TY_ERROR));
    }
}

// A name that finds an index signature of `object` (tsc's getApplicableIndexSymbol): `__index`, declared by the index
// signatures that apply to the key (several patterns can), named after the object type's symbol. An index signature
// without a declaration (of a mapped type) is the type's symbol itself for a JSX tag (`undeclared_is_type`), else
// nothing. False if no line was added
static bool index_symbol_compile_test_info(BaselineTestCase* ctx, Ty const* object, Ty const* key, Span span, bool undeclared_is_type) {
    Checker* checker = current_checker;
    Ty const* apparent = ty_apparent_type(checker, object);
    if (!(apparent->flags & TYF_OBJECT)) return false;
    // (tsgo's index symbol's parent is the symbol of the object's type, not the apparent one: a type parameter's
    // (`T.__index`, `this` the class's). A type alias of a type literal names nothing, `__type`)
    Symbol const* param_symbol = nullptr;
    if (object->kind == TY_TYPE_PARAM) {
        param_symbol = object->param.symbol;
        if (!param_symbol && object->param.state->is_this && apparent->kind == TY_DECLARED) param_symbol = apparent->declared.symbol;
        if (!param_symbol && object->param.state->is_this && apparent->kind == TY_REFERENCE) param_symbol = apparent->reference.target->declared.symbol;
    }
    Symbol const* container = object->kind == TY_TYPE_PARAM ? param_symbol : apparent->kind == TY_DECLARED ? apparent->declared.symbol :
        apparent->kind == TY_REFERENCE ? apparent->reference.target->declared.symbol : ty_rest_type_symbol(checker, apparent);
    Members const* members = ty_members(checker, apparent);
    VecSpan declarations = {0};
    bool any = false;
    // (a specific one first: tsc lists the applicable ones in the order of the members, but the string index
    // signature only applies when nothing else does)
    for (u32 pass = 0; pass < 2 && !any; pass++) {
        for (u32 i = 0; i < members->index_count; i++) {
            IndexInfo const* info = &members->index[i];
            bool is_string = info->key->flags & TYF_STRING;
            if (is_string != (pass == 1)) continue;
            if (!is_string && !ty_specific_index_applies(checker, info, key)) continue;
            if (is_string && !(ty_key_flags(key) & (TYF_STRING_LIKE | TYF_NUMBER | TYF_NUMBER_LITERAL))) continue;
            any = true;
            if (span_valid(info->decl_span)) {
                vec_span_push(&declarations, info->decl_span);
            }
        }
    }
    if (!any || (vec_span_is_empty(&declarations) && !undeclared_is_type)) return false;
    cstr text = container ? symbol_text(ctx, container) : cstr_init();
    if (!vec_span_is_empty(&declarations)) {
        if (cstr_size(&text)) cstr_append_lit(&text, ".");
        cstr_append_lit(&text, "__index");
    } else if (container) {
        declarations = symbol_declaration_spans(container);
    }
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declarations,
        .allow_declaration_empty = true,
        .span = span,
        .text = text,
    });
    return true;
}

// The opening or closing tag name of an element: a value tag is a reference, an intrinsic one is `any` and names a
// member of JSX.IntrinsicElements (its property, or its index signature: `__index`)
static void jsx_tag_compile_test_info(JsxElement const* element, Expr* name, BaselineTestCase* ctx) {
    // (The closing tag by its own name: `<a.b></a>` closes with an intrinsic one)
    bool closing = name == &element->closing_name;
    bool intrinsic_tag = closing ? jsx_closing_is_intrinsic(element) : jsx_element_is_intrinsic(element);
    bool namespaced = closing ? element->closing_namespaced : element->namespaced;
    if (name->tag == EXPR_IMPLICIT_ANY || !span_valid(name->inner_span)) return;
    if (!name->inner_span.size) {
        // A missing name (`<` at the end of the file, a missing closing tag): an error type, shown where the next token
        // starts (the harness skips the trivia after the missing node's position)
        const char* at = name->inner_span.buf;
        at = skip_trivia_forward(at);
        baseline_test_case_add_type(ctx, span_make_in(name->inner_span, at, at), ty_intrinsic(current_checker, TY_ANY));
        return;
    }
    if (!intrinsic_tag) {
        expr_compile_test_info(name, ctx);
        return;
    }
    if (namespaced) {
        jsx_namespaced_name_compile_test_info(name->inner_span, ctx);
        return;
    }
    Checker* checker = current_checker;
    JsxIntrinsic intrinsic = closing ? ty_jsx_intrinsic_named(checker, element, span_sv(name->inner_span)) :
        ty_jsx_intrinsic(checker, element);
    if (intrinsic.prop) {
        PropSymbolInfo info = {0};
        collect_prop_symbol(checker, intrinsic.elements, intrinsic.prop->name, &info, 0);
        if (info.found) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = name->inner_span,
                .text = prop_text(&info, ctx, name->inner_span),
            });
        }
        vec_span_drop(&info.declarations);
    } else if (intrinsic.index) {
        index_symbol_compile_test_info(ctx, intrinsic.elements, ty_string_literal(checker, span_sv(name->inner_span)), name->inner_span, true);
    }
    // tsc checks the name as an expression: what it is as a value here (see jsx_lookup_names()), else any
    Symbol const* value = name->tag == EXPR_IDENTIFIER ? name->identifier.target : nullptr;
    ctx->any_keeps_rendering = true; // (tsc's harness keeps it for JSX tag names)
    baseline_test_case_add_type(ctx, name->inner_span, value ? ty_expr_type(checker, name) : ty_intrinsic(checker, TY_ANY));
}

static void property_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (ty_is_import_meta(me) && !csview_equals(span_sv(property_key_inner_span(my property)), "meta")) {
        // `import.metal` (TS17012) is the error type, and has no symbols
        baseline_test_case_add_type(ctx, property_key_span(my property), ty_intrinsic(current_checker, TY_ERROR));
        return;
    }
    if (ty_is_import_meta(me)) {
        // The symbol of ImportMeta's type, and `meta` as the property of tsc's synthetic ImportMetaExpression
        Ty const* type = ty_import_meta_type(current_checker);
        Symbol const* symbol = type->kind == TY_DECLARED ? type->declared.symbol : nullptr;
        if (symbol) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = declaration_get_spans(symbol),
                .span = my inner_span,
                .text = symbol_text(ctx, symbol),
            });
        }
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .span = property_key_span(my property),
            .allow_declaration_empty = true,
            .text = cstr_lit("ImportMetaExpression.meta"),
        });
        ctx->any_keeps_rendering = true;
        baseline_test_case_add_type(ctx, property_key_span(my property), type);
        return;
    }
    bool has_compiled_expr_info = false;
    // The symbol of the property: the declarations of the property of every member of the (apparent) object
    // type that has one, like tsc's synthetic union/intersection property symbols. The parent is the first
    // member's, so a union of two interfaces gets the plain name like tsc's synthetic symbol
    Checker* checker = current_checker;
    Ty const* object = ty_expr_type(checker, &my property->expr);
    // (Widened for an assignment target or a method call, like the checker's access: tsc's resolvedSymbol. The
    // literal of an element access is looked up in the expression's type as it is)
    if ((my property->method_call || my property->assignment_target) && my property->form == PROPERTY_EXPR_DOT) {
        object = ty_widened_objects(checker, ty_widened_nullable(checker, object));
    }
    Ty const* key = ty_access_key_type(checker, my property);
    // (`o["a"]`: tsc's getSymbolAtLocation of the literal is getPropertyOfType of the object's type, which has none
    // when a member like `undefined` doesn't have it: `o?.["a"]`)
    PropSymbolInfo info = { .qualified_name = my property->qualified_name, .strict_union = my property->form == PROPERTY_EXPR_BRACKET };
    if (my property->expr.tag == EXPR_IDENTIFIER && my property->expr.identifier.target) {
        info.synthetic_namespace = symbol_rare(my property->expr.identifier.target)->synthetic_default != nullptr;
    }
    // (Not of a parenthesized key: getSymbolAtLocation of `0` in `o[(0)]` sees no element access around it)
    if ((key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) && !(key->flags & TYF_ENUM_LITERAL) &&
        !(my property->form == PROPERTY_EXPR_BRACKET && expr_is_perenthisized(&my property->index))
    ) {
        collect_prop_symbol(checker, object, ty_literal_name(checker, key), &info, 0);
    }
    // The synthetic `default` of a namespace import of a JSON module (with_synthetic_default()): named as the module
    Symbol const* default_of = nullptr;
    if (info.found && !info.parent && vec_span_is_empty(&info.declarations) && (my property->form == PROPERTY_EXPR_DOT || my property->index.tag == EXPR_LITERAL) &&
        csview_equals(span_sv(property_key_span(my property)), "default") && my property->expr.tag == EXPR_IDENTIFIER
    ) {
        Symbol const* alias = my property->expr.identifier.symbol;
        PortItem const* item = alias && alias->alias ? alias->alias->item : nullptr;
        // (Of a JSON module: what other modules' synthetic defaults print as is their alias's name, `default`)
        if (item && item->tag == PORT_IMPORT_GATHER && !item->is_require && item->module_ref && item->module_ref->file &&
            item->module_ref->file->is_json
        ) {
            default_of = binder_module_of_ref(alias->binder, item->module_ref);
        }
    }
    if (info.found) {
        if (my property->form == PROPERTY_EXPR_DOT) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = my inner_span,
                .text = default_of ? module_text(ctx, default_of) : prop_text(&info, ctx, property_key_span(my property)),
            });
        }
        expr_compile_test_info(&my property->expr, ctx);
        has_compiled_expr_info = true;
        // tsc has no line for the name in a private name access (`a.#b`)
        if ((my property->form == PROPERTY_EXPR_DOT || my property->index.tag == EXPR_LITERAL) && !csview_starts_with(span_sv(property_key_span(my property)), "#")) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = property_key_span(my property),
                .text = default_of ? module_text(ctx, default_of) : prop_text(&info, ctx, property_key_span(my property)),
            });
        }
    }
    vec_span_drop(&info.declarations);
    if (!info.found && has_symbol_override(ctx, property_key_span(my property))) {
        // The target of a CommonJS assignment shows what it declares, even through an `exports` that is any
        if (my property->form == PROPERTY_EXPR_DOT && has_symbol_override(ctx, my inner_span)) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my inner_span, .allow_declaration_empty = true });
        }
        ctx->any_keeps_rendering = my property->form == PROPERTY_EXPR_DOT; // The object of a property access
        expr_compile_test_info(&my property->expr, ctx);
        has_compiled_expr_info = true;
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = property_key_span(my property), .allow_declaration_empty = true });
        info.found = true;
    }
    if (!info.found && (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) &&
        !(key->flags & TYF_ENUM_LITERAL) && (my property->form == PROPERTY_EXPR_DOT || my property->index.tag == EXPR_LITERAL) &&
        !csview_starts_with(span_sv(property_key_span(my property)), "#")
    ) {
        // Through a declared index signature: `__index`, on the access and on the name (a type parameter's
        // constraint's too, tsgo; not in element accesses: tsc has no symbol there)
        // (Any type whose applicable index signature has a declaration: tsgo's getApplicableIndexSymbol, a spread's
        // too, where 6.0 looked the `__index` member up in the type's symbol). A reverse mapped type's index signatures
        // have none (resolveReverseMappedTypeMembers)
        bool reverse = object->kind == TY_ANONYMOUS && object->anonymous.is_reverse;
        if (my property->form == PROPERTY_EXPR_DOT && !reverse &&
            index_symbol_compile_test_info(ctx, object, key, my inner_span, false)
        ) {
            expr_compile_test_info(&my property->expr, ctx);
            has_compiled_expr_info = true;
            index_symbol_compile_test_info(ctx, object, key, property_key_span(my property), false);
        }
    }

    bool dot = my property->form != PROPERTY_EXPR_BRACKET;
    if (!has_compiled_expr_info) {
        ctx->any_keeps_rendering = dot; // The object of a property access
        expr_compile_test_info(&my property->expr, ctx);
    }

    if (!dot) {
        expr_compile_test_info(&my property->index, ctx);
    } else if (!csview_starts_with(span_sv(property_key_span(my property)), "#")) {
        ctx->any_keeps_rendering = true;
        baseline_test_case_add_type(ctx, property_key_span(my property), ty_regular(current_checker, expr_get_type((Expr*)me)));
    }
}

static bool expr_compile_test_info_head(Expr* me, BaselineTestCase* ctx);

static void drop_symbol_overrides_to(BaselineTestCase* ctx, u32 count) {
    while (ctx->symbol_override_count > count) {
        ctx->symbol_override_count--;
        cstr_drop(&ctx->symbol_overrides[ctx->symbol_override_count].text);
        vec_span_drop(&ctx->symbol_overrides[ctx->symbol_override_count].declarations);
    }
}

// A binary expression and its operands. Iterative over the left spine (`a + b + c + ...`, thousands deep in
// binderBinaryExpressionStress): each left operand that is a binary expression again shows itself and is descended
// into, then the right operands are walked on the way back up, in the order recursion would have
static void binary_op_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    typedef struct { Expr* node; u32 overrides; } SpineEntry;
    SpineEntry local[64];
    SpineEntry* spine = local;
    isize count = 0, capacity = c_arraylen(local);
    for (Expr* node = me;;) {
        BinaryOpExpr* binary = node->binary_op;
        if (count == capacity) {
            capacity *= 2;
            SpineEntry* grown = malloc((size_t)capacity * sizeof(SpineEntry));
            memcpy(grown, spine, (size_t)count * sizeof(SpineEntry));
            if (spine != local) free(spine);
            spine = grown;
        }
        spine[count++] = (SpineEntry){ node, ctx->symbol_override_count };
        if (binary->op == BINARY_OP_EQUAL && span_source(binary->left.inner_span) && span_source(binary->left.inner_span)->is_js) {
            this_property_symbol_overrides(&binary->left, ctx);
        }
        ctx->private_name_in_left = binary->op == BINARY_OP_IN && binary->left.tag == EXPR_PRIVATE_NAME &&
            !expr_is_perenthisized(&binary->left); // (`(#x) in v` is an error, no PrivateIdentifier to tsc)
        Expr* left = &binary->left;
        if (left->tag != EXPR_BINARY_OP) {
            expr_compile_test_info(left, ctx);
            break;
        }
        if (!expr_compile_test_info_head(left, ctx)) break;
        node = left;
    }
    while (count > 0) {
        SpineEntry entry = spine[--count];
        BinaryOpExpr* binary = entry.node->binary_op;
        ctx->private_name_in_left = false;
        drop_symbol_overrides_to(ctx, entry.overrides);
        if (binary_commonjs_declaration(binary, nullptr)) commonjs_undefined_override(binary, ctx);
        expr_compile_test_info(&binary->right, ctx);
        drop_symbol_overrides_to(ctx, entry.overrides);
    }
    if (spine != local) free(spine);
}

void expr_compile_test_info_inner(Expr* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_UNDEFINED && csview_equals(span_sv(my inner_span), "undefined")) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .span = my inner_span,
                });
            }
            break;

        case EXPR_IDENTIFIER:
            // A `this` expression's symbol is the symbol of its (narrowed) type (tsc's getSymbolAtLocation,
            // checkExpression): none for a class's `this` narrowed to never (`"a" in this` of a class that has `a`, in
            // its else branch) or to an intersection (`this instanceof D`). Not in a type query, which is the declared
            // `this`
            if (my identifier.keyword == KEYWORD_IDENTIFIER_THIS && (
                    (my identifier.this_kind == THIS_CLASS_INSTANCE &&
                        (ty_expr_type(current_checker, me)->flags & TYF_NEVER)) ||
                    (!ctx->in_type_query && my identifier.target &&
                        ty_expr_type(current_checker, me)->kind == TY_INTERSECTION))
            ) {
                break;
            }
            identifier_expr_compile_test_info(&my identifier, ctx);
            if (!my identifier.target && my identifier.is_arguments) {
                // A function's `arguments` (tsc's argumentsSymbol, which has no declaration)
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my inner_span, .allow_declaration_empty = true });
            }
            if (my identifier.keyword == KEYWORD_IDENTIFIER_NEW_TARGET) {
                // tsc shows the `target` of `new.target` too, like the name of a property access
                Span target = my inner_span;
                char const* start = span_end_ptr(target);
                while (start > span_start_ptr(target) && start[-1] != '.' && !isspace((unsigned char)start[-1])) start--;
                span_set_sv(&target, csview_from_range(start, span_end_ptr(target)));
                // (A misspelled one, TS17012, has no symbol)
                if (my identifier.target && csview_equals(span_sv(target), "target")) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = symbol_declaration_spans(my identifier.target),
                        .span = target,
                        .name = my identifier.target->name_span,
                    });
                } else if (identifier_this_function(&my identifier) && csview_equals(span_sv(target), "target")) {
                    new_target_function_symbol(&my identifier, target, ctx);
                }
                baseline_test_case_add_type(ctx, target, ty_regular(current_checker, expr_get_type(me)));
            }
            break;

        case EXPR_BINARY_OP:
            binary_op_compile_test_info(me, ctx);
            break;

        case EXPR_UNARY_OP:
            if (my unary_op->op != UNARY_OP_YIELD || my unary_op->expr.tag != EXPR_IMPLICIT_ANY) { // Not a bare `yield`
                expr_compile_test_info(&my unary_op->expr, ctx);
            }
            break;

        case EXPR_PROPERTY: {
            u32 overrides = ctx->symbol_override_count;
            Expr const* module = &my property->expr;
            if (expr_is_module_exports(me) && module->identifier.target && module->identifier.target->decl_count &&
                module->identifier.target->decls[0].kind == SYMBOL_DECL_COMMONJS_MODULE &&
                ctx->symbol_override_count + 2 <= c_arraylen(ctx->symbol_overrides)
            ) {
                // tsc's synthetic `exports` property of `module`, named after it and declared by the file
                SourceFile const* file = module->identifier.target->decls[0].node;
                Span spans[2] = { my inner_span, property_key_span(my property) };
                for (u32 i = 0; i < 2; i++) {
                    VecSpan declarations = vec_span_init();
                    vec_span_push(&declarations, span_from_sv(file, csview_from_n(source_file_start(file), 0)));
                    ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
                        .span = spans[i], .text = cstr_lit("exports"), .declarations = declarations,
                    };
                }
            }
            property_compile_test_info(me, ctx);
            while (ctx->symbol_override_count > overrides) {
                ctx->symbol_override_count--;
                cstr_drop(&ctx->symbol_overrides[ctx->symbol_override_count].text);
                vec_span_drop(&ctx->symbol_overrides[ctx->symbol_override_count].declarations);
            }
        }   break;
        case EXPR_GENERIC_PARAMS:
            expr_compile_test_info(&my generic_params->expr, ctx);
            for_each (i, vec_spanned_type, &my generic_params->params) {
                type_compile_test_info(&i->type, ctx);
            }
            break;

        case EXPR_AS_TYPE:
            if (my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            expr_compile_test_info(&my as_type->expr, ctx);
            if (!my as_type->angle_bracket_syntax) {
                type_compile_test_info(&my as_type->type, ctx);
            }
            break;

        case EXPR_TERNARY:
            expr_compile_test_info(&my ternary->condition, ctx);
            expr_compile_test_info(&my ternary->yes, ctx);
            expr_compile_test_info(&my ternary->no, ctx);
            break;

        case EXPR_CALL: {
            // (`f<T>?.()` is `f<T>` then an optional call: tsc's parseCallExpressionRest; `f?.<T>()` has the call's)
            const char* after_callee = span_end_ptr(expr_outer_span(&my call->callable));
            while (*after_callee == ' ' || *after_callee == '\t' || *after_callee == '\n' || *after_callee == '\r') after_callee++;
            if (my call->optional_chain && after_callee[0] == '?' && after_callee[1] == '.') expr_compile_test_info(&my call->callable, ctx);
            else callee_compile_test_info(&my call->callable, ctx);
            // The specifier of `import("./a")` names the module (tsc's getSymbolAtLocation of a module name)
            // (`import<T>("./a")` too, TS1326)
            Expr const* callee = &my call->callable;
            if (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
            ModuleRef* ref = callee->tag == EXPR_IDENTIFIER ? identifier_module_ref(&callee->identifier) : nullptr;
            SourceFile const* in_file = span_source(my inner_span);
            Symbol const* module = ref && in_file && in_file->scope ? binder_module_of_ref(in_file->scope->binder, ref) : ref ? ref->module : nullptr;
            if (ref && module && !callee->identifier.require_call && !callee->identifier.import_source) module = ty_dynamic_import_module(current_checker, my call, ref);
            // (A `require("./a")` only where a declaration's alias imports it: tsgo's
            // isVariableDeclarationInitializedToRequire of the grandparent)
            bool require = callee->tag == EXPR_IDENTIFIER && callee->identifier.require_call;
            if (module && (!require || callee->identifier.require_call >= 2) && vec_expr_size(&my call->args) &&
                vec_expr_at(&my call->args, 0)->tag == EXPR_LITERAL
            ) {
                // (a file's module is declared by the file, and named by its path without the extension)
                SourceFile const* file = module->module ? module->module->file : nullptr;
                Span file_start = file ? span_from_sv(file, csview_from_n(source_file_start(file), 0)) : span_null;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = file ? vec_span_single(file_start) : declaration_get_spans(module),
                    .allow_declaration_empty = true,
                    .span = vec_expr_at(&my call->args, 0)->inner_span,
                    .text = file ? file_module_text(ctx, module) : symbol_text(ctx, module),
                });
            }
            for_each (i, vec_expr, &my call->args) {
                // The name `Object.defineProperty(o, "x", ...)` declares (tsc's getSymbolAtLocation of the literal; of a
                // numeric one only as an element access argument)
                if (i_index == 1 && call_rare(my call)->declares && !(i->tag == EXPR_LITERAL && i->literal.tag == VALUE_NUMBER)) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(call_rare(my call)->declares),
                        .span = i->inner_span,
                        .text = symbol_text(ctx, call_rare(my call)->declares),
                    });
                }
                expr_compile_test_info(i, ctx);
            }
        }   break;

        case EXPR_FUNCTION:
            function_impl_compile_test_info(my function, ctx);
            break;

        case EXPR_OBJECT:
            for_each (i, vec_object_expr_item, &my object) {
                ObjectExprItem const* first = nullptr;
                VecSpan merged = object_member_symbol_decls(me, i, &first);
                if (i->tag != OBJECT_ITEM_SPREAD && vec_span_size(&merged) > 1 &&
                    (i->tag == OBJECT_ITEM_PROPERTY || i->tag == OBJECT_ITEM_SHORTHAND) &&
                    (first->tag == OBJECT_ITEM_PROPERTY || first->tag == OBJECT_ITEM_SHORTHAND)
                ) {
                    // A name declared by several properties is one symbol, typed by the first of them
                    // (named like its first declaration: `{ a: 1, \u0061: 2 }` is `a` at both)
                    bool literal_key = i->computed_key && i->computed_key->tag == EXPR_LITERAL &&
                        (i->computed_key->literal.tag == VALUE_STRING || i->computed_key->literal.tag == VALUE_NUMBER);
                    VecSpan key_declarations = literal_key ? vec_span_clone(merged) : vec_span_init();
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = merged,
                        .span = i->name_span,
                        .name = first->name_span,
                        .name_is_value = first->name_is_value,
                    });
                    baseline_test_case_add_type(ctx, i->name_span, ty_object_literal_member_type(current_checker, me, first));
                    if (literal_key) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = key_declarations,
                            .span = expr_outer_span(i->computed_key),
                            .name = first->name_span,
                            .name_is_value = first->name_is_value,
                        });
                    }
                    if (i->computed_key) expr_compile_test_info(i->computed_key, ctx);
                    if (i->tag == OBJECT_ITEM_SHORTHAND) {
                        if (object_item_rare(i)->shorthand_default) expr_compile_test_info(object_item_rare(i)->shorthand_default, ctx);
                    } else {
                        expr_compile_test_info(&i->value, ctx);
                    }
                    continue;
                }
                vec_span_drop(&merged);
                if (i->tag != OBJECT_ITEM_SPREAD) {
                    VecSpan declarations = {0};
                    ObjectExprItem const* named = i; // (named like its first declaration)
                    // (A late-bound name too: its symbol may have merged into the early one of its name)
                    bool late = i->computed_key && !expr_is_perenthisized(i->computed_key) &&
                        !computed_key_early_name(i->computed_key, nullptr) && expr_is_entity_name(i->computed_key);
                    if (!i->computed_key || late) {
                        // (Its symbol: with the other accessor or the other methods of its name, alone if it conflicts
                        // with what came first)
                        ObjectExprItem const* accessor_first = nullptr;
                        declarations = object_member_symbol_decls(me, i, &accessor_first);
                        if (vec_span_is_empty(&declarations)) vec_span_push(&declarations, i->span);
                        if (accessor_first && vec_span_size(&declarations) > 1) named = accessor_first;
                    } else if ((i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) &&
                        (i->computed_key->tag == EXPR_LITERAL || expr_is_entity_name(i->computed_key))
                    ) {
                        // A late bound name: the accessors written with the same name (see the pairing in expr.c)
                        for_each (other, vec_object_expr_item, &my object) {
                            if ((other->tag == OBJECT_ITEM_GETTER || other->tag == OBJECT_ITEM_SETTER) && other->computed_key &&
                                csview_equals_sv(span_sv(expr_outer_span(other->computed_key)), span_sv(expr_outer_span(i->computed_key)))
                            ) {
                                vec_span_push(&declarations, other->span);
                            }
                        }
                    } else {
                        vec_span_push(&declarations, i->span);
                    }
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declarations,
                        .span = i->name_span,
                        .name = named != i ? named->name_span : span_null,
                        .name_is_value = named->name_is_value,
                    });
                    if (i->computed_key && i->computed_key->tag == EXPR_LITERAL &&
                        (i->computed_key->literal.tag == VALUE_STRING || i->computed_key->literal.tag == VALUE_NUMBER)
                    ) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = vec_span_single(i->span),
                            .span = expr_outer_span(i->computed_key),
                            .name = i->name_span,
                        });
                    }
                    // (A method's type parameters are in scope at its name)
                    bool method = i->value.tag == EXPR_FUNCTION && i->tag != OBJECT_ITEM_PROPERTY && i->tag != OBJECT_ITEM_SHORTHAND;
                    u32 saved = method ? baseline_test_case_enter_generic(ctx, &i->value.function->generic) : 0;
                    // (A method's symbol has its own type, whatever JSDoc `@type` the object literal gives the member)
                    // (`{ x = 1 }` outside a destructuring target: the name's symbol is typed by the name as an expression,
                    // tsc's getTypeOfVariableOrParameterOrProperty, whatever the default made the literal's property)
                    if (i->tag == OBJECT_ITEM_SHORTHAND && object_item_rare(i)->shorthand_default && !my assignment_pattern) {
                        baseline_test_case_add_type(ctx, i->name_span, ty_mutable_location_type(current_checker, &i->value, nullptr));
                    } else {
                        baseline_test_case_add_type(ctx, i->name_span, method && object_item_rare(i)->jsdoc_type ?
                            ty_function_type(current_checker, i->value.function) : ty_object_literal_member_type(current_checker, me, i));
                    }
                    if (method) baseline_test_case_leave_generic(ctx, saved);
                }
                if (i->computed_key) {
                    expr_compile_test_info(i->computed_key, ctx);
                }

                switch (i->tag) {
                    case OBJECT_ITEM_SHORTHAND:
                        // tsc only shows the property, not the identifier expression it's initialized from
                        if (object_item_rare(i)->shorthand_default) expr_compile_test_info(object_item_rare(i)->shorthand_default, ctx);
                        break;

                    case OBJECT_ITEM_METHOD:
                    case OBJECT_ITEM_GETTER:
                    case OBJECT_ITEM_SETTER:
                        // No line for the function as a whole, unlike a function expression
                        function_impl_compile_test_info(i->value.function, ctx);
                        break;

                    default:
                        expr_compile_test_info(&i->value, ctx);
                        break;
                }
            }
            break;

        case EXPR_ARRAY:
            for_each (i, vec_expr, &my array) {
                // (An omitted element, `[, x]`, has no line: tsc 6's typeWriter skips OmittedExpression)
                if (i->tag == EXPR_LITERAL && i->literal.tag == VALUE_UNDEFINED && !i->inner_span.size) continue;
                expr_compile_test_info(i, ctx);
            }
            break;

        case EXPR_CLASS_DECL:
            class_decl_compile_test_info(my class_decl, true, ctx);
            break;

        case EXPR_SPREAD:
            expr_compile_test_info(my spread, ctx);
            break;

        case EXPR_REGEX:
            break;

        case EXPR_PRIVATE_NAME:
            private_name_compile_test_info(me, ctx);
            break;

        case EXPR_TEMPLATE:
            if (my template_->tag) {
                callee_compile_test_info(my template_->tag, ctx);
                if (vec_expr_is_empty(&my template_->substitutions)) {
                    // Don't clone the string, and don't drop the type
                    TypeNode t = type_value(value_string(my template_->plain_string));
                    baseline_test_case_add_type_node(ctx, my template_->template_span, &t);
                } else {
                    baseline_test_case_add_type_node(ctx, my template_->template_span, &type_string);
                }
            }
            for_each (i, vec_expr, &my template_->substitutions) {
                expr_compile_test_info(i, ctx);
            }
            break;

        case EXPR_JSX_ELEMENT:
        case EXPR_JSX_FRAGMENT:
            if (my tag == EXPR_JSX_ELEMENT) jsx_tag_compile_test_info(my jsx, &my jsx->tag_name, ctx);
            for_each (i, vec_spanned_type, &my jsx->type_args) type_compile_test_info(&i->type, ctx);
            expr_compile_test_info_inner(&my jsx->attributes, ctx);
            for_each (i, vec_jsx_child, &my jsx->children) {
                if (i->tag == JSX_CHILD_EXPR || i->tag == JSX_CHILD_ELEMENT) expr_compile_test_info(&i->expr, ctx);
            }
            if (my tag == EXPR_JSX_ELEMENT && !my jsx->self_closing) jsx_tag_compile_test_info(my jsx, &my jsx->closing_name, ctx);
            break;

        case EXPR_JSX_ATTRIBUTES:
            for_each (i, vec_jsx_attribute, &my jsx_attributes) {
                if (i->tag != JSX_ATTRIBUTE_SPREAD) {
                    // An attribute declares a property of the attributes object (its members: attributes of one name
                    // are one symbol)
                    // (whose type is the first one's, the symbol's value declaration: `<X a={1} a />` is number twice)
                    VecSpan declarations = vec_span_init();
                    JsxAttribute const* first = nullptr;
                    for_each (j, vec_jsx_attribute, &my jsx_attributes) {
                        if (j->tag != JSX_ATTRIBUTE_SPREAD && csview_equals_sv(span_sv(j->name), span_sv(i->name))) {
                            vec_span_push(&declarations, j->span);
                            if (!first) first = j;
                        }
                    }
                    baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = declarations, .span = i->name });
                    baseline_test_case_add_type(ctx, i->name, ty_jsx_attribute_type(current_checker, me, first));
                    jsx_namespaced_name_compile_test_info(i->name, ctx);
                }
                // (tsc has no line for a string attribute's value)
                if (i->tag != JSX_ATTRIBUTE_NO_VALUE && i->tag != JSX_ATTRIBUTE_EMPTY_EXPR && i->tag != JSX_ATTRIBUTE_STRING) {
                    expr_compile_test_info(&i->value, ctx);
                }
            }
            break;
    }
}

// The callee of a call or tagged template, or a type query's expression: `f<T>(x)`'s type arguments are the call's
// (tsc has no node for `f<T>`), only a parenthesized `(f<T>)(x)` is an instantiation expression of its own
static void callee_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (my tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(me)) expr_compile_test_info_inner(me, ctx);
    else expr_compile_test_info(me, ctx);
}

// What an expression shows itself (its type), before its operands: false if it has nothing else to show
static bool expr_compile_test_info_head(Expr* me, BaselineTestCase* ctx) {
    if (my tag == EXPR_IMPLICIT_ANY && my missing_decorators) { // (tsc's MissingDeclaration: no expression of its own)
        for_each (i, vec_expr, my missing_decorators) expr_compile_test_info(i, ctx);
        return false;
    }
    if (my tag == EXPR_IMPLICIT_ANY && my missing && !my inner_span.size && !my paren_before && span_valid(my inner_span)) {
        // A missing identifier is at the full start of the next token: the harness shows it where the token starts
        const char* at = my inner_span.buf;
        at = skip_trivia_forward(at);
        baseline_test_case_add_type_recursive(ctx, span_make_in(my inner_span, at, at), span_make_in(my inner_span, at, at),
            ty_regular(current_checker, expr_get_type(me)));
        return false;
    }
    if (my tag == EXPR_LITERAL && my literal.tag == VALUE_NULL) {
        // `null` itself has no line, `(null)` does
        const char* start = span_start_ptr(my inner_span);
        const char* end = span_end_ptr(my inner_span);
        const char* outer_start = span_start_ptr(expr_outer_span(me));
        const char* outer_end = span_end_ptr(expr_outer_span(me));
        while (start > outer_start && start[-1] != '(') start--;
        while (end < outer_end && *end != ')') end++;
        if (start > outer_start && end < outer_end) {
            Span parenthesized = span_make_in(my inner_span, start - 1, end + 1);
            baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), parenthesized, ty_regular(current_checker, expr_get_type(me)));
        }
    } else if (my tag == EXPR_PRIVATE_NAME && !ctx->private_name_in_left) {
        // (tsc's isExpressionNode: a private name is an expression only as the left operand of `in`. Parenthesized,
        // `(#x) in v` (an error), the parentheses are one, of type any)
        if (!span_eq(expr_outer_span(me), my inner_span)) {
            ctx->any_keeps_rendering = true;
            baseline_test_case_add_type(ctx, expr_outer_span(me), ty_intrinsic(current_checker, TY_ANY));
        }
    } else if (my tag == EXPR_GENERIC_PARAMS && my generic_params->heritage) {
        // (A class's `extends B<T>` is tsc's ExpressionWithTypeArguments, which the type writer skips)
    } else if (my tag == EXPR_AS_TYPE && my as_type->is_jsdoc && span_eq(expr_outer_span(me), expr_outer_span(&my as_type->expr))) {
        // `/** @satisfies {T} */ const x = v`: v isn't parenthesized, the satisfies expression isn't tsc's
    } else if (my tag == EXPR_IDENTIFIER && my identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
        // The `import` of `import("m")` and `import.meta` isn't an expression of its own. The `defer` of
        // `import.defer("m")` is a name, of type any (tsc's MetaProperty callee isn't checked as an expression)
        if (my identifier.import_defer || my identifier.import_source) {
            // (The name, maybe escaped: `import.s\u006furce(...)`)
            Span name = my inner_span;
            char const* start = span_end_ptr(name);
            while (start > span_start_ptr(name) && (isalnum((unsigned char)start[-1]) || start[-1] == '\\' ||
                start[-1] == '{' || start[-1] == '}' || start[-1] == '_' || start[-1] == '$')
            ) {
                start--;
            }
            span_set_sv(&name, csview_from_range(start, span_end_ptr(name)));
            ctx->any_keeps_rendering = true;
            baseline_test_case_add_type(ctx, name, ty_intrinsic(current_checker, TY_ANY));
        }
    } else if (me == ctx->export_name && my identifier.target && (my identifier.target->flags & SYMBOL_VARIABLE)) {
        // (Of a local type and an exported value of the name: tsc's local symbol, whose declared type it is, see
        // getTypeOfNode's isInRightSideOfImportOrExportAssignment)
        Symbol const* local = ctx->enclosing ? symbol_table_get(scope_locals(ctx->enclosing), span_sv(my inner_span)) : nullptr;
        bool type_twin = local && local != my identifier.target && my identifier.target->exported &&
            !(local->flags & SYMBOL_VALUE) && (local->flags & SYMBOL_TYPE);
        baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), my inner_span, type_twin ?
            ty_declared_type_of(current_checker, local) : ty_type_of_symbol(current_checker,
                // (Through the alias: `import * as ns` of an `export =` module can be a clone, getTypeOfAlias)
                my identifier.symbol && (my identifier.symbol->flags & SYMBOL_ALIAS) ? my identifier.symbol : my identifier.target));
    } else {
        // tsc prints an expression's regular type (getRegularTypeOfExpression), a call's quick type if it has one
        // (getTypeOfExpression: `new A()` of an abstract class is A, though checking it gives the error type)
        Ty const* quick = my tag == EXPR_CALL ? ty_quick_expr_type(current_checker, me, false) : nullptr;
        // tsc's types walk asks for a reference's type again, after the check: when a body too large for control flow
        // analysis disabled it at the top level (getTypeAtFlowNode at 2000 recursions, which only a function body's
        // check restores), that answer is the error type, where the check may have narrowed it before the limit
        if (!quick && my tag == EXPR_IDENTIFIER && my identifier.flow && current_checker->flow_analysis_disabled) {
            quick = ty_compute_expr_without_context(current_checker, me);
        }
        baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), my inner_span,
            ty_regular(current_checker, quick ? quick : expr_get_type(me)));
    }
    return true;
}

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (expr_compile_test_info_head(me, ctx)) expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    if (statement_declares_name(my tag) && my decl.props.invalid_decorators) {
        for_each (i, vec_expr, my decl.props.invalid_decorators) expr_compile_test_info(i, ctx);
    }
    if ((my tag == STATEMENT_BREAK || my tag == STATEMENT_CONTINUE) && span_valid(my jump_label)) {
        // Both labels and the targets of break and continue are shown as any
        ctx->any_keeps_rendering = true;
        baseline_test_case_add_type_node(ctx, my jump_label, &type_any);
    }
    switch (my tag) {
        case STATEMENT_LABELED:
            ctx->any_keeps_rendering = true;
            baseline_test_case_add_type_node(ctx, my labeled.name, &type_any);
            statement_compile_test_info(my labeled.body, ctx);
            break;

        case STATEMENT_NOOP:
        case STATEMENT_SOURCE_DIRECTIVE:
            break;

        case STATEMENT_WITH: {
            expr_compile_test_info(&my with->object, ctx);
            bool saved = ctx->in_with;
            ctx->in_with = true;
            statement_compile_test_info(&my with->body, ctx);
            ctx->in_with = saved;
        }   break;

        case STATEMENT_BLOCK:
            block_compile_test_info(&my block, ctx);
            break;

        case STATEMENT_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR:
            // A name exported by `export = X` or `export default X` isn't an expression: it shows the type it declares
            // if it declares one (tsc's getTypeOfNode, isInRightSideOfImportOrExportAssignment)
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && my expr.tag == EXPR_IDENTIFIER && !my expr.identifier.keyword &&
                my expr.identifier.symbol
            ) {
                Symbol const* type_symbol = symbol_for_meaning(my expr.identifier.symbol, SYMBOL_TYPE);
                if (type_symbol && !type_symbol->is_placeholder &&
                    (type_symbol->flags & (SYMBOL_CLASS | SYMBOL_INTERFACE | SYMBOL_ENUM | SYMBOL_TYPE_ALIAS))
                ) {
                    Symbol const* shown = my expr.identifier.target ? my expr.identifier.target : type_symbol;
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(shown_declaration(my expr.identifier.symbol, shown)),
                        .span = my expr.inner_span,
                        .name = shown_declaration(my expr.identifier.symbol, shown)->name_span,
                    });
                    ctx->any_keeps_rendering = true;
                    baseline_test_case_add_type(ctx, my expr.inner_span, ty_declared_type_of(current_checker, type_symbol));
                    break;
                }
                // A namespace without a value: its symbol, and the expression's any
                Symbol const* namespace = symbol_for_meaning(my expr.identifier.symbol, SYMBOL_NAMESPACE);
                if (namespace && !namespace->is_placeholder && !symbol_for_meaning(my expr.identifier.symbol, SYMBOL_VALUE)) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(shown_declaration(my expr.identifier.symbol, namespace)),
                        .span = my expr.inner_span,
                        .name = shown_declaration(my expr.identifier.symbol, namespace)->name_span,
                    });
                }
            }
            // fallthrough
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_THROW_EXPR:
            // (A bare `return;` returns an implicit undefined literal, a written `undefined` is an expression)
            // A JSON file's value is an expression statement to tsc (its empty file's `{}` is nothing)
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && span_source(my span) && span_source(my span)->is_json) {
                if (expr_outer_span(&my expr).size) expr_compile_test_info(&my expr, ctx);
                break;
            }
            // A number or string literal or `this` is no expression to tsc's type writer under an export assignment
            // (isInExpressionContext), a template, `-1` or anything parenthesized is
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && !expr_is_perenthisized(&my expr) && ((my expr.tag == EXPR_LITERAL &&
                (my expr.literal.tag == VALUE_NUMBER || my expr.literal.tag == VALUE_STRING ||
                    my expr.literal.tag == VALUE_BIGINT) &&
                csview_size(span_sv(my expr.inner_span)) && !strchr("-+", my expr.inner_span.buf[0])) ||
                (my expr.tag == EXPR_IDENTIFIER && my expr.identifier.keyword == KEYWORD_IDENTIFIER_THIS))
            ) {
                break;
            }
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED ||
                csview_equals(span_sv(my expr.inner_span), "undefined")
            ) {
                ctx->any_keeps_rendering = my tag == STATEMENT_EXPORT_DEFAULT_EXPR; // Of an export assignment
                ctx->export_name = my tag == STATEMENT_EXPORT_DEFAULT_EXPR && my expr.tag == EXPR_IDENTIFIER ? &my expr : nullptr;
                expr_compile_test_info(&my expr, ctx);
                ctx->export_name = nullptr;
            }
            break;

        case STATEMENT_IF_ELSE: {
            int statement_size = vec_statement_size(&my if_else.statements);
            int cond_size = vec_expr_size(&my if_else.conditions);
            for (int i = 0; i < statement_size; i++) {
                if (i < cond_size) {
                    expr_compile_test_info(vec_expr_at_mut(&my if_else.conditions, i), ctx);
                }
                statement_compile_test_info(vec_statement_at_mut(&my if_else.statements, i), ctx);
            }
        }   break;

        case STATEMENT_FOR_LOOP:
            if (my for_loop->is_do_while) {
                statement_compile_test_info(&my for_loop->body, ctx);
                expr_compile_test_info(&my for_loop->condition, ctx);
                break;
            }
            statement_compile_test_info(&my for_loop->initialization, ctx);
            // (`for (;;)` has a `true` condition the parser made up, with an empty span)
            // (a missing one is tsc's missing identifier: `while ()`)
            if (my for_loop->condition.inner_span.size || my for_loop->condition.missing) {
                expr_compile_test_info(&my for_loop->condition, ctx);
            }
            if (my for_loop->afterthought.tag != EXPR_LITERAL ||
                my for_loop->afterthought.literal.tag != VALUE_UNDEFINED
            ) {
                expr_compile_test_info(&my for_loop->afterthought, ctx);
            }
            statement_compile_test_info(&my for_loop->body, ctx);
            break;

        case STATEMENT_FOR_EACH_LOOP:
            switch (my for_each_loop->variable.tag) {
                case FOR_EACH_LOOP_STATEMENT:
                    variable_statement_compile_test_info(&my for_each_loop->variable.statement, ctx);
                    break;

                case FOR_EACH_LOOP_EXPR:
                    expr_compile_test_info(&my for_each_loop->variable.expr, ctx);
                    break;
            }
            expr_compile_test_info(&my for_each_loop->iterable, ctx);
            statement_compile_test_info(&my for_each_loop->body, ctx);
            break;

        case STATEMENT_SWITCH:
            expr_compile_test_info(&my switch_->needle, ctx);
            for_each (i, vec_switch_case, &my switch_->cases) {
                if (i->is_pattern) {
                    expr_compile_test_info(&i->pattern, ctx);
                }
                for_each (j, vec_statement, &i->statements) {
                    statement_compile_test_info(j, ctx);
                }
            }
            break;

        case STATEMENT_TRY_CATCH:
            block_compile_test_info(&my try_catch->try_block, ctx);
            if (my try_catch->catch_var.tag != BINDING_NOOP) binding_compile_test_info(&my try_catch->catch_var, ctx);
            if (my try_catch->catch_initializer) expr_compile_test_info(my try_catch->catch_initializer, ctx);
            if (my try_catch->has_catch) block_compile_test_info(&my try_catch->catch_block, ctx);
            block_compile_test_info(&my try_catch->finally_block, ctx);
            break;

        case STATEMENT_BREAK: break;
        case STATEMENT_CONTINUE: break;

        case STATEMENT_IMPORT_EXPORT:
            for_each (i, vec_expr, &port_statement_rare(my import_export)->decorators) expr_compile_test_info(i, ctx);
            port_statement_compile_test_info(my import_export, ctx);
            break;

        case STATEMENT_DECL_VARIABLE:
            variable_statement_compile_test_info(&my decl.variable, ctx);
            break;

        case STATEMENT_DECL_FUNCTION:
            function_impl_compile_test_info(my decl.function.impl, ctx);
            break;

        case STATEMENT_DECL_ENUM:
            enum_decl_compile_test_info(my decl.enum_, ctx);
            break;

        case STATEMENT_DECL_CLASS:
            class_decl_compile_test_info(my decl.class, false, ctx);
            break;

        case STATEMENT_DECL_TYPE:
            type_decl_compile_test_info(my decl.type, ctx);
            break;

        case STATEMENT_DECL_INTERFACE:
            interface_decl_compile_test_info(my decl.interface, ctx);
            break;

        case STATEMENT_DECL_NAMESPACE:
            // (One a dotted JSDoc typedef name makes is in JSDoc, which tsc's walk doesn't go into)
            if (my decl.namespace->jsdoc) break;
            namespace_decl_compile_test_info(my decl.namespace, ctx);
            break;
    }
}

void block_compile_test_info(Block const* me, BaselineTestCase* ctx) {
    // (Types are named from where they're printed, tsc's enclosing declaration: a declaration of the block shadows
    // an outer one of the same name)
    Scope const* outer = ctx->enclosing;
    if (my scope) ctx->enclosing = my scope;
    for_each (i, vec_statement, &my statements) {
        statement_compile_test_info(i, ctx);
    }
    ctx->enclosing = outer;
}
