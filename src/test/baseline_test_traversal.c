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
static VecSpan class_prop_declarations(Symbol const* class_symbol, bool is_static, csview name, Span own, u32 min_count);

// What the symbols baseline shows for a reference: the declarations of the symbol the name found (an alias shows its
// import, not what it resolves to), or of the declaration when there is no symbol (keywords, synthetic nodes)
static inline Symbol const* shown_declaration(Symbol const* symbol, Symbol const* decl) {
    return symbol && symbol->decl_count ? symbol : decl;
}

static inline VecSpan declaration_get_spans(Symbol const* me) {
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

// The name of a file's module from here: its path in quotes, unless an alias in scope names it (`var m =
// require("./m")`, `import * as m`: tsc's getAccessibleSymbolChain)
static cstr file_module_text(BaselineTestCase* ctx, Symbol const* module) {
    SymbolChain chain = symbol_chain_at(ctx->enclosing, module, 0, false);
    Symbol const* first = chain.len ? chain.items[0] : nullptr;
    PortItem const* item = first && first != module && (first->flags & SYMBOL_ALIAS) && first->alias ?
        first->alias->item : nullptr;
    // (A namespace import can be a clone of the module, tsc's resolveESModuleSymbol, which the chain doesn't know
    // about: with esModuleInterop when the module's type has a `default` or signatures. An ES module's import of a
    // CommonJS one is a symbol of its own here already)
    bool plain_namespace = item && item->tag == PORT_IMPORT_GATHER && (!compiler_options()->es_module_interop ||
        (!symbol_get_export(module, c_sv("default")) && !symbol_get_export(module, c_sv("export="))));
    if (item && (item->is_require || plain_namespace)) {
        cstr ret = cstr_init();
        symbol_chain_append(&ret, ctx->enclosing, &chain);
        return ret;
    }
    return module_name_for_error(module, nullptr, false);
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

// tsc's getSpecialPropertyAssignmentSymbolFromEntityName: in `exports.x = v` the access, `exports` and `x` are the
// export x; in `module.exports = v` `module` and `exports` are the `export=` (the access itself is `module.exports`)
static void commonjs_symbol_overrides(Expr const* left, BaselineTestCase* ctx) {
    SourceFile const* file = left->inner_span.source;
    if (!file || !file->is_commonjs || !file->scope || !file->scope->container) return;
    Symbol const* module = file->scope->container;
    Span name = span_null;
    switch (expr_commonjs_target(left, &name)) {
        case COMMONJS_TARGET_NONE:
            break;
        case COMMONJS_TARGET_EXPORTS_PROPERTY: {
            Symbol const* symbol = module->exports ? symbol_table_get(module->exports, name.sv) : nullptr;
            add_symbol_override(ctx, left->inner_span, symbol);
            // (`exports["x"]`: an element access isn't a special property assignment name, its object is itself)
            if (left->property->form != PROPERTY_EXPR_BRACKET) add_symbol_override(ctx, left->property->expr.inner_span, symbol);
            add_symbol_override(ctx, expr_outer_span(&left->property->prop), symbol);
        }   break;
        case COMMONJS_TARGET_MODULE_EXPORTS: {
            Symbol const* symbol = module->exports ? symbol_table_get(module->exports, c_sv("export=")) : nullptr;
            add_symbol_override(ctx, left->property->expr.inner_span, symbol);
            add_symbol_override(ctx, expr_outer_span(&left->property->prop), symbol);
        }   break;
    }
}

// `exports.x = undefined` is an alias of undefined to tsc, whose `undefined` shows the file's first such export
// (getSymbolAtLocation of the name in an alias declaration)
static void commonjs_undefined_override(BinaryOpExpr const* assignment, BaselineTestCase* ctx) {
    Expr const* value = &assignment->right;
    if (value->tag != EXPR_LITERAL || value->literal.tag != VALUE_UNDEFINED || expr_is_perenthisized(value)) return;
    SourceFile const* file = value->inner_span.source;
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

// JavaScript `F.prototype.x = v` (tsc's getSpecialPropertyAssignmentSymbol): `F.prototype` and `x` are the member x
// of F, the access as a whole has no symbol
static void prototype_symbol_overrides(Expr* left, BaselineTestCase* ctx) {
    Span name = expr_static_property_name(left);
    Expr* constructor = span_valid(name) ? expr_prototype_object(&left->property->expr) : nullptr;
    Symbol const* symbol = constructor ? expr_entity_name_symbol(constructor) : nullptr;
    Symbol const* member = symbol && symbol_rare(symbol)->members ? symbol_table_get(symbol_rare(symbol)->members, name.sv) : nullptr;
    if (!member || ctx->symbol_override_count + 3 > c_arraylen(ctx->symbol_overrides)) return;
    // (A class's `prototype` is its instance type, where the access finds the member)
    if (symbol_find_node(symbol, SYMBOL_DECL_CLASS)) {
        add_symbol_override(ctx, left->inner_span, member);
    } else {
        ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
            .span = left->inner_span, .text = cstr_init(), .declarations = vec_span_init(), .suppress = true,
        };
    }
    add_symbol_override(ctx, left->property->expr.inner_span, member);
    if (left->property->form == PROPERTY_EXPR_BRACKET) {
        // (`F.prototype["x"] = v`: the literal has no symbol)
        ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
            .span = expr_outer_span(&left->property->prop), .text = cstr_init(), .declarations = vec_span_init(), .suppress = true,
        };
    } else if (name.sv.size && name.sv.buf[0] == '#') {
        // (A private name outside its class has no symbol)
        ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
            .span = expr_outer_span(&left->property->prop), .text = cstr_init(), .declarations = vec_span_init(), .suppress = true,
        };
    } else {
        add_symbol_override(ctx, expr_outer_span(&left->property->prop), member);
    }
}

// JavaScript `this.x = v` that declares x: the name `x` is the member it declares, whatever the access finds (a
// base class's member: tsc's getSymbolOfDeclaration for the name of a declaration)
static void this_property_symbol_overrides(Expr* left, BaselineTestCase* ctx) {
    Span name = expr_static_property_name(left);
    Expr const* object = span_valid(name) ? &left->property->expr : nullptr;
    if (!object || object->tag != EXPR_IDENTIFIER || object->identifier.keyword != KEYWORD_IDENTIFIER_THIS) return;
    IdentifierExpr const* this = &object->identifier;
    // (A global `this.x = v` in an arrow function declares x in the arrow function, not on globalThis)
    if (this->this_kind == THIS_GLOBAL && this->this_through_arrow && left->property->declares) {
        add_symbol_override(ctx, expr_outer_span(&left->property->prop), left->property->declares);
        if (ctx->symbol_override_count < c_arraylen(ctx->symbol_overrides)) {
            ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
                .span = left->inner_span, .text = cstr_init(), .declarations = vec_span_init(), .suppress = true,
            };
        }
        return;
    }
    SymbolTable const* table = nullptr;
    if (this->this_kind == THIS_CLASS_INSTANCE && this->target) table = symbol_rare(this->target)->members;
    if (this->this_kind == THIS_CLASS_STATIC && this->target) table = this->target->exports;
    Symbol const* js_class = (this->this_kind == THIS_ANY || this->this_kind == THIS_PARAMETER) && identifier_this_function(this) ?
        ty_js_this_class(identifier_this_function(this)) : nullptr;
    if (js_class) table = symbol_rare(js_class)->members;
    Symbol const* member = table ? symbol_table_get(table, name.sv) : nullptr;
    // (Not when the class body declares it)
    ClassDecl const* class_decl = this->target ? symbol_find_node(this->target, SYMBOL_DECL_CLASS) : nullptr;
    ClassItem const* item = class_decl ? class_decl_find_member(class_decl, name.sv, this->this_kind == THIS_CLASS_STATIC) : nullptr;
    if (item) {
        // A method declared after it replaces the symbol the assignment declared (tsc's isReplaceableByMethod), which
        // the name keeps, alone
        if (item->tag == CLASS_ITEM_METHOD && item->span.source == expr_outer_span(left).source &&
            span_start_ptr(item->span) > span_start_ptr(expr_outer_span(left)) && member &&
            ctx->symbol_override_count < c_arraylen(ctx->symbol_overrides)
        ) {
            ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
                .span = expr_outer_span(&left->property->prop), .text = symbol_text(ctx, member),
                .declarations = vec_span_single(expr_outer_span(left)),
            };
        }
        return;
    }
    bool declares = false;
    for (u32 i = 0; member && i < member->decl_count; i++) declares |= span_eq(member->decls[i].full, expr_outer_span(left));
    if (declares) add_symbol_override(ctx, expr_outer_span(&left->property->prop), member);
}

// JavaScript `module.exports.a.x = v` / `exports.a.x = v` declaring x (tsc's AssignmentDeclarationKind.ExportsProperty
// for an entity name rooted at exports): the access `module.exports.a` is x too, not what it names (tsc's
// getSpecialPropertyAssignmentSymbolFromEntityName)
static void nested_expando_symbol_overrides(Expr* left, BaselineTestCase* ctx) {
    if (left->tag != EXPR_PROPERTY || !left->property->declares || left->property->form != PROPERTY_EXPR_DOT) return;
    Expr const* entity = &left->property->expr;
    if (entity->tag != EXPR_PROPERTY || expr_is_perenthisized(entity) || !expr_is_entity_name(entity)) return;
    if (expr_prototype_object((Expr*)entity) || expr_commonjs_target(left, nullptr) != COMMONJS_TARGET_NONE) return;
    if (span_equals(expr_static_property_name(left), "prototype")) return; // (AssignmentDeclarationKind.Prototype)
    Expr const* next_to_last = entity;
    while (next_to_last->property->expr.tag == EXPR_PROPERTY) next_to_last = &next_to_last->property->expr;
    Expr const* root = &next_to_last->property->expr;
    bool exports_rooted = root->tag == EXPR_IDENTIFIER && (span_equals(root->inner_span, "exports") ||
        (span_equals(root->inner_span, "module") && span_equals(expr_static_property_name(next_to_last), "exports")));
    if (!exports_rooted) return;
    if (has_symbol_override(ctx, entity->inner_span)) return;
    add_symbol_override(ctx, entity->inner_span, left->property->declares);
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
    u32 seen_count;
} PropSymbolInfo;

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
            if (!info->found) {
                info->found = true;
                info->parent = inner.parent;
                info->namespace = inner.namespace;
                info->member_symbol = inner.member_count == 1 ? inner.member_symbol : nullptr;
                info->name_span = inner.name_span;
                info->name_is_value = inner.name_is_value;
                info->first_declaration = inner.first_declaration;
            } else if (!span_eq(info->first_declaration, inner.first_declaration)) {
                info->non_uniform = true;
            }
            info->non_uniform |= inner.non_uniform;
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
                for_each (k, vec_span, &inner.declarations) vec_span_push(&info->declarations, *k);
            }
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
        if (info->strict_union && !(object->flags & (TYF_NEVER | TYF_ANY))) info->missing_in_member = true;
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
        // (An object literal's type without it too: isObjectLiteralType, `(o || {}).a`)
        if (object->kind == TY_ANONYMOUS && object->anonymous.object_literal) indexed = true;
        if (!indexed) info->missing_in_member = true;
        return;
    }
    Prop const* root = prop;
    while (root->target && !root->decl->decl_span_count) root = root->target;
    Span first_declaration = root->decl->decl_span_count ? root->decl->decl_spans[0] : span_null;
    if (!info->found) {
        info->found = true;
        info->parent = prop->parent;
        if (object->kind == TY_DECLARED && object->declared.kind == TY_DECLARED_NAMESPACE) {
            info->namespace = object->declared.namespace_symbol;
        }
        info->member_symbol = root->decl->symbol;
        info->name_span = root->decl->name_span;
        info->name_is_value = root->decl->name_is_value;
        info->first_declaration = first_declaration;
    } else if (!span_eq(info->first_declaration, first_declaration)) {
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
    for (u32 j = 0; j < root->decl->decl_span_count; j++) {
        Span span = root->decl->decl_spans[j];
        bool repeated = false;
        for (u32 k = 0; k < j; k++) repeated |= span_eq(root->decl->decl_spans[k], span);
        if (!repeated) vec_span_push(&info->declarations, span);
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

// The name of a property's symbol from here: a namespace's or module's export by its own chain, a member by the chain
// of its container (tsc's getContainersOfSymbol of a member), else bare
static cstr prop_text(PropSymbolInfo const* info, BaselineTestCase* ctx, Span fallback) {
    csview name = span_valid(info->name_span) ? info->name_span.sv : fallback.sv;
    // (A property no declaration names, `x["a"]` of what `"a" in x` synthesized, is named by its symbol's name)
    if (!span_valid(info->name_span) && name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'') &&
        name.buf[name.size - 1] == name.buf[0] && !memchr(name.buf, '\\', (size_t)name.size)
    ) {
        name = csview_slice(name, 1, name.size - 1);
    }
    cstr ret = cstr_init();
    Symbol const* member = info->member_symbol;
    // A member of a namespace's or module's object is its export (maybe an alias: getSymbolAtLocation doesn't resolve)
    // (by the name accessed: the export's own name may differ, `export default class C`)
    csview accessed = fallback.sv;
    if (accessed.size >= 2 && (accessed.buf[0] == '"' || accessed.buf[0] == '\'')) accessed = csview_slice(accessed, 1, accessed.size - 1);
    Symbol const* exported = info->member_count == 1 && info->namespace ?
        symbol_get_export(info->namespace, accessed) : nullptr;
    if (exported) {
        cstr_drop(&ret);
        return symbol_text(ctx, exported);
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
            baseline_test_case_add_type_node(ctx, my content_span, &type_undefined);
            break;

        case BINDING_IDENTIFIER:
            if (my parent) ctx->any_keeps_underline = true; // A binding element
            if (!my parent && span_valid(ctx->parameter_property_class) && my symbol) {
                // A parameter property's name declares the property (tsc's parameter.symbol is the property's)
                // (One symbol with a field of its name: the class's)
                VecSpan declarations = class_prop_declarations(ctx->parameter_property_symbol, false, my content_span.sv,
                    ctx->parameter_property_arg, 1);
                if (vec_span_is_empty(&declarations)) declarations = baseline_symbol_declaration_spans(my symbol);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declarations,
                    .namespaces = vec_span_single(ctx->parameter_property_class),
                    .span = my content_span,
                });
                baseline_test_case_add_symbol_value(ctx, my content_span, my symbol);
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
                } else if (!i->target.rest && !span_eq(i->source_span, i->target.content_span) &&
                    i->source_span.sv.size && i->source_span.sv.buf[0] != '"' && i->source_span.sv.buf[0] != '\'' && !isdigit((unsigned char)i->source_span.sv.buf[0])
                ) {
                    // `{ a: x }`: the property name is the property of the pattern's type (tsc's getSymbolAtLocation
                    // of a binding element's propertyName). A string or numeric name isn't an expression to tsc's walker
                    Checker* checker = current_checker;
                    Ty const* object = ty_binding_type(checker, me, (LowerCtx){ .generic = true });
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
                    ctx->any_keeps_underline = true;
                    baseline_test_case_add_type_node(ctx, i->source_span, &type_any);
                }
                binding_content_compile_test_info(&i->target, ctx);
            }
            break;
    }
    if (my default_value) {
        if (my parent) ctx->any_keeps_underline = true; // (A binding element's initializer: its parent is one too)
        expr_compile_test_info(my default_value, ctx);
    }
}

void binding_compile_test_info(Binding const* me, BaselineTestCase* ctx) {
    binding_content_compile_test_info(me, ctx);
    // (the parameters of a parameter property's type are no members)
    Span parameter_property_class = ctx->parameter_property_class;
    ctx->parameter_property_class = span_null;
    if (my has_explicit_type) {
        type_compile_test_info(&my explicit_type, ctx);
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
            // A property after a method of its name joined the method's symbol (see builder_add_interface_member)
            Prop const* joined = nullptr;
            if (!my key.name_is_member_function && ctx->member_owner && !redeclared && span_valid(my declaration)) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                bool mine = false;
                for (u32 i = 0; prop && (prop->flags & PROP_METHOD) && i < prop->decl->decl_span_count; i++) {
                    mine |= span_eq(prop->decl->decl_spans[i], my declaration);
                }
                if (mine) joined = prop;
            }
            VecSpan declarations = vec_span_init();
            // (A symbol is named as its first declaration writes it: `1; 1.0;` is `I[1]` twice)
            csview written = my key.span.sv;
            // (An instantiation's declarations are its target's: `interface Array<T>` merged with the lib's)
            Prop const* merged_root = merged;
            while (merged_root && !merged_root->decl->decl_span_count && merged_root->target) merged_root = merged_root->target;
            if ((merged_root && merged_root->decl->decl_span_count) || redeclared || joined) {
                Prop const* shown = merged ? merged_root : redeclared ? redeclared : joined;
                for (u32 i = 0; i < shown->decl->decl_span_count; i++) vec_span_push(&declarations, shown->decl->decl_spans[i]);
                if (span_valid(shown->decl->name_span)) written = shown->decl->name_span.sv;
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
            Ty const* type = merged ? prop_type(current_checker, declared_member) : optional_member_type(ty_of_node(&my type), my optional);
            if (redeclared) type = prop_type(current_checker, redeclared);
            // A property declared after a method of its name is that method's symbol (PropertyExcludes is nothing)
            if (!merged && !redeclared && !my key.name_is_member_function && ctx->member_owner && !my has_write_type) {
                Checker* checker = current_checker;
                Prop const* prop = ty_get_prop(checker, ctx->member_owner, checker_atom(checker, cstr_sv(&my key.name_string)));
                if (prop && (prop->flags & PROP_METHOD) && prop->decl->decl_span_count && span_valid(my declaration) &&
                    !span_eq(prop->decl->decl_spans[0], my declaration)
                ) {
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
            bool setter_first = pair && span_valid(setter_param) && setter_param.sv.buf < second_name.sv.buf;
            if (pair && !setter_first) type_compile_test_info(&my type, ctx);
            if (span_valid(setter_param) && setter_first) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = setter_param });
                baseline_test_case_add_type(ctx, setter_param, my has_write_type ? ty_of_node(&my accessor->write_type) : type);
                if (my has_write_type) type_compile_test_info(&my accessor->write_type, ctx);
            }
            if (pair) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = interface_member_declaration_vec(me),
                    .span = second_name,
                    .text = member_text(ctx, ctx->member_container, scope, second_name.sv, false),
                });
                baseline_test_case_add_type(ctx, second_name, type);
            }
            if (span_valid(setter_param) && !setter_first) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = setter_param });
                baseline_test_case_add_type(ctx, setter_param, my has_write_type ? ty_of_node(&my accessor->write_type) : type);
                if (pair && my has_write_type) type_compile_test_info(&my accessor->write_type, ctx);
            }
            if (pair) {
                if (setter_first) type_compile_test_info(&my type, ctx);
                return;
            }
        }   break;

        case INTERFACE_KEY_INDEX:
            if (span_valid(my key.index->name)) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .span = my key.index->name, .declarations = vec_span_single(my key.index->decl),
                });
                baseline_test_case_add_type_node(ctx, my key.index->name, &my key.index->type);
            }
            type_compile_test_info(&my key.index->type, ctx);
            for (u32 k = 0; k < vec_span_size(&my key.index->more_names); k++) {
                Span name = *vec_span_at(&my key.index->more_names, k);
                TypeNode const* type = vec_type_at(&my key.index->more_types, k);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .span = name, .declarations = vec_span_single(*vec_span_at(&my key.index->more_decls, k)),
                });
                baseline_test_case_add_type_node(ctx, name, type);
                type_compile_test_info(type, ctx);
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
                Atom const* name = ty_property_name_of_key(checker, ty_expr_type(checker, my key.expr.ref), &is_number);
                Prop const* prop = name ? ty_get_prop(checker, ctx->member_owner, name) : nullptr;
                bool mine = false;
                Span own = span_valid(my declaration) ? my declaration : my key.span;
                for (u32 i = 0; prop && i < prop->decl->decl_span_count; i++) mine |= span_eq(prop->decl->decl_spans[i], own);
                if (mine && prop->decl->decl_span_count > 1) {
                    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(&declarations, prop->decl->decl_spans[i]);
                    if ((prop->flags & PROP_METHOD) && prop->decl->overload_count > 1) overloads = prop;
                    if (my computed_accessor) accessor_pair = prop;
                }
            }
            if (vec_span_is_empty(&declarations)) vec_span_push(&declarations, span_valid(my declaration) ? my declaration : my key.span);
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my key.span,
                .text = member_text(ctx, ctx->member_container, scope, my key.span.sv, false),
            }, my key.expr.ref);
            baseline_test_case_add_type(ctx, my key.span, overloads ? prop_type(current_checker, overloads) :
                accessor_pair ? prop_type(current_checker, accessor_pair) : optional_member_type(ty_of_node(&my type), my optional));
            computed_member_name_compile_test_info(my key.expr.ref, ctx);
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
    type_compile_test_info(&my type, ctx);
    if (my has_write_type) type_compile_test_info(&my accessor->write_type, ctx);
}

void interface_compile_test_info(Interface const* me, Span scope, BaselineTestCase* ctx) {
    for_each (i, vec_interface_member, &my members) {
        interface_member_compile_test_info(i, scope, ctx);
    }
}

static void type_predicate_compile_test_info(TypePredicateNode* me, BaselineTestCase* ctx);

// The type parameter of `infer X`: from the end of `infer` (its leading trivia) to the end of the name
static Span inferred_type_parameter_span(InferredType const* inferred) {
    csview full = inferred->full_span.sv;
    isize at = csview_find_sv(full, c_sv("infer"));
    char const* start = at == c_NPOS ? inferred->name.sv.buf : full.buf + at + 5;
    if (start > inferred->name.sv.buf) start = inferred->name.sv.buf;
    return span_make(inferred->name.source, start, span_end_ptr(inferred->name));
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
            bool negative = (my value.tag == VALUE_NUMBER || my value.tag == VALUE_BIGINT) && my value_span.sv.size && my value_span.sv.buf[0] == '-';
            if (my value.tag != VALUE_BOOLEAN && !negative) break;
            baseline_test_case_add_type_node(ctx, my value_span, me);
            if (negative) {
                Span operand = my value_span;
                operand.sv.buf++;
                operand.sv.size--;
                while (operand.sv.size && (operand.sv.buf[0] == ' ' || operand.sv.buf[0] == '\t')) {
                    operand.sv.buf++;
                    operand.sv.size--;
                }
                Value positive = my value.tag == VALUE_BIGINT ? value_bigint_negated(&my value)
                                                              : value_number(-my value.number);
                baseline_test_case_add_type(ctx, operand, ty_literal(current_checker, &positive));
            }
        }   break;

        case TYPE_ANY:
            if (span_valid(my any.unkown_symbol_span)) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .name = sv_make_span(c_sv("unknown")),
                    .span = my any.unkown_symbol_span,
                });
            }
            break;

        case TYPE_IDENTIFIER:
            if (identifier_type_import_argument(my identifier)) { // (Nothing after an invalid argument is resolved)
                type_compile_test_info(identifier_type_import_argument(my identifier), ctx);
                for_each (i, vec_spanned_type, &my identifier->generic_params) type_compile_test_info(&i->type, ctx);
                break;
            }
            if (span_equals(my identifier->name, "object")) {
                break;
            }
            if (my identifier->keyword == KEYWORD_IDENTIFIER_THIS) break; // A `this` type isn't an identifier to tsc
            // (An alias of an anonymous class, `import C = require()` of `export = class {}`, is shown)
            if (my identifier->target && !span_valid(my identifier->target->name_span) &&
                !(my identifier->symbol && my identifier->symbol != my identifier->target &&
                    my identifier->symbol->flags & SYMBOL_ALIAS && span_valid(my identifier->symbol->name_span))
            ) {
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
                for (isize_t k = (count < known ? count : known) - 1; k >= 1; k--) {
                    Symbol const* decl = *vec_symbol_cptr_at(identifier_type_namespace_symbols(my identifier), k);
                    Span prefix = span_between(*vec_span_at(identifier_type_namespaces(my identifier), 0), *vec_span_at(identifier_type_namespaces(my identifier), k));
                    if (decl && !decl->is_placeholder) {
                        baseline_test_case_add_symbol(ctx, (BaselineSym){
                            .declarations = declaration_get_spans(decl),
                            .allow_declaration_empty = true,
                            .span = prefix,
                            .text = symbol_text(ctx, decl),
                        });
                    }
                    ctx->any_keeps_underline = true;
                    if (decl && !decl->is_placeholder && symbol_for_meaning(decl, SYMBOL_VALUE) &&
                        !symbol_type_only_declaration(decl)) {
                        baseline_test_case_add_symbol_value(ctx, prefix, decl);
                    } else {
                        baseline_test_case_add_type_node(ctx, prefix, &type_any);
                    }
                }
            }
            VecSpan so_far = vec_span_init();
            for_each (i, vec_span, identifier_type_namespaces(my identifier)) {
                intptr_t index = i_index;
                Symbol const* decl = index < vec_symbol_cptr_size(identifier_type_namespace_symbols(my identifier)) ?
                    *vec_symbol_cptr_at(identifier_type_namespace_symbols(my identifier), index) :
                    nullptr;
                ctx->any_keeps_underline = true; // Part of a qualified name
                if (heritage && decl && !decl->is_placeholder && symbol_for_meaning(decl, SYMBOL_VALUE) &&
                    (i_index == 0 || !symbol_type_only_declaration(decl))) {
                    // A heritage clause's name is an expression to tsc's writer (ExpressionWithTypeArguments)
                    baseline_test_case_add_symbol_value(ctx, *i, decl);
                } else {
                    baseline_test_case_add_type_node(ctx, *i, &type_any);
                }
                if (heritage && !decl) break;
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
                    .text = decl && !decl->is_placeholder ? symbol_text(ctx, decl) : cstr_init(),
                });
                vec_span_push(&so_far, *i);
            }
            vec_span_drop(&so_far);
            if (!span_valid(my identifier->name)) break; // A bare `import("./a")`
            if (!my identifier->name.sv.size && !identifier_type_import_ref(my identifier)) {
                // A missing name (`a.` at the end of a line): tsc's unknownSymbol, shown where the next token starts
                const char* at = my identifier->name.sv.buf;
                while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') at++;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .allow_declaration_empty = true,
                    .span = span_make(my identifier->name.source, at, at),
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
            // (A name an import type can't find, in a module that doesn't resolve or doesn't export it, is nothing: tsc's
            // getSymbolAtLocation of an ImportTypeNode's qualifier resolves through the module)
            bool unresolved_import = identifier_type_import_ref(my identifier) && (!shown || shown->is_placeholder);
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
                    ctx->any_keeps_underline = true;
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
                .declarations = my inferred->symbol_ref ? declaration_get_spans(my inferred->symbol_ref) :
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
            FunctionImpl const* function = my callable->function;
            if (span_valid(function_impl_rare(function)->jsdoc_new)) { // (Outside JSDoc: `function(new: T)` in TypeScript)
                baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = function_impl_rare(function)->jsdoc_new });
                baseline_test_case_add_type(ctx, function_impl_rare(function)->jsdoc_new, ty_of_node(&function->ret));
            }
            for_each (i, vec_function_arg, &my callable->function->args) {
                // (A JSDoc function type's unnamed parameters are `argN`, written nowhere)
                Span name = i->initializer.binding.content_span;
                if (function->jsdoc_type && i->initializer.binding.tag == BINDING_IDENTIFIER && name.source &&
                    (name.sv.buf < cstr_str(&name.source->contents) ||
                        name.sv.buf >= cstr_str(&name.source->contents) + cstr_size(&name.source->contents))
                ) {
                    if (i->initializer.binding.has_explicit_type) type_compile_test_info(&i->initializer.binding.explicit_type, ctx);
                    continue;
                }
                binding_content_compile_test_info(&i->initializer.binding, ctx);
                if (i->initializer.binding.has_explicit_type) {
                    type_compile_test_info(&i->initializer.binding.explicit_type, ctx);
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
            interface_compile_test_info(&my interface, span_null, ctx);
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
    }
    block_compile_test_info(&my body, ctx);
    baseline_test_case_leave_generic(ctx, saved);
    ctx->parameter_property_class = outer_parameter_property_class;
}

// An alias shows its own declaration as the symbol and the value of what it resolves to as the type. In a type-only
// `import type`/`export type` (not `* as ns`) its name is a type declaration's to tsc's getTypeOfNode: the declared
// type of what it resolves to, errorType if that isn't a type
static void port_item_add_alias(PortItem const* me, BaselineTestCase* ctx, bool type_declaration) {
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = baseline_symbol_declaration_spans(my symbol),
        .span = my decl_name,
        .text = symbol_text(ctx, my symbol),
    });
    // tsc shows aliases of pure types as any
    // An import/export name (the harness's isImportStatementName, isExportStatementName: not `* as ns`, whose errorType
    // is `error`)
    ctx->any_keeps_underline = !((my tag == PORT_IMPORT_GATHER && !my is_require) || my tag == PORT_REEXPORT_GATHER);
    if (type_declaration && my tag != PORT_IMPORT_GATHER && my tag != PORT_REEXPORT_GATHER) {
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
        ctx->any_keeps_underline = false;
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
        ctx->shorthand_alias.source == item->decl_name.source;
    if (local_import && !same_file) {
        ctx->shorthand_module = module;
        ctx->shorthand_alias = item->decl_name;
    }
    return module;
}

void port_statement_compile_test_info(PortStatement const* me, BaselineTestCase* ctx) {
    if (my jsdoc) return;
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
                ctx->any_keeps_underline = true;
                baseline_test_case_add_type_node(ctx, my main.decl_name, &type_any);
            } else {
                port_item_add_alias(&my main, ctx, false);
            }
            for_each (i, vec_span, &my main.alias_path) {
                // Part of a qualified name: any is written like any other type, else it's the intrinsic name
                ctx->any_keeps_underline = vec_span_size(&my main.alias_path) > 1;
                if (i_index >= vec_symbol_cptr_size(&my main.alias_path_symbols)) {
                    baseline_test_case_add_type_node(ctx, *i, &type_any);
                    continue;
                }
                Symbol const* symbol = *vec_symbol_cptr_at(&my main.alias_path_symbols, i_index);
                Symbol const* decl = symbol_for_meaning(symbol, SYMBOL_ANY_MEANING);
                if (!decl) decl = symbol;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(decl->is_placeholder ? decl : shown_declaration(symbol, decl)),
                    .span = *i,
                    .text = decl->is_placeholder ? cstr_from_sv(decl->name_span.sv) : symbol_text(ctx, shown_declaration(symbol, decl)),
                });
                if ((decl->flags & SYMBOL_ENUM_MEMBER) && !decl->is_placeholder) {
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
        if (i->decl_name.sv.buf != i->source_name.sv.buf) {
            // (a string literal name, `{ "<X>" as x }`, is no expression to tsc's walker: no line)
            bool quoted = i->source_name.sv.size && (i->source_name.sv.buf[0] == '"' || i->source_name.sv.buf[0] == '\'');
            if (i->source_symbol && !quoted) {
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(i->source_symbol),
                    .span = i->source_name,
                    .text = i->source_symbol->is_placeholder ? cstr_init() : symbol_text(ctx, i->source_symbol),
                });
            } else if (!quoted && csview_equals(i->source_name.sv, "default") && i->symbol->alias &&
                i->symbol->alias->target && i->symbol->alias->target->is_module && !shorthand
            ) {
                // A synthetic default (a module without one, allowSyntheticDefaultImports or a CommonJS module from an
                // ES module): the module itself (tsc's resolveExternalModuleSymbol of the `default` it can't find)
                Symbol const* module = i->symbol->alias->target;
                SourceFile const* file = module->module->file;
                VecSpan declarations = vec_span_init();
                if (file) vec_span_push(&declarations, (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) });
                VecSpan own = symbol_declaration_spans(module);
                for_each (k, vec_span, &own) vec_span_push(&declarations, *k);
                vec_span_drop(&own);
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = declarations,
                    .span = i->source_name,
                    .text = symbol_text(ctx, module),
                });
            } else if (shorthand && !quoted) {
                // (tsc's getExternalModuleMember: any member of a shorthand ambient module is the module)
                bool aliased = ctx->shorthand_module == shorthand && span_valid(ctx->shorthand_alias) &&
                    ctx->shorthand_alias.source == i->source_name.source;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = symbol_declaration_spans(shorthand),
                    .span = i->source_name,
                    .text = aliased ? cstr_from_sv(ctx->shorthand_alias.sv) : symbol_text(ctx, shorthand),
                });
            }
            ctx->any_keeps_underline = true;
            if (!quoted) baseline_test_case_add_symbol_value(ctx, i->source_name, i->symbol);
            else ctx->any_keeps_underline = false;
        }
        port_item_add_alias(i, ctx, my main.type_only);
    }
    if (span_valid(my missing_specifier)) baseline_test_case_add_type(ctx, my missing_specifier, ty_intrinsic(current_checker, TY_ANY));
    // `with { type: "json" }`: the keys written as identifiers are errorType (tsc resolves them as nothing)
    if (my attributes) {
        for_each (i, vec_import_attribute, &my attributes->elements) {
            if (span_valid(i->name) && i->name.sv.buf[0] != '"' && i->name.sv.buf[0] != '\'') {
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
        Expr const* value = initialized_binding_value(i);
        if (i->binding.has_explicit_type && i->binding.explicit_type.from_jsdoc && value->tag != EXPR_IMPLICIT_ANY &&
            (value->tag == EXPR_AS_TYPE && value->as_type->is_jsdoc ? value->as_type->is_satisfies &&
                !span_eq(expr_outer_span(value), expr_outer_span(&value->as_type->expr)) : !span_eq(expr_outer_span(value), value->inner_span))
        ) {
            ctx->jsdoc_paren_span = expr_outer_span(value);
            ctx->jsdoc_paren_type = ty_of_node(&i->binding.explicit_type);
        }
        initialized_binding_compile_test_info(i, ctx);
        ctx->jsdoc_paren_span = span_null;
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
        my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_unresolved(current_checker, my name.sv)
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
                member_text(ctx, my symbol, my name, (i->name_is_value ? i->full_name : declaration->name_span).sv, true),
        });
        baseline_test_case_add_symbol_value(ctx, span, declaration);
        // `[2] = 2`, `["4"] = 4`: the literal in the computed name names the member too
        if (!i->computed_name && span_valid(i->full_name) && i->full_name.sv.size && i->full_name.sv.buf[0] == '[' && declaration) {
            Span literal = i->name;
            if (i->name_is_value) literal = span_make(literal.source, span_start_ptr(literal) - 1, span_end_ptr(literal) + 1);
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = symbol_declaration_spans(declaration),
                .span = literal,
                .text = !i->name_is_value && declaration->parent ? symbol_text(ctx, declaration) :
                    member_text(ctx, my symbol, my name, (i->name_is_value ? i->full_name : declaration->name_span).sv, true),
            });
        }
        // (A computed name isn't checked, its expression is any: tsc's isComputedNonLiteralName)
        if (i->computed_key) baseline_test_case_add_type_node(ctx, expr_outer_span(i->computed_key), &type_any);
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
        *flags = 1;
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
// bound (tsc's lateBindMember), and in a class every late bound member of a name is one symbol, conflicts or not
static u32 computed_member_symbol_items(ClassItem const* me, VecClassItem const* items, ClassItem const** out, u32 cap) {
    bool late;
    u32 flags;
    Atom const* name = class_member_name(me, &late, &flags);
    if (!name) return 0;
    bool is_static = class_item_is_static((ClassItem*)me);
    u32 len = 0;
    if (late) {
        for (ClassItem const* j = items->data; j < items->data + items->size; j++) {
            bool other_late;
            u32 other_flags;
            if (class_item_is_static((ClassItem*)j) != is_static) continue;
            if (class_member_name(j, &other_late, &other_flags) != name || !other_late) continue;
            if (len < cap) out[len++] = j;
        }
        return len;
    }
    // (The binder, in order: each member's symbol is the first of its name or itself)
    ClassItem const* firsts[64];
    u32 count = 0;
    ClassItem const* first = nullptr;
    u32 existing = 0;
    ClassItem const* mine = nullptr;
    for (ClassItem const* j = items->data; j < items->data + items->size && count < c_arraylen(firsts); j++) {
        bool other_late;
        u32 other_flags;
        if (class_item_is_static((ClassItem*)j) != is_static) continue;
        if (class_member_name(j, &other_late, &other_flags) != name || other_late) continue;
        u32 excludes = other_flags == 1 ? 0 : other_flags == 2 ? ~2u : other_flags == 4 ? ~8u : ~4u;
        ClassItem const* symbol = j;
        if (!first) {
            first = j;
            existing = other_flags;
        } else if (!(existing & excludes)) {
            symbol = first;
            existing |= other_flags;
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
            // A method of a class merged with an interface: the member's declarations are both's (the checker's member
            // has them, see builder_add_interface_member)
            if (!my method.accessor && !my method.computed_key && ctx->class_symbol && !span_valid(class_item_private_name(me))) {
                Checker* checker = current_checker;
                Ty const* owner = my method.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, checker_atom(checker, cstr_sv(&my method.name_string)));
                bool mine = false;
                for (u32 i = 0; prop && i < prop->decl->decl_span_count; i++) mine |= span_eq(prop->decl->decl_spans[i], my span);
                if (mine && prop->decl->decl_span_count > (u32)vec_span_size(&declarations)) {
                    vec_span_clear(&declarations);
                    for (u32 i = 0; i < prop->decl->decl_span_count; i++) vec_span_push(&declarations, prop->decl->decl_spans[i]);
                }
            }
            // (named as its first declaration writes it)
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my method.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, written_name.sv, false),
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
            add_member_symbol(ctx, (BaselineSym){
                .declarations = declarations,
                .span = my field.name_span,
                .text = member_text(ctx, ctx->class_symbol, class_name, written_name.sv, false),
            }, my field.computed_key);
            Ty const* type = optional_member_type(class_item_get_type(me), my field.is_optional);
            // (Joined into a symbol something else made first: that symbol's type. A static `prototype` property joins
            // the class's own prototype symbol, which the binder makes first: the instance type)
            bool prototype = my field.is_static && !my field.computed_key && cstr_equals(&my field.name_string, "prototype");
            if ((first != me || prototype) && ctx->class_symbol) {
                Checker* checker = current_checker;
                Ty const* owner = my field.is_static ? ty_static_type_of(checker, ctx->class_symbol) :
                    ty_declared_type_of(checker, ctx->class_symbol);
                Prop const* prop = ty_get_prop(checker, owner, checker_atom(checker, cstr_sv(&my field.name_string)));
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
        ctx->hidden_type_spans[ctx->hidden_type_span_count++] = expr_outer_span(&e->property->prop);
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

// An element of a class's `extends` clause: the base type (instance) if there is one, else the expression's
static void class_extends_compile_test_info(Expr* extends, Ty const* instance, BaselineTestCase* ctx) {
    bool generic = extends->tag == EXPR_GENERIC_PARAMS;
    Expr* base = generic ? &extends->generic_params->expr : extends;
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
                Span inside = span_make(expr_outer_span(base).source, start + 1, end - 1);
                baseline_test_case_add_type_recursive(ctx, inside, base->inner_span, ty_regular(current_checker, expr_get_type(base)));
            }
        }
        expr_compile_test_info_inner(base, ctx);
    } else {
        expr_compile_test_info(base, ctx);
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
        Prop const* prop = ty_get_prop(checker, declared, checker_atom(checker, i->name.sv));
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
        ctx->merged_declarations[ctx->merged_declaration_count].name = i->name.sv;
        ctx->merged_declarations[ctx->merged_declaration_count++].declarations = spans;
    }
}

void class_decl_compile_test_info(ClassDecl* me, bool is_expr, BaselineTestCase* ctx) {
    for_each (i, vec_expr, &my decorators) {
        expr_compile_test_info(i, ctx);
    }
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
    // (With the wrong number of type arguments the reference is errorType, which the harness replaces with the
    // expression's type)
    if (instance && !class_decl_base_arity_fits(me)) instance = nullptr;
    if (my extends.tag != EXPR_IMPLICIT_ANY) class_extends_compile_test_info(&my extends, instance, ctx);
    for_each (i, vec_spanned_type, &my implements) heritage_type_compile_test_info(&i->type, ctx);
    for_each (i, vec_expr, &my unchecked_extends) class_extends_compile_test_info(i, instance, ctx);
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
    baseline_test_case_add_type(
        ctx, my name,
        my symbol ? ty_declared_type_of(current_checker, my symbol) : ty_intrinsic(current_checker, TY_ANY)
    );
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
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = vec_span_single(my full_span), .span = my name });
        if (namespace_decl_is_instantiated(me)) {
            baseline_test_case_add_type_text(ctx, my name, c_sv("typeof global"));
        } else {
            ctx->any_keeps_underline = true;
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
            vec_span_push(&decls, (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) });
            file_name = file_module_text(ctx, my symbol);
        }
        VecSpan own = declaration_get_spans(my symbol);
        for_each (i, vec_span, &own) vec_span_push(&decls, *i);
        vec_span_drop(&own);
        // An ambient module's name is its string in double quotes
        // (an augmentation of an `export =` module declares into the exported symbol: named like any symbol)
        if (!file && my is_ambient_module && my module_ref) {
            if (my symbol->is_module) {
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
            ctx->any_keeps_underline = true;
            baseline_test_case_add_type_node(ctx, my name, &type_any);
        } else if (my is_augmentation && my module_ref && !binder_module_of_ref(my symbol->binder, my module_ref) &&
            !namespace_decl_is_instantiated(me)
        ) {
            // (An augmentation is a namespace to tsc's binder, a value only if instantiated: unmerged, its symbol has no
            // type)
            ctx->any_keeps_underline = true;
            baseline_test_case_add_type_node(ctx, my name, &type_any);
        } else if ((my symbol->flags & SYMBOL_VALUE) && namespace_decl_declares_value(me)) {
            baseline_test_case_add_symbol_value(ctx, my name, my symbol);
        }
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
    Symbol const* js_class = my this_kind == THIS_ANY && identifier_this_function(me) ? ty_js_this_class(identifier_this_function(me)) : nullptr;
    if (js_class) {
        // A JavaScript constructor function's class: declared by the function (a function expression's own symbol
        // to tsc, named like the variable)
        SymbolDecl const* variable = symbol_find_decl(js_class, SYMBOL_DECL_VARIABLE);
        Expr const* init = variable ? initialized_binding_value(((InitializedBinding const*)variable->node)) : nullptr;
        // (A function expression assigned to a property, `exports.F = function () {}`, is declared by itself)
        bool own = function_impl_rare(identifier_this_function(me))->class_symbol == js_class && identifier_this_function(me)->is_expression;
        // (Assigned to a property, `ns.F = function () {}`, the class is the function expression, named by itself or
        // the property: from its prototype's methods too)
        SymbolDecl const* assignment = variable ? nullptr : symbol_find_decl(js_class, SYMBOL_DECL_ASSIGNMENT);
        Expr const* assigned = assignment ? assignment->node : nullptr;
        csview class_name;
        bool named = ty_js_assigned_class_name(js_class, &class_name);
        bool assigned_class = named && assigned && assigned->tag == EXPR_FUNCTION;
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = init ? vec_span_single(expr_outer_span(init)) :
                assigned_class ? vec_span_single(assigned->function->full_span) :
                own ? vec_span_single(identifier_this_function(me)->full_span) : declaration_get_spans(js_class),
            .span = identifier_expr_name(me),
            .text = named ? cstr_from_sv(class_name) : symbol_text(ctx, js_class),
        });
        return;
    }
    if (!symbol && my this_kind == THIS_ANY && identifier_this_function(me)) {
        Signature const* sig = ty_function_assigned_contextual_signature(checker, identifier_this_function(me));
        while (sig && sig->target) sig = sig->target;
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
        if (type && type->kind == TY_ANONYMOUS && type->anonymous.object_literal && literal) {
            // The object literal's own type (noImplicitThis): its symbol, `__object` or the variable's name
            bool named = literal == ctx->variable_literal;
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_single(literal->inner_span),
                .span = identifier_expr_name(me),
                .text = named ? cstr_from_sv(ctx->variable_literal_name.sv) : cstr_lit("__object"),
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
                .text = cstr_lit("__type"),
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
            }
        }
    }
    if (!symbol || !symbol->decl_count) return;
    // The `this` parameter of a JSDoc `@this {T}` (its name is the tag's): no declaration to tsc
    Span first = symbol->decls[0].name;
    if (symbol->is_parameter && span_valid(first) && first.source && first.sv.buf > source_file_start(first.source) &&
        first.sv.buf[-1] == '@'
    ) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = identifier_expr_name(me), .allow_declaration_empty = true, .text = cstr_lit("this") });
        return;
    }
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = declaration_get_spans(symbol),
        .span = identifier_expr_name(me),
        .text = symbol_text(ctx, symbol),
    });
}

// The declarations of the symbol tsc's binder makes for an object literal member (bindPropertyOrMethodOrAccessor into
// the literal's members): a later member of the same name joins it unless the symbol has a meaning it excludes
// (a property excludes nothing, a method other values, an accessor other values but its other half). *first: the
// symbol's first member (whose value types it)
// An object literal member's binder name (tsc's getDeclarationName: a written name, a literal computed name's text,
// `+1`/`-1` as written), or with `late` its late bound name (a computed entity name of a literal or unique symbol
// type). Null for a symbol of its own
static Atom const* object_member_name(ObjectExprItem const* item, bool* late) {
    *late = false;
    if (item->tag == OBJECT_ITEM_SPREAD) return nullptr;
    Checker* checker = current_checker;
    Expr const* key = item->computed_key;
    if (!key) return checker_atom(checker, cstr_sv(&item->name_string));
    if (expr_is_perenthisized(key)) return nullptr;
    bool is_number = false;
    if ((key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)) ||
        (key->tag == EXPR_TEMPLATE && !key->template_->tag && vec_expr_is_empty(&key->template_->substitutions))
    ) {
        return ty_property_name_of_key(checker, ty_expr_type(checker, key), &is_number);
    }
    if (key->tag == EXPR_UNARY_OP && (key->unary_op->op == UNARY_OP_MINUS || key->unary_op->op == UNARY_OP_PLUS)) {
        Expr const* operand = &key->unary_op->expr;
        if (operand->tag != EXPR_LITERAL || operand->literal.tag != VALUE_NUMBER || expr_is_perenthisized(operand)) return nullptr;
        Atom const* text = ty_property_name_of_key(checker, ty_expr_type(checker, operand), &is_number);
        if (!text) return nullptr;
        cstr name = cstr_d(key->unary_op->op == UNARY_OP_MINUS ? "-" : "+", atom_sv(text));
        Atom const* ret = checker_atom(checker, cstr_sv(&name));
        cstr_drop(&name);
        return ret;
    }
    if (!expr_is_entity_name(key)) return nullptr;
    *late = true;
    return ty_property_name_of_key(checker, ty_expr_type(checker, key), &is_number);
}

// The declarations of an object literal member's symbol: the binder's table joins members of a name unless the first
// one's meaning excludes the new one's (then it's a symbol of its own); late bound names (lateBindMember) the same way
// among themselves
static VecSpan object_member_symbol_decls(ObjectExpr const* object, ObjectExprItem const* item, ObjectExprItem const** first) {
    VecSpan ret = vec_span_init();
    *first = item;
    if (item->tag == OBJECT_ITEM_SPREAD) return ret;
    // (Only an object with a computed name asks the checker for names)
    bool computed = false;
    for_each (other, vec_object_expr_item, object) computed |= other->computed_key != nullptr;
    bool late = false;
    Atom const* name = computed ? object_member_name(item, &late) : nullptr;
    if (computed && !name) return ret;
    enum { PROP = 1, METHOD = 2, GET = 4, SET = 8 };
    u32 flags = 0; // The table's symbol of the name
    for_each (other, vec_object_expr_item, object) {
        if (other->tag == OBJECT_ITEM_SPREAD) continue;
        if (computed) {
            bool other_late = false;
            if (object_member_name(other, &other_late) != name || other_late != late) continue;
        } else if (!cstr_eq(&other->name_string, &item->name_string)) {
            continue;
        }
        u32 kind = other->tag == OBJECT_ITEM_METHOD ? METHOD : other->tag == OBJECT_ITEM_GETTER ? GET :
            other->tag == OBJECT_ITEM_SETTER ? SET : PROP;
        u32 excludes = kind == PROP ? 0 : kind == METHOD ? (PROP | GET | SET) : kind == GET ? (PROP | METHOD | GET) : (PROP | METHOD | SET);
        if (flags && (flags & excludes)) {
            // A symbol of its own, outside the table
            if (other == item) {
                vec_span_clear(&ret);
                vec_span_push(&ret, other->span);
                return ret;
            }
            continue;
        }
        if (!flags) *first = other;
        flags |= kind;
        vec_span_push(&ret, other->span);
    }
    return ret;
}

// `new.target` in a function expression: the function's symbol (named like tsc's getNameOfSymbolAsWritten: the
// variable it initializes, else `(Anonymous function)`), declared by the expression
static void new_target_function_symbol(IdentifierExpr const* me, Span span, BaselineTestCase* ctx) {
    FunctionImpl const* function = identifier_this_function(me);
    baseline_test_case_add_symbol(ctx, (BaselineSym){
        .declarations = vec_span_single(function->full_span),
        .span = span,
        .text = span_valid(function_impl_variable_name(function)) ? cstr_from_sv(function_impl_variable_name(function).sv) : cstr_lit("(Anonymous function)"),
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
        if (base && my super_call && class_decl->extends.tag == EXPR_IDENTIFIER) {
            Ty const* constructor = ty_expr_type(current_checker, &class_decl->extends);
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
    if (my target && my symbol == my target && my target->is_module && my target->module->file) {
        // `exports` in a CommonJS file: the file's symbol, `"a"` declared by the whole file
        SourceFile const* file = my target->module->file;
        VecSpan declarations = vec_span_init();
        vec_span_push(&declarations, (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) });
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declarations,
            .span = identifier_expr_name(me),
            .text = module_name_for_error(my target, nullptr, false),
        });
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
    // (an alias of an anonymous declaration, `export default class {}`, has a name)
    if (shown && (span_valid(target->name_span) || span_valid(shown->name_span))) {
        baseline_test_case_add_symbol(ctx, (BaselineSym){
            .declarations = declaration_get_spans(shown),
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
    isize colon = csview_find(name.sv, ":");
    if (colon == c_NPOS) return;
    Span parts[2] = { name, name };
    parts[0].sv = csview_slice(name.sv, 0, colon);
    parts[1].sv = csview_slice(name.sv, colon + 1, name.sv.size);
    for (u32 i = 0; i < 2; i++) {
        while (parts[i].sv.size && isspace((unsigned char)parts[i].sv.buf[0])) parts[i].sv = csview_slice(parts[i].sv, 1, parts[i].sv.size);
        while (parts[i].sv.size && isspace((unsigned char)parts[i].sv.buf[parts[i].sv.size - 1])) parts[i].sv.size--;
        if (parts[i].sv.size) baseline_test_case_add_type(ctx, parts[i], ty_intrinsic(current_checker, TY_ERROR));
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
    Symbol const* container = object->alias ? object->alias :
        apparent->kind == TY_DECLARED ? apparent->declared.symbol :
        apparent->kind == TY_REFERENCE ? apparent->reference.target->declared.symbol : nullptr;
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
            // (Never one of a lib: no baseline of tsc's has such an `__index`, useRegexpGroups has none where a
            // local copy of the same declarations does. Unexplained, hacks.md)
            if (span_valid(info->decl_span) && !(info->decl_span.source && info->decl_span.source->is_stdlib && !undeclared_is_type)) {
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
    if (name->tag == EXPR_IMPLICIT_ANY || !span_valid(name->inner_span)) return;
    if (!name->inner_span.sv.size) {
        // A missing name (`<` at the end of the file, a missing closing tag): an error type
        baseline_test_case_add_type(ctx, name->inner_span, ty_intrinsic(current_checker, TY_ANY));
        return;
    }
    if (!jsx_element_is_intrinsic(element)) {
        expr_compile_test_info(name, ctx);
        return;
    }
    if (element->namespaced) {
        jsx_namespaced_name_compile_test_info(name->inner_span, ctx);
        return;
    }
    Checker* checker = current_checker;
    JsxIntrinsic intrinsic = ty_jsx_intrinsic(checker, element);
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
        index_symbol_compile_test_info(ctx, intrinsic.elements, ty_string_literal(checker, name->inner_span.sv), name->inner_span, true);
    }
    // tsc checks the name as an expression: what it is as a value here (see jsx_lookup_names()), else any
    Symbol const* value = name->tag == EXPR_IDENTIFIER ? name->identifier.target : nullptr;
    ctx->any_keeps_underline = true; // (tsc's harness keeps it for JSX tag names)
    baseline_test_case_add_type(ctx, name->inner_span, value ? ty_type_of_symbol(checker, value) : ty_intrinsic(checker, TY_ANY));
}

static void property_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (ty_is_import_meta(me) && !csview_equals(my property->prop.inner_span.sv, "meta")) {
        // `import.metal` (TS17012) is the error type, and has no symbols
        baseline_test_case_add_type(ctx, expr_outer_span(&my property->prop), ty_intrinsic(current_checker, TY_ERROR));
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
            .span = expr_outer_span(&my property->prop),
            .allow_declaration_empty = true,
            .text = cstr_lit("ImportMetaExpression.meta"),
        });
        ctx->any_keeps_underline = true;
        baseline_test_case_add_type(ctx, expr_outer_span(&my property->prop), type);
        return;
    }
    bool has_compiled_expr_info = false;
    // The symbol of the property: the declarations of the property of every member of the (apparent) object
    // type that has one, like tsc's synthetic union/intersection property symbols. The parent is the first
    // member's, so a union of two interfaces gets the plain name like tsc's synthetic symbol
    Checker* checker = current_checker;
    Ty const* object = ty_expr_type(checker, &my property->expr);
    Ty const* key = ty_access_key_type(checker, my property);
    // (`o["a"]`: tsc's getSymbolAtLocation of the literal is getPropertyOfType of the object's type, which has none
    // when a member like `undefined` doesn't have it: `o?.["a"]`)
    PropSymbolInfo info = { .qualified_name = my property->qualified_name, .strict_union = my property->form == PROPERTY_EXPR_BRACKET };
    if ((key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) && !(key->flags & TYF_ENUM_LITERAL)) {
        collect_prop_symbol(checker, object, ty_literal_name(checker, key), &info, 0);
    }
    if (info.found) {
        if (my property->form == PROPERTY_EXPR_DOT) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = my inner_span,
                .text = prop_text(&info, ctx, expr_outer_span(&my property->prop)),
            });
        }
        expr_compile_test_info(&my property->expr, ctx);
        has_compiled_expr_info = true;
        // tsc has no line for the name in a private name access (`a.#b`)
        if (my property->prop.tag == EXPR_LITERAL && !csview_starts_with(expr_outer_span(&my property->prop).sv, "#")) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){
                .declarations = vec_span_clone(info.declarations),
                .allow_declaration_empty = true,
                .span = expr_outer_span(&my property->prop),
                .text = prop_text(&info, ctx, expr_outer_span(&my property->prop)),
            });
        }
    }
    vec_span_drop(&info.declarations);
    if (!info.found && has_symbol_override(ctx, expr_outer_span(&my property->prop))) {
        // The target of a CommonJS assignment shows what it declares, even through an `exports` that is any
        if (my property->form == PROPERTY_EXPR_DOT) {
            baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my inner_span, .allow_declaration_empty = true });
        }
        ctx->any_keeps_underline = my property->form == PROPERTY_EXPR_DOT; // The object of a property access
        expr_compile_test_info(&my property->expr, ctx);
        has_compiled_expr_info = true;
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = expr_outer_span(&my property->prop), .allow_declaration_empty = true });
        info.found = true;
    }
    if (!info.found && my property->form == PROPERTY_EXPR_DOT && has_symbol_override(ctx, my inner_span)) {
        // `F.prototype` of `F.prototype.x = v` shows the member it declares even where F has no `prototype` (a
        // variable initialized with an object: prototype_symbol_overrides())
        baseline_test_case_add_symbol(ctx, (BaselineSym){ .span = my inner_span, .allow_declaration_empty = true });
        expr_compile_test_info(&my property->expr, ctx);
        has_compiled_expr_info = true;
        info.found = true;
    }
    if (!info.found && (key->flags & (TYF_STRING_LITERAL | TYF_NUMBER_LITERAL)) &&
        !(key->flags & TYF_ENUM_LITERAL) && my property->prop.tag == EXPR_LITERAL &&
        !csview_starts_with(expr_outer_span(&my property->prop).sv, "#")
    ) {
        // Through a declared index signature: `__index`, on the access and on the name (not of a type
        // parameter's constraint, not in element accesses: tsc has no symbol there)
        // Only types whose members have an `__index` symbol: declared ones and written type literals, not
        // what spreads and the like synthesize (tsc's getIndexSymbolFromSymbolTable)
        Ty const* root = object->kind == TY_ANONYMOUS ? object : nullptr;
        while (root && root->anonymous.target) root = root->anonymous.target;
        bool declared = object->kind == TY_DECLARED || object->kind == TY_REFERENCE ||
            (root && root->anonymous.node && !root->anonymous.object_literal);
        if (my property->form == PROPERTY_EXPR_DOT && declared &&
            index_symbol_compile_test_info(ctx, object, key, my inner_span, false)
        ) {
            expr_compile_test_info(&my property->expr, ctx);
            has_compiled_expr_info = true;
            index_symbol_compile_test_info(ctx, object, key, expr_outer_span(&my property->prop), false);
        }
    }

    bool dot = my property->form != PROPERTY_EXPR_BRACKET;
    if (!has_compiled_expr_info) {
        ctx->any_keeps_underline = dot; // The object of a property access
        expr_compile_test_info(&my property->expr, ctx);
    }

    if (!dot) {
        expr_compile_test_info(&my property->prop, ctx);
    } else if (!csview_starts_with(expr_outer_span(&my property->prop).sv, "#")) {
        ctx->any_keeps_underline = true;
        baseline_test_case_add_type(ctx, expr_outer_span(&my property->prop), ty_regular(current_checker, expr_get_type((Expr*)me)));
    }
}

void expr_compile_test_info_inner(Expr* me, BaselineTestCase* ctx) {
    switch (my tag) {
        case EXPR_IMPLICIT_ANY:
        case EXPR_LITERAL:
            if (my literal.tag == VALUE_UNDEFINED && csview_equals(my inner_span.sv, "undefined")) {
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
                target.sv = csview_from_range(start, span_end_ptr(target));
                // (A misspelled one, TS17012, has no symbol)
                if (my identifier.target && csview_equals(target.sv, "target")) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = symbol_declaration_spans(my identifier.target),
                        .span = target,
                        .name = my identifier.target->name_span,
                    });
                } else if (identifier_this_function(&my identifier) && csview_equals(target.sv, "target")) {
                    new_target_function_symbol(&my identifier, target, ctx);
                }
                baseline_test_case_add_type(ctx, target, ty_regular(current_checker, expr_get_type(me)));
            }
            break;

        case EXPR_BINARY_OP: {
            u32 overrides = ctx->symbol_override_count;
            if (binary_commonjs_declaration(my binary_op, nullptr)) commonjs_symbol_overrides(&my binary_op->left, ctx);
            if (my binary_op->op == BINARY_OP_EQUAL && my binary_op->left.inner_span.source &&
                my binary_op->left.inner_span.source->is_js
            ) {
                prototype_symbol_overrides(&my binary_op->left, ctx);
                this_property_symbol_overrides(&my binary_op->left, ctx);
                nested_expando_symbol_overrides(&my binary_op->left, ctx);
            }
            ctx->private_name_in_left = my binary_op->op == BINARY_OP_IN && my binary_op->left.tag == EXPR_PRIVATE_NAME;
            expr_compile_test_info(&my binary_op->left, ctx);
            ctx->private_name_in_left = false;
            while (ctx->symbol_override_count > overrides) {
                ctx->symbol_override_count--;
                cstr_drop(&ctx->symbol_overrides[ctx->symbol_override_count].text);
                vec_span_drop(&ctx->symbol_overrides[ctx->symbol_override_count].declarations);
            }
            if (binary_commonjs_declaration(my binary_op, nullptr)) commonjs_undefined_override(my binary_op, ctx);
            expr_compile_test_info(&my binary_op->right, ctx);
            while (ctx->symbol_override_count > overrides) {
                ctx->symbol_override_count--;
                cstr_drop(&ctx->symbol_overrides[ctx->symbol_override_count].text);
                vec_span_drop(&ctx->symbol_overrides[ctx->symbol_override_count].declarations);
            }
        }   break;

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
                Span spans[2] = { my inner_span, expr_outer_span(&my property->prop) };
                for (u32 i = 0; i < 2; i++) {
                    VecSpan declarations = vec_span_init();
                    vec_span_push(&declarations, (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) });
                    ctx->symbol_overrides[ctx->symbol_override_count++] = (typeof(ctx->symbol_overrides[0])){
                        .span = spans[i], .text = cstr_lit("module.exports"), .declarations = declarations,
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
            ModuleRef const* ref = my call->callable.tag == EXPR_IDENTIFIER ? identifier_module_ref(&my call->callable.identifier) : nullptr;
            Symbol const* module = ref ? ref->module : nullptr;
            if (module && vec_expr_size(&my call->args) && vec_expr_at(&my call->args, 0)->tag == EXPR_LITERAL) {
                // (a file's module is declared by the file, and named by its path without the extension)
                SourceFile const* file = module->module ? module->module->file : nullptr;
                Span file_start = file ? (Span){ .source = file, .sv = csview_from_n(source_file_start(file), 0) } : span_null;
                baseline_test_case_add_symbol(ctx, (BaselineSym){
                    .declarations = file ? vec_span_single(file_start) : declaration_get_spans(module),
                    .allow_declaration_empty = true,
                    .span = vec_expr_at(&my call->args, 0)->inner_span,
                    .text = file ? file_module_text(ctx, module) : symbol_text(ctx, module),
                });
            }
            for_each (i, vec_expr, &my call->args) {
                // The name `Object.defineProperty(o, "x", ...)` declares (tsc's getSymbolAtLocation of the literal)
                if (i_index == 1 && my call->declares) {
                    baseline_test_case_add_symbol(ctx, (BaselineSym){
                        .declarations = declaration_get_spans(my call->declares),
                        .span = i->inner_span,
                        .text = symbol_text(ctx, my call->declares),
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
                VecSpan merged = object_member_symbol_decls(&my object, i, &first);
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
                        if (i->shorthand_default) expr_compile_test_info(i->shorthand_default, ctx);
                    } else {
                        expr_compile_test_info(&i->value, ctx);
                    }
                    continue;
                }
                vec_span_drop(&merged);
                if (i->tag != OBJECT_ITEM_SPREAD) {
                    VecSpan declarations = {0};
                    ObjectExprItem const* named = i; // (named like its first declaration)
                    if ((i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER || i->tag == OBJECT_ITEM_METHOD) &&
                        !i->computed_key
                    ) {
                        // (Its symbol: with the other accessor or the other methods of its name, alone if it conflicts
                        // with what came first)
                        ObjectExprItem const* accessor_first = nullptr;
                        declarations = object_member_symbol_decls(&my object, i, &accessor_first);
                        if (vec_span_is_empty(&declarations)) vec_span_push(&declarations, i->span);
                        if (accessor_first && vec_span_size(&declarations) > 1) named = accessor_first;
                    } else if ((i->tag == OBJECT_ITEM_GETTER || i->tag == OBJECT_ITEM_SETTER) &&
                        (i->computed_key->tag == EXPR_LITERAL || expr_is_entity_name(i->computed_key))
                    ) {
                        // A late bound name: the accessors written with the same name (see the pairing in expr.c)
                        for_each (other, vec_object_expr_item, &my object) {
                            if ((other->tag == OBJECT_ITEM_GETTER || other->tag == OBJECT_ITEM_SETTER) && other->computed_key &&
                                csview_equals_sv(expr_outer_span(other->computed_key).sv, expr_outer_span(i->computed_key).sv)
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
                    if (i->tag == OBJECT_ITEM_SHORTHAND && i->shorthand_default && !my assignment_pattern) {
                        baseline_test_case_add_type(ctx, i->name_span, ty_mutable_location_type(current_checker, &i->value, nullptr));
                    } else {
                        baseline_test_case_add_type(ctx, i->name_span, method && i->jsdoc_type ?
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
                        if (i->shorthand_default) expr_compile_test_info(i->shorthand_default, ctx);
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
                    // An attribute declares a property of the attributes object
                    baseline_test_case_add_symbol(ctx, (BaselineSym){ .declarations = vec_span_single(i->span), .span = i->name });
                    baseline_test_case_add_type(ctx, i->name, ty_jsx_attribute_type(current_checker, me, i));
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

void expr_compile_test_info(Expr* me, BaselineTestCase* ctx) {
    if (my tag == EXPR_IMPLICIT_ANY && my missing_decorators) { // (tsc's MissingDeclaration: no expression of its own)
        for_each (i, vec_expr, my missing_decorators) expr_compile_test_info(i, ctx);
        return;
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
            Span parenthesized = span_make(my inner_span.source, start - 1, end + 1);
            baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), parenthesized, ty_regular(current_checker, expr_get_type(me)));
        }
    } else if (my tag == EXPR_PRIVATE_NAME && !ctx->private_name_in_left) {
        // (tsc's isExpressionNode: a private name is an expression only as the left operand of `in`)
    } else if (my tag == EXPR_GENERIC_PARAMS && my generic_params->heritage) {
        // (A class's `extends B<T>` is tsc's ExpressionWithTypeArguments, which the type writer skips)
    } else if (my tag == EXPR_AS_TYPE && my as_type->is_jsdoc && span_eq(expr_outer_span(me), expr_outer_span(&my as_type->expr))) {
        // `/** @satisfies {T} */ const x = v`: v isn't parenthesized, the satisfies expression isn't tsc's
    } else if (my tag == EXPR_IDENTIFIER && my identifier.keyword == KEYWORD_IDENTIFIER_IMPORT) {
        // The `import` of `import("m")` and `import.meta` isn't an expression of its own
    } else if (me == ctx->export_name && my identifier.target && (my identifier.target->flags & SYMBOL_VARIABLE)) {
        baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), my inner_span, ty_type_of_symbol(current_checker, my identifier.target));
    } else {
        // tsc prints an expression's regular type (getRegularTypeOfExpression)
        baseline_test_case_add_type_recursive(ctx, expr_outer_span(me), my inner_span, ty_regular(current_checker, expr_get_type(me)));
    }

    expr_compile_test_info_inner(me, ctx);
}

void statement_compile_test_info(Statement* me, BaselineTestCase* ctx) {
    if (statement_declares_name(my tag) && my decl.props.invalid_decorators) {
        for_each (i, vec_expr, my decl.props.invalid_decorators) expr_compile_test_info(i, ctx);
    }
    if ((my tag == STATEMENT_BREAK || my tag == STATEMENT_CONTINUE) && span_valid(my jump_label)) {
        // Both labels and the targets of break and continue are shown as any
        ctx->any_keeps_underline = true;
        baseline_test_case_add_type_node(ctx, my jump_label, &type_any);
    }
    switch (my tag) {
        case STATEMENT_LABELED:
            ctx->any_keeps_underline = true;
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
                    ctx->any_keeps_underline = true;
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
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && my span.source && my span.source->is_json) {
                if (expr_outer_span(&my expr).sv.size) expr_compile_test_info(&my expr, ctx);
                break;
            }
            // A number or string literal or `this` is no expression to tsc's type writer under an export assignment
            // (isInExpressionContext), a template, `-1` or anything parenthesized is
            if (my tag == STATEMENT_EXPORT_DEFAULT_EXPR && !expr_is_perenthisized(&my expr) && ((my expr.tag == EXPR_LITERAL &&
                (my expr.literal.tag == VALUE_NUMBER || my expr.literal.tag == VALUE_STRING ||
                    my expr.literal.tag == VALUE_BIGINT) &&
                csview_size(my expr.inner_span.sv) && !strchr("-+", my expr.inner_span.sv.buf[0])) ||
                (my expr.tag == EXPR_IDENTIFIER && my expr.identifier.keyword == KEYWORD_IDENTIFIER_THIS))
            ) {
                break;
            }
            if (my expr.tag != EXPR_LITERAL || my expr.literal.tag != VALUE_UNDEFINED ||
                csview_equals(my expr.inner_span.sv, "undefined")
            ) {
                ctx->any_keeps_underline = my tag == STATEMENT_EXPORT_DEFAULT_EXPR; // Of an export assignment
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
            if (my for_loop->condition.inner_span.sv.size || my for_loop->condition.missing) {
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
            for_each (i, vec_expr, &my import_export->decorators) expr_compile_test_info(i, ctx);
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
    for_each (i, vec_statement, &my statements) {
        statement_compile_test_info(i, ctx);
    }
}
