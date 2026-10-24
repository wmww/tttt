#include "string_helpers.h"
#include "ty/pseudo.h"
#include "ty/ty_internal.h"
#include "ty/decl_comments.h"
#include "ty/decl_map.h"

// Declaration maps: the file being written whose nodes a TyDisplay marks (null: none), see TyDisplay.no_map
#define D_MAP(d) ((d).comments && (d).comments->map && !(d).no_map ? (d).comments->file : nullptr)
#include "scope.h"
#include "program.h"
#include "function_impl.h"
#include "several_types.h"
#include "interface_member.h"
#include "statement.h"
#include "source_file.h"
#include "callable_type.h"
#include "symbol_chain.h"
#include "import_attributes.h"
#include "enum_decl.h"
#include "unicode_identifiers.h"
#include "port_statement.h"
#include "class_decl.h"
#include "module_resolution.h"

// Prints a Ty the way tsc's typeToString() does, in its order of preference: alias name, reference name + arguments
// (array and tuple syntax for those targets), declared name, structure.

// A safety net: a message's type nested deeper than four times this prints `...` (the truncation length and tsc's own
// elision of an anonymous type nested eleven times, anonymous_elided(), come long before)
#define MAX_DISPLAY_DEPTH 11

// A message type is cut short (tsc's typeToString without noErrorTruncation, the CLI's default; the test harness always
// sets it): the stand-in depth limits apply and the elided information placeholder prints `...`, not `any`
// (A message's type printed from where its expression is, reuse_nodes, is one too)
static bool truncating(TyDisplay d) {
    return (!d.enclosing || d.reuse_nodes) && !d.types_baseline && !d.tracker && !compiler_options()->no_error_truncation;
}

// The length past which declaration emit doesn't serialize a type (tsc's noTruncationMaximumTruncationLength)
#define DECLARATION_MAX_LENGTH 1000000

// tsc's noTruncationMaximumTruncationLength: where nothing is cut short (the types baseline, noErrorTruncation), a type
// is still elided past this length
#define NO_TRUNCATION_MAXIMUM_LENGTH 1000000

// tsc's defaultMaximumTruncationLength: a message's type longer than this (as far as tsc's approximateLength tells) is
// cut short from there on
#define DEFAULT_MAXIMUM_TRUNCATION_LENGTH 160

// A message's type is past the truncation length (tsc's checkTruncationLength): object types print as the elided
// information placeholder `...`, type argument lists as `<...>`, a type literal's members as `... N more ...` and its
// last one. Nothing past that point is worked out, which is what keeps a message about a huge mapped type cheap
// (expect-type's MismatchInfo of an interface of a hundred methods: tsgo resolves the first few properties' types).
// The length is tsc's approximateLength (TyDisplay.approx), counted where its node builder counts: keywords and
// literals, names, list separators, members, signatures and parameters, the fixed parts of mapped, conditional,
// keyof, indexed access and template types, and a reused annotation by its text
static bool display_over(TyDisplay d) {
    return (d.over && *d.over) ||
        (d.approx && *d.approx > (d.cut_off ? DEFAULT_MAXIMUM_TRUNCATION_LENGTH : NO_TRUNCATION_MAXIMUM_LENGTH));
}

// The depth limits elided a type (tsc's visitAndTransformType sets context.truncating): the rest of the print is past
// the truncation length
static void set_over(TyDisplay d) {
    if (d.over) *d.over = true;
}

// (A macro: what is counted is only worked out for a message)
#define approx_add(d, n) do { if ((d).approx) *(d).approx += (u32)(n); } while (0)

static void out(TyDisplay d, csview sv) {
    if (d.tracker && d.tracker->truncated) return;
    cstr_append_sv(d.target, sv);
    // (Not what the declaration transformer visits: only the node builder serializes with a limit)
    if (d.tracker && !d.transformer_node && cstr_size(d.target) - d.target_start > DECLARATION_MAX_LENGTH) {
        d.tracker->truncated = true;
        d.tracker->report(d.tracker, DECL_REPORT_TRUNCATION, c_sv(""));
    }
}
#define out_lit(d, s) out(d, c_sv(s))

// Declaration maps: the output is where `at` (a position in the file being written) starts or ends a node
static void map_at(TyDisplay d, char const* at) {
    if (!D_MAP(d) || !at || (d.tracker && d.tracker->truncated)) return;
    char const* text = cstr_str(&D_MAP(d)->contents);
    if (at < text || at > text + cstr_size(&D_MAP(d)->contents)) return;
    decl_map_mark(d.target, (u32)(at - text));
}

// (A span's end may take in the whitespace after it: a node ends at its last token)
static void map_span(TyDisplay d, Span span, bool end) {
    if (D_MAP(d) && span_valid(span)) map_at(d, end ? decl_map_node_end(D_MAP(d), span_end_ptr(span), span.buf) : span.buf);
}

// A name as written, its start and end mapped
static void out_mapped(TyDisplay d, Span span) {
    map_span(d, span, false);
    out(d, span_sv(span));
    map_span(d, span, true);
}

// A token as written (`?`, `...`): `text` printed, mapped from where it is to its end
static void out_token_mapped(TyDisplay d, char const* at, csview text) {
    map_at(d, at);
    out(d, text);
    if (D_MAP(d) && at) map_at(d, at + text.size);
}

// Declaration maps: where a token that follows `from` is (past trivia), if it is `c`
static char const* token_after(TyDisplay d, char const* from, char c) {
    if (!D_MAP(d) || !from) return nullptr;
    char const* end = cstr_str(&D_MAP(d)->contents) + cstr_size(&D_MAP(d)->contents);
    char const* at = decl_map_skip_trivia(from, end);
    return at < end && *at == c ? at : nullptr;
}

// A parameter's start (past its decorators and modifiers: tsc's node starts at them, but declaration emit makes a new
// one without them where there were any... the transformer's updateParameterDeclaration keeps the range)
// (An identifier's first character: a JSDoc node starting with one maps at it, others at their line's `*`)
static bool word_start_char(char c) { return isalpha((unsigned char)c) || c == '_' || c == '$' || (unsigned char)c >= 0x80; }

static char const* arg_start(FunctionArg const* arg) { return span_valid(arg->span) ? arg->span.buf : nullptr; }

// An object type's braces and members: `{ a: T; }` on one line, or declaration emit's one member per line (see
// TyDisplay.multiline). brace_open() returns what the members are printed with
static TyDisplay brace_open(TyDisplay d) {
    out(d, d.multiline ? c_sv("{\n") : c_sv("{ "));
    d.indent++;
    return d;
}

static void out_indent(TyDisplay d) {
    static char const spaces[] = "                                                                ";
    for (u32 n = d.indent * 4; n;) {
        u32 now = n < sizeof spaces - 1 ? n : (u32)sizeof spaces - 1;
        out(d, (csview){ spaces, now });
        n -= now;
    }
}

// Before a member (with the members' TyDisplay; not after a leading comment on its line)
static void member_begin(TyDisplay d) {
    isize_t size = cstr_size(d.target);
    if (d.multiline && (!size || cstr_str(d.target)[size - 1] == '\n')) out_indent(d);
}

static void member_end(TyDisplay d) {
    out(d, d.multiline ? c_sv(";\n") : c_sv("; "));
}

// Declaration emit: where a property printed from its type was declared, whose comments it copies (tsgo's
// setCommentRange of its valueDeclaration in addPropertyToElementList, or of the signature's declaration: `index`)
static Span prop_comment_span(TyDisplay d, Prop const* prop, u32 index) {
    if (!d.comments || !d.multiline) return span_null;
    // (Not of a property without a valueDeclaration: a mapped type's, a spread's fresh copy... see check.c's
    // no_value_declaration())
    for (Prop const* i = prop; i; i = i->target) {
        if (i->mapped || (i->flags & PROP_NO_VALUE_DECLARATION) || prop_decl_rare(i->decl)->reverse_of) return span_null;
    }
    while (prop->target) prop = prop->target;
    u32 count = prop->decl->decl_span_count;
    if (!count) return span_null;
    return prop->decl->decl_spans[index < count ? index : 0];
}

// A reused member's leading comments, and its end with its trailing ones (declaration emit's, decl_comments.h)
static void member_begin_node(TyDisplay d, Span declaration) {
    if (d.multiline && d.comments && span_valid(declaration)) decl_comments_leading(d.comments, d.target, d.indent, declaration.buf);
    member_begin(d);
}

static void member_end_node_mapped(TyDisplay d, Span declaration, char const* separators, bool mapped) {
    if (!d.multiline || !d.comments || !span_valid(declaration)) {
        member_end(d);
        return;
    }
    out_lit(d, ";");
    char const* end = decl_comments_end(d.comments, span_end_ptr(declaration), separators);
    if (mapped) map_at(d, end);
    decl_comments_trailing(d.comments, d.target, d.indent, end);
    out_lit(d, "\n");
}

// (A member made anew: its comments are the declaration's, it has no position of its own)
static void member_end_node(TyDisplay d, Span declaration, char const* separators) {
    member_end_node_mapped(d, declaration, separators, false);
}

// (With the TyDisplay brace_open() was given)
static void brace_close(TyDisplay d) {
    if (d.multiline) out_indent(d);
    out(d, c_sv("}"));
}

static void out_fmt_more(TyDisplay d, u32 count) {
    char more[48];
    snprintf(more, sizeof more, "... %u more ...; ", count);
    out(d, csview_from(more));
}

// The elided information placeholder (tsc's createElidedInformationPlaceholder): `...` in a message, `any` where
// nothing truncates (the types baseline, declaration emit, noErrorTruncation)
// (Whether tsc's NoTruncation flag is off: a message cut short, display_cut_off(), or a signature's, which never sets it)
static void out_elided(TyDisplay d) {
    approx_add(d, 3);
    out(d, d.cut_off ? c_sv("...") : c_sv("any"));
}

// Past the truncation length in a list, `... N more ...` stands for the members left out in a message, followed by
// `separator`; where nothing truncates it's `any` (tsc's mapToTypeNodes: a keyword with the text as a comment)
static void out_more(TyDisplay d, u32 count, char const* separator) {
    if (!d.cut_off) {
        out_lit(d, "any");
        out(d, csview_from(separator));
        return;
    }
    char more[48];
    snprintf(more, sizeof more, "... %u more ...%s", count, separator);
    out(d, csview_from(more));
}

typedef struct PrintedRange {
    isize_t start, end;
} PrintedRange;
static PrintedRange printed_range_start(TyDisplay d) {
    return (PrintedRange){ .start = cstr_size(d.target) };
}
static void printed_range_end(TyDisplay d, PrintedRange* range) {
    range->end = cstr_size(d.target);
}
// Prints again what was printed in `range` (tsc's serializedTypes cache: the same type printed twice is the same node,
// type parameter names and all, and isn't worked out twice)
static void reprint_range(TyDisplay d, PrintedRange range) {
    if (d.tracker && d.tracker->truncated) return;
    cstr text = cstr_from_sv(csview_slice(cstr_sv(d.target), range.start, range.end));
    cstr_append_sv(d.target, cstr_sv(&text));
    cstr_drop(&text);
}


typedef struct TyPrinted {
    Ty const* ty;
    cstr const* target;
    PrintedRange range;
    u32 names_used; // How many of the type parameter names given it depends on (see param_names_leave())
    u32 added; // What it added to the approximate length (tsc's SerializedTypeEntry.addedLength)
    bool over; // The print was past the truncation length after it (SerializedTypeEntry.truncating)
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

// Around a print that may be remembered: which type parameter names it used (TyParamNames.used)
static u32 names_used_start(TyDisplay d) {
    if (!d.param_names) return 0;
    u32 outer = d.param_names->used;
    d.param_names->used = 0;
    return outer;
}
static u32 names_used_end(TyDisplay d, u32 outer) {
    if (!d.param_names) return 0;
    u32 used = d.param_names->used;
    if (outer > d.param_names->used) d.param_names->used = outer;
    return used;
}

static void remember_printed(TyDisplay d, Ty const* ty, PrintedRange range, u32 added, u32 names_used) {
    TyParamNames* names = d.param_names;
    if (!names) return;
    if (names->printed_len == names->printed_cap) {
        names->printed_cap = names->printed_cap ? names->printed_cap * 2 : 8;
        names->printed = realloc(names->printed, names->printed_cap * sizeof(TyPrinted));
    }
    names->printed[names->printed_len++] = (TyPrinted){
        .ty = ty, .target = d.target, .range = range, .names_used = names_used, .added = added, .over = d.over && *d.over,
    };
}

// What was printed into `from`, a buffer of its own, is inserted into `d`'s target at `at`: what is remembered of it is
// now there, what was remembered after `at` moves
static void insert_printed(TyDisplay d, isize_t at, cstr* from) {
    isize_t len = cstr_size(from);
    cstr_insert_sv(d.target, at, cstr_sv(from));
    TyParamNames* names = d.param_names;
    for (u32 i = 0; names && i < names->printed_len; i++) {
        TyPrinted* printed = &names->printed[i];
        if (printed->target == from) {
            printed->target = d.target;
            printed->range.start += at;
            printed->range.end += at;
        } else if (printed->target == d.target && printed->range.start >= at) {
            printed->range.start += len;
            printed->range.end += len;
        }
    }
    cstr_drop(from);
}

// What was printed into `from`, a buffer of its own, is appended to `d`'s target: what is remembered of it is now
// there (a later buffer at the same address would reprint from the wrong text)
static void append_printed(TyDisplay d, cstr* from) {
    isize_t base = cstr_size(d.target);
    cstr_append_sv(d.target, cstr_sv(from));
    TyParamNames* names = d.param_names;
    for (u32 i = 0; names && i < names->printed_len; i++) {
        TyPrinted* printed = &names->printed[i];
        if (printed->target != from) continue;
        printed->target = d.target;
        printed->range.start += base;
        printed->range.end += base;
    }
    cstr_drop(from);
}

// A type printed before in this print is printed again the same, in whole, whatever the length is by now (tsc's
// visitAndTransformType's serializedTypes cache: the cached node, its addedLength, and its truncating)
static bool reprint_printed(TyDisplay d, Ty const* ty) {
    TyPrinted const* before = printed_before(d, ty);
    if (!before) return false;
    reprint_range(d, before->range);
    approx_add(d, before->added);
    if (d.param_names->used < before->names_used) d.param_names->used = before->names_used;
    if (before->over) set_over(d);
    return true;
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
    if (type_decl && type_decl->local && !d.tracker && d.types_baseline) return false;
    if (!d.tracker && !d.types_baseline) return true;
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

// tsc's typesAreSameReference: the same type, or types with the same symbol (an interface's or class's references, a
// type literal's instantiations); null for those without (intersections, unions... an alias's instantiations of one)
static void const* reference_identity(Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    if (ty->kind == TY_DECLARED) return ty->declared.symbol;
    if (ty->kind == TY_ANONYMOUS) {
        for (u32 depth = 0; ty->anonymous.target && ty->anonymous.target->kind == TY_ANONYMOUS && depth < 64; depth++) ty = ty->anonymous.target;
        return ty;
    }
    return nullptr;
}

static bool same_reference(Ty const* a, Ty const* b) {
    if (a == b) return true;
    void const* x = reference_identity(a);
    return x && x == reference_identity(b);
}

// tsc's mapToTypeNodes: members of a list (union, intersection, tuple, type arguments) that print the same name, not
// all the same reference (`[Yep, Yep]` of two namespaces, `X<A<1>, A<2>>` of an alias), are printed again fully
// qualified after the list (counted twice: see ListReprint). Null if none are; else which are (to free)
static uint8_t* name_collisions(Checker* me, TyDisplay d, TyList list) {
    // (Printed from a place outside a message, nothing changes: the same text, nothing counted)
    if ((d.enclosing && !d.approx) || d.fully_qualified || list.len < 2 || list.len > 256) return nullptr;
    Symbol const* stack_symbols[16];
    Symbol const** symbols = list.len <= c_arraylen(stack_symbols) ? stack_symbols : malloc(sizeof(Symbol const*) * list.len);
    u32 named = 0;
    for (u32 i = 0; i < list.len; i++) {
        symbols[i] = reference_name_symbol(me, d, list.data[i]);
        named += symbols[i] != nullptr;
    }
    uint8_t* ret = nullptr;
    for (u32 i = 0; named >= 2 && i < list.len; i++) {
        if (!symbols[i] || (ret && ret[i])) continue;
        // (The group of i's name, the first of it: tsc's arrayIsHomogeneous compares each with the first)
        bool first = true;
        for (u32 k = 0; k < i && first; k++) first = !symbols[k] || !csview_equals_sv(symbols[k]->name, symbols[i]->name);
        if (!first) continue;
        bool homogeneous = true;
        for (u32 j = i + 1; j < list.len && homogeneous; j++) {
            if (!symbols[j] || !csview_equals_sv(symbols[j]->name, symbols[i]->name)) continue;
            // (An interface or class merged across programs is one symbol: binder_redirect())
            homogeneous = same_reference(list.data[i], list.data[j]) ||
                (!shows_alias(d, list.data[i]) && !shows_alias(d, list.data[j]) && same_symbol(me, symbols[i], symbols[j]));
        }
        if (homogeneous) continue;
        if (!ret) ret = calloc(list.len, 1);
        for (u32 j = i; j < list.len; j++) {
            if (symbols[j] && csview_equals_sv(symbols[j]->name, symbols[i]->name)) ret[j] = 1;
        }
    }
    if (symbols != stack_symbols) free((void*)symbols);
    return ret;
}

// The members of a list printed again (name_collisions()): tsc prints each plainly, then the colliding ones again with
// UseFullyQualifiedType, replacing them; both count towards the approximate length
typedef enum { LIST_ITEM_PLAIN, LIST_ITEM_PARENS, LIST_ITEM_UNION_MEMBER } ListItemHow;
typedef struct ListReprint {
    uint8_t const* collisions;
    u32 count;
    struct { isize_t start, end; Ty const* ty; uint8_t how; bool parens; } items[];
} ListReprint;

static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens);
static void display_union_member(Checker* me, TyDisplay d, Ty const* ty);

static void display_list_item(Checker* me, TyDisplay d, Ty const* ty, ListItemHow how, bool parens) {
    switch (how) {
        case LIST_ITEM_PLAIN: ty_display(me, d, ty); return;
        case LIST_ITEM_PARENS: display_parenthesized_if(me, d, ty, parens); return;
        case LIST_ITEM_UNION_MEMBER: display_union_member(me, d, ty); return;
    }
}

static ListReprint* list_reprint_new(uint8_t const* collisions, u32 len) {
    if (!collisions) return nullptr;
    ListReprint* ret = malloc(sizeof(ListReprint) + sizeof(ret->items[0]) * len);
    panic_if(!ret, "out of memory");
    ret->collisions = collisions;
    ret->count = 0;
    return ret;
}

// Prints the list's i'th member, remembering it if it is printed again
static void list_item(Checker* me, TyDisplay d, ListReprint* reprint, u32 i, Ty const* ty, ListItemHow how, bool parens) {
    isize_t start = cstr_size(d.target);
    display_list_item(me, d, ty, how, parens);
    if (!reprint || !reprint->collisions[i]) return;
    reprint->items[reprint->count].start = start;
    reprint->items[reprint->count].end = cstr_size(d.target);
    reprint->items[reprint->count].ty = ty;
    reprint->items[reprint->count].how = (uint8_t)how;
    reprint->items[reprint->count++].parens = parens;
}

// After the list: the remembered members again, fully qualified, in their places (frees `reprint`)
static void list_reprint_finish(Checker* me, TyDisplay d, ListReprint* reprint) {
    if (!reprint) return;
    u32 n = reprint->count;
    cstr* texts = calloc(n ? n : 1, sizeof(cstr));
    panic_if(!texts, "out of memory");
    for (u32 k = 0; k < n; k++) {
        TyDisplay q = d;
        q.target = &texts[k];
        q.target_start = 0;
        // (Printed from a place, its names are qualified as needed from there already: tttt's fully_qualified is for
        // messages' bare names only. The reprint is past the truncation length more often: `SpreadTwo<...>`)
        // (Without yielding a module: the message's flags keep UseAliasDefinedOutsideCurrentScope, an ambient module's
        // export or an imported alias stays its bare name)
        q.fully_qualified = q.qualified_without_modules = !d.enclosing;
        display_list_item(me, q, reprint->items[k].ty, (ListItemHow)reprint->items[k].how, reprint->items[k].parens);
    }
    // (Prints remembered into the scratch texts go with them)
    for (u32 i = 0; d.param_names && i < d.param_names->printed_len; i++) {
        TyPrinted* printed = &d.param_names->printed[i];
        if (printed->target >= texts && printed->target < texts + n) printed->ty = nullptr;
    }
    for (u32 k = n; k-- > 0;) {
        isize_t start = reprint->items[k].start, end = reprint->items[k].end;
        if (cstr_size(&texts[k]) == end - start && !memcmp(cstr_str(&texts[k]), cstr_str(d.target) + start, (size_t)(end - start))) {
            cstr_drop(&texts[k]);
            continue;
        }
        // (The prints remembered for reuse, TyPrinted: those inside go, those after move)
        isize_t delta = cstr_size(&texts[k]) - (end - start);
        for (u32 i = 0; d.param_names && i < d.param_names->printed_len; i++) {
            TyPrinted* printed = &d.param_names->printed[i];
            if (printed->target != d.target || printed->range.end <= start) continue;
            if (printed->range.start >= end) {
                printed->range.start += delta;
                printed->range.end += delta;
            } else {
                printed->ty = nullptr;
            }
        }
        cstr tail = cstr_from_sv(csview_slice(cstr_sv(d.target), end, cstr_size(d.target)));
        cstr_resize(d.target, start, '\0');
        cstr_append_s(d.target, texts[k]);
        cstr_append_s(d.target, tail);
        cstr_drop(&tail);
        cstr_drop(&texts[k]);
    }
    free(texts);
    free(reprint);
}

// Type arguments. The first one is parenthesized if it's a generic function type (tsc's parenthesizeTypeArguments:
// `X<<T>() => T>` would start with `<<`)
static void display_list(Checker* me, TyDisplay d, TyList list) {
    // (A type argument list past the truncation length: tsc's mapToTypeNodes, not a bare list)
    if (list.len && display_over(d)) {
        out_elided(d);
        return;
    }
    uint8_t* collisions = name_collisions(me, d, list);
    ListReprint* reprint = list_reprint_new(collisions, list.len);
    for (u32 i = 0; i < list.len; i++) {
        if (i) out_lit(d, ", ");
        // (Past the truncation length: how many more and the last one, tsc's mapToTypeNodes)
        if (display_over(d) && i + 3 < list.len - 1) {
            out_more(d, list.len - (i + 1), ", ");
            i = list.len - 1;
        }
        approx_add(d, 2); // (tsc's mapToTypeNodes: whitespace and separator)
        // (A generic function type first isn't parenthesized, `X<<T>() => T>`: tsgo's printer has no
        // parenthesizeLeadingTypeArgument)
        list_item(me, d, reprint, i, list.data[i], LIST_ITEM_PLAIN, false);
    }
    list_reprint_finish(me, d, reprint);
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
    return ty_is_non_local_function(symbol);
}

// A function with expando properties: tsgo has no bindPotentiallyMissingNamespaces, so it is a function like any other,
// whose type prints structurally (shouldWriteTypeOfFunctionSymbol wants UseTypeOfFunction or recursion):
// `{ (): void; p6: number; }`
static bool is_nested_expando_function(Ty const* ty) {
    Symbol const* symbol = ty->declared.kind == TY_DECLARED_NAMESPACE ? ty->declared.namespace_symbol : nullptr;
    if (!symbol) symbol = ty->declared.symbol;
    if (!symbol || !(symbol->flags & SYMBOL_FUNCTION)) return false;
    // (A namespace merged in makes it `typeof f` only when it's instantiated, tsc's createAnonymousTypeNode asking
    // for SymbolFlags.ValueModule: nodemailer's `declare namespace nmfetch { export type Options = ... }`)
    if (symbol->flags & SYMBOL_VALUE_MODULE) return false;
    bool expando = false;
    for (u32 i = 0; i < symbol->decl_count; i++) {
        SymbolDecl const* decl = &symbol->decls[i];
        if (decl->kind == SYMBOL_DECL_CLASS) return false;
        expando |= decl->kind == SYMBOL_DECL_EXPANDO;
    }
    return expando;
}

// A global whose name a parameter of the signatures being printed shadows: named through globalThis (tsc's
// lookupSymbolChain from the signature's fake scope)
static bool shadowed_global(Checker* me, TyDisplay d, Symbol const* symbol) {
    if (!symbol || !d.printed_params) return false;
    bool shadowed = false;
    for (TyDisplayParams const* i = d.printed_params; i && !shadowed; i = i->outer) {
        for (u32 j = 0; j < i->count && !shadowed; j++) shadowed = i->params[j].name && csview_equals_sv(atom_sv(i->params[j].name), symbol->name);
    }
    return shadowed && program_get_global(my program, symbol->name, SYMBOL_VALUE) == symbol;
}

// Declaration emit (TyDisplay.tracker) prints a function's own type by its name, `typeof f`, where that name is
// accessible as a value (tsc's declarationEmitNodeBuilderFlags have UseTypeOfFunction, and UseStructuralFallback asks
// isValueSymbolAccessible); not at the function's own declaration, whose type is what's being written
static bool typeof_function_in_declaration(TyDisplay d, Symbol const* symbol) {
    if (!d.tracker || !symbol || !d.enclosing || !symbol_value_accessible_at(d.enclosing, symbol)) return false;
    // (A function declaration's type isn't what its declaration writes: its signature is)
    for (u32 i = 0; span_valid(d.at) && i < symbol->decl_count; i++) {
        if (symbol->decls[i].kind == SYMBOL_DECL_FUNCTION) continue;
        if (span_eq(symbol->decls[i].name, d.at) || span_contains_span(symbol->decls[i].full, d.at)) return false;
    }
    return true;
}

static void display_quoted_string(TyDisplay d, csview sv, bool single, bool ascii);
static bool is_identifier_name(csview name);

// The string literal attributes of an ambient module's declaration (`declare module "*.css" with { type: "css" }`)
static u32 module_attributes_count(Symbol const* module) {
    if (!module->is_module || !module->module || !module->module->attributes) return 0;
    u32 count = 0;
    for_each (i, vec_interface_member, &module->module->attributes->interface.members) {
        count += i->key.tag == INTERFACE_KEY_NAME && !i->is_method && i->type.tag == TYPE_VALUE && i->type.value.tag == VALUE_STRING;
    }
    return count;
}

// `, { with: { type: "css" } }` of them, by name (tsgo's createImportAttributesForModuleSpecifier with
// getTypeOfModuleImportAttributes)
static void out_module_attributes(TyDisplay d, Symbol const* module) {
    struct { csview name; csview value; } found[16];
    u32 count = 0;
    for_each (i, vec_interface_member, &module->module->attributes->interface.members) {
        if (count == c_arraylen(found)) break;
        if (i->key.tag != INTERFACE_KEY_NAME || i->is_method || i->type.tag != TYPE_VALUE || i->type.value.tag != VALUE_STRING) continue;
        csview name = cstr_sv(&i->key.name_string);
        u32 at = count++;
        while (at > 0 && csview_cmp(&found[at - 1].name, &name) > 0) {
            found[at] = found[at - 1];
            at--;
        }
        found[at].name = name;
        found[at].value = cstr_sv(&i->type.value.string);
    }
    out_lit(d, ", { with: { ");
    for (u32 i = 0; i < count; i++) {
        if (i) out_lit(d, ", ");
        if (is_identifier_name(found[i].name)) out(d, found[i].name);
        else display_quoted_string(d, found[i].name, false, true);
        out_lit(d, ": ");
        display_quoted_string(d, found[i].value, false, true);
    }
    out_lit(d, " } }");
}

// symbol_chain_at() from where the display is, inside the import or export whose name is printed if it is
static SymbolChain display_chain_at(TyDisplay d, Symbol const* symbol, u32 meaning, bool yield_module) {
    if (d.usage_ref) symbol_chain_set_usage(d.usage_ref, !d.usage_in_fake_scope);
    SymbolChain chain = symbol_chain_at(d.enclosing, symbol, meaning, yield_module);
    if (d.usage_ref) symbol_chain_set_usage(nullptr, false);
    return chain;
}

// `import("m")` for a module at the root of a name (tsc's symbolToTypeNode: an ImportTypeNode), named from a place
// as program_module_display_name() says
static void out_module_import(Checker* me, TyDisplay d, Symbol const* module) {
    // An ES module named from a file of the other format under node16/nodenext resolution says how to resolve it
    // (tsc's symbolToTypeNode: the resolution-mode import attribute)
    ModuleResolutionKind resolution = compiler_options()->module_resolution;
    SourceFile const* target = module->module ? module->module->file : nullptr;
    SourceFile const* context = d.enclosing && span_valid(d.at) ? span_source(d.at) : nullptr;
    bool resolution_mode = (resolution == MODULE_RESOLUTION_NODE16 || resolution == MODULE_RESOLUTION_NODENEXT) &&
        target && context && target->has_format && target->is_esm && !(context->has_format && context->is_esm);
    // (Remembered: computing a module specifier walks the package's exports, paths and symlinks, and a printed
    // declaration type can name the same module hundreds of times)
    SpecifierMode override = resolution_mode ? SPECIFIER_MODE_ESM : SPECIFIER_MODE_NONE;
    ModuleRef const* usage = d.usage_in_fake_scope ? nullptr : d.usage_ref;
    u64 key[5] = { KEY_MODULE_DISPLAY_NAME, (u64)(uintptr_t)module, (u64)(uintptr_t)context, override, (u64)(uintptr_t)usage };
    csview const* cached = intern_get(&my links, key, 5);
    if (!cached) {
        cstr computed = program_module_display_name(my program, module, context, override, usage);
        csview* copy = arena_new(&my arena, csview);
        char* chars = arena_new_n(&my arena, char, cstr_size(&computed) + 1);
        memcpy(chars, cstr_str(&computed), (size_t)cstr_size(&computed));
        *copy = (csview){ .buf = chars, .size = cstr_size(&computed) };
        cstr_drop(&computed);
        *intern_slot(me, &my links, key, 5) = copy;
        cached = copy;
    }
    csview name = *cached;
    out_lit(d, "import(");
    if (name.size >= 2 && name.buf[0] == '\'') {
        // (a string literal the printer makes: double quotes)
        out_lit(d, "\"");
        out(d, csview_slice(name, 1, name.size - 1));
        out_lit(d, "\"");
    } else {
        out(d, name);
    }
    // The attributes of the import or export the name is printed for, when its specifier is this module (tsgo's
    // createImportAttributesForModuleSpecifier: `resolution-mode` first, then the string-valued ones by name)
    ImportAttributes const* attributes = d.usage_attributes && usage && usage->module == module ?
        d.usage_attributes : nullptr;
    struct { csview name; csview value; } found[16];
    u32 count = 0;
    if (attributes) {
        for_each (i, vec_import_attribute, &attributes->elements) {
            if (count == c_arraylen(found)) break;
            if (i->value.tag != EXPR_LITERAL || i->value.literal.tag != VALUE_STRING) continue;
            csview name = span_sv(i->name);
            if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'')) name = csview_slice(name, 1, name.size - 1);
            u32 at = count++;
            while (at > 0 && csview_cmp(&found[at - 1].name, &name) > 0) {
                found[at] = found[at - 1];
                at--;
            }
            found[at].name = name;
            found[at].value = cstr_sv(&i->value.literal.string);
        }
    } else if (module_attributes_count(module)) {
        out_module_attributes(d, module);
    }
    if (resolution_mode || count) {
        out_lit(d, ", { with: { ");
        if (resolution_mode) out_lit(d, "\"resolution-mode\": \"import\"");
        for (u32 i = 0; i < count; i++) {
            if (i || resolution_mode) out_lit(d, ", ");
            if (is_identifier_name(found[i].name)) out(d, found[i].name);
            else display_quoted_string(d, found[i].name, false, true);
            out_lit(d, ": ");
            display_quoted_string(d, found[i].value, false, true);
        }
        out_lit(d, " } }");
    }
    out_lit(d, ")");
}

// What tsc's createAccessFromSymbolChain counts for a name: each part's length and one, the first part's twice (it
// counts it when it starts the chain and again with every part), nothing for a module's `import("m")` (counted as its
// specifier by symbolToTypeNode, and printed in messages only where that isn't reached)
static u32 entity_name_length(csview text) {
    if (csview_starts_with(text, "import(")) return (u32)text.size;
    isize_t first = 0;
    while (first < text.size && text.buf[first] != '.') first++;
    return (u32)text.size + 1 + (u32)first + 1;
}

static void out_symbol_name_impl(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning);
// A name counts its text and one more (tsc's createAccessFromSymbolChain: each part's length and one)
static void out_symbol_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    if (!d.approx || d.naming) {
        out_symbol_name_impl(me, d, symbol, meaning);
        return;
    }
    isize_t start = cstr_size(d.target);
    TyDisplay inner = d;
    inner.naming = true;
    out_symbol_name_impl(me, inner, symbol, meaning);
    approx_add(d, entity_name_length(csview_slice(cstr_sv(d.target), start, cstr_size(d.target))));
}

// tsc's UseFullyQualifiedType without an enclosing declaration (two types that print the same in a message, or list
// members whose names collide): getSymbolChain from the globals alone, so a global (a UMD global's namespace too) is
// its name, anything else is named from its module, `import("/path/to/file")` (no context file: the module's path),
// except in a list (qualified_without_modules)
static void out_fully_qualified_name(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    Scope const* globals = program_get_binder(my program)->global_scope;
    SymbolChain chain = symbol->flags & SYMBOL_TYPE_PARAMETER || !globals ?
        (SymbolChain){ .items = { symbol }, .len = 1 } : symbol_chain_at(globals, symbol, meaning, !d.qualified_without_modules);
    u32 start = 0;
    if (chain.len && !chain.items[0]->is_module && symbol_is_augmented_export_equals(chain.items[0])) {
        // (What an augmented `export =` assigns stands for its module: the file of its first declaration)
        SourceFile const* file = chain.items[0]->decl_count ? span_source(chain.items[0]->decls[0].full) : nullptr;
        Symbol const* module = file && file->scope ? file->scope->container : nullptr;
        if (module && module->is_module && module->module) {
            out_module_import(me, d, module);
            start = 1;
        }
    } else if (chain.len && chain.items[0]->is_module) {
        out_module_import(me, d, chain.items[0]);
        start = 1;
    }
    cstr text = cstr_init();
    SymbolChain rest = { .len = chain.len - start };
    memcpy(rest.items, chain.items + start, rest.len * sizeof(*rest.items));
    if (start && rest.len) cstr_append_lit(&text, ".");
    symbol_chain_append_entity(&text, globals, &rest, true, start != 0);
    out(d, cstr_sv(&text));
    cstr_drop(&text);
}

static void out_symbol_name_impl(Checker* me, TyDisplay d, Symbol const* symbol, u32 meaning) {
    if (d.tracker) d.tracker->track_symbol(d.tracker, symbol, meaning);
    if (!d.enclosing && !d.fully_qualified && symbol_is_augmented_export_equals(symbol) && symbol->decl_count) {
        // What an augmented `export =` assigns is a module to tsc's printer (see below), in a message too
        SourceFile const* file = span_source(symbol->decls[0].full);
        out_lit(d, "import(\"");
        out(d, file ? source_file_display_path(file) : c_sv(""));
        out_lit(d, "\")");
        return;
    }
    if ((!d.enclosing && !d.fully_qualified) || (symbol->is_global_this && !d.enclosing)) {
        out(d, span_sv(ty_symbol_display_name(symbol)));
        return;
    }
    if (!d.enclosing) {
        out_fully_qualified_name(me, d, symbol, meaning);
        return;
    }
    // An augmentation that merged into nothing (TS2671/TS2649) is a module symbol of its own to tsgo, without a file:
    // named by its name (getSpecifierForModuleSymbol, IsModuleWithStringLiteralName)
    if (symbol->is_augmentation && !symbol->is_module && symbol->name.size >= 2) {
        out_lit(d, "import(\"");
        out(d, csview_slice(symbol->name, 1, symbol->name.size - 1));
        out_lit(d, "\")");
        return;
    }
    // tsc's symbolToTypeNode: the chain accessible from here, `import("m")` when it starts at a module. Not in a
    // message (typeToString's UseAliasDefinedOutsideCurrentScope: no module symbol is yielded, what nothing reaches is
    // its bare name)
    SymbolChain chain = display_chain_at(d, symbol, meaning, d.types_baseline || d.tracker);
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
        SourceFile const* file = span_source(module->decls[0].full);
        if (d.tracker && file && d.tracker->track_module_file) d.tracker->track_module_file(d.tracker, file, symbol);
        csview ambient = {0};
        for (u32 i = 0; i < module->decl_count && !ambient.size; i++) {
            if (module->decls[i].kind != SYMBOL_DECL_NAMESPACE) continue;
            NamespaceDecl const* ns = module->decls[i].node;
            if (!ns->is_ambient_module || ns->name.size < 2) continue;
            csview name = csview_slice(span_sv(ns->name), 1, ns->name.size - 1);
            if (!ns->is_augmentation || !module_name_is_relative(name)) ambient = name;
        }
        SourceFile const* context = span_valid(d.at) ? span_source(d.at) : nullptr;
        out_lit(d, "import(\"");
        if (ambient.size) {
            out(d, ambient);
        } else if (file && context) {
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

// The object literal member that declared a property, through its instantiations (a homomorphic mapped type's property
// is declared where the property it maps is, a property of one over an intersection where any of its members' is:
// tsgo's mapped symbol has their declarations)
// The class field that declared an inferred property of a class printed as a type literal (its PropDecl.lazy is the
// field's), null for any other: declaration emit prints it from the field's pseudo type (tsgo's
// serializeTypeForDeclaration of the PropertyDeclaration, lit's `labels = [] as unknown as NodeListOf<...>`)
static ClassItem const* declared_field(Ty const* ty, Prop const* prop) {
    while (prop->target) prop = prop->target; // (An instantiation for `this`: the field's own)
    if (!prop->decl->lazy) return nullptr;
    Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    if (declared->kind != TY_DECLARED || (!ty_declared_is_class(declared) &&
        declared->declared.kind != TY_DECLARED_CLASS_STATIC) || !declared->declared.symbol) {
        return nullptr;
    }
    ClassDecl const* cls = symbol_find_node(declared->declared.symbol, SYMBOL_DECL_CLASS);
    if (!cls) return nullptr;
    for_each (i, vec_class_item, &cls->items) {
        if (i->tag == CLASS_ITEM_FIELD && &i->field.lazy_type == prop->decl->lazy) return i;
    }
    return nullptr;
}

static ObjectExprItem const* declared_item(Checker* me, Prop const* prop, u32 depth) {
    for (; depth < 64 && prop; depth++) {
        if (prop->decl->item) return prop->decl->item;
        Ty const* intersection = prop_decl_rare(prop->decl)->mapped_intersection;
        for (u32 i = 0; intersection && i < intersection->composite.types.len; i++) {
            Ty const* part = ty_apparent_type(me, intersection->composite.types.data[i]);
            if (!(part->flags & TYF_OBJECT)) continue;
            Prop const* found = ty_get_prop(me, part, prop->name);
            ObjectExprItem const* item = found ? declared_item(me, found, depth + 1) : nullptr;
            if (item) return item;
        }
        Prop const* mapped = prop_decl_rare(prop->decl)->mapped_from;
        prop = mapped ? mapped : prop->target;
    }
    return nullptr;
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty);

// tsc's getTypeAliasForTypeLiteral: the type alias whose declared type is this type literal (written directly, perhaps
// in parentheses)
// (A mapped type has no node: its declaration is the alias whose type is a mapped type with its template node, the
// alias its uninstantiated root goes by, whatever alias an instantiation does: `Omit<T, K>`'s `Pick<...>` is Pick's)
static Symbol const* type_alias_of_literal(Checker* me, Ty const* ty) {
    TypeNode const* node = ty->anonymous.node;
    if (!node && ty->anonymous.mapped) {
        Ty const* root = ty;
        for (u32 depth = 0; root->anonymous.target && root->anonymous.target->kind == TY_ANONYMOUS && depth < 16; depth++) {
            root = root->anonymous.target;
        }
        Symbol const* alias = root->alias;
        MappedInfo const* mapped = root->anonymous.mapped ? ty_mapped_info_unexpanded(me, root) : nullptr;
        if (!mapped || !mapped->template_node || !alias || !(alias->flags & SYMBOL_TYPE_ALIAS)) return nullptr;
        for (u32 i = 0; i < alias->decl_count; i++) {
            if (alias->decls[i].kind != SYMBOL_DECL_TYPE_ALIAS) continue;
            TypeDecl const* decl = alias->decls[i].node;
            if (!decl || decl->type.tag != TYPE_INTERFACE || vec_interface_member_size(&decl->type.interface.members) != 1) continue;
            InterfaceMember const* member = vec_interface_member_at(&decl->type.interface.members, 0);
            if (member->key.tag == INTERFACE_KEY_MAPPED && &member->type == mapped->template_node) return alias;
        }
        return nullptr;
    }
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
            if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
            out_elided(d); // (A message\'s cyclic structure placeholder is `...`: tsc\'s createElidedInformationPlaceholder)
            return;
        }
    }
    TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
    d.visiting = &visit;
    display_structure(me, d, ty);
}

// The name of a declared type: by its symbol when it has one
// The outer type arguments of a local class or interface (tsc's typeReferenceToTypeNode): `f.` for each enclosing
// declaration whose type parameters aren't passed as themselves. `outer` is innermost first, the lists
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
            out(d, span_sv(outer->owner_class));
            out_lit(d, ".");
        }
        // (Without its type arguments: tsgo's appendReferenceToType keeps only the last reference's, unless
        // UseInstantiationExpressions, which only hover sets)
        out(d, span_sv(outer->owner));
        out_lit(d, ".");
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
    } else if (symbol_is_augmented_export_equals(assigned)) {
        // (The clone's declarations include the augmentation, a module: `typeof import("m")`)
        out_symbol_name(me, d, assigned, SYMBOL_VALUE);
    } else {
        out(d, span_sv(ty_symbol_display_name(assigned)));
    }
    return true;
}

static void out_declared_name_impl(Checker* me, TyDisplay d, Ty const* declared, u32 meaning);
// A name counts its text and one more (tsc's createAccessFromSymbolChain: each part's length and one)
static void out_declared_name(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    if (!d.approx || d.naming) {
        out_declared_name_impl(me, d, declared, meaning);
        return;
    }
    isize_t start = cstr_size(d.target);
    TyDisplay inner = d;
    inner.naming = true;
    out_declared_name_impl(me, inner, declared, meaning);
    approx_add(d, entity_name_length(csview_slice(cstr_sv(d.target), start, cstr_size(d.target))));
}

static void out_declared_name_impl(Checker* me, TyDisplay d, Ty const* declared, u32 meaning) {
    Symbol const* symbol = declared->declared.symbol;
    if (!symbol) symbol = declared->declared.merged_value;
    if (!symbol && declared->declared.kind == TY_DECLARED_NAMESPACE) symbol = declared->declared.namespace_symbol;
    // A class's twin for a cloning namespace import: the clone symbol, reached through that import where it can be,
    // else by its bare name (no parent leads to it: the module's `export =` is the original)
    Symbol const* clone_import = symbol ? ty_clone_class_import(me, declared) : nullptr;
    if (clone_import) {
        SymbolChain chain = d.enclosing ? display_chain_at(d, clone_import, meaning, d.types_baseline || d.tracker) : (SymbolChain){0};
        if (chain.len) out_symbol_name(me, d, clone_import, meaning);
        else out(d, symbol->name);
        return;
    }
    // A module's object, also through `import * as ns` (whose symbol stands for the module): named from here like tsc
    if (d.enclosing && symbol && symbol != symbol_members(symbol) && namespace_import_is_clone(symbol)) {
        // The clone `import * as ns` makes is named by the import where that reaches it, else as the module
        SymbolChain chain = display_chain_at(d, symbol, meaning, d.types_baseline || d.tracker);
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
        if (module_attributes_count(symbol_members(symbol))) out_module_attributes(d, symbol_members(symbol));
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
// A no-substitution template literal node (a cooked text, escaped again for backticks: tsgo's escapeString with a
// backtick quote, which leaves line feeds, and `${` escaped)
static void display_template_text(TyDisplay d, csview sv);
static void display_template_string(TyDisplay d, csview sv) {
    out_lit(d, "`");
    display_template_text(d, sv);
    out_lit(d, "`");
}
static void display_string_literal(TyDisplay d, csview sv) {
    display_quoted_string(d, sv, false, false);
}

static u32 utf8_next(csview sv, isize_t* i);

void ty_display_synthesized_string(TyDisplay d, csview sv) { display_quoted_string(d, sv, false, true); }

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
                } else if (c == 0xED && i + 2 < sv.size && (unsigned char)sv.buf[i + 1] >= 0xA0 &&
                    (unsigned char)sv.buf[i + 1] <= 0xBF
                ) {
                    // A lone surrogate (WTF-8): always escaped, it isn't valid text (tsgo's escapeStringWorker)
                    code = ((u32)(c & 0x0F) << 12) | ((u32)((unsigned char)sv.buf[i + 1] & 0x3F) << 6) |
                        ((unsigned char)sv.buf[i + 2] & 0x3F);
                    i += 2;
                } else if (c == 0xE2 && i + 2 < sv.size && (unsigned char)sv.buf[i + 1] == 0x80 &&
                    ((unsigned char)sv.buf[i + 2] == 0xA8 || (unsigned char)sv.buf[i + 2] == 0xA9)
                ) {
                    code = (unsigned char)sv.buf[i + 2] == 0xA8 ? 0x2028 : 0x2029;
                    i += 2;
                }
                // A byte that isn't UTF-8 (raw in a literal's value: text_decode() keeps them) is escaped as U+FFFD (tsgo's
                // escapeStringWorker: RuneError one byte wide)
                if (!code && c >= 0x80) {
                    int size = utf8_rune_size_n(sv.buf + i, sv.size - i);
                    if (size < 0) {
                        out_lit(d, "\\uFFFD");
                        break;
                    }
                    if (!ascii) {
                        out(d, csview_from_n(&sv.buf[i], size));
                        i += size - 1;
                        break;
                    }
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
            case 0:
                if (i + 1 < sv.size && sv.buf[i + 1] >= '0' && sv.buf[i + 1] <= '9') out_lit(d, "\\x00");
                else out_lit(d, "\\0");
                break;
            default: {
                // (What tsgo's escapeStringWorker escapes whatever the quote: other control characters, U+0085,
                // U+2028/2029, a lone surrogate (WTF-8), and a byte that isn't UTF-8 as U+FFFD)
                u32 code = c < 0x20 ? c : 0;
                isize_t extra = 0;
                unsigned char c1 = i + 1 < sv.size ? (unsigned char)sv.buf[i + 1] : 0;
                unsigned char c2 = i + 2 < sv.size ? (unsigned char)sv.buf[i + 2] : 0;
                if (c == 0xC2 && c1 == 0x85) {
                    code = 0x85, extra = 1;
                } else if (c == 0xED && c1 >= 0xA0 && c1 <= 0xBF && i + 2 < sv.size) {
                    code = ((u32)(c & 0x0F) << 12) | ((u32)(c1 & 0x3F) << 6) | (c2 & 0x3F), extra = 2;
                } else if (c == 0xE2 && c1 == 0x80 && (c2 == 0xA8 || c2 == 0xA9)) {
                    code = c2 == 0xA8 ? 0x2028 : 0x2029, extra = 2;
                } else if (c >= 0x80) {
                    int size = utf8_rune_size_n(sv.buf + i, sv.size - i);
                    if (size < 0) {
                        out_lit(d, "\\uFFFD");
                    } else {
                        out(d, csview_from_n(&sv.buf[i], size));
                        i += size - 1;
                    }
                    break;
                }
                if (code) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04X", code);
                    out(d, csview_from(buf));
                    i += extra;
                } else {
                    out(d, csview_from_n(&sv.buf[i], 1));
                }
                break;
            }
        }
    }
}

// One UTF-8 sequence at `*i` (advanced past it); malformed bytes are themselves
static u32 utf8_next(csview sv, isize_t* i) {
    unsigned char c = (unsigned char)sv.buf[*i];
    u32 len = c < 0x80 ? 1 : (u32)utf8_rune_size(sv.buf + *i);
    if (*i + len > sv.size) len = 1;
    if (len == 1 && c >= 0x80) {
        *i += 1;
        return 0xFFFD; // (Go's RuneError)
    }
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
            if (!(first ? unicode_is_identifier_start(code) : unicode_is_identifier_part(code))) return false;
            continue;
        }
        // (`#` only starts a private name: `#x`, not `#` alone or inside)
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
            (c == '#' && i == 0 && sv.size > 1 && !(sv.buf[1] >= '0' && sv.buf[1] <= '9'));
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
static void out_unique_symbol_name_impl(Checker* me, TyDisplay d, UniqueSymbolDecl const* decl);
static void out_unique_symbol_name(Checker* me, TyDisplay d, UniqueSymbolDecl const* decl) {
    if (!d.approx || d.naming) {
        out_unique_symbol_name_impl(me, d, decl);
        return;
    }
    isize_t start = cstr_size(d.target);
    TyDisplay inner = d;
    inner.naming = true;
    out_unique_symbol_name_impl(me, inner, decl);
    approx_add(d, entity_name_length(csview_slice(cstr_sv(d.target), start, cstr_size(d.target))));
}

static void out_unique_symbol_name_impl(Checker* me, TyDisplay d, UniqueSymbolDecl const* decl) {
    Symbol const* owner = decl->owner;
    if (!span_valid(decl->member)) {
        if (owner) out_symbol_name(me, d, owner, SYMBOL_VALUE);
        return;
    }
    // Printed from nowhere, a member is its bare name (tsc's lookupSymbolChain without an enclosing declaration: the
    // chain is the symbol alone): `typeof VALID` for a static `SecurePassword.VALID`, `typeof toStringTag`
    if (d.typeof_unique_symbols && !d.enclosing && !d.fully_qualified && !d.tracker) {
        out(d, span_sv(decl->member));
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
                out(d, span_sv(decl->member));
                return;
            }
        }
    }
    if (owner) {
        out_symbol_name(me, d, owner, SYMBOL_VALUE);
        out_lit(d, ".");
    }
    out(d, span_sv(decl->member));
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
    // (a mapped type's properties are instantiations of its template, under other names, but declared where the
    // property they map is: tsc's mapped symbol's declarations)
    Span name = prop->mapped ? prop->decl->name_span : root->name == prop->name ? root->decl->name_span : span_null;
    // (tsc's getNameOfSymbolAsWritten: a name that isn't an identifier as written, `"a"`, `2.0`, `["a"]`)
    if (span_valid(name) && name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'' || name.buf[0] == '[')) {
        return cstr_from_sv(span_sv(name));
    }
    if (span_valid(name) && name.size && name.buf[0] >= '0' && name.buf[0] <= '9') return cstr_from_sv(span_sv(name));
    // (A missing name, a JSDoc `@property {function(A): B} f`'s: the binder's '(Missing)' as it is)
    if (span_valid(name) && !name.size) return cstr_from_sv(atom_sv(prop->name));
    if (ty_prop_name_form_of(root) & PROP_NAME_RAW) return cstr_from_sv(atom_sv(prop->name));
    // (A symbol without declarations named by a unique symbol, a non-homomorphic mapped type's over `keyof X`: tsc's
    // getNameOfSymbolFromNameType, the unique symbol's own name as written in brackets, `[iterator]`)
    if (!prop->decl->decl_span_count && !root->decl->decl_span_count) {
        Ty const* unique = ty_unique_symbol_of_name(me, prop->name);
        UniqueSymbolDecl const* decl = unique ? unique->literal.unique : nullptr;
        if (decl && span_valid(decl->member) && decl->member.size) return cstr_d("[", span_sv(decl->member), "]");
        if (decl && decl->owner) return cstr_d("[", span_sv(ty_symbol_display_name(decl->owner)), "]");
    }
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
        form = span_valid(decl->name_span) && decl->name_span.size && decl->name_span.buf[0] != '[' ?
            ty_prop_name_form(span_sv(decl->name_span), nullptr, nullptr, false) : PROP_NAME_KNOWN;
    }
    // (A mapped type's property is named by its key type)
    if (root->name != prop->name) form |= PROP_NAME_TYPED;
    return form;
}

static Scope const* statement_scope_at(Block const* block, Scope const* scope, Span at, u32 depth);

// The innermost statement-level scope around a property's declaration (a mapped type's property is declared where the
// property it comes from is), null if it has none
static Scope const* prop_declaration_scope(Prop const* prop) {
    while (prop->target && !span_valid(prop->decl->name_span) && !prop->decl->decl_span_count) prop = prop->target;
    Span name = prop->decl->name_span;
    SourceFile const* file = span_valid(name) ? span_source(name) : nullptr;
    if (!file || !file->scope) return nullptr;
    return statement_scope_at(&file->ast, file->scope, name, 0);
}

// tsc's getPropertyNameNodeForSymbol: an identifier, a number, a string literal quoted like every declaration wrote it,
// or `[-1]` for a negative number that came from a type
static void display_member_name(Checker* me, TyDisplay d, Prop const* prop) {
    d.naming = true; // (Counted by its symbol's name, see prop_symbol_name_length())
    csview sv = atom_sv(prop->name);
    if (ty_unique_symbol_of_name(me, prop->name)) {
        // (Named from the property's declaration: tsc's addPropertyToElementList makes it the enclosing declaration)
        if (d.enclosing && !d.tracker) {
            Scope const* declared_at = prop_declaration_scope(prop);
            if (declared_at) d.enclosing = declared_at;
        }
        display_prop_name(me, d, prop->name);
        return;
    }
    // A name that is an enum member's value prints as that member, where the enum is accessible as a value (tsgo's
    // getPropertyNameNodeForSymbolFromNameType: `{ [E.A]: x }`, `{ [K in E]: x }`). Not in a message: tsc has no
    // enclosing file there
    if (d.enclosing) {
        Prop const* declared = prop;
        while (declared->target && !declared->decl->decl_span_count && !prop_decl_rare(declared->decl)->name_type) declared = declared->target;
        Ty const* name_type = prop_decl_rare(declared->decl)->name_type;
        if (name_type && (name_type->flags & TYF_ENUM_LITERAL) && name_type->kind == TY_LITERAL) {
            Symbol const* enum_symbol = name_type->literal.enum_decl;
            csview member_name = name_type->literal.enum_member->name;
            if (enum_symbol && symbol_accessible_without_modules_at(d.enclosing, enum_symbol, SYMBOL_VALUE)) {
                if (d.tracker) d.tracker->track_symbol(d.tracker, enum_symbol, SYMBOL_VALUE);
                out_lit(d, "[");
                out_symbol_name(me, d, enum_symbol, SYMBOL_VALUE);
                if (is_identifier_name(member_name)) {
                    out_lit(d, ".");
                    out(d, member_name);
                } else {
                    out_lit(d, "[");
                    display_quoted_string(d, member_name, false, true);
                    out_lit(d, "]");
                }
                out_lit(d, "]");
                return;
            }
        }
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
    // (A string named `#x` isn't a private name)
    if (identifier && sv.buf[0] == '#' && string_named) identifier = false;
    if (identifier) out(d, sv);
    else if (!string_named && is_numeric_name(sv)) out(d, sv);
    else display_quoted_string(d, sv, single, true);
}

// What tsgo's createExpressionFromSymbolChain counts for a computed name it printed as `text`: each `.`-separated part
// one more than its length, an element access part its length and two
static void approx_add_expression_text(TyDisplay d, csview text) {
    if (!d.approx) return;
    u32 part = 0;
    for (isize i = 0; i <= text.size; i++) {
        if (i == text.size || text.buf[i] == '.' || text.buf[i] == '[') {
            if (part) approx_add(d, part + 1);
            part = 0;
            if (i < text.size && text.buf[i] == '[') {
                isize end = i;
                while (end < text.size && text.buf[end] != ']') end++;
                approx_add(d, (u32)(end - i - 1) + 2);
                i = end;
            }
            continue;
        }
        part++;
    }
}

static void display_prop_name(Checker* me, TyDisplay d, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (unique) {
        // (The expression's parts count toward the truncation length, see approx_add_expression_text(): measured on
        // what was written, so not where nothing is)
        isize start = d.target ? cstr_size(d.target) + 1 : 0;
        out_lit(d, "[");
        // A computed name is an expression (tsc's symbolToExpression): a chain that would start at a module isn't
        // yielded, the variable prints by its bare name
        UniqueSymbolDecl const* decl = unique->literal.unique;
        if (d.enclosing && !d.tracker && decl->owner && !span_valid(decl->member)) {
            SymbolChain chain = display_chain_at(d, decl->owner, SYMBOL_VALUE, true);
            if (chain.len && chain.items[0]->is_module) {
                out(d, span_sv(ty_symbol_display_name(decl->owner)));
                approx_add_expression_text(d, span_sv(ty_symbol_display_name(decl->owner)));
                out_lit(d, "]");
                return;
            }
        }
        // (Printed from nowhere, the name is still looked up from the property's declaration, never bare: tsc's
        // addPropertyToElementList makes it the enclosing declaration, `[Symbol.toStringTag]`)
        d.typeof_unique_symbols = false;
        out_unique_symbol_name(me, d, decl);
        if (d.target && d.approx) {
            approx_add_expression_text(d, csview_subview(cstr_sv(d.target), start, cstr_size(d.target) - start));
        }
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
// prints *the node*, as written (`Array<string>`, `typeof a`, `(() => S)[]`, `Omit<O, "p">`). For the types
// baseline (TyDisplay.types_baseline), declaration emit and `reuse_nodes`. A name in the annotation that doesn't mean the same from where the type is printed isn't reused: that part prints its type instead (tsc's
// visitExistingNodeTreeSymbols recovers per node)

static void display_node(Checker* me, TyDisplay d, TypeNode const* node);
static bool is_identifier_name(csview sv);
static void display_string_literal(TyDisplay d, csview sv);
static void display_template_text(TyDisplay d, csview sv);
static void append_binding_pattern(cstr* out, Binding const* binding);
static void append_binding_pattern_mapped(cstr* out, Binding const* binding, SourceFile const* map);

// What a type parameter is called where it's printed, see TyParamNames
static Atom const* numbered_name(Checker* me, csview base, char const* suffix_format, u32 n);

// Whether `function` declares the type parameter `param` and a value parameter called `name`
static bool declares_value_param_named(FunctionImpl const* function, Ty const* param, Atom const* name) {
    if (!param->param.state->item) return false;
    bool declared = false;
    for_each (i, vec_generic_item, &function->generic.items) declared |= i == param->param.state->item;
    if (!declared) return false;
    for_each (i, vec_function_arg, &function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER && csview_equals_sv(span_sv(binding->content_span), atom_sv(name))) return true;
    }
    return false;
}
static Atom const* type_param_name(Checker* me, TyDisplay d, Ty const* param) {
    TyParamNames* names = d.param_names;
    if (!names || names->as_written || param->param.state->is_this) return param->param.name;
    for (u32 i = names->len; i--;) {
        if (names->given[i].param != param) continue;
        if (names->used < i + 1) names->used = i + 1;
        return names->given[i].name;
    }
    Ty const* declared = param; // (A clone made by instantiating a signature has its original's symbol)
    while (declared->param.state->clone_of) declared = declared->param.state->clone_of;
    Atom const* name = param->param.name;
    for (u32 n = 1;; n++) {
        bool taken = false;
        for (u32 i = 0; i < names->len && !taken; i++) taken = names->given[i].name == name;
        for (u32 i = 0; i < names->value_param_count && !taken; i++) taken = names->value_params[i].name == name;
        // (tsc's typeParameterShadowsOtherTypeParameterInScope. A type parameter of the signature being named that
        // has a value parameter of its name is found first, in the signature's fake scope: tsc's binder makes one
        // symbol of both, which is the type parameter's own. Not a copy of the parameter, see value_params)
        bool merged = !taken && names->params_of && declares_value_param_named(names->params_of, declared, name);
        for (u32 i = names->in_scope.len; i-- && !taken && !merged;) {
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
    // (Its name depends on all the names given before it)
    names->used = names->len;
    return name;
}

// A renamed one is a new identifier, which isn't reused, except in a reused node (tsc's trackExistingEntityName gives
// it the range of the node it replaces)
static void out_type_param_name(Checker* me, TyDisplay d, Ty const* param, bool in_node) {
    Atom const* name = type_param_name(me, d, param);
    // (A missing name, `<in in>`'s recovery, is tsc's `__missing`: declarationNameToString's "(Missing)")
    csview text = atom_sv(name).size ? atom_sv(name) : c_sv("(Missing)");
    // (Declaration maps: a name the node builder makes has its declaration's range when that is in the file being
    // written, tsgo's typeParameterToName)
    Symbol const* symbol = param->param.symbol;
    Span declared = !in_node && D_MAP(d) && d.param_declaration && symbol && symbol->decl_count && !param->param.state->is_this ?
        symbol->decls[0].name : span_null;
    if (span_valid(declared) && span_source(declared) != D_MAP(d)) declared = span_null;
    map_span(d, declared, false);
    out(name == param->param.name || in_node ? d : reused_if(d, false), text);
    map_span(d, declared, true);
}

// tsc's enterNewScope() for a signature: names given inside it don't count outside
static u32 param_names_enter(TyDisplay d) {
    return d.param_names ? d.param_names->len : 0;
}
// (What was printed with names given inside the scope can't be reprinted after it: those names may be given to other
// type parameters then, or the same ones named otherwise. tsgo gets there by its types: the type parameters of a scope
// are fresh clones, instantiateMappedType's and instantiateSignature's, so the types that mention them are others)
static void param_names_leave(TyDisplay d, u32 saved) {
    TyParamNames* names = d.param_names;
    if (!names || names->len <= saved) return;
    names->len = saved;
    if (names->used > saved) names->used = saved;
    u32 kept = 0;
    for (u32 i = 0; i < names->printed_len; i++) {
        if (names->printed[i].names_used > saved) continue;
        names->printed[kept++] = names->printed[i];
    }
    names->printed_len = kept;
}

static bool name_means_same(Checker* me, Symbol const* found, Symbol const* symbol);

// Whether any element of the pattern, nested ones too, declares `symbol`
static bool pattern_has(Binding const* pattern, Symbol const* symbol) {
    if (pattern->tag == BINDING_IDENTIFIER) return symbol && pattern->symbol == symbol;
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) if (pattern_has(i, symbol)) return true;
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) if (pattern_has(&i->target, symbol)) return true;
    }
    return false;
}

// (Only the first element of each pattern: tsgo's enterNewScope binds a pattern's elements in a loop that returns after
// the first one, a hole included, so `typeof b` of `({ a, b }: T)` can't be reused where the signature is printed)
static bool pattern_declares(Binding const* pattern, csview name, Symbol const* symbol) {
    if (pattern->tag == BINDING_IDENTIFIER) return symbol && pattern->symbol == symbol && csview_equals_sv(span_sv(pattern->content_span), name);
    if (pattern->tag == BINDING_ARRAY) {
        if (vec_binding_size(&pattern->array)) return pattern_declares(vec_binding_at(&pattern->array, 0), name, symbol);
    } else if (pattern->tag == BINDING_OBJECT) {
        if (vec_object_binding_item_size(&pattern->object)) {
            return pattern_declares(&vec_object_binding_item_at(&pattern->object, 0)->target, name, symbol);
        }
    }
    return false;
}

// tsc's trackExistingEntityName(): the first name of an entity name means the same where the type is printed
static bool name_accessible(Checker* me, TyDisplay d, Symbol const* symbol, csview first, u32 meaning) {
    // Unresolved names are an error type; type parameters and parameters in scope where they're printed (tsc checks
    // that by resolving from the signature's fake scope)
    // (A name the signatures being printed declare as a parameter means that parameter there: a rest tuple's expanded
    // elements are none that can be referenced, tsc's unknownSymbol)
    bool hidden_by_quirk = false;
    if (meaning == SYMBOL_VALUE) {
        for (TyDisplayParams const* i = d.printed_params; i; i = i->outer) {
            for (u32 j = 0; j < i->count; j++) {
                SigParam const* param = &i->params[j];
                // (Where the rest tuple was expanded, the original parameter at that index is the name, if any:
                // tsgo's enterNewScope; `typeof a` of the rest `a` means it)
                if (i->originals && i->originals != i->params) {
                    SigParam const* original = j < i->original_count ? &i->originals[j] : nullptr;
                    if (!original) continue;
                    if (original->decl != param->decl || original->name != param->name) param = original;
                }
                // (A binding pattern's names are in the fake scope too: tsgo's enterNewScope)
                if (param->decl && param->decl->initializer.binding.tag != BINDING_IDENTIFIER) {
                    if (pattern_declares(&param->decl->initializer.binding, first, symbol)) return true;
                    // (One of its other names isn't: it means what it means where the type is printed, as if the
                    // signature weren't)
                    if (pattern_has(&param->decl->initializer.binding, symbol)) hidden_by_quirk = true;
                }
                if (!csview_equals_sv(atom_sv(param->name), first)) continue;
                return symbol && param->decl && param->decl->initializer.binding.tag == BINDING_IDENTIFIER &&
                    param->decl->initializer.binding.symbol == symbol;
            }
        }
    }
    // (A parameter is only in scope inside its function: printed elsewhere it means nothing, tsc's symAtLocation. Where
    // the declaration transformer writes the signature's annotation, it is)
    if (symbol && symbol->is_parameter && d.transformer_node) return true;
    // (Hidden by the quirk: unknown unless printed from inside the function, a declaration's name)
    if (hidden_by_quirk && symbol->decl_count && symbol->decls[0].block) {
        FunctionImpl const* function = symbol->decls[0].block->function;
        if (!function || !span_valid(d.at) || !span_valid(function->full_span) || span_eq(d.at, function->full_span) ||
            span_source(function->full_span) != span_source(d.at) || !span_contains_span(function->full_span, d.at)) {
            return false;
        }
    }
    if (symbol && symbol->is_parameter && span_valid(d.at) && symbol->decl_count && symbol->decls[0].block) {
        FunctionImpl const* function = symbol->decls[0].block->function;
        bool printed = false;
        for (TyDisplayParams const* i = d.printed_params; i && !printed; i = i->outer) printed = i->function == function;
        // (An accessor's own name is in its declaration: its parameters are in scope there, the type of an interface's
        // `set x({ a: b }: T & { o: typeof b })` at `x`)
        // (Printed in another file too: grammy's `HttpAdapter = (req: { on: (...) => typeof req }) => ...` printed for
        // `Parameters<HttpAdapter>` in a declaration elsewhere expands the query, a cyclic structure, TS5088)
        if (!printed && function && span_valid(function->full_span) &&
            (span_source(function->full_span) != span_source(d.at) || !span_contains_span(function->full_span, d.at)) &&
            !(function->accessor && span_eq(function_impl_member_name(function), d.at))
        ) {
            return false;
        }
    }
    // (Unresolved where it's written, a name printed inside the function being printed (the enclosing declaration
    // is the function, say at its name) resolves there to the function's local, which isn't accessible: tsc's
    // symAtLocation, `function f(): typeof b { var b }` is `() => any` at f)
    if (!symbol && meaning == SYMBOL_VALUE && span_valid(d.at)) {
        for (TyDisplayParams const* i = d.printed_params; i; i = i->outer) {
            FunctionImpl const* function = i->function;
            if (!function || !function->has_body || !function_impl_body(function)->scope || !span_valid(function->full_span) ||
                span_source(function->full_span) != span_source(d.at) || !span_contains_span(function->full_span, d.at)
            ) {
                continue;
            }
            Symbol const* local = scope_resolve(function_impl_body(function)->scope, first, meaning);
            if (local && local->decl_count && local->decls[0].block && local->decls[0].block->function == function) return false;
        }
    }
    if (!symbol || (symbol->flags & SYMBOL_TYPE_PARAMETER) || symbol->is_parameter) return true;
    // (An import of nothing is tsc's unknownSymbol: where it's printed, the name must find an alias of nothing too,
    // getSymbolIfSameReference)
    if (d.enclosing && symbol_alias_of_unknown(symbol)) {
        Symbol const* found = scope_resolve(d.enclosing, first, meaning);
        return found && (found == symbol || symbol_alias_of_unknown(found));
    }
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
    // (An import tsc combines is a type of its own: SymbolAlias.combined_type)
    Symbol const* combined = ty_alias_combined_type(me, found);
    if (combined) return combined == symbol;
    // (So is a namespace import an augmentation made a clone of: SymbolRare.export_equals_clone)
    if ((found->flags & SYMBOL_ALIAS) && symbol_rare(found)->export_equals_clone) return symbol_rare(found)->export_equals_clone == symbol;
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
    if (!sig || !sig_target(me, sig)) return ty;
    ty = instantiate_for(me, sig_target(me, sig), ty);
    return sig_mapper(me, sig) ? ty_instantiate(me, ty, sig_mapper(me, sig)) : ty;
}

// The type of an annotation, the way the checker lowered it (and instantiated, see TyDisplay.mapped_by)
// Whether an annotation names no type parameter (nor `this`) as far as a quick look can tell: reprinting it under an
// instantiation's mapper then says what the instantiation is (tsgo's node reuse can't keep a mapped `T`). Anything
// it doesn't look into counts as naming one
static bool node_names_no_type_params(TypeNode const* node, u32 depth) {
    if (depth > 16) return false;
    switch (node->tag) {
        case TYPE_ANY: case TYPE_UNKNOWN: case TYPE_NEVER: case TYPE_VOID: case TYPE_BOOLEAN: case TYPE_NUMBER:
        case TYPE_STRING: case TYPE_SYMBOL: case TYPE_BIGINT: case TYPE_VALUE:
            return true;
        case TYPE_UNION: case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (!node_names_no_type_params(i, depth + 1)) return false;
            }
            return true;
        case TYPE_ARRAY:
            return node_names_no_type_params(node->array.element, depth + 1);
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            if (identifier->keyword || !identifier->target || (identifier->target->flags & SYMBOL_TYPE_PARAMETER)) return false;
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                if (!node_names_no_type_params(&i->type, depth + 1)) return false;
            }
            return true;
        }
        case TYPE_CALLABLE: {
            FunctionImpl const* function = node->callable->function;
            if (vec_generic_item_size(&function->generic.items)) return false;
            for_each (i, vec_function_arg, &function->args) {
                Binding const* binding = &i->initializer.binding;
                if (binding->tag != BINDING_IDENTIFIER || !binding->has_explicit_type) return false;
                if (!node_names_no_type_params(binding_type(binding), depth + 1)) return false;
            }
            return function->ret_explicit && node_names_no_type_params(&function->ret, depth + 1);
        }
        default:
            return false;
    }
}

static Ty const* node_type(Checker* me, TyDisplay d, TypeNode const* node) {
    return instantiate_for(me, d.mapped_by, ty_from_node(me, node, (LowerCtx){ .generic = true }));
}

// A part of an annotation that can't be reused prints its type
static bool needs_parens_in_array(Checker* me, TyDisplay d, Ty const* ty);
static u32 written_prop_count(TyDisplay d, Members const* members);
static bool class_literal_function_like(Checker* me, TyDisplay d, Ty const* ty);
static bool displays_as_intersection(Checker* me, TyDisplay d, Ty const* ty);
static bool anonymous_prints_type_query(TyDisplay d, Ty const* ty);
static bool mapped_as_conditional(Checker* me, TyDisplay d, Ty const* ty);
static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens);
static bool is_whole_enum(Checker* me, Ty const* ty);
static bool conditional_elided(TyDisplay d, Ty const* ty);

static bool needs_parens_as_constituent(Checker* me, TyDisplay d, Ty const* ty) {
    // (What prints: a union or intersection reduced, see ty_display())
    if (!d.no_reduction && (ty->kind == TY_INTERSECTION || ty->kind == TY_UNION) && !shows_alias(d, ty)) ty = ty_reduced(me, ty);
    if (shows_alias(d, ty) || (ty->flags & TYF_BOOLEAN) || is_whole_enum(me, ty)) return false;
    bool conditional = (ty->kind == TY_CONDITIONAL && !display_over(d) && !conditional_elided(d, ty)) || mapped_as_conditional(me, d, ty);
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || conditional || is_function_like(me, d, ty) ||
        class_literal_function_like(me, d, ty);
}

// Declaration maps: the node display_node() is printing printed its type instead (no mappings of its own)
static _Thread_local TypeNode const* node_fell_back;

static void display_node_fallback(Checker* me, TyDisplay d, TypeNode const* node) {
    node_fell_back = node;
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

static void param_comments_begin_at(TyDisplay d, Span span);

static void display_node_args(Checker* me, TyDisplay d, VecSpannedType const* args) {
    if (!vec_spanned_type_size(args)) return;
    // (The arguments aren't constituents or elements of what the reference is one of)
    d.array_element_node = false;
    d.constituent_node = false;
    out_lit(d, "<");
    for_each (i, vec_spanned_type, args) {
        if (i_index) out_lit(d, ", ");
        param_comments_begin_at(d, i->span); // (Declaration emit: the comments around it, emitListItems)
        // (Written here: the argument's node leaves them)
        TyDisplay arg_d = d;
        if (d.comments && span_valid(i->span)) {
            arg_d.comments_container_start = decl_map_type_start(&i->type, d.comments->file);
            arg_d.comments_container_end = decl_map_type_end(&i->type, d.comments->file);
        }
        display_node(me, arg_d, &i->type); // (A generic function type first stays bare, see display_list())
        if (d.comments && span_valid(i->span)) decl_comments_trailing(d.comments, d.target, d.indent, span_end_ptr(i->span));
    }
    out_lit(d, ">");
}

// A JSDoc reference tsc prints as its type instead of reusing it (visitExistingNodeTreeSymbols): a name with a JSDoc
// meaning (`String`, `Object`, `array`...), one that finds nothing, a generic type without enough type arguments, a
// name an `@import` declares (not a visible declaration to tsc)
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
    // A value used as a type (TS2749) is tsgo's errorType, reused as written like an unresolved name
    if (!(identifier->target->flags & (SYMBOL_TYPE | SYMBOL_ALIAS))) return node_type(me, d, node)->flags & TYF_ERROR;
    Symbol const* first = identifier_type_namespace_count(identifier) ?
        (ast_symbols_size(identifier_type_namespace_symbols(identifier)) ? *identifier->rare->namespace_symbols.data : nullptr) : identifier->symbol;
    // (A CommonJS export's alias has no import item)
    if (!identifier_type_namespace_count(identifier) && first && first->flags & SYMBOL_ALIAS && first->decl_count &&
        first->decls[0].kind == SYMBOL_DECL_ALIAS && first->decls[0].node && ((PortItem const*)first->decls[0].node)->jsdoc
    ) {
        return false;
    }
    if (!identifier_type_namespace_count(identifier)) {
        // (`array`, `promise` and `Object` mean something else only without type arguments and noImplicitAny)
        static char const* const intended[] = { "Function", "function", "array", "promise", "Object" };
        bool plain = !compiler_options()->no_implicit_any && !vec_spanned_type_size(&identifier->generic_params);
        for (u32 i = 0; i < c_arraylen(intended); i++) {
            if (csview_equals(span_sv(identifier->name), intended[i])) {
                if (i < 2 || plain) return false;
            }
        }
    }
    Ty const* type = node_type(me, d, node);
    if (type->kind != TY_REFERENCE) return true;
    // (Only a type reference node naming the reference's own target: an import type, or an alias of an
    // instantiation, `@type {import('./m').SVG}`, is reused; tsc's
    // existingTypeNodeIsNotReferenceOrIsReferenceWithCompatibleTypeArgumentCount)
    if (identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier)) return true;
    if (ty_declared_type_of(me, identifier->target) != type->reference.target) return true;
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
// (IDENTIFIER_RENAMED_UNKNOWN: an import of nothing, tsc's unknownSymbol, named by an alias of nothing where it's printed)
typedef enum IdentifierReuse {
    IDENTIFIER_REUSED, IDENTIFIER_RENAMED, IDENTIFIER_RENAMED_UNKNOWN, IDENTIFIER_MAPPED, IDENTIFIER_FAILS
} IdentifierReuse;
static IdentifierReuse identifier_reuse(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    bool qualified = identifier_type_namespace_count(identifier) > 0;
    Symbol const* symbol = identifier->symbol;
    if (qualified) {
        symbol = ast_symbols_size(identifier_type_namespace_symbols(identifier)) ?
            *ast_symbols_at(identifier_type_namespace_symbols(identifier), 0) : nullptr;
    }
    Span first = qualified ? *ast_spans_at(identifier_type_namespaces(identifier), 0) : identifier->name;
    // (A type parameter that the instantiation maps: tsgo's tryVisitTypeReference. A generic reference keeps its name,
    // its type arguments are visited, `State<T>` with T mapped is `State<{ foo: number; }>`)
    bool mapped = d.mapped_by && symbol && (symbol->flags & SYMBOL_TYPE_PARAMETER) && !qualified &&
        node_type(me, d, node) != node_type(me, (TyDisplay){0}, node);
    if (mapped) return IDENTIFIER_MAPPED;
    if (name_accessible(me, d, symbol, span_sv(first), qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE)) return IDENTIFIER_REUSED;
    // (tsc's serializeTypeName resolves the whole name to unknownSymbol, a qualified one too, accessible through an
    // alias of it)
    if (symbol_alias_of_unknown(symbol)) return d.enclosing && scope_unknown_alias(d.enclosing) ? IDENTIFIER_RENAMED_UNKNOWN : IDENTIFIER_FAILS;
    Symbol const* target = identifier->target;
    if (target && !(target->flags & SYMBOL_TYPE_PARAMETER) &&
        symbol_type_accessible_at(d.enclosing, target)
    ) {
        // (Renamed through `import("...")` of a module only a node_modules path reaches, TS2883: tsgo's recovery
        // boundary drops the reuse and prints the type. graphql-inspector's octokit `components["schemas"][...]`)
        if (d.tracker && d.tracker->module_file_unsafe) {
            SymbolChain chain = display_chain_at(d, target, SYMBOL_TYPE, true);
            SourceFile const* file = chain.len && chain.items[0]->is_module && chain.items[0]->module ? chain.items[0]->module->file : nullptr;
            if (file && d.tracker->module_file_unsafe(d.tracker, file)) return IDENTIFIER_FAILS;
        }
        return IDENTIFIER_RENAMED;
    }
    return IDENTIFIER_FAILS;
}

// tsc's tryVisitSimpleTypeNode fails (the object of an indexed access, the operand of `keyof`): the whole indexed
// access or `keyof` prints its type then
static Symbol const* typeof_renamed_symbol(TyDisplay d, TypeNode const* node);
static bool simple_node_fails(Checker* me, TyDisplay d, TypeNode const* node) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            if (identifier->keyword || identifier->jsdoc || identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier)) return false;
            // (A type parameter the instantiation maps fails too: tsgo's tryVisitTypeReference)
            IdentifierReuse reuse = identifier_reuse(me, d, node);
            return reuse == IDENTIFIER_FAILS || reuse == IDENTIFIER_MAPPED;
        }
        case TYPE_TYPEOF: {
            Expr const* leftmost = entity_leftmost(node->typeof_->expr);
            return leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
                !name_accessible(me, d, leftmost->identifier.symbol, span_sv(leftmost->inner_span), SYMBOL_VALUE) &&
                !typeof_renamed_symbol(d, node);
        }
        case TYPE_INDEXED: return simple_node_fails(me, d, &node->indexed->interface);
        case TYPE_KEYOF: return simple_node_fails(me, d, &node->keyof->interface);
        default: return false;
    }
}

// `{ with: { a: "b", ... } }` (or `assert`), each attribute a name or string and a value: anything else is a parse error
// tsgo's parser recovers from with empty attributes
static bool import_type_options_valid(csview options) {
    options = csview_trim_whitespace(options);
    if (options.size < 2 || options.buf[0] != '{' || options.buf[options.size - 1] != '}') return false;
    csview rest = csview_trim_whitespace(csview_slice(options, 1, options.size - 1));
    isize_t keyword = csview_starts_with(rest, "with") ? 4 : csview_starts_with(rest, "assert") ? 6 : 0;
    if (!keyword) return false;
    rest = csview_trim_whitespace(csview_slice(rest, keyword, rest.size));
    if (!rest.size || rest.buf[0] != ':') return false;
    rest = csview_trim_whitespace(csview_slice(rest, 1, rest.size));
    if (rest.size && rest.buf[rest.size - 1] == ',') rest = csview_trim_whitespace(csview_slice(rest, 0, rest.size - 1));
    if (rest.size < 2 || rest.buf[0] != '{' || rest.buf[rest.size - 1] != '}') return false;
    csview entries = csview_trim_whitespace(csview_slice(rest, 1, rest.size - 1));
    // (Each entry `name: value` up to a comma outside of strings)
    while (entries.size) {
        isize_t i = 0;
        char quote = 0;
        bool colon = false;
        for (; i < entries.size; i++) {
            char c = entries.buf[i];
            if (quote) {
                if (c == '\\') i++;
                else if (c == quote) quote = 0;
                continue;
            }
            if (c == '"' || c == '\'') quote = c;
            else if (c == ':') colon = true;
            else if (c == ',') break;
        }
        if (!colon) return false;
        entries = csview_trim_whitespace(csview_slice(entries, i < entries.size ? i + 1 : i, entries.size));
    }
    return true;
}

// Whether tsgo's trackExistingEntityName finds a JSDoc reference's name as a type, tracking (painting) it: not a value
// the JSDoc fallback makes a type of (a JavaScript constructor function, TS2749's), nor a `const { X } = require()`
// of a `module.exports = { X }` property, which tsgo's alias resolves to the property, a value. Its import is elided
// then, which tsgo's own output can't resolve (jsDeclarationsFunctionLikeClasses)
bool ty_jsdoc_name_is_type(Symbol const* found, IdentifierType const* identifier) {
    if (identifier->target && !(identifier->target->flags & (SYMBOL_TYPE | SYMBOL_ALIAS))) return false;
    if (!(found->flags & SYMBOL_ALIAS) || !found->alias || !found->alias->item || !found->alias->item->require_binding) return true;
    Symbol const* target = symbol_resolve_alias((Symbol*)found);
    return !(target && target != found && target->decl_count && target->decls[0].kind == SYMBOL_DECL_EXPORT_EQUALS_PROPERTY);
}

static void display_node_identifier(Checker* me, TyDisplay d, TypeNode const* node) {
    IdentifierType const* identifier = node->identifier;
    if (identifier->keyword) {
        out(d, span_sv(identifier->name));
        return;
    }
    if (identifier->jsdoc && !jsdoc_reference_reusable(me, d, node)) {
        // (A name an `@import` declares isn't accessible to tsc's trackExistingEntityName, so it names what the alias
        // resolves to anew, serializeTypeName, when that is accessible)
        Symbol const* first = identifier_type_namespace_count(identifier) ? nullptr : identifier->symbol;
        Symbol const* target = identifier->target;
        if (first && (first->flags & SYMBOL_ALIAS) && first->decl_count && first->decls[0].kind == SYMBOL_DECL_ALIAS &&
            ((PortItem const*)first->decls[0].node)->jsdoc && target && (target->flags & SYMBOL_TYPE) &&
            !(target->flags & SYMBOL_TYPE_PARAMETER) && d.enclosing && symbol_type_accessible_at(d.enclosing, target)
        ) {
            out_symbol_name(me, reused_if(d, false), target, SYMBOL_TYPE);
            display_node_args(me, d, &identifier->generic_params);
            return;
        }
        display_node_fallback(me, d, node);
        return;
    }
    if (identifier_type_import_ref(identifier) || identifier_type_import_argument(identifier)) {
        if (identifier_type_import_typeof(identifier)) out_lit(d, "typeof ");
        out_lit(d, "import(");
        ModuleRef const* ref = identifier_type_import_ref(identifier);
        // (Reused from another file, the specifier is made anew from here: tsc's rewriteModuleSpecifier)
        SourceFile const* here = d.enclosing && span_valid(d.at) ? span_source(d.at) : nullptr;
        SourceFile const* written = span_valid(identifier->name) ? span_source(identifier->name) : nullptr;
        if (ref && here && written && written != here && ref->module_state == ALIAS_DONE && ref->module &&
            ref->module->is_module && ref->module->module && ref->module->module->file
        ) {
            // (Named after the module the chain of what it refers to starts at, when there is one: a re-exporting
            // package entry rather than the declaring file)
            Symbol const* module = ref->module;
            Symbol const* target = identifier->target;
            if (target && span_valid(identifier->name)) {
                u32 meaning = identifier_type_import_typeof(identifier) ? SYMBOL_VALUE : SYMBOL_TYPE;
                SymbolChain chain = display_chain_at(d, target, meaning, true);
                if (chain.len && chain.items[0]->is_module && chain.items[0]->module && chain.items[0]->module->file) {
                    module = chain.items[0];
                }
            }
            cstr name = program_module_display_name(my program, module, here, SPECIFIER_MODE_NONE, d.usage_in_fake_scope ? nullptr : d.usage_ref);
            // (The same specifier as written: the node itself is reused, its quotes kept: tsgo's rewriteModuleSpecifier)
            csview named = cstr_sv(&name);
            bool single = span_valid(ref->span) && ref->span.size && ref->span.buf[0] == '\'';
            if (single && named.size == cstr_size(&ref->path) + 2 && named.buf[0] == '"' &&
                !memcmp(named.buf + 1, cstr_str(&ref->path), (size_t)cstr_size(&ref->path))
            ) {
                display_quoted_string(d, cstr_sv(&ref->path), true, false);
            } else {
                out(d, named);
            }
            cstr_drop(&name);
        } else if (ref) {
            // (The argument's literal, its quotes kept: tsgo's node copy)
            bool single = span_valid(ref->span) && ref->span.size && ref->span.buf[0] == '\'';
            // (A literal type around the string literal: both mapped)
            map_span(d, ref->span, false);
            map_span(d, ref->span, false);
            display_quoted_string(d, cstr_sv(&ref->path), single, false);
            map_span(d, ref->span, true);
            // Declaration emit: the options as written (`import("m", { with: { ... } })`)
            if (d.multiline && span_valid(ref->span) && span_source(ref->span)) {
                SourceFile const* file = span_source(ref->span);
                char const* end = cstr_str(&file->contents) + cstr_size(&file->contents);
                char const* c = span_end_ptr(ref->span);
                while (c < end && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++;
                if (c < end && *c == ',') {
                    char const* from = c + 1;
                    int depth = 0;
                    for (; c < end && !(depth == 0 && *c == ')'); c++) {
                        if (*c == '{' || *c == '(' || *c == '[') depth++;
                        if (*c == '}' || *c == ')' || *c == ']') depth--;
                    }
                    csview options = csview_trim_whitespace((csview){ from, c - from });
                    if (options.size && options.buf[options.size - 1] == ',') options = csview_trim_whitespace(csview_slice(options, 0, options.size - 1));
                    // (Options that aren't `{ with: ... }` are a parse error: tsgo's parser makes empty attributes)
                    bool attributes = import_type_options_valid(options);
                    if (options.size && !attributes) {
                        out_lit(d, ", { with: {} }");
                    } else if (options.size) {
                        // (The printer's object literals: `{ a: b }`, single line as written)
                        out_lit(d, ", ");
                        for (isize_t k = 0; k < options.size; k++) {
                            char ch = options.buf[k];
                            if (ch == '}' && k && options.buf[k - 1] != ' ' && options.buf[k - 1] != '{') out_lit(d, " ");
                            out(d, (csview){ &options.buf[k], 1 });
                            if (ch == '{' && k + 1 < options.size && options.buf[k + 1] != ' ' && options.buf[k + 1] != '}') out_lit(d, " ");
                        }
                    }
                }
            }
        } else {
            display_node(me, d, identifier_type_import_argument(identifier));
        }
        out_lit(d, ")");
        // (The qualifier: an entity name, each name mapped)
        for_each (i, ast_spans, identifier_type_namespaces(identifier)) {
            out_lit(d, ".");
            out_mapped(d, *i);
        }
        if (span_valid(identifier->name)) {
            out_lit(d, ".");
            out_mapped(d, identifier->name);
        }
        display_node_args(me, d, &identifier->generic_params);
        return;
    }
    bool qualified = identifier_type_namespace_count(identifier) > 0;
    Symbol const* symbol = identifier->symbol;
    IdentifierReuse reuse = identifier_reuse(me, d, node);
    if (reuse != IDENTIFIER_REUSED) {
        // tsc names what it refers to anew (serializeTypeName) and keeps the type arguments, unless no accessible
        // name reaches it (then the reference prints its type)
        Symbol const* target = identifier->target;
        if (reuse == IDENTIFIER_RENAMED) {
            if (d.tracker) d.tracker->track_symbol(d.tracker, target, SYMBOL_TYPE); // (lookupSymbolChain tracks it)
            out_symbol_name(me, reused_if(d, false), target, SYMBOL_TYPE);
            display_node_args(me, d, &identifier->generic_params);
        } else if (reuse == IDENTIFIER_RENAMED_UNKNOWN) {
            Symbol const* chain[2];
            u32 count = scope_unknown_alias_chain(d.enclosing, chain);
            if (d.tracker) d.tracker->track_symbol(d.tracker, chain[0], count == 2 ? SYMBOL_NAMESPACE : SYMBOL_TYPE);
            out(reused_if(d, false), chain[0]->name);
            if (count == 2) {
                out_lit(reused_if(d, false), ".");
                out(reused_if(d, false), chain[1]->name);
            }
            display_node_args(me, d, &identifier->generic_params);
        } else {
            display_node_fallback(me, d, node);
        }
        return;
    }
    // (Declaration emit: what the name refers to here is tracked, painting the import it may be: tsc's
    // trackExistingEntityName)
    Symbol const* first = qualified ? (ast_symbols_size(identifier_type_namespace_symbols(identifier)) ?
        *ast_symbols_at(identifier_type_namespace_symbols(identifier), 0) : nullptr) : symbol;
    if (d.tracker && first && !(first->flags & SYMBOL_TYPE_PARAMETER) && !first->is_parameter && d.enclosing) {
        Symbol const* found = scope_resolve(d.enclosing, span_sv(qualified ? *ast_spans_at(identifier_type_namespaces(identifier), 0) :
            identifier->name), qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE);
        if (found && (qualified || !identifier->jsdoc || ty_jsdoc_name_is_type(found, identifier))) {
            d.tracker->track_symbol(d.tracker, found, qualified ? SYMBOL_NAMESPACE : SYMBOL_TYPE);
        }
    }
    for_each (i, ast_spans, identifier_type_namespaces(identifier)) {
        out_mapped(d, *i);
        out_lit(d, ".");
    }
    if (!qualified && d.param_names && symbol && (symbol->flags & SYMBOL_TYPE_PARAMETER)) {
        map_span(d, identifier->name, false);
        out_type_param_name(me, d, ty_type_param(me, symbol), true);
        map_span(d, identifier->name, true);
    } else {
        out_mapped(d, identifier->name);
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
        // (A qualified name: from its left's start to its right's end)
        map_span(d, expr->inner_span, false);
        display_entity_expr(me, d, &expr->property->expr);
        out_lit(d, ".");
        out_mapped(d, property_name(expr->property));
    } else if (expr->tag == EXPR_GENERIC_PARAMS) {
        display_entity_expr(me, d, &expr->generic_params->expr);
        display_node_args(me, d, &expr->generic_params->params);
    } else {
        out_mapped(d, expr->inner_span);
    }
}

// Whether an object literal initializes a visible variable of its file (exported, or a script's): what tsc's
// getContainersOfSymbol names its members through
static bool object_literal_variable_visible(Expr const* literal) {
    SourceFile const* file = span_valid(literal->inner_span) ? span_source(literal->inner_span) : nullptr;
    if (!file || !file->scope) return false;
    SymbolTable const* tables[2] = { scope_locals(file->scope), file->scope->container ? file->scope->container->exports : nullptr };
    for (u32 t = 0; t < 2; t++) {
        for (u32 i = 0; tables[t] && i < tables[t]->count; i++) {
            Symbol const* symbol = tables[t]->entries[i].symbol;
            if (!(symbol->flags & SYMBOL_VARIABLE) || !symbol->decl_count || symbol->decls[0].kind != SYMBOL_DECL_VARIABLE) continue;
            Binding const* binding = symbol->decls[0].node;
            if (!binding || binding->parent || binding->tag != BINDING_IDENTIFIER) continue;
            Expr const* value = ((InitializedBinding const*)binding)->value;
            if (value != literal) continue;
            return symbol->exported || !file->is_module;
        }
    }
    return false;
}

// What tsc's serializeTypeName names a query whose name isn't accessible as written by (the name resolved, aliases
// too), null where it fails: a module is accessible through an import type wherever printed (isAnySymbolAccessible's
// hasNonGlobalAugmentationExternalModuleSymbol), `typeof import("m")` (babel-parser's `keyof typeof charCodes` of a
// namespace import printed in another file)
static Symbol const* entity_expr_symbol(Expr const* expr) {
    Symbol const* symbol = nullptr;
    if (expr->tag == EXPR_IDENTIFIER) {
        if (expr->identifier.keyword) return nullptr;
        symbol = expr->identifier.symbol;
    } else if (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) {
        Symbol const* left = entity_expr_symbol(&expr->property->expr);
        if (!left || (!(left->flags & SYMBOL_NAMESPACE) && !left->is_module)) return nullptr;
        symbol = symbol_get_export(left, span_sv(property_name(expr->property)));
    } else {
        return nullptr;
    }
    if (symbol && symbol->flags & SYMBOL_ALIAS) symbol = symbol_resolve_alias((Symbol*)symbol);
    // (`import * as ns`'s own namespace symbol is the module in tsc)
    if (symbol && symbol_rare(symbol)->gather_module && symbol_rare(symbol)->gather_module->is_module) symbol = symbol_rare(symbol)->gather_module;
    return symbol;
}

static Symbol const* typeof_renamed_symbol(TyDisplay d, TypeNode const* node) {
    if (!node->typeof_->expr || !d.enclosing) return nullptr;
    Symbol const* symbol = entity_expr_symbol(node->typeof_->expr);
    if (!symbol || symbol->is_local || symbol->is_parameter || symbol->is_placeholder) return nullptr;
    if (symbol->is_module && !symbol->is_augmentation && !symbol->is_global_this) return symbol;
    if (!symbol->parent || !symbol_value_accessible_at(d.enclosing, symbol)) return nullptr;
    return symbol;
}

static void display_node_typeof(Checker* me, TyDisplay d, TypeNode const* node) {
    Expr const* leftmost = entity_leftmost(node->typeof_->expr);
    // (In JavaScript a name through `exports` or `module.exports` is never reused: tsc's trackExistingEntityName)
    if (leftmost->tag == EXPR_IDENTIFIER && span_source(leftmost->inner_span) && span_source(leftmost->inner_span)->is_js) {
        csview entity = span_sv(expr_outer_span(node->typeof_->expr));
        bool exports = csview_equals(span_sv(leftmost->inner_span), "exports") || (csview_starts_with(entity, "module.exports") &&
            (entity.size == 14 || entity.buf[14] == '.'));
        if (exports) {
            display_node_fallback(me, d, node);
            return;
        }
    }
    // A leftmost `this` stands for its container's symbol (tsc's trackExistingEntityName: getThisContainer, its
    // accessibility from the `this` itself). An object literal's method or accessor is accessible only through a visible
    // variable the literal initializes (getContainersOfSymbol's variable of an object literal: an exported one, or a
    // script's), a function expression never: else the query prints its type (that object literal's, reaching itself:
    // TS5088 in declaration emit; headlessui's `disposables()` returns a local `let api = { ... }`)
    if (leftmost->tag == EXPR_IDENTIFIER && leftmost->identifier.keyword == KEYWORD_IDENTIFIER_THIS &&
        leftmost->identifier.this_kind == THIS_ANY && d.enclosing
    ) {
        FunctionImpl const* container = identifier_this_function(&leftmost->identifier);
        if (container && container->is_expression && !function_impl_is_arrow(container)) {
            Expr const* literal = function_impl_rare(container)->this_literal;
            bool visible = literal && !function_impl_rare(container)->this_literal_parenthesized &&
                object_literal_variable_visible(literal);
            if (!visible) {
                display_node_fallback(me, d, node);
                return;
            }
        }
    }
    if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        !name_accessible(me, d, leftmost->identifier.symbol, span_sv(leftmost->inner_span), SYMBOL_VALUE)
    ) {
        // tsc's tryVisitTypeQuery: a name that means something else here is named anew where its symbol is accessible
        // (serializeTypeName), in a node with the query's range: `typeof import("a").x` reuses `typeof import(` and
        // `).`, `typeof a.x` (through a namespace import) only `typeof `. The whole name resolved, see typeof_renamed_symbol()
        Symbol const* symbol = typeof_renamed_symbol(d, node);
        if (symbol) {
            out_lit(d, "typeof ");
            out_symbol_name(me, reused_if(d, false), symbol, SYMBOL_VALUE);
            return;
        }
        // (An import of an unresolved module: serializeTypeName resolves the name to unknownSymbol, which the file
        // names by its first import that resolves to nothing too, painting it, as identifier_reuse() says of a type
        // reference: better-auth's `typeof BASE_ERROR_CODES` printed `typeof BetterAuthClientOptions` elsewhere)
        Symbol const* unknown = leftmost == node->typeof_->expr ? leftmost->identifier.symbol : nullptr;
        Symbol const* chain[2];
        u32 count = unknown && symbol_alias_of_unknown(unknown) && d.enclosing ? scope_unknown_alias_chain(d.enclosing, chain) : 0;
        if (count) {
            if (d.tracker) d.tracker->track_symbol(d.tracker, chain[0], count == 2 ? SYMBOL_NAMESPACE : SYMBOL_VALUE);
            out_lit(d, "typeof ");
            out(reused_if(d, false), chain[0]->name);
            if (count == 2) {
                out_lit(reused_if(d, false), ".");
                out(reused_if(d, false), chain[1]->name);
            }
            return;
        }
        display_node_fallback(me, d, node);
        return;
    }
    // (Declaration emit: tracked, see display_node_identifier())
    Symbol const* symbol = leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword ? leftmost->identifier.symbol : nullptr;
    if (d.tracker && symbol && !symbol->is_parameter && d.enclosing) {
        Symbol const* found = scope_resolve(d.enclosing, span_sv(leftmost->inner_span), SYMBOL_VALUE);
        if (found) d.tracker->track_symbol(d.tracker, found, SYMBOL_VALUE);
    }
    out_lit(d, "typeof ");
    display_entity_expr(me, d, node->typeof_->expr);
}

static void param_comments_begin_at(TyDisplay d, Span span);

static void display_node_type_params(Checker* me, TyDisplay d, Generic const* generic) {
    if (generic_empty(generic)) return;
    out_lit(d, "<");
    for_each (i, vec_generic_item, &generic->items) {
        if (i_index) out_lit(d, ", ");
        if (!span_valid(i->jsdoc_tag)) param_comments_begin_at(d, span_valid(i->full_span) ? i->full_span : i->name);
        TyDisplay param_d = d;
        // (A JSDoc `@template {C} T`'s parameter is mapped at its name, its constraint where it's written, and ends where
        // its name does: tsgo's reparsed node)
        bool jsdoc = span_valid(i->jsdoc_tag);
        // (A constraint or default ending the parameter leaves its trailing comments to it, written below)
        if (d.comments && span_valid(i->full_span)) {
            param_d.comments_container_end = decl_comments_end(d.comments, span_end_ptr(i->full_span), nullptr);
        }
        if (!jsdoc) map_span(param_d, i->full_span, false);
        // (Its modifiers are token nodes, in the order written)
        char const* word = D_MAP(param_d) && span_valid(i->full_span) ? i->full_span.buf : nullptr;
        char const* text_end = word ? cstr_str(&D_MAP(param_d)->contents) + cstr_size(&D_MAP(param_d)->contents) : nullptr;
        for (u32 k = 0; k < 3; k++) {
            bool on = k == 0 ? i->is_const : k == 1 ? i->is_in : i->is_out;
            if (!on) continue;
            csview keyword = k == 0 ? c_sv("const") : k == 1 ? c_sv("in") : c_sv("out");
            char const* at = nullptr;
            // (Among the words before the name)
            for (char const* c = word; c && c < i->name.buf && c < text_end; c = decl_map_skip_trivia(c, text_end)) {
                char const* e = c;
                while (e < text_end && (isalnum((unsigned char)*e) || *e == '_' || *e == '$')) e++;
                if (e == c) break;
                if (e - c == keyword.size && !memcmp(c, keyword.buf, (size_t)keyword.size)) at = c;
                c = e;
            }
            out_token_mapped(param_d, at, keyword);
            out_lit(d, " ");
        }
        // (A renamed one, `T_1`, keeps the name's range)
        map_span(param_d, i->name, false);
        if (d.param_names && i->symbol_ref) out_type_param_name(me, d, ty_type_param(me, i->symbol_ref), true);
        else out(d, span_sv(i->name));
        map_span(param_d, i->name, true);
        if (i->extends_explicit) {
            out_lit(d, " extends ");
            display_node(me, param_d, &i->extends);
        }
        if (i->fallback_explicit) {
            out_lit(d, " = ");
            display_node(me, param_d, i->fallback);
        } else if (i->jsdoc_default_missing && d.multiline) {
            out_lit(d, " = ");
        }
        if (jsdoc && D_MAP(param_d) && i->fallback_explicit) {
            // (`[T=D]`: past its `]`)
            char const* close = token_after(param_d, decl_map_type_end(i->fallback, D_MAP(param_d)), ']');
            if (close) map_at(param_d, close + 1);
            else map_span(param_d, i->name, true);
        } else if (jsdoc) {
            map_span(param_d, i->name, true);
        }
        else if (D_MAP(param_d) && span_valid(i->full_span)) map_at(param_d, decl_map_back_trivia(span_end_ptr(i->full_span), i->full_span.buf));
        if (d.comments && !span_valid(i->jsdoc_tag) && span_valid(i->full_span)) decl_comments_trailing(d.comments, d.target, d.indent, span_end_ptr(i->full_span));
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

// Declaration emit: the type a binding pattern without an annotation implies, its elements `any` (tsc's
// getTypeFromBindingPattern as the node builder prints it): `{ a: any; }`, `[any, any]`
static void display_binding_pattern_type(TyDisplay d, Binding const* pattern) {
    if (pattern->tag == BINDING_ARRAY) {
        out_lit(d, "[");
        for_each (i, vec_binding, &pattern->array) {
            if (i_index) out_lit(d, ", ");
            if (i->rest) out_lit(d, "...");
            if (i->tag == BINDING_ARRAY || i->tag == BINDING_OBJECT) display_binding_pattern_type(d, i);
            else out(d, i->rest ? c_sv("any[]") : c_sv("any"));
        }
        out_lit(d, "]");
        return;
    }
    if (pattern->tag != BINDING_OBJECT) {
        out_lit(d, "any");
        return;
    }
    u32 count = 0;
    for_each (i, vec_object_binding_item, &pattern->object) count += !i->target.rest;
    if (!count) {
        out_lit(d, "{}");
        return;
    }
    TyDisplay members = brace_open(d);
    for_each (i, vec_object_binding_item, &pattern->object) {
        if (i->target.rest) continue;
        member_begin(members);
        csview name = span_sv(i->source_span);
        if (i->computed_key) name = span_sv(expr_outer_span(i->computed_key));
        {
            if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'') && is_identifier_name(csview_slice(name, 1, name.size - 1))) {
                name = csview_slice(name, 1, name.size - 1);
            }
        }
        out(members, name);
        out_lit(members, ": ");
        if (i->target.tag == BINDING_ARRAY || i->target.tag == BINDING_OBJECT) display_binding_pattern_type(members, &i->target);
        else out_lit(members, "any");
        member_end(members);
    }
    brace_close(d);
}

// Declaration emit: the comments around a reused parameter (tsc's emitListItems, decl_comments.h)
static void param_comments_begin_at(TyDisplay d, Span span) {
    if (!d.comments || !span_valid(span)) return;
    decl_comments_intervening(d.comments, d.target, d.indent, span.buf);
    decl_comments_leading(d.comments, d.target, d.indent, span.buf);
    if (decl_map_last_char(cstr_sv(d.target)) == '\n') out_indent(d);
}

static void param_comments_begin(TyDisplay d, FunctionArg const* arg) { param_comments_begin_at(d, arg->span); }

static void param_comments_end(TyDisplay d, FunctionArg const* arg) {
    if (d.comments && span_valid(arg->span)) decl_comments_trailing(d.comments, d.target, d.indent, span_end_ptr(arg->span));
}

static void display_node_signature_inner(Checker* me, TyDisplay d, FunctionImpl const* function, char const* separator) {
    if (function->is_abstract_constructor) {
        // (A modifier: a token node)
        out_token_mapped(d, D_MAP(d) && span_valid(function->full_span) ? function->full_span.buf : nullptr, c_sv("abstract"));
        out_lit(d, " ");
    }
    if (function->is_constructor || function->is_abstract_constructor) out_lit(d, "new ");
    // (Only its return type is in an outer `extends` clause's context)
    bool in_extends = d.in_extends;
    d.in_extends = false;
    display_node_type_params(me, d, &function->generic);
    out_lit(d, "(");
    for_each (i, vec_function_arg, &function->args) {
        if (i_index) out_lit(d, ", ");
        if (!function->jsdoc_type) param_comments_begin(d, i);
        Binding const* binding = &i->initializer.binding;
        TyDisplay arg_d = d;
        if (function->jsdoc_type && !function->jsdoc_callback) arg_d.no_map = true;
        // (A JSDoc type's parameter first on a continuation line starts at its `*` unless it starts with its name:
        // tsgo maps skipTrivia(pos), `...rest`)
        char const* start = arg_start(i);
        if (start && D_MAP(arg_d) && !word_start_char(*start)) start = decl_map_jsdoc_line_start(D_MAP(arg_d), start);
        map_at(arg_d, start);
        if (i->rest) {
            Span rest = function_arg_rest_span(i);
            // (Its token from the line's `*` too, like the parameter)
            char const* dots = span_valid(rest) ? span_end_ptr(rest) - 3 : nullptr;
            map_at(arg_d, dots && D_MAP(arg_d) ? decl_map_jsdoc_line_start(D_MAP(arg_d), dots) : dots);
            out_lit(d, "...");
            if (dots && D_MAP(arg_d)) map_at(arg_d, dots + 3);
        }
        if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
            cstr pattern = cstr_init();
            if (d.transformer_node && d.multiline && d.comments) {
                ty_append_binding_pattern_commented(&pattern, binding, D_MAP(arg_d), d.comments, d.indent);
            } else {
                append_binding_pattern_mapped(&pattern, binding, D_MAP(arg_d));
            }
            out(d, cstr_sv(&pattern));
            cstr_drop(&pattern);
        } else if (function->jsdoc_type && !ty_is_identifier_name(span_sv(binding->content_span))) {
            // (`@param {T} a-b` of a `@callback`/`@overload`: tsgo's reparser names the parameter `a_b`, each character
            // that can't be in an identifier made `_`)
            csview name = span_sv(binding->content_span);
            cstr valid = cstr_from_sv(name);
            char* c = cstr_data(&valid);
            for (isize k = 0; k < name.size; k++) {
                unsigned char ch = (unsigned char)c[k];
                bool part = ch >= 0x80 || ch == '_' || ch == '$' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (k && ch >= '0' && ch <= '9');
                if (!part) c[k] = '_';
            }
            out(arg_d, cstr_sv(&valid));
            cstr_drop(&valid);
        } else {
            out_mapped(arg_d, binding->content_span);
        }
        if (i->explicit_optional && !span_valid(function_arg_question_span(i)) && i->jsdoc_question) {
            // (A JSDoc parameter's question token is its tag)
            map_at(arg_d, i->jsdoc_question->buf);
            out_lit(d, "?");
            map_at(arg_d, span_end_ptr(*i->jsdoc_question));
        } else if (i->explicit_optional) {
            Span question = function_arg_question_span(i);
            out_token_mapped(arg_d, span_valid(question) ? question.buf : nullptr, c_sv("?"));
        }
        // (tsc reprints a JSDoc function type's parameters as new declarations around the written names and types)
        out(function->jsdoc_type ? reused_if(d, false) : d, c_sv(": "));
        if (binding->has_explicit_type) {
            // (The parameter ends with it: param_comments_end() writes its trailing comments)
            TyDisplay type_d = d;
            if (d.comments && d.multiline) type_d.comments_container_end = decl_map_type_end(binding_type(binding), d.comments->file);
            display_node(me, type_d, binding_type(binding));
        } else if (d.multiline && binding->tag != BINDING_IDENTIFIER) display_binding_pattern_type(d, binding);
        else out(reused_if(d, false), i->rest && d.multiline ? c_sv("any[]") : c_sv("any")); // tsc gives it a synthesized `any` (`any[]` a rest one, declaration emit)
        // (A `@callback`'s parameter is its tag, which ends exactly at the next tag's `@`)
        if (function->jsdoc_callback && span_valid(i->span)) map_at(arg_d, span_end_ptr(i->span));
        else map_span(arg_d, i->span, true);
        if (!function->jsdoc_type) param_comments_end(d, i);
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TypePredicateNode const* predicate = function->predicate;
    if (predicate) {
        map_span(d, predicate->span, false);
        if (predicate->asserts) {
            out_token_mapped(d, D_MAP(d) ? predicate->span.buf : nullptr, c_sv("asserts"));
            out_lit(d, " ");
        }
        out_mapped(d, predicate->name);
        if (predicate->has_type) {
            out_lit(d, " is ");
            display_node(me, d, &predicate->type);
        }
        map_span(d, predicate->span, true);
    } else if (function->ret_explicit) {
        // (`() => (infer U extends V)` there: the printer's emitReturnType)
        TypeNode const* ret = &function->ret;
        bool parens = in_extends && !ret->parenthesized && ret->tag == TYPE_INFERRED && ret->inferred->extends.parsed;
        if (parens) out(reused_if(d, false), c_sv("("));
        d.in_extends = in_extends && !parens;
        display_node(me, d, ret);
        if (parens) out(reused_if(d, false), c_sv(")"));
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
        d.tracker->track_computed_name(d.tracker, csview_slice(span_sv(name), 0, name.size), span_source(name),
            leftmost->identifier.symbol);
    }
    if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword &&
        !name_accessible(me, d, leftmost->identifier.symbol, span_sv(leftmost->inner_span), SYMBOL_VALUE)
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
            // (A negative one is a new literal in the computed name node, whose brackets keep its range: tsc's
            // updateComputedPropertyName; a positive one replaces the node, range and all)
            bool negative = key->literal.value.number < 0;
            if (negative) out_lit(d, "[");
            cstr_append_value_js(d.target, &key->literal.value);
            if (negative) out_lit(d, "]");
            return;
        }
    }
    // (Declaration maps: from the `[` to the `]`, an entity name's parts)
    Span key = expr_outer_span(expr);
    char const* open = nullptr;
    if (D_MAP(d) && span_valid(key)) {
        char const* text = cstr_str(&D_MAP(d)->contents);
        char const* c = key.buf;
        while (c > text && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
        if (c > text && c[-1] == '[') open = c - 1;
    }
    map_at(d, open);
    out_lit(d, "[");
    if (D_MAP(d) && (expr->tag == EXPR_IDENTIFIER || (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT))) {
        display_entity_expr(me, d, expr);
    } else {
        out_mapped(d, key);
    }
    out_lit(d, "]");
    if (open) map_at(d, token_after(d, span_end_ptr(key), ']') ? token_after(d, span_end_ptr(key), ']') + 1 : nullptr);
}

// A computed name that isn't a literal or unique symbol isn't a member (tsc's shouldRemoveDeclaration), except an
// unresolved entity name (`[Symbol.match]` without the lib that has it: the types baseline allows unresolved names,
// a message's typeToString doesn't)
static bool member_printed(Checker* me, TyDisplay d, InterfaceMember const* member) {
    if (member->key.tag != INTERFACE_KEY_EXPR || !member->key.expr) return true;
    Ty const* key = ty_expr_type(me, member->key.expr);
    // (One the declaration transformer visits: only a late bound name's, tsgo's visitDeclarationSubtree elides the rest)
    if (d.transformer_node && d.multiline && !compiler_options()->isolated_declarations) {
        Expr const* expr = member->key.expr;
        bool literal = expr->tag == EXPR_LITERAL || (expr->tag == EXPR_UNARY_OP && expr->unary_op->expr.tag == EXPR_LITERAL);
        return literal || (key->kind == TY_LITERAL && expr_is_entity_name(expr));
    }
    return key->kind == TY_LITERAL ||
        ((d.types_baseline || d.tracker) && (key->flags & TYF_ANY) && expr_is_entity_name(member->key.expr));
}

static void param_comments_begin_at(TyDisplay d, Span span);

// A `@property` tag's description, the way tsgo's parseTagComments collects it (the lines after the first lose their
// `*` and their indentation up to the description's margin), right trimmed: empty if none. `tag` is the tag's node
// span (from its `@`), `name` its name
static cstr jsdoc_tag_description(Span tag, Span name) {
    cstr ret = cstr_init();
    if (!span_valid(tag) || !span_valid(name)) return ret;
    char const* end = span_end_ptr(tag);
    // (The `@`'s column: only blanks and a `*` (or the comment's `/**`) are before it on its line)
    char const* l = tag.buf;
    while (l[-1] == ' ' || l[-1] == '\t' || l[-1] == '*' || l[-1] == '/') l--;
    int tag_indent = (int)(tag.buf - l);
    char const* c = span_end_ptr(name);
    // (Past a bracketed name's `]`)
    char const* before = name.buf;
    while (before[-1] == ' ' || before[-1] == '\t') before--;
    bool bracketed = before[-1] == '[';
    if (bracketed) {
        // (A default may have brackets of its own, `[a=[]]`: tsgo parses it as an expression)
        int depth = 0;
        while (c < end && (*c != ']' || depth) && *c != '\n') {
            if (*c == '[') depth++;
            else if (*c == ']') depth--;
            c++;
        }
        if (c < end && *c == ']') c++;
    }
    int margin;
    char const* text = c;
    while (text < end && (*text == ' ' || *text == '\t')) text++;
    int indent;
    if (text < end && *text != '\n' && *text != '\r') {
        // (On the tag's line: the description's column is the margin)
        margin = tag_indent + (int)(text - tag.buf);
        indent = margin;
        c = text;
    } else {
        // (On a later line: the leading blanks, `*` and blanks of the last line before it, past the tag's column, start it)
        char const* p = text;
        char const* last_line = nullptr;
        while (p < end) {
            if (*p == '\r' || *p == '\n') {
                if (*p == '\r' && p + 1 < end && p[1] == '\n') p++;
                p++;
                last_line = p;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p < end && *p == '*' && !(p + 1 < end && p[1] == '/')) p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                continue;
            }
            break;
        }
        if (!last_line || p >= end || *p == '@') return ret;
        if (bracketed) {
            // (The regular scanner read the `]` and skipped the line break: no line break seen, so no indentation
            // kept, the margin is the text's offset from the tag)
            margin = tag_indent + (int)(p - tag.buf);
            indent = margin;
            c = p;
            goto lines;
        }
        margin = tag_indent;
        int indent_text = (int)(p - last_line);
        if (margin < indent_text) cstr_append_n(&ret, last_line + margin, indent_text - margin);
        indent = indent_text;
        c = p;
    }
    // The first line's text, then each line's past its margin
lines:
    for (;;) {
        char const* eol = c;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;
        cstr_append_n(&ret, c, eol - c);
        if (eol >= end) break;
        c = eol;
        if (*c == '\r' && c + 1 < end && c[1] == '\n') c++;
        c++;
        cstr_append(&ret, "\n");
        indent = 0;
        char const* ws = c;
        while (c < end && (*c == ' ' || *c == '\t')) c++;
        if (c < end && *c == '*' && !(c + 1 < end && c[1] == '/')) {
            int n = (int)(c - ws);
            if (n > margin) cstr_append_n(&ret, ws + margin, n - margin);
            indent = n + 1;
            c++;
            ws = c;
            while (c < end && (*c == ' ' || *c == '\t')) c++;
        }
        // (A tag on its own line ends it: the next one's, a nested `@property`)
        if (c < end && *c == '@') break;
        int n = (int)(c - ws);
        if (indent + n > margin) {
            int skip = margin - indent < 0 ? 0 : margin - indent;
            cstr_append_n(&ret, ws + skip, n - skip);
        }
        indent += n;
        (void)indent;
    }
    // (Leading line breaks and trailing whitespace dropped: removeLeadingNewlines, GetTextOfJSDocComment's trim)
    isize_t start = 0;
    while (start < cstr_size(&ret) && (cstr_str(&ret)[start] == '\n')) start++;
    isize_t size = cstr_size(&ret);
    while (size > start && (cstr_str(&ret)[size - 1] == ' ' || cstr_str(&ret)[size - 1] == '\t' ||
        cstr_str(&ret)[size - 1] == '\n' || cstr_str(&ret)[size - 1] == '\r')) {
        size--;
    }
    cstr out = cstr_init();
    cstr_append_n(&out, cstr_str(&ret) + start, size - start);
    cstr_drop(&ret);
    return out;
}

// tsgo's preservePartialJsDoc: `/**`, ` * ` before each of the description's lines, ` */`, each line indented
static void out_jsdoc_description(TyDisplay d, InterfaceMember const* member) {
    cstr text = jsdoc_tag_description(member->declaration, member->key.span);
    if (cstr_size(&text)) {
        out_indent(d);
        out_lit(d, "/**\n");
        csview rest = cstr_sv(&text);
        for (;;) {
            isize_t at = csview_find_sv(rest, c_sv("\n"));
            csview line = at == c_NPOS ? rest : csview_slice(rest, 0, at);
            out_indent(d);
            // (An empty line is ` *`: the writer leaves no trailing space)
            if (line.size) {
                out_lit(d, " * ");
                out(d, line);
            } else {
                out_lit(d, " *");
            }
            out_lit(d, "\n");
            if (at == c_NPOS) break;
            rest = csview_slice(rest, at + 1, rest.size);
        }
        out_indent(d);
        out_lit(d, " */\n");
    }
    cstr_drop(&text);
}


// `types_multiline`: how the member types print (a reused mapped type is single line itself, not the types in it)
static void display_node_interface(Checker* me, TyDisplay d, Interface const* interface, bool types_multiline) {
    bool described = d.jsdoc_described == interface && d.multiline;
    d.jsdoc_described = nullptr;
    bool empty = true;
    for_each (i, vec_interface_member, &interface->members) empty &= !member_printed(me, d, i);
    if (empty) {
        out_lit(d, "{}");
        return;
    }
    TyDisplay outer = d;
    d = brace_open(d);
    for_each (i, vec_interface_member, &interface->members) {
        if (!member_printed(me, d, i)) continue;
        // (Declaration emit: a type ending the member, which has no `;`, leaves its trailing comments to it)
        if (d.comments && d.multiline && span_valid(i->declaration)) {
            d.comments_container_start = i->declaration.buf;
            d.comments_container_end = decl_comments_end(d.comments, span_end_ptr(i->declaration), ";,");
        }
        // Declaration emit: stripInternal leaves out a member with `@internal` in a leading comment
        if (d.comments && compiler_options()->strip_internal && span_valid(i->declaration) &&
            decl_comments_leading_contains(d.comments, i->declaration.buf, "@internal")
        ) {
            continue;
        }
        // Declaration emit: accessors as written (the transformer visits the declarations)
        InterfaceAccessor const* accessor = i->accessor;
        if (d.multiline && i->accessor_kind && (i->key.tag == INTERFACE_KEY_NAME || i->key.tag == INTERFACE_KEY_EXPR)) {
            bool getter = i->accessor_kind == 1;
            member_begin_node(d, i->declaration);
            map_span(d, i->declaration, false);
            out(d, getter ? c_sv("get ") : c_sv("set "));
            if (i->key.tag == INTERFACE_KEY_EXPR) display_node_computed_name(me, d, i->key.expr);
            else out_mapped(d, i->key.span);
            if (getter) {
                out_lit(d, "(): ");
            } else {
                out_lit(d, "(");
                // (The parameter and its name, mapped)
                Span param = accessor && span_valid(accessor->setter_param) ? accessor->setter_param :
                    accessor && span_valid(accessor->setter_pattern) ? accessor->setter_pattern : span_null;
                if (span_valid(param)) out_mapped(d, param);
                else out_lit(d, "value");
                out_lit(d, ": ");
            }
            if (i->type.parsed) display_node(me, d, &i->type);
            else out(reused_if(d, false), c_sv("any"));
            if (!getter) out_lit(d, ")");
            member_end_node_mapped(d, i->declaration, ";,", true);
            continue;
        }
        // (A mapped type isn't a member list: no comments, tsc's emitMappedType)
        Span comment_span = i->key.tag == INTERFACE_KEY_MAPPED ? span_null : i->declaration;
        if (described && i->key.tag == INTERFACE_KEY_NAME) out_jsdoc_description(d, i);
        member_begin_node(d, comment_span);
        if (D_MAP(d) && span_valid(comment_span) && !(i->key.tag == INTERFACE_KEY_NAME && i->key.span.buf == comment_span.buf &&
            word_start_char(*comment_span.buf) && !(i->readonly && csview_starts_with(span_sv(comment_span), "readonly")))) {
            // (A JSDoc type literal's member first on its line starts at the line's `*` unless it starts with its name:
            // tsgo maps a property or method signature from an identifier name, others, `'a-b'?: T` too, from skipTrivia(pos))
            map_at(d, decl_map_jsdoc_line_start(D_MAP(d), comment_span.buf));
        } else {
            map_span(d, comment_span, false);
        }
        switch (i->key.tag) {
            case INTERFACE_KEY_MAPPED: {
                u32 saved = param_names_enter(d); // (A mapped type is a scope)
                // (`+readonly` and `+?` as written: the printer keeps the token)
                bool plus_readonly = false, plus_optional = false;
                if (d.multiline && span_valid(i->key.span)) {
                    // (The key's span: from the modifiers to past the `?`)
                    plus_readonly = i->key.span.buf[0] == '+';
                    char const* close = span_end_ptr(i->key.span);
                    while (close > i->key.span.buf && close[-1] != ']') close--;
                    while (*close == ' ' || *close == '\t' || *close == '\n' || *close == '\r') close++;
                    plus_optional = close < span_end_ptr(i->key.span) && close[0] == '+';
                }
                if (i->key.mapped->readonly) {
                    // (The token: `readonly`, or the `+`/`-` before it)
                    char const* token = D_MAP(d) && span_valid(i->key.span) ? i->key.span.buf : nullptr;
                    if (token && *token == '[') {
                        char const* text = cstr_str(&D_MAP(d)->contents);
                        char const* c = decl_map_back_trivia(token, text);
                        token = c - text >= 8 && !memcmp(c - 8, "readonly", 8) ? c - 8 : nullptr;
                        if (token && token > text && (token[-1] == '+' || token[-1] == '-')) token--;
                    }
                    if (i->key.mapped->readonly < 0 || plus_readonly) {
                        out_token_mapped(d, token, i->key.mapped->readonly < 0 ? c_sv("-") : c_sv("+"));
                        out_lit(d, "readonly ");
                    } else {
                        out_token_mapped(d, token, c_sv("readonly"));
                        out_lit(d, " ");
                    }
                }
                out_lit(d, "[");
                // (The type parameter node from its name, to its constraint's end)
                map_span(d, i->key.mapped->type_name, false);
                map_span(d, i->key.mapped->type_name, false);
                if (d.param_names && i->key.mapped->owned_symbol) {
                    out_type_param_name(me, d, ty_type_param(me, i->key.mapped->owned_symbol), true);
                } else {
                    out(d, span_sv(i->key.mapped->type_name));
                }
                map_span(d, i->key.mapped->type_name, true);
                out_lit(d, " in ");
                // (The types in it print as they would where the mapped type is: EmitFlags.SingleLine is the node's)
                TyDisplay type_d = d;
                type_d.multiline = types_multiline;
                if (!d.multiline) type_d.indent = outer.indent; // (A single line one doesn't indent)
                display_node(me, type_d, &i->key.mapped->in_type);
                map_at(d, decl_map_type_end(&i->key.mapped->in_type, D_MAP(d)));
                if (i->key.mapped->has_as_type) {
                    out_lit(d, " as ");
                    display_node(me, type_d, &i->key.mapped->as_type);
                }
                out_lit(d, "]");
                if (i->key.mapped->optional) {
                    // (The token is the `?`, or the `+`/`-` before it)
                    TypeNode const* last = i->key.mapped->has_as_type ? &i->key.mapped->as_type : &i->key.mapped->in_type;
                    char const* close = token_after(d, decl_map_type_end(last, D_MAP(d)), ']');
                    char const* token = nullptr;
                    if (close) {
                        char const* end = cstr_str(&D_MAP(d)->contents) + cstr_size(&D_MAP(d)->contents);
                        token = decl_map_skip_trivia(close + 1, end);
                        if (token >= end || (*token != '?' && *token != '+' && *token != '-')) token = nullptr;
                    }
                    if (i->key.mapped->optional < 0 || plus_optional) {
                        out_token_mapped(d, token, i->key.mapped->optional < 0 ? c_sv("-") : c_sv("+"));
                        out_lit(d, "?");
                    } else {
                        out_token_mapped(d, token, c_sv("?"));
                    }
                }
                out_lit(d, ": ");
                if (i->type.parsed) display_node(me, type_d, &i->type);
                else out(reused_if(d, false), c_sv("any"));
                param_names_leave(d, saved);
            }   break;
            case INTERFACE_KEY_INDEX:
                if (i->readonly) out_token_mapped(d, D_MAP(d) ? i->declaration.buf : nullptr, c_sv("readonly"));
                if (i->readonly) out_lit(d, " ");
                out_lit(d, "[");
                param_comments_begin_at(d, i->key.index->name);
                out_mapped(d, i->key.index->name);
                out_lit(d, ": ");
                {
                    // (Indented: tsc's LFIndexSignatureParameters)
                    TyDisplay key_d = d;
                    key_d.indent++;
                    display_node(me, key_d, &i->key.index->type);
                }
                // (The parameter ends with its type)
                map_at(d, decl_map_type_end(&i->key.index->type, D_MAP(d)));
                out_lit(d, "]: ");
                display_node(me, d, &i->type);
                break;
            case INTERFACE_KEY_CALLABLE:
                display_node_signature(me, d, i->type.callable->function, ": ");
                break;
            case INTERFACE_KEY_EXPR:
            case INTERFACE_KEY_NAME:
                if (i->readonly) {
                    // (From the JSDoc line's `*` where it's first on its line, like the member)
                    char const* at = D_MAP(d) && span_valid(i->declaration) ? i->declaration.buf : nullptr;
                    map_at(d, at ? decl_map_jsdoc_line_start(D_MAP(d), at) : nullptr);
                    out_lit(d, "readonly");
                    if (at) map_at(d, at + 8);
                    out_lit(d, " ");
                }
                if (i->key.tag == INTERFACE_KEY_EXPR) {
                    display_node_computed_name(me, d, i->key.expr);
                } else if (interface->jsdoc_literal && i->key.span.size && !ty_is_identifier_name(span_sv(i->key.span))) {
                    // (A JSDoc `@property {T} a-b` is a string literal name: tsgo's reparser; one first on a continuation
                    // line of a type literal starts at its `*`)
                    if (D_MAP(d) && span_valid(i->key.span)) map_at(d, decl_map_jsdoc_line_start(D_MAP(d), i->key.span.buf));
                    display_quoted_string(d, span_sv(i->key.span), false, true);
                    map_span(d, i->key.span, true);
                } else if (D_MAP(d) && span_valid(i->key.span) && i->key.span.size && !word_start_char(*i->key.span.buf) &&
                    D_MAP(d)->is_js
                ) {
                    // (A literal name first on a JSDoc continuation line starts at its `*`)
                    map_at(d, decl_map_jsdoc_line_start(D_MAP(d), i->key.span.buf));
                    out(d, span_sv(i->key.span));
                    map_span(d, i->key.span, true);
                } else {
                    out_mapped(d, i->key.span);
                }
                if (i->optional && interface->jsdoc_literal && span_valid(i->declaration)) {
                    // (A bracketed tag's question token is the whole tag: tsgo's reparser)
                    map_at(d, i->declaration.buf);
                    out_lit(d, "?");
                    map_at(d, span_end_ptr(i->declaration));
                } else if (i->optional) {
                    out_token_mapped(d, token_after(d, span_end_ptr(i->key.span), '?'), c_sv("?"));
                }
                if ((i->key.tag == INTERFACE_KEY_NAME ? i->key.name_is_member_function : i->is_method) && i->type.tag == TYPE_CALLABLE) {
                    display_node_signature(me, d, i->type.callable->function, ": ");
                } else {
                    out_lit(d, ": ");
                    // (A JavaScript property signature's type goes through the node builder: tsgo's ensureType,
                    // TryJSTypeNodeToTypeNode)
                    TyDisplay type_d = d;
                    if (d.transformer_node && d.comments && d.comments->file && d.comments->file->is_js) type_d.builder_part = true;
                    if (i->type.parsed) display_node(me, type_d, &i->type);
                    else out(reused_if(d, false), c_sv("any"));
                }
                break;
        }
        if (interface->jsdoc_literal && d.multiline && d.comments && span_valid(comment_span)) {
            // (A JSDoc tag's member ends where its tag does, at the next tag's `@`: no trivia before it to skip back
            // over, no comments in it)
            out_lit(d, ";");
            map_at(d, span_end_ptr(comment_span));
            out_lit(d, "\n");
        } else {
            member_end_node_mapped(d, comment_span, ";,", true);
        }
    }
    brace_close(outer);
}
static bool node_in_mapped_file(TypeNode const* node, SourceFile const* file, SourceFile const* node_file);

// Declaration emit: the comments before a reused list element starting at `start`, on the line of the delimiter before
// it (or of the list's start), unfiltered (the printer's emitListItems' intervening ones)
static void out_intervening_comments(TyDisplay d, char const* start) {
    if (!d.transformer_node || !d.multiline || !d.comments || !start) return;
    decl_comments_intervening(d.comments, d.target, d.indent, start);
    if (decl_map_last_char(cstr_sv(d.target)) == '\n') out_indent(d);
}

// (A list the node builder reuses has them too where it's in the file being emitted: tsgo's visitor keeps the members'
// ranges (setTextRange) and the printer's emitListItems writes the comments at their positions, babel-parser's
// `/*?N.Placeholder<T>*/ MaybePlaceholder<T> | undefined` of a mixin's class expression)
static void display_node_list(Checker* me, TyDisplay d, VecTypeNode const* list, char const* separator) {
    bool first = true;
    TypeNode const* previous = nullptr;
    bool comments = d.multiline && d.comments && !d.comments->off;
    if (comments && !d.transformer_node) {
        // (Only from the file being written: the comments are looked up at the nodes' offsets in its text)
        for_each (i, vec_type, list) {
            if (!i->placeholder && !node_in_mapped_file(i, d.comments->file, d.node_file)) comments = false;
        }
    }
    for_each (i, vec_type, list) {
        if (i->placeholder) continue;
        if (!first) {
            // (Declaration emit: the comments on the lines between the previous element and the delimiter, unless it
            // ends the list's node: emitListItems' emitLeadingComments(previousSibling.End()))
            if (comments && previous && !previous->synthesized) {
                char const* end = decl_map_type_end(previous, d.comments->file);
                if (end && end != d.comments_container_end) {
                    isize_t before = cstr_size(d.target);
                    decl_comments_leading_at(d.comments, d.target, d.indent, end);
                    if (cstr_size(d.target) != before && decl_map_last_char(cstr_sv(d.target)) == '\n') out_indent(d);
                }
            }
            out(d, csview_from(separator));
        }
        first = false;
        previous = i;
        if (comments && !i->synthesized && i->pos) {
            TyDisplay comments_d = d;
            comments_d.transformer_node = true;
            out_intervening_comments(comments_d, decl_map_type_start(i, d.comments->file));
        }
        // A member that needs parentheses without having them written: JSDoc's `?T`, `T=` of a union or function
        // type, and a union or intersection in a union or intersection (tsc's visitor recreates the list node, and the
        // factory's parenthesizeConstituentTypeOfUnionType/IntersectionType wraps them)
        bool parens = !i->parenthesized && !i->readonly_invalid && (i->tag == TYPE_CALLABLE || i->tag == TYPE_UNION ||
            i->tag == TYPE_INTERSECTION || i->tag == TYPE_TERNARY ||
            (i->tag == TYPE_JSDOC && !i->jsdoc->non_null && i->jsdoc->type.tag == TYPE_UNION));
        if (parens) out(reused_if(d, false), c_sv("("));
        TyDisplay member_d = d;
        member_d.constituent_node = !parens;
        display_node(me, member_d, i);
        if (parens) out(reused_if(d, false), c_sv(")"));
    }
}

// A template literal type's part from `at` (past its backtick or `}`): to its `${` or closing backtick included, null
// if the source isn't like that
static char const* template_part_end(char const* at, char const* end, bool last) {
    while (at < end && *at != '`' && !(at[0] == '$' && at + 1 < end && at[1] == '{')) at += *at == '\\' ? 2 : 1;
    if (at >= end || (*at == '`') != last) return nullptr;
    return at + (last ? 1 : 2);
}

// The `}` after a template literal type's type `i`, null if the source isn't like that
static char const* template_part_close(TemplateType const* template, isize_t i, char const* end) {
    Span type = *ast_spans_at(&template->type_spans, i);
    if (!span_valid(type)) return nullptr;
    char const* close = decl_map_skip_trivia(span_end_ptr(type), end);
    return close < end && *close == '}' ? close : nullptr;
}

// A template literal's part as written: the declaration transformer's original node prints its source text, a node the
// node builder reuses (a clone) its raw text, where a CRLF is a line feed
static void out_template_raw(TyDisplay d, csview sv) {
    if (d.transformer_node && d.multiline) {
        out(d, sv);
        return;
    }
    for (isize_t i = 0; i < sv.size;) {
        isize_t run = i;
        while (i < sv.size && sv.buf[i] != '\r') i++;
        out(d, (csview){ sv.buf + run, i - run });
        if (i == sv.size) break;
        i++;
        if (i == sv.size || sv.buf[i] != '\n') out_lit(d, "\n");
    }
}

// A template literal type as written (out_template_raw()), false if the source isn't where its spans say
static bool display_template_source(Checker* me, TyDisplay d, TemplateType const* template) {
    isize_t types = vec_type_size(&template->types);
    if (!span_valid(template->span) || template->span.buf[0] != '`' || ast_spans_size(&template->type_spans) != types) {
        return false;
    }
    char const* end = span_end_ptr(template->span);
    char const* at = template->span.buf + 1;
    for (isize_t i = 0; i <= types; i++) {
        if (!(at = template_part_end(at, end, i == types))) return false;
        if (i < types && !(at = template_part_close(template, i, end))) return false;
        at++;
    }
    at = template->span.buf;
    for (isize_t i = 0; i <= types; i++) {
        char const* part_end = template_part_end(at + 1, end, i == types);
        out_template_raw(d, (csview){ at, part_end - at });
        if (i == types) break;
        TypeNode const* type = vec_type_at(&template->types, i);
        // (Mapped at its start whatever prints it: a type parameter reference of an instantiated signature, effect's
        // `tagStartsWith`, is printed by name without a mapping of its own)
        map_at(d, decl_map_type_start(type, D_MAP(d)));
        display_node(me, d, type);
        at = template_part_close(template, i, end);
        map_at(d, at);
    }
    return true;
}

// Prints an annotation as written, as reused
static void display_node_impl(Checker* me, TyDisplay d, TypeNode const* node);
// A reused annotation counts as its text with the whitespace before it, whatever printing it counted (tsc's
// tryReuseExistingNodeHelper: the node's range, inside a recovery boundary)
static void display_node_mapped(Checker* me, TyDisplay d, TypeNode const* node);
static void display_node(Checker* me, TyDisplay d, TypeNode const* node) {
    // Declaration emit: a node the declaration transformer visits is an original node, with the comments around it
    // (the printer's emitCommentsBeforeNode and emitCommentsAfterNode)
    if (!d.transformer_node || !d.multiline || !d.comments || d.comments->off || node->synthesized) {
        display_node_mapped(me, d, node);
        return;
    }
    SourceFile const* file = d.comments->file;
    char const* start = decl_map_type_start(node, file);
    char const* end = start ? decl_map_type_end(node, file) : nullptr;
    if (!end) {
        display_node_mapped(me, d, node);
        return;
    }
    if (start != d.comments_container_start) {
        // (The writer indents lazily, tsc's writeLine is a no-op at a line start: a line that is only the indentation
        // out_intervening_comments() wrote after a `//` comment is a line start for the comments on the lines below,
        // and the node goes on an indented line after them, ts-proto's `| //\n /** doc */\n { ... }`)
        cstr* target = d.target;
        isize_t size = cstr_size(target), spaces = 0;
        char const* text = cstr_str(target);
        while (spaces < size && text[size - 1 - spaces] == ' ') spaces++;
        bool blank_line = spaces && spaces < size && text[size - 1 - spaces] == '\n';
        if (blank_line) cstr_resize(target, size - spaces, '\0');
        isize_t before = cstr_size(target);
        decl_comments_leading(d.comments, target, d.indent, start);
        if (cstr_size(target) == before) {
            if (blank_line) for (isize_t i = 0; i < spaces; i++) out(d, c_sv(" "));
        } else if (decl_map_last_char(cstr_sv(target)) == '\n') {
            out_indent(d);
        }
    }
    char const* container_end = d.comments_container_end;
    d.comments_container_start = start;
    d.comments_container_end = end;
    display_node_mapped(me, d, node);
    if (end != container_end) decl_comments_trailing(d.comments, d.target, d.indent, end);
}

// The file a reused node was written in, as far as one of its spans says (null if none does)
static SourceFile const* node_source_file(TypeNode const* node, u32 depth) {
    if (depth > 8) return nullptr;
    Span span = span_null;
    switch (node->tag) {
        case TYPE_VALUE: span = node->value_span; break;
        case TYPE_IDENTIFIER: span = node->identifier->name; break;
        case TYPE_INFERRED: span = node->inferred->name; break;
        case TYPE_CALLABLE: span = node->callable && node->callable->function ? node->callable->function->full_span : span_null; break;
        case TYPE_INTERFACE:
            for_each (i, vec_interface_member, &node->interface.members) {
                if (span_valid(i->declaration)) return span_source(i->declaration);
                if (span_valid(i->key.span)) return span_source(i->key.span);
            }
            break;
        case TYPE_ARRAY:
            if (span_valid(node->array.span)) return span_source(node->array.span);
            return node_source_file(node->array.element, depth + 1);
        case TYPE_TUPLE: span = node->tuple.span; break;
        case TYPE_INDEXED: span = node->indexed->span; break;
        case TYPE_KEYOF: return node_source_file(&node->keyof->interface, depth + 1);
        case TYPE_TERNARY: return node_source_file(&node->ternary->maybe_sub, depth + 1);
        case TYPE_TEMPLATE: span = node->template_->span; break;
        case TYPE_TYPEOF: if (node->typeof_->expr) span = expr_outer_span(node->typeof_->expr); break;
        case TYPE_JSDOC: span = node->jsdoc->span; break;
        case TYPE_UNION: case TYPE_INTERSECTION:
            // (The first member that knows its file: a keyword doesn't, `string | null`)
            for_each (i, vec_type, &node->composite) {
                if (i->placeholder) continue;
                SourceFile const* source = node_source_file(i, depth + 1);
                if (source) return source;
            }
            break;
        default: break;
    }
    return span_valid(span) ? span_source(span) : nullptr;
}

// Declaration maps: whether a reused node is one of the file being written (the node builder reuses annotations of
// other files too, whose positions mean nothing here). A keyword is checked by its text there
static bool node_in_mapped_file(TypeNode const* node, SourceFile const* file, SourceFile const* node_file) {
    SourceFile const* source = node_source_file(node, 0);
    if (source) return source == file;
    if (node_file) return node_file == file;
    char const* keyword = nullptr;
    switch (node->tag) {
        case TYPE_ANY: keyword = "any"; break;
        case TYPE_UNKNOWN: keyword = "unknown"; break;
        case TYPE_NEVER: keyword = "never"; break;
        case TYPE_VOID: keyword = "void"; break;
        case TYPE_BOOLEAN: keyword = "boolean"; break;
        case TYPE_NUMBER: keyword = "number"; break;
        case TYPE_STRING: keyword = "string"; break;
        case TYPE_SYMBOL: keyword = "symbol"; break;
        case TYPE_BIGINT: keyword = "bigint"; break;
        default: return true;
    }
    char const* start = decl_map_type_inner_start(node, file);
    if (!start) return true;
    char const* end = cstr_str(&file->contents) + cstr_size(&file->contents);
    if (node->readonly_invalid && end - start >= 8 && !memcmp(start, "readonly", 8)) start = decl_map_skip_trivia(start + 8, end);
    // (JSDoc's `*`, printed `any`)
    if (node->tag == TYPE_ANY && start < end && *start == '*') return true;
    size_t n = strlen(keyword);
    return end - start >= (isize_t)n && !memcmp(start, keyword, n);
}

static void display_node_mapped_inner(Checker* me, TyDisplay d, TypeNode const* node);
static void display_node_mapped(Checker* me, TyDisplay d, TypeNode const* node) {
    // (tsgo's emitTypeNode: in an `extends` clause, a conditional type has to be parenthesized, the parentheses outside
    // the node's mappings; they end the clause's context)
    bool extends_parens = d.in_extends && !node->parenthesized && node->tag == TYPE_TERNARY;
    if (!extends_parens) {
        display_node_mapped_inner(me, d, node);
        return;
    }
    d.in_extends = false;
    out(reused_if(d, false), c_sv("("));
    display_node_mapped_inner(me, d, node);
    out(reused_if(d, false), c_sv(")"));
}

static void display_node_mapped_inner(Checker* me, TyDisplay d, TypeNode const* node) {
    if (D_MAP(d) && (d.reprinted || !node_in_mapped_file(node, D_MAP(d), d.node_file))) d.no_map = true;
    if (D_MAP(d)) {
        // (A node printed as its type isn't mapped, nor is one that has no position here)
        isize_t marked_at = cstr_size(d.target);
        char const* start = decl_map_type_start(node, D_MAP(d));
        // (A parsed one: what JSDoc tags make starts at a tag's `@`)
        map_at(d, node->pos ? decl_map_jsdoc_line_start(D_MAP(d), start) : start);
        bool marked = cstr_size(d.target) != marked_at;
        TypeNode const* outer = node_fell_back;
        node_fell_back = nullptr;
        display_node_impl(me, d, node);
        if (node_fell_back == node) {
            if (marked) cstr_erase(d.target, marked_at, DECL_MAP_MARK_SIZE);
        } else if (start) {
            map_at(d, decl_map_type_end(node, D_MAP(d)));
        }
        node_fell_back = outer;
        return;
    }
    if (!d.approx) {
        display_node_impl(me, d, node);
        return;
    }
    u32* approx = d.approx;
    isize_t start = cstr_size(d.target);
    d.approx = nullptr;
    display_node_impl(me, d, node);
    *approx += (u32)(cstr_size(d.target) - start) + 1;
}

// Declaration maps: a `readonly T[]` is a type operator around the array, which starts past the `readonly`
static char const* readonly_operand_start(TypeNode const* node, SourceFile const* file) {
    char const* start = decl_map_type_inner_start(node, file);
    if (!start) return nullptr;
    char const* end = cstr_str(&file->contents) + cstr_size(&file->contents);
    if (end - start < 8 || memcmp(start, "readonly", 8)) return nullptr;
    return decl_map_skip_trivia(start + 8, end);
}

// The instantiation expression type `typeof f<T>` makes, if printing it fails: its `typeof` node can't be reused
// here (see ty_display()'s cyclic structure placeholder for it)
static Ty const* failing_instantiation_query(Checker* me, TyDisplay d, TypeNode const* node) {
    if (node->tag != TYPE_TYPEOF || !simple_node_fails(me, d, node)) return nullptr;
    Ty const* ty = node_type(me, d, node);
    if (ty->kind != TY_ANONYMOUS || !ty->anonymous.applies_type_args) return nullptr;
    TypeNode const* query = intern_get(&my links, (u64[]){ KEY_INSTANTIATION_QUERY, ty->id }, 2);
    return query && simple_node_fails(me, d, query) ? ty : nullptr;
}

// Whether printing `ty` prints `target` (as far as a few levels of unions, references and object members go)
static bool type_prints_type(Checker* me, Ty const* ty, Ty const* target, u32 depth) {
    if (ty == target) return true;
    if (depth > 4) return false;
    for (u32 i = 0; i < ty->alias_args.len; i++) {
        if (type_prints_type(me, ty->alias_args.data[i], target, depth + 1)) return true;
    }
    if (ty->alias) return false;
    switch ((TyKind)ty->kind) {
        case TY_REFERENCE: {
            TyList args = ty_reference_args(me, ty);
            for (u32 i = 0; i < args.len; i++) {
                if (type_prints_type(me, args.data[i], target, depth + 1)) return true;
            }
            return false;
        }
        case TY_UNION:
        case TY_INTERSECTION:
            for (u32 i = 0; i < ty->composite.types.len; i++) {
                if (type_prints_type(me, ty->composite.types.data[i], target, depth + 1)) return true;
            }
            return false;
        case TY_ANONYMOUS: {
            if (ty->anonymous.applies_type_args) return false;
            Members const* members = ty_members(me, ty);
            for (u32 i = 0; i < members->prop_count; i++) {
                if (type_prints_type(me, prop_type(me, members->props[i]), target, depth + 1)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Whether reusing `node` fails because a part of it does: tsgo's existing node visitor reprints a part that can't be
// reused as its type, and an error doing that (a `typeof f<T>` whose node can't be reused: the cyclic structure
// placeholder) fails the node around it, which prints its type instead, the error dropped (the recovery scope),
// unless that type prints the failing part again: then the error goes up another node
static bool node_part_fails(Checker* me, TyDisplay d, TypeNode const* node, Ty const** failing);
static bool node_child_fails(Checker* me, TyDisplay d, TypeNode const* child, Ty const** failing) {
    Ty const* query = failing_instantiation_query(me, d, child);
    if (query) {
        *failing = query;
        return true;
    }
    Ty const* inner = nullptr;
    if (!node_part_fails(me, d, child, &inner)) return false;
    if (!type_prints_type(me, node_type(me, d, child), inner, 0)) return false;
    *failing = inner;
    return true;
}

static bool node_part_fails(Checker* me, TyDisplay d, TypeNode const* node, Ty const** failing) {
    switch (node->tag) {
        case TYPE_IDENTIFIER: {
            IdentifierType const* identifier = node->identifier;
            if (identifier->keyword) return false;
            for_each (i, vec_spanned_type, &identifier->generic_params) {
                if (node_child_fails(me, d, &i->type, failing)) return true;
            }
            return false;
        }
        case TYPE_ARRAY: return node_child_fails(me, d, node->array.element, failing);
        case TYPE_INDEXED:
            return node_child_fails(me, d, &node->indexed->interface, failing) || node_child_fails(me, d, &node->indexed->index, failing);
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (node_child_fails(me, d, i, failing)) return true;
            }
            return false;
        default: return false;
    }
}

// Whether a node mentions `typeof x<...>` anywhere it is looked into above (a cheap first test)
static bool node_has_instantiation_query(TypeNode const* node) {
    switch (node->tag) {
        case TYPE_TYPEOF: return node->typeof_->expr && node->typeof_->expr->tag == EXPR_GENERIC_PARAMS;
        case TYPE_IDENTIFIER:
            if (node->identifier->keyword) return false;
            for_each (i, vec_spanned_type, &node->identifier->generic_params) {
                if (node_has_instantiation_query(&i->type)) return true;
            }
            return false;
        case TYPE_ARRAY: return node_has_instantiation_query(node->array.element);
        case TYPE_INDEXED: return node_has_instantiation_query(&node->indexed->interface) || node_has_instantiation_query(&node->indexed->index);
        case TYPE_UNION:
        case TYPE_INTERSECTION:
            for_each (i, vec_type, &node->composite) {
                if (node_has_instantiation_query(i)) return true;
            }
            return false;
        default: return false;
    }
}

static void display_node_impl(Checker* me, TyDisplay d, TypeNode const* node) {
    // `readonly T` of what isn't an array or tuple (TS1354): reprinted as written, `readonly (T | null)` around a
    // JSDoc `?T` (the operator is printed, then the node again without it)
    static _Thread_local TypeNode const* readonly_printed;
    if (node->readonly_invalid && readonly_printed != node) {
        out_lit(d, "readonly ");
        bool parens = !node->parenthesized && (node->tag == TYPE_UNION || node->tag == TYPE_INTERSECTION ||
            node->tag == TYPE_CALLABLE || node->tag == TYPE_TERNARY);
        if (parens) out(reused_if(d, false), c_sv("("));
        TypeNode const* outer = readonly_printed;
        readonly_printed = node;
        display_node(me, d, node);
        readonly_printed = outer;
        if (parens) out(reused_if(d, false), c_sv(")"));
        return;
    }
    d.reused = !node->synthesized;
    // (Parentheses end an `extends` clause's context, and so do the parts emitted "outside extends": see
    // display_node_mapped())
    if (node->parenthesized) d.in_extends = false;
    if (!(node->tag == TYPE_CALLABLE || node->tag == TYPE_ARRAY || node->tag == TYPE_INDEXED || node->tag == TYPE_KEYOF ||
            node->tag == TYPE_TERNARY || node->tag == TYPE_UNION || node->tag == TYPE_INTERSECTION || node->tag == TYPE_JSDOC)) {
        d.in_extends = false;
    }
    // (Declaration emit: a part that fails the node, its type instead, see node_part_fails(). Elsewhere the placeholder
    // is no error, tsgo's AllowAnonymousIdentifier: it stays in the node)
    Ty const* failing = nullptr;
    if (d.tracker && node->tag != TYPE_TYPEOF && node_has_instantiation_query(node) && node_part_fails(me, d, node, &failing)) {
        display_node_fallback(me, d, node);
        return;
    }
    // (Only a fallback of this very node, see display_node_identifier() and display_node_typeof(), is an element)
    bool array_element = d.array_element_node && !node->parenthesized;
    bool constituent = d.constituent_node && !node->parenthesized;
    d.array_element_node = false;
    d.constituent_node = false;
    if (node->parenthesized) {
        // (Each nested pair is a node of its own, mapped from inside its `(`)
        SourceFile const* file = D_MAP(d);
        char const* at = node->paren_extra && file ? decl_map_type_start(node, file) : nullptr;
        char const* file_end = file ? cstr_str(&file->contents) + cstr_size(&file->contents) : nullptr;
        for (u32 i = 0; i < node->paren_extra; i++) {
            out_lit(d, "(");
            if (at && at < file_end && *at == '(') {
                at = decl_map_skip_trivia(at + 1, file_end);
                map_at(d, at);
            }
        }
        out_lit(d, "(");
        map_at(d, decl_map_type_inner_start(node, D_MAP(d)));
    }
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
        case TYPE_INTERFACE: {
            // (A mapped type the node builder reuses is SingleLine)
            TyDisplay interface_d = d;
            if ((!d.transformer_node || d.builder_part) && vec_interface_member_size(&node->interface.members) == 1 &&
                vec_interface_member_at(&node->interface.members, 0)->key.tag == INTERFACE_KEY_MAPPED
            ) {
                interface_d.multiline = false;
            }
            display_node_interface(me, interface_d, &node->interface, d.multiline);
            break;
        }
        case TYPE_ARRAY: {
            if (node->array.readonly) {
                out_lit(d, "readonly ");
                map_at(d, readonly_operand_start(node, D_MAP(d)));
            }
            TyDisplay element_d = d;
            element_d.array_element_node = true;
            // (A synthesized array of a union, JSDoc's `{...T|U}`: tsgo's printer parenthesizes the element)
            TypeNode const* element = node->array.element;
            bool parens = !element->parenthesized && (element->tag == TYPE_UNION || element->tag == TYPE_INTERSECTION ||
                element->tag == TYPE_CALLABLE);
            if (parens) out(reused_if(d, false), c_sv("("));
            display_node(me, element_d, element);
            if (parens) out(reused_if(d, false), c_sv(")"));
            out_lit(d, "[]");
            break;
        }
        case TYPE_TUPLE: {
            if (node->tuple.readonly) {
                out_lit(d, "readonly ");
                map_at(d, readonly_operand_start(node, D_MAP(d)));
            }
            out_lit(d, "[");
            // (Declaration emit: written on several lines, one element per line: the printer's MultiLineTupleTypeElements,
            // a reused tuple node not being SingleLine)
            bool lines = false;
            // (An empty one too: the printer's emitList writes the line break of an empty multi-line list)
            if (d.multiline && d.transformer_node && !d.builder_part && d.comments && span_valid(node->tuple.span)) {
                // (From its full start: a tuple on a line of its own after `=` is multi-line, tsc's pos)
                char const* close = span_end_ptr(node->tuple.span);
                while (close > node->tuple.span.buf && close[-1] != ']') close--;
                lines = decl_comments_spans_lines(d.comments, node->tuple.span.buf, close);
            }
            TyDisplay element_d = d;
            if (lines) element_d.indent++;
            for_each (i, vec_tuple_element, &node->tuple.elements) {
                if (lines) {
                    out(d, i_index ? c_sv(",\n") : c_sv("\n"));
                    if (d.comments && span_valid(i->span)) decl_comments_leading(d.comments, d.target, element_d.indent, i->span.buf);
                    member_begin(element_d);
                } else {
                    if (i_index) out_lit(d, ", ");
                    if (span_valid(i->span)) out_intervening_comments(d, i->span.buf);
                }
                TyDisplay outer = d;
                d = element_d;
                // (Its leading comments are written above: an element starting there leaves them)
                if (lines && span_valid(i->span)) d.comments_container_start = i->span.buf;
                // (A named member, a rest type `...T` and an optional type `T?` are nodes: from the element's start to its
                // end)
                bool element_node = span_valid(i->maybe_name) || i->rest || i->optional;
                if (element_node) map_span(d, i->span, false);
                if (i->rest) out_token_mapped(d, span_valid(i->span) && D_MAP(d) ? i->span.buf : nullptr, c_sv("..."));
                if (span_valid(i->maybe_name)) {
                    out_mapped(d, i->maybe_name);
                    if (i->optional) out_token_mapped(d, token_after(d, span_end_ptr(i->maybe_name), '?'), c_sv("?"));
                    out_lit(d, ": ");
                    display_node(me, d, &i->type);
                } else {
                    display_node(me, d, &i->type);
                    if (i->optional) out_lit(d, "?");
                }
                if (element_node) map_span(d, i->span, true);
                d = outer;
            }
            if (lines) {
                out_lit(d, "\n");
                out_indent(d);
            }
            out_lit(d, "]");
            break;
        }
        case TYPE_INDEXED: {
            // (A parenthesized object doesn't fail the whole: its own part prints its type, in the parentheses)
            if (!node->indexed->interface.parenthesized && simple_node_fails(me, d, &node->indexed->interface)) {
                // (Parenthesized where its type needs it as a member or element, see TYPE_KEYOF)
                TyDisplay element_d = d;
                element_d.array_element_node = array_element;
                element_d.constituent_node = constituent;
                display_node_fallback(me, element_d, node);
                break;
            }
            // (Parenthesized as written: tsgo keeps a reused node's parentheses and adds none)
            display_node(me, d, &node->indexed->interface);
            out_lit(d, "[");
            d.in_extends = false;
            display_node(me, d, &node->indexed->index);
            out_lit(d, "]");
            break;
        }
        case TYPE_KEYOF: {
            if (!node->keyof->interface.parenthesized && simple_node_fails(me, d, &node->keyof->interface)) {
                // (Its type, a union of the keys, is parenthesized where this node was a member or element: `keyof T &
                // string` with T mapped to an object type is `("a" | "b") & string`; in a union too, `(A | B) | undefined`,
                // tsgo's factory parenthesizing a union constituent of a union)
                TyDisplay element_d = d;
                element_d.array_element_node = array_element;
                element_d.constituent_node = constituent;
                display_node_fallback(me, element_d, node);
                break;
            }
            out_lit(d, "keyof ");
            display_node(me, d, &node->keyof->interface); // (Parenthesized as written, like an indexed access's object)
            break;
        }
        case TYPE_TERNARY: {
            display_node(me, d, &node->ternary->maybe_sub);
            out_lit(d, " extends ");
            u32 saved = param_names_enter(d); // (Its `infer` type parameters are in scope in the true branch)
            TyDisplay extends_d = d;
            extends_d.in_extends = true;
            display_node(me, extends_d, &node->ternary->maybe_super);
            d.in_extends = false;
            out_lit(d, " ? ");
            display_node(me, d, &node->ternary->if_true);
            param_names_leave(d, saved);
            out_lit(d, " : ");
            display_node(me, d, &node->ternary->if_false);
        }   break;
        case TYPE_INFERRED: {
            out_lit(d, "infer ");
            // (The type parameter node and its name)
            map_span(d, node->inferred->name, false);
            map_span(d, node->inferred->name, false);
            if (d.param_names && node->inferred->owned_symbol) {
                out_type_param_name(me, d, ty_type_param(me, node->inferred->owned_symbol), true);
            } else {
                out(d, span_sv(node->inferred->name));
            }
            map_span(d, node->inferred->name, true);
            if (node->inferred->extends.parsed) {
                out_lit(d, " extends ");
                TyDisplay extends_d = d;
                extends_d.in_extends = true;
                display_node(me, extends_d, &node->inferred->extends);
            }
            if (node->inferred->extends.parsed) map_at(d, decl_map_type_end(&node->inferred->extends, D_MAP(d)));
            else map_span(d, node->inferred->name, true);
        }   break;
        case TYPE_UNION:
            display_node_list(me, d, &node->composite, " | ");
            break;
        case TYPE_INTERSECTION:
            display_node_list(me, d, &node->composite, " & ");
            break;
        case TYPE_TEMPLATE: {
            TemplateType const* template = node->template_;
            if (!node->synthesized && display_template_source(me, d, template)) break;
            out_lit(d, "`");
            for (isize_t i = 0; i < ast_cstrs_size(&template->texts); i++) {
                if (i) {
                    out_lit(d, "${");
                    TypeNode const* type = vec_type_at(&template->types, i - 1);
                    display_node(me, d, type);
                    // (The literal after it starts at its `}`)
                    map_at(d, token_after(d, decl_map_type_end(type, D_MAP(d)), '}'));
                    out_lit(d, "}");
                }
                display_template_text(d, cstr_sv(ast_cstrs_at(&template->texts, i)));
            }
            out_lit(d, "`");
        }   break;
        case TYPE_UNIQUE_SYMBOL: {
            if (node->written_symbol) {
                out_lit(d, "symbol");
                break;
            }
            // (A type operator around the `symbol` keyword)
            char const* symbol = nullptr;
            char const* start = decl_map_type_inner_start(node, D_MAP(d));
            if (start) {
                char const* end = cstr_str(&D_MAP(d)->contents) + cstr_size(&D_MAP(d)->contents);
                char const* c = start;
                while (c < end && (isalnum((unsigned char)*c) || *c == '_')) c++;
                symbol = decl_map_skip_trivia(c, end);
                if (end - symbol < 6 || memcmp(symbol, "symbol", 6)) symbol = nullptr;
            }
            out_lit(d, "unique ");
            out_token_mapped(d, symbol, c_sv("symbol"));
        }   break;
        case TYPE_JSDOC: display_node(me, d, &node->jsdoc->type); break;
        case TYPE_PREDICATE:
            // (Declaration emit writes it as written)
            if (d.transformer_node && d.multiline) {
                TypePredicateNode const* predicate = node->predicate;
                if (predicate->asserts) {
                    out_token_mapped(d, D_MAP(d) ? predicate->span.buf : nullptr, c_sv("asserts"));
                    out_lit(d, " ");
                }
                out_mapped(d, predicate->name);
                if (predicate->has_type) {
                    out_lit(d, " is ");
                    display_node(me, d, &predicate->type);
                }
                break;
            }
            if (node->predicate->asserts) out_lit(d, "void");
            else out_lit(d, "boolean");
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
            // A string literal keeps its quotes, its text escaped again (tsgo's node copy clones the node, which keeps
            // its TokenFlagsSingleQuote, with NoAsciiEscaping, and the printer can't reach the original text of a
            // clone): `'a"b'` stays `'a"b'`, `'\x41'` prints `'A'`
            if (node->value.tag == VALUE_STRING) {
                // (One the declaration transformer visits is the original node: the printer writes its source text)
                if (d.transformer_node && d.multiline && !node->synthesized && span_valid(node->value_span)) {
                    out(d, span_sv(node->value_span));
                    break;
                }
                if (node->value_span.size && node->value_span.buf[0] == '`') {
                    // (A no-substitution template has no raw text: a clone prints its cooked text escaped)
                    display_template_string(d, cstr_sv(&node->value.string));
                    break;
                }
                bool single = node->value_span.size && node->value_span.buf[0] == '\'';
                display_quoted_string(d, cstr_sv(&node->value.string), single, false);
                break;
            }
            // (A number the declaration transformer visits prints as written: `1e999`)
            if (d.transformer_node && d.multiline && node->value.tag == VALUE_NUMBER && span_valid(node->value_span)) {
                csview written = span_sv(node->value_span);
                // (`-1`: a prefix operator around its operand)
                if (written.size && written.buf[0] == '-') {
                    char const* operand = decl_map_skip_trivia(written.buf + 1, written.buf + written.size);
                    out_lit(d, "-");
                    map_at(d, operand);
                    out(d, (csview){ operand, written.buf + written.size - operand });
                    break;
                }
                out(d, written);
                break;
            }
            // Other literals print normalized (`0x10` is 16)
            Ty const* literal = ty_literal(me, &node->value);
            if (literal->kind == TY_LITERAL && !(literal->flags & (TYF_BIGINT_LITERAL | TYF_BOOLEAN_LITERAL)) &&
                !literal->literal.string && node->value.tag == VALUE_NUMBER && node->value.number < 0
            ) {
                out_lit(d, "-");
                // (A prefix operator around its operand)
                if (D_MAP(d) && span_valid(node->value_span) && node->value_span.buf[0] == '-') {
                    map_at(d, decl_map_skip_trivia(node->value_span.buf + 1, span_end_ptr(node->value_span)));
                }
                ty_display(me, d, ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = -node->value.number }));
            } else {
                ty_display(me, d, literal);
            }
        }   break;
    }
    if (node->parenthesized) {
        SourceFile const* file = D_MAP(d);
        char const* at = decl_map_type_inner_end(node, file);
        map_at(d, at);
        out_lit(d, ")");
        char const* file_end = file ? cstr_str(&file->contents) + cstr_size(&file->contents) : nullptr;
        for (u32 i = 0; i < node->paren_extra; i++) {
            if (at && at < file_end) {
                at = decl_map_skip_trivia(at, file_end);
                if (at < file_end && *at == ')') at = at + 1;
                map_at(d, at);
            }
            out_lit(d, ")");
        }
    }
}

// Whether an annotation says what is printed (tsc's typeNodeIsEquivalentToType: an optional parameter or property
// without its `undefined`), so that it can be reprinted
static bool annotation_is_d(Checker* me, TyDisplay d, TypeNode const* node, Ty const* ty, bool optional);
static bool annotation_is(Checker* me, TypeNode const* node, Ty const* ty, bool optional) {
    return annotation_is_d(me, (TyDisplay){0}, node, ty, optional);
}

// The same with the annotation instantiated by `d.mapped_by`
static bool annotation_is_d(Checker* me, TyDisplay d, TypeNode const* node, Ty const* ty, bool optional) {
    // (A `unique symbol` where it can't be one is plain symbol, which it says as well as any annotation: tsc reuses it)
    if (!node->parsed || (node->tag == TYPE_UNIQUE_SYMBOL && node->unique_symbol.key)) return false;
    Ty const* annotated = node_type(me, d, node);
    // (A type that is the error type is taken to be whatever the annotation says: tsgo's pseudoTypeEquivalentToType)
    if (annotated == ty || (ty->flags & TYF_ERROR)) return true;
    // (Without undefined by type facts: `x?: T` with T constrained to undefined, or `x?: void`, isn't reprinted)
    Ty const* target = optional ? ty_with_ne_undefined_facts(me, ty) : ty;
    if (annotated == target) return true;
    // (Unions identical but for an alias, `string | number | null` and `type Id = number | string | null`: tsgo's
    // compareTypesIdentical in pseudoTypeEquivalentToType)
    if (!annotated || annotated->kind != TY_UNION) return false;
    return (target->kind == TY_UNION && ty_is_related(me, annotated, target, TY_RELATION_IDENTITY)) ||
        (target != ty && ty->kind == TY_UNION && ty_is_related(me, annotated, ty, TY_RELATION_IDENTITY));
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
    // (A function or conditional type as a union's member is parenthesized: parenthesizeCheckTypeOfConditionalType)
    if ((((node->tag == TYPE_UNION || node->tag == TYPE_INTERSECTION) && !can_add_undefined(node, 0)) ||
         node->tag == TYPE_CALLABLE || node->tag == TYPE_TERNARY) && !node->parenthesized) {
        out(reused_if(d, false), c_sv("("));
        display_node(me, d, node);
        out(reused_if(d, false), c_sv(")"));
    } else if (node->tag == TYPE_UNION && !node->parenthesized && vec_type_size(&node->composite)) {
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
// baseline and a message's type with `reuse_nodes`
static bool reuses_nodes(TyDisplay d) {
    return (d.types_baseline || d.reuse_nodes) && d.enclosing;
}

static void display_annotated(Checker* me, TyDisplay d, TypeNode const* annotation, Ty const* ty, bool optional) {
    if (annotation && reuses_nodes(d) && annotation_is_d(me, d, annotation, ty, optional)) {
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

// `mapped_by`: the instantiated signature they belong to, whose mapper their declared constraints are reused with
static u32 reused_node_length(Span span);
static void display_type_params(Checker* me, TyDisplay d, TyList params, bool reusable, Signature const* mapped_by) {
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
        // (A copy renamed by inference, `T1`, is a new symbol without declarations: tsc's getUniqueTypeParameterName)
        if (declared->kind == TY_TYPE_PARAM && declared->param.name != params.data[i]->param.name) item = nullptr;
        if (item && item->is_const) out_lit(d, "const ");
        if (item && item->is_in) out_lit(d, "in ");
        if (item && item->is_out) out_lit(d, "out ");
        // (The name isn't counted: tsc's typeParameterToName)
        TyDisplay name_d = reused_if(d, d.reused || item);
        name_d.approx = nullptr;
        name_d.param_declaration = true;
        ty_display(me, name_d, params.data[i]);
        Ty const* constraint = ty_param_constraint(me, params.data[i]);
        if (constraint) {
            out_lit(d, " extends ");
            TyDisplay mapped_d = d;
            mapped_d.mapped_by = mapped_by;
            if (d.reused) {
                ty_display(me, d, constraint);
            } else if (mapped_by && item && item->extends_explicit && ((reusable && reuses_nodes(d)) || (!d.types_baseline && !d.tracker)) &&
                node_type(me, mapped_d, &item->extends) == constraint
            ) {
                // (An instantiated signature's: the declared constraint with the instantiation's mapper, tsgo's
                // typeToTypeNodeHelperWithPossibleReusableTypeNode under enterNewScope's mapper. In messages too)
                u32 before = d.approx ? *d.approx : 0;
                display_node(me, mapped_d, &item->extends);
                // (Counted as the node's range with the whitespace before it, whatever printing it counted:
                // tryReuseExistingNodeHelper's recovery boundary, rxjs's `<K extends T>` printed `<K extends (string |
                // number)[]>` is 2. Known for a type name; other nodes count as printed, see display_node_mapped_inner)
                if (d.approx && item->extends.tag == TYPE_IDENTIFIER && !item->extends.parenthesized &&
                    span_valid(identifier_type_span(item->extends.identifier))
                ) {
                    *d.approx = before + reused_node_length(identifier_type_span(item->extends.identifier));
                }
            } else if (!d.types_baseline && item && item->extends_explicit && annotation_is(me, &item->extends, constraint, false)) {
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
static void track_pattern_keys(Checker* me, TyDisplay d, Binding const* pattern);
static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new);

// (A message's signature counts tsc's approximateLength like a type, one count per signatureToString, so what comes
// after 160 is elided: `Promise<...>`. No overall cut, unlike typeToString, and noErrorTruncation doesn't reach it:
// tsgo's signatureToString never sets NoTruncation)
static void display_signature_top(Checker* me, TyDisplay d, Signature const* sig, char const* separator) {
    u32 approximate_length = 0;
    bool over = false; // (One per print: tsc's context.truncating)
    if (!d.over) d.over = &over;
    if (!d.depth && !d.approx && (!d.enclosing || d.reuse_nodes) && !d.types_baseline && !d.tracker) {
        d.cut_off = true;
        d.approx = &approximate_length;
    } else if (!d.depth && !d.approx && !d.tracker) {
        d.approx = &approximate_length; // (Elided past NO_TRUNCATION_MAXIMUM_LENGTH)
    }
    display_signature(me, d, sig, separator, (sig->flags & SIGNATURE_CONSTRUCT) != 0);
}

void ty_display_signature(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature_top(me, d, sig, ": ");
}

// tsc's signatureToString with WriteArrowStyleSignature: `new (x: number) => C`
void ty_display_signature_arrow(Checker* me, TyDisplay d, Signature const* sig) {
    display_signature_top(me, d, sig, " => ");
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
        Atom const* name = checker_atom(me, span_sv(binding->content_span));
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
    SigResolved const* sig_r = sig_res(me, sig);
    if (!sig_r->param_count || !sig_r->params[sig_r->param_count - 1].rest) return nullptr;
    SigParam const* rest = &sig_r->params[sig_r->param_count - 1];
    TupleShape const* shape = ty_tuple_shape(rest->type);
    if (!shape) return nullptr;
    bool empty = !ty_is_tuple(rest->type);
    for (u32 i = 0; i + 1 < shape->arity; i++) {
        if (shape->flags[i] & TUPLE_ELEMENT_REST) return nullptr;
    }
    TyList elements = empty ? (TyList){0} : ty_reference_args(me, rest->type);
    u32 count = sig_r->param_count - 1 + shape->arity;
    SigParam* params = arena_new_n(&my arena, SigParam, count ? count : 1);
    memcpy(params, sig_r->params, sizeof(SigParam) * (sig_r->param_count - 1));
    for (u32 i = 0; i < shape->arity; i++) {
        Atom const* name = shape->names && shape->names[i] ? shape->names[i] : rest_element_label(me, rest, i, shape->flags[i]);
        // Labels are made unique: `[a: A, a: B]` prints `a: A, a_1: B`
        Atom const* base = name;
        for (u32 counter = 1, j = sig_r->param_count - 1; j < sig_r->param_count - 1 + i; j++) {
            if (params[j].name != name) continue;
            name = numbered_name(me, atom_sv(base), "_%u", counter++);
            j = sig_r->param_count - 2;
        }
        params[sig_r->param_count - 1 + i] = (SigParam){
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
static bool pseudo_equivalent(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty, bool optional, PseudoReporter const* rep);
static void pseudo_report(PseudoReporter const* rep, PseudoError error);
static PseudoReporter const* display_reporter(TyDisplay d, PseudoReporter* storage);
static void display_pseudo_with_fallback(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty);
static Ty const* pseudo_type(Checker* me, TyDisplay d, Pseudo const* pseudo);
static void display_pseudo(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty);
static void display_signature(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
    u32 saved = param_names_enter(d);
    display_signature_inner(me, d, sig, separator, with_new);
    param_names_leave(d, saved);
}

// Whether the declared signature at the root of an instantiated one mentions what the instantiations map (other than
// its own type parameters). tsc doesn't instantiate a signature that doesn't (getObjectTypeInstantiation keeps only the
// outer type parameters the declaration may reference): it prints as declared, fresh type parameters and all
static bool mentions_mapped(Checker* me, Signature const* sig, Signature const* root) {
    SigResolved const* root_r = sig_res(me, root);
    u32 count = 0;
    for (Signature const* i = sig; i != root; i = sig_target(me, i)) count += sig_mapper(me, i) ? sig_mapper(me, i)->len : 0;
    Ty const** sources = arena_new_n(&my arena, Ty const*, count ? count : 1);
    u32 len = 0;
    for (Signature const* i = sig; i != root; i = sig_target(me, i)) {
        for (u32 j = 0; sig_mapper(me, i) && j < sig_mapper(me, i)->len; j++) {
            Ty const* source = sig_mapper(me, i)->sources[j];
            bool own = false;
            for (u32 k = 0; k < root_r->type_params.len; k++) own |= root_r->type_params.data[k] == source;
            if (!own) sources[len++] = source;
        }
    }
    TyList mapped = { .data = sources, .len = len };
    if (!len) return false;
    // (tsc's isTypeParameterPossiblyReferenced is syntactic: a body without a return type annotation may reference anything)
    FunctionImpl const* function = root->function;
    if (function && function->has_body && !function->ret_explicit) return true;
    for (u32 i = 0; i < root_r->param_count; i++) {
        if (mentions(me, root_r->params[i].type, mapped)) return true;
    }
    for (u32 i = 0; i < root_r->type_params.len; i++) {
        Ty const* constraint = ty_param_constraint(me, root_r->type_params.data[i]);
        if (constraint && mentions(me, constraint, mapped)) return true;
        // (A default too, `ext<E = Options>()`: printed `<E = string>` for Ext<string>)
        Ty const* fallback = ty_param_default(me, root_r->type_params.data[i]);
        if (fallback && mentions(me, fallback, mapped)) return true;
    }
    TyPredicate const* predicate = signature_predicate(me, root);
    if (predicate && predicate->type && mentions(me, predicate->type, mapped)) return true;
    Ty const* ret = signature_return_type(me, root);
    if ((root_r->this_type && mentions(me, root_r->this_type, mapped)) || mentions(me, ret, mapped)) return true;
    // (An annotation whose type is an error, an unresolved import in it, mentions nothing as a type, but tsc's check is
    // syntactic: `x: Unresolved | ((s: Readonly<S>) => void)` is instantiated, its reused node prints the mapped `S`)
    bool error = (ret->flags & TYF_ERROR) != 0;
    for (u32 i = 0; i < root_r->param_count && !error; i++) error = (root_r->params[i].type->flags & TYF_ERROR) != 0;
    if (!error) return false;
    TyList free_params;
    if (!function || !ty_function_free_params(me, function, &free_params)) return true;
    for (u32 i = 0; i < free_params.len; i++) {
        for (u32 j = 0; j < mapped.len; j++) if (mapped.data[j] == free_params.data[i]) return true;
    }
    return false;
}

// Whether an instantiated signature's mapper maps a type parameter of a merged declaration (an interface declared twice):
// tsc's isTypeParameterPossiblyReferenced says yes for those whatever the node, so every anonymous type of the signature
// is a new instantiation, which no annotation says. `this` too, whose declarations are its class's or interface's: a
// method of a non-thisless interface declared twice (IteratorConstructor) has fresh type parameters, so `Iterator.from`'s
// `value: Iterator<T> | Iterable<T>` prints its type, not the annotation
static bool maps_merged_type_param(Checker* me, Signature const* sig, Signature const* root) {
    Binder const* binder = program_get_binder(my program);
    for (Signature const* i = sig; i && i != root; i = sig_target(me, i)) {
        for (u32 j = 0; sig_mapper(me, i) && j < sig_mapper(me, i)->len; j++) {
            Ty const* source = sig_mapper(me, i)->sources[j];
            if (source->kind != TY_TYPE_PARAM) continue;
            // (`this` is declared by its class or interface: IteratorConstructor's two interfaces)
            Symbol const* symbol = source->param.symbol;
            if (source->param.state->is_this) {
                Ty const* owner = ty_param_constraint_published(source->param.state); // (`this`'s, made with it)
                if (owner && owner->kind == TY_REFERENCE) owner = owner->reference.target;
                symbol = owner && owner->kind == TY_DECLARED ? owner->declared.symbol : nullptr;
            }
            if (symbol && binder_redirect(binder, symbol)->decl_count > 1) return true;
        }
    }
    return false;
}

// The `this` parameter of a signature being printed, `this: T`
static void display_this_param(
    Checker* me, TyDisplay d, Signature const* sig, Signature const* root, FunctionImpl const* function, bool reusable,
    bool jsdoc_this, bool instantiated
) {
    SigResolved const* sig_r = sig_res(me, sig);
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
        !mentions(me, sig_r->this_type, d.fresh_params) && (!instantiated || sig_r->this_type == sig_res(me, root)->this_type);
    // A function expression's `this` from its contextual signature is that signature's parameter (tsc's
    // assignContextualParameterTypes: createSymbolWithType(context.thisParameter)), whose annotation is reused
    Signature const* contextual = function && !binding && reusable ? ty_function_assigned_contextual_signature(me, function) : nullptr;
    while (contextual && !contextual->function && (contextual->target || contextual->declared_by)) contextual = contextual->target ? contextual->target : contextual->declared_by;
    FunctionImpl const* context_function = contextual ? contextual->function : nullptr;
    FunctionArg const* context_this = context_function && vec_function_arg_size(&context_function->args) ?
        vec_function_arg_at(&context_function->args, 0) : nullptr;
    if (context_this && !context_this->jsdoc_this && context_this->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(context_this->initializer.binding.content_span, "this") &&
        context_this->initializer.binding.has_explicit_type && sig_res(me, contextual)->this_type == sig_r->this_type
    ) {
        binding = &context_this->initializer.binding;
        reused = true;
    }
    display_annotated(me, reused_if(d, false), reused ? binding_type(binding) : nullptr, sig_r->this_type, false);
    approx_add(d, 4 + 3); // (After its type, like the parameters)
}

static void display_signature_inner(Checker* me, TyDisplay d, Signature const* sig, char const* separator, bool with_new) {
    SigResolved const* sig_rs = signature_resolve(me, sig);
    approx_add(d, 3); // (tsc's signatureToSignatureDeclarationHelper; its parameters each their name and three)
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
    for (; sig_target(me, root); root = sig_target(me, root)) {
        class_default |= root->class_instance != nullptr;
        for (u32 i = 0; sig_mapper(me, root) && i < sig_mapper(me, root)->len; i++) {
            Ty const* source = sig_mapper(me, root)->sources[i];
            this_only &= source->kind == TY_TYPE_PARAM && source->param.state->is_this;
        }
    }
    SigResolved const* root_res = signature_resolve(me, root);
    // Inside a declared signature with parameters or type parameters the node builder's enclosing declaration is a fake
    // scope for them (tsc's enterNewScope; type parameters under GenerateNamesForShadowedTypeParams, which printing
    // sets), so no import or export around supplies the originalModuleSpecifier: modules print as from the file, by its
    // default resolution mode (`import("./types")` where the import's ESM usage would add `/index.js`)
    if ((sig_rs->param_count || sig_rs->type_params.len) && (root->function || root->declared_by)) d.usage_in_fake_scope = true;
    bool sibling_mapped = d.sibling_mentions_mapped;
    d.sibling_mentions_mapped = false;
    // (Unless its own type parameters were instantiated away: a call's candidate with what was inferred)
    bool own_instantiated = root_res->type_params.len && !sig_rs->type_params.len;
    // (A merged declaration's type parameter may be referenced anywhere: tsc instantiates the whole member)
    bool merged_mapped = root != sig && maps_merged_type_param(me, sig, root);
    if (root != sig && !this_only && !class_default && !own_instantiated && !sibling_mapped && !merged_mapped &&
        !mentions_mapped(me, sig, root)
    ) {
        sig = root;
    }
    bool instantiated = root != sig;
    bool fresh_anonymous = instantiated && merged_mapped;
    bool own_params_fresh = d.own_params_fresh && (!instantiated || this_only) &&
        !(instantiated && (sibling_mapped || merged_mapped || mentions_mapped(me, sig, root)));
    d.own_params_fresh = false;
    if (own_params_fresh && root_res->type_params.len) {
        if (d.fresh_params.len) {
            Ty const** both = arena_new_n(&my arena, Ty const*, d.fresh_params.len + root_res->type_params.len);
            memcpy(both, d.fresh_params.data, sizeof(Ty const*) * d.fresh_params.len);
            memcpy(both + d.fresh_params.len, root_res->type_params.data, sizeof(Ty const*) * root_res->type_params.len);
            d.fresh_params = (TyList){ .data = both, .len = d.fresh_params.len + root_res->type_params.len };
        } else {
            d.fresh_params = root_res->type_params;
        }
    }
    // (Added to an enclosing signature's: a function type in a parameter of `then<U>` of `P<number>` doesn't reuse a
    // `U` it mentions either)
    // (Instantiated for `this` only, it's the same: tsc instantiates a member that mentions `this` (a class or interface
    // reference's members are instantiated with `this` as the reference), with fresh type parameters)
    if (instantiated && root_res->type_params.len && (!this_only || sibling_mapped || merged_mapped || mentions_mapped(me, sig, root))) {
        if (d.fresh_params.len) {
            Ty const** both = arena_new_n(&my arena, Ty const*, d.fresh_params.len + root_res->type_params.len);
            memcpy(both, d.fresh_params.data, sizeof(Ty const*) * d.fresh_params.len);
            memcpy(both + d.fresh_params.len, root_res->type_params.data, sizeof(Ty const*) * root_res->type_params.len);
            d.fresh_params = (TyList){ .data = both, .len = d.fresh_params.len + root_res->type_params.len };
        } else {
            d.fresh_params = root_res->type_params;
        }
    }
    // (A class's constructor has its parameters reused, but it returns the class)
    FunctionImpl const* function = root->function && root->class_instance && !root->function->is_constructor ? nullptr : root->function;
    // (A JSDoc `@this` makes a parameter of its own, which isn't reused)
    bool jsdoc_this = function && vec_function_arg_size(&function->args) && function->args.data[0].jsdoc_this;
    bool reusable = (d.types_baseline || d.reuse_nodes) && function;
    // (Its annotations were written in its file: babel-standalone's keyword annotations of another file's signature)
    d.node_file = function && span_valid(function->full_span) ? span_source(function->full_span) : nullptr;
    // (An instantiated signature's parameters that could contain type variables are copies: one named like a type
    // parameter shadows it, see TyParamNames.value_params)
    SigParam* copies = nullptr;
    u32 copy_count = 0;
    if (instantiated && sig_res(me, sig)->type_params.len && d.param_names && !d.param_names->value_param_count) {
        for (u32 i = 0; i < root_res->param_count && i < sig_res(me, sig)->param_count; i++) {
            bool named_like = false;
            for (u32 j = 0; j < sig_res(me, sig)->type_params.len && !named_like; j++) {
                named_like = sig_res(me, sig)->type_params.data[j]->param.name == root_res->params[i].name;
            }
            if (!named_like || !mentions(me, root_res->params[i].type, root_res->type_params)) continue;
            if (!copies) copies = arena_new_n(&my arena, SigParam, root_res->param_count);
            copies[copy_count++] = root_res->params[i];
        }
        d.param_names->value_params = copies;
        d.param_names->value_param_count = copy_count;
    }
    FunctionImpl const* saved_params_of = d.param_names ? d.param_names->params_of : nullptr;
    if (d.param_names) d.param_names->params_of = root->function;
    display_type_params(me, d, sig_res(me, sig)->type_params, reusable, instantiated ? sig : nullptr);
    if (d.param_names) d.param_names->params_of = saved_params_of;
    if (copy_count) {
        d.param_names->value_params = nullptr;
        d.param_names->value_param_count = 0;
    }
    out_lit(d, "(");
    u32 param_count = sig_res(me, sig)->param_count;
    SigParam const* params = expanded_params(me, sig, &param_count);
    if (!params) params = sig_res(me, sig)->params;
    TyDisplayParams printed = {
        .params = params, .count = param_count, .originals = sig_res(me, sig)->params, .original_count = sig_res(me, sig)->param_count,
        .function = function, .outer = d.printed_params, .serial = d.printed ? ++d.printed->serial : 0,
    };
    if (d.types_baseline || d.reuse_nodes) d.printed_params = &printed;
    if (root->function && (param_count || sig_res(me, sig)->type_params.len)) d.fake_scope = true;
    // (tsc's signatureToSignatureDeclarationHelper makes the parameters' nodes before the `this` parameter's: a
    // message counts their length first, so a long `this` type is what gets cut short, `Record<...>`. Printed in
    // place afterwards; not where positions are mapped or comments copied)
    bool this_later = sig_res(me, sig)->this_type && d.approx && !d.tracker && !D_MAP(d) && !d.comments;
    size_t this_at = cstr_size(d.target);
    if (sig_res(me, sig)->this_type && !this_later) {
        display_this_param(me, d, sig, root, function, reusable, jsdoc_this, instantiated);
        if (param_count) out_lit(d, ", ");
    }
    for (u32 i = 0; i < param_count; i++) {
        SigParam const* param = &params[i];
        if (i) out_lit(d, ", ");
        if (param->rest) out(reused_if(d, d.reused), c_sv("..."));
        Binding const* name_binding = param->decl ? &param->decl->initializer.binding : nullptr;
        // (A binding pattern's symbol is `__0`)
        bool pattern_name = name_binding && (name_binding->tag == BINDING_ARRAY || name_binding->tag == BINDING_OBJECT);
        // (Counted after its type: tsc's symbolToParameterDeclaration makes the type first)
        u32 name_length = (pattern_name ? 3 : (u32)atom_sv(param->name).size) + 3;
        if (name_binding && (name_binding->tag == BINDING_ARRAY || name_binding->tag == BINDING_OBJECT)) {
            // (Its computed keys are tracked: the node builder's cloneBindingName, trackComputedName)
            track_pattern_keys(me, d, name_binding);
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
        out(reused_if(d, d.reused), c_sv(": "));
        Binding const* binding = param->decl ? &param->decl->initializer.binding : nullptr;
        // (An instantiated one whose annotation instantiates to its type: tsgo's node reuse instantiates the annotation's
        // type with the signature's mapper, the same instantiation, `p.finally` of a merged `Promise<T>` whose
        // `() => void` is instantiated with T)
        TyDisplay mapped_param_d = d;
        mapped_param_d.mapped_by = sig;
        // (Its type without the undefined its optionality added: what its annotation says)
        Ty const* declared = ty_sig_param_declared_type(me, param);
        bool mapped_same = instantiated && binding && binding->has_explicit_type && param->type != root_res->params[i].type &&
            node_names_no_type_params(binding_type(binding), 0) && node_type(me, mapped_param_d, binding_type(binding)) == declared;
        bool reused = reusable && binding && binding->has_explicit_type && !contextual_jsdoc &&
            (mapped_same || (!mentions(me, param->type, d.fresh_params) && (!instantiated || param->type == root_res->params[i].type))) &&
            !(fresh_anonymous && declared->kind == TY_ANONYMOUS) &&
            // (A `@param {T} [x]` tag of a JSDoc signature (`@overload`, `@callback`) declares x without a `?` that
            // would say the undefined its type has: tsc doesn't reuse `T` there)
            !(function && function->jsdoc_callback && param->decl->explicit_optional && !binding_type(binding)->jsdoc_optional &&
                compiler_options()->strict_null_checks);
        TyDisplay param_d = reused_if(d, false);
        Expr const* initializer = param->decl ? initialized_binding_value(&param->decl->initializer) : nullptr;
        // An optional parameter's type has undefined under strictNullChecks (tsc's getOptionalType in
        // serializeTypeForDeclaration), which a reused annotation doesn't say
        // (Not a binding pattern's without annotation or initializer: that type is the pattern's as it is, tsc's
        // getTypeForVariableLikeDeclaration returns getTypeFromBindingPattern without addOptionality)
        bool bare_pattern = binding && binding->tag != BINDING_IDENTIFIER && !binding->has_explicit_type &&
            initializer && initializer->tag == EXPR_IMPLICIT_ANY;
        Ty const* shown = ty_sig_param_symbol_type(me, param);
        if (param->optional && (!param->rest || (binding && binding->optional_parameter)) && param->decl &&
            (param->decl->explicit_optional || param->decl->jsdoc_optional_initialized) &&
            compiler_options()->strict_null_checks && ty_without_undefined(me, shown) == shown && !bare_pattern
        ) {
            shown = ty_union(me, (Ty const*[]){ shown, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
        }
        // The parameter's pseudo type where it says the same (tsgo's serializeTypeForDeclaration of the parameter):
        // an initialized parameter before a required one has undefined added (requiresAddingImplicitUndefined), an
        // optional one's undefined isn't in its annotation (isOptionalAnnotated)
        // (Not where its type mentions fresh type parameters: tsgo's is then another type than its annotation's)
        if (reusable && reuses_nodes(param_d) && !d.reused && param->decl && !contextual_jsdoc &&
            !function->jsdoc_callback && !function->jsdoc_type && !(own_params_fresh && mentions(me, param->type, d.fresh_params)) &&
            param->decl >= function->args.data && param->decl < function->args.data + vec_function_arg_size(&function->args)
        ) {
            Pseudo const* pseudo = pseudo_of_parameter(me, function, param->decl);
            bool initialized = initializer && initializer->tag != EXPR_IMPLICIT_ANY;
            bool add_undefined = compiler_options()->strict_null_checks && !param->optional && initialized && !param->rest;
            Ty const* type = shown;
            if (add_undefined && ty_without_undefined(me, type) == type) {
                type = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
            }
            bool optional_annotated = !add_undefined && param->decl->explicit_optional;
            PseudoReporter storage;
            PseudoReporter const* rep = display_reporter(d, &storage);
            bool equivalent = pseudo_equivalent(me, d, pseudo, type, optional_annotated, rep);
            // (Its error nodes reported: its type is printed without reporting again)
            bool reported = !equivalent && rep && pseudo->kind == PSEUDO_INFERRED && pseudo->count;
            // (Whether the pseudo type has undefined: its type's say if it has one, else its syntax)
            Ty const* as_type = add_undefined ? pseudo_type(me, d, pseudo) : nullptr;
            bool lacks_undefined = as_type ? ty_without_undefined(me, as_type) == as_type : !pseudo_could_be_undefined(pseudo);
            if (equivalent && add_undefined && as_type && lacks_undefined && ty_without_undefined(me, type) != type) {
                pseudo = pseudo_add_undefined(me, pseudo);
            } else if (!equivalent && add_undefined && lacks_undefined) {
                pseudo = pseudo_add_undefined(me, pseudo);
                equivalent = pseudo_equivalent(me, d, pseudo, type, false, rep);
            }
            if (equivalent) {
                TyDisplay pseudo_d = param_d;
                pseudo_d.mapped_by = instantiated ? sig : nullptr;
                display_pseudo_with_fallback(me, pseudo_d, pseudo, type);
                approx_add(d, name_length);
                continue;
            }
            if (reported) param_d.suppress_fallback = true;
        }
        if (reused && reuses_nodes(param_d) && !param->optional && annotation_plus_undefined(me, binding_type(binding), shown)) {
            // The annotation with undefined added (a parameter with an initializer before a required one): tsc reuses
            // the annotation in a new union with `undefined` (serializeTypeForDeclaration's addUndefined)
            display_node_plus_undefined(me, param_d, binding_type(binding));
        } else {
            TyDisplay annotated_d = param_d;
            if (mapped_same) annotated_d.mapped_by = sig;
            display_annotated(me, annotated_d, reused ? binding_type(binding) : nullptr, shown, param->optional);
        }
        approx_add(d, name_length);
    }
    if (this_later) {
        cstr this_text = cstr_init();
        TyDisplay this_d = d;
        this_d.target = &this_text;
        display_this_param(me, this_d, sig, root, function, reusable, jsdoc_this, instantiated);
        if (param_count) out_lit(this_d, ", ");
        insert_printed(d, (isize_t)this_at, &this_text);
    }
    out_lit(d, ")");
    out(d, csview_from(separator));
    TyPredicate const* predicate = signature_predicate(me, sig);
    Ty const* ret = signature_return_type(me, sig);
    // (A signature of a declaration whose return type is being printed through its pseudo type further up returns that
    // type, instantiated or not: tsgo's serializeReturnTypeForSignature reads context.enclosingSymbolTypes first, so a
    // function returning an instantiation of itself prints the cycle as `any`)
    bool enclosing_symbol_type = false;
    for (TyDisplaySymbolType const* i = d.symbol_types; i && root->function; i = i->outer) {
        if (i->decl != root->function) continue;
        ret = i->type;
        enclosing_symbol_type = true;
        break;
    }
    // (Otherwise the instantiated signature's return type instantiated by its mapper once more, tsgo's
    // instantiateType(getReturnTypeOfSignature(signature), ctx.mapper): what maps a type parameter to a type that names
    // it, `U` to `Value<K, U>`, is applied twice)
    if (!enclosing_symbol_type && instantiated && root->function) ret = instantiate_for(me, sig, ret);
    TypeNode const* ret_annotation = !reusable || root->class_instance || !function->ret_explicit ? nullptr : &function->ret;
    bool reused = ret_annotation &&
        !mentions(me, ret, d.fresh_params) && (!instantiated || ret == signature_return_type(me, root)) &&
        !(fresh_anonymous && ret->kind == TY_ANONYMOUS);
    if (predicate) {
        // (An inferred predicate: the pseudo return type doesn't say it, reported at the function)
        PseudoReporter storage;
        PseudoReporter const* rep = reusable && reuses_nodes(d) && !d.reused && !root->class_instance && !function->predicate &&
            !function->ret_explicit ? display_reporter(d, &storage) : nullptr;
        pseudo_report(rep, (PseudoError){ .kind = PSEUDO_ERROR_FUNCTION, .function = function });
        // In an instantiated signature, a parameter it names isn't the one of the instantiated signature (tsc resolves
        // it to the declaration's own), so the whole predicate is printed
        bool names_param = function && function->predicate && !function->predicate->is_this;
        TyDisplay predicate_d = reused_if(d, reusable && function->predicate && !(instantiated && !this_only && names_param));
        if (predicate_d.reused && reuses_nodes(d) && (!predicate->type || function->predicate->has_type)) {
            // (The written node reused: its keyword and parameter name are mapped, like the declaration transformer's)
            TypePredicateNode const* written = function->predicate;
            map_span(predicate_d, written->span, false);
            if (written->asserts) {
                out_token_mapped(predicate_d, D_MAP(predicate_d) ? written->span.buf : nullptr, c_sv("asserts"));
                out_lit(predicate_d, " ");
            }
            out_mapped(predicate_d, written->name);
            if (predicate->type) {
                out_lit(predicate_d, " is ");
                predicate_d.mapped_by = instantiated && !this_only ? sig : nullptr;
                display_node(me, predicate_d, &written->type);
            }
            map_span(predicate_d, written->span, true);
            return;
        }
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
    // The return type as the declaration's syntax says it, where that's the same type (tsgo's
    // serializeReturnTypeForSignature: its pseudo type; an instantiated signature's own type parameters are fresh, see
    // TyDisplay.fresh_params)
    if (reusable && reuses_nodes(d) && !d.reused && !root->class_instance) {
        Pseudo const* pseudo = pseudo_of_return(me, function);
        PseudoReporter storage;
        if (pseudo_equivalent(me, d, pseudo, ret, false, display_reporter(d, &storage))) {
            // (Reused through the instantiation's mapper, tsgo's ctx.mapper: what equivalence let through is an error
            // type's instantiation, `T` of `Array<error>` prints `any`)
            TyDisplay pseudo_d = ret_d;
            pseudo_d.mapped_by = instantiated ? sig : nullptr;
            TyDisplaySymbolType symbol_type = { .decl = function, .type = ret, .outer = d.symbol_types };
            pseudo_d.symbol_types = &symbol_type;
            display_pseudo_with_fallback(me, pseudo_d, pseudo, ret);
            return;
        }
        // (serializeInferredReturnTypeForSignature: the checker's type, without reporting in it)
        ret_d.suppress_fallback = true;
    }
    display_annotated(me, ret_d, reused ? ret_annotation : nullptr, ret, false);
}

// The pattern ends with a comma before its `]` or `}` (tsc's NodeArray.hasTrailingComma, which the printer keeps):
// what follows its last element, comments skipped (`b = 1, // why`)
static bool pattern_has_trailing_comma(Binding const* binding) {
    csview text = span_sv(binding->content_span);
    if (!text.size) return false;
    char const* end = text.buf + text.size - 1; // On the closing bracket
    Binding const* last = nullptr;
    if (binding->tag == BINDING_ARRAY && binding->array.size) last = &binding->array.data[binding->array.size - 1];
    if (binding->tag == BINDING_OBJECT && binding->object.size) last = &binding->object.data[binding->object.size - 1].target;
    char const* c = nullptr;
    if (last) {
        Span after = last->default_value ? expr_outer_span(last->default_value) : last->content_span;
        if (span_valid(after) && span_end_ptr(after) > text.buf && span_end_ptr(after) <= end) c = span_end_ptr(after);
    }
    if (!c) {
        // (No element to start from, an omitted one: the last character before the bracket)
        for (isize_t i = text.size - 2; i >= 0; i--) {
            char ch = text.buf[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
            return ch == ',';
        }
        return false;
    }
    while (c < end) {
        if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
            c++;
        } else if (c + 1 < end && c[0] == '/' && c[1] == '/') {
            while (c < end && *c != '\n') c++;
        } else if (c + 1 < end && c[0] == '/' && c[1] == '*') {
            c += 2;
            while (c + 1 < end && !(c[0] == '*' && c[1] == '/')) c++;
            c += 2;
        } else {
            return *c == ',';
        }
    }
    return false;
}

// A destructured parameter's name the way tsc prints it in a signature: the pattern without its defaults
// Declaration maps: `at` (a position in `map`, the file being written) as a mark in `out`
static void mark_in(cstr* out, SourceFile const* map, char const* at) {
    if (!map || !at) return;
    char const* text = cstr_str(&map->contents);
    if (at < text || at > text + cstr_size(&map->contents)) return;
    decl_map_mark(out, (u32)(at - text));
}

static void mark_span_in(cstr* out, SourceFile const* map, Span span, bool end) {
    if (map && span_valid(span)) mark_in(out, map, end ? span_end_ptr(span) : span.buf);
}

// A binding pattern as written (its default values left out). With `map`, each of its nodes is mapped: the patterns,
// their elements (from a `...` or property name to the name or default value), the names
// Declaration emit: the comments around a parameter's binding pattern's elements (emitListItems' intervening ones and
// each element's leading ones), null elsewhere
static _Thread_local DeclComments const* pattern_comments;
static _Thread_local u32 pattern_indent;

static void pattern_element_comments(cstr* out, char const* start) {
    if (!pattern_comments || !start) return;
    decl_comments_intervening(pattern_comments, out, pattern_indent, start);
    decl_comments_leading(pattern_comments, out, pattern_indent, start);
    if (decl_map_last_char(cstr_sv(out)) == '\n') {
        for (u32 i = 0; i < pattern_indent; i++) cstr_append_sv(out, c_sv("    "));
    }
}

static void append_binding_pattern_mapped(cstr* out, Binding const* binding, SourceFile const* map) {
    if (binding->rest) cstr_append_sv(out, c_sv("..."));
    if (binding->tag == BINDING_ARRAY) {
        mark_span_in(out, map, binding->content_span, false);
        cstr_append_sv(out, c_sv("["));
        for (isize_t i = 0; i < binding->array.size; i++) {
            if (i) cstr_append_sv(out, c_sv(", "));
            Binding const* element = &binding->array.data[i];
            // (The element: from its name, or `...`, to its default value)
            char const* start = span_valid(element->content_span) ? element->content_span.buf : nullptr;
            if (map && start && element->rest) {
                char const* c = start;
                char const* text = cstr_str(&map->contents);
                while (c > text && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
                if (c - text >= 3 && !memcmp(c - 3, "...", 3)) start = c - 3;
            }
            pattern_element_comments(out, start);
            mark_in(out, map, start);
            append_binding_pattern_mapped(out, element, map);
            mark_span_in(out, map, element->default_value ? expr_outer_span(element->default_value) : element->content_span, true);
        }
        if (binding->array.size && pattern_has_trailing_comma(binding)) cstr_append_sv(out, c_sv(","));
        cstr_append_sv(out, c_sv("]"));
        mark_span_in(out, map, binding->content_span, true);
    } else if (binding->tag == BINDING_OBJECT) {
        mark_span_in(out, map, binding->content_span, false);
        if (!binding->object.size) {
            cstr_append_sv(out, c_sv("{}"));
            mark_span_in(out, map, binding->content_span, true);
            return;
        }
        cstr_append_sv(out, c_sv("{ "));
        for (isize_t i = 0; i < binding->object.size; i++) {
            ObjectBindingItem const* item = &binding->object.data[i];
            if (i) cstr_append_sv(out, c_sv(", "));
            bool shorthand = item->target.rest || (item->target.tag == BINDING_IDENTIFIER &&
                item->target.content_span.buf == item->source_span.buf);
            Binding const* target = &item->target;
            char const* start = shorthand ? (span_valid(target->content_span) ? target->content_span.buf : nullptr) :
                (span_valid(item->source_span) ? item->source_span.buf : nullptr);
            if (map && start && target->rest) {
                char const* text = cstr_str(&map->contents);
                char const* c = decl_map_back_trivia(start, text);
                if (c - text >= 3 && !memcmp(c - 3, "...", 3)) start = c - 3;
            }
            pattern_element_comments(out, start);
            mark_in(out, map, start);
            if (!shorthand) {
                mark_span_in(out, map, item->source_span, false);
                Span key = item->computed_key ? expr_outer_span(item->computed_key) : span_null;
                if (map && span_valid(key) && span_valid(item->source_span) && key.buf >= item->source_span.buf &&
                    span_end_ptr(key) <= span_end_ptr(item->source_span)
                ) {
                    // (A computed name's expression is a node of its own: `['_$litType$']: type`)
                    cstr_append_n(out, item->source_span.buf, key.buf - item->source_span.buf);
                    mark_span_in(out, map, key, false);
                    cstr_append_sv(out, span_sv(key));
                    mark_span_in(out, map, key, true);
                    cstr_append_n(out, span_end_ptr(key), span_end_ptr(item->source_span) - span_end_ptr(key));
                } else {
                    cstr_append_sv(out, span_sv(item->source_span));
                }
                mark_span_in(out, map, item->source_span, true);
                cstr_append_sv(out, c_sv(": "));
            }
            append_binding_pattern_mapped(out, target, map);
            mark_span_in(out, map, target->default_value ? expr_outer_span(target->default_value) : target->content_span, true);
        }
        if (pattern_has_trailing_comma(binding)) cstr_append_sv(out, c_sv(","));
        cstr_append_sv(out, c_sv(" }"));
        mark_span_in(out, map, binding->content_span, true);
    } else {
        mark_span_in(out, map, binding->content_span, false);
        cstr_append_sv(out, span_sv(binding->content_span));
        mark_span_in(out, map, binding->content_span, true);
    }
}

static void append_binding_pattern(cstr* out, Binding const* binding) { append_binding_pattern_mapped(out, binding, nullptr); }

static MappedInfo const* mapped_root(MappedInfo const* mapped);
static void display_mapped_body(Checker* me, TyDisplay d, MappedInfo const* mapped, Ty const* wrapper, bool keyof_wrapper);

// How an instantiated mapped type's root was instantiated to make it (tsc's type.mapper)
static TyMapper const* mapped_mapper_from_root(Checker* me, MappedInfo const* mapped) {
    if (!mapped->template_from) return nullptr;
    TyMapper const* inner = mapped_mapper_from_root(me, mapped->template_from);
    return inner ? ty_compose_mappers(me, inner, mapped->template_mapper) : mapped->template_mapper;
}

// Whether display_mapped() writes the mapped type inside a conditional type (see there)
static bool mapped_needs_wrapper(Checker* me, TyDisplay d, MappedInfo const* mapped, bool* homomorphic_lost) {
    *homomorphic_lost = false;
    if (!d.param_names || !mapped->modifiers_type || mapped->modifiers_type == ty_intrinsic(me, TY_UNKNOWN_)) return false;
    MappedInfo const* root = mapped_root(mapped);
    if (mapped->keyof_constraint) {
        // (Homomorphic as tsc's getHomomorphicTypeVariable says: its constraint is `keyof` a type parameter, which
        // `keyof` of a generic mapped type is too, `keyof Required<L>` being `keyof L`)
        // (A substitution type is its type variable: getActualTypeVariable, `X<S>` in the true branch of `S extends C`)
        Ty const* constraint = mapped->constraint;
        bool homomorphic = constraint->kind == TY_KEYOF &&
            ty_actual_type_variable(me, ty_non_distributed(constraint->keyof.operand))->kind == TY_TYPE_PARAM;
        *homomorphic_lost = root != mapped && root->modifiers_type && root->modifiers_type->kind == TY_TYPE_PARAM &&
            ty_actual_type_variable(me, mapped->modifiers_type)->kind != TY_TYPE_PARAM && !homomorphic;
    }
    bool preserving = !mapped->keyof_constraint && !(mapped->constraint->kind == TY_TYPE_PARAM &&
        ty_param_constraint(me, ty_non_distributed(mapped->constraint)) &&
        ty_param_constraint(me, ty_non_distributed(mapped->constraint))->kind == TY_KEYOF);
    return *homomorphic_lost || preserving;
}

// A mapped type written inside a conditional type: parenthesized like one
static bool mapped_as_conditional(Checker* me, TyDisplay d, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || shows_alias(d, ty)) return false;
    // (Inside itself it's a name or the cyclic placeholder: no parentheses)
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) return false;
    }
    Members const* members = ty_members(me, ty);
    bool homomorphic_lost;
    return members->mapped && mapped_needs_wrapper(me, d, members->mapped, &homomorphic_lost);
}

static void display_mapped(Checker* me, TyDisplay d, MappedInfo const* mapped) {
    // With GenerateNamesForShadowedTypeParams (the types baseline), tsc's createMappedTypeNodeFromType keeps what makes
    // the mapped type homomorphic or modifier preserving when inlining wouldn't: a homomorphic `[K in keyof T]` whose T
    // is no longer a type parameter is written over a new T_1 inferred from it (`X extends infer T_1 ? { [K in keyof
    // T_1]: ... } : never`), a `[P in K]` with `K extends keyof T` whose K is no longer a type parameter over a new T_1
    // inferred from it and constrained to `keyof T`
    Ty const* wrapper = nullptr;
    bool homomorphic_lost = false;
    if (mapped_needs_wrapper(me, d, mapped, &homomorphic_lost)) {
        wrapper = ty_synthetic_type_param(me, c_sv("T"));
        type_param_name(me, d, wrapper);
    }
    TyDisplay fresh = reused_if(d, false);
    if (wrapper) {
        // (Written after the mapped type, but named before it)
        cstr inner = cstr_null;
        TyDisplay inner_d = d;
        inner_d.target = &inner;
        display_mapped_body(me, inner_d, mapped, wrapper, homomorphic_lost);
        // (The check type of the conditional it prints as: parenthesized if it is one, tsc's
        // parenthesizeCheckTypeOfConditionalType)
        Ty const* check = homomorphic_lost ? mapped->modifiers_type : mapped->constraint;
        display_parenthesized_if(me, d, check, !shows_alias(d, check) && (is_function_like(me, d, check) ||
            (check->kind == TY_CONDITIONAL && !display_over(d) && !conditional_elided(d, check)) ||
            mapped_as_conditional(me, d, check)));
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
        append_printed(d, &inner);
        out(fresh, c_sv(" : never"));
        return;
    }
    display_mapped_body(me, d, mapped, nullptr, false);
}

static void display_mapped_body(Checker* me, TyDisplay d, MappedInfo const* mapped, Ty const* wrapper, bool keyof_wrapper) {
    // The constraint is printed before the mapped type's scope is entered, its type parameter and the rest inside it
    // (tsgo's createMappedTypeNodeFromType: enterNewScope, so names given inside are given again after it, `{ [K in
    // keyof P]: ... }` twice in a conditional's branches)
    cstr constraint_text = cstr_null;
    TyDisplay cd = d;
    cd.target = &constraint_text;
    d.fake_scope = true; // (Its type parameter's scope, after the constraint)
    if (wrapper) {
        TyDisplay fresh = reused_if(cd, false);
        if (keyof_wrapper) out(fresh, c_sv("keyof "));
        out_type_param_name(me, fresh, wrapper, false);
    } else if (mapped->keyof_constraint && mapped->modifiers_type) {
        // (`keyof` and the modifiers type: the constraint itself may have distributed `keyof` over an intersection)
        out_lit(cd, "keyof ");
        display_parenthesized_if(me, cd, mapped->modifiers_type, needs_parens_in_array(me, cd, mapped->modifiers_type));
    } else {
        ty_display(me, cd, mapped->constraint);
    }
    // Inside a mapped type with the same type parameter (an instantiation nested in another, `Compute<A[K]>`), it is
    // another one: tsgo's instantiateMappedType clones the type parameter for every instantiation, so the inner one is
    // named anew (`K_1`). tttt's instantiations share it: a clone for printing, the template and name type over it
    Ty const* type_param = mapped->type_param;
    TyMapper const* fresh_mapper = nullptr;
    if (d.param_names && type_param->kind == TY_TYPE_PARAM) {
        bool named = false;
        for (u32 i = 0; i < d.param_names->len && !named; i++) named = d.param_names->given[i].param == type_param;
        if (named) {
            Ty const* clone = ty_clone_type_param(me, type_param, nullptr, nullptr);
            fresh_mapper = ty_mapper_new(me, (TyList){ .data = &type_param, .len = 1 }, (TyList){ .data = &clone, .len = 1 });
            type_param = clone;
        }
    }
    u32 saved = param_names_enter(d);
    // (The node builder's mapped type nodes are single line, EmitFlags.SingleLine: the node itself, not the types in
    // it, which print as they would at its indentation)
    TyDisplay inner_d = d;
    d.multiline = false;
    TyDisplay outer = d;
    d = brace_open(d);
    member_begin(d);
    if (mapped->readonly) out(d, mapped->readonly > 0 ? c_sv("readonly ") : c_sv("-readonly "));
    out_lit(d, "[");
    TyDisplay name_d = reused_if(d, !fresh_mapper);
    name_d.approx = nullptr; // (tsc's typeParameterToDeclarationWithConstraint doesn't count the name)
    name_d.param_declaration = true; // (Declaration maps: typeParameterToName gives it its declaration's range)
    ty_display(me, name_d, type_param); // (tsc reuses the name of the declared type parameter)
    out_lit(d, " in ");
    append_printed(d, &constraint_text);
    if (mapped->name_type) {
        out_lit(d, " as ");
        ty_display(me, inner_d, fresh_mapper ? ty_instantiate(me, mapped->name_type, fresh_mapper) : mapped->name_type);
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
        Ty const* targets[2] = { type_param, wrapper };
        template = ty_instantiate(me, ty_mapped_template(me, root),
            ty_mapper_new(me, (TyList){ .data = sources, .len = 2 }, (TyList){ .data = targets, .len = 2 }));
    } else if (fresh_mapper) {
        template = ty_instantiate(me, template, fresh_mapper);
    }
    if (mapped->optional > 0 && compiler_options()->strict_null_checks && !compiler_options()->exact_optional_property_types) {
        Ty const* parts[2] = { template, ty_intrinsic(me, TY_UNDEFINED) };
        template = ty_union(me, parts, 2, nullptr, (TyList){0});
    }
    ty_display(me, inner_d, template);
    member_end(d);
    brace_close(outer);
    approx_add(d, 10); // (tsc's createMappedTypeNodeFromType, after its parts)
    param_names_leave(d, saved);
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
        Symbol const* found = scope_resolve(d.enclosing, span_sv(leftmost->inner_span), SYMBOL_VALUE);
        if (!found || !same_symbol(me, found, leftmost->identifier.symbol)) return false;
    }
    return true;
}

// What the first identifier of a late bound property's computed name `name` resolves to where it is written (tsc's
// trackComputedName's fallback, resolveName at the identifier): the name's own resolution, else (a copy of the
// property, a spread's) a lookup in the scope of the declaration of its parent (a class, an interface) that contains
// it. Null: not known
static Symbol const* computed_name_declared(Prop const* prop, Span name) {
    Expr const* late = prop_decl_rare(prop->decl)->late_name;
    if (!late && prop->decl->item) late = prop->decl->item->computed_key;
    if (late) {
        Expr const* leftmost = entity_leftmost(late);
        return leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword ? leftmost->identifier.symbol : nullptr;
    }
    Symbol const* parent = prop->parent;
    for (u32 i = 0; parent && i < parent->decl_count; i++) {
        SymbolDecl const* decl = &parent->decls[i];
        if (!decl->block || !span_contains_span(decl->full, name)) continue;
        char const* c = name.buf + 1;
        char const* end = name.buf + name.size;
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++;
        char const* start = c;
        while (c < end && *c != '.' && *c != ']' && *c != ' ' && *c != '[' && *c != '(') c++;
        return c == start ? nullptr : scope_resolve(decl->block, csview_from_n(start, c - start), SYMBOL_VALUE);
    }
    return nullptr;
}

// Declaration emit's checks of a property it writes (createTypeNodesFromResolvedType, addPropertyToElementList).
// Whether it is skipped
static bool track_property(Checker* me, TyDisplay d, Prop const* prop) {
    // A class's `prototype` isn't written; its private and protected members can't be in a type literal
    if (prop->flags & PROP_PROTOTYPE) return true;
    // (A `#private` one too: tsc's isPrivateIdentifierSymbol report in createTypeNodesFromResolvedType)
    if ((prop->flags & (PROP_PRIVATE | PROP_PROTECTED)) || ty_private_name_class(me, prop->name)) {
        d.tracker->report(d.tracker, DECL_REPORT_PRIVATE_IN_CLASS_LITERAL, atom_sv(prop->name));
    }
    if (!ty_unique_symbol_of_name(me, prop->name)) {
        // A name that is an enum member's value is written as that member where the enum is accessible (see
        // display_member_name()), which tracks the enum (getPropertyNameNodeForSymbolFromNameType's symbolToExpression)
        Prop const* declared = prop;
        while (declared->target && !declared->decl->decl_span_count && !prop_decl_rare(declared->decl)->name_type) declared = declared->target;
        Ty const* name_type = prop_decl_rare(declared->decl)->name_type;
        Symbol const* enum_symbol = name_type && (name_type->flags & TYF_ENUM_LITERAL) && name_type->kind == TY_LITERAL ?
            name_type->literal.enum_decl : nullptr;
        if (enum_symbol && d.enclosing && symbol_accessible_without_modules_at(d.enclosing, enum_symbol, SYMBOL_VALUE)) {
            d.tracker->track_symbol(d.tracker, enum_symbol, SYMBOL_VALUE);
        }
        return false;
    }
    // A late bound name: the declaration's computed name, else it can't be written
    // (A mapped type's property is declared where the property it comes from is, see mapped_members())
    Prop const* declared = prop;
    while (declared->target && !span_valid(declared->decl->name_span) && !declared->decl->decl_span_count) {
        declared = declared->target;
    }
    Span name = declared->decl->name_span;
    if (span_valid(name) && name.size && name.buf[0] == '[') {
        d.tracker->track_computed_name(d.tracker, span_sv(name), span_source(name), computed_name_declared(declared, name));
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
// inside itself, inside one whose source property's type isn't an anonymous object type, and three deep with one of the
// last four from the same mapped type
static bool reverse_placeholder(TyDisplay d, Prop const* prop) {
    // (For display, not declaration emit: tsc's NodeBuilderFlags.AllowAnonymousIdentifier, which IgnoreErrors has)
    if (!d.reverse_stack || d.tracker) return false;
    for (TyDisplayReverse const* i = d.reverse_stack; i; i = i->outer) {
        if (i->prop == prop) return true;
    }
    if (prop_decl_rare(d.reverse_stack->prop->decl)->reverse_source->kind != TY_ANONYMOUS) return true;
    // (3: with at least three on the stack, one of the last four from the same mapped type; tsc's
    // MAX_REVERSE_MAPPED_NESTING_INSPECTION_DEPTH)
    u32 len = 0;
    for (TyDisplayReverse const* i = d.reverse_stack; i; i = i->outer) len++;
    if (len < 3) return false;
    MappedInfo const* root = mapped_root(prop_decl_rare(prop->decl)->reverse_of->mapped);
    u32 depth = 0;
    for (TyDisplayReverse const* i = d.reverse_stack; i && depth <= 3; i = i->outer, depth++) {
        if (mapped_root(prop_decl_rare(i->prop->decl)->reverse_of->mapped) == root) return true;
    }
    return false;
}

// Whether a type literal is the annotation of the declaration whose name is at `at`: only `?` and `:` between them
static bool annotates_name_at(Span at, Span literal) {
    if (!span_valid(at) || !span_valid(literal) || span_source(at) != span_source(literal)) return false;
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

// What tsc's addPropertyToElementList counts for a property's name: its symbol's name, which for a unique symbol's is
// "\xFE@name@" and the symbol's id
static u32 prop_symbol_name_length(Checker* me, Atom const* name) {
    Ty const* unique = ty_unique_symbol_of_name(me, name);
    if (!unique) return (u32)atom_sv(name).size;
    UniqueSymbolDecl const* decl = unique->literal.unique;
    u32 own = span_valid(decl->member) ? (u32)decl->member.size : decl->owner ? (u32)ty_symbol_display_name(decl->owner).size : 0;
    // (tsgo's escaped name of a unique symbol, getESSymbolLikeTypeForNode: "\xFE@" + name + "@" + the symbol's id,
    // which is BlockIdOffset (2^48) plus a small count, 15 digits)
    return 2 + own + 1 + 15;
}

// tsgo's pseudo types (pseudo.h): where a declaration's type is printed (serializeTypeForDeclaration and
// serializeReturnTypeForSignature), its pseudo type is printed instead of its type when it stands for the same type
// (pseudoTypeEquivalentToType), built from the declaration's nodes (pseudoTypeToNode)

// pseudoTypeToType: the type a pseudo type stands for where that's direct, null for the structures
static Ty const* pseudo_type(Checker* me, TyDisplay d, Pseudo const* pseudo) {
    bool strict = compiler_options()->strict_null_checks;
    switch (pseudo->kind) {
        case PSEUDO_DIRECT: return pseudo->predicate ? ty_intrinsic(me, TY_BOOLEAN) : node_type(me, d, pseudo->node);
        case PSEUDO_INFERRED:
            if (pseudo->signature_return) {
                Signature const* sig = ty_function_signature(me, pseudo->function);
                return sig ? signature_return_type(me, sig) : nullptr;
            }
            {
                // (getWidenedType of getRegularTypeOfExpression, which checks the expression again: an object literal in
                // it is a new type each time, never the declaration's)
                // A union is still compared by identity, structurally (`c: cond ? { a: 1, b: "" } : { a: 2 }`, whose
                // widened union has `b?: undefined` in the second member, is equivalent to the widened literal's `c`)
                Ty const* type = ty_expr_type(me, pseudo->expr);
                Ty const* widened = ty_widened_nullable(me, ty_widened_objects(me, ty_regular(me, type)));
                if ((type->flags & TYF_FRESH_OBJECT) && !(type->kind == TY_UNION && widened->kind == TY_UNION)) return nullptr;
                return widened;
            }
        case PSEUDO_NO_RESULT: return nullptr;
        case PSEUDO_MAYBE_CONST: return pseudo_type(me, d, pseudo->in_const ? pseudo->const_type : pseudo->regular_type);
        case PSEUDO_UNION: {
            Ty const** types = arena_new_n(&my arena, Ty const*, pseudo->count ? pseudo->count : 1);
            u32 count = 0;
            bool elided = false;
            for (u32 i = 0; i < pseudo->count; i++) {
                Pseudo const* member = pseudo->types[i];
                if (!strict && (member->kind == PSEUDO_UNDEFINED || member->kind == PSEUDO_NULL)) {
                    elided = true;
                    continue;
                }
                Ty const* type = pseudo_type(me, d, member);
                if (!type) return nullptr;
                types[count++] = type;
            }
            if (count == 1) return types[0];
            if (!count) return ty_intrinsic(me, elided ? TY_ANY : TY_NEVER);
            return ty_union(me, types, count, nullptr, (TyList){0});
        }
        case PSEUDO_UNDEFINED: return ty_intrinsic(me, strict ? TY_UNDEFINED : TY_UNDEFINED_WIDENING);
        case PSEUDO_NULL: return ty_intrinsic(me, strict ? TY_NULL : TY_NULL_WIDENING);
        case PSEUDO_ANY: return ty_intrinsic(me, TY_ANY);
        case PSEUDO_STRING: return ty_intrinsic(me, TY_STRING);
        case PSEUDO_NUMBER: return ty_intrinsic(me, TY_NUMBER);
        case PSEUDO_BIGINT: return ty_intrinsic(me, TY_BIGINT);
        case PSEUDO_BOOLEAN: return ty_intrinsic(me, TY_BOOLEAN);
        case PSEUDO_FALSE: return ty_intrinsic(me, TY_FALSE);
        case PSEUDO_TRUE: return ty_intrinsic(me, TY_TRUE);
        case PSEUDO_STRING_LITERAL:
        case PSEUDO_NUMERIC_LITERAL:
        case PSEUDO_BIGINT_LITERAL:
            return ty_regular(me, ty_expr_type(me, pseudo->expr));
        case PSEUDO_SIGNATURE:
        case PSEUDO_TUPLE:
        case PSEUDO_OBJECT:
            return nullptr;
    }
    impossible;
}

static bool pseudo_equivalent(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty, bool optional, PseudoReporter const* rep);

static bool pseudo_is_this_param(PseudoParam const* param) {
    Binding const* binding = &param->arg->initializer.binding;
    return binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this");
}

// An instantiated signature's own type parameters are fresh ones in tsgo (instantiateSignature): an annotation that
// names one doesn't say the instantiated type
static TyDisplay pseudo_enter_signature(Checker* me, TyDisplay d, Signature const* sig) {
    Signature const* root = sig;
    while (sig_target(me, root)) root = sig_target(me, root);
    if (root == sig || !sig_res(me, root)->type_params.len) return d;
    Ty const** both = arena_new_n(&my arena, Ty const*, d.fresh_params.len + sig_res(me, root)->type_params.len);
    if (d.fresh_params.len) memcpy(both, d.fresh_params.data, sizeof(Ty const*) * d.fresh_params.len);
    memcpy(both + d.fresh_params.len, sig_res(me, root)->type_params.data, sizeof(Ty const*) * sig_res(me, root)->type_params.len);
    d.fresh_params = (TyList){ .data = both, .len = d.fresh_params.len + sig_res(me, root)->type_params.len };
    return d;
}

// ReportInferenceFallback where equivalence reports (reportErrors)
static void pseudo_report(PseudoReporter const* rep, PseudoError error) {
    if (rep) rep->report(rep->ctx, &error);
}

static void pseudo_report_item(PseudoReporter const* rep, ObjectExprItem const* item) {
    pseudo_report(rep, (PseudoError){ .kind = PSEUDO_ERROR_OBJECT_MEMBER, .item = item });
}

static void tracker_report_fallback(void* ctx, PseudoError const* node) {
    DeclTracker* tracker = ctx;
    tracker->report_fallback(tracker, node);
}

// ... whether or not the type being printed is the checker's fallback (pseudoTypeToNode's reports don't look)
static PseudoReporter const* display_reporter_unsuppressed(TyDisplay d, PseudoReporter* storage) {
    if (!d.tracker || !d.tracker->report_fallback) return nullptr;
    *storage = (PseudoReporter){ .report = tracker_report_fallback, .ctx = d.tracker };
    return storage;
}

// Where the printer reports for declaration emit (tsgo's tracker's ReportInferenceFallback), null where it doesn't
static PseudoReporter const* display_reporter(TyDisplay d, PseudoReporter* storage) {
    if (d.suppress_fallback) return nullptr;
    return display_reporter_unsuppressed(d, storage);
}

// pseudoParametersEquivalentToParameters. `where`: what is reported when the lists don't match (tsgo's
// nonParamErrorLocation), `function`: whose parameters they are
static bool pseudo_params_equivalent(Checker* me, TyDisplay d, PseudoParam const* params, u32 count, Signature const* sig,
    PseudoReporter const* rep, PseudoError where, FunctionImpl const* function
) {
    SigResolved const* sig_r = sig_res(me, sig);
    if (sig_r->this_type) {
        if (!count || !pseudo_is_this_param(&params[0])) {
            pseudo_report(rep, where); // (a missing `this` parameter)
            return false;
        }
        if (!pseudo_equivalent(me, d, params[0].type, sig_r->this_type, params[0].optional, nullptr)) {
            pseudo_report(rep, (PseudoError){ .kind = PSEUDO_ERROR_PARAMETER, .function = function, .arg = params[0].arg });
            return false;
        }
        params++;
        count--;
    }
    if (count != sig_r->param_count) {
        pseudo_report(rep, where);
        return false;
    }
    for (u32 i = 0; i < count; i++) {
        if (params[i].optional != sig_r->params[i].optional ||
            !pseudo_equivalent(me, d, params[i].type, sig_r->params[i].type, params[i].optional, nullptr)
        ) {
            pseudo_report(rep, (PseudoError){ .kind = PSEUDO_ERROR_PARAMETER, .function = function, .arg = params[i].arg });
            return false;
        }
    }
    return true;
}

// pseudoReturnTypeMatchesPredicate
static bool pseudo_matches_predicate(Checker* me, TyDisplay d, Pseudo const* ret, TyPredicate const* predicate) {
    if (ret->kind != PSEUDO_DIRECT || !ret->predicate) return false;
    TypePredicateNode const* node = ret->function->predicate;
    if (node->asserts != predicate->asserts || node->is_this != predicate->is_this) return false;
    if (!node->is_this && !csview_equals_sv(span_sv(node->name), atom_sv(predicate->param_name))) return false;
    if (predicate->type) return node->has_type && node_type(me, d, &node->type) == predicate->type;
    return !node->has_type;
}

static bool prop_declared_at(Prop const* prop, ObjectExprItem const* item) {
    while (prop->target && !prop->decl->decl_span_count) prop = prop->target;
    for (u32 j = 0; j < prop->decl->decl_span_count; j++) {
        if (span_eq(prop->decl->decl_spans[j], item->span)) return true;
    }
    return false;
}

// The property of an object type that an object literal's member declares, by name first like tsgo's
// pseudoTypeEquivalentToType (getPropertyOfType of the member's symbol's name: a type the literal didn't declare matches
// by its properties' names, `location: { line: 1 }` where a spread made it `Loc`), then by declaration (a computed
// name). `hint`: where it usually is (the members are mostly in the literal's order: the scan alone is quadratic in a
// big literal)
static Prop const* pseudo_member_prop(Members const* members, ObjectExprItem const* item, u32 hint) {
    csview name = item->computed_key ? (csview){0} : cstr_sv(&item->name_string);
    if (name.size) {
        if (hint < members->prop_count && csview_equals_sv(atom_sv(members->props[hint]->name), name)) return members->props[hint];
        for (u32 i = 0; i < members->prop_count; i++) {
            if (csview_equals_sv(atom_sv(members->props[i]->name), name)) return members->props[i];
        }
    }
    if (hint < members->prop_count && prop_declared_at(members->props[hint], item)) return members->props[hint];
    for (u32 i = 0; i < members->prop_count; i++) {
        if (prop_declared_at(members->props[i], item)) return members->props[i];
    }
    return nullptr;
}

static u32 prop_declaration_count(Prop const* prop) {
    while (prop->target && !prop->decl->decl_span_count) prop = prop->target;
    return prop->decl->decl_span_count;
}

// An object literal's member against its property (`type` without the undefined its optionality adds)
static bool pseudo_member_equivalent(Checker* me, TyDisplay d, PseudoMember const* member, Prop const* prop, Ty const* type,
    PseudoReporter const* rep
) {
    switch (member->kind) {
        case PSEUDO_MEMBER_PROPERTY:
            if (pseudo_equivalent(me, d, member->type, type, member->optional, nullptr)) return true;
            if (member->type->kind == PSEUDO_INFERRED && member->type->count) {
                pseudo_report_inferred(rep, member->type); // (its error nodes)
            } else if (!pseudo_is_structural(member->type)) {
                pseudo_report_item(rep, member->item);
            }
            return false;
        case PSEUDO_MEMBER_METHOD: {
            // (An optional method's signature is its type's without undefined)
            Signature const* sig = ty_single_signature(me, member->optional ? ty_without_undefined(me, type) : type, false, false);
            if (!sig) return true; // (Not a single call signature: nothing to compare with)
            signature_resolve(me, sig);
            TyDisplay sig_d = pseudo_enter_signature(me, d, sig);
            PseudoError where = { .kind = PSEUDO_ERROR_OBJECT_MEMBER, .item = member->item };
            if (!pseudo_params_equivalent(me, sig_d, member->params, member->param_count, sig, rep, where, member->function)) {
                return false;
            }
            TyPredicate const* predicate = signature_predicate(me, sig);
            bool same = predicate ? pseudo_matches_predicate(me, sig_d, member->type, predicate) :
                pseudo_equivalent(me, sig_d, member->type, signature_return_type(me, sig), false, nullptr);
            if (!same) pseudo_report_item(rep, member->item);
            return same;
        }
        case PSEUDO_MEMBER_GETTER:
            if (pseudo_equivalent(me, d, member->type, type, false, nullptr)) return true;
            pseudo_report_item(rep, member->item);
            return false;
        case PSEUDO_MEMBER_SETTER: {
            Ty const* write = prop_write_type(me, prop);
            if (member->param_count && pseudo_equivalent(me, d, member->params[0].type, write ? write : type, false, nullptr)) {
                return true;
            }
            pseudo_report_item(rep, member->item);
            return false;
        }
    }
    impossible;
}

// pseudoTypeEquivalentToType. `optional`: the declaration is optional (its type's undefined isn't in the pseudo type).
// `rep`: null, or where to report what makes it not equivalent (reportErrors)
static bool pseudo_equivalent(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty, bool optional, PseudoReporter const* rep) {
    if (!ty) return false;
    // (An error type is taken to be whatever the syntax says)
    if (ty->flags & TYF_ERROR) return true;
    Ty const* from_pseudo = pseudo_type(me, d, pseudo);
    bool fresh = from_pseudo && d.fresh_params.len && mentions(me, from_pseudo, d.fresh_params);
    if (from_pseudo == ty && !fresh) return true;
    Ty const* stripped = optional ? ty_without_undefined(me, ty) : ty;
    if (from_pseudo && !fresh) {
        if (optional && stripped == from_pseudo) return true;
        if (ty_regular(me, from_pseudo) == ty_regular(me, ty)) return true;
        Ty const* target = optional ? stripped : ty;
        if (from_pseudo->kind == TY_UNION && target->kind == TY_UNION &&
            ty_is_related(me, from_pseudo, target, TY_RELATION_IDENTITY)
        ) {
            return true;
        }
    }
    switch (pseudo->kind) {
        case PSEUDO_INFERRED:
            pseudo_report_inferred(rep, pseudo);
            return false;
        case PSEUDO_NO_RESULT:
            pseudo_report(rep, pseudo_no_result_node(pseudo));
            return false;
        case PSEUDO_OBJECT: {
            // (Compared by properties: an empty object literal says any type without properties, `undefined`, `never`,
            // `any`, tsc's getPropertiesOfType being empty for them; the `?: undefined` member a union of object literals
            // adds next to a `p: {}` is declared by it, and prints `p?: {}`)
            if (!(stripped->flags & TYF_OBJECT)) {
                return !pseudo->count && (stripped->flags & (TYF_UNDEFINED | TYF_NULL | TYF_NEVER | TYF_VOID | TYF_ANY | TYF_UNKNOWN));
            }
            Members const* members = ty_members(me, stripped);
            u32 declarations = 0;
            for (u32 i = 0; i < members->prop_count; i++) declarations += prop_declaration_count(members->props[i]);
            if (declarations != pseudo->count) return false;
            for (u32 i = 0; i < pseudo->count; i++) {
                PseudoMember const* member = &pseudo->members[i];
                Prop const* prop = pseudo_member_prop(members, member->item, i);
                bool prop_optional = prop && (prop->flags & PROP_OPTIONAL) != 0;
                if (!prop || member->optional != prop_optional) {
                    pseudo_report_item(rep, member->item);
                    return false;
                }
                Ty const* type = ty_remove_missing(me, prop_type(me, prop), prop_optional);
                if (!pseudo_member_equivalent(me, d, member, prop, type, rep)) return false;
            }
            return true;
        }
        case PSEUDO_TUPLE: {
            // (An empty tuple is its target itself, no reference: `readonly []` of an `as const` `[]`)
            if (stripped->kind == TY_DECLARED && stripped->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                return !pseudo->count && !stripped->declared.tuple->arity;
            }
            if (stripped->kind != TY_REFERENCE || stripped->reference.target->declared.kind != TY_DECLARED_TUPLE_TARGET) return false;
            TupleShape const* shape = stripped->reference.target->declared.tuple;
            // (From an `as const` array literal: only required elements)
            for (u32 i = 0; i < shape->arity; i++) {
                if (shape->flags[i]) return false;
            }
            TyList args = ty_reference_args(me, stripped);
            if (shape->arity != pseudo->count || args.len < shape->arity) return false;
            for (u32 i = 0; i < pseudo->count; i++) {
                if (!pseudo_equivalent(me, d, pseudo->types[i], args.data[i], false, rep)) return false;
            }
            return true;
        }
        case PSEUDO_SIGNATURE: {
            Signature const* sig = ty_single_signature(me, stripped, false, false);
            if (!sig) return false;
            SigResolved const* sig_rs = signature_resolve(me, sig);
            PseudoError where = { .kind = PSEUDO_ERROR_FUNCTION, .function = pseudo->function };
            if (sig_rs->type_params.len != (u32)vec_generic_item_size(&pseudo->function->generic.items)) {
                pseudo_report(rep, where);
                return false;
            }
            TyDisplay sig_d = pseudo_enter_signature(me, d, sig);
            if (!pseudo_params_equivalent(me, sig_d, pseudo->params, pseudo->count, sig, rep, where, pseudo->function)) return false;
            TyPredicate const* predicate = signature_predicate(me, sig);
            if (predicate) {
                if (pseudo_matches_predicate(me, sig_d, pseudo->ret, predicate)) return true;
                pseudo_report(rep, where);
                return false;
            }
            // (What makes the return type differ is reported in it)
            return pseudo_equivalent(me, sig_d, pseudo->ret, signature_return_type(me, sig), false, rep);
        }
        default: return false;
    }
}

Ty const* ty_pseudo_type(Checker* me, Pseudo const* pseudo) { return pseudo_type(me, (TyDisplay){0}, pseudo); }

bool ty_pseudo_equivalent(Checker* me, Pseudo const* pseudo, Ty const* ty, bool optional, PseudoReporter const* reporter) {
    return pseudo_equivalent(me, (TyDisplay){0}, pseudo, ty, optional, reporter);
}

static void display_pseudo(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty);

// pseudoTypeToNodeWithCheckerFallback: an Inferred pseudo type reports, and prints the checker's type without reporting
// in it
static void display_pseudo_with_fallback(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty) {
    if (pseudo->kind == PSEUDO_INFERRED) {
        PseudoReporter storage;
        pseudo_report_inferred(display_reporter(d, &storage), pseudo);
        d.suppress_fallback = true;
        if (ty) ty_display(me, d, ty);
        else out_lit(d, "any");
        return;
    }
    display_pseudo(me, d, pseudo, ty);
}

// What tsgo's approximateLength counts for a node reused from the source (tryReuseExistingNodeHelper): its range, with
// the whitespace before it
static u32 reused_node_length(Span span) {
    char const* start = span.buf;
    SourceFile const* file = span_source(span);
    char const* contents = file ? cstr_str(&file->contents) : start;
    while (start > contents && (start[-1] == ' ' || start[-1] == '\t' || start[-1] == '\n' || start[-1] == '\r')) start--;
    return (u32)(span.buf - start + span.size);
}

// display_pseudo() of a pseudo type whose parent is a declaration (`property`: that object literal member's, `function`:
// that arrow function's body's): a name there reports the declaration. `in_return`: a function's return type (printed
// by serializeInferredReturnTypeForSignature, which doesn't report)
static void display_pseudo_in(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty, ObjectExprItem const* property,
    FunctionImpl const* function, bool in_return
) {
    if (pseudo->kind != PSEUDO_INFERRED) {
        display_pseudo(me, d, pseudo, ty);
        return;
    }
    PseudoReporter storage;
    pseudo_report_inferred_node(display_reporter_unsuppressed(d, &storage), pseudo, property, function);
    if (in_return || pseudo->signature_return) d.suppress_fallback = true;
    if (ty) ty_display(me, d, ty);
    else out_lit(d, "any");
}

// An object literal member's name: a computed one as written (tsgo's reuseName), else as the property prints
static void display_pseudo_member_name(Checker* me, TyDisplay d, Prop const* prop, ObjectExprItem const* item) {
    if (d.multiline && item && span_valid(item->name_span) && item->name_span.buf[0] == '[') {
        // (A literal's is renamed by its text: an identifier, a non-negative number, else a string, tsgo's reuseName
        // with classifyPropertyName)
        Expr const* key = item->computed_key;
        if (key && key->tag == EXPR_LITERAL && (key->literal.tag == VALUE_STRING || key->literal.tag == VALUE_NUMBER)) {
            csview text = atom_sv(prop->name);
            bool method_new = item->tag == OBJECT_ITEM_METHOD && csview_equals(text, "new");
            map_span(d, item->name_span, false); // (The renamed node has the computed name's range)
            if (!method_new && (is_identifier_name(text) && text.buf[0] != '#')) {
                out(d, text);
            } else if (!method_new && is_numeric_name(text)) {
                // (Kept: a number's printed by its value)
                if (key->literal.tag == VALUE_NUMBER) {
                    out_lit(d, "[");
                    out(d, text);
                    out_lit(d, "]");
                } else {
                    out(d, span_sv(item->name_span));
                }
            } else {
                display_quoted_string(d, text, false, true);
            }
            map_span(d, item->name_span, true);
            return;
        }
        out(d, span_sv(item->name_span));
        return;
    }
    isize_t at = cstr_size(d.target);
    display_member_name(me, d, prop);
    // (Declaration maps: a name printed as written is the node, tsgo's reuseName)
    if (D_MAP(d) && item && span_valid(item->name_span) &&
        csview_equals_sv(csview_slice(cstr_sv(d.target), at, cstr_size(d.target)), span_sv(item->name_span))
    ) {
        cstr_resize(d.target, at, '\0');
        out_mapped(d, item->name_span);
    } else if (D_MAP(d) && item && span_valid(item->name_span) && item->name_span.size &&
        (item->name_span.buf[0] == '"' || item->name_span.buf[0] == '\'')
    ) {
        // (A string literal name printed otherwise, `'a'` as `a`: the renamed node keeps the name's range)
        cstr printed = cstr_from_sv(csview_slice(cstr_sv(d.target), at, cstr_size(d.target)));
        cstr_resize(d.target, at, '\0');
        map_span(d, item->name_span, false);
        out(d, cstr_sv(&printed));
        map_span(d, item->name_span, true);
        cstr_drop(&printed);
    }
}

// Declaration emit: the entity names of a reused binding pattern's computed keys are tracked (painting the imports
// they are)
static void track_pattern_keys(Checker* me, TyDisplay d, Binding const* pattern) {
    if (!d.tracker || !d.enclosing) return;
    if (pattern->tag == BINDING_ARRAY) {
        for_each (i, vec_binding, &pattern->array) track_pattern_keys(me, d, i);
    } else if (pattern->tag == BINDING_OBJECT) {
        for_each (i, vec_object_binding_item, &pattern->object) {
            if (i->computed_key && expr_is_entity_name(i->computed_key)) {
                Expr const* leftmost = entity_leftmost(i->computed_key);
                if (leftmost->tag == EXPR_IDENTIFIER && !leftmost->identifier.keyword) {
                    Symbol const* found = scope_resolve(d.enclosing, span_sv(leftmost->inner_span), SYMBOL_VALUE);
                    if (found) d.tracker->track_symbol(d.tracker, found, SYMBOL_VALUE);
                }
            }
            track_pattern_keys(me, d, &i->target);
        }
    }
}

// A parameter as its declaration says (pseudoParameterToNode): `type` its type in the signature
static void display_pseudo_param(Checker* me, TyDisplay d, PseudoParam const* param, Ty const* type) {
    Binding const* binding = &param->arg->initializer.binding;
    track_pattern_keys(me, d, binding);
    if (param->rest) out(reused_if(d, true), c_sv("..."));
    if (binding->tag == BINDING_ARRAY || binding->tag == BINDING_OBJECT) {
        cstr pattern = cstr_init();
        append_binding_pattern(&pattern, binding);
        out(reused_if(d, true), cstr_sv(&pattern));
        cstr_drop(&pattern);
    } else {
        out(reused_if(d, true), span_sv(binding->content_span));
    }
    if (param->optional) out_lit(d, "?");
    out_lit(d, ": ");
    // (A declaration without a type of its own: its type, with the undefined a `?` adds, tsgo's
    // serializeTypeForDeclaration of the parameter)
    if (param->type->kind == PSEUDO_NO_RESULT && type && param->arg->explicit_optional && compiler_options()->strict_null_checks &&
        ty_without_undefined(me, type) == type
    ) {
        type = ty_union(me, (Ty const*[]){ type, ty_intrinsic(me, TY_UNDEFINED) }, 2, nullptr, (TyList){0});
    }
    display_pseudo(me, d, param->type, type);
}

// The parameters of a signature as declared: `(a: A, b?: B)`
static void display_pseudo_params(Checker* me, TyDisplay d, PseudoParam const* params, u32 count, Signature const* sig) {
    SigResolved const* sig_r = sig_res(me, sig);
    out_lit(d, "(");
    u32 index = 0;
    for (u32 i = 0; i < count; i++) {
        if (i) out_lit(d, ", ");
        Ty const* type = nullptr;
        if (pseudo_is_this_param(&params[i])) {
            type = sig ? sig_r->this_type : nullptr;
        } else {
            type = sig && index < sig_r->param_count ? ty_sig_param_symbol_type(me, &sig_r->params[index]) : nullptr;
            index++;
        }
        param_comments_begin(d, params[i].arg);
        display_pseudo_param(me, d, &params[i], type);
        param_comments_end(d, params[i].arg);
    }
    out_lit(d, ")");
}

// `function`: whose return type it is
static void display_pseudo_return(Checker* me, TyDisplay d, Pseudo const* ret, Signature const* sig, FunctionImpl const* function) {
    if (ret->kind == PSEUDO_DIRECT && ret->predicate) {
        TypePredicateNode const* node = ret->function->predicate;
        TyDisplay reused = reused_if(d, true);
        map_span(d, node->span, false);
        if (node->asserts) {
            out_token_mapped(reused, D_MAP(d) ? node->span.buf : nullptr, c_sv("asserts"));
            out_lit(reused, " ");
        }
        if (node->is_this) out_lit(reused, "this");
        else out_mapped(reused, node->name);
        if (node->has_type) {
            out_lit(reused, " is ");
            display_node(me, d, &node->type);
        }
        map_span(d, node->span, true);
        return;
    }
    // (An arrow function's expression body has the function as its parent)
    FunctionImpl const* parent = function && function->expression_body ? function : nullptr;
    display_pseudo_in(me, d, ret, sig ? signature_return_type(me, sig) : nullptr, nullptr, parent, true);
}

// pseudoTypeToNode. `ty`: what it is equivalent to, printed where the pseudo type says nothing (Inferred, NoResult)
static void display_pseudo(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty) {
    if (d.transformer_node) d.target_start = cstr_size(d.target); // (The length limit is the node builder's)
    d.transformer_node = false;
    bool strict = compiler_options()->strict_null_checks;
    switch (pseudo->kind) {
        case PSEUDO_DIRECT:
            if (pseudo->predicate) {
                // (Declaration emit reuses the predicate node: a setter's parameter typed by its getter's return)
                TypePredicateNode const* node = pseudo->function ? pseudo->function->predicate : nullptr;
                if (d.multiline && node) {
                    TyDisplay reused = reused_if(d, true);
                    map_span(d, node->span, false);
                    if (node->asserts) {
                        out_token_mapped(reused, D_MAP(d) ? node->span.buf : nullptr, c_sv("asserts"));
                        out_lit(reused, " ");
                    }
                    if (node->is_this) out_lit(reused, "this");
                    else out_mapped(reused, node->name);
                    if (node->has_type) {
                        out_lit(reused, " is ");
                        display_node(me, d, &node->type);
                    }
                    map_span(d, node->span, true);
                    return;
                }
                ty_display(me, d, ty_intrinsic(me, TY_BOOLEAN));
                return;
            }
            display_node(me, d, pseudo->node);
            return;
        case PSEUDO_INFERRED:
        case PSEUDO_NO_RESULT: {
            // (What the syntax doesn't say: reported, then the checker's type)
            PseudoReporter storage;
            PseudoReporter const* rep = display_reporter_unsuppressed(d, &storage);
            if (pseudo->kind == PSEUDO_NO_RESULT) pseudo_report(rep, pseudo_no_result_node(pseudo));
            else pseudo_report_inferred_node(rep, pseudo, nullptr, nullptr);
            if (pseudo->kind == PSEUDO_INFERRED && pseudo->signature_return) d.suppress_fallback = true;
            // (A NoResult is printed as its declaration's symbol's type, tsgo's serializeTypeForDeclaration of it: only
            // reached for an error type, which the pseudo type stands for. An object literal member written again is
            // the first one's symbol, typed by that first one: `{ a: x = [], a: unresolved }` prints `a: never[]`)
            if (ty && (ty->flags & TYF_ERROR) && pseudo->kind == PSEUDO_NO_RESULT &&
                pseudo->decl_kind == PSEUDO_DECL_OBJECT_MEMBER && pseudo->item->tag == OBJECT_ITEM_PROPERTY
            ) {
                ty = ty_widened_literal(me, ty_mutable_location_type(me, &pseudo->item->value, nullptr));
            }
            if (ty) ty_display(me, d, ty);
            else out_lit(d, "any");
            return;
        }
        case PSEUDO_MAYBE_CONST:
            display_pseudo(me, d, pseudo->in_const ? pseudo->const_type : pseudo->regular_type, ty);
            return;
        case PSEUDO_UNION: {
            // (A union node's members join the new union's, undefined once)
            u32 printed = 0;
            bool elided = false, has_undefined = false;
            for (u32 i = 0; i < pseudo->count; i++) {
                Pseudo const* member = pseudo->types[i];
                if (!strict && (member->kind == PSEUDO_UNDEFINED || member->kind == PSEUDO_NULL)) {
                    elided = true;
                    continue;
                }
                if (member->kind == PSEUDO_UNDEFINED) {
                    if (has_undefined) continue;
                    has_undefined = true;
                }
                if (member->kind == PSEUDO_DIRECT && !member->predicate && member->node->tag == TYPE_UNION &&
                    !member->node->parenthesized
                ) {
                    for_each (j, vec_type, &member->node->composite) {
                        if (j->placeholder) continue;
                        bool undefined = j->tag == TYPE_VALUE && j->value.tag == VALUE_UNDEFINED;
                        if (undefined && has_undefined) continue;
                        has_undefined |= undefined;
                        if (printed++) out(reused_if(d, false), c_sv(" | "));
                        TyDisplay member_d = d;
                        member_d.constituent_node = true;
                        display_node(me, member_d, j);
                    }
                    continue;
                }
                if (printed++) out(reused_if(d, false), c_sv(" | "));
                TyDisplay member_d = d;
                member_d.constituent_node = true;
                // (A function, conditional or intersection type node is parenthesized: the printer's union constituents
                // are of TypeOperator precedence)
                bool parens = member->kind == PSEUDO_DIRECT && !member->predicate && !member->node->parenthesized &&
                    (member->node->tag == TYPE_CALLABLE || member->node->tag == TYPE_TERNARY || member->node->tag == TYPE_INTERSECTION);
                if (parens) out(reused_if(d, false), c_sv("("));
                // (An Inferred member is the initializer of the declaration whose pseudo type got `undefined` added
                // (pseudo_add_undefined()): tsgo prints its parent declaration's type, serializeTypeForDeclaration, the
                // whole union's type, whose `undefined` the union's own then doesn't repeat)
                Ty const* fallback = member->kind == PSEUDO_INFERRED && !member->signature_return ? ty : nullptr;
                display_pseudo(me, member_d, member, fallback);
                if (fallback && ty_without_undefined(me, fallback) != fallback) has_undefined = true;
                if (parens) out(reused_if(d, false), c_sv(")"));
            }
            if (!printed) out(d, elided ? c_sv("any") : c_sv("never"));
            return;
        }
        case PSEUDO_UNDEFINED: out(d, strict ? c_sv("undefined") : c_sv("any")); return;
        case PSEUDO_NULL: out(d, strict ? c_sv("null") : c_sv("any")); return;
        case PSEUDO_ANY: out_lit(d, "any"); return;
        case PSEUDO_STRING: out_lit(d, "string"); return;
        case PSEUDO_NUMBER: out_lit(d, "number"); return;
        case PSEUDO_BIGINT: out_lit(d, "bigint"); return;
        case PSEUDO_BOOLEAN: out_lit(d, "boolean"); return;
        case PSEUDO_FALSE: out_lit(d, "false"); return;
        case PSEUDO_TRUE: out_lit(d, "true"); return;
        case PSEUDO_STRING_LITERAL: {
            // (The literal node cloned: its quotes as written; a template's tabs and carriage returns escaped, the
            // printer's template text)
            approx_add(d, reused_node_length(pseudo->expr->inner_span));
            csview text = span_sv(pseudo->expr->inner_span);
            Ty const* literal = ty_expr_type(me, pseudo->expr);
            TyDisplay reused = reused_if(d, true);
            if (literal->kind != TY_LITERAL || !literal->literal.string) {
                out(reused, text);
                return;
            }
            // (Its value escaped again, with NoAsciiEscaping: the clone has no source text, it keeps its quotes; a
            // no-substitution template has no raw text)
            map_span(d, pseudo->expr->inner_span, false);
            csview value = atom_sv(literal->literal.string);
            if (text.size && text.buf[0] == '`') display_template_string(reused, value);
            else display_quoted_string(reused, value, text.size && text.buf[0] == '\'', false);
            map_span(d, pseudo->expr->inner_span, true);
            return;
        }
        case PSEUDO_NUMERIC_LITERAL:
        case PSEUDO_BIGINT_LITERAL: {
            // (A cloned numeric literal prints its value: `0x1` is `1`)
            approx_add(d, reused_node_length(pseudo->expr->inner_span));
            TyDisplay literal_d = d;
            literal_d.approx = nullptr;
            // (The expression reused: `-1` a prefix operator around its operand)
            Span expr_span = pseudo->expr->inner_span;
            Ty const* literal = ty_regular(me, ty_expr_type(me, pseudo->expr));
            if (D_MAP(d) && span_valid(expr_span) && expr_span.size && expr_span.buf[0] == '-' && literal->kind == TY_LITERAL &&
                literal->literal.value.tag == VALUE_NUMBER && literal->literal.value.number < 0 && !(literal->flags & TYF_BIGINT_LITERAL)
            ) {
                // (A prefix operator around its operand)
                map_span(d, expr_span, false);
                out_lit(literal_d, "-");
                map_at(d, decl_map_skip_trivia(expr_span.buf + 1, span_end_ptr(expr_span)));
                ty_display(me, literal_d, ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = -literal->literal.value.number }));
                map_span(d, expr_span, true);
                return;
            }
            map_span(d, expr_span, false);
            ty_display(me, literal_d, literal);
            map_span(d, expr_span, true);
            return;
        }
        case PSEUDO_SIGNATURE: {
            Signature const* sig = ty ? ty_single_signature(me, ty, false, false) : nullptr;
            TyDisplay sig_d = reused_if(d, false);
            // (Its parameters are in scope in its return type: tsgo's enterNewScope)
            TyDisplayParams printed = { .params = sig ? sig_res(me, sig)->params : nullptr, .count = sig ? sig_res(me, sig)->param_count : 0,
                .function = pseudo->function, .outer = d.printed_params, .serial = d.printed ? ++d.printed->serial : 0 };
            if (sig) sig_d.printed_params = &printed;
            if (pseudo->count || vec_generic_item_size(&pseudo->function->generic.items)) sig_d.fake_scope = true;
            u32 saved = param_names_enter(sig_d); // (Its type parameters are its own: a sibling may reuse their names)
            display_node_type_params(me, sig_d, &pseudo->function->generic);
            display_pseudo_params(me, sig_d, pseudo->params, pseudo->count, sig);
            out_lit(sig_d, " => ");
            display_pseudo_return(me, sig_d, pseudo->ret, sig, pseudo->function);
            param_names_leave(sig_d, saved);
            return;
        }
        case PSEUDO_TUPLE: {
            TyList args = {0};
            if (ty && ty->kind == TY_REFERENCE && ty->reference.target->declared.kind == TY_DECLARED_TUPLE_TARGET) {
                args = ty_reference_args(me, ty);
            }
            out_lit(d, "readonly [");
            for (u32 i = 0; i < pseudo->count; i++) {
                if (i) out_lit(d, ", ");
                display_pseudo(me, d, pseudo->types[i], i < args.len ? args.data[i] : nullptr);
            }
            out_lit(d, "]");
            return;
        }
        case PSEUDO_OBJECT: {
            if (!pseudo->count) {
                out_lit(d, "{}");
                return;
            }
            Members const* members = ty && (ty->flags & TYF_OBJECT) ? ty_members(me, ty) : nullptr;
            bool is_const = pseudo->in_const;
            TyDisplay member_d = reused_if(brace_open(d), false);
            member_d.in_object_literal = true;
            for (u32 i = 0; i < pseudo->count; i++) {
                PseudoMember const* member = &pseudo->members[i];
                Prop const* prop = members ? pseudo_member_prop(members, member->item, i) : nullptr;
                Ty const* type = prop ? ty_remove_missing(me, prop_type(me, prop), (prop->flags & PROP_OPTIONAL) != 0) : nullptr;
                bool readonly = is_const || (member->kind == PSEUDO_MEMBER_PROPERTY && member->readonly);
                member_begin_node(member_d, member->item->span);
                if (readonly) out_lit(member_d, "readonly ");
                // (tsgo's reuseName: the name as a property name node would print it, counted as reused)
                approx_add(member_d, reused_node_length(member->item->name_span));
                TyDisplay name_d = member_d;
                name_d.tracker = nullptr;
                name_d.approx = nullptr;
                switch (member->kind) {
                    case PSEUDO_MEMBER_METHOD: {
                        Signature const* sig = !type ? nullptr :
                            ty_single_signature(me, member->optional ? ty_without_undefined(me, type) : type, false, false);
                        if (prop && csview_equals(atom_sv(prop->name), "new")) out_lit(name_d, "\"new\"");
                        else if (prop) display_pseudo_member_name(me, name_d, prop, member->item);
                        if (is_const) out_lit(member_d, ": ");
                        u32 saved = param_names_enter(member_d); // (Its type parameters are its own)
                        TyDisplay method_d = member_d;
                        TyDisplayParams printed = { .params = sig ? sig_res(me, sig)->params : nullptr, .count = sig ? sig_res(me, sig)->param_count : 0,
                            .function = member->function, .outer = member_d.printed_params,
                            .serial = member_d.printed ? ++member_d.printed->serial : 0 };
                        if (sig) method_d.printed_params = &printed;
                        if (member->param_count || vec_generic_item_size(&member->function->generic.items)) method_d.fake_scope = true;
                        display_node_type_params(me, method_d, &member->function->generic);
                        display_pseudo_params(me, method_d, member->params, member->param_count, sig);
                        out(method_d, is_const ? c_sv(" => ") : c_sv(": "));
                        display_pseudo_return(me, method_d, member->type, sig, member->function);
                        param_names_leave(member_d, saved);
                        break;
                    }
                    case PSEUDO_MEMBER_PROPERTY: {
                        if (prop) display_pseudo_member_name(me, name_d, prop, member->item);
                        out_lit(member_d, ": ");
                        // (An Inferred one prints its property assignment's own type, tsgo's serializeTypeForDeclaration
                        // of it: the literal's property, its literals widened but not its implicit anys, `x: []` is
                        // `x: undefined[]` without strictNullChecks where the widened type has `any[]`)
                        Ty const* own = type;
                        if (type && member->type->kind == PSEUDO_INFERRED && !member->type->signature_return &&
                            member->item->tag == OBJECT_ITEM_PROPERTY && pseudo->expr
                        ) {
                            Ty const* literal = ty_expr_type(me, pseudo->expr);
                            Prop const* literal_prop = literal->flags & TYF_OBJECT ? pseudo_member_prop(ty_members(me, literal), member->item, i) : nullptr;
                            if (literal_prop) own = ty_widened_literal(me, prop_type(me, literal_prop));
                        }
                        display_pseudo_in(me, member_d, member->type, own,
                            member->item->tag == OBJECT_ITEM_PROPERTY ? member->item : nullptr, nullptr, false);
                        break;
                    }
                    case PSEUDO_MEMBER_SETTER: {
                        out_lit(member_d, "set ");
                        if (prop) display_pseudo_member_name(me, name_d, prop, member->item);
                        out_lit(member_d, "(");
                        Ty const* write = prop ? prop_write_type(me, prop) : nullptr;
                        if (member->param_count) {
                            param_comments_begin(member_d, member->params[0].arg);
                            display_pseudo_param(me, member_d, &member->params[0], write ? write : type);
                            param_comments_end(member_d, member->params[0].arg);
                        }
                        out_lit(member_d, ")");
                        break;
                    }
                    case PSEUDO_MEMBER_GETTER:
                        out_lit(member_d, "get ");
                        if (prop) display_pseudo_member_name(me, name_d, prop, member->item);
                        out_lit(member_d, "(): ");
                        display_pseudo(me, member_d, member->type, type);
                        break;
                }
                member_end_node(member_d, member->item->span, nullptr);
            }
            brace_close(d);
            return;
        }
    }
    impossible;
}

// tsgo's ObjectFlags.RequiresWidening: an object or array literal that isn't widened yet, or a widening null or
// undefined in it. A declaration of such a type is printed from its type, not its syntax
static bool requires_widening(Ty const* ty) {
    return (ty->flags & TYF_FRESH_OBJECT) || (ty->xflags & TYX_CONTAINS_WIDENING);
}

// An object literal's property whose value is an array or object literal (through `as const` or `satisfies`): in the
// literal's own type, before widening, its type is that literal's, which tsc's ContainsObjectOrArrayLiteral marks as
// still widening (tttt's tuples and arrays have no such twin), so serializeTypeForDeclaration doesn't reuse its syntax
static bool item_is_literal(ObjectExprItem const* item) {
    if (item->tag != OBJECT_ITEM_PROPERTY) return false;
    Expr const* value = &item->value;
    while (value->tag == EXPR_AS_TYPE && (value->as_type->is_satisfies || as_type_is_const(value->as_type))) value = &value->as_type->expr;
    return value->tag == EXPR_ARRAY || value->tag == EXPR_OBJECT;
}

// A type that may be an array or object literal's own (item_is_literal()): not a named object type, which a spread's
// optional property of the same name can make it, `{ location: { line: 1 }, ...overrides }` with `location?: Loc`
// (tsc's subtype-reduced union of both has no ContainsObjectOrArrayLiteral)
static bool literal_shaped(Checker* me, Ty const* ty) {
    if (ty->kind == TY_UNION) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (literal_shaped(me, ty->composite.types.data[i])) return true;
        }
        return false;
    }
    return ty->kind == TY_ANONYMOUS || ty_is_tuple(ty) || ty_is_array(me, ty);
}

static void display_structure(Checker* me, TyDisplay d, Ty const* ty) {
    if (reuses_nodes(d) && !span_valid(d.decl_literal) && ty->kind == TY_ANONYMOUS && ty->anonymous.node &&
        ty->anonymous.node->tag == TYPE_INTERFACE && annotates_name_at(d.at, ty->anonymous.node->interface.span)
    ) {
        d.decl_literal = ty->anonymous.node->interface.span;
    }
    d.node_file = nullptr; // (Its members' nodes are their own declarations')
    Members const* members = ty_members(me, ty);
    if (members->mapped) {
        display_mapped(me, d, members->mapped);
        return;
    }
    // (One whose property's type needed itself, as a mapped type too: tsc's createAnonymousTypeNode, containsError)
    if (ty_members_contain_error(me, members) && members_rare(members)->expanded_from) {
        display_mapped(me, d, members_rare(members)->expanded_from);
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
        approx_add(d, 2);
        out_lit(d, "{}");
        return;
    }
    // (Past the truncation length: tsc's createTypeNodesFromResolvedType)
    if (display_over(d)) {
        approx_add(d, 2);
        out_lit(d, "{ ...; }");
        return;
    }
    // Abstract construct signatures can't be members of a type literal: `(abstract new () => C) & { rest }` (tsc's
    // createTypeNodeFromObjectType)
    u32 abstract_count = 0;
    for (u32 i = 0; i < members->construct_count; i++) abstract_count += (members->construct[i]->flags & SIGNATURE_ABSTRACT) != 0;
    if (abstract_count) {
        // (One alone is the function type: `abstract new () => C`, parenthesized where the context needs it)
        bool rest = members->call_count + members->construct_count - abstract_count + members->index_count + written_prop_count(d, members);
        bool alone = abstract_count == 1 && !rest;
        for (u32 i = 0, printed = 0; i < members->construct_count; i++) {
            if (!(members->construct[i]->flags & SIGNATURE_ABSTRACT)) continue;
            if (printed++) out_lit(d, " & ");
            if (!alone) out_lit(d, "(");
            display_signature(me, d, members->construct[i], " => ", true);
            if (!alone) out_lit(d, ")");
        }
        if (!rest) return;
        out_lit(d, " & ");
    }
    TyDisplay brace_outer = d;
    d = brace_open(d);
    d.in_object_literal = true;
    TyDisplay sig_d = d;
    for (u32 i = 0; i < members->call_count && members->call_count > 1 && (d.types_baseline || d.reuse_nodes) && !sig_d.sibling_mentions_mapped; i++) {
        Signature const* root = members->call[i];
        while (sig_target(me, root)) root = sig_target(me, root);
        sig_d.sibling_mentions_mapped = root != members->call[i] && mentions_mapped(me, members->call[i], root);
    }
    for (u32 i = 0; i < members->call_count; i++) {
        member_begin(d);
        display_signature(me, sig_d, members->call[i], ": ", false);
        member_end(d);
    }
    for (u32 i = 0; i < members->construct_count; i++) {
        if (members->construct[i]->flags & SIGNATURE_ABSTRACT) continue;
        member_begin(d);
        display_signature(me, d, members->construct[i], ": ", true);
        member_end(d);
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
                    Expr const* leftmost = entity_leftmost(component->name);
                    d.tracker->track_computed_name(d.tracker, span_sv(name), span_source(name),
                        leftmost->tag == EXPR_IDENTIFIER ? leftmost->identifier.symbol : nullptr);
                }
                member_begin(d);
                // (Declaration maps: the member's node and its name's tokens keep their ranges, the type is made)
                ObjectExprItem const* item = D_MAP(d) && component->item && span_valid(component->item->name_span) ? component->item : nullptr;
                if (item) map_span(d, item->span, false);
                if (info->readonly) out_lit(reused, "readonly ");
                if (item) {
                    map_span(d, item->name_span, false);
                    out_lit(reused, "[");
                    display_entity_expr(me, reused, component->name);
                    out_lit(reused, "]");
                    map_span(d, item->name_span, true);
                    out_lit(reused, ": ");
                } else {
                    out_lit(reused, "[");
                    out(reused, span_sv(expr_outer_span(component->name)));
                    out_lit(reused, "]: ");
                }
                ty_display(me, d, component->type);
                out_lit(reused, ";");
                if (item) map_span(d, item->span, true);
                out(d, d.multiline ? c_sv("\n") : c_sv(" "));
            }
            continue;
        }
        member_begin(d);
        if (info->readonly) out_lit(d, "readonly ");
        out_lit(d, "[");
        out(d, info->param_name ? atom_sv(info->param_name) : c_sv("x"));
        out_lit(d, ": ");
        // (tsc's emitter writes the parameter list indented, LFIndexSignatureParameters: a multi-line key type's
        // members are one level deeper)
        TyDisplay key_d = d;
        key_d.indent++;
        ty_display(me, key_d, info->key);
        out_lit(d, "]: ");
        // (A reverse mapped type's is the elided information placeholder, except in declaration emit: tsc's
        // createTypeNodesFromResolvedType with AllowAnonymousIdentifier)
        if (ty_reverse_of(ty) && !d.tracker) {
            approx_add(d, 3);
            out_lit(d, "any");
        }
        else ty_display(me, d, info->value);
        // (tsc's indexInfoToIndexSignatureDeclarationHelper, after the types: the parameter's name and four, readonly nine)
        approx_add(d, (info->param_name ? (u32)atom_sv(info->param_name).size : 1) + 4 + (info->readonly ? 9 : 0));
        member_end(d);
    }
    TyDisplay outer_d = d;
    TyDisplayReverse reverse_frame;
    bool cut = false;
    for (u32 i = 0; i < members->prop_count; i++) {
        d = outer_d;
        // Past the truncation length, how many more and the last one (tsc's createTypeNodesFromResolvedType counts
        // from 1)
        if (!cut && display_over(d) && i + 3 < members->prop_count - 1) {
            // (Where nothing truncates, the comment saying so is all that's left out: the last one follows)
            if (d.cut_off) out_fmt_more(d, members->prop_count - (i + 1));
            cut = true;
            i = members->prop_count - 1;
        }
        Prop const* prop = members->props[i];
        if (d.tracker) {
            if (d.tracker->truncated) break;
            if (track_property(me, d, prop)) continue;
        }
        approx_add(d, prop_symbol_name_length(me, prop->name) + 1); // (tsc's addPropertyToElementList)
        bool reverse_prop = prop_decl_rare(prop->decl)->reverse_of != nullptr;
        bool placeholder = reverse_prop && reverse_placeholder(d, prop);
        // (Without the missing type of an optional one under exactOptionalPropertyTypes: tsc's getNonMissingTypeOfSymbol)
        Ty const* type = placeholder ? ty_intrinsic(me, TY_ANY) : ty_remove_missing(me, prop_type(me, prop), prop->flags & PROP_OPTIONAL);
        if (reverse_prop && !placeholder) {
            reverse_frame = (TyDisplayReverse){ .prop = prop, .outer = d.reverse_stack };
            d.reverse_stack = &reverse_frame;
        }
        // (A property that is a function of a namespace, as a spread copies it: tsc prints a property whose symbol is a
        // Function or Method as methods)
        Prop const* declared = prop;
        while (declared->target) declared = declared->target;
        Symbol const* member_symbol = declared->decl->symbol ? declared->decl->symbol :
            prop_decl_rare(declared->decl)->spread_alias ? nullptr : prop_decl_rare(declared->decl)->spread_variable;
        // (An optional one's signatures are those of its type without undefined)
        Ty const* method_type = (prop->flags & PROP_OPTIONAL) ? ty_without_undefined(me, type) : type;
        bool function_symbol = member_symbol && (member_symbol->flags & SYMBOL_FUNCTION) && member_symbol->parent &&
            !(prop->flags & PROP_READONLY) && (method_type->flags & TYF_OBJECT);
        // (Not a readonly one, `as const`: tsc's addPropertyToElementList prints it as a property; nor one several parts
        // of an intersection have, whose symbol createUnionOrIntersectionProperty makes a Property)
        bool synthetic = false;
        for (Prop const* i = prop; i && !synthetic; i = i->target) synthetic = i->synthetic;
        Members const* type_members = ((prop->flags & PROP_METHOD) && !(prop->flags & PROP_READONLY) && !synthetic) || function_symbol ?
            ((method_type->flags & TYF_OBJECT) ? ty_members(me, method_type) : nullptr) : nullptr;
        if (type_members && type_members->call_count && !type_members->prop_count) {
            // (A class instance's method: tsgo's resolveClassOrInterfaceMembers instantiates the members for `this`,
            // all but the thisless ones with one declaration (isThisless), with fresh type parameters: what mentions
            // them isn't reused. babel-traverse's overloaded generic methods of `class NodePath`. The member is that
            // instantiation in tttt too, an instance of the declared method type, where a declaration possibly
            // references `this`: members.c's thisless, lower.c's free parameters)
            TyDisplay method_d = d;
            method_d.own_params_fresh = d.tracker && !(prop->flags & PROP_STATIC) &&
                ((ty->kind == TY_DECLARED || (ty->kind == TY_REFERENCE && ty->reference.target->kind == TY_DECLARED)) &&
                 ty_declared_is_class(ty)) &&
                method_type->kind == TY_ANONYMOUS && method_type->anonymous.target != nullptr;
            // Methods print once per signature
            for (u32 j = 0; j < type_members->call_count; j++) {
                // (A method named `new` is quoted: it would read as a construct signature)
                Span comments = prop_comment_span(d, prop, j);
                member_begin_node(d, comments);
                if (csview_equals(atom_sv(prop->name), "new")) out_lit(d, "\"new\"");
                else display_member_name(me, d, prop);
                if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
                display_signature(me, method_d, type_members->call[j], ": ", false);
                member_end_node(d, comments, ";");
            }
            continue;
        }
        // Declaration emit: a class's accessors print as they are declared, a getter then a setter (tsc's
        // addPropertyToElementList: an accessor of a class without a property declaration)
        PropDeclRare const* accessors = prop_decl_rare(declared->decl);
        if (d.multiline && d.tracker && !prop->synthetic && accessors->class_auto_accessor && type != ty_intrinsic(me, TY_ERROR)) {
            // (An auto-accessor's are made up: tsc's fakeGetterDeclaration and its setter of `arg`)
            Span comments = prop_comment_span(d, prop, 0);
            TypeNode const* node = prop != declared && mentions(me, type, d.fresh_params) ? nullptr : declared->decl->node;
            member_begin_node(d, comments);
            out_lit(d, "get ");
            display_member_name(me, d, prop);
            out_lit(d, "(): ");
            display_annotated(me, d, node, type, false);
            member_end_node(d, comments, ";");
            member_begin(d);
            out_lit(d, "set ");
            display_member_name(me, d, prop);
            out_lit(d, "(arg: ");
            ty_display(me, d, type);
            out_lit(d, ")");
            member_end(d);
            continue;
        }
        if (d.multiline && d.tracker && !prop->synthetic && !declared->synthetic && (declared->flags & PROP_ACCESSOR) && !(declared->flags & PROP_FIELD) &&
            (accessors->class_getter || accessors->class_setter) && type != ty_intrinsic(me, TY_ERROR)
        ) {
            bool fresh = prop != declared && mentions(me, type, d.fresh_params);
            if (accessors->class_getter) {
                Span comments = prop_comment_span(d, prop, 0);
                member_begin_node(d, comments);
                out_lit(d, "get ");
                display_member_name(me, d, prop);
                out_lit(d, "(): ");
                FunctionImpl const* getter = accessors->class_getter;
                display_annotated(me, d, fresh || !getter->ret_explicit ? nullptr : &getter->ret, type, false);
                member_end_node(d, comments, ";");
            }
            if (accessors->class_setter) {
                Span comments = prop_comment_span(d, prop, accessors->class_getter ? 1 : 0);
                member_begin_node(d, comments);
                out_lit(d, "set ");
                display_member_name(me, d, prop);
                out_lit(d, "(");
                FunctionImpl const* setter = accessors->class_setter;
                Ty const* write = prop_write_type(me, prop);
                if (vec_function_arg_size(&setter->args)) {
                    Binding const* param = &vec_function_arg_at(&setter->args, 0)->initializer.binding;
                    cstr name = cstr_null;
                    append_binding_pattern(&name, param);
                    out(reused_if(d, true), cstr_sv(&name));
                    cstr_drop(&name);
                    out_lit(d, ": ");
                    display_annotated(me, d, fresh || !param->has_explicit_type ? nullptr : binding_type(param), write ? write : type, false);
                } else {
                    out_lit(d, "arg: ");
                    ty_display(me, d, write ? write : type);
                }
                out_lit(d, ")");
                member_end_node(d, comments, ";");
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
                // (Two signatures: three each, the setter's parameter its name and three)
                approx_add(d, 3 + 3 + 3 + (prop_decl_rare(declared->decl)->setter ? 5 : (u32)prop_decl_rare(declared->decl)->setter_param.size));
                member_begin_node(d, prop_comment_span(d, prop, 0));
                out_lit(d, "get ");
                display_member_name(me, d, prop);
                out_lit(d, "(): ");
                // (The annotations as written where they still say it: tsc's signatureToSignatureDeclarationHelper)
                FunctionImpl const* getter = prop_decl_rare(declared->decl)->setter ? declared->decl->function : nullptr;
                TypeNode const* read_node = getter ? (getter->ret_explicit ? &getter->ret : nullptr) : declared->decl->node;
                // (Instantiated, an annotation that still says it: `get done(): true`)
                bool fresh = prop != declared && (mentions(me, type, d.fresh_params) || mentions(me, write, d.fresh_params));
                display_annotated(me, d, fresh ? nullptr : read_node, type, false);
                member_end_node(d, prop_comment_span(d, prop, 0), ";");
                member_begin_node(d, prop_comment_span(d, prop, 1));
                out_lit(d, "set ");
                display_member_name(me, d, prop);
                out_lit(d, "(");
                // (The parameter's name is its declaration's, reused)
                if (prop_decl_rare(declared->decl)->setter) {
                    cstr param = cstr_null;
                    append_binding_pattern(&param, &vec_function_arg_at(&prop_decl_rare(declared->decl)->setter->args, 0)->initializer.binding);
                    out(reused_if(d, true), cstr_sv(&param));
                    cstr_drop(&param);
                } else {
                    out(reused_if(d, true), span_sv(prop_decl_rare(declared->decl)->setter_param));
                }
                out_lit(d, ": ");
                TypeNode const* write_node = prop_decl_rare(declared->decl)->setter ?
                    binding_type(&vec_function_arg_at(&prop_decl_rare(declared->decl)->setter->args, 0)->initializer.binding) : prop_decl_rare(declared->decl)->write_node;
                display_annotated(me, d, fresh ? nullptr : write_node, write, false);
                out_lit(d, ")");
                member_end_node(d, prop_comment_span(d, prop, 1), ";");
                continue;
            }
        }
        // An object literal's property assignment or accessor whose pseudo type says what its type is: printed from its
        // syntax (tsgo's serializeTypeForDeclaration of its valueDeclaration; a property's type that still widens, the
        // literal's own type, is printed as a type)
        bool suppress = false;
        bool unwidened = ty->kind == TY_ANONYMOUS && (ty->flags & TYF_FRESH_OBJECT);
        ObjectExprItem const* item = placeholder || reverse_prop || d.reused || !reuses_nodes(d) ? nullptr : declared_item(me, prop, 0);
        bool item_accessor = item && (item->tag == OBJECT_ITEM_GETTER || item->tag == OBJECT_ITEM_SETTER) && item->value.tag == EXPR_FUNCTION;
        bool item_property = item && (item->tag == OBJECT_ITEM_PROPERTY || item->tag == OBJECT_ITEM_SHORTHAND);
        ClassItem const* field = !item && !placeholder && !reverse_prop && !d.reused && reuses_nodes(d) && d.tracker ?
            declared_field(ty, prop) : nullptr;
        if ((item_property && !requires_widening(type) && !(unwidened && item_is_literal(item) && literal_shaped(me, type))) ||
            item_accessor || (field && !requires_widening(type))
        ) {
            Pseudo const* pseudo = field ? pseudo_of_field(me, field) : item_accessor ? pseudo_of_accessor(me, item->value.function) :
                pseudo_of_object_member(me, item);
            Ty const* item_type = type;
            PseudoReporter storage;
            if (pseudo_equivalent(me, d, pseudo, item_type, false, display_reporter(d, &storage))) {
                member_begin_node(d, prop_comment_span(d, prop, 0));
                // (tsc's addPropertyToElementList counts the modifier whatever prints the type)
                if (prop->flags & PROP_READONLY) {
                    approx_add(d, 9);
                    out_lit(d, "readonly ");
                }
                TyDisplay name_d = d;
                name_d.tracker = nullptr; // (named from its own declaration, see track_property())
                display_member_name(me, name_d, prop);
                if (prop->flags & PROP_OPTIONAL) out_lit(d, "?");
                out_lit(d, ": ");
                display_pseudo_with_fallback(me, d, pseudo, item_type);
                member_end_node(d, prop_comment_span(d, prop, 0), ";");
                continue;
            }
            // (Its error nodes reported: its type is printed without reporting again)
            if (pseudo->kind == PSEUDO_INFERRED && pseudo->count) suppress = true;
        }
        member_begin_node(d, prop_comment_span(d, prop, 0));
        if (prop->flags & PROP_READONLY) {
            approx_add(d, 9);
            out_lit(d, "readonly ");
        }
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
        // (An instance of a mapped type's property, its signature instantiated, is declared where that property is)
        Prop const* mapped_prop = prop;
        while (mapped_prop->target && !prop_decl_rare(mapped_prop->decl)->mapped_from) mapped_prop = mapped_prop->target;
        bool mapped_from = prop_decl_rare(origin->decl)->template_of && prop_decl_rare(mapped_prop->decl)->mapped_from;
        if (mapped_from) {
            origin = prop_decl_rare(mapped_prop->decl)->mapped_from;
            // (Through a modifiers type that is mapped itself, `keyof (Partial<A> & B)`: the declarations tsc's mapped
            // property copies are its modifiers property's, all the way down)
            for (int depth = 0; depth < 16 && origin->mapped && prop_decl_rare(origin->decl)->mapped_from; depth++) {
                origin = prop_decl_rare(origin->decl)->mapped_from;
            }
            while (origin->target) origin = origin->target;
        }
        // (An instantiation's: the annotation its object literal reused, while it still says the same, checked below:
        // tsgo's serializeTypeForDeclaration of the property's declaration, instantiated or not)
        TypeNode const* annotation = placeholder ? nullptr : prop_decl_asserted(prop->decl) ? prop_decl_asserted(prop->decl) : prop_decl_asserted(origin->decl);
        if (origin->decl->node && origin->decl->node->parsed && !origin->decl->lazy && !origin->decl->function && !prop_decl_rare(origin->decl)->overload_count &&
            !origin->decl->symbol
        ) {
            annotation = origin->decl->node;
        }
        // A module's variable (a namespace's member, spread or not): its declaration's annotation, or a `const`'s
        // literal initializer (tsc's serializeTypeOfDeclaration through the property's declarations)
        Pseudo const* variable_pseudo = nullptr;
        Symbol const* variable = origin->decl->symbol ? origin->decl->symbol :
            prop_decl_rare(origin->decl)->spread_alias ? nullptr : prop_decl_rare(origin->decl)->spread_variable;
        if (!annotation && !placeholder && variable && reuses_nodes(d)) {
            Binding const* binding = variable->decl_count == 1 && variable->decls[0].kind == SYMBOL_DECL_VARIABLE ?
                variable->decls[0].node : nullptr;
            // (tsgo's typeFromVariable, through the property's declaration: not a type that still widens)
            if (binding && !binding->parent && binding->tag == BINDING_IDENTIFIER && binding->symbol == variable &&
                !d.reused && !requires_widening(type)
            ) {
                variable_pseudo = pseudo_of_variable(me, (InitializedBinding const*)binding, variable->is_const, (PseudoPlace){0});
            }
            if (binding && !binding->parent && binding->tag == BINDING_IDENTIFIER && binding->symbol == variable &&
                binding->has_explicit_type
            ) {
                annotation = binding_type(binding);
            }
        }
        // An accessor: the getter's return annotation, else the setter's parameter's (tsc's typeFromAccessor; an object
        // literal's went the pseudo type's way above)
        if ((origin->flags & (PROP_GETTER | PROP_SETTER)) && origin->decl->function) {
            FunctionImpl const* accessor = origin->decl->function;
            Binding const* param = setter_value_parameter(accessor);
            Binding const* setter_param = prop_decl_rare(origin->decl)->setter ? setter_value_parameter(prop_decl_rare(origin->decl)->setter) : nullptr;
            if (setter_param && !accessor->ret_explicit) {
                param = setter_param;
                if (param->has_explicit_type) annotation = binding_type(param);
            } else if ((origin->flags & PROP_GETTER) && accessor->ret_explicit) {
                annotation = &accessor->ret;
            } else if (!(origin->flags & PROP_GETTER) && param && param->has_explicit_type) {
                annotation = binding_type(param);
            }
        }
        if (suppress) d.suppress_fallback = true;
        TyDisplay type_d = d;
        // (Instantiated in a signature that has fresh type parameters in tsc, not tttt: see TyDisplay.fresh_params)
        if (origin != prop && mentions(me, type, d.fresh_params)) annotation = nullptr;
        // (Printed, not reused: tsc doesn't reuse the `unique symbol` annotation that declares the symbol)
        // An optional property's type has undefined under strictNullChecks, unless exactOptionalPropertyTypes (tsc's
        // addOptionality in getTypeOfSymbol, the missing type taken out above); a reused annotation doesn't say it
        Ty const* shown = type;
        bool optional = (prop->flags & PROP_OPTIONAL) != 0;
        // (A mapped property whose template added `| undefined` to what the annotation says: the annotation in a new
        // union, like a parameter's, see annotation_plus_undefined())
        bool reverse = prop_decl_rare(prop->decl)->reverse_of != nullptr;
        // (A mapped one from an optional declaration reuses the annotation as it is: tsgo's pseudoTypeEquivalentToType
        // with isOptionalAnnotated, requiresAddingImplicitUndefined being only for reverse mapped ones)
        if (variable_pseudo && pseudo_equivalent(me, d, variable_pseudo, shown, false, nullptr)) {
            display_pseudo(me, d, variable_pseudo, shown);
        }
        else if (reverse && annotation && !d.reused && reuses_nodes(d) && (origin->flags & PROP_OPTIONAL) &&
            annotation_plus_undefined(me, annotation, shown)
        ) {
            display_node_plus_undefined(me, d, annotation);
        }
        else if (mapped_from && annotation && !d.reused && reuses_nodes(d) && (origin->flags & PROP_OPTIONAL) &&
            annotation_is(me, annotation, shown, true)
        ) {
            display_node(me, d, annotation);
        }
        else if (mapped_from && annotation && !annotation_is(me, annotation, shown, false)) ty_display(me, d, shown);
        else if (d.reused && annotation && annotation->tag != TYPE_UNIQUE_SYMBOL) ty_display(me, d, shown);
        else if (annotation && annotation->tag == TYPE_UNIQUE_SYMBOL && reuses_nodes(d) && span_valid(d.decl_literal) &&
            span_contains_span(d.decl_literal, annotation->unique_symbol.member) && (type->flags & TYF_UNIQUE_ES_SYMBOL)
        ) {
            // (tsc's canReuseTypeNode: a `unique symbol` inside the enclosing declaration)
            display_node(me, d, annotation);
        }
        else if (annotation && reuses_nodes(d) && annotation_is(me, annotation, optional ? type : shown, optional)) display_node(me, d, annotation);
        else display_annotated(me, type_d, annotation, shown, optional);
        member_end_node(d, prop_comment_span(d, prop, 0), ";");
    }
    approx_add(d, 2); // (tsc's createTypeNodeFromObjectType)
    brace_close(brace_outer);
}

static bool anonymous_elided(TyDisplay d, Ty const* ty);
// Declaration emit: the symbol an anonymous type prints `typeof` of, null if it doesn't: a function's wherever it is
// accessible (see typeof_function_in_declaration(); a function expression's by the variable it initializes), an
// overloaded function's (its symbol's type), a static method's `typeof C.m` where C is (tsgo's isStaticMethodSymbol:
// `member` is then the method's name)
static Symbol const* anonymous_type_query(TyDisplay d, Ty const* ty, csview* member) {
    if (!d.tracker || ty->kind != TY_ANONYMOUS) return nullptr;
    if (ty->anonymous.target) return nullptr;
    if (ty->anonymous.function) {
        FunctionImpl const* impl = ty->anonymous.function;
        Symbol const* named = impl->symbol && is_non_local_function(impl->symbol) ? impl->symbol :
            function_impl_rare(impl)->top_level_variable;
        // (A function expression's variable is named in its own declaration too from a fake scope: tsgo's
        // shouldWriteTypeOfFunctionSymbol takes the variable's symbol where its declaration isn't the enclosing one)
        bool variable = named && named == function_impl_rare(impl)->top_level_variable;
        if (variable && d.fake_scope && d.tracker && d.enclosing && symbol_value_accessible_at(d.enclosing, named)) return named;
        if (typeof_function_in_declaration(d, named)) return named;
    }
    if (!ty->anonymous.function && !ty->anonymous.is_reverse &&
        ty->anonymous.merged_value && (ty->anonymous.merged_value->flags & SYMBOL_FUNCTION) &&
        is_non_local_function(ty->anonymous.merged_value) && typeof_function_in_declaration(d, ty->anonymous.merged_value)
    ) {
        return ty->anonymous.merged_value;
    }
    // (An overloaded method's type has its signatures, the first one's declaration says which method it is)
    FunctionImpl const* static_method = ty->anonymous.function ? ty->anonymous.function :
        !ty->anonymous.object_literal && ty->anonymous.overload_count ? ty->anonymous.overloads[0] : nullptr;
    if (d.enclosing && static_method) {
        FunctionImpl const* method = static_method;
        Symbol const* owner = function_impl_rare(method)->static_method_of;
        csview name = span_valid(function_impl_member_name(method)) ? span_sv(function_impl_member_name(method)) : c_sv("");
        if (name.size >= 2 && name.buf[0] == '[' && name.buf[name.size - 1] == ']') {
            name = csview_trim_whitespace(csview_slice(name, 1, name.size - 1));
            if (!name.size || (name.buf[0] != '"' && name.buf[0] != '\'')) name = c_sv("");
        }
        if (name.size >= 2 && (name.buf[0] == '"' || name.buf[0] == '\'')) name = csview_slice(name, 1, name.size - 1);
        if (owner && name.size && name.buf[0] != '#' && is_identifier_name(name) &&
            symbol_value_accessible_at(d.enclosing, owner)
        ) {
            *member = name;
            return owner;
        }
    }
    return nullptr;
}

static bool anonymous_prints_type_query(TyDisplay d, Ty const* ty) {
    csview member = {0};
    return ty->kind == TY_ANONYMOUS && !ty->anonymous.module_clone && !shows_alias(d, ty) && anonymous_type_query(d, ty, &member);
}

static bool is_function_like(Checker* me, TyDisplay d, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS || shows_alias(d, ty)) return false;
    if (anonymous_prints_type_query(d, ty)) return false; // (`typeof f`)
    // (Inside its own rendering it is `any` (or `typeof f`): no function type node to parenthesize)
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) return false;
    }
    if (display_over(d) || anonymous_elided(d, ty)) return false; // (The placeholder is a keyword)
    Members const* members = ty_members(me, ty);
    // (A class's `prototype` counts, even where it isn't written: `{ new (): C; }`, tsc's createTypeNodeFromObjectType)
    // (Not for an abstract construct signature, which is written alone where `prototype` isn't: an instantiation
    // expression of an abstract class, `(abstract new () => C<string>) & typeof M`)
    bool abstract_alone = members->construct_count == 1 && !members->call_count &&
        (members->construct[0]->flags & SIGNATURE_ABSTRACT) && !written_prop_count(d, members);
    return (!members->prop_count || abstract_alone) && !members->index_count && !members->mapped &&
        members->call_count + members->construct_count == 1;
}

// The static side of a mixin class, which tsc prints as a type literal rather than `typeof C` (createAnonymousTypeNode:
// only a class without getBaseTypeVariableOfClass is a type query)
static bool is_mixin_static_side(Checker* me, Ty const* ty) {
    if (ty->kind == TY_REFERENCE) ty = ty->reference.target;
    return ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_CLASS_STATIC && ty->declared.symbol &&
        !ty->declared.merged_value && ty_class_base_type_variable(me, ty->declared.symbol);
}

// The properties a type literal of these members writes: declaration emit leaves out a class's `prototype` (tsc's
// WriteClassExpressionAsTypeLiteral)
static u32 written_prop_count(TyDisplay d, Members const* members) {
    u32 count = members->prop_count;
    if (!d.tracker) return count;
    for (u32 i = 0; i < members->prop_count; i++) count -= (members->props[i]->flags & PROP_PROTOTYPE) != 0;
    return count;
}

// A type literal that prints as an intersection: its abstract construct signatures are split off (display_structure())
static bool displays_as_intersection(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty)) return false;
    Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    bool class_literal = declared->kind == TY_DECLARED && declared->declared.kind == TY_DECLARED_CLASS_STATIC &&
        class_as_literal(d, declared);
    if (ty->kind != TY_ANONYMOUS && !is_mixin_static_side(me, ty) && !class_literal) return false;
    Members const* members = ty_members(me, ty);
    if (members->mapped) return false;
    u32 abstract_count = 0;
    for (u32 i = 0; i < members->construct_count; i++) abstract_count += (members->construct[i]->flags & SIGNATURE_ABSTRACT) != 0;
    return abstract_count && (abstract_count > 1 || written_prop_count(d, members) || members->index_count || members->call_count ||
        members->construct_count > abstract_count);
}

static void display_parenthesized_if(Checker* me, TyDisplay d, Ty const* ty, bool parens) {
    // (Past a message's truncation length the member may print as the elided information placeholder, a type
    // reference node `...` that tsc's printer doesn't parenthesize: printed aside first then)
    if (parens && d.cut_off && display_over(d) && !d.tracker && !D_MAP(d) && !d.comments) {
        cstr text = cstr_init();
        TyDisplay aside = d;
        aside.target = &text;
        ty_display(me, aside, ty);
        bool elided = csview_equals(cstr_sv(&text), "...");
        if (!elided) out_lit(d, "(");
        append_printed(d, &text);
        if (!elided) out_lit(d, ")");
        return;
    }
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
    sources[0] = ty_non_distributed(root->conditional.check); // (prependTypeMapping: getNonDistributedTypeParameter)
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

static Ty const* anonymous_root(Ty const* ty) {
    while (ty->kind == TY_ANONYMOUS && ty->anonymous.target && !ty->anonymous.applies_type_args) ty = ty->anonymous.target;
    return ty;
}

// What stands for tsc's symbol of an anonymous type: the declaration it was instantiated from, an object literal's
// expression (its widened and regular copies have the literal's symbol too), a function's declaration
static void const* anonymous_key(Ty const* ty) {
    Ty const* root = anonymous_root(ty);
    if (root->kind != TY_ANONYMOUS) return root;
    if (root->anonymous.object_literal && !root->anonymous.overload_count && root->anonymous.literal_expr) return root->anonymous.literal_expr;
    if (root->anonymous.function) return root->anonymous.function;
    return root;
}

static bool anonymous_elided(TyDisplay d, Ty const* ty) {
    void const* key = anonymous_key(ty);
    u32 depth = 0;
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        depth += i->ty->kind == TY_ANONYMOUS && anonymous_key(i->ty) == key;
    }
    return depth > 10;
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

// A conditional type's branch (tsc's typeToTypeNodeOrCircularityElision): a union printed inside itself is the cyclic
// structure placeholder, `any` (TS5088 in declaration emit), which ends a recursive conditional whose instantiations
// come back to the same union
static void display_branch(Checker* me, TyDisplay d, Ty const* branch) {
    if (branch->kind != TY_UNION) {
        ty_display(me, d, branch);
        return;
    }
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty != branch) continue;
        if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
        approx_add(d, 3);
        out(d, truncating(d) ? c_sv("...") : c_sv("any"));
        return;
    }
    // (Visited as a whole, visitAndTransformType: past the truncation length the union is one placeholder, not one per
    // member)
    if (display_over(d)) {
        out_elided(d);
        return;
    }
    TyDisplayVisit visit = { .ty = branch, .outer = d.visiting };
    d.visiting = &visit;
    ty_display(me, d, branch);
}

static void display_conditional(Checker* me, TyDisplay d, Ty const* ty) {
    // Eleven instantiations of one conditional type deep, the next is the elided information placeholder (tsc's
    // visitAndTransformType counts conditional types by their root's node); `any` without truncation (the
    // types baseline, declaration emit), `...` in messages
    if (conditional_elided(d, ty)) {
        if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_TRUNCATION, c_sv(""));
        set_over(d);
        out_elided(d);
        return;
    }
    TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
    d.visiting = &visit;
    PrintedRange check_range = printed_range_start(d);
    display_parenthesized_if(
        me, d, ty->conditional.check,
        is_function_like(me, d, ty->conditional.check) ||
            (ty->conditional.check->kind == TY_CONDITIONAL && !shows_alias(d, ty->conditional.check)) ||
            mapped_as_conditional(me, d, ty->conditional.check)
    );
    printed_range_end(d, &check_range);
    approx_add(d, 15); // (tsc's conditionalTypeToTypeNode, after the check type)
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
    display_branch(me, d, ty_conditional_branch(me, ty, true));
    out_lit(d, " : ");
    display_branch(me, d, ty_conditional_branch(me, ty, false));
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

// A function with expandos (or a JavaScript `{}` container, the object literal's type in tsgo) prints its structure,
// except within itself (`typeof f`: tsc's shouldWriteTypeOfFunctionSymbol, visitedTypes) or where declaration emit
// names it
static bool expando_displays_structurally(TyDisplay d, Ty const* ty) {
    if (!is_nested_expando_function(ty) && !(ty->declared.kind == TY_DECLARED_NAMESPACE && ty->declared.js_container)) return false;
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) return false;
    }
    Symbol const* function = ty->declared.namespace_symbol ? ty->declared.namespace_symbol : ty->declared.symbol;
    return !(is_non_local_function(function) && typeof_function_in_declaration(d, function));
}

// Within itself, a function with expandos that isn't declared in a file or namespace (or a JavaScript `{}` container)
// can't be `typeof f`: tsc's createAnonymousTypeNode prints the cyclic structure placeholder
// (shouldWriteTypeOfFunctionSymbol only names non-local functions)
static bool expando_cyclic_placeholder(TyDisplay d, Ty const* ty) {
    if (!is_nested_expando_function(ty) && !(ty->declared.kind == TY_DECLARED_NAMESPACE && ty->declared.js_container)) return false;
    Symbol const* function = ty->declared.namespace_symbol ? ty->declared.namespace_symbol : ty->declared.symbol;
    if (function && is_non_local_function(function)) return false;
    for (TyDisplayVisit const* i = d.visiting; i; i = i->outer) {
        if (i->ty == ty) return true;
    }
    return false;
}

static bool class_literal_function_like(Checker* me, TyDisplay d, Ty const* ty) {
    Ty const* declared = ty->kind == TY_REFERENCE ? ty->reference.target : ty;
    if (declared->kind != TY_DECLARED || declared->declared.kind != TY_DECLARED_CLASS_STATIC || !class_as_literal(d, declared) ||
        is_mixin_static_side(me, declared)
    ) {
        return false;
    }
    Members const* members = ty_members(me, ty);
    return !written_prop_count(d, members) && !members->index_count && members->call_count + members->construct_count == 1 &&
        members->construct_count && (members->construct[0]->flags & SIGNATURE_ABSTRACT);
}

static bool needs_parens_in_array(Checker* me, TyDisplay d, Ty const* ty) {
    if (shows_alias(d, ty)) return false;
    // (What prints: a union or intersection reduced, see ty_display())
    if (!d.no_reduction && (ty->kind == TY_INTERSECTION || ty->kind == TY_UNION)) ty = ty_reduced(me, ty);
    // (A structure: a type literal, or a function type when it is one signature)
    if (ty->kind == TY_DECLARED && ty->declared.kind == TY_DECLARED_NAMESPACE && ty->declared.name && ty->declared.name->len &&
        expando_displays_structurally(d, ty)
    ) {
        Members const* members = ty_members(me, ty);
        return !members->prop_count && !members->index_count && members->call_count + members->construct_count == 1;
    }
    if ((ty->flags & TYF_BOOLEAN) || is_whole_enum(me, ty)) return false;
    // (A class's static side declaration emit writes as a type literal: one abstract construct signature alone is a
    // function type)
    if (class_literal_function_like(me, d, ty)) return true;
    // `(typeof C)[]`: a type query is parenthesized too (the static side of a local class in a generic function too)
    Ty const* declared = ty->kind == TY_REFERENCE && ty->reference.target->declared.kind == TY_DECLARED_CLASS_STATIC ?
        ty->reference.target : ty;
    bool type_query = declared->kind == TY_DECLARED &&
        (declared->declared.kind == TY_DECLARED_CLASS_STATIC || declared->declared.kind == TY_DECLARED_ENUM_OBJECT ||
            declared->declared.kind == TY_DECLARED_NAMESPACE) && declared->declared.name && declared->declared.name->len;
    // `(readonly T[])[]`, `(readonly [K, V])[]`: a type operator
    bool readonly = ty->kind == TY_REFERENCE && (ty->reference.target == my readonly_array_target ||
        (ty_is_tuple(ty) && ty->reference.target->declared.tuple->readonly));
    // (`(unique symbol)[]`, `(typeof sym)[]`: a type operator or query)
    bool unique_symbol = ty->kind == TY_LITERAL && (ty->flags & TYF_UNIQUE_ES_SYMBOL);
    // (A conditional type printed as the elided information placeholder is `any`)
    bool conditional = (ty->kind == TY_CONDITIONAL && !display_over(d) && !conditional_elided(d, ty)) || mapped_as_conditional(me, d, ty);
    return ty->kind == TY_UNION || ty->kind == TY_INTERSECTION || conditional || ty->kind == TY_KEYOF ||
        type_query || readonly || unique_symbol || is_function_like(me, d, ty) || anonymous_prints_type_query(d, ty) ||
        displays_as_intersection(me, d, ty);
}

static void display_tuple(Checker* me, TyDisplay d, TupleShape const* shape, TyList args) {
    if (shape->readonly) out_lit(d, "readonly ");
    out_lit(d, "[");
    // (Its elements are a list like type arguments: tsc's mapToTypeNodes)
    if (shape->arity && display_over(d)) {
        out_elided(d);
        out_lit(d, "]");
        return;
    }
    uint8_t* collisions = name_collisions(me, d, (TyList){ .data = args.data, .len = shape->arity });
    ListReprint* reprint = list_reprint_new(collisions, shape->arity);
    for (u32 i = 0; i < shape->arity; i++) {
        if (i) out_lit(d, ", ");
        if (display_over(d) && i + 3 < shape->arity - 1) {
            out_more(d, shape->arity - (i + 1), ", ");
            i = shape->arity - 1;
        }
        approx_add(d, 2);
        bool rest = shape->flags[i] & TUPLE_ELEMENT_REST;
        bool optional = shape->flags[i] & TUPLE_ELEMENT_OPTIONAL;
        Ty const* element = ty_remove_missing(me, args.data[i], optional); // (tsc's typeReferenceToTypeNode)
        if (rest) out_lit(d, "...");
        if (shape->names && shape->names[i]) {
            out(d, atom_sv(shape->names[i]));
            if (optional) out_lit(d, "?");
            out_lit(d, ": ");
            list_item(me, d, reprint, i, element, LIST_ITEM_PLAIN, false);
        } else {
            list_item(me, d, reprint, i, element, LIST_ITEM_PARENS, optional && needs_parens_in_array(me, d, element));
            if (optional) out_lit(d, "?");
        }
    }
    list_reprint_finish(me, d, reprint);
    free(collisions);
    out_lit(d, "]");
}

// How many members of a union formatUnionTypes() leaves (booleans and whole enums are one), nullables included
static u32 union_display_count(Checker* me, TyList types, Ty const** last) {
    u32 count = 0;
    u32 nullable = 0;
    *last = nullptr;
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        if (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE)) {
            nullable |= t->flags;
            continue;
        }
        count++;
        *last = t;
        if (ty_regular(me, t) == ty_intrinsic(me, TY_FALSE) && i + 1 < types.len &&
            ty_regular(me, types.data[i + 1]) == ty_intrinsic(me, TY_TRUE)
        ) {
            i++;
            *last = nullptr; // (`boolean`: not worth printing alone, no truncation)
            continue;
        }
        if (t->kind == TY_LITERAL && t->literal.enum_decl) {
            Ty const* enum_type = ty_enum_of_member(me, t);
            if (enum_type->kind == TY_UNION) {
                u32 n = enum_type->composite.types.len;
                if (i + n <= types.len && ty_regular(me, types.data[i + n - 1]) == enum_type->composite.types.data[n - 1]) {
                    i += n - 1;
                    *last = enum_type;
                }
            }
        }
    }
    if (nullable & TYF_NULL) count++, *last = ty_intrinsic(me, TY_NULL);
    if (nullable & TYF_UNDEFINED) count++, *last = ty_intrinsic(me, TY_UNDEFINED);
    return count;
}

// A member of a union, parenthesized as tsc's printer does (parenthesizeConstituentTypeOfUnionType)
static void display_union_member(Checker* me, TyDisplay d, Ty const* t) {
    // (A member that is itself a union is one in an origin, `(string | false) | (string | false)[]` of a hidden
    // local alias: tsc's printer parenthesizes a union constituent of a union)
    bool union_member = t->kind == TY_UNION && !(t->flags & TYF_BOOLEAN) && !ty_is_enum_union(t) &&
        !(t->composite.origin && t->composite.origin->kind == TY_KEYOF);
    bool parens = is_function_like(me, d, t) || class_literal_function_like(me, d, t) ||
        (!shows_alias(d, t) && (t->kind == TY_CONDITIONAL || t->kind == TY_INTERSECTION || union_member ||
            displays_as_intersection(me, d, t))) || mapped_as_conditional(me, d, t);
    display_parenthesized_if(me, d, t, parens);
}

static void display_union(Checker* me, TyDisplay d, Ty const* ty) {
    // tsc's formatUnionTypes(): `true | false` prints as `boolean`, `null` and `undefined` go last
    TyList types = ty->composite.types;
    bool first = true;
    u32 nullable = 0;
    // (tsc's mapToTypeNodes: past the truncation length, `... N more ...` and the last member)
    Ty const* last = nullptr;
    u32 display_count = d.approx || d.over ? union_display_count(me, types, &last) : 0;
    if (display_count < 3) last = nullptr; // (Nothing to elide)
    u32 display_index = 0;
    uint8_t* collisions = name_collisions(me, d, types);
    ListReprint* reprint = list_reprint_new(collisions, types.len);
    for (u32 i = 0; i < types.len; i++) {
        Ty const* t = types.data[i];
        if (t->kind == TY_INTRINSIC && (t->flags & TYF_NULLABLE)) {
            nullable |= t->flags;
            continue;
        }
        display_index++;
        bool over = last && display_over(d);
        // (Already past it before the first member: the first, how many more, the last)
        if (over && display_index == 1 && display_count > 2) {
            // (`false | true` is formatUnionTypes' `boolean` there too: `boolean | ... 1 more ... | undefined`)
            if (ty_regular(me, t) == ty_intrinsic(me, TY_FALSE) && i + 1 < types.len &&
                ty_regular(me, types.data[i + 1]) == ty_intrinsic(me, TY_TRUE)
            ) {
                approx_add(d, 7);
                out_lit(d, "boolean");
            } else {
                display_union_member(me, d, t);
            }
            out_lit(d, " | ");
            out_more(d, display_count - 2, " | ");
            display_union_member(me, d, last);
            list_reprint_finish(me, d, reprint);
            free(collisions);
            return;
        }
        if (over && display_index + 2 < display_count - 1) {
            out_lit(d, " | ");
            out_more(d, display_count - display_index, " | ");
            display_union_member(me, d, last);
            list_reprint_finish(me, d, reprint);
            free(collisions);
            return;
        }
        if (!first) out_lit(d, " | ");
        first = false;
        // (tsc's mapToTypeNodes: whitespace and separator, unless formatUnionTypes left one type, `boolean`)
        if (display_count > 1) approx_add(d, 2);
        if (ty_regular(me, t) == ty_intrinsic(me, TY_FALSE) && i + 1 < types.len &&
            ty_regular(me, types.data[i + 1]) == ty_intrinsic(me, TY_TRUE)
        ) {
            approx_add(d, 7);
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
        list_item(me, d, reprint, i, t, LIST_ITEM_UNION_MEMBER, false);
    }
    list_reprint_finish(me, d, reprint);
    free(collisions);
    if (nullable & TYF_NULL) {
        approx_add(d, (display_count > 1 ? 2 : 0) + 4);
        out(d, first ? c_sv("null") : c_sv(" | null"));
        first = false;
    }
    if (nullable & TYF_UNDEFINED) {
        approx_add(d, (display_count > 1 ? 2 : 0) + 9);
        out(d, first ? c_sv("undefined") : c_sv(" | undefined"));
    }
}

// A message's type is cut off at twice the truncation length whatever the printer did (tsc's typeToString)
// Every parameter written with a type: a function expression whose pseudo type (tsgo's typeFromFunctionLikeExpression)
// is what the checker says, unlike one typed by its context
// What tsc's typeToTypeNode counts for a keyword type: its length, but none for `unknown`
static u32 intrinsic_length(Ty const* ty) {
    if (ty->flags & TYF_UNKNOWN) return 0;
    csview name = ty->intrinsic.name;
    // (An unresolved name, ty_unresolved(), is tsc's error type with the name as its alias: printed by
    // TypeAlias.ToTypeReferenceNode, which counts its type arguments only)
    if ((ty->flags & TYF_ERROR) && !csview_equals(name, "any")) return 0;
    return name.size == 9 && name.buf[0] == 'i' ? 3 : (u32)name.size; // (`intrinsic` is any's three)
}

static void display_cut_off(Checker* me, TyDisplay d, Ty const* ty) {
    isize_t start = cstr_size(d.target);
    d.cut_off = true;
    u32 approximate_length = 0; // (tsc's context.approximateLength: one per typeToString)
    d.approx = &approximate_length;
    ty_display(me, d, ty);
    isize_t max_length = DEFAULT_MAXIMUM_TRUNCATION_LENGTH * 2;
    if (cstr_size(d.target) - start >= max_length) {
        cstr_resize(d.target, start + max_length - 3, '.');
        cstr_append(d.target, "...");
    }
}

// The serial of the outermost printed signature with parameters, 0 for none (see TyPrintedTypes)
static u32 printed_scope(TyDisplayParams const* params) {
    u32 scope = 0;
    for (TyDisplayParams const* i = params; i; i = i->outer) {
        if (i->count) scope = i->serial;
    }
    return scope;
}

void ty_display(Checker* me, TyDisplay d, Ty const* ty) {
    // (The node builder's from here on, and so is the length limit)
    if (d.transformer_node) d.target_start = cstr_size(d.target);
    d.transformer_node = false;
    bool over = false; // (One per print: tsc's context.truncating)
    if (!d.over) d.over = &over;
    u32 approximate_length = 0; // (Where nothing truncates: see NO_TRUNCATION_MAXIMUM_LENGTH)
    if (!d.depth && !d.approx && !d.tracker && !truncating(d)) d.approx = &approximate_length;
    if (!d.depth) {
        if (!d.cut_off && truncating(d)) {
            display_cut_off(me, d, ty);
            return;
        }
    }
    if (d.tracker && d.tracker->truncated) return;
    if (ty == my display_of) ty = my display_as;
    ty = ty_non_distributed(ty); // (tsgo's typeToTypeNode prints a distributed type parameter as its original)
    if (ty->kind == TY_SUBSTITUTION) {
        // Its base (tsc's typeToTypeNodeHelper)
        if (!ty_is_no_infer(ty)) {
            ty_display(me, d, ty->substitution.base);
            return;
        }
        out_lit(d, "NoInfer<");
        ty_display(me, d, ty->substitution.base);
        out_lit(d, ">");
        // (The global alias's name, after its argument: tsc's symbolToTypeNode, createAccessFromSymbolChain)
        approx_add(d, entity_name_length(c_sv("NoInfer")));
        return;
    }
    // (A union or intersection prints reduced: tsc's typeToTypeNodeHelper without NoTypeReduction)
    if (!d.no_reduction && (ty->kind == TY_INTERSECTION || ty->kind == TY_UNION)) ty = ty_reduced(me, ty);
    d.depth++;
    d.mapped_by = nullptr; // (A type is printed as it is, annotations of other declarations inside it aren't instantiated)
    // (Declaration maps: a conditional or object type the declaration printed before is tsgo's cached deep clone,
    // unmapped: trpc's `infer T extends object` in a repeated inferred type argument)
    // (Keyed by the print of the outermost signature with parameters being printed: tsgo's enterNewScope makes a fresh
    // fake scope the enclosing declaration there, whose cache starts empty; nested signatures reuse that scope)
    if (d.printed && D_MAP(d) && !d.reprinted && (ty->kind == TY_CONDITIONAL || (ty->kind == TY_ANONYMOUS && !ty->anonymous.mapped)) && !shows_alias(d, ty) &&
        ty_printed_types_add(d.printed, ty, (void const*)(uintptr_t)printed_scope(d.printed_params))
    ) {
        d.reprinted = true;
        d.no_map = true;
    }
    // (Where nothing truncates, tsc has no limit but the length: deep enough for a recursive alias's arguments. Declaration
    // emit: unchanged, see hacks.md TS7056)
    if (d.depth > (truncating(d) ? MAX_DISPLAY_DEPTH * 4 : d.tracker ? 8 * 16 : 1024)) {
        // (Declaration emit: tsc has no depth limit, a type nested this deep keeps growing until it is too long to
        // serialize, TS7056: a recursive reverse mapped type's index signatures, new types at every level)
        if (d.tracker && !d.tracker->truncated) {
            d.tracker->truncated = true;
            d.tracker->report(d.tracker, DECL_REPORT_TRUNCATION, c_sv(""));
            return;
        }
        approx_add(d, 3);
        out_lit(d, "...");
        return;
    }

    if (ty_is_enum_union(ty)) {
        out_symbol_name(me, d, ty->alias, SYMBOL_TYPE);
        return;
    }
    if (shows_alias(d, ty)) {
        // (tsc counts the name after the type arguments, which it makes first)
        u32 name_length = 0;
        TyDisplay name_d = d;
        if (d.approx) name_d.approx = &name_length;
        out_symbol_name(me, name_d, ty->alias, SYMBOL_TYPE);
        if (ty->alias_args.len) {
            out_lit(d, "<");
            display_list(me, d, ty->alias_args);
            out_lit(d, ">");
        }
        approx_add(d, name_length);
        return;
    }

    switch ((TyKind)ty->kind) {
        case TY_INTRINSIC:
            approx_add(d, intrinsic_length(ty));
            out(d, ty->intrinsic.name);
            if (ty->intrinsic.unresolved_args.len) {
                out_lit(d, "<");
                display_list(me, d, ty->intrinsic.unresolved_args);
                out_lit(d, ">");
            }
            break;

        case TY_LITERAL:
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
                    approx_add(d, 13);
                    out_lit(d, "unique symbol");
                } else {
                    approx_add(d, 6);
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
                // `enum E { #x }`, `enum E { 0n }`: tsgo's binder names the member InternalSymbolNameMissing ("\xFEmissing"),
                // not an identifier, which the printer escapes as U+FFFD: `(typeof E)["\uFFFDmissing"]`
                bool missing = private_name || (csview_equals(member_name, "__missing") && !(member_case && member_case->name_is_value));
                if (member && missing) {
                    out_lit(d, "(typeof ");
                    out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_VALUE);
                    out_lit(d, ")[\"\\uFFFDmissing\"]");
                    break;
                }
                if (member && (!is_identifier_name(member_name) || (member_name.size && member_name.buf[0] == '#'))) {
                    // `(typeof E)["a b"]`: a member whose name isn't an identifier
                    out_lit(d, "(typeof ");
                    out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_VALUE);
                    out_lit(d, ")[");
                    // (A string literal node tsc's printer makes: non-ASCII escaped)
                    display_quoted_string(d, member_name, false, true);
                    out_lit(d, "]");
                    break;
                }
                out_symbol_name(me, d, ty->literal.enum_decl, SYMBOL_TYPE);
                if (member) {
                    out_lit(d, ".");
                    out(d, member_name);
                }
            } else if (ty->literal.string) {
                approx_add(d, (u32)atom_sv(ty->literal.string).size + 2);
                display_string_literal(d, atom_sv(ty->literal.string));
            } else if (ty->flags & TYF_BOOLEAN_LITERAL) {
                approx_add(d, ty->literal.value.boolean ? 4 : 5);
                out(d, ty->literal.value.boolean ? c_sv("true") : c_sv("false"));
            } else {
                // (A number's text, a bigint's digits and one)
                isize_t start = cstr_size(d.target);
                cstr_append_value_js(d.target, &ty->literal.value);
                if (ty->flags & TYF_BIGINT_LITERAL) out_lit(d, "n");
                approx_add(d, (u32)(cstr_size(d.target) - start));
            }
            break;

        case TY_TYPE_PARAM: {
            if (d.tracker && d.in_object_literal && ty->param.state->is_this) {
                d.tracker->report(d.tracker, DECL_REPORT_THIS, c_sv(""));
            }
            bool declared_here = false;
            for (u32 i = 0; i < d.infer_params.len; i++) declared_here |= d.infer_params.data[i] == ty;
            if (declared_here) out_lit(d, "infer ");
            // (tsc counts `this` as four, a name like any (entity_name_length()), `infer N` as the name and six; with
            // GenerateNamesForShadowedTypeParams (the types baseline) the name it gives, typeParameterToName)
            isize_t name_start = cstr_size(d.target);
            // (Declared here, the name gets its declaration's range: tsc's typeParameterToName, reused)
            TyDisplay name_d = declared_here ? reused_if(d, true) : d;
            name_d.param_declaration = declared_here || d.param_declaration;
            out_type_param_name(me, name_d, ty, false);
            u32 name_length = (u32)(cstr_size(d.target) - name_start);
            approx_add(d, ty->param.state->is_this ? 4 : declared_here ? name_length + 6 : d.types_baseline ? name_length :
                2 * (name_length + 1));
            // (Its constraint, written or implied by a type argument's position, unless the position implies it
            // otherwise, `${infer S extends string}`: tsc's typeToTypeNodeHelper, omitTypeReferences as those may be
            // elided)
            Ty const* constraint = declared_here ? ty_param_constraint(me, ty) : nullptr;
            Ty const* implied = constraint ? ty_implied_infer_constraint(me, ty, true) : nullptr;
            if (constraint && !(implied && (implied == constraint || ty_is_related(me, constraint, implied, TY_RELATION_IDENTITY)))) {
                approx_add(d, 9);
                out_lit(d, " extends ");
                TyDisplay inner = d;
                inner.infer_params = (TyList){0};
                ty_display(me, inner, constraint);
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
                    if (expando_displays_structurally(d, ty)) {
                        TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
                        d.visiting = &visit;
                        display_structure(me, d, ty);
                        break;
                    }
                    if (expando_cyclic_placeholder(d, ty)) {
                        if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
                        out_elided(d); // (A message\'s cyclic structure placeholder is `...`: tsc\'s createElidedInformationPlaceholder)
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
                    // (The declared Array is a reference to itself with its own type parameters to tsc: `T[]`)
                    if ((ty == my array_target || ty == my readonly_array_target) && ty_local_type_params(ty).len == 1) {
                        if (ty == my readonly_array_target) out_lit(d, "readonly ");
                        ty_display(me, d, ty_local_type_params(ty).data[0]);
                        out_lit(d, "[]");
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
            // An array or tuple inside its own rendering (an alias nothing can name, `type R = R[]` in a function):
            // `any`, the cyclic structure placeholder (tsgo's visitedTypes of typeReferenceToTypeNode)
            bool container = target == my array_target || target == my readonly_array_target ||
                target->declared.kind == TY_DECLARED_TUPLE_TARGET;
            if (container) {
                bool recursive = false;
                for (TyDisplayVisit const* i = d.visiting; i && !recursive; i = i->outer) recursive = i->ty == ty;
                if (recursive) {
                    if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
                    out_elided(d); // (A message\'s cyclic structure placeholder is `...`: tsc\'s createElidedInformationPlaceholder)
                    break;
                }
            }
            // (Past the truncation length: tsc's visitAndTransformType of an array or tuple, or of a deferred reference,
            // `Hook<X, Y>` in an alias with a defaulted type parameter left out: one with a node)
            if ((container || ty->reference.deferred) && display_over(d)) {
                out_elided(d);
                break;
            }
            TyDisplayVisit container_visit = { .ty = ty, .outer = d.visiting };
            if (container) d.visiting = &container_visit;
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
                // (Not a deferred reference whose node writes them all, `AsyncIterable<A, B, C>` in an alias's body:
                // tsgo's typeReferenceToTypeNode looks at the reference's node)
                TyDeferredRef const* deferred = ty->reference.deferred;
                bool written = deferred && deferred->node->tag == TYPE_IDENTIFIER &&
                    vec_spanned_type_size(&deferred->node->identifier->generic_params) >= args.len;
                if (!written && ty_elides_default_type_args(me, target)) {
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
                // (tsc counts the name after the type arguments, which it makes first)
                u32 name_length = 0;
                TyDisplay name_d = d;
                if (d.approx) name_d.approx = &name_length;
                out_declared_name(me, name_d, target, SYMBOL_TYPE);
                if (args.len) { // (None: a class or interface with only a this-argument)
                    out_lit(d, "<");
                    display_list(me, d, args);
                    out_lit(d, ">");
                }
                approx_add(d, name_length);
            }
        }   break;

        case TY_ANONYMOUS: {
            if (ty->anonymous.module_clone && display_module_clone(me, d, ty)) break;
            {
                csview member = {0};
                Symbol const* named = anonymous_type_query(d, ty, &member);
                if (named) {
                    out_lit(d, "typeof ");
                    if (member.size) {
                        out_symbol_name(me, d, named, SYMBOL_VALUE);
                        out_lit(d, ".");
                        out(d, member);
                        return;
                    }
                    if (shadowed_global(me, d, named)) out_lit(d, "globalThis.");
                    out_symbol_name(me, d, named, SYMBOL_VALUE);
                    return;
                }
            }
            // An anonymous type inside its own rendering prints as `any`, like tsc's elided information placeholder
            // (`function f() { return f }` is `() => any`)
            // (Not a reverse mapped type for display, which has no symbol: its properties have placeholders of their own,
            // see reverse_placeholder(); declaration emit has none, the cycle is a cycle there)
            for (TyDisplayVisit const* i = ty_reverse_of(ty) && !d.tracker ? nullptr : d.visiting; i; i = i->outer) {
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
                // (A function expression initializing a variable of a file or namespace too, by the variable, except
                // where printed for that very declaration, by its own name if it has one)
                FunctionImpl const* expression = ty->anonymous.function && !ty->anonymous.target ? ty->anonymous.function : nullptr;
                Symbol const* variable = expression ? function_impl_rare(expression)->top_level_variable : nullptr;
                if (variable) {
                    out_lit(d, "typeof ");
                    Span own = function_impl_variable_name(expression);
                    bool here = span_valid(d.at) && (span_eq(d.at, own) || span_contains_span(d.at, expression->full_span));
                    // (Declaration emit can't name the function expression from its variable's declaration: the
                    // structural fallback, which is the cyclic placeholder, tsgo's shouldWriteTypeOfFunctionSymbol)
                    if (here && d.tracker && !d.fake_scope) {
                        d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
                        out_elided(d); // (A message\'s cyclic structure placeholder is `...`: tsc\'s createElidedInformationPlaceholder)
                        return;
                    }
                    // (Declaration emit names the variable from a fake scope too: tsgo's shouldWriteTypeOfFunctionSymbol takes
                    // the variable's symbol wherever its declaration isn't the enclosing one)
                    if (here && !d.tracker && span_valid(expression->name) && expression->symbol) out(d, span_sv(expression->name));
                    else out_symbol_name(me, d, variable, SYMBOL_VALUE);
                    return;
                }
                // (and a static method as `typeof C.m`)
                FunctionImpl const* method = ty->anonymous.function && !ty->anonymous.target ? ty->anonymous.function : nullptr;
                Symbol const* owner = method ? function_impl_rare(method)->static_method_of : nullptr;
                // (Only a name `typeof C.m` can write: not `#m`, `"m"`, `[k]` or `1`, tsgo's shouldWriteTypeOfFunctionSymbol)
                if (owner && span_valid(owner->name_span) && span_valid(function_impl_member_name(method)) &&
                    is_identifier_name(span_sv(function_impl_member_name(method))) && function_impl_member_name(method).buf[0] != '#'
                ) {
                    out_lit(d, "typeof ");
                    // (Qualified by the class only where printed for a location: tsc's symbolToTypeNode without an
                    // enclosing declaration has the symbol alone)
                    if (d.enclosing) {
                        out_symbol_name(me, d, owner, SYMBOL_VALUE);
                        out_lit(d, ".");
                    }
                    out(d, span_sv(function_impl_member_name(method)));
                    return;
                }
                // (and a type alias's type literal by the alias: tsc's getTypeAliasForTypeLiteral)
                Symbol const* alias = type_alias_of_literal(me, ty);
                if (alias) {
                    out_symbol_name(me, d, alias, SYMBOL_TYPE);
                    return;
                }
                if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
                out_elided(d); // (A message\'s cyclic structure placeholder is `...`: tsc\'s createElidedInformationPlaceholder)
                return;
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
                // (A reuse that fails falls back to printing the type, which is being visited then: tsgo's
                // createAnonymousTypeNodeEx guards that with the cyclic structure placeholder)
                if (query) {
                    if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_CYCLIC, c_sv(""));
                    approx_add(d, 3);
                    out_lit(d, "any");
                    break;
                }
            }
            // Eleven instantiations of one anonymous type deep (their symbol: the root they were instantiated from),
            // the next is the elided information placeholder (tsc's visitAndTransformType's symbolDepth); `any`
            // without truncation (the types baseline, declaration emit), `...` in messages
            // (Declaration emit reports it: the node builder's truncating, ReportTruncationError, TS7056)
            // (Past the truncation length: tsc's visitAndTransformType, before the depth. Not a type without a symbol,
            // which createAnonymousTypeNodeEx prints from its members: `{ ...; }`)
            if (display_over(d) && !ty->anonymous.no_symbol) {
                out_elided(d);
                break;
            }
            // (Printed before: again, tsc's serializedTypes. Not in declaration emit, whose tracker hears from each print)
            if (!d.tracker && reprint_printed(d, ty)) break;
            if (anonymous_elided(d, ty)) {
                if (d.tracker) d.tracker->report(d.tracker, DECL_REPORT_TRUNCATION, c_sv(""));
                set_over(d);
                out_elided(d);
                break;
            }
            TyDisplayVisit visit = { .ty = ty, .outer = d.visiting };
            d.visiting = &visit;
            u32 approx_start = d.approx ? *d.approx : 0;
            u32 outer_used = names_used_start(d);
            PrintedRange whole = printed_range_start(d);
            display_structure(me, d, ty);
            printed_range_end(d, &whole);
            u32 names_used = names_used_end(d, outer_used);
            if (!d.tracker) remember_printed(d, ty, whole, d.approx ? *d.approx - approx_start : 0, names_used);
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
            uint8_t* collisions = name_collisions(me, d, ty->composite.types);
            ListReprint* reprint = list_reprint_new(collisions, ty->composite.types.len);
            u32 count = ty->composite.types.len;
            for (u32 i = 0; i < count; i++) {
                // (Past the truncation length: tsc's mapToTypeNodes of a bare list, the first, how many more and the
                // last, or from where it went past)
                if (display_over(d) && count > 2 && (i == 0 || i + 3 < count - 1)) {
                    if (i == 0) {
                        approx_add(d, 2);
                        ty_display(me, d, ty->composite.types.data[0]);
                    }
                    out_lit(d, " & ");
                    out_more(d, i == 0 ? count - 2 : count - (i + 1), " & ");
                    approx_add(d, 2);
                    ty_display(me, d, ty->composite.types.data[count - 1]);
                    break;
                }
                Ty const* t = ty->composite.types.data[i];
                if (i) out_lit(d, " & ");
                approx_add(d, 2);
                // (A union that prints as the `keyof X` it was made from is a type operator, which needs none)
                bool keyof_origin = t->kind == TY_UNION && t->composite.origin && t->composite.origin->kind == TY_KEYOF;
                // (`boolean` and a whole enum print as a name: no parentheses)
                if (t->kind == TY_UNION && ((t->flags & TYF_BOOLEAN) || ty_is_enum_union(t))) keyof_origin = true;
                list_item(me, d, reprint, i, t, LIST_ITEM_PARENS, !shows_alias(d, t) &&
                    ((t->kind == TY_UNION && !keyof_origin) || t->kind == TY_CONDITIONAL || is_function_like(me, d, t) ||
                    class_literal_function_like(me, d, t) || displays_as_intersection(me, d, t) || mapped_as_conditional(me, d, t)));
            }
            list_reprint_finish(me, d, reprint);
            free(collisions);
        }   break;

        case TY_KEYOF: {
            // (tsc's parenthesizeOperandOfTypeOperator: unions, intersections, conditional and function types, not
            // another type operator or query: `keyof readonly any[]`, `keyof keyof T`)
            Ty const* operand = ty->keyof.operand;
            bool parens = needs_parens_in_array(me, d, operand) && !shows_alias(d, operand) &&
                (operand->kind == TY_UNION || operand->kind == TY_INTERSECTION ||
                    (operand->kind == TY_CONDITIONAL && !display_over(d) && !conditional_elided(d, operand)) || is_function_like(me, d, operand) ||
                    mapped_as_conditional(me, d, operand));
            approx_add(d, 6);
            out_lit(d, "keyof ");
            display_parenthesized_if(me, d, operand, parens);
        }   break;

        case TY_INDEXED:
            display_parenthesized_if(me, d, ty->indexed.object, needs_parens_in_array(me, d, ty->indexed.object));
            out_lit(d, "[");
            ty_display(me, d, ty->indexed.index);
            out_lit(d, "]");
            approx_add(d, 2); // (After both)
            break;

        case TY_SUBSTITUTION: impossible;

        case TY_CONDITIONAL: {
            if (display_over(d)) { // (tsc's visitAndTransformType)
                out_elided(d);
                break;
            }
            if (reprint_printed(d, ty)) break;
            u32 approx_start = d.approx ? *d.approx : 0;
            u32 outer_used = names_used_start(d);
            PrintedRange whole = printed_range_start(d);
            display_conditional(me, d, ty);
            printed_range_end(d, &whole);
            u32 names_used = names_used_end(d, outer_used);
            remember_printed(d, ty, whole, d.approx ? *d.approx - approx_start : 0, names_used);
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
            approx_add(d, 2); // (After its types, not its texts)
            break;

        case TY_STRING_MAPPING: {
            static char const* const names[] = { "Uppercase", "Lowercase", "Capitalize", "Uncapitalize" };
            out(d, csview_from(names[ty->string_mapping.mapping]));
            approx_add(d, (u32)strlen(names[ty->string_mapping.mapping]) + 1);
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
        Ty const* type = prop_type_peek(me, prop);
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
                inner = function_impl_body(i->decl.function.impl);
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
        SourceFile const* file = function->span_source ? function->span_source : span_source(function->full_span);
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
    SourceFile const* file = span_valid(at) ? span_source(at) : nullptr;
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

// Declaration emit's entry points (plan 64, declaration_emit.c's writer)

void ty_display_node(Checker* me, TyDisplay d, TypeNode const* node) {
    bool over = false;
    if (!d.over) d.over = &over;
    d.transformer_node = true;
    display_node(me, d, node);
}

void ty_display_node_signature(Checker* me, TyDisplay d, FunctionImpl const* function, char const* separator) {
    bool over = false;
    if (!d.over) d.over = &over;
    d.transformer_node = true;
    display_node_signature(me, d, function, separator);
}

void ty_display_type_params(Checker* me, TyDisplay d, Generic const* generic) {
    bool over = false;
    if (!d.over) d.over = &over;
    d.transformer_node = true;
    display_node_type_params(me, d, generic);
}

void ty_display_pseudo(Checker* me, TyDisplay d, Pseudo const* pseudo, Ty const* ty) {
    bool over = false;
    if (!d.over) d.over = &over;
    display_pseudo_with_fallback(me, d, pseudo, ty);
}

void ty_append_binding_pattern(cstr* out, Binding const* binding) { append_binding_pattern(out, binding); }
void ty_append_binding_pattern_mapped(cstr* out, Binding const* binding, SourceFile const* map) {
    append_binding_pattern_mapped(out, binding, map);
}

void ty_append_binding_pattern_commented(cstr* out, Binding const* binding, SourceFile const* map, DeclComments const* comments,
    u32 indent
) {
    DeclComments const* outer = pattern_comments;
    u32 outer_indent = pattern_indent;
    pattern_comments = comments;
    pattern_indent = indent;
    append_binding_pattern_mapped(out, binding, map);
    pattern_comments = outer;
    pattern_indent = outer_indent;
}

bool ty_is_identifier_name(csview name) { return is_identifier_name(name); }

bool ty_printed_types_add(TyPrintedTypes* me, Ty const* ty, void const* scope) {
    if (my count * 2 >= my cap) {
        u32 cap = my cap ? my cap * 2 : 16;
        TyPrintedKey* slots = calloc(cap, sizeof(TyPrintedKey));
        panic_if(!slots, "out of memory");
        for (u32 i = 0; i < my cap; i++) {
            TyPrintedKey old = my slots[i];
            if (!old.ty) continue;
            u32 h = (u32)((((uintptr_t)old.ty >> 4) ^ ((uintptr_t)old.scope >> 3)) * 2654435761u) & (cap - 1);
            while (slots[h].ty) h = (h + 1) & (cap - 1);
            slots[h] = old;
        }
        free(my slots);
        my slots = slots;
        my cap = cap;
    }
    u32 h = (u32)((((uintptr_t)ty >> 4) ^ ((uintptr_t)scope >> 3)) * 2654435761u) & (my cap - 1);
    while (my slots[h].ty) {
        if (my slots[h].ty == ty && my slots[h].scope == scope) return true;
        h = (h + 1) & (my cap - 1);
    }
    my slots[h] = (TyPrintedKey){ ty, scope };
    my count++;
    return false;
}

void ty_printed_types_clear(TyPrintedTypes* me) {
    free(my slots);
    *me = (TyPrintedTypes){0};
}
