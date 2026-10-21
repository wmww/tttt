#include "ty/ty_internal.h"
#include "unicode_case.h"
#include <math.h>

// Template literal types (`a${B}c`) and string mappings (`Uppercase<T>` & co.): the string-like types that aren't
// literals. Like tsc's getTemplateLiteralType / getStringMappingType, the constructors fold whatever they can into the
// texts, distribute over unions, and only keep type variables and pattern placeholders (`string`, `number`, `bigint`,
// `any`, other patterns) as types. A template of placeholders only is a *pattern*: a concrete (non-generic) set of
// strings, which relations match string literals against (tsc's isTypeMatchedByTemplateLiteralType).

// Too many members in a cross product of unions (tsc's checkCrossProductUnion)
enum { MAX_CROSS_PRODUCT = 100000 };
// A template literal type longer than this (all its texts) or with more placeholders is an error (TS2589), not a
// type: a recursive alias doubling a string otherwise runs until memory runs out (tsgo's maxTemplateLiteralTypeLength
// and maxTemplateLiteralTypeSpans)
enum { MAX_TEMPLATE_LITERAL_LENGTH = 50000000, MAX_TEMPLATE_LITERAL_SPANS = 100000 };

bool ty_is_generic_index(Ty const* ty) {
    if (ty->kind == TY_SUBSTITUTION) return ty_is_generic_index(ty->substitution.base) || ty_is_generic_index(ty->substitution.constraint);
    if (ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED)) return true;
    if ((ty->kind == TY_UNION || ty->kind == TY_INTERSECTION) && (ty->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES)) {
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            if (ty_is_generic_index(ty->composite.types.data[i])) return true;
        }
    }
    return false;
}

bool ty_is_pattern_placeholder(Ty const* ty) {
    if (ty->kind == TY_INTERSECTION) {
        // Placeholders with object type tags: `${number & { __brand: "x" }}`
        bool seen = false;
        for (u32 i = 0; i < ty->composite.types.len; i++) {
            Ty const* t = ty->composite.types.data[i];
            if ((t->flags & (TYF_LITERAL | TYF_NULLABLE)) || ty_is_pattern_placeholder(t)) {
                seen = true;
            } else if (!(t->flags & TYF_OBJECT)) {
                return false;
            }
        }
        return seen;
    }
    if (ty->kind == TY_INTRINSIC && (ty->flags & (TYF_ANY | TYF_STRING | TYF_NUMBER | TYF_BIGINT))) return true;
    return ty_is_pattern_literal(ty);
}

bool ty_is_pattern_literal(Ty const* ty) {
    if (ty->kind == TY_TEMPLATE_LITERAL) {
        for (u32 i = 0; i < ty->template_literal.types.len; i++) {
            if (!ty_is_pattern_placeholder(ty->template_literal.types.data[i])) return false;
        }
        return true;
    }
    return ty->kind == TY_STRING_MAPPING && ty_is_pattern_placeholder(ty->string_mapping.type);
}

// tsc's getTemplateStringForType: false if the type isn't a literal (or null, undefined)
static bool append_template_string(cstr* out, Ty const* ty) {
    if (ty->kind == TY_LITERAL && (ty->flags & TYF_STRING_LITERAL)) {
        cstr_append_sv(out, atom_sv(ty->literal.string));
    } else if (ty->kind == TY_LITERAL && (ty->flags & (TYF_NUMBER_LITERAL | TYF_BIGINT_LITERAL))) {
        cstr_append_value_js(out, &ty->literal.value);
    } else if (ty->kind == TY_LITERAL && (ty->flags & TYF_BOOLEAN_LITERAL)) {
        cstr_append(out, ty->literal.value.boolean ? "true" : "false");
    } else if (ty->flags & TYF_NULL) {
        cstr_append(out, "null");
    } else if (ty->flags & TYF_UNDEFINED) {
        cstr_append(out, "undefined");
    } else {
        return false;
    }
    return true;
}

// tsgo's CombineSurrogatePairs: a lone high surrogate followed by a lone low one (each kept as its WTF-8, ED A0-AF xx
// and ED B0-BF xx) is the character they make, like "\uD83D" + "\uDE00" in JavaScript
static void combine_surrogate_pairs(cstr* text) {
    char* s = cstr_data(text);
    isize n = cstr_size(text);
    if (!memchr(s, 0xED, (size_t)n)) return;
    isize w = 0;
    for (isize r = 0; r < n;) {
        unsigned char const* u = (unsigned char const*)s + r;
        if (r + 6 <= n && u[0] == 0xED && u[1] >= 0xA0 && u[1] <= 0xAF && u[3] == 0xED && u[4] >= 0xB0 && u[4] <= 0xBF) {
            u32 high = 0xD000 | ((u32)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            u32 low = 0xD000 | ((u32)(u[4] & 0x3F) << 6) | (u[5] & 0x3F);
            u32 cp = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
            w += cutf8_encode(s + w, cp);
            r += 6;
        } else {
            s[w++] = s[r++];
        }
    }
    cstr_resize(text, w, 0);
}

typedef struct TemplateBuilder {
    cstr text; // The text since the last kept type
    Atom const** texts;
    Ty const** types;
    u32 len; // of types
    u32 cap;
    u64 texts_length; // Of the texts already in `texts`
    bool too_large;
} TemplateBuilder;

static void builder_push(Checker* me, TemplateBuilder* b, Ty const* type) {
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->texts = realloc(b->texts, sizeof(Atom const*) * b->cap);
        b->types = realloc(b->types, sizeof(Ty const*) * b->cap);
        panic_if(!b->texts || !b->types, "out of memory");
    }
    b->texts_length += cstr_size(&b->text);
    combine_surrogate_pairs(&b->text);
    b->texts[b->len] = checker_atom(me, cstr_sv(&b->text));
    b->types[b->len++] = type;
    cstr_clear(&b->text);
}

// tsc's addSpans: false if some type can't be part of a template (the whole thing is `string` then)
static bool add_spans(Checker* me, TemplateBuilder* b, Atom const* const* texts, Ty const* const* types, u32 len) {
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        if (append_template_string(&b->text, t)) {
        } else if (t->kind == TY_TEMPLATE_LITERAL) {
            cstr_append_sv(&b->text, atom_sv(t->template_literal.texts[0]));
            if (!add_spans(me, b, t->template_literal.texts, t->template_literal.types.data, t->template_literal.types.len)) {
                return false;
            }
        } else if (ty_is_generic_index(t) || ty_is_pattern_placeholder(t)) {
            builder_push(me, b, t);
        } else {
            return false;
        }
        cstr_append_sv(&b->text, atom_sv(texts[i + 1]));
        if (b->texts_length + cstr_size(&b->text) > MAX_TEMPLATE_LITERAL_LENGTH || b->len > MAX_TEMPLATE_LITERAL_SPANS) {
            b->too_large = true;
            return false;
        }
    }
    return true;
}

static Ty const* make_template(Checker* me, Atom const* const* texts, Ty const* const* types, u32 len) {
    u32 count = 2 + 2 * len + 1;
    u64 stack_key[64];
    u64* key = count <= c_arraylen(stack_key) ? stack_key : malloc(sizeof(u64) * count);
    panic_if(!key, "out of memory");
    key[0] = KEY_TEMPLATE_LITERAL;
    key[1] = len;
    for (u32 i = 0; i < len; i++) {
        key[2 + 2 * i] = (u64)(uintptr_t)texts[i];
        key[3 + 2 * i] = types[i]->id;
    }
    key[2 + 2 * len] = (u64)(uintptr_t)texts[len];
    void const** slot = intern_slot(me, &my types, key, count);
    if (key != stack_key) free(key);
    if (!*slot) {
        u32 flags = TYF_TEMPLATE_LITERAL;
        for (u32 i = 0; i < len; i++) {
            flags |= types[i]->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES;
            if (ty_is_generic_index(types[i])) flags |= TYF_DEFERRED;
        }
        Ty* ret = ty_alloc(me, TY_TEMPLATE_LITERAL, flags);
        Atom const** texts_copy = arena_new_n(&my arena, Atom const*, len + 1);
        memcpy(texts_copy, texts, sizeof(Atom const*) * (len + 1));
        ret->template_literal.texts = texts_copy;
        ret->template_literal.types = ty_list_copy(me, types, len);
        intern_set(me, &my types, slot, ret);
    }
    return *slot;
}

Ty const* ty_template_literal(Checker* me, Atom const* const* texts, Ty const* const* types, u32 len) {
    // A union anywhere distributes, the first one first (never is the empty union)
    for (u32 i = 0; i < len; i++) {
        Ty const* t = types[i];
        if (t->flags & TYF_NEVER) return t;
        if (t->kind != TY_UNION) continue;
        // (tsc's checkCrossProductUnion: a never anywhere makes it empty)
        u64 product = 1;
        for (u32 j = 0; j < len && product && product < MAX_CROSS_PRODUCT; j++) {
            if (types[j]->kind == TY_UNION) product *= types[j]->composite.types.len;
            else if (types[j]->flags & TYF_NEVER) product = 0;
        }
        if (product >= MAX_CROSS_PRODUCT) {
            ty_error_at_current_node_d(me, 2590, "Expression produces a union type that is too complex to represent.");
            return ty_intrinsic(me, TY_ERROR);
        }
        Ty const** replaced = malloc(sizeof(Ty const*) * len);
        panic_if(!replaced, "out of memory");
        memcpy(replaced, types, sizeof(Ty const*) * len);
        TyBuf results = {0};
        for (u32 m = 0; m < t->composite.types.len; m++) {
            replaced[i] = t->composite.types.data[m];
            ty_buf_push(&results, ty_template_literal(me, texts, replaced, len));
        }
        Ty const* ret = ty_union(me, results.data, results.len, nullptr, (TyList){0});
        ty_buf_drop(&results);
        free(replaced);
        return ret;
    }

    TemplateBuilder b = { .text = cstr_null };
    cstr_append_sv(&b.text, atom_sv(texts[0]));
    Ty const* ret = nullptr;
    if (!add_spans(me, &b, texts, types, len)) {
        if (b.too_large) {
            ty_instantiation_limit_reached(me);
            ret = ty_intrinsic(me, TY_ERROR);
        } else {
            ret = ty_intrinsic(me, TY_STRING);
        }
    } else if (!b.len) {
        combine_surrogate_pairs(&b.text);
        ret = ty_string_literal(me, cstr_sv(&b.text));
    } else {
        builder_push(me, &b, nullptr); // The last text
        b.len--;
        bool all_empty = true;
        for (u32 i = 0; i <= b.len && all_empty; i++) all_empty = b.texts[i]->len == 0;
        if (all_empty) {
            bool all_string = true;
            for (u32 i = 0; i < b.len && all_string; i++) all_string = b.types[i]->flags & TYF_STRING;
            if (all_string) {
                ret = ty_intrinsic(me, TY_STRING);
            } else if (b.len == 1 && ty_is_pattern_literal(b.types[0])) {
                ret = b.types[0]; // `${Uppercase<string>}` is Uppercase<string>
            }
        }
        if (!ret) ret = make_template(me, b.texts, b.types, b.len);
    }
    cstr_drop(&b.text);
    free(b.texts);
    free(b.types);
    return ret;
}

// String mappings

// tsc's applyStringMapping
static Atom const* apply_mapping(Checker* me, TyStringMapping mapping, csview sv) {
    cstr out = cstr_null;
    switch (mapping) {
        case TY_MAPPING_UPPERCASE: unicode_to_upper_js(&out, sv); break;
        case TY_MAPPING_LOWERCASE: unicode_to_lower_js(&out, sv); break;
        case TY_MAPPING_CAPITALIZE:
        case TY_MAPPING_UNCAPITALIZE: {
            // (The first character alone: Final_Sigma doesn't see the rest)
            isize first = unicode_first_char_size(sv);
            csview head = csview_from_n(sv.buf, first);
            if (mapping == TY_MAPPING_CAPITALIZE) unicode_to_upper_js(&out, head);
            else unicode_to_lower_js(&out, head);
            cstr_append_n(&out, sv.buf + first, sv.size - first);
        }   break;
    }
    Atom const* ret = checker_atom(me, cstr_sv(&out));
    cstr_drop(&out);
    return ret;
}

static Ty const* make_string_mapping(Checker* me, TyStringMapping mapping, Symbol const* symbol, Ty const* type) {
    u64 key[3] = { KEY_STRING_MAPPING, mapping, type->id };
    void const** slot = intern_slot(me, &my types, key, 3);
    if (!*slot) {
        u32 flags = TYF_STRING_MAPPING | (type->flags & TYF_COULD_CONTAIN_TYPE_VARIABLES);
        if (ty_is_generic_index(type) || ((type->flags & (TYF_TEMPLATE_LITERAL | TYF_STRING_MAPPING)) && (type->flags & TYF_DEFERRED))) {
            flags |= TYF_DEFERRED;
        }
        Ty* ret = ty_alloc(me, TY_STRING_MAPPING, flags);
        ret->string_mapping.mapping = mapping;
        ret->string_mapping.symbol = symbol;
        ret->string_mapping.type = type;
        intern_set(me, &my types, slot, ret);
    }
    return *slot;
}

Ty const* ty_string_mapping(Checker* me, TyStringMapping mapping, Symbol const* symbol, Ty const* type) {
    if (type->flags & TYF_NEVER) return type;
    if (type->kind == TY_UNION) {
        TyBuf results = {0};
        for (u32 i = 0; i < type->composite.types.len; i++) {
            ty_buf_push(&results, ty_string_mapping(me, mapping, symbol, type->composite.types.data[i]));
        }
        Ty const* ret = ty_union(me, results.data, results.len, nullptr, (TyList){0});
        ty_buf_drop(&results);
        return ret;
    }
    if (type->kind == TY_LITERAL && (type->flags & TYF_STRING_LITERAL)) {
        return ty_string_literal(me, atom_sv(apply_mapping(me, mapping, atom_sv(type->literal.string))));
    }
    if (type->kind == TY_TEMPLATE_LITERAL) {
        // tsc's applyTemplateStringMapping: every text and type for the case changes, the first one for (un)capitalize
        u32 len = type->template_literal.types.len;
        Atom const** texts = malloc(sizeof(Atom const*) * (len + 1));
        Ty const** types = malloc(sizeof(Ty const*) * (len ? len : 1));
        panic_if(!texts || !types, "out of memory");
        memcpy(texts, type->template_literal.texts, sizeof(Atom const*) * (len + 1));
        memcpy(types, type->template_literal.types.data, sizeof(Ty const*) * len);
        bool whole = mapping == TY_MAPPING_UPPERCASE || mapping == TY_MAPPING_LOWERCASE;
        if (whole) {
            for (u32 i = 0; i <= len; i++) texts[i] = apply_mapping(me, mapping, atom_sv(texts[i]));
            for (u32 i = 0; i < len; i++) types[i] = ty_string_mapping(me, mapping, symbol, types[i]);
        } else if (texts[0]->len) {
            texts[0] = apply_mapping(me, mapping, atom_sv(texts[0]));
        } else {
            types[0] = ty_string_mapping(me, mapping, symbol, types[0]);
        }
        Ty const* ret = ty_template_literal(me, texts, types, len);
        free(texts);
        free(types);
        return ret;
    }
    if (type->kind == TY_STRING_MAPPING && type->string_mapping.mapping == mapping) return type;
    if ((type->flags & (TYF_ANY | TYF_STRING | TYF_STRING_MAPPING)) || ty_is_generic_index(type)) {
        return make_string_mapping(me, mapping, symbol, type);
    }
    if (ty_is_pattern_placeholder(type)) {
        // Uppercase<`${number}`>
        Atom const* empty = my atoms.empty;
        Atom const* texts[2] = { empty, empty };
        return make_string_mapping(me, mapping, symbol, ty_template_literal(me, texts, &type, 1));
    }
    return type;
}

// Matching

// JavaScript's `+s`: false if that is NaN
static bool js_string_to_number(csview s, double* out) {
    char const* p = s.buf;
    char const* end = s.buf + s.size;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')) p++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r' || end[-1] == '\v' || end[-1] == '\f')) end--;
    double value = 0;
    if (p == end) {
        value = 0;
    } else if (end - p > 2 && p[0] == '0' && strchr("xXoObB", p[1])) {
        int base = (p[1] == 'x' || p[1] == 'X') ? 16 : (p[1] == 'o' || p[1] == 'O') ? 8 : 2;
        for (char const* q = p + 2; q < end; q++) {
            int digit = *q >= '0' && *q <= '9' ? *q - '0' : *q >= 'a' && *q <= 'f' ? *q - 'a' + 10 : *q >= 'A' && *q <= 'F' ? *q - 'A' + 10 : 99;
            if (digit >= base) return false;
            value = value * base + digit;
        }
    } else {
        char const* q = p;
        if (q < end && (*q == '+' || *q == '-')) q++;
        if (end - q == 8 && memcmp(q, "Infinity", 8) == 0) {
            *out = p[0] == '-' ? -INFINITY : INFINITY;
            return true;
        }
        u32 digits = 0;
        while (q < end && *q >= '0' && *q <= '9') { q++; digits++; }
        if (q < end && *q == '.') {
            q++;
            while (q < end && *q >= '0' && *q <= '9') { q++; digits++; }
        }
        if (!digits) return false;
        if (q < end && (*q == 'e' || *q == 'E')) {
            q++;
            if (q < end && (*q == '+' || *q == '-')) q++;
            u32 exponent_digits = 0;
            while (q < end && *q >= '0' && *q <= '9') { q++; exponent_digits++; }
            if (!exponent_digits) return false;
        }
        if (q != end) return false;
        cstr copy = cstr_from_n(p, end - p);
        value = strtod(cstr_str(&copy), nullptr);
        cstr_drop(&copy);
    }
    *out = value;
    return true;
}

// JavaScript's `+s` is a finite number, and `"" + (+s) === s` if round_trip (tsc's isValidNumberString)
static bool is_valid_number_string(csview s, bool round_trip) {
    double value;
    if (!s.size || !js_string_to_number(s, &value) || !isfinite(value)) return false;
    if (!round_trip) return true;
    cstr back = cstr_null;
    Value v = { .tag = VALUE_NUMBER, .number = value };
    cstr_append_value_js(&back, &v);
    bool ret = csview_eq(&(csview){ .buf = cstr_str(&back), .size = cstr_size(&back) }, &s);
    cstr_drop(&back);
    return ret;
}

// tsc's isValidBigIntString without round tripping: `-`, then a bigint literal without its `n`
static bool is_valid_bigint_string(csview s) {
    char const* p = s.buf;
    char const* end = s.buf + s.size;
    if (p < end && *p == '-') p++;
    if (p == end) return false;
    if (end - p > 2 && p[0] == '0' && strchr("xXoObB", p[1])) {
        int base = (p[1] == 'x' || p[1] == 'X') ? 16 : (p[1] == 'o' || p[1] == 'O') ? 8 : 2;
        for (char const* q = p + 2; q < end; q++) {
            int digit = *q >= '0' && *q <= '9' ? *q - '0' : *q >= 'a' && *q <= 'f' ? *q - 'a' + 10 : *q >= 'A' && *q <= 'F' ? *q - 'A' + 10 : 99;
            if (digit >= base) return false;
        }
        return true;
    }
    if (*p == '0') return end - p == 1;
    for (char const* q = p; q < end; q++) {
        if (*q < '0' || *q > '9') return false;
    }
    return true;
}

// The value of a valid bigint string (is_valid_bigint_string()): its text, which `s` (an atom's) outlives
static Value bigint_string_value(csview s) {
    bool negative = s.size && s.buf[0] == '-';
    if (negative) s = csview_slice(s, 1, s.size);
    return value_bigint(s.buf, (u32)s.size, negative);
}

// tsc's inferToTemplateLiteralType, for a string literal matched by a placeholder that is a type variable whose
// constraint (not `string`) has other kinds of placeholders: the literal of the best of them (`"12"` is 12 for `infer N
// extends number`). Null if there's none
Ty const* ty_template_literal_for_constraint(Checker* me, Ty const* source, Ty const* constraint) {
    if (constraint->flags & TYF_ANY) return nullptr;
    Ty const* const* members = constraint->kind == TY_UNION ? constraint->composite.types.data : &constraint;
    u32 count = constraint->kind == TY_UNION ? constraint->composite.types.len : 1;
    u32 all = 0;
    for (u32 i = 0; i < count; i++) all |= members[i]->flags;
    if (all & TYF_STRING) return nullptr;
    csview str = atom_sv(source->literal.string);
    u32 const number_like = TYF_NUMBER | TYF_NUMBER_LITERAL | TYF_ENUM;
    u32 const bigint_like = TYF_BIGINT | TYF_BIGINT_LITERAL;
    if ((all & number_like) && !is_valid_number_string(str, true)) all &= ~number_like;
    if ((all & bigint_like) && !is_valid_bigint_string(str)) all &= ~bigint_like;
    // (bigint round trip: a valid one without leading zeros or a prefix)
    if ((all & bigint_like) && str.size > 1 && (str.buf[0] == '0' || (str.buf[0] == '-' && (str.buf[1] == '0' || str.size == 1)))) all &= ~bigint_like;
    double number = 0;
    if (all & number_like) js_string_to_number(str, &number);
    Ty const* left = ty_intrinsic(me, TY_NEVER);
    for (u32 i = 0; i < count; i++) {
        Ty const* right = members[i];
        u32 lf = left->flags;
        u32 rf = right->flags;
        if (!(rf & all)) continue;
        if (lf & TYF_STRING) continue;
        if (rf & TYF_STRING) { left = source; continue; }
        if (lf & TYF_TEMPLATE_LITERAL) continue;
        if ((rf & TYF_TEMPLATE_LITERAL) && ty_template_matches(me, source, right)) { left = source; continue; }
        if (lf & TYF_STRING_MAPPING) continue;
        if ((rf & TYF_STRING_MAPPING) && apply_mapping(me, right->string_mapping.mapping, str) == source->literal.string) {
            left = source;
            continue;
        }
        if (lf & TYF_STRING_LITERAL) continue;
        if ((rf & TYF_STRING_LITERAL) && right->literal.string == source->literal.string) { left = right; continue; }
        if (lf & TYF_NUMBER) continue;
        if (rf & TYF_NUMBER) { left = ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = number }); continue; }
        if (lf & TYF_ENUM) continue;
        if (rf & TYF_ENUM) { left = ty_literal(me, &(Value){ .tag = VALUE_NUMBER, .number = number }); continue; }
        if (lf & TYF_NUMBER_LITERAL) continue;
        if ((rf & TYF_NUMBER_LITERAL) && right->literal.value.number == number) { left = right; continue; }
        if (lf & TYF_BIGINT) continue;
        if (rf & TYF_BIGINT) { left = ty_literal(me, &(Value[]){ bigint_string_value(str) }[0]); continue; }
        if (lf & TYF_BIGINT_LITERAL) continue;
        if ((rf & TYF_BIGINT_LITERAL) && right == ty_literal(me, &(Value[]){ bigint_string_value(str) }[0])) { left = right; continue; }
        if (lf & TYF_BOOLEAN_LITERAL) continue;
        if ((rf & TYF_BOOLEAN_LITERAL) && csview_equals(str, right->literal.value.boolean ? "true" : "false")) { left = right; continue; }
        if (lf & TYF_UNDEFINED) continue;
        if ((rf & TYF_UNDEFINED) && csview_equals(str, "undefined")) { left = right; continue; }
        if (lf & TYF_NULL) continue;
        if ((rf & TYF_NULL) && csview_equals(str, "null")) { left = right; continue; }
    }
    return left->flags & TYF_NEVER ? nullptr : left;
}

bool ty_string_mapping_contains(Checker* me, Ty const* source, Ty const* target) {
    if (target->flags & TYF_ANY) return true;
    if (target->flags & (TYF_STRING | TYF_TEMPLATE_LITERAL)) return ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
    if (target->kind == TY_STRING_MAPPING) {
        // Applying the target's mappings to the source changes nothing, and the source fits what is mapped
        TyStringMapping stack[16];
        Symbol const* symbols[16];
        u32 count = 0;
        while (target->kind == TY_STRING_MAPPING && count < c_arraylen(stack)) {
            stack[count] = target->string_mapping.mapping;
            symbols[count++] = target->string_mapping.symbol;
            target = target->string_mapping.type;
        }
        Ty const* mapped = source;
        for (u32 i = count; i--;) mapped = ty_string_mapping(me, stack[i], symbols[i], mapped);
        return mapped == source && ty_string_mapping_contains(me, source, target);
    }
    return false;
}

static bool is_empty_type_literal(Checker* me, Ty const* ty) {
    if (ty->kind != TY_ANONYMOUS) return false;
    Members const* members = ty_members(me, ty);
    return !members->prop_count && !members->call_count && !members->construct_count && !members->index_count && !members->mapped;
}

// The comparison of a relation in progress, or a fresh assignability one (tsc's compareTypes: a relater's
// isRelatedToWorker, whose recursion limits then reach through a template literal, or compareTypesAssignable)
static bool template_compare(Checker* me, TyCompare const* compare, Ty const* source, Ty const* target) {
    if (compare) return compare->fn(me, compare->ctx, source, target);
    return ty_is_related(me, source, target, TY_RELATION_ASSIGNABLE);
}

static bool template_matches(Checker* me, Ty const* source, Ty const* target, TyCompare const* compare);

// tsc's isValidTypeForTemplateLiteralPlaceholder
static bool placeholder_accepts(Checker* me, Ty const* source, Ty const* target, TyCompare const* compare) {
    if (target->kind == TY_INTERSECTION) {
        for (u32 i = 0; i < target->composite.types.len; i++) {
            Ty const* t = target->composite.types.data[i];
            if (!is_empty_type_literal(me, t) && !placeholder_accepts(me, source, t, compare)) return false;
        }
        return true;
    }
    if ((target->flags & TYF_STRING) || template_compare(me, compare, source, target)) return true;
    if (source->kind == TY_LITERAL && (source->flags & TYF_STRING_LITERAL)) {
        csview value = atom_sv(source->literal.string);
        if ((target->flags & TYF_NUMBER) && is_valid_number_string(value, false)) return true;
        if ((target->flags & TYF_BIGINT) && is_valid_bigint_string(value)) return true;
        if (target->kind == TY_LITERAL && (target->flags & TYF_BOOLEAN_LITERAL)) {
            return csview_equals(value, target->literal.value.boolean ? "true" : "false");
        }
        if (target->flags & TYF_NULL) return csview_equals(value, "null");
        if (target->flags & TYF_UNDEFINED) return csview_equals(value, "undefined");
        if (target->kind == TY_STRING_MAPPING) return ty_string_mapping_contains(me, source, target);
        if (target->kind == TY_TEMPLATE_LITERAL) return template_matches(me, source, target, compare);
        return false;
    }
    if (source->kind == TY_TEMPLATE_LITERAL) {
        return source->template_literal.types.len == 1 && !source->template_literal.texts[0]->len &&
            !source->template_literal.texts[1]->len &&
            template_compare(me, compare, source->template_literal.types.data[0], target);
    }
    return false;
}

static bool sv_starts_with(csview s, csview prefix) {
    return s.size >= prefix.size && memcmp(s.buf, prefix.buf, prefix.size) == 0;
}

static bool sv_ends_with(csview s, csview suffix) {
    return s.size >= suffix.size && memcmp(s.buf + s.size - suffix.size, suffix.buf, suffix.size) == 0;
}

static isize_t sv_find_from(csview s, csview needle, isize_t from) {
    if (from > s.size) return -1;
    for (isize_t i = from; i + needle.size <= s.size; i++) {
        if (memcmp(s.buf + i, needle.buf, needle.size) == 0) return i;
    }
    return -1;
}

typedef struct PartsMatch {
    Checker* checker;
    Atom const* const* texts; // of the source
    Ty const* const* types;
    u32 last; // Index of the last source text
    csview remaining_end; // The last source text without the target's end text
    u32 seg;
    isize_t pos;
    Ty const** out;
    u32 out_len;
} PartsMatch;

static csview parts_text(PartsMatch const* m, u32 index) {
    return index < m->last ? atom_sv(m->texts[index]) : m->remaining_end;
}

static void parts_add_match(PartsMatch* m, u32 s, isize_t p) {
    Checker* me = m->checker;
    Ty const* match;
    if (s == m->seg) {
        csview text = parts_text(m, s);
        match = ty_string_literal(me, csview_from_n(text.buf + m->pos, p - m->pos));
    } else {
        // A template of the source's parts in between
        u32 count = s - m->seg;
        Atom const** texts = malloc(sizeof(Atom const*) * (count + 1));
        panic_if(!texts, "out of memory");
        csview first = atom_sv(m->texts[m->seg]);
        texts[0] = checker_atom(me, csview_from_n(first.buf + m->pos, first.size - m->pos));
        for (u32 i = 1; i < count; i++) texts[i] = m->texts[m->seg + i];
        csview last = parts_text(m, s);
        texts[count] = checker_atom(me, csview_from_n(last.buf, p));
        match = ty_template_literal(me, texts, m->types + m->seg, count);
        free(texts);
    }
    m->out[m->out_len++] = match;
    m->seg = s;
    m->pos = p;
}

// tsc's inferFromLiteralPartsToTemplateLiteral: the source is texts around types (a string literal is one text)
static bool infer_from_parts(
    Checker* me, Atom const* const* source_texts, Ty const* const* source_types, u32 source_len, Ty const* target,
    Ty const** out
) {
    u32 last_source = source_len;
    csview source_start = atom_sv(source_texts[0]);
    csview source_end = atom_sv(source_texts[last_source]);
    Atom const* const* target_texts = target->template_literal.texts;
    u32 last_target = target->template_literal.types.len;
    csview target_start = atom_sv(target_texts[0]);
    csview target_end = atom_sv(target_texts[last_target]);
    if ((last_source == 0 && source_start.size < target_start.size + target_end.size) ||
        !sv_starts_with(source_start, target_start) || !sv_ends_with(source_end, target_end)
    ) {
        return false;
    }
    PartsMatch m = {
        .checker = me,
        .texts = source_texts,
        .types = source_types,
        .last = last_source,
        .remaining_end = csview_from_n(source_end.buf, source_end.size - target_end.size),
        .seg = 0,
        .pos = target_start.size,
        .out = out,
    };
    for (u32 i = 1; i < last_target; i++) {
        csview delim = atom_sv(target_texts[i]);
        if (delim.size > 0) {
            u32 s = m.seg;
            isize_t p = m.pos;
            while (true) {
                p = sv_find_from(parts_text(&m, s), delim, p);
                if (p >= 0) break;
                s++;
                if (s == source_len + 1) return false;
                p = 0;
            }
            parts_add_match(&m, s, p);
            m.pos += delim.size;
        } else if (m.pos < parts_text(&m, m.seg).size) {
            // One character
            csview text = parts_text(&m, m.seg);
            int size = cutf8_chr_size(text.buf + m.pos);
            if (size <= 0 || m.pos + size > text.size) size = 1;
            parts_add_match(&m, m.seg, m.pos + size);
        } else if (m.seg < last_source) {
            parts_add_match(&m, m.seg + 1, 0);
        } else {
            return false;
        }
    }
    parts_add_match(&m, last_source, parts_text(&m, last_source).size);
    return true;
}

// The string-like type a template placeholder stands for (tsc's getStringLikeTypeForType)
static Ty const* string_like_of(Checker* me, Ty const* ty) {
    if (ty->flags & (TYF_ANY | TYF_STRING_LIKE)) return ty;
    Atom const* empty = my atoms.empty;
    Atom const* texts[2] = { empty, empty };
    return ty_template_literal(me, texts, &ty, 1);
}

static Ty const* base_constraint_or_self(Checker* me, Ty const* ty) {
    Ty const* constraint = ty->kind == TY_TYPE_PARAM || (ty->flags & TYF_DEFERRED) ? ty_base_constraint(me, ty) : nullptr;
    return constraint ? constraint : ty;
}

static bool infer_from_template(Checker* me, Ty const* source, Ty const* target, Ty const** out, TyCompare const* compare) {
    if (source->kind == TY_LITERAL && (source->flags & TYF_STRING_LITERAL)) {
        return infer_from_parts(me, &source->literal.string, nullptr, 0, target, out);
    }
    if (source->kind != TY_TEMPLATE_LITERAL) return false;
    u32 len = source->template_literal.types.len;
    bool same_texts = len == target->template_literal.types.len;
    for (u32 i = 0; i <= len && same_texts; i++) same_texts = source->template_literal.texts[i] == target->template_literal.texts[i];
    if (same_texts) {
        for (u32 i = 0; i < len; i++) {
            Ty const* s = source->template_literal.types.data[i];
            Ty const* t = target->template_literal.types.data[i];
            Ty const* source_constraint = base_constraint_or_self(me, s);
            bool fits = template_compare(me, compare, source_constraint, base_constraint_or_self(me, t));
            out[i] = fits ? s : string_like_of(me, s);
        }
        return true;
    }
    return infer_from_parts(me, source->template_literal.texts, source->template_literal.types.data, len, target, out);
}

bool ty_infer_from_template(Checker* me, Ty const* source, Ty const* target, Ty const** out) {
    return infer_from_template(me, source, target, out, nullptr);
}

// tsc's isTypeMatchedByTemplateLiteralType
static bool template_matches(Checker* me, Ty const* source, Ty const* target, TyCompare const* compare) {
    u32 len = target->template_literal.types.len;
    Ty const* stack_buf[16];
    Ty const** inferred = len <= c_arraylen(stack_buf) ? stack_buf : malloc(sizeof(Ty const*) * len);
    panic_if(!inferred, "out of memory");
    bool ret = infer_from_template(me, source, target, inferred, compare);
    for (u32 i = 0; i < len && ret; i++) ret = placeholder_accepts(me, inferred[i], target->template_literal.types.data[i], compare);
    if (inferred != stack_buf) free(inferred);
    return ret;
}

bool ty_template_matches(Checker* me, Ty const* source, Ty const* target) {
    return template_matches(me, source, target, nullptr);
}

bool ty_template_matches_with(Checker* me, Ty const* source, Ty const* target, TyCompare const* compare) {
    return template_matches(me, source, target, compare);
}

bool ty_templates_definitely_unrelated(Ty const* source, Ty const* target) {
    csview source_start = atom_sv(source->template_literal.texts[0]);
    csview target_start = atom_sv(target->template_literal.texts[0]);
    csview source_end = atom_sv(source->template_literal.texts[source->template_literal.types.len]);
    csview target_end = atom_sv(target->template_literal.texts[target->template_literal.types.len]);
    isize_t start_len = source_start.size < target_start.size ? source_start.size : target_start.size;
    isize_t end_len = source_end.size < target_end.size ? source_end.size : target_end.size;
    return memcmp(source_start.buf, target_start.buf, start_len) != 0 ||
        memcmp(source_end.buf + source_end.size - end_len, target_end.buf + target_end.size - end_len, end_len) != 0;
}
