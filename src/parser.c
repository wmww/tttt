#include "parser.h"
#include <pthread.h>
#include <stdatomic.h>
#include "unicode_identifiers.h"
#include "scope.h"
#include "block.h"
#include "interface.h"
#include "interface_member.h"
#include "span.h"
#include "modifier_list.h"
#include "statement.h"
#include "port_statement.h"
#include "several_types.h"
#include "generic.h"
#include "class_decl.h"
#include "enum_decl.h"
#include "interface_decl.h"
#include "callable_type.h"
#include "type.h"
#include "error.h"
#include "flow.h"
#include "source_file.h"
#include "math_helpers.h"
#include <math.h>
#include "string_helpers.h"

#define g_type VecU8, vec_u8
#define g_key uint8_t, u8
#include <cleat/generic/vec.h>

typedef struct JsDocRange {
    const char* start; // The `/**`
    const char* end; // Past the `*/`
    // Its declaration tags (`@typedef`, `@callback`, `@import`) were bound by the innermost statement list containing
    // it (jsdoc_bind_comments())
    bool claimed;
} JsDocRange;
#define g_key JsDocRange, jsdoc_range
#include <cleat/generic/vec.h>

#define MAX_ERRORS 500 // Must be less than the max value storable in Parser.error_count

typedef enum WhitespaceType {
    WHITESPACE_NONE = 0,
    WHITESPACE_PARSED,
    WHITESPACE_NEWLINE,
} WhitespaceType;

typedef struct FirstPassStatement {
    const char* start;
    bool await;
} FirstPassStatement;

typedef struct ParserShared {
    SourceFile const* source;
    char const* text; // The source's text (TypeNode.pos is an offset in it), null where nothing reads TypeNode.pos
    // A `/*` without its `*/` was hopped (the comment goes to the end of the file): TS1010 at the end, once
    bool unterminated_comment;
    const char* fail_cache_start;
    VecU8 fail_cache;
    VecError errors;
    // The last scanner error about a token (an unterminated literal, an identifier after a number) and that token's
    // start: tsc's scanner reports it as soon as the token is scanned, before the parser's errors at that token, which
    // decides which error the next one at its position is deduplicated against (see last_error_starts_at())
    isize scanner_error_index;
    isize scanner_error_first; // The errors before the token was read
    const char* scanner_error_token;
    // tsgo's DisallowIn context: set for a `for (` statement's initializer and cleared (saved and restored: allow_in_enter()) only where
    // tsgo clears it, so it reaches into arrow parameters, array literals and nested function and class bodies there
    // (`for (var f = function () { x in b };;)` is a syntax error in tsgo)
    bool disallow_in;
    bool using_forced; // The next variable statement starts with `using` whatever follows it (see parse_decl_statement())
    // After modifiers `let` is a variable statement whatever follows (tsc's parseDeclarationWorker, reached without
    // isLetDeclaration's lookahead: `declare let` then a line break and `=` is an empty list)
    bool let_forced;
    // After decorators the keyword decides the declaration, without the lookahead a statement takes (tsc's
    // parseDeclarationWorker, reached from parseDeclaration on `@`): `@dec interface\nI {}` is an interface, `@dec
    // namespace = 1` one missing its name (TS1003). Set by parse_decl_statement() for the declaration parser it tries
    // next, which takes it (take_decl_committed())
    bool decl_committed;
    // parse_modifier_list_ex() after decorators: every modifier keyword is one (tsc's parseModifiersEx), `@dec in class`
    bool modifiers_after_decorators;
    // The modifiers of the declaration parse_decl_statement() tries next fail tsc's checkGrammarModifiers, which ends
    // the grammar checks of the declaration's own parts (checkVariableStatement's list, a class's or interface's
    // heritage clauses: `public class B extends A, {}` has no TS1009). The declaration parser takes it at its start
    bool decl_modifiers_failed;
    // The statement's decorators came before `export` (a class with its own after it: TS8038, a modifier error too)
    bool decl_leading_decorators;
    // What kind of function the parser is currently inside, decides if `await` and `yield` are operators
    bool in_async;
    bool in_generator;
    int function_depth;
    bool in_static_block; // Directly in a class `static {}` block, not in a function inside it
    bool in_parameters; // In the parameter list of the current function: `yield`/`await` there are TS2523/TS2524
    int jsx_children_depth; // Inside the children of a JSX element (at any depth): see parse_jsx_attributes()
    // A conflict marker ended the JSX children (tsc's scanJsxToken gives a ConflictMarkerTrivia token that starts with
    // the text before it): where that token starts, for the closing tag's '</' expected. Null otherwise
    const char* jsx_conflict_start;
    // Where the last JSX child element ended without its tag's final `>` (tsgo's parser token then stays the ordinary
    // token there, which its next child's rescan doesn't update: parse_jsx_element()'s `stale`). Null otherwise
    const char* jsx_stale_end;
    // What moduleDetection looks at (SourceFile.has_import_meta, has_jsx): set by attempts that backtrack too
    bool saw_import_meta;
    bool saw_jsx;
    // `this` keywords (types, predicates, expressions) parsed so far, attempts that backtrack included: an interface
    // declaration that saw one is tsc's NodeFlags.ContainsThis (InterfaceDecl.contains_this)
    u32 this_count;
    // An `await` outside functions (an identifier, or an operator where it can only be one): a module file is parsed
    // again with the top level an await context, `top_level_await` (tsc's reparseTopLevelAwait)
    bool saw_top_level_await;
    bool top_level_await;
    // Parsing a computed property name: an `await` identifier there doesn't make its statement one tsc reparses in an
    // await context (tsc's parsePropertyName saves and restores statementHasAwaitIdentifier): `{ [await]: 1 }` at a
    // module's top level is TS1262 and TS2304
    u32 in_property_name;
    // The file's statements from the first one with an `await` identifier on (malloc'd), each with whether it has one:
    // the first pass records them, the second parses each run of statements with one as an await context, and on
    // until a statement ends where the next statement without one started (tsc's reparseTopLevelAwait)
    struct FirstPassStatement* await_statements;
    u32 await_statement_count; // Of those, the ones with an `await` identifier
    u32 first_pass_count;
    u32 await_statement_cursor; // The second pass: the first recorded statement that may still start a section
    bool in_await_section;
    i32 await_section_next; // In a section: the recorded statement without an `await` it ends at, -1 for the file's end
    // in_async around the function being parsed: its parameters' decorators are in that await context (tsc's
    // parseParameterWorker, inOuterAwaitContext)
    bool outer_in_async;
    bool saw_commonjs; // A `require()` call, an assignment to `exports.x` or `module.exports` (a JavaScript file's)
    // JavaScript files: every `/** */` comment hopped so far (not `/**/`), sorted by start. Hosts find theirs by looking
    // back from their first token (jsdoc_before())
    VecJsDocRange jsdoc_comments;
    // Where declarations are emitted: every comment is recorded (SourceFile.comments)
    bool record_comments;
    // Parsing a deferred function body (plan 78, parse_deferred_body()): its comments were recorded when it was skipped,
    // and nothing in it is deferred
    bool materializing;
    // Parsing a type inside a JSDoc comment: a line's leading `*` is whitespace, `*/` ends everything, the JSDoc-only
    // type forms (`?T`, `T=`, `*`, `function(...)`...) are allowed
    bool in_jsdoc;
    const char* jsdoc_end; // The `*/` of the comment being parsed, with in_jsdoc
    // In the type of a tag tsc's checker doesn't visit (`@returns`, `@throws`: checkSourceElementWorker has no case for
    // them), whose grammar errors (TS1099, TS7014) aren't reported
    bool jsdoc_unchecked;
    // Applying JSDoc to a function declaration or class method (not a function expression): a `@type` signature is its
    // signature (tsc's getSignatureOfTypeTag in getSignaturesOfSymbol), not moved into its parameters
    bool jsdoc_declaration;
    // The last postfix `T?` parsed in JSDoc, from T's start to after the `?`: a tuple element that is exactly this is
    // an optional element (tsc's parseTupleElementType)
    const char* jsdoc_postfix_nullable_start;
    const char* jsdoc_postfix_nullable_end;
    u32 parsing_context; // The lists being parsed, a bit per ParsingContext
    // Where the statements being parsed are, for the modifier checks (modifier_list_check_grammar())
    bool in_ambient; // tsc's NodeFlags.Ambient context: a declaration file, a `declare` declaration's insides
    // Inside the body of a `with` statement, which tsc's checkWithStatement never checks: no checkGrammarModifiers
    u32 with_body_depth;
    // tsc's checkGrammarTypeOperatorNode: a `unique symbol` is only allowed as the whole annotation of a variable, a
    // class property or a property signature. unique_annotation_next: the next parse_binding_type_ex() is such an
    // annotation; unique_allowed_at: where its type starts (parentheses moving it in), unique_allowed_found: set to
    // it when a `unique symbol` is found there, which must turn out to be the whole type
    bool unique_annotation_next;
    char const* unique_allowed_at;
    char const* unique_allowed_found;
    // Where tsc's NonTextFileMarkerTrivia starts (a U+FFFD, TS1490): a token to the end of the file, which the
    // statement list that gets there says isn't a statement (TS1128). Null if none or once said
    char const* binary_marker;
    // Parsing the type arguments of `import<...>`: TS1326 replaces their own grammar errors (TS1099)
    bool import_type_args;
    // The next type argument list parse_generic_params() parses is an import type's (`import("./a").G<1,>`), whose
    // grammar tsc never checks (checkImportType has no checkGrammarTypeArguments): taken at its start
    bool unchecked_type_args;
    // A word with a unicode escape was parsed (note_escaped_names()): only then do keyword probes cook words
    bool any_escaped_word;
    // The type member being parsed can't be a mapped type's `[K in T]` (tsc's isStartOfMappedType looks only at a type
    // literal's first member): it's a property named by an `in` expression
    bool no_mapped_member;
    // tsc's DisallowConditionalTypes context: parsing a conditional type's extends type or an `infer X extends C`
    // constraint, where a type operator's operands can't be conditional types (a primary type's insides can)
    bool disallow_conditional;
    // The type parse_type_inner() parses next is a type operator's operand (`keyof`'s): no constructor type there
    bool type_operator_operand;
    // parse_modifier_list_ex() parses a type member's modifiers: `static {` is no static block there
    bool modifiers_in_type;
    // The next identifier type is a heritage element's (tsc's parseExpressionWithTypeArguments, tryParseTypeArguments):
    // its type arguments want a `<` token, not a `<<`. Cleared by the identifier type that reads it
    bool heritage_type_args;
    // The next identifier type is a JSDoc `@augments`/`@implements` class (tsc's
    // parseExpressionWithTypeArgumentsForAugments): the `<` after its name is the JSDoc scanner's one-character token,
    // `A<=` has type arguments. Cleared by the identifier type that reads it
    bool jsdoc_augments_type_args;
    // parse_generic_params()'s `<` is a one-character token whatever follows it (set and cleared around one call)
    bool single_less_than;
    // The parameters parse_function_args() parses next are a type member's (set and cleared around one call)
    bool type_member_args;
    // The import or export declaration parse_import_export_statement() parses next follows decorators: it is one
    // whatever follows the keyword (tsc's parseDeclarationWorker), `@dec import("./m")` has no clause and an expression
    // for a module specifier, `@dec export;` misses its `{`
    bool import_export_committed;
    // The `{` of the innermost Block (statement block, function body) being parsed, for TernaryType.enclosing_block
    const char* block_open;
    ModifiedParent statement_parent;
    bool in_namespace_body; // The module block is a `namespace N {}`'s, not an ambient module's
    u32 ambient_module_depth; // Inside `declare module "m" {}` bodies (augmentations too)
    u32 global_block_depth; // Inside `global {}` bodies
    // The end of the file's last token (tsc's full start of the end-of-file token): where a missing expression at the
    // end of the file is reported. The smallest position trivia was skipped from up to the end
    const char* eof_full_start;
    // The trivia hop_whitespace_slow() skipped last: token_full_start()
    const char* last_trivia_start;
    const char* last_trivia_end;
    u32 errors_attempted; // Every parse_error() call, the ones dropped too (a position that already has one)
    // The parse errors of JSDoc comments (tsc's jsDocDiagnostics): kept whether or not the attempt that parsed the
    // comment succeeds (a comment is parsed by every attempt that parses its host), deduplicated at the end, and only
    // reported in checked JavaScript
    VecError jsdoc_errors;
    // A chunk of a file parsed on its own (parse_file_chunked()): what the parse would write into the SourceFile is
    // logged in it instead, and the file-level statement list stops once a statement ends at or past chunk_stop
    // (chunk_reached: where it did). Null for a whole file
    struct ParseChunk* chunk;
    const char* chunk_stop;
    const char* chunk_reached;
} ParserShared;

typedef struct FunctionFlags {
    bool is_async;
    bool is_generator;
    Span asterisk; // The `*` of a generator
    // What came before commits to a function (`function f`, a member's `*`, `constructor`): a missing parameter list
    // is TS1005 '(' expected and the function goes on without one (tsc's parseParameters)
    bool committed;
    // A body that isn't there and can't be left out is '{' expected (object literal methods and accessors, tsc's
    // parseFunctionBlockOrSemicolon without a message), not TS1144
    bool brace_expected;
    // A function expression or object literal method (or `export default function`): see skip_body()
    bool expression;
} FunctionFlags;

static const FunctionFlags function_flags_null = {0};

typedef struct FunctionCtx {
    bool in_async;
    bool outer_in_async;
    bool in_generator;
    bool in_static_block;
    bool in_parameters;
} FunctionCtx;

void parser_shared_drop(ParserShared* me) {
    vec_u8_drop(&my fail_cache);
    vec_jsdoc_range_drop(&my jsdoc_comments);
    vec_error_drop(&my errors);
    vec_error_drop(&my jsdoc_errors);
    free(my await_statements);
}

typedef struct ExprCtx {
    bool reject_operators;
    bool left_hand_side; // tsc's parseLeftHandSideExpressionOrHigher: no unary operators either (a heritage element)
    bool entity_name; // The operand of a `typeof` type: `a.b.c<T>`, no calls and no `[i]` (that is an indexed access type)
    bool decorator; // `@dec [x]`: the `[` starts a computed member name, not an element access (tsc's inDecoratorContext)
    // A conditional's true branch (tsc's allowReturnTypeInArrowFunction false): an arrow function there only has a
    // return type if a `:` follows it, `a ? (b) : c => d` is `a ? b : (c => d)`
    bool is_parsing_ternary;
    int min_precedence;
} ExprCtx;

static const ExprCtx expr_ctx_null = {0};

typedef enum FailCacheFlag {
    FAIL_CACHE_EXPR = 1 << 0,
    FAIL_CACHE_ARROW_FUNCTION = 1 << 1,
    FAIL_CACHE_TYPE = 1 << 2,
} FailCacheFlag;

typedef struct Parser Parser;
typedef struct Parser {
    ParserShared* shared; // State that is shared across all parsers parsing a single document
    Parser* parent; // the parser this parser was created from (and will merge it's result into on success)
    const char* pos; // current position in the code
    WhitespaceType whitespace; // the type of whitespace detected in the most recent call to hop_whitespace()
    const char* whitespace_end; // the end of the most recent whitespace (whitespace type is retained if same as pos)
    uint8_t fail_cache_flag;
    uint16_t error_count; // Number of errors added to this parser, includes children only after they succeed
#ifdef DEBUG_BUILD
    bool has_panding_child;
#endif
} Parser;

typedef enum TypePrecedence {
    TYPE_PRECEDENCE_NULL = 0,
    TYPE_PRECEDENCE_TERNARY,
    TYPE_PRECEDENCE_UNION,
    TYPE_PRECEDENCE_INTERSECTION,
    TYPE_PRECEDENCE_OPERATOR, // The operand of `keyof`: `keyof A & B` is `(keyof A) & B`, `keyof A[]` is `keyof (A[])`
} TypePrecedence;

static bool hop_token(Parser* parent);

// tsc's isExternalModuleNameRelative: `.`/`..` segments first, or a rooted path
static bool is_relative_module_name(csview path) {
    if (csview_starts_with(path, "/") || csview_starts_with(path, "\\")) return true;
    if (path.size >= 3 && path.buf[1] == ':' && (path.buf[2] == '/' || path.buf[2] == '\\')) return true;
    if (!path.size || path.buf[0] != '.') return false;
    isize dots = path.size >= 2 && path.buf[1] == '.' ? 2 : 1;
    return path.size == dots || path.buf[dots] == '/' || path.buf[dots] == '\\';
}

// What a chunk's parse would have done to its SourceFile (parse_file_chunked()), done in chunk order once every chunk
// parsed: the same calls in the same order as one parse of the file
typedef enum ChunkEffectKind {
    CHUNK_EFFECT_MODULE_REF,
    CHUNK_EFFECT_COMMENT_DIRECTIVE,
    CHUNK_EFFECT_DEPRECATED_JSDOC,
    CHUNK_EFFECT_ESCAPED_NAMES,
    CHUNK_EFFECT_DEFERRED_BODIES,
} ChunkEffectKind;

typedef struct ChunkEffect {
    uint8_t kind;
    bool flag; // A comment directive's expect_error
    const char* start;
    const char* end;
    ModuleRef* ref;
} ChunkEffect;

typedef struct ParseChunk {
    const char* start;
    const char* stop; // Where the next chunk starts, null for the last
    bool first;
    bool helped; // Parsed by another thread than the file's, into arena
    AstArena arena;
    VecStatement statements;
    ChunkEffect* effects;
    u32 effect_count, effect_cap;
    bool saw_import_meta, saw_commonjs, saw_jsx;
    const char* failure; // Why the chunk's parse can't stand for the file's (null: it can)
} ParseChunk;

static void chunk_effect(ParserShared* shared, ChunkEffect effect) {
    ParseChunk* chunk = shared->chunk;
    if (chunk->effect_count == chunk->effect_cap) {
        chunk->effect_cap = chunk->effect_cap ? chunk->effect_cap * 2 : 16;
        chunk->effects = realloc(chunk->effects, sizeof(ChunkEffect) * chunk->effect_cap);
        panic_if(!chunk->effects, "out of memory");
    }
    chunk->effects[chunk->effect_count++] = effect;
}

// Takes path. The file owns it: what a failed parse attempt leaves behind is merely one more file looked for
static ModuleRef* add_module_ref(Parser* p, cstr path, Span span) {
    if (p->shared->chunk) {
        ModuleRef* ret = c_new(ModuleRef, { .path = path, .span = span });
        chunk_effect(p->shared, (ChunkEffect){ .kind = CHUNK_EFFECT_MODULE_REF, .ref = ret });
        return ret;
    }
    return source_file_add_module_ref((SourceFile*)p->shared->source, path, span);
}

// Its names are cooked from now on (name_cooked())
static void note_escaped_names(ParserShared* shared) {
    shared->any_escaped_word = true;
    if (shared->chunk) chunk_effect(shared, (ChunkEffect){ .kind = CHUNK_EFFECT_ESCAPED_NAMES });
    else names_register_source((SourceFile*)shared->source);
}

static void append_code_point(cstr* str, u32 code) {
    // A surrogate is kept as its WTF-8 encoding (three bytes, ED A0-BF xx), which the printer escapes (`"\uD800"`); a
    // low one right after a high one is the pair's character (`"\uD83D\uDE00"` is the emoji)
    if (code >= 0xD800 && code <= 0xDFFF) {
        isize size = cstr_size(str);
        const char* data = cstr_str(str);
        if (code >= 0xDC00 && size >= 3 && (unsigned char)data[size - 3] == 0xED &&
            (unsigned char)data[size - 2] >= 0xA0 && (unsigned char)data[size - 2] <= 0xAF
        ) {
            u32 high = 0xD000 | ((u32)((unsigned char)data[size - 2] & 0x3F) << 6) | ((unsigned char)data[size - 1] & 0x3F);
            cstr_resize(str, size - 3, '\0');
            code = 0x10000 + ((high - 0xD800) << 10) + (code - 0xDC00);
        } else {
            char wtf8[3] = { (char)0xED, (char)(0x80 | ((code >> 6) & 0x3F)), (char)(0x80 | (code & 0x3F)) };
            cstr_append_n(str, wtf8, 3);
            return;
        }
    }
    char buf[8];
    int n = cutf8_encode(buf, code);
    cstr_append_n(str, buf, n);
}

// The escape sequence after a backslash (p is on the character after it): appends its value and moves past it.
// False for a malformed \x or \u escape (nothing consumed beyond the letter, the caller reports)
// The errors of an invalid escape (tsc's scanEscapeSequence), which the caller reports unless it's in a tagged template
typedef struct EscapeErrors {
    u32 count;
    struct { u32 code; const char* at; u32 len; u32 value; } errors[2];
} EscapeErrors;

static void escape_error(EscapeErrors* errors, u32 code, const char* at, u32 len) {
    if (errors->count < c_arraylen(errors->errors)) errors->errors[errors->count++] = (typeof(errors->errors[0])){ code, at, len, 0 };
}

// After the backslash. An invalid escape stands for its own text (`\u{` in "\u{-1}", like tsc's) and fills `errors`.
// In a tagged template a legacy octal or `\8` is its own text too (tsc's reScanTemplateToken scans them without
// ReportInvalidEscapeErrors, so they aren't cooked: tag`\5` is "\\5")
static bool parse_string_escape(Parser* p, cstr* str, EscapeErrors* errors, bool tagged) {
    const char* start = p->pos - 1;
    char c = *p->pos;
    p->pos++;
    switch (c) {
        case 'n': cstr_append_lit(str, "\n"); return true;
        case 'r': cstr_append_lit(str, "\r"); return true;
        case 't': cstr_append_lit(str, "\t"); return true;
        case 'b': cstr_append_lit(str, "\b"); return true;
        case 'f': cstr_append_lit(str, "\f"); return true;
        case 'v': cstr_append_lit(str, "\v"); return true;
        case '0':
            if (!(*p->pos >= '0' && *p->pos <= '9')) {
                cstr_append_n(str, "\0", 1);
                return true;
            }
            // (`\08` is `\0` and `8`, but still a legacy octal escape)
            [[fallthrough]];
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            // A legacy octal escape (TS1487): `\1`, `\17`, `\177`, but `\47` not `\477`
            u32 value = (u32)(c - '0');
            if (c <= '3' && *p->pos >= '0' && *p->pos <= '7') value = value * 8 + (u32)(*p->pos++ - '0');
            if (*p->pos >= '0' && *p->pos <= '7' && p->pos - start < 4) value = value * 8 + (u32)(*p->pos++ - '0');
            escape_error(errors, 1487, start, (u32)(p->pos - start));
            errors->errors[errors->count - 1].value = value;
            if (tagged) cstr_append_n(str, start, p->pos - start);
            else append_code_point(str, value);
            return false;
        }
        case '8': case '9':
            escape_error(errors, 1488, start, 2);
            if (tagged) cstr_append_n(str, start, 2);
            else cstr_append_n(str, &c, 1);
            return false;
        case '\r':
            if (*p->pos == '\n') p->pos++;
            return true; // Line continuation
        case '\n':
            return true;
        case 'x': {
            int hi = hex_digit_value(p->pos[0]);
            int lo = hi < 0 ? -1 : hex_digit_value(p->pos[1]);
            if (lo < 0) {
                if (hi >= 0) p->pos++;
                escape_error(errors, 1125, p->pos, 0);
                cstr_append_n(str, start, p->pos - start);
                return false;
            }
            p->pos += 2;
            append_code_point(str, (u32)(hi * 16 + lo));
            return true;
        }
        case 'u': {
            u32 code = 0;
            if (*p->pos == '{') {
                const char* digits_start = p->pos + 1;
                const char* q = digits_start;
                bool too_big = false;
                for (; hex_digit_value(*q) >= 0; q++) {
                    code = code * 16 + (u32)hex_digit_value(*q);
                    too_big |= code > 0x10FFFF;
                }
                bool valid = true;
                if (q == digits_start) {
                    escape_error(errors, 1125, q, 0);
                    valid = false;
                } else if (too_big) {
                    escape_error(errors, 1198, digits_start, (u32)(q - digits_start));
                    valid = false;
                }
                if (!*q) {
                    escape_error(errors, 1126, q, 0);
                    valid = false;
                } else if (*q == '}') {
                    q++;
                } else {
                    escape_error(errors, 1199, q, 0);
                    valid = false;
                }
                p->pos = q;
                if (!valid) {
                    cstr_append_n(str, start, p->pos - start);
                    return false;
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    int v = hex_digit_value(p->pos[i]);
                    if (v < 0) {
                        p->pos += i;
                        escape_error(errors, 1125, p->pos, 0);
                        cstr_append_n(str, start, p->pos - start);
                        return false;
                    }
                    code = code * 16 + (u32)v;
                }
                p->pos += 4;
                // A surrogate pair written as two \u escapes is one code point
                if (code >= 0xD800 && code <= 0xDBFF && p->pos[0] == '\\' && p->pos[1] == 'u') {
                    u32 low = 0;
                    bool valid = true;
                    for (int i = 0; i < 4 && valid; i++) {
                        int v = hex_digit_value(p->pos[2 + i]);
                        if (v < 0) valid = false; else low = low * 16 + (u32)v;
                    }
                    if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        p->pos += 6;
                    }
                }
            }
            append_code_point(str, code);
            return true;
        }
        case '\xE2':
            if ((unsigned char)p->pos[0] == 0x80 && ((unsigned char)p->pos[1] & 0xFE) == 0xA8) {
                p->pos += 2;
                return true; // Line continuation by U+2028/U+2029
            }
            break;
        default: break;
    }
    // Any other character stands for itself (`\'`, `\"`, `\\`, `\a`); a multi-byte one is copied whole
    int size = utf8_rune_size(p->pos - 1);
    cstr_append_n(str, p->pos - 1, size > 0 ? size : 1);
    p->pos += (size > 1 ? size - 1 : 0);
    return true;
}

static bool parse_literal(Parser* parent, Value* ret);
static bool parse_binary_op(Parser* parent, BinaryOp* op);
static bool parse_left_unary_op(Parser* parent, UnaryOp* op);
static bool parse_right_unary_op(Parser* parent, UnaryOp* op);
static bool parse_block(Parser* parent, bool toplevel, Block* ret);
static bool skip_body(Parser* parent, FunctionImpl* func, bool expression);
static bool parse_interface(Parser* parent, bool is_declaration, Interface* ret);

// The first push reserves room for 4, and these elements are big (a parameter is ~570 bytes): lists of one or two were
// most of the AST's memory. A finished list gives back what it doesn't use
// Most lists have one element: the first push reserves room for exactly one (the vec's own first push reserves 4)
#define list_push(vec_kind, vec, ...) do { \
        if (!(vec)->capacity) vec_kind##_reserve(vec, 1); \
        vec_kind##_push(vec, __VA_ARGS__); \
    } while (0)
#define shrink_list(vec_kind, vec) do { \
        if ((vec)->size && (vec)->size < (vec)->capacity) vec_kind##_shrink_to_fit(vec); \
    } while (0)
// A list built on the thread's scratch stack and allocated once, at its exact size, when it's finished
// (scratch_list_finish()): growing a list in the arena moves it at nearly every growth, its elements' nodes having been
// allocated after it. Lists nest (an argument's own call), each on top of the ones still being built; a list abandoned
// by a failed attempt is simply overwritten. Elements are copied in and out by value: nothing may point at them while
// the list is being built
typedef struct ScratchList {
    size_t base;
    isize count;
} ScratchList;

static thread_local struct { char* data; size_t top, capacity; } scratch;

static inline ScratchList scratch_list_begin(void) {
    return (ScratchList){ .base = (scratch.top + 15) & ~(size_t)15 };
}

static void scratch_grow(size_t needed) {
    size_t capacity = scratch.capacity ? scratch.capacity : 4096;
    while (capacity < needed) capacity *= 2;
    scratch.data = realloc(scratch.data, capacity);
    check(scratch.data);
    scratch.capacity = capacity;
}

static inline void scratch_list_push(ScratchList* me, void const* element, size_t size) {
    size_t at = my base + (size_t)my count * size;
    if (at + size > scratch.capacity) scratch_grow(at + size);
    memcpy(scratch.data + at, element, size);
    my count++;
    scratch.top = at + size;
}

// The elements in the arena (null for none); the list's scratch is given back
static void* scratch_list_finish(ScratchList* me, size_t size) {
    void* ret = nullptr;
    if (my count) {
        ret = ast_malloc(my count * (isize)size);
        memcpy(ret, scratch.data + my base, (size_t)my count * size);
    }
    scratch.top = my base;
    return ret;
}

static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret);
static bool parse_statement(Parser* parent, bool toplevel, Statement* ret);
static inline bool expect_statement(Parser* parent, bool toplevel, Statement* ret);
static bool parse_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);
static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_function_args,
    bool commit_on_paren,
    FunctionImpl* ret
);
static bool parse_numeric_value(Parser* parent, Value* ret);
static bool parse_string(Parser* parent, cstr* ret);
static inline bool parse_string_or_tagged(Parser* parent, cstr* ret, bool report_escapes);
static Span span_from_token_or_eof(Parser* parent);
static const char* token_full_start(Parser* me);
static bool parse_template(Parser* parent, VecExpr* ret, AstCStrs* texts);
static bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret);
static void parse_computed_key_comma(Parser* p, Expr* key);
static bool parse_decorators(Parser* parent, VecExpr* ret);
static bool parse_decorated_class_with_modifiers(Parser* parent, ClassDecl* ret);
static void unhop_trivia(Parser* me);
static bool parse_type_compat_literal(Parser* parent, Value* ret);
static bool parse_template_type(Parser* parent, TypeNode* ret);
static bool parse_type_predicate(Parser* parent, FunctionImpl* ret);
static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret);
static bool parse_expr_right_side(Parser* parent, ExprCtx ctx, Expr* left_and_ret);
static bool expr_ends_with_arrow_or_yield(Expr const* e);
static bool jsx_generic_arrow_start(Parser* parent);
static bool parse_binding_content(Parser* parent, Binding* ret);
static bool is_reserved_word(csview word);
static void unique_symbol_misplaced(Parser* p, char const* found);
// tsc's `\s` in a regex, ASCII only
static bool is_regex_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
static bool parse_binding_identifier(Parser* parent);
static void note_top_level_await_identifier(Parser* p, csview name);
static bool parse_binding_content_or_private_name(Parser* parent, Binding* ret, u32 code, char const* message);
static void parse_import_attributes_body(Parser* parent, ImportAttributes* ret);
static bool parse_binding_element(Parser* parent, Binding* ret);
static bool object_binding_name_follows(Parser* parent);
static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret);
static bool parse_binding_type_ex(Parser* parent, Binding* ret, bool report_js, TypeNode* into);
static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret);
static void report_missing_semicolon_after_identifier(Parser* p, Span name);
static bool parse_initialization_or_set_to_default(Parser* parent, ExprCtx ctx, Expr* ret);
static Expr* parse_initialization_boxed(Parser* parent, ExprCtx ctx);

// tsc's isNamedEvaluationSource(): `value` gets its name from where it is (`x = class {}`). An anonymous class there
// is named by the emitter at runtime (ClassDecl.named_evaluation)
static void mark_named_evaluation(Expr* value, bool computed_key) {
    while (true) {
        if (value->tag == EXPR_AS_TYPE) value = &value->as_type->expr;
        else if (value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_RIGHT_BANG) value = &value->unary_op->expr;
        else break;
    }
    if (value->tag == EXPR_CLASS_DECL && !span_valid(value->class_decl->name)) {
        value->class_decl->named_evaluation = computed_key ? CLASS_NAMED_BY_COMPUTED_KEY : CLASS_NAMED_BY_CONTEXT;
    }
}

// tsc's getAssignedName(): an anonymous class or function expression that is directly (no parentheses) the value of a
// variable, property assignment, binding element or the right operand of a binary expression is called by that name
static void mark_assigned_name(Expr* value, Span name) {
    if (value->tag == EXPR_FUNCTION && !span_valid(value->function->name) && !function_impl_is_arrow(value->function) &&
        expr_outer_span(value).size == value->inner_span.size
    ) {
        function_impl_rare_mut(value->function)->assigned_name = name;
        return;
    }
    if (value->tag != EXPR_CLASS_DECL || span_valid(value->class_decl->name)) return;
    if (expr_outer_span(value).size != value->inner_span.size) return;
    value->class_decl->assigned_name = name;
}

static inline Parser parser(Parser* parent) {
#ifdef DEBUG_BUILD
    check(!parent->has_panding_child);
    parent->has_panding_child = true;
#endif
    return (Parser){
        .shared = parent->shared,
        .parent = parent,
        .pos = parent->pos,
        .whitespace = parent->whitespace,
        .whitespace_end = parent->whitespace_end,
        .error_count = 0,
    };
}

// tsgo's doInContext(DisallowIn, false, ...) (parseExpressionAllowIn): `bool saved = allow_in_enter(p); ...;
// allow_in_leave(p, saved);` around what tsgo parses with `in` allowed again (ParserShared.disallow_in)
static inline bool allow_in_enter(Parser* me) {
    bool saved = my shared->disallow_in;
    my shared->disallow_in = false;
    return saved;
}

static inline void allow_in_leave(Parser* me, bool saved) {
    my shared->disallow_in = saved;
}

/// Must not be used on the root parser, always returns true, drops parser
static inline bool parse_success(Parser* me) {
#ifdef DEBUG_BUILD
    check(my parent->has_panding_child);
    my parent->has_panding_child = false;
#endif
    my parent->pos = my pos;
    my parent->whitespace = my whitespace;
    my parent->whitespace_end = my whitespace_end;
    my parent->error_count += my error_count;
    return true;
}

/// Must not be used on the root parser, always returns false, drops parser
static inline bool parse_fail(Parser* me) {
#ifdef DEBUG_BUILD
    check(my parent->has_panding_child);
    my parent->has_panding_child = false;
#endif
    if (my error_count) {
        vec_error_erase_n(
            &my shared->errors,
            vec_error_size(&my shared->errors) - my error_count,
            my error_count
        );
    }
    if (my fail_cache_flag &&
        my shared->fail_cache_start &&
        my parent->pos >= my shared->fail_cache_start
    ) {
        intptr_t index = my parent->pos - my shared->fail_cache_start;
        if (vec_u8_size(&my shared->fail_cache) <= index) {
            vec_u8_resize(&my shared->fail_cache, index + 1, 0);
        }
        *vec_u8_at_mut(&my shared->fail_cache, index) |= my fail_cache_flag;
    }
    return false;
}

// Returns true if this position has already failed for this flag
static bool fail_cache_register_and_check(Parser* parser, FailCacheFlag flag) {
    // PERF: only activate fail cache once a threashold number of failures has been hit
    if (!parser->shared->fail_cache_start) {
        parser->shared->fail_cache_start = parser->pos;
    }
    if (parser->pos >= parser->shared->fail_cache_start &&
        parser->pos - parser->shared->fail_cache_start < vec_u8_size(&parser->shared->fail_cache) &&
        *vec_u8_at(&parser->shared->fail_cache, parser->pos - parser->shared->fail_cache_start) & flag
    ) {
        return true;
    } else {
        parser->fail_cache_flag = flag;
        return false;
    }
}
#ifdef DEBUG_BUILD
#define FAIL_CACHE_HOOK(parser, flag) if (fail_cache_register_and_check(parser, flag)) return parse_fail(parser)
#else
#define FAIL_CACHE_HOOK(parser, flag) if (fail_cache_register_and_check(parser, flag)) return false
#endif

static void reset_fail_cache(Parser* me) {
    my shared->fail_cache_start = nullptr;
    // _clear() loops in order to drop. there's no drop work, so the loop is not required. this should be safe.
    my shared->fail_cache.size = 0;
}

// Should only be called for the current active parser
static inline Error* add_error_to_parser(Parser* me, Error error) {
#ifdef DEBUG_BUILD
    check(!my has_panding_child);
#endif
    my error_count++;
    return vec_error_push(&my shared->errors, error);
}

static bool last_error_starts_at(Parser* me, Span span);
// Whether at (a NUL byte) is a character of the file rather than its end (tsc's scanner reads past it)
static bool nul_at_inside_file(Parser const* p, char const* at) {
    char const* file_start = cstr_str(&p->shared->source->contents);
    return at >= file_start && at < file_start + cstr_size(&p->shared->source->contents);
}
// The end of the text being parsed. A NUL byte inside the file is a character like any other (an Unknown token with
// TS1127 to tsc's scanner)
static inline bool at_eof(Parser const* p, char const* at) {
    return !*at && !nul_at_inside_file(p, at);
}
static int invalid_char_size(Parser const* p, const char* c);
// A checker's code (TS2xxx) the parser reports in tttt: not one of tsc's parse diagnostics, which parseErrorAtPosition
// compares with. (tsc's parser reports a few TS2xxx itself: parseErrorForInvalidName's, JSX's TS2657, `super<T>`'s,
// TS2809, TS2880)
static bool is_semantic_code(unsigned code) {
    switch (code) {
        case 2427: case 2457: case 2819: case 2657: case 2754: case 2809: case 2880: return false;
        default: return code >= 2000 && code < 3000;
    }
}

// tsc's parseErrorAtPosition: an error that starts where the previous one does is dropped (the first error at a
// position is the one that explains it; whatever recovery reports there next is noise). An error at a character no
// token starts with is TS1127, which tsc's scanner reports before the parser gets to say anything there
static Error* parse_error_inner(Parser* me, Span span, unsigned ts_code, cstr message, bool in_literal) {
    my shared->errors_attempted++;
    int invalid = span_source(span) && !in_literal ? invalid_char_size(me, span_start_ptr(span)) : 0;
    if (invalid && ts_code != 1127) {
        cstr_drop(&message);
        return parse_error_inner(me, span_make(my shared->source, span_start_ptr(span), span_start_ptr(span) + invalid),
            1127, cstr_lit("Invalid character."), false);
    }
    // Likewise a `#!` anywhere but at the start of the file (the scanner's TS18026 comes first)
    const char* at = span_start_ptr(span);
    if (span_source(span) && !in_literal && ts_code != 18026 && at && at[0] == '#' && at[1] == '!' &&
        at != cstr_str(&my shared->source->contents)
    ) {
        cstr_drop(&message);
        return parse_error_inner(me, span_make(my shared->source, at, at + 2), 18026,
            cstr_lit("'#!' can only be used at the start of a file."), false);
    }
    // (A grammar error is the checker's to tsc: no position is taken by another; nor is a semantic one the parser
    // reports, TS2523/TS2524)
    if (!is_semantic_code(ts_code) && last_error_starts_at(me, span)) {
        cstr_drop(&message);
        return nullptr;
    } else if (vec_error_size(&my shared->errors) >= MAX_ERRORS) {
        cstr_drop(&message);
        return nullptr;
    } else if (vec_error_size(&my shared->errors) == MAX_ERRORS - 1) {
        cstr_drop(&message);
        ts_code = TS_CODE_NONE;
        message = cstr_lit("Too many errors, additional errors not shown.");
    }
    return add_error_to_parser(me, (Error){
        .message = message,
        .span = span,
        .ts_code = ts_code,
        .phase = ERROR_PHASE_PARSE,
    });
}

static Error* parse_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    return parse_error_inner(me, span, ts_code, message, false);
}

// A grammar error: tsc's checker's (or binder's), not a parse error: dropped in a file with parse errors
// (SourceFile.has_parse_errors), and never takes a position from a parse error
static Error* grammar_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    my shared->errors_attempted++;
    return add_error_to_parser(me, (Error){ .message = message, .span = span, .ts_code = ts_code,
        .phase = ERROR_PHASE_PARSE, .grammar = true });
}
#define grammar_error(me, span, ts_code, ...) grammar_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

// An error tsc's checker reports with a plain error() (not a grammar error): semantic, so it neither makes the file one
// with parse errors nor is dropped in one (`import e = require(foo)`'s TS1141, a parameter initializer's `yield`)
static Error* checker_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    my shared->errors_attempted++;
    return add_error_to_parser(me, (Error){ .message = message, .span = span, .ts_code = ts_code,
        .phase = ERROR_PHASE_RESOLVE_NAMES });
}
#define checker_error(me, span, ts_code, ...) checker_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

/// Builds the message from a display sequence
#define parse_error(me, span, ts_code, ...) parse_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

// A scanner error about the token that starts at `token`, see ParserShared.scanner_error_index
// `first_error`: how many errors there were when tttt started reading the token (what came after are its scanner errors)
static void scanner_error_s(Parser* me, const char* token, isize first_error, Span span, unsigned ts_code, cstr message) {
    Error* error = parse_error_s(me, span, ts_code, message);
    if (!error) return;
    error->scanner = true;
    my shared->scanner_error_index = vec_error_size(&my shared->errors) - 1;
    my shared->scanner_error_first = first_error;
    my shared->scanner_error_token = token;
}
#define scanner_error(me, token, first_error, span, ts_code, ...) \
    scanner_error_s(me, token, first_error, span, ts_code, cstr_d(__VA_ARGS__))
// An error inside a literal (an escape sequence): whatever character it starts at, it's not a token
#define parse_error_in_literal(me, span, ts_code, ...) parse_error_inner(me, span, ts_code, cstr_d(__VA_ARGS__), true)

// TypeScript-only syntax in a JavaScript file (tsc's getJSSyntacticDiagnosticsForFile: TS8002-TS8017, TS8037). They're
// syntactic diagnostics of their own: not parse errors (grammar errors are still reported), reported whether or not
// the file is checked, never deduplicated with parse errors
static bool is_js_syntax_code(unsigned code) {
    return (code >= 8002 && code <= 8017) || code == 8037;
}

static bool reports_js_syntax(Parser const* me) {
    return my shared->source && my shared->source->is_js && !my shared->in_jsdoc;
}

static void js_syntax_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    if (!reports_js_syntax(me)) {
        cstr_drop(&message);
        return;
    }
    add_error_to_parser(me, (Error){ .message = message, .span = span, .ts_code = ts_code, .phase = ERROR_PHASE_PARSE, .js_syntax = true });
}

#define js_syntax_error(me, span, ts_code, ...) js_syntax_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

// The TypeScript-only syntax errors inside what one of them is about are not reported (tsc's walk returns "skip"):
// drops those reported since `start` (a size of the error list)
static void js_syntax_skip_since(Parser* me, intptr_t start) {
    if (!reports_js_syntax(me)) return;
    VecError* errors = &my shared->errors;
    intptr_t kept = start;
    for (intptr_t i = start; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        if (is_js_syntax_code(error->ts_code)) {
            error_drop(error);
            my error_count--;
        } else {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        }
    }
    errors->size = kept;
}

// Moves the errors a JSDoc parser `me` reported to the JSDoc errors, before it fails
static void jsdoc_keep_errors(Parser* me) {
    VecError* errors = &my shared->errors;
    for (intptr_t i = vec_error_size(errors) - my error_count; i < vec_error_size(errors); i++) {
        Error error = errors->data[i];
        if (my shared->source->is_js) {
            error.phase = ERROR_PHASE_RESOLVE_NAMES;
            error.jsdoc = true;
            vec_error_push(&my shared->jsdoc_errors, error);
        } else {
            error_drop(&error);
        }
    }
    errors->size -= my error_count;
    my error_count = 0;
}

// An error about a JSDoc comment that isn't a parse error of its text (tsc's checker reports it about the tags), kept
// like those
static void jsdoc_error_s(Parser* me, Span span, unsigned ts_code, cstr message) {
    vec_error_push(&my shared->jsdoc_errors, (Error){
        .message = message, .span = span, .ts_code = ts_code, .phase = ERROR_PHASE_RESOLVE_NAMES, .jsdoc = true,
    });
}

#define jsdoc_error(me, span, ts_code, ...) jsdoc_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

static intptr_t error_mark(Parser const* me) {
    return vec_error_size(&my shared->errors);
}

// Drops the grammar errors (tsc's checker's) reported since `start`: what they're about is something tsc's checker
// never checks (a heritage element past the first). With `code`, only the first of that code whose span starts at
// `at`, which is returned (span_null if none): an error the caller reports itself, if at all
static Span grammar_drop_since(Parser* me, intptr_t start, unsigned code, const char* at) {
    VecError* errors = &my shared->errors;
    intptr_t kept = start;
    Span found = span_null;
    for (intptr_t i = start; i < vec_error_size(errors); i++) {
        Error* error = vec_error_at_mut(errors, i);
        // (TS18012 is tsc's binder's, which every declaration goes through, checked or not)
        bool drop = error->grammar && (!code ? error->ts_code != 18012 :
            (!span_valid(found) && error->ts_code == code && span_start_ptr(error->span) == at));
        if (drop) {
            if (code) found = error->span;
            error_drop(error);
            my error_count--;
        } else {
            if (kept != i) errors->data[kept] = *error;
            kept++;
        }
    }
    errors->size = kept;
    return found;
}

// Whether the declaration parser called now has modifiers that fail tsc's checkGrammarModifiers (see
// ParserShared.decl_modifiers_failed)
static bool take_decl_modifiers_failed(Parser* me) {
    bool failed = my shared->decl_modifiers_failed;
    my shared->decl_modifiers_failed = false;
    return failed;
}

// Whether the declaration parser called now was committed to by decorators (see ParserShared.decl_committed)
static bool take_decl_committed(Parser* me) {
    bool committed = my shared->decl_committed;
    my shared->decl_committed = false;
    return committed;
}

// A function's type parameters (`type_params`: of a declaration, not of a signature in a type) and return type in
// JavaScript
static void js_syntax_function(Parser* me, FunctionImpl const* function, bool type_params) {
    if (!reports_js_syntax(me)) return;
    VecGenericItem const* items = &function->generic.items;
    if (type_params && !vec_generic_item_is_empty(items) && !vec_generic_item_front(items)->jsdoc) {
        Span span = span_between(vec_generic_item_front(items)->full_span, vec_generic_item_back(items)->full_span);
        js_syntax_error(me, span, 8004, "Type parameter declarations can only be used in TypeScript files.");
    }
    // (A signature declaration is reported as a whole instead: tsgo's checkJSSyntax)
    if (function->ret_explicit && function->has_body && span_valid(function_impl_ret_span(function))) {
        js_syntax_error(me, function_impl_ret_span(function), 8010, "Type annotations can only be used in TypeScript files.");
    }
}

typedef struct Anchor {
    Parser* parser;
    const char* start;
} Anchor;

static inline Anchor anchor(Parser* parser) {
    return (Anchor) {
        .parser = parser,
        .start = parser->pos,
    };
}

static inline Anchor span_starting_at(Parser* parser, Span start) {
    return (Anchor) {
        .parser = parser,
        .start = span_start_ptr(start),
    };
}

static inline Span span_from(Anchor spanner) {
    return span_make(
        spanner.parser->shared->source,
        spanner.start,
        spanner.parser->pos
    );
}

static inline Span span_from_pos(Parser* me) {
    return span_make(my shared->source, my pos, my pos);
}

static void report_escape_errors(Parser* me, EscapeErrors const* errors) {
    for (u32 i = 0; i < errors->count; i++) {
        Span at = span_make(my shared->source, errors->errors[i].at, errors->errors[i].at + errors->errors[i].len);
        switch (errors->errors[i].code) {
            case 1125: parse_error_in_literal(me, at, 1125, "Hexadecimal digit expected."); break;
            case 1126: parse_error_in_literal(me, at, 1126, "Unexpected end of text."); break;
            case 1198:
                parse_error_in_literal(me, at, 1198, "An extended Unicode escape value must be between 0x0 and 0x10FFFF inclusive.");
                break;
            case 1199: parse_error_in_literal(me, at, 1199, "Unterminated Unicode escape sequence."); break;
            case 1487: {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\x%02x", errors->errors[i].value);
                parse_error_in_literal(me, at, 1487, "Octal escape sequences are not allowed. Use the syntax '", hex, "'.");
            }   break;
            case 1488: parse_error_in_literal(me, at, 1488, "Escape sequence '", span_sv(at), "' is not allowed."); break;
            default: impossible;
        }
    }
}

static inline Span span_from_previous_char(Parser* me) {
    return span_make(my shared->source, my pos - 1, my pos);
}

static Span span_from_token(Parser* me) {
    // (The end of a JSDoc comment is its scanner's end of file: no length)
    if (my shared->in_jsdoc && my shared->jsdoc_end && my pos >= my shared->jsdoc_end) {
        return span_make(my shared->source, my pos, my pos);
    }
    Parser p = parser(me);
    Anchor a = anchor(&p);
    hop_token(&p);
    parse_fail(&p);
    return span_from(a);
}

// Of each ASCII character: bit 1 if it can start an identifier, bit 2 if it can continue one
static const uint8_t identifier_char_class[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
};

// ASCII only: identifier_char_size() knows the rest
static inline bool is_identifier_char(char c) {
    return identifier_char_class[(uint8_t)c] & 2;
}

static inline bool is_identifier_start_char(char c) {
    return identifier_char_class[(uint8_t)c] & 1;
}

static int identifier_char_size_slow(const char* c, bool start);

// The length of the character at c if it can start (`start`) or continue an identifier, else 0: ASCII, non-ASCII
// (tsc's unicode tables for the target) and unicode escapes of either (`\u0061`, `\u{61}`)
static inline int identifier_char_size(const char* c, bool start) {
    uint8_t b = (uint8_t)*c;
    if (identifier_char_class[b] & (start ? 1 : 2)) return 1;
    if (b < 0x80 && b != '\\') return 0;
    return identifier_char_size_slow(c, start);
}

static inline bool is_number_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static inline bool is_number_start_char(char c) {
    return (c >= '0' && c <= '9') || c == '.';
}

// ret can be null
static inline bool parse_identifier_or_keyword(Parser* parent, Span* ret) {
    Anchor a = anchor(parent);
    int size = identifier_char_size(parent->pos, true);
    if (size) {
        parent->pos += size;
        bool escaped = *a.start == '\\';
        while (true) {
            while (is_identifier_char(*parent->pos)) parent->pos++;
            if ((uint8_t)*parent->pos < 0x80 && *parent->pos != '\\') break;
            if (!(size = identifier_char_size_slow(parent->pos, false))) break;
            escaped |= *parent->pos == '\\';
            parent->pos += size;
        }
        if (escaped) note_escaped_names(parent->shared);
        if (ret) {
            *ret = span_from(a);
        }
        return true;
    } else {
        return false;
    }
}

// The words parse_identifier() refuses
static inline bool is_reserved_identifier(const char* word, intptr_t len) {
#define WORD_IS(lit) (len == sizeof(lit) - 1 && memcmp(word, lit, sizeof(lit) - 1) == 0)
    switch (word[0]) {
        case 'r': return WORD_IS("return");
        case 't': return WORD_IS("throw") || WORD_IS("typeof") || WORD_IS("this");
        case 'c': return WORD_IS("case");
        case 'i': return WORD_IS("in");
        case 's': return WORD_IS("super");
        case 'd': return WORD_IS("default");
        // (tsc's reserved words that start no expression: `export default var a` is missing its expression)
        case 'v': return WORD_IS("var");
        default: return false;
    }
#undef WORD_IS
}

// ret can be null
static bool parse_identifier(Parser* parent, Span* ret) {
    Anchor a = anchor(parent);
    if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    Span s = span_from(a);
    if (is_reserved_identifier(a.start, parent->pos - a.start)) {
        parent->pos = a.start;
        return false;
    }
    if (ret) {
        *ret = s;
    }
    return true;
}

// A private name `#x` (its span is the name), or a lone `#`: tsc's scanner makes that a PrivateIdentifier `#` too,
// reporting TS1127 (not `#!`)
static bool hop_private_name(Parser* parent) {
    if (parent->pos[0] != '#' || parent->pos[1] == '!') return false;
    Anchor a = anchor(parent);
    parent->pos++;
    if (!parse_identifier_or_keyword(parent, nullptr)) {
        parse_error(parent, span_from(a), 1127, "Invalid character.");
    } else if (span_equals(span_from(a), "#constructor")) {
        // (tsc's binder, checkPrivateIdentifier: unless the file has parse errors)
        grammar_error(parent, span_from(a), 18012, "'#constructor' is a reserved word.");
    }
    return true;
}

static bool hop_private_name(Parser* parent);

static inline bool expect_identifier(Parser* parent, Span* ret) {
    if (parse_identifier(parent, ret)) {
        return true;
    } else if (parent->pos[0] == '#' && parent->pos[1] != '!' && identifier_char_size(parent->pos + 1, true)) {
        // A private name where an identifier must be: tsc's createIdentifierWithDiagnostic reports TS18016 and takes it
        // as the identifier (`import x = #a.b`)
        Anchor a = anchor(parent);
        hop_private_name(parent);
        *ret = span_from(a);
        parse_error(parent, *ret, 18016, "Private identifiers are not allowed outside class bodies.");
        return true;
    } else {
        parse_error(parent, span_from_token_or_eof(parent), 1003, "Identifier expected.");
        *ret = span_from_pos(parent);
        return false;
    }
}

static inline bool is_js_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

// `at` is the `@` a comment directive would start with (tsc's commentDirectiveRegExSingleLine/MultiLine matched up to
// there): records a `@ts-expect-error`/`@ts-ignore` (no word boundary after it in tsc either)
static void note_comment_directive(ParserShared* shared, const char* start, const char* at, const char* end) {
    csview rest = csview_from_range(at + 1, end);
    bool expect_error = csview_starts_with(rest, "ts-expect-error");
    if (!expect_error && !csview_starts_with(rest, "ts-ignore")) return;
    if (shared->chunk) {
        chunk_effect(shared, (ChunkEffect){ .kind = CHUNK_EFFECT_COMMENT_DIRECTIVE, .flag = expect_error, .start = start, .end = end });
        return;
    }
    source_file_add_comment_directive((SourceFile*)shared->source, start, end, expect_error);
}

// Backtracking hops comments repeatedly: only what is past the last one recorded is new
static void note_jsdoc_comment(Parser* parent, const char* start, const char* end) {
    VecJsDocRange* comments = &parent->shared->jsdoc_comments;
    if (comments->size && vec_jsdoc_range_back(comments)->start >= start) return;
    vec_jsdoc_range_push(comments, (JsDocRange){ .start = start, .end = end });
}

// A JSDoc comment's `@deprecated` tag, for TS6385 (tsc's NodeFlags.Deprecated from parseJSDocComment): only the
// harness asks (capture_suggestions)
static void note_deprecated_jsdoc(ParserShared* shared, const char* start, const char* end) {
    for (const char* at = start; (at = memchr(at, '@', (size_t)(end - at))); at++) {
        if (end - at < 11 || memcmp(at, "@deprecated", 11) != 0) continue;
        char before = at > start ? at[-1] : ' ';
        char after = at + 11 < end ? at[11] : ' ';
        // (A tag: past blanks or the comment's `*`s, and a whole word)
        if ((before == ' ' || before == '\t' || before == '*' || before == '\n') && !is_identifier_char(after)) {
            if (shared->chunk) chunk_effect(shared, (ChunkEffect){ .kind = CHUNK_EFFECT_DEPRECATED_JSDOC, .end = end });
            else source_file_add_deprecated_jsdoc((SourceFile*)shared->source, end);
            return;
        }
    }
}

// strchr() that goes on past NUL bytes inside the file
static char const* find_char_in_file(Parser const* p, char const* from, char c) {
    while (true) {
        char const* found = strchr(from, c);
        if (found) return found;
        char const* nul = from + strlen(from);
        if (!nul_at_inside_file(p, nul)) return nullptr;
        from = nul + 1;
    }
}

// What a `//` comment from start to end (its line's end) records: a comment directive, the comment where declarations
// are emitted. hop_comment() and the body skipper (skip_balanced_braces())
static void note_line_comment(ParserShared* shared, const char* start, const char* end) {
    // A materialized body's comments were recorded when it was skipped (plan 78)
    if (shared->materializing) return;
    // /^\/\/\/?\s*@(ts-expect-error|ts-ignore)/
    const char* c = start + 2;
    if (*c == '/') c++;
    while (c < end && is_js_ascii_space(*c)) c++;
    if (*c == '@' && c < end) note_comment_directive(shared, start, c, end);
    if (shared->record_comments && !shared->in_jsdoc) source_file_add_comment((SourceFile*)shared->source, start, end);
}

// The same for a `/* */` comment (end past the `*/`), whose last line starts at last_line_start (start if it has one)
static void note_block_comment(ParserShared* shared, const char* start, const char* last_line_start, const char* end) {
    if (shared->materializing) return;
    if (shared->record_comments && !shared->in_jsdoc) source_file_add_comment((SourceFile*)shared->source, start, end);
    if (start[2] == '*' && start[3] != '/' && compiler_options()->capture_suggestions) note_deprecated_jsdoc(shared, start, end);
    // The last line, trimmed, against /^(?:\/|\*)*\s*@(ts-expect-error|ts-ignore)/
    const char* c = last_line_start;
    while (is_js_ascii_space(*c)) c++;
    while (*c == '/' || *c == '*') c++;
    while (c < end && is_js_ascii_space(*c)) c++;
    if (*c == '@' && c < end) note_comment_directive(shared, last_line_start, c, end);
}

static inline bool hop_comment(Parser* parent) {
    if (*parent->pos != '/') {
        return false;
    }
    const char* start = parent->pos;
    switch (*(parent->pos + 1)) {
        case '/': {
            parent->pos += 2 + strcspn(parent->pos + 2, "\n\r");
            while (!*parent->pos && nul_at_inside_file(parent, parent->pos)) {
                parent->pos += 1 + strcspn(parent->pos + 1, "\n\r");
            }
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            note_line_comment(parent->shared, start, parent->pos);
        }   break;

        case '*': {
            if (parent->whitespace == WHITESPACE_NONE) {
                parent->whitespace = WHITESPACE_PARSED;
            }
            // The `*/` (not the `*` of the `/*`), found with the libc scans
            const char* pos = start + 2;
            const char* slash;
            while (true) {
                slash = find_char_in_file(parent, pos, '/');
                if (!slash) {
                    // (tsc's scanner: the comment goes to the end of the file, '*/' expected there)
                    if (find_char_in_file(parent, start, '\n')) parent->whitespace = WHITESPACE_NEWLINE;
                    parent->pos = cstr_str(&parent->shared->source->contents) + cstr_size(&parent->shared->source->contents);
                    if (!nul_at_inside_file(parent, start)) parent->pos = start + 2 + strlen(start + 2);
                    parent->shared->unterminated_comment = true;
                    return true;
                }
                if (slash[-1] == '*' && slash - 1 >= start + 2) break;
                pos = slash + 1;
            }
            pos = slash + 1;
            const char* last_line_start = start;
            if (memchr(start, '\n', pos - start)) {
                parent->whitespace = WHITESPACE_NEWLINE;
                last_line_start = pos;
                while (last_line_start[-1] != '\n') last_line_start--;
            }
            parent->pos = pos;
            if (start[2] == '*' && start[3] != '/' && parent->shared->source->is_js) note_jsdoc_comment(parent, start, pos);
            note_block_comment(parent->shared, start, last_line_start, pos);
        }   break;

        default: return false;
    }
    return true;
}

// The byte length of the non-ASCII whitespace (tsc's isWhiteSpaceSingleLine: NBSP, NEL, BOM, category Zs and U+200B)
// at c, negated for a line terminator (U+2028, U+2029), 0 if there is none
static int unicode_whitespace_size(const char* c) {
    uint8_t b0 = (uint8_t)c[0];
    if (b0 < 0xC2) return 0;
    if (b0 == 0xC2) return (uint8_t)c[1] == 0xA0 || (uint8_t)c[1] == 0x85 ? 2 : 0;
    uint8_t b1 = (uint8_t)c[1];
    uint8_t b2 = b1 ? (uint8_t)c[2] : 0;
    if (b0 == 0xE1) return b1 == 0x9A && b2 == 0x80 ? 3 : 0; // U+1680
    if (b0 == 0xE2 && b1 == 0x80) {
        if (b2 >= 0x80 && b2 <= 0x8B) return 3; // U+2000..U+200B
        if (b2 == 0xA8 || b2 == 0xA9) return -3; // U+2028, U+2029
        if (b2 == 0xAF) return 3; // U+202F
        return 0;
    }
    if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3; // U+205F
    if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3; // U+3000
    if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3; // U+FEFF
    return 0;
}

// The length of the UTF-8 character at c (1 for a byte that isn't well-formed UTF-8: tsgo's RuneError)
static int utf8_char_size(const char* c) {
    return utf8_rune_size(c);
}

static int identifier_escape_size(const char* c);

// The code point of the (well-formed) UTF-8 character of `size` bytes at c
static u32 utf8_code_point(const char* c, int size) {
    const unsigned char* b = (const unsigned char*)c;
    switch (size) {
        case 2: return (u32)(b[0] & 0x1F) << 6 | (b[1] & 0x3F);
        case 3: return (u32)(b[0] & 0x0F) << 12 | (u32)(b[1] & 0x3F) << 6 | (b[2] & 0x3F);
        case 4: return (u32)(b[0] & 0x07) << 18 | (u32)(b[1] & 0x3F) << 12 | (u32)(b[2] & 0x3F) << 6 | (b[3] & 0x3F);
        default: return b[0];
    }
}

static int identifier_char_size_slow(const char* c, bool start) {
    u32 code;
    int size;
    if (*c == '\\') {
        size = identifier_escape_size(c);
        if (!size) return 0;
        code = 0;
        for (const char* d = c + 2; d < c + size; d++) {
            if (hex_digit_value(*d) >= 0) code = code * 16 + (u32)hex_digit_value(*d);
            if (code > 0x10FFFF) return 0;
        }
        if (code < 0x80) return (identifier_char_class[code] & (start ? 1 : 2)) ? size : 0;
    } else {
        size = utf8_char_size(c);
        if (size < 2) return 0;
        code = utf8_code_point(c, size);
    }
    return (start ? unicode_is_identifier_start(code) : unicode_is_identifier_part(code)) ? size : 0;
}

// A character at c that starts no token (tsc's scanner: TS1127 Invalid character): its length, else 0
static int invalid_char_size(Parser const* p, const char* c) {
    unsigned char b = (unsigned char)c[0];
    if (!b) return nul_at_inside_file(p, c) ? 1 : 0;
    // (A U+FFFD is the binary marker, a token of its own: hop_whitespace_slow())
    if (b >= 0x80) return unicode_whitespace_size(c) || utf8_is_rune_error(c) || identifier_char_size_slow(c, true) ? 0 : utf8_char_size(c);
    if (b == '\\') return identifier_char_size_slow(c, true) ? 0 : 1;
    if (b && b < 0x20 && !is_js_ascii_space((char)b)) return 1;
    // A `#` that starts no private name (tsc's scanner still makes it a PrivateIdentifier token, with this error).
    // `#!` is TS18026
    if (b == '#' && c[1] != '!' && !identifier_char_size(c + 1, true)) return 1;
    return 0;
}

static void hop_jsdoc_optional(Parser* parent, TypeNode* type);
static void hop_whitespace_slow(Parser* parent, bool hop_comments, bool hop_newlines);

// utf8.RuneError to tsgo's scanner: U+FFFD, or a byte that isn't well-formed UTF-8 (text_decode() keeps them)
static bool is_replacement_char(const char* c) {
    return utf8_is_rune_error(c);
}

// tsc's isConflictMarkerTrivia: seven `<`, `=`, `>` or `|` at the start of a line, then a space (`=======` needs
// none)
static bool is_conflict_marker(Parser* parent, const char* c) {
    if (c[0] != '<' && c[0] != '=' && c[0] != '>' && c[0] != '|') return false;
    if (c != cstr_str(&parent->shared->source->contents) && c[-1] != '\n' && c[-1] != '\r') return false;
    for (int i = 1; i < 7; i++) {
        if (c[i] != c[0]) return false;
    }
    return c[7] && (c[0] == '=' || c[7] == ' ');
}

// tsc's scanConflictMarkerTrivia: TS1185, then the marker's line is trivia, and after a `=======` or `|||||||` the
// other side of the conflict too (up to the next `=======` or `>>>>>>>`)
static const char* hop_conflict_marker(Parser* parent, const char* c) {
    parse_error(parent, span_make(parent->shared->source, c, c + 7), 1185, "Merge conflict marker encountered.");
    char marker = c[0];
    if (marker == '<' || marker == '>') {
        while (*c && *c != '\n' && *c != '\r') c++;
        return c;
    }
    for (; *c; c++) {
        if ((*c == '=' || *c == '>') && *c != marker && is_conflict_marker(parent, c)) break;
    }
    return c;
}

static inline bool is_conflict_marker_char(char c) {
    return c == '<' || c == '=' || c == '>' || c == '|';
}

// At the start of a line: the conflict markers there (after a ======= the next one is at a line start too)
static void hop_conflict_markers(Parser* parent) {
    while (is_conflict_marker(parent, parent->pos)) parent->pos = hop_conflict_marker(parent, parent->pos);
}

// Most calls are at a token already: whitespace is ASCII at most ' ', and the rest starts with `/` or a non-ASCII byte
static inline void hop_whitespace_full(Parser* parent, bool hop_comments, bool hop_newlines) {
    if (parent->pos == parent->whitespace_end) {
        return;
    }
    char c = *parent->pos;
    if ((unsigned char)c > ' ' && (unsigned char)c < 0x80 && c != '/') {
        parent->whitespace = WHITESPACE_NONE;
        parent->whitespace_end = parent->pos;
        return;
    }
    hop_whitespace_slow(parent, hop_comments, hop_newlines);
}

// (Out of line: PGO inlined it, hop_comment() with it, into ~30 callers, 2 KB each: 130 KB of hot text, 5% of tsc's I1
// misses, for a call per token that has trivia before it)
[[gnu::noinline]] static void hop_whitespace_slow(Parser* parent, bool hop_comments, bool hop_newlines) {
    const char* start = parent->pos;
    parent->whitespace = WHITESPACE_NONE;
    while (true) {
        switch (*parent->pos) {
            case '\r':
                // (A lone \r is a line terminator, of a CRLF the \n is)
                if (parent->pos[1] == '\n') {
                    parent->pos++;
                    break;
                }
                if (!hop_newlines) {
                    goto ws_end;
                }
                parent->whitespace = WHITESPACE_NEWLINE;
                parent->pos++;
                if (!parent->shared->in_jsdoc && is_conflict_marker_char(*parent->pos)) hop_conflict_markers(parent);
                break;

            case ' ':
            case '\t':
                // (Indentation: runs of spaces, taken without going around the switch for each)
                do parent->pos++; while (*parent->pos == ' ');
                break;

            case '\n':
                if (!hop_newlines) {
                    goto ws_end;
                }
                parent->whitespace = WHITESPACE_NEWLINE;
                parent->pos++;
                if (parent->shared->in_jsdoc) {
                    // tsc's skipJsDocLeadingAsterisks: the `*` a comment's line starts with isn't part of the type
                    while (*parent->pos == ' ' || *parent->pos == '\t') parent->pos++;
                    if (*parent->pos == '*' && parent->pos[1] != '/') parent->pos++;
                } else if (is_conflict_marker_char(*parent->pos)) {
                    // (Only a line's start can be a conflict marker, the file's start is parse_file()'s)
                    hop_conflict_markers(parent);
                }
                break;

            case '/':
                if (!hop_comments || !hop_comment(parent)) {
                    goto ws_end;
                }
                break;

            case '\v':
            case '\f':
                parent->pos++;
                break;

            default: {
                // (Where the whitespace ends, mostly: no other ASCII byte is whitespace or U+FFFD)
                if ((unsigned char)*parent->pos < 0x80) goto ws_end;
                int size = unicode_whitespace_size(parent->pos);
                if (!size && is_replacement_char(parent->pos)) {
                    // tsc's NonTextFileMarkerTrivia: the rest of the file is trivia
                    const char* file_start = cstr_str(&parent->shared->source->contents);
                    parse_error_inner(parent, span_make(parent->shared->source, file_start, file_start), 1490,
                        cstr_lit("File appears to be binary."), true);
                    if (!parent->shared->binary_marker) parent->shared->binary_marker = parent->pos;
                    // (To the end of the file, not the next NUL byte: binary content has them, MPEG-TS chunks named .ts)
                    parent->pos = file_start + cstr_size(&parent->shared->source->contents);
                    break;
                }
                if (!size) goto ws_end;
                if (size < 0) {
                    // U+2028/U+2029 are line terminators
                    if (!hop_newlines) goto ws_end;
                    parent->whitespace = WHITESPACE_NEWLINE;
                    size = -size;
                }
                parent->pos += size;
                break;
            }
        }
        if (parent->whitespace == WHITESPACE_NONE) {
            parent->whitespace = WHITESPACE_PARSED;
        }
    }

ws_end:
    parent->whitespace_end = parent->pos;
    parent->shared->last_trivia_start = start;
    parent->shared->last_trivia_end = parent->pos;
    if (at_eof(parent, parent->pos) && !parent->shared->in_jsdoc &&
        (!parent->shared->eof_full_start || start < parent->shared->eof_full_start)) {
        parent->shared->eof_full_start = start;
    }
}

static inline void hop_whitespace(Parser* parent) {
    hop_whitespace_full(parent, true, true);
}

static inline bool hop_start(Parser* parent, const char* prefix) {
    const char* local_pos = parent->pos;
    while (*prefix) {
        if (!*local_pos || *prefix != *local_pos) {
            return false;
        }
        prefix++;
        local_pos++;
    }
    parent->pos = local_pos;
    return true;
}

static inline bool hop_char(Parser* parent, char c) {
    if (*parent->pos == c) {
        parent->pos++;
        return true;
    } else {
        return false;
    }
}

static inline bool expect_char(Parser* parent, char c) {
    if (hop_char(parent, c)) {
        return true;
    } else {
        parse_error(parent, span_from_token(parent), 1005, "'", c, "' expected.");
        return false;
    }
}

static Error* expected_token_error(Parser* parent, const char* expected) {
    return parse_error(parent, span_from_token(parent), 1005, "'", expected, "' expected.");
}

// The closing bracket of a bracket opened at `open` (tsc's parseExpectedMatchingBrackets: a missing one points at it)
static bool expect_closing(Parser* parent, char close, const char* open) {
    if (hop_char(parent, close)) return true;
    Error* error = parse_error(parent, span_from_token(parent), 1005, "'", close, "' expected.");
    error_add_related_d(error, span_make(parent->shared->source, open, open + 1), 1007,
        "The parser expected to find a '", close, "' to match the '", *open, "' token here.");
    return false;
}

// tsc's `if (!parseExpected(close))` of a bracket at `open`: the parser's last error, if it's a TS1005 (what was
// expected may have been reported at that position already), gets "The parser expected to find a '}' to match..."
static void relate_last_expected_to_open(Parser* parent, const char* open, char open_char, char close) {
    if (!vec_error_size(&parent->shared->errors)) return;
    Error* last = vec_error_back_mut(&parent->shared->errors);
    if (last->ts_code != 1005 || span_source(last->span) != parent->shared->source) return;
    error_add_related_d(last, span_make(parent->shared->source, open, open + 1), 1007,
        "The parser expected to find a '", close, "' to match the '", open_char, "' token here.");
}

// At the end of the file tsc reports at the end of the last token (createIdentifier's reportAtCurrentPosition)
static Span span_from_token_or_eof(Parser* parent) {
    const char* eof_start = parent->shared->eof_full_start;
    if (at_eof(parent, parent->pos) && eof_start && !parent->shared->in_jsdoc) return span_make(parent->shared->source, eof_start, eof_start);
    return span_from_token(parent);
}

static Error* expected_expression_error(Parser* parent) {
    return parse_error(parent, span_from_token_or_eof(parent), 1109, "Expression expected.");
}

static bool hop_escaped_keyword(Parser* parent, const char* keyword);

// tsc's fileExtensionIsOneOf(file, [.mts, .cts]): where `<T>x` and `<T>() =>` are reserved (TS7059, TS7060)
static bool source_is_mts_or_cts(Parser const* p) {
    csview path = cstr_sv(&p->shared->source->canonical_path);
    return csview_ends_with(path, ".mts") || csview_ends_with(path, ".cts");
}

// The quick tests in front of parse functions (a character no branch starts with: fail without a child parser).
// GATE_FAIL(call) fails; a -DVERIFY_GATES build makes the call anyway, gates off (GATE_FAIL then falls through), and
// panics if it succeeds
#ifdef VERIFY_GATES
static bool gate_bypass;
#define GATE_ACTIVE (!gate_bypass)
#define GATE_FAIL(call) do { \
        if (!gate_bypass) { \
            gate_bypass = true; \
            Parser v_ = parser(parent); \
            bool ok_ = (call); \
            parse_fail(&v_); \
            gate_bypass = false; \
            if (ok_) panic("gate failed what parses: ", __func__, " at ", csview_from_range(parent->pos, parent->pos + 30)); \
            return false; \
        } \
    } while (0)
#else
#define GATE_ACTIVE true
#define GATE_FAIL(call) return false
#endif

// Whether `keyword` may start at `at`: its characters, or an escape where they stop matching (hop_escaped_keyword()
// cooks one). The quick test before a probe for a keyword is worth a child parser
static inline bool may_start_keyword(const char* at, const char* keyword) {
    for (; *keyword; at++, keyword++) {
        if (*at != *keyword) return *at == '\\';
    }
    return true;
}

// The keyword probe: no child parser (nothing to backtrack), and most probes fail on the first character
__attribute__((always_inline)) static inline bool hop_start_if_ends_on_word_break(Parser* parent, const char* prefix) {
#ifdef DEBUG_BUILD
    check(!parent->has_panding_child);
#endif
    const char* pos = parent->pos;
    const char* keyword = prefix;
    if (*pos != *prefix) return *pos == '\\' && hop_escaped_keyword(parent, keyword);
    while (*prefix) {
        if (*pos != *prefix) return *pos == '\\' && hop_escaped_keyword(parent, keyword);
        prefix++;
        pos++;
    }
    if (identifier_char_size(pos, false)) return false;
    parent->pos = pos;
    return true;
}

static cstr identifier_name_value(csview name);

// A keyword spelled with unicode escapes (`\u0076ar`): tsc's scanner makes it the keyword's token, and its parser's
// nextToken() reports TS1260 where it is consumed as one. Probes that don't match a word with a `\` come here
static bool hop_escaped_keyword(Parser* parent, const char* keyword) {
    const char* start = parent->pos;
    Span word;
    if (!parse_identifier_or_keyword(parent, &word) || !memchr(word.buf, '\\', (size_t)word.size)) {
        parent->pos = start;
        return false;
    }
    cstr cooked = identifier_name_value(span_sv(word));
    bool equal = strcmp(cstr_str(&cooked), keyword) == 0;
    cstr_drop(&cooked);
    if (!equal) {
        parent->pos = start;
        return false;
    }
    parse_error(parent, word, 1260, "Keywords cannot contain escape characters.");
    return true;
}

// A word that is `keyword` once its escapes are cooked, consumed as an identifier: no TS1260 (tsc's createIdentifier
// takes its token with nextTokenWithoutCheck)
static bool hop_word_cooked(Parser* parent, const char* keyword) {
    if (!may_start_keyword(parent->pos, keyword)) return false;
    const char* start = parent->pos;
    Span word;
    if (!parse_identifier_or_keyword(parent, &word)) {
        parent->pos = start;
        return false;
    }
    cstr cooked = identifier_name_value(span_sv(word));
    bool equal = strcmp(cstr_str(&cooked), keyword) == 0;
    cstr_drop(&cooked);
    if (!equal) parent->pos = start;
    return equal;
}

static inline bool starts_with_followed_by_word_break(Parser* parent, const char* prefix) {
    // (Without a child parser unless an escape may spell the keyword: hop_escaped_keyword() reports TS1260)
    const char* at = parent->pos;
    size_t i = 0;
    for (; prefix[i]; i++) {
        if (at[i] != prefix[i]) break;
    }
    if (GATE_ACTIVE && (!prefix[i] || at[i] != '\\')) {
        bool fast = !prefix[i] && !identifier_char_size(at + i, false);
#ifdef VERIFY_GATES
        gate_bypass = true;
        bool slow = starts_with_followed_by_word_break(parent, prefix);
        gate_bypass = false;
        if (slow != fast) panic("starts_with_followed_by_word_break ", prefix);
#endif
        return fast;
    }
    Parser p = parser(parent);
    bool ret = hop_start_if_ends_on_word_break(&p, prefix);
    parse_fail(&p);
    return ret;
}

// tsc's isIdentifier: `yield` in a generator and `await` in an async function or a class static block aren't
// identifiers (where they aren't operators either, as the operand of a `++` or a type assertion, the expression is
// missing)
static bool yield_await_is_keyword_here(Parser* parent) {
    return (parent->shared->in_generator && starts_with_followed_by_word_break(parent, "yield")) ||
        ((parent->shared->in_async || parent->shared->in_static_block) && starts_with_followed_by_word_break(parent, "await"));
}

static bool hop_token(Parser* parent) {
    switch (*parent->pos) {
        case '\0':
            if (at_eof(parent, parent->pos)) return false;
            parent->pos++;
            return true;

        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
            parent->pos++;
            return true;

        case '.':
            if (parent->pos[1] == '.' && parent->pos[2] == '.') {
                parent->pos += 3;
                return true;
            } else if (is_number_char(*(parent->pos + 1))) {
                break;
            } else {
                parent->pos++;
                return true;
            }

        case '#':
            // (`#!` past the start of the file is one Unknown token, tsc's scanner after its TS18026)
            if (parent->pos[1] == '!') {
                parent->pos += 2;
                return true;
            }
            if (identifier_char_size(parent->pos + 1, true)) {
                parent->pos++;
                parse_identifier_or_keyword(parent, nullptr);
                return true;
            }
            break;

        case '<':
            // (tsc's scanner in a JSX file: `</` is one token, LessThanSlashToken, unless a comment starts)
            if (parent->pos[1] == '/' && parent->pos[2] != '*' && parent->shared->source && parent->shared->source->is_jsx) {
                parent->pos += 2;
                return true;
            }
            break;

        default: break;
    }

    union {
        Value value;
        BinaryOp bin_op;
        UnaryOp un_op;
    } parsed;

    if (parse_literal(parent, &parsed.value)) {
        value_drop(&parsed.value);
        return true;
    } else if (parse_identifier_or_keyword(parent, nullptr)) {
        return true;
    } else if (hop_start(parent, "=>")) {
        return true;
    } else if (parse_binary_op(parent, &parsed.bin_op)) {
        return true;
    } else if (parse_left_unary_op(parent, &parsed.un_op)) {
        return true;
    } else if (parse_right_unary_op(parent, &parsed.un_op)) {
        return true;
    } else {
        parent->pos += utf8_char_size(parent->pos);
        return true;
    }
}

static Span hop_token_get_span(Parser* parent) {
    Anchor a = anchor(parent);
    hop_token(parent);
    return span_from(a);
}

// Token lookahead, tsc's isStartOf* predicates. Only list recovery uses these (see ParsingContext): the parser proper
// decides by trying to parse
typedef struct Tok {
    const char* start;
    const char* end;
    bool newline_before;
    bool escaped; // A word with a unicode escape (tok_is() cooks it)
} Tok;

// The end of the token at c, tsc's scanner's idea of it but cheap: no values
static const char* tok_scan_end_ex(Parser const* p, const char* c, bool* escaped);
static const char* tok_scan_end(Parser const* p, const char* c) {
    bool escaped;
    return tok_scan_end_ex(p, c, &escaped);
}

static const char* tok_scan_end_ex(Parser const* p, const char* c, bool* escaped) {
    *escaped = false;
    if (!*c) return at_eof(p, c) ? c : c + 1;
    // (A JSX file's `</`: tsc's scanner's LessThanSlashToken, wherever it is)
    if (c[0] == '<' && c[1] == '/' && c[2] != '*' && p->shared->source && p->shared->source->is_jsx) return c + 2;
    // (`#!` past the start of the file is one Unknown token, tsc's scanner after its TS18026)
    if (c[0] == '#' && c[1] == '!') return c + 2;
    int size = identifier_char_size(c + (*c == '#'), true);
    if (size) {
        // (An escape is more than one character, like a non-ASCII one: only those are looked at)
        if (size > 1 && c[*c == '#'] == '\\') *escaped = true;
        c += size + (*c == '#');
        while ((size = identifier_char_size(c, false))) {
            if (size > 1 && *c == '\\') *escaped = true;
            c += size;
        }
        return c;
    }
    if ((*c >= '0' && *c <= '9') || (*c == '.' && c[1] >= '0' && c[1] <= '9')) {
        c++;
        while (is_number_char(*c) || ((*c == '+' || *c == '-') && (c[-1] == 'e' || c[-1] == 'E'))) c++;
        return c;
    }
    if (*c == '"' || *c == '\'') {
        char quote = *c++;
        while (*c && *c != quote && *c != '\n') {
            if (*c == '\\' && c[1]) c++;
            c++;
        }
        return *c == quote ? c + 1 : c;
    }
    if (*c == '`') {
        // A template without substitutions, or a template head (up to its `${`)
        c++;
        while (*c && *c != '`' && !(c[0] == '$' && c[1] == '{')) {
            if (*c == '\\' && c[1]) c++;
            c++;
        }
        return *c == '`' ? c + 1 : *c ? c + 2 : c;
    }
    // Only these start a punctuator of more than one character
    if (!strchr(">.=!*<&|?+-/%^", *c)) return c + utf8_char_size(c);
    // Longest punctuator first
    static const char* const puncts[] = {
        ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=",
        "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "**", "<<", ">>", nullptr,
    };
    for (const char* const* punct = puncts; *punct; punct++) {
        if ((*punct)[0] != *c) continue;
        size_t n = strlen(*punct);
        if (strncmp(c, *punct, n) == 0) {
            // `?.5` is `?` then a number
            if (n == 2 && c[0] == '?' && c[1] == '.' && c[2] >= '0' && c[2] <= '9') break;
            return c + n;
        }
    }
    return c + utf8_char_size(c);
}

static Tok tok_at(Parser* parent, const char* pos, bool newline_before) {
    // (No trivia to hop: no child parser)
    if (GATE_ACTIVE && (unsigned char)*pos > ' ' && (unsigned char)*pos < 0x80 && *pos != '/') {
        Tok fast = { .start = pos, .newline_before = newline_before };
        fast.end = tok_scan_end_ex(parent, pos, &fast.escaped);
#ifdef VERIFY_GATES
        gate_bypass = true;
        Tok slow = tok_at(parent, pos, newline_before);
        gate_bypass = false;
        if (slow.start != fast.start || slow.end != fast.end || slow.newline_before != fast.newline_before) panic("tok_at");
#endif
        return fast;
    }
    Parser p = parser(parent);
    p.pos = pos;
    p.whitespace = newline_before ? WHITESPACE_NEWLINE : WHITESPACE_NONE;
    p.whitespace_end = nullptr;
    hop_whitespace(&p);
    Tok t = { .start = p.pos, .newline_before = newline_before || p.whitespace == WHITESPACE_NEWLINE };
    t.end = tok_scan_end_ex(&p, p.pos, &t.escaped);
    parse_fail(&p);
    return t;
}

// The token at the parser's position (after whitespace, which may be hopped already)
static Tok tok_peek(Parser* parent) {
    return tok_at(parent, parent->pos, parent->pos == parent->whitespace_end && parent->whitespace == WHITESPACE_NEWLINE);
}

static Tok tok_next(Parser* parent, Tok t) {
    return tok_at(parent, t.end, false);
}

[[gnu::noinline]] static bool tok_is_escaped_keyword(Tok t, const char* text);

__attribute__((always_inline)) static inline bool tok_is(Tok t, const char* text) {
    size_t n = strlen(text);
    // (A word spelled with unicode escapes is its keyword's token all the same: tsc's scanner; the parser reports
    // TS1260 where it consumes one as a keyword)
    return ((size_t)(t.end - t.start) == n && memcmp(t.start, text, n) == 0) ||
        (__builtin_expect(t.escaped, 0) && tok_is_escaped_keyword(t, text));
}

[[gnu::noinline]] static bool tok_is_escaped_keyword(Tok t, const char* text) {
    cstr cooked = identifier_name_value(csview_from_range(t.start, t.end));
    bool equal = strcmp(cstr_str(&cooked), text) == 0;
    cstr_drop(&cooked);
    return equal;
}

static bool tok_is_eof(Tok t) {
    // (A NUL inside the file is a token of one character)
    return !*t.start && t.end == t.start;
}

// tsc's tokenIsIdentifierOrKeyword, which private names (`#x`) pass too
static bool tok_is_word(Tok t) {
    // (A lone `#` is a private name too, with TS1127: see hop_private_name())
    return identifier_char_size(t.start, true) || (*t.start == '#' && t.start[1] != '!');
}

static bool tok_is_one_of(Tok t, const char* const* words) {
    for (; *words; words++) {
        if (tok_is(t, *words)) return true;
    }
    return false;
}

static bool is_reserved_word(csview word);

// tsc's isIdentifier (reserved words and private names aren't; `yield`/`await` are, which only matters to the
// stricter predicates)
static bool tok_is_identifier(Tok t) {
    if (!identifier_char_size(t.start, true)) return false;
    if (!t.escaped) return !is_reserved_word((csview){ t.start, t.end - t.start });
    // (Spelled with escapes a reserved word is that word all the same: tsc's scanner makes `\u0065lse` ElseKeyword)
    cstr cooked = identifier_name_value((csview){ t.start, t.end - t.start });
    bool reserved = is_reserved_word(cstr_sv(&cooked));
    cstr_drop(&cooked);
    return !reserved;
}

// Any keyword tsc's scanner knows, contextual ones included (tsc's textToKeywordObj)
static const char* const all_keywords[] = {
        "abstract", "accessor", "any", "as", "asserts", "assert", "bigint", "boolean", "break", "case", "catch",
        "class", "continue", "const", "debugger", "declare", "default", "delete", "do", "else", "enum", "export",
        "extends", "false", "finally", "for", "from", "function", "get", "if", "implements", "import", "in", "infer",
        "instanceof", "interface", "intrinsic", "is", "keyof", "let", "module", "namespace", "never", "new", "null",
        "number", "object", "package", "private", "protected", "public", "override", "out", "readonly", "require",
        "global", "return", "satisfies", "set", "static", "string", "super", "switch", "symbol", "this", "throw",
        "true", "try", "type", "typeof", "undefined", "unique", "unknown", "using", "var", "void", "while", "with",
        "yield", "async", "await", "of", nullptr,
};

// tsc's isKeyword
// tsc's parseIdentifier: a reserved word is TS1359 and a missing name, left for what follows to trip over
static bool expect_non_reserved_identifier(Parser* parent, Span* ret) {
    Tok t = tok_peek(parent);
    if (tok_is_word(t) && *t.start != '#' && is_reserved_word((csview){ t.start, t.end - t.start })) {
        Span word = span_make(parent->shared->source, t.start, t.end);
        parse_error(parent, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
        // (tsc's missing node is at the token's full start, where errors about it go)
        *ret = span_make(parent->shared->source, token_full_start(parent), token_full_start(parent));
        return false;
    }
    return expect_identifier(parent, ret);
}

static bool tok_is_keyword(Tok t) {
    return tok_is_word(t) && tok_is_one_of(t, all_keywords);
}

static bool tok_is_modifier(Tok t) {
    static const char* const words[] = {
        "abstract", "accessor", "async", "const", "declare", "default", "export", "in", "public", "private",
        "protected", "readonly", "static", "out", "override", nullptr,
    };
    return tok_is_word(t) && tok_is_one_of(t, words);
}

static bool tok_is_class_member_modifier(Tok t) {
    static const char* const words[] = {
        "public", "private", "protected", "readonly", "static", "override", "accessor", nullptr,
    };
    return tok_is_word(t) && tok_is_one_of(t, words);
}

static bool tok_is_string(Tok t) {
    return *t.start == '"' || *t.start == '\'';
}

static bool tok_is_number(Tok t) {
    return (*t.start >= '0' && *t.start <= '9') || (*t.start == '.' && t.start[1] >= '0' && t.start[1] <= '9');
}

static bool tok_is_template(Tok t) {
    return *t.start == '`';
}

// tsc's isLiteralPropertyName
static bool tok_is_literal_property_name(Tok t) {
    return tok_is_word(t) || tok_is_string(t) || tok_is_number(t);
}

// tsc's canParseSemicolon at t
static bool tok_can_parse_semicolon(Tok t) {
    return tok_is(t, ";") || tok_is(t, "}") || tok_is_eof(t) || t.newline_before;
}

// Has a binary operator precedence in tsc (getBinaryOperatorPrecedence > 0): not assignments, `,`, `?`, `=>`
static bool tok_is_binary_operator(Tok t) {
    static const char* const ops[] = {
        "??", "||", "&&", "|", "^", "&", "==", "!=", "===", "!==", "<", ">", "<=", ">=", "<<", ">>", ">>>", "+", "-",
        "*", "/", "%", "**", nullptr,
    };
    static const char* const words[] = { "instanceof", "in", "as", "satisfies", nullptr };
    return tok_is_word(t) ? tok_is_one_of(t, words) : tok_is_one_of(t, ops);
}

// tsc's isBinaryOperator: not `in` in a DisallowIn context (ParserShared.disallow_in)
static bool tok_is_binary_operator_here(Parser* p, Tok t) {
    return tok_is_binary_operator(t) && !(p->shared->disallow_in && tok_is(t, "in"));
}

// tsc's isAssignmentOperator
static bool tok_is_assignment_operator(Tok t) {
    static const char* const ops[] = {
        "=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=", "||=", "?\?=", nullptr,
    };
    return tok_is_one_of(t, ops);
}

// tsc's isIdentifier: `yield` in a generator and `await` in an await context are keywords
static bool tok_is_identifier_here(Parser* p, Tok t) {
    if (!tok_is_identifier(t)) return false;
    if (tok_is(t, "await")) return !p->shared->in_async && !p->shared->in_static_block;
    if (tok_is(t, "yield")) return !p->shared->in_generator;
    return true;
}

static bool tok_is_start_of_left_hand_side_expression(Parser* p, Tok t) {
    static const char* const words[] = { "this", "super", "null", "true", "false", "function", "class", "new", nullptr };
    if (tok_is_number(t) || tok_is_string(t) || tok_is_template(t)) return true;
    switch (*t.start) {
        case '(': case '[': case '{': case '/': return true;
        default: break;
    }
    if (tok_is(t, "import")) {
        Tok next = tok_next(p, t);
        return tok_is(next, "(") || tok_is(next, "<") || tok_is(next, ".");
    }
    return tok_is_one_of(t, words) || tok_is_identifier_here(p, t);
}

// tsc's isStartOfExpression
static bool tok_is_start_of_expression(Parser* p, Tok t) {
    static const char* const ops[] = { "+", "-", "~", "!", "++", "--", "<", "@", nullptr };
    static const char* const words[] = { "delete", "typeof", "void", "await", "yield", nullptr };
    if (tok_is_start_of_left_hand_side_expression(p, t)) return true;
    if (*t.start == '#' && t.start[1] != '!') return true; // (`#!` is tsc's Unknown token)
    return tok_is_one_of(t, ops) || tok_is_one_of(t, words) || tok_is_binary_operator_here(p, t) || tok_is_identifier(t);
}

// tsc's isDeclaration (a lookahead from t)
static bool tok_is_start_of_declaration(Parser* p, Tok t) {
    static const char* const declarations[] = { "var", "let", "const", "function", "class", "enum", nullptr };
    static const char* const modifiers[] = {
        "abstract", "accessor", "async", "declare", "private", "protected", "public", "readonly", nullptr,
    };
    while (true) {
        if (!tok_is_word(t)) return false;
        if (tok_is_one_of(t, declarations)) return true;
        if (tok_is(t, "using") || tok_is(t, "await")) {
            Tok next = tok_next(p, t);
            if (tok_is(t, "await")) {
                if (!tok_is(next, "using") || next.newline_before) return false;
                next = tok_next(p, next);
            }
            return !next.newline_before && (tok_is(next, "{") || tok_is_identifier(next));
        }
        if (tok_is(t, "interface") || tok_is(t, "type")) {
            Tok next = tok_next(p, t);
            return !next.newline_before && tok_is_identifier(next);
        }
        if (tok_is(t, "module") || tok_is(t, "namespace")) {
            Tok next = tok_next(p, t);
            return !next.newline_before && (tok_is_identifier(next) || tok_is_string(next));
        }
        if (tok_is_one_of(t, modifiers)) {
            Tok next = tok_next(p, t);
            if (next.newline_before) return false;
            if (tok_is(t, "declare") && tok_is(next, "type")) return true;
            t = next;
            continue;
        }
        if (tok_is(t, "global")) {
            Tok next = tok_next(p, t);
            return tok_is(next, "{") || tok_is_identifier(next) || tok_is(next, "export");
        }
        if (tok_is(t, "import")) {
            Tok next = tok_next(p, t);
            return tok_is_string(next) || tok_is(next, "*") || tok_is(next, "{") || tok_is_word(next);
        }
        if (tok_is(t, "export")) {
            Tok next = tok_next(p, t);
            Tok after = tok_is(next, "type") ? tok_next(p, next) : next;
            if (tok_is(after, "=") || tok_is(after, "*") || tok_is(after, "{") || tok_is(after, "default") ||
                tok_is(after, "as") || tok_is(after, "@")
            ) {
                return true;
            }
            t = next;
            continue;
        }
        if (tok_is(t, "static")) {
            t = tok_next(p, t);
            continue;
        }
        return false;
    }
}

// tsc's isStartOfStatement
static bool tok_is_start_of_statement(Parser* p, Tok t) {
    static const char* const puncts[] = { "@", ";", "{", nullptr };
    static const char* const words[] = {
        "var", "let", "using", "function", "class", "enum", "if", "do", "while", "for", "continue", "break", "return",
        "with", "switch", "throw", "try", "debugger", "catch", "finally",
        "async", "declare", "interface", "module", "namespace", "type", "global", nullptr,
    };
    static const char* const member_modifiers[] = {
        "accessor", "public", "private", "protected", "static", "readonly", nullptr,
    };
    if (tok_is_one_of(t, puncts)) return true;
    if (!tok_is_word(t)) return tok_is_start_of_expression(p, t);
    if (tok_is_one_of(t, words)) return true;
    if (tok_is(t, "import")) {
        if (tok_is_start_of_declaration(p, t)) return true;
        Tok next = tok_next(p, t);
        return tok_is(next, "(") || tok_is(next, "<") || tok_is(next, ".");
    }
    if (tok_is(t, "const") || tok_is(t, "export")) return tok_is_start_of_declaration(p, t);
    if (tok_is_one_of(t, member_modifiers)) {
        if (tok_is_start_of_declaration(p, t)) return true;
        Tok next = tok_next(p, t);
        return !(tok_is_word(next) && !next.newline_before);
    }
    return tok_is_start_of_expression(p, t);
}

// tsc's isClassMemberStart
static bool tok_is_class_member_start(Parser* p, Tok t) {
    if (tok_is(t, "@")) return true;
    Tok id = {0};
    while (tok_is_modifier(t)) {
        if (tok_is_class_member_modifier(t)) return true;
        id = t;
        t = tok_next(p, t);
    }
    if (tok_is(t, "*")) return true;
    if (tok_is_literal_property_name(t)) {
        id = t;
        t = tok_next(p, t);
    }
    if (tok_is(t, "[")) return true;
    if (id.start) {
        if (!tok_is_keyword(id) || tok_is(id, "set") || tok_is(id, "get")) return true;
        static const char* const follows[] = { "(", "<", "!", ":", "=", "?", nullptr };
        return tok_is_one_of(t, follows) || tok_can_parse_semicolon(t);
    }
    return false;
}

// tsc's isTypeMemberStart
static bool tok_is_type_member_start(Parser* p, Tok t) {
    if (tok_is(t, "(") || tok_is(t, "<") || tok_is(t, "get") || tok_is(t, "set")) return true;
    bool id = false;
    while (tok_is_modifier(t)) {
        id = true;
        t = tok_next(p, t);
    }
    if (tok_is(t, "[")) return true;
    if (tok_is_literal_property_name(t)) {
        id = true;
        t = tok_next(p, t);
    }
    static const char* const follows[] = { "(", "<", "?", ":", ",", nullptr };
    return id && (tok_is_one_of(t, follows) || tok_can_parse_semicolon(t));
}

// Every list being parsed is a bit of ParserShared.parsing_context (tsc's ParsingContext, same names). When an element
// can't be parsed, the list reports its error and then gives up if the current token belongs to any list being parsed
// (it starts an element or ends the list), else skips one token and goes on (tsc's abortParsingListOrMoveToNextToken).
// The mask only matters to recovery: on valid code no list ever consults it
typedef enum ParsingContext {
    PARSING_SOURCE_ELEMENTS,
    PARSING_BLOCK_STATEMENTS,
    PARSING_SWITCH_CLAUSES,
    PARSING_SWITCH_CLAUSE_STATEMENTS,
    PARSING_TYPE_MEMBERS,
    PARSING_CLASS_MEMBERS,
    PARSING_ENUM_MEMBERS,
    PARSING_OBJECT_LITERAL_MEMBERS,
    PARSING_PARAMETERS,
    PARSING_ARGUMENT_EXPRESSIONS,
    PARSING_ARRAY_LITERAL_MEMBERS,
    PARSING_VARIABLE_DECLARATIONS,
    PARSING_HERITAGE_CLAUSES,
    PARSING_HERITAGE_CLAUSE_ELEMENT,
    PARSING_ARRAY_BINDING_ELEMENTS,
    PARSING_IMPORT_OR_EXPORT_SPECIFIERS,
    PARSING_OBJECT_BINDING_ELEMENTS,
    PARSING_TUPLE_ELEMENT_TYPES,
    // A JSX element's children (parse_jsx_children()): every token is one (JSX text), so any list inside them, an arrow
    // body's statements in a `{...}` child, gives up at a token it can't use and unwinds to the children
    PARSING_JSX_CHILDREN,
    PARSING_CONTEXT_COUNT,
} ParsingContext;

static bool parse_list_member(Parser* parent, ParsingContext ctx, bool is_type, bool is_abstract, ClassItem* ret);
static bool parse_append_statement_sequence(Parser* parent, bool toplevel, ParsingContext ctx, const char* list_start, VecStatement* ret);

// tsc's isStartOfType(inStartOfParameter = true)
static bool tok_is_start_of_parameter_type(Tok t) {
    static const char* const words[] = {
        "any", "unknown", "string", "number", "bigint", "boolean", "readonly", "symbol", "unique", "void",
        "undefined", "null", "this", "typeof", "never", "new", "true", "false", "object", "infer", "import", "asserts",
        nullptr,
    };
    static const char* const puncts[] = { "{", "[", "<", "|", "&", "*", "?", "!", "...", nullptr };
    return tok_is_string(t) || tok_is_number(t) || tok_is_template(t) || tok_is_one_of(t, puncts) ||
        tok_is_one_of(t, words) || tok_is_identifier(t);
}

// tsc's isStartOfParameter
static bool tok_is_start_of_parameter(Tok t) {
    return tok_is(t, "...") || tok_is(t, "{") || tok_is(t, "[") || tok_is(t, "@") || tok_is_modifier(t) ||
        (*t.start == '#' && tok_is_word(t)) || tok_is_identifier(t) || tok_is_start_of_parameter_type(t);
}

// tsc's isStartOfType(inStartOfParameter = false). (`(` only before what can start a parenthesized or function type,
// nextIsParenthesizedOrFunctionType)
static bool tok_is_start_of_type(Parser* p, Tok t) {
    if (tok_is_start_of_parameter_type(t) || tok_is(t, "function")) return true;
    if (tok_is(t, "-")) return tok_is_number(tok_next(p, t));
    if (tok_is(t, "(")) {
        Tok next = tok_next(p, t);
        return tok_is(next, ")") || tok_is_start_of_parameter(next) || tok_is_start_of_type(p, next);
    }
    return false;
}

// tsc's isListElement(HeritageClauseElement): an expression, but `{}` only if it can't be the body, and not the keyword
// of the next clause (`extends A implements B`)
static bool tok_is_heritage_clause_element(Parser* p, Tok t, bool in_error_recovery) {
    if (tok_is(t, "{")) {
        // tsc's isValidHeritageClauseObjectLiteral: `extends {} {`, `extends {},`, `extends {} extends`...
        Tok next = tok_next(p, t);
        if (!tok_is(next, "}")) return true;
        Tok after = tok_next(p, next);
        return tok_is(after, ",") || tok_is(after, "{") || tok_is(after, "extends") || tok_is(after, "implements");
    }
    if ((tok_is(t, "extends") || tok_is(t, "implements")) && tok_is_start_of_expression(p, tok_next(p, t))) {
        return false;
    }
    return in_error_recovery ? tok_is_identifier_here(p, t) : tok_is_start_of_left_hand_side_expression(p, t);
}

// tsc's isListElement with inErrorRecovery (the stricter form, which is all the abort rule asks)
static bool list_is_element(Parser* p, ParsingContext ctx, Tok t) {
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
        case PARSING_BLOCK_STATEMENTS:
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            return !tok_is(t, ";") && tok_is_start_of_statement(p, t);
        case PARSING_SWITCH_CLAUSES:
            return tok_is(t, "case") || tok_is(t, "default");
        case PARSING_TYPE_MEMBERS:
            // `{ -readonly [K in T]: X }` is a mapped type, which tsc parses before it gets to any list
            return tok_is_type_member_start(p, t) ||
                ((tok_is(t, "-") || tok_is(t, "+")) && tok_is(tok_next(p, t), "readonly"));
        case PARSING_CLASS_MEMBERS:
            return tok_is_class_member_start(p, t);
        case PARSING_ENUM_MEMBERS:
            return tok_is(t, "[") || tok_is_literal_property_name(t);
        case PARSING_OBJECT_LITERAL_MEMBERS:
            return tok_is(t, "[") || tok_is(t, "*") || tok_is(t, "...") || tok_is(t, ".") ||
                tok_is_literal_property_name(t);
        case PARSING_PARAMETERS:
            return tok_is_start_of_parameter(t);
        case PARSING_ARRAY_LITERAL_MEMBERS:
            if (tok_is(t, ",") || tok_is(t, ".")) return true;
            return tok_is(t, "...") || tok_is_start_of_expression(p, t);
        case PARSING_ARGUMENT_EXPRESSIONS:
            return tok_is(t, "...") || tok_is_start_of_expression(p, t);
        case PARSING_VARIABLE_DECLARATIONS:
            return tok_is(t, "{") || tok_is(t, "[") || (*t.start == '#' && tok_is_word(t)) || tok_is_identifier(t);
        case PARSING_HERITAGE_CLAUSES:
            return tok_is(t, "extends") || tok_is(t, "implements");
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            return tok_is_heritage_clause_element(p, t, true);
        case PARSING_OBJECT_BINDING_ELEMENTS:
            // (tsgo's isLiteralPropertyName: a private identifier is one, its Kind among the identifiers)
            return tok_is(t, "[") || tok_is(t, "...") || tok_is_literal_property_name(t) || (*t.start == '#' && tok_is_word(t));
        case PARSING_ARRAY_BINDING_ELEMENTS:
            // (tsc's isBindingIdentifierOrPrivateIdentifierOrPattern)
            return tok_is(t, ",") || tok_is(t, "...") || tok_is(t, "{") || tok_is(t, "[") ||
                (*t.start == '#' && tok_is_word(t)) || (tok_is_identifier(t) && !is_reserved_word((csview){ t.start, t.end - t.start }));
        case PARSING_IMPORT_OR_EXPORT_SPECIFIERS:
            // (`{ a, from "m"`: the list stops before the `from`)
            if (tok_is(t, "from") && tok_is_string(tok_next(p, t))) return false;
            return tok_is_string(t) || tok_is_word(t);
        case PARSING_TUPLE_ELEMENT_TYPES:
            return tok_is(t, ",") || tok_is_start_of_type(p, t);
        case PARSING_JSX_CHILDREN:
            return true;
        case PARSING_CONTEXT_COUNT: break;
    }
    impossible;
}

// tsc's isListTerminator
static bool list_is_terminator(ParsingContext ctx, Tok t) {
    if (tok_is_eof(t)) return true;
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
            return false;
        case PARSING_BLOCK_STATEMENTS:
        case PARSING_SWITCH_CLAUSES:
        case PARSING_TYPE_MEMBERS:
        case PARSING_CLASS_MEMBERS:
        case PARSING_ENUM_MEMBERS:
        case PARSING_OBJECT_LITERAL_MEMBERS:
            return tok_is(t, "}");
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            return tok_is(t, "}") || tok_is(t, "case") || tok_is(t, "default");
        case PARSING_PARAMETERS:
            return tok_is(t, ")") || tok_is(t, "]");
        case PARSING_ARGUMENT_EXPRESSIONS:
            return tok_is(t, ")") || tok_is(t, ";");
        case PARSING_ARRAY_LITERAL_MEMBERS:
            return tok_is(t, "]");
        case PARSING_VARIABLE_DECLARATIONS:
            // tsc's isVariableDeclaratorListTerminator
            return tok_can_parse_semicolon(t) || tok_is(t, "in") || tok_is(t, "of") || tok_is(t, "=>");
        case PARSING_HERITAGE_CLAUSES:
            return tok_is(t, "{") || tok_is(t, "}");
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            return tok_is(t, "{") || tok_is(t, "extends") || tok_is(t, "implements");
        case PARSING_ARRAY_BINDING_ELEMENTS:
        case PARSING_TUPLE_ELEMENT_TYPES:
            return tok_is(t, "]");
        case PARSING_JSX_CHILDREN:
            return false; // (`</`, which list_is_element() already takes, like every token)
        case PARSING_OBJECT_BINDING_ELEMENTS:
            return tok_is(t, "}");
        case PARSING_IMPORT_OR_EXPORT_SPECIFIERS:
            return tok_is(t, "}");
        case PARSING_CONTEXT_COUNT: break;
    }
    impossible;
}

// tsc's parsingContextErrors: reported at the current token
static void list_report_error(Parser* p, ParsingContext ctx, Tok t) {
    Span span = span_make(p->shared->source, t.start, t.end);
    switch (ctx) {
        case PARSING_SOURCE_ELEMENTS:
            if (tok_is(t, "default")) {
                parse_error(p, span, 1005, "'export' expected.");
            } else {
                parse_error(p, span, 1128, "Declaration or statement expected.");
            }
            break;
        case PARSING_BLOCK_STATEMENTS:
            parse_error(p, span, 1128, "Declaration or statement expected.");
            break;
        case PARSING_SWITCH_CLAUSES:
            parse_error(p, span, 1130, "'case' or 'default' expected.");
            break;
        case PARSING_SWITCH_CLAUSE_STATEMENTS:
            parse_error(p, span, 1129, "Statement expected.");
            break;
        case PARSING_TYPE_MEMBERS:
            parse_error(p, span, 1131, "Property or signature expected.");
            break;
        case PARSING_CLASS_MEMBERS:
            parse_error(p, span, 1068, "Unexpected token. A constructor, method, accessor, or property was expected.");
            break;
        case PARSING_ENUM_MEMBERS:
            parse_error(p, span, 1132, "Enum member expected.");
            break;
        case PARSING_OBJECT_LITERAL_MEMBERS:
            parse_error(p, span, 1136, "Property assignment expected.");
            break;
        case PARSING_PARAMETERS:
            if (tok_is_keyword(t)) {
                cstr name = identifier_name_value(span_sv(span));
                parse_error(p, span, 1390, "'", name, "' is not allowed as a parameter name.");
                cstr_drop(&name);
            } else {
                parse_error(p, span, 1138, "Parameter declaration expected.");
            }
            break;
        case PARSING_ARGUMENT_EXPRESSIONS:
            parse_error(p, span, 1135, "Argument expression expected.");
            break;
        case PARSING_ARRAY_LITERAL_MEMBERS:
            parse_error(p, span, 1137, "Expression or comma expected.");
            break;
        case PARSING_VARIABLE_DECLARATIONS:
            if (tok_is_keyword(t)) {
                // (Named by the keyword: tsc's tokenToString, `v\u0061r` is 'var')
                cstr name = identifier_name_value(span_sv(span));
                parse_error(p, span, 1389, "'", name, "' is not allowed as a variable declaration name.");
                cstr_drop(&name);
            } else {
                parse_error(p, span, 1134, "Variable declaration expected.");
            }
            break;
        case PARSING_HERITAGE_CLAUSES:
            parse_error(p, span, 1179, "Unexpected token. '{' expected.");
            break;
        case PARSING_HERITAGE_CLAUSE_ELEMENT:
            parse_error(p, span, 1109, "Expression expected.");
            break;
        case PARSING_ARRAY_BINDING_ELEMENTS:
            parse_error(p, span, 1181, "Array element destructuring pattern expected.");
            break;
        case PARSING_OBJECT_BINDING_ELEMENTS:
            parse_error(p, span, 1180, "Property destructuring pattern expected.");
            break;
        case PARSING_IMPORT_OR_EXPORT_SPECIFIERS:
            if (tok_is(t, "from")) {
                parse_error(p, span, 1005, "'}' expected.");
            } else {
                parse_error(p, span, 1003, "Identifier expected.");
            }
            break;
        case PARSING_TUPLE_ELEMENT_TYPES:
            parse_error(p, span, 1110, "Type expected.");
            break;
        case PARSING_JSX_CHILDREN: // (Never: every token is a child)
            parse_error(p, span, 1003, "Identifier expected.");
            break;
        case PARSING_CONTEXT_COUNT: impossible;
    }
}

// Enters a list: returns the mask to restore with list_leave()
static u32 list_enter(Parser* p, ParsingContext ctx) {
    u32 saved = p->shared->parsing_context;
    p->shared->parsing_context |= 1u << ctx;
    return saved;
}

static void list_leave(Parser* p, u32 saved) {
    p->shared->parsing_context = saved;
}

// The current token can't start an element of the ctx list: reports it, then returns true if the list should end (the
// token belongs to some list being parsed), else skips the token and returns false (tsc's
// abortParsingListOrMoveToNextToken). Whitespace is hopped either way
// tsc's isInSomeParsingContext: a list being parsed (those in `mask`) takes the token as an element or ends there
static bool list_token_wanted(Parser* p, u32 mask, Tok t) {
    for (u32 i = 0; i < PARSING_CONTEXT_COUNT; i++) {
        if ((mask & (1u << i)) && (list_is_element(p, (ParsingContext)i, t) || list_is_terminator((ParsingContext)i, t))) {
            return true;
        }
    }
    return false;
}

static bool list_abort_or_skip(Parser* p, ParsingContext ctx) {
    hop_whitespace(p);
    Tok t = tok_peek(p);
    list_report_error(p, ctx, t);
    // Something that starts an element of this very list is skipped: the element parser just said no to it, where
    // tsc's would have committed and consumed it with errors
    if (list_is_terminator(ctx, t)) return true;
    if (!list_is_element(p, ctx, t) && list_token_wanted(p, p->shared->parsing_context & ~(1u << ctx), t)) return true;
    p->pos = t.end;
    hop_whitespace(p);
    return false;
}

// tsc's nextTokenCanFollowDefaultKeyword, at the token after `default`
static bool can_follow_default_keyword(Parser* p) {
    if (*p->pos == '@') return true;
    if (starts_with_followed_by_word_break(p, "class") || starts_with_followed_by_word_break(p, "function") ||
        starts_with_followed_by_word_break(p, "interface")
    ) {
        return true;
    }
    Tok t = tok_peek(p);
    if (tok_is(t, "abstract")) return tok_is(tok_next(p, t), "class") && !tok_next(p, t).newline_before;
    if (tok_is(t, "async")) return tok_is(tok_next(p, t), "function") && !tok_next(p, t).newline_before;
    return false;
}

// `permit_const`: `const` is a modifier before a name (tsc's permitConstAsModifier: class members), else only before
// `enum`
static bool parse_modifier_list_ex(Parser* parent, bool is_member, bool permit_const, ModifierList* ret);
static bool parse_modifier_list(Parser* parent, bool is_member, ModifierList* ret) {
    return parse_modifier_list_ex(parent, is_member, is_member, ret);
}

// A word with a unicode escape, `st\u0061tic`: into buf (16 bytes) as its cooked text, which *word then spans. False
// for a word without one
[[gnu::noinline]] static bool cook_escaped_word(Span* word, char* buf) {
    if (!memchr(word->buf, '\\', (size_t)word->size)) return false;
    cstr cooked = identifier_name_value(span_sv(*word));
    size_t len = (size_t)cstr_size(&cooked);
    if (len < 16) memcpy(buf, cstr_str(&cooked), len + 1);
    else buf[0] = 0, len = 0;
    cstr_drop(&cooked);
    word->buf = buf;
    word->size = (i32)len;
    return true;
}

static bool parse_modifier_list_ex(Parser* parent, bool is_member, bool permit_const, ModifierList* ret) {
    Parser p = parser(parent);
    const char* end_pos = p.pos;
    bool has_static = false;
    bool prev_was_export = false;

    *ret = (ModifierList){0};

    while (true) {
        SpannedModifier mod = {
            .tag = MODIFIER_ENUM_CAP,
            .span = {0},
        };
        if (!parse_identifier_or_keyword(&p, &mod.span)) {
            goto loop_end;
        }
        // (Spelled with escapes it is the keyword all the same, TS1260 once taken as one: tsc's scanner makes it the
        // keyword's token, its parser's nextToken() reports it)
        // (While it is looked at, mod.span is the cooked text then, restored once the modifier is taken)
        char cooked_buf[16];
        Span written = mod.span;
        bool escaped = __builtin_expect(p.shared->any_escaped_word, 0) && cook_escaped_word(&mod.span, cooked_buf);

        hop_whitespace(&p);
        // Only `static`, `export` and `default` can be followed by a line break (tsc's nextTokenCanFollowModifier):
        // `static\n x` is static, `export default\n@dec class C {}` a default export
        if (p.whitespace == WHITESPACE_NEWLINE &&
            !(is_member && span_equals(mod.span, "static")) &&
            !span_equals(mod.span, "export") &&
            !(prev_was_export && span_equals(mod.span, "default"))
        ) {
            goto loop_end;
        }

        // `export default` is followed by any expression: `-1`, `` `a` ``, `!x`
        bool export_default = prev_was_export && !is_member && span_equals(mod.span, "default");
        if (!is_identifier_char(*p.pos) && !identifier_char_size(p.pos, true) && !export_default) {
            switch (*p.pos) {
                case '[': case '"': case '\'': break;

                case '#': case '*':
                    if (!is_member) {
                        goto loop_end;
                    }
                    break;

                case '@': // `export @decorator class`
                    // (Only after `export` and `default`: tsc's canFollowModifier has no `@`, `abstract @dec class` is
                    // an expression statement `abstract`)
                    if (is_member || !(span_equals(mod.span, "export") || span_equals(mod.span, "default"))) {
                        goto loop_end;
                    }
                    break;

                case '.': // `constructor(public ...rest)`
                    if (p.pos[1] != '.' || p.pos[2] != '.') goto loop_end;
                    break;

                case '{': // `constructor(public { x })`, `export {}`; `static {` is a static block
                    if (is_member && span_equals(mod.span, "static") && !p.shared->modifiers_in_type) goto loop_end;
                    break;

                case '(':
                case '<': // `export default <div />`
                    // (tsc's canFollowModifier has no `(` or `<`: `export default async () => {}` is an async arrow,
                    // `export default declare(...)` a call of a function named `declare`)
                    if (is_member || !span_equals(mod.span, "default")) {
                        goto loop_end;
                    }
                    break;

                default:
                    // If the current parse is going to succeed at all, what we just parsed must be an identifier not a
                    // modifier. Break without handling it
                    goto loop_end;
            }
        }

        if (span_equals(mod.span, "readonly")) {
            mod.tag = MODIFIER_READONLY;
        } else if (span_equals(mod.span, "abstract")) {
            mod.tag = MODIFIER_ABSTRACT;
        } else if (span_equals(mod.span, "static")) {
            mod.tag = MODIFIER_STATIC;
            if (has_static) {
                // Why is the logic for static different? I don't know
                break;
            }
            has_static = true;
        } else if (span_equals(mod.span, "accessor")) {
            mod.tag = MODIFIER_ACCESSOR;
        } else if (span_equals(mod.span, "export")) {
            // tsc's canFollowExportModifier: `export {`, `export *`, `export as namespace` are export declarations
            if (!is_member && (*p.pos == '{' || *p.pos == '*' || starts_with_followed_by_word_break(&p, "as"))) break;
            // A member's `export default` is a modifier only before what can follow `default` (tsc's
            // nextTokenCanFollowModifier): `interface X { export default: 1 }` is a property `export`
            if (is_member && starts_with_followed_by_word_break(&p, "default")) {
                Parser q = parser(&p);
                hop_start_if_ends_on_word_break(&q, "default");
                hop_whitespace(&q);
                bool follows = can_follow_default_keyword(&q);
                parse_fail(&q);
                if (!follows) break;
            }
            mod.tag = MODIFIER_EXPORT;
        } else if (span_equals(mod.span, "default") && (prev_was_export || can_follow_default_keyword(&p))) {
            // (A member's too, tsc's nextTokenCanFollowDefaultKeyword: `interface I { default class: 1 }` has a property
            // `class` with the modifier, TS1070)
            mod.tag = MODIFIER_EXPORT_DEFAULT;
        } else if (span_equals(mod.span, "async")) {
            mod.tag = MODIFIER_ASYNC;
        } else if (span_equals(mod.span, "declare")) {
            mod.tag = MODIFIER_DECLARE;
        } else if (span_equals(mod.span, "override")) {
            mod.tag = MODIFIER_OVERRIDE;
        } else if (span_equals(mod.span, "private")) {
            mod.tag = MODIFIER_PRIVATE;
        } else if (span_equals(mod.span, "protected")) {
            mod.tag = MODIFIER_PROTECTED;
        } else if (span_equals(mod.span, "public")) {
            mod.tag = MODIFIER_PUBLIC;
        } else if (is_member && span_equals(mod.span, "const") && (permit_const || starts_with_followed_by_word_break(&p, "enum"))) {
            mod.tag = MODIFIER_CONST;
        } else if ((is_member || p.shared->modifiers_after_decorators) && span_equals(mod.span, "in")) {
            mod.tag = MODIFIER_IN; // (Only on a type parameter: TS1274)
        } else if ((is_member || p.shared->modifiers_after_decorators) && span_equals(mod.span, "out")) {
            mod.tag = MODIFIER_OUT;
        } else {
            break;
        }

        if (escaped) {
            mod.span = written;
            parse_error(&p, mod.span, 1260, "Keywords cannot contain escape characters.");
        }
        vec_spanned_modifier_push(&ret->spanned, mod);
        ret->includes[mod.tag] = true;
        end_pos = span_end_ptr(mod.span);

        prev_was_export = false;
        switch (mod.tag) {
            case MODIFIER_CONST:
                goto loop_end;

            case MODIFIER_EXPORT:
                prev_was_export = true;
                break;

            case MODIFIER_EXPORT_DEFAULT:
                // Before an expression nothing more is a modifier: `export default async x => 1` is an async arrow
                // (tsc's nextTokenCanFollowDefaultKeyword: `async` only before `function` on the same line)
                if (!can_follow_default_keyword(&p)) goto loop_end;
                break;

            default:;
        }
    }
    loop_end:

    p.pos = end_pos;
    return parse_success(&p);
}

// TypeScript-only modifiers in JavaScript (tsgo's checkJSSyntax: those of a class, class member, function or variable
// statement that aren't JavaScript's, TS8012 for a parameter's)
static void check_js_modifiers(Parser* parent, ModifierList const* me, ModifiedNode node) {
    if (!reports_js_syntax(parent) || node.parent == MODIFIED_IN_TYPE) return;
    switch (node.kind) {
        case MODIFIED_PARAMETER: {
            Span first = vec_spanned_modifier_front(&me->spanned)->span;
            Span last = vec_spanned_modifier_back(&me->spanned)->span;
            js_syntax_error(parent, span_between(first, last), 8012, "Parameter modifiers can only be used in TypeScript files.");
            return;
        }
        case MODIFIED_PROPERTY:
        case MODIFIED_METHOD:
        case MODIFIED_GET_ACCESSOR:
        case MODIFIED_SET_ACCESSOR:
        case MODIFIED_CONSTRUCTOR:
        case MODIFIED_VARIABLE_STATEMENT:
        case MODIFIED_FUNCTION:
        case MODIFIED_CLASS:
            break;
        default:
            return;
    }
    for_each (i, vec_spanned_modifier, &me->spanned) {
        // (Every modifier but JavaScript's, ModifierFlagsJavaScript: a variable statement's `const` is its keyword)
        bool reported;
        switch (i->tag) {
            case MODIFIER_EXPORT:
            case MODIFIER_EXPORT_DEFAULT:
            case MODIFIER_STATIC:
            case MODIFIER_ACCESSOR:
            case MODIFIER_ASYNC:
                reported = false;
                break;
            case MODIFIER_CONST: reported = node.kind != MODIFIED_VARIABLE_STATEMENT; break;
            default: reported = true; break;
        }
        if (reported) {
            js_syntax_error(parent, i->span, 8009, "The '", i->span, "' modifier can only be used in TypeScript files.");
        }
    }
}

static bool check_modifiers(Parser* parent, ModifierList const* me, ModifiedNode node) {
    if (vec_spanned_modifier_is_empty(&me->spanned)) return false;
    check_js_modifiers(parent, me, node);
    if (parent->shared->with_body_depth) return false;
    node.parent_ambient |= parent->shared->in_ambient;
    ErrorCtx errs = { .phase = ERROR_PHASE_PARSE };
    bool reported = modifier_list_check_grammar(me, &node, &errs);
    for_each (i, vec_error, &errs.errors) {
        i->grammar = true; // (tsc's checkGrammarModifiers)
        add_error_to_parser(parent, *i);
        *i = (Error){0};
    }
    error_ctx_drop(&errs);
    return reported;
}

// check_modifiers() without reporting: whether tsc's checkGrammarModifiers would complain about these on a node of
// this kind (node: as check_modifiers() will see it, its parent_ambient included)
static bool modifiers_would_fail(Parser* p, ModifierList const* me, ModifiedNode node, ModifiedKind kind) {
    if (vec_spanned_modifier_is_empty(&me->spanned) || p->shared->with_body_depth) return false;
    node.kind = kind;
    return modifier_list_check_grammar(me, &node, nullptr);
}

static inline bool expect_binary_op(Parser* parent, BinaryOp expected) {
    Parser p = parser(parent);
    BinaryOp actual;
    if (parse_binary_op(&p, &actual) && actual == expected) {
        parse_success(&p);
        return true;
    } else {
        parse_fail(&p);
        expected_token_error(parent, binary_op_get_zv(expected).str);
        return false;
    }
}

static bool parse_generic_decl_item(Parser* parent, TypeParamOwner owner, GenericItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // Modifiers (tsc's parseModifiers with permitConstAsModifier): `const T`, `in T`, `out T`, `in out T`, and the
    // others, which the grammar check rejects. A modifier keyword is the name when nothing that can be a name follows
    // it on the same line
    static const char* const words[] = {
        "abstract", "accessor", "async", "const", "declare", "export", "in", "public", "private", "protected",
        "readonly", "static", "out", "override", nullptr,
    };
    ModifierList modifiers = {0};
    while (true) {
        Parser modifier_p = parser(&p);
        Span word;
        bool is_modifier = parse_identifier_or_keyword(&modifier_p, &word);
        if (is_modifier) {
            is_modifier = false;
            for (const char* const* w = words; *w && !is_modifier; w++) is_modifier = csview_equals(span_sv(word), *w);
        }
        if (is_modifier) {
            hop_whitespace(&modifier_p);
            is_modifier = (modifier_p.whitespace != WHITESPACE_NEWLINE || span_equals(word, "static")) &&
                identifier_char_size(modifier_p.pos, true);
        }
        if (!is_modifier) {
            parse_fail(&modifier_p);
            break;
        }
        SpannedModifier mod = { .span = word };
        mod.tag = span_equals(word, "in") ? MODIFIER_IN : span_equals(word, "out") ? MODIFIER_OUT :
            span_equals(word, "const") ? MODIFIER_CONST : span_equals(word, "readonly") ? MODIFIER_READONLY :
            span_equals(word, "abstract") ? MODIFIER_ABSTRACT : span_equals(word, "static") ? MODIFIER_STATIC :
            span_equals(word, "accessor") ? MODIFIER_ACCESSOR : span_equals(word, "export") ? MODIFIER_EXPORT :
            span_equals(word, "async") ? MODIFIER_ASYNC : span_equals(word, "declare") ? MODIFIER_DECLARE :
            span_equals(word, "override") ? MODIFIER_OVERRIDE : span_equals(word, "private") ? MODIFIER_PRIVATE :
            span_equals(word, "protected") ? MODIFIER_PROTECTED : MODIFIER_PUBLIC;
        vec_spanned_modifier_push(&modifiers.spanned, mod);
        modifiers.includes[mod.tag] = true;
        parse_success(&modifier_p);
    }
    bool is_const = modifiers.includes[MODIFIER_CONST];
    bool is_in = modifiers.includes[MODIFIER_IN];
    bool is_out = modifiers.includes[MODIFIER_OUT];
    check_modifiers(&p, &modifiers, (ModifiedNode){ .kind = MODIFIED_TYPE_PARAMETER, .parent = MODIFIED_IN_OTHER,
        .type_param_owner = owner });
    bool had_modifiers = !vec_spanned_modifier_is_empty(&modifiers.spanned);
    modifier_list_drop(&modifiers);

    Span name;
    bool name_missing = false;
    Tok t = tok_peek(&p);
    bool reserved = tok_is_word(t) && *t.start != '#' && is_reserved_word((csview){ t.start, t.end - t.start });
    // (tsc's isIdentifier: not `await` in an await context, nor `yield` in a generator. A function's type parameters
    // are in the context around it, tsc's parseTypeParameters before the function's own flags apply)
    bool await_context = owner == TYPE_PARAM_OF_FUNCTION && p.shared->function_depth > 0 ? p.shared->outer_in_async :
        p.shared->in_async || p.shared->in_static_block;
    reserved |= (await_context && tok_is(t, "await")) || (p.shared->in_generator && tok_is(t, "yield"));
    if (reserved || !parse_identifier(&p, &name)) {
        // A type parameter starts at a modifier, or at `in` or `const` (tsc's isListElement for TypeParameters):
        // tsc's parseIdentifier then reports a reserved word (TS1359) or whatever else is there, and the name is
        // missing (`<in in>`). parse_generic_decl() skips a token an element took nothing of
        if (!had_modifiers && !tok_is(t, "in") && !tok_is(t, "const")) return parse_fail(&p);
        expect_non_reserved_identifier(&p, &name);
        name_missing = true;
    } else {
        note_top_level_await_identifier(&p, span_sv(name)); // (Any Identifier node: tsc's newIdentifier)
    }
    hop_whitespace(&p);

    TypeNode extends = type_any;
    bool extends_explicit = false;
    Span extends_span = span_null;
    if (hop_start_if_ends_on_word_break(&p, "extends")) {
        extends_explicit = true;
        hop_whitespace(&p);
        Anchor extends_a = anchor(&p);
        parse_type_or_set_to_default(&p, TYPE_PRECEDENCE_NULL, &extends);
        extends_span = span_from(extends_a);
        hop_whitespace(&p);
    }

    TypeNode fallback = type_any;
    bool fallback_explicit = false;
    Span fallback_span = span_null;
    if (hop_start(&p, "=")) {
        hop_whitespace(&p);
        Anchor fallback_a = anchor(&p);
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &fallback)) {
            fallback_span = span_from(fallback_a);
            fallback_explicit = true;
        } else {
            fallback = type_any;
        }
    }

    Span full_span = span_from(a);
    if (name_missing && !extends_explicit && !fallback_explicit) {
        // (tsc's node ends with its missing name, at the modifiers' end; without modifiers it is all missing, at
        // the name's full start)
        full_span = span_make(p.shared->source, had_modifiers ? a.start : span_start_ptr(name), span_start_ptr(name));
    }
    *ret = (GenericItem){
        .name = name,
        .full_span = full_span,
        .is_const = is_const,
        .is_in = is_in,
        .is_out = is_out,
        .extends_explicit = extends_explicit,
        .extends = extends,
        .extends_span = extends_span,
        .fallback_explicit = fallback_explicit,
        .fallback = fallback_explicit ? ast_new(TypeNode, fallback) : nullptr,
        .fallback_span = fallback_span,
    };
    return parse_success(&p);
}

static bool parse_generic_decl(Parser* parent, TypeParamOwner owner, Generic* ret) {
    Parser p = parser(parent);
    *ret = (Generic){0};

    const char* open = p.pos;
    // (In a JSX file `</` is a token of its own, tsc's LessThanSlashToken: no list)
    if (p.pos[0] == '<' && p.pos[1] == '/' && p.pos[2] != '*' && p.shared->source->is_jsx) return parse_fail(&p);
    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (*p.pos == '>' && owner != TYPE_PARAM_OF_TYPE) {
        // tsc's checkGrammarTypeParameterList (classes and function-likes): over the `<` to the `>`
        grammar_error(&p, span_make(p.shared->source, open, p.pos + 1), 1098, "Type parameter list cannot be empty.");
    }

    while (!at_eof(&p, p.pos) && *p.pos != '>') {
        GenericItem item;
        const char* item_start = p.pos;
        // (tsc's isListElement(TypeParameters): `in`, `const` or an identifier, so not another reserved word: `<T
        // export const x = ...` ends the list at the `export`, which a statement takes)
        // (No reserved word starts with an upper case letter, `_` or `$`: the common `T` needs no token)
        char c0 = *p.pos;
        bool element = (c0 >= 'A' && c0 <= 'Z') || c0 == '_' || c0 == '$';
        if (!element) {
            Tok first = tok_peek(&p);
            element = tok_is_identifier(first) || tok_is(first, "in") || tok_is(first, "const");
        }
        if (element && parse_generic_decl_item(&p, owner, &item)) {
            list_push(vec_generic_item, &ret->items, item);
            // (tsc's parseDelimitedList: an element that took nothing, `<in in>`'s second, skips its token)
            if (p.pos == item_start) hop_token(&p);
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            // (tsc's parseBracketedList(TypeParameters): what isn't an element is TS1139, the `,` taken as the delimiter)
            parse_error(&p, span_from_token(&p), 1139, "Type parameter declaration expected.");
        } else if (*p.pos == ':' && !generic_empty(ret)) {
            // (tsc's parseDelimitedList: a token nothing around can use is skipped, `<T: Bound>` of Flow)
            parse_error(&p, span_from_token(&p), 1139, "Type parameter declaration expected.");
            p.pos++;
            hop_whitespace(&p);
            continue;
        } else if ((owner != TYPE_PARAM_OF_FUNCTION || !p.shared->in_jsdoc) && !tok_is_eof(tok_peek(&p)) && *p.pos != '(' && *p.pos != '{' &&
            !tok_is(tok_peek(&p), "extends") && !tok_is(tok_peek(&p), "implements")
        ) {
            // A `<` that opened a type parameter list is one (tsc's parseTypeParameters, whose recovery is the same in a
            // speculative arrow function, which fails later if no `=>` follows: `<T, default>() => 1`; a JSDoc
            // function type's keeps failing, `{<}`'s TS1098 is parse_function_args_inner()'s): a
            // token that can't start one is TS1139 and skipped, unless a list being parsed takes it (tsc's
            // abortParsingListOrMoveToNextToken): `class <%= classify(name) %>Component` of a schematics template
            Tok t = tok_peek(&p);
            parse_error(&p, span_from_token(&p), 1139, "Type parameter declaration expected.");
            if (list_token_wanted(&p, p.shared->parsing_context, t)) break;
            p.pos = t.end;
            hop_whitespace(&p);
            continue;
        } else {
            break;
        }

        // (tsc's isListTerminator for type parameters: tokens other than `>` end the list for better recovery, the
        // `>` is what's missing then)
        Tok next = tok_peek(&p);
        if (*p.pos == '>' || *p.pos == '(' || *p.pos == '{' || tok_is(next, "extends") || tok_is(next, "implements") ||
            at_eof(&p, p.pos)
        ) {
            break;
        }
        expect_char(&p, ',');
        hop_whitespace(&p);
    }

    if (generic_empty(ret) && *p.pos != '>' && owner == TYPE_PARAM_OF_FUNCTION) {
        generic_drop(ret);
        return parse_fail(&p);
    }

    expect_char(&p, '>');
    shrink_list(vec_generic_item, &ret->items);

    return parse_success(&p);
}

static bool parse_generic_params(Parser* parent, bool fail_on_error, VecSpannedType* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = vec_spanned_type_init();
    bool unchecked = p.shared->unchecked_type_args;
    p.shared->unchecked_type_args = false;

    // (tsc's reScanLessThanToken makes only a `<<` a `<`: `<=` and `<<=` start no list, `A<=`, unless the `<` is the
    // JSDoc scanner's)
    if (!p.shared->single_less_than && p.pos[0] == '<' && (p.pos[1] == '=' || (p.pos[1] == '<' && p.pos[2] == '='))) {
        return parse_fail(&p);
    }
    if (!hop_char(&p, '<')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span trailing_comma_span = span_null;
    while (!at_eof(&p, p.pos) && *p.pos != '>') {
        trailing_comma_span = span_null;
        TypeNode t;
        Anchor type_a = anchor(&p);
        // (A private name is no start of a type, tsc's isStartOfType for the list's elements, though a type reference
        // takes it as its name where a type must be: parse_identifier_type(). Nor a `<<` or `<=` token, which isn't a `<`)
        bool less_than_token = p.pos[0] == '<' && (p.pos[1] == '<' || p.pos[1] == '=');
        if (*p.pos != '#' && !less_than_token && parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
            list_push(vec_spanned_type, ret, spanned_type(span_from(type_a), t));
            hop_whitespace(&p);
        } else if (*p.pos == ',') {
            list_push(vec_spanned_type, ret, spanned_type(span_from_pos(&p), (TypeNode){
                .tag = TYPE_ANY,
                .any.unkown_symbol_span = span_from_pos(&p),
            }));
            parse_error(&p, span_from_token(&p), 1110, "Type expected.");
        } else {
            break;
        }

        if (*p.pos != '>') {
            if (*p.pos == ',') {
                expect_char(&p, ',');
                trailing_comma_span = span_from_previous_char(&p);
            } else {
                // (tsc's isListTerminator for type arguments: any token but `,` ends the list, which then expects `>`)
                if (fail_on_error) { goto fail; }
                break;
            }
        }
        hop_whitespace(&p);
    }

    if (span_valid(trailing_comma_span) && !(p.shared->in_jsdoc && p.shared->jsdoc_unchecked) && !unchecked) {
        grammar_error(&p, trailing_comma_span, 1009, "Trailing comma not allowed.");
    }

    char const* close = p.pos;
    if (!expect_char(&p, '>')) {
        if (fail_on_error) { goto fail; }
    }

    // (tsc's checkGrammarTypeArguments: from the `<` over the token after the list, `>` or not)
    if (vec_spanned_type_is_empty(ret) && !(p.shared->in_jsdoc && p.shared->jsdoc_unchecked) && !p.shared->import_type_args &&
        !unchecked
    ) {
        grammar_error(&p, span_make(p.shared->source, a.start, close < p.pos ? p.pos : close + (!at_eof(&p, close))), 1099,
            "Type argument list cannot be empty.");
    }
    shrink_list(vec_spanned_type, ret);

    return parse_success(&p);

fail:
    vec_spanned_type_drop(ret);
    return parse_fail(&p);
}

static bool parse_ternary_type(Parser* parent, char const* left_start, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    TypeNode maybe_super = {0};
    TypeNode if_true = {0};
    TypeNode if_false = {0};

    if (!hop_start_if_ends_on_word_break(&p, "extends")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool outer_disallow = p.shared->disallow_conditional;
    p.shared->disallow_conditional = true;
    parse_type(&p, TYPE_PRECEDENCE_TERNARY, &maybe_super);
    p.shared->disallow_conditional = outer_disallow;
    hop_whitespace(&p);

    expect_char(&p, '?');
    hop_whitespace(&p);

    // (A missing branch is TS1110: tsc's parseType)
    expect_type(&p, TYPE_PRECEDENCE_TERNARY, &if_true);
    hop_whitespace(&p);

    expect_char(&p, ':');
    hop_whitespace(&p);

    expect_type(&p, TYPE_PRECEDENCE_TERNARY, &if_false);

    *left_and_ret = type_ternary(ast_new(TernaryType, {
        .maybe_sub = *left_and_ret,
        .maybe_super = maybe_super,
        .if_true = if_true,
        .if_false = if_false,
        .enclosing_block = p.shared->block_open,
        .span = span_make(p.shared->source, left_start, p.pos),
    }));
    return parse_success(&p);
}

static bool parse_tuple(Parser* parent, VecTupleElement* ret) {
    Parser p = parser(parent);
    *ret = vec_tuple_element_init();

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    u32 saved_context = list_enter(&p, PARSING_TUPLE_ELEMENT_TYPES);

    while (!at_eof(&p, p.pos) && *p.pos != ']') {
        // (tsc's parseDelimitedList: what can't start an element is skipped, or ends the list where another list being
        // parsed wants it)
        if (!list_is_element(&p, PARSING_TUPLE_ELEMENT_TYPES, tok_peek(&p))) {
            if (list_abort_or_skip(&p, PARSING_TUPLE_ELEMENT_TYPES)) break;
            continue;
        }
        TupleElement elem = {0};
        char const* element_start = p.pos;

        if (hop_start(&p, "...")) {
            elem.rest = true;
        }
        hop_whitespace(&p);

        // `name: T` or `name?: T`
        Parser name_p = parser(&p);
        Span name;
        bool named = parse_identifier_or_keyword(&name_p, &name);
        if (named) {
            hop_whitespace(&name_p);
            if (hop_char(&name_p, '?')) {
                elem.optional = true;
                hop_whitespace(&name_p);
            }
            named = hop_char(&name_p, ':');
        }
        if (named) {
            parse_success(&name_p);
            elem.maybe_name = name;
            hop_whitespace(&p);
        } else {
            parse_fail(&name_p);
            elem.optional = false;
        }

        // tsc's checkNamedTupleMember: `...a?: T` (TS5085), `a: ...T` (TS5087, still a rest element)
        bool type_rest = false;
        char const* type_rest_start = p.pos;
        if (named && hop_start(&p, "...")) {
            type_rest = true;
            elem.rest = true;
            elem.rest_in_type = true;
            hop_whitespace(&p);
        }
        char const* type_start = p.pos;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &elem.type);
        if (!p.shared->in_jsdoc) elem.span = span_make(p.shared->source, element_start, p.pos);
        if (named && type_rest) {
            grammar_error(&p, span_make(p.shared->source, type_rest_start, p.pos), 5087,
                "A labeled tuple element is declared as rest with a '...' before the name, rather than before the type.");
        } else if (named && elem.rest && elem.optional) {
            grammar_error(&p, span_make(p.shared->source, element_start, p.pos), 5085, "A tuple member cannot be both optional and rest.");
        }
        // JSDoc `[T?]`: the postfix `?` that would make `T | null` marks an optional element instead
        if (!named && p.shared->in_jsdoc && p.shared->jsdoc_postfix_nullable_start == type_start &&
            p.shared->jsdoc_postfix_nullable_end == p.pos && elem.type.tag == TYPE_UNION) {
            TypeNode null_type = *vec_type_back(&elem.type.composite);
            check(null_type.synthesized);
            vec_type_pop(&elem.type.composite);
            if (vec_type_size(&elem.type.composite) == 1) {
                TypeNode inner = *vec_type_front(&elem.type.composite);
                elem.type.composite.size = 0;
                type_drop(&elem.type);
                elem.type = inner;
            }
            elem.optional = true;
        }
        // Likewise outside JSDoc, where `T?` is a TYPE_JSDOC (tsc's parseTupleElementType; after a name it's TS5086)
        // (Not `...T?`: tsc's RestTypeNode takes the whole `T?`, TS17019)
        if (!type_rest && !(elem.rest && !named) && elem.type.tag == TYPE_JSDOC && elem.type.jsdoc->postfix && !elem.type.jsdoc->non_null &&
            span_start_ptr(elem.type.jsdoc->span) == type_start
        ) {
            if (named) {
                grammar_error(&p, elem.type.jsdoc->span, 5086, "A labeled tuple element is declared as optional with a question "
                    "mark after the name and before the colon, rather than after the type.");
            }
            TypeNode* nullable = &elem.type.jsdoc->type;
            TypeNode inner = *vec_type_front(&nullable->composite);
            vec_type_pop(&nullable->composite); // (The synthesized null)
            nullable->composite.size = 0;
            type_drop(&elem.type);
            elem.type = inner;
            elem.optional = true;
        }
        hop_whitespace(&p);

        // (Not after an `infer X`, which takes no postfix `?`: tsc's optional element is a JSDocNullableType)
        if (!named && !(elem.type.tag == TYPE_INFERRED && !elem.type.parenthesized) && hop_char(&p, '?')) {
            elem.optional = true;
            hop_whitespace(&p);
        }

        vec_tuple_element_push(ret, elem);

        // (A missing `,` is reported and the list goes on: tsc's parseDelimitedList)
        if (*p.pos != ']' && !hop_char(&p, ',')) {
            if (list_is_terminator(PARSING_TUPLE_ELEMENT_TYPES, tok_peek(&p))) break;
            expect_char(&p, ',');
            if (p.pos == element_start) {
                Tok t = tok_peek(&p);
                p.pos = t.end;
            }
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    expect_char(&p, ']');

    return parse_success(&p);

}

// tsc's nextTokenIsStartOfType, roughly: after a postfix `?`, a type means a conditional type's true branch
static bool is_type_start_char(char c) {
    return is_identifier_start_char(c) || (c >= '0' && c <= '9') || (uint8_t)c >= 0x80 || c == '"' || c == '\'' ||
        c == '`' || c == '{' || c == '[' || c == '(' || c == '<' || c == '-' || c == '*' || c == '?' || c == '!' ||
        c == '|' || c == '&' || c == '.';
}

static TypeNode jsdoc_synthesized(TypeNode node) {
    node.parsed = true;
    node.synthesized = true;
    return node;
}

// `T | null` of `?T`, `T | undefined` of `T=`: a union of its own, not merged with a union T (tsc's JSDocNullableType
// and JSDocOptionalType, which print as a parenthesized union in a union)
static TypeNode jsdoc_union(TypeNode type, TypeNode added) {
    TypeNode ret = { .tag = TYPE_UNION, .parsed = true, .composite = vec_type_init() };
    vec_type_push(&ret.composite, type);
    vec_type_push(&ret.composite, added);
    return ret;
}

// A JSDoc-only type form outside JSDoc: tsc parses it all the same, then its checker says it isn't TypeScript
// (checkJSDocTypeIsInJsFile, not in JavaScript files). TS8020 is reported here; `?T`, `T?`, `!T` and `T!` become
// TYPE_JSDOC nodes, whose TS17019/TS17020 print a type
static void jsdoc_type_outside_jsdoc(Parser* p, char const* start, TypeNode* ret, bool nullable, bool non_null, bool postfix) {
    Span span = span_make(p->shared->source, start, p->pos);
    if (!nullable && !non_null) {
        if (!p->shared->source->is_js) {
            grammar_error(p, span, 8020, "JSDoc types can only be used inside documentation comments.");
        }
        return;
    }
    TypeNode meaning = *ret;
    meaning.parsed = true;
    *ret = (TypeNode){
        .tag = TYPE_JSDOC,
        .parsed = true,
        .jsdoc = ast_new(JsdocType, { .type = meaning, .span = span, .non_null = non_null, .postfix = postfix }),
    };
}

// JSDoc's type forms that come first: `*` (any), `?T` (T | null), `!T` (T). tsgo has no Closure `function(...)` (a
// type reference to `function`) nor a standalone `?` (a missing type)
static bool parse_jsdoc_prefix_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    bool in_jsdoc = p.shared->in_jsdoc;
    if (hop_char(&p, '*')) {
        *ret = type_any;
        if (!in_jsdoc) jsdoc_type_outside_jsdoc(&p, a.start, ret, false, false, false);
        return parse_success(&p);
    }
    if (hop_char(&p, '?')) {
        hop_whitespace(&p);
        TypeNode inner;
        // (tsgo's parseJSDocNullableType: a type operator or higher, `?A | B` is `(A | null) | B`, no leading `|`)
        if (*p.pos == '|' || *p.pos == '&') {
            parse_error(&p, span_from_token(&p), 1110, "Type expected.");
            inner = (TypeNode){ .tag = TYPE_ANY, .any.unkown_symbol_span = span_from_pos(&p) };
        } else {
            expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &inner);
        }
        *ret = jsdoc_union(inner, jsdoc_synthesized(type_null));
        ret->jsdoc_nullable = true;
        if (!in_jsdoc) jsdoc_type_outside_jsdoc(&p, a.start, ret, true, false, false);
        return parse_success(&p);
    }
    if (hop_char(&p, '!')) {
        hop_whitespace(&p);
        if (*p.pos == '|' || *p.pos == '&') {
            parse_error(&p, span_from_token(&p), 1110, "Type expected.");
            *ret = (TypeNode){ .tag = TYPE_ANY, .any.unkown_symbol_span = span_from_pos(&p) };
        } else {
            expect_type(&p, TYPE_PRECEDENCE_OPERATOR, ret);
        }
        ret->jsdoc_non_null = in_jsdoc;
        if (!in_jsdoc) jsdoc_type_outside_jsdoc(&p, a.start, ret, false, true, false);
        return parse_success(&p);
    }
    return parse_fail(&p);
}

// JSDoc's `T=` (optional: T | undefined), which only ends a whole JSDoc type or a parameter of `function()` (tsc's
// parseJSDocType): `(a: A) => R=` is an optional function, not one returning `R | undefined`
static void hop_jsdoc_optional(Parser* parent, TypeNode* type) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (p.pos[0] != '=' || p.pos[1] == '>' || p.pos[1] == '=') {
        parse_fail(&p);
        return;
    }
    p.pos++;
    u32 pos = type->pos;
    *type = jsdoc_union(*type, jsdoc_synthesized(type_undefined));
    type->pos = pos; // (tsgo's JSDocOptionalType: from T's start past the `=`, decl_map_type_end())
    type->jsdoc_optional = true;
    type->parsed = true;
    parse_success(&p);
}

// JSDoc's postfix type forms: `T?` (T | null), `T!` (T). Outside JSDoc (jsdoc_type_outside_jsdoc()) only on the
// same line, and not the first character of a longer token (`!=`, `??`, `?.`)
static bool parse_jsdoc_postfix_type(Parser* parent, char const* left_start, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    bool in_jsdoc = p.shared->in_jsdoc;
    if (!in_jsdoc && (p.whitespace == WHITESPACE_NEWLINE || (p.pos[0] == '!' && p.pos[1] == '=') ||
        (p.pos[0] == '?' && (p.pos[1] == '?' || (p.pos[1] == '.' && !(p.pos[2] >= '0' && p.pos[2] <= '9')))))
    ) {
        return parse_fail(&p);
    }
    if (hop_char(&p, '!')) {
        if (!in_jsdoc) jsdoc_type_outside_jsdoc(&p, left_start, left_and_ret, false, true, true);
        return parse_success(&p);
    }
    if (hop_char(&p, '?')) {
        Parser next = parser(&p);
        hop_whitespace(&next);
        bool type_follows = is_type_start_char(*next.pos) && !(next.pos[0] == '*' && next.pos[1] == '/');
        parse_fail(&next);
        if (!type_follows) {
            *left_and_ret = jsdoc_union(*left_and_ret, jsdoc_synthesized(type_null));
            left_and_ret->jsdoc_nullable = true;
            if (in_jsdoc) {
                p.shared->jsdoc_postfix_nullable_start = left_start;
                p.shared->jsdoc_postfix_nullable_end = p.pos;
            } else {
                jsdoc_type_outside_jsdoc(&p, left_start, left_and_ret, true, false, true);
            }
            return parse_success(&p);
        }
    }
    return parse_fail(&p);
}

// `left_start`: where the type on the left began (the span of `T[K]`)
// tsc's parseFunctionOrConstructorTypeToError: a function or constructor type written as a union or intersection
// member (its return type takes the rest of the union), on its range from the operator's end
static void check_unparenthesized_function_type(Parser* p, const char* operand_pos, TypeNode const* type, bool in_union) {
    if (type->tag != TYPE_CALLABLE || type->parenthesized || p->shared->in_jsdoc) return;
    const char* start = operand_pos;
    while (is_regex_space(*start)) start++;
    if (*start != '(' && *start != '<' && *start != 'n' && *start != 'a') return;
    const char* end = p->pos;
    while (end > operand_pos && is_regex_space(end[-1])) end--;
    bool constructor = type->callable->function->is_constructor;
    Span span = span_make(p->shared->source, operand_pos, end);
    if (constructor) {
        parse_error(p, span, in_union ? 1386 : 1388, "Constructor type notation must be parenthesized when used in ",
            in_union ? "a union" : "an intersection", " type.");
    } else {
        parse_error(p, span, in_union ? 1385 : 1387, "Function type notation must be parenthesized when used in ",
            in_union ? "a union" : "an intersection", " type.");
    }
}

// (TypeNode.pos: where `at` is in the file, plus one)
static u32 type_pos(Parser const* p, char const* at) {
    return p->shared->text ? (u32)(at - p->shared->text) + 1 : 0;
}

static bool parse_type_right_side_inner(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret);

// What the right side makes (an array, indexed access, union...) starts where its left side does
static bool parse_type_right_side(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret) {
    if (!parse_type_right_side_inner(parent, min_precedence, left_start, left_and_ret)) return false;
    if (parent->shared->text) left_and_ret->pos = type_pos(parent, left_start);
    return true;
}

[[gnu::noinline]] static bool parse_type_right_side_inner_slow(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_type_right_side_inner(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret) {
    // (No trivia first and none of the characters a branch below starts with: no child parser)
    char first = *parent->pos;
    if ((unsigned char)first > ' ' && (unsigned char)first < 0x80 && first != '/' && first != '!' && first != '?' &&
        first != '[' && first != '&' && first != '|' && first != 'e' && first != '\\'
    ) {
        GATE_FAIL(parse_type_right_side_inner(&v_, min_precedence, left_start, left_and_ret));
    }
    return parse_type_right_side_inner_slow(parent, min_precedence, left_start, left_and_ret);
}

[[gnu::noinline]] static bool parse_type_right_side_inner_slow(Parser* parent, TypePrecedence min_precedence, char const* left_start, TypeNode* left_and_ret) {
    Parser p = parser(parent);
    TypeNode type;

    hop_whitespace(&p);

    // An `infer X` takes no postfix (tsc's parseTypeOperatorOrHigher returns the InferType itself): `infer U[]` and
    // `[infer U?]` leave the `[` and the `?` to what follows
    bool postfix = !(left_and_ret->tag == TYPE_INFERRED && !left_and_ret->parenthesized);

    if (postfix && (*p.pos == '!' || *p.pos == '?') && parse_jsdoc_postfix_type(&p, left_start, left_and_ret)) {
        return parse_success(&p);
    }

    if (postfix && p.whitespace != WHITESPACE_NEWLINE && hop_char(&p, '[')) {
        hop_whitespace(&p);
        Anchor index_anchor = anchor(&p);
        // (Only what isStartOfType says yes to is an index, no private name: `C[#a]` is an array type missing its `]`)
        if (*p.pos != '#' && parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
            Span index_span = span_from(index_anchor);
            hop_whitespace(&p);
            expect_char(&p, ']');
            *left_and_ret = type_indexed(ast_new(IndexedType, {
                .interface = *left_and_ret,
                .index = type,
                .index_span = index_span,
                .span = span_make(p.shared->source, left_start, p.pos),
            }));
            return parse_success(&p);
        } else {
            expect_char(&p, ']');
            *left_and_ret = type_array(ast_new(TypeNode, *left_and_ret), false);
            left_and_ret->array.span = span_make(p.shared->source, left_start, p.pos);
            return parse_success(&p);
        }
    }

    // (tsc's parseType: not after a line break, where `extends` starts the next member: `a?: T\n extends?: U`)
    if (TYPE_PRECEDENCE_TERNARY >= min_precedence && p.whitespace != WHITESPACE_NEWLINE && !p.shared->disallow_conditional) {
        if (parse_ternary_type(&p, left_start, left_and_ret)) {
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_INTERSECTION >= min_precedence) {
        if (hop_char(&p, '&') && *p.pos != '&' && *p.pos != '=') {
            const char* operand_pos = p.pos;
            hop_whitespace(&p);

            // One level tighter: the caller's loop takes the next `&`, so a long chain doesn't recurse per member
            expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &type);
            check_unparenthesized_function_type(&p, operand_pos, &type, false);
            *left_and_ret = type_composite_without_simplify(TYPE_INTERSECTION, *left_and_ret, type);
            return parse_success(&p);
        }
    }

    if (TYPE_PRECEDENCE_UNION >= min_precedence) {
        if (hop_char(&p, '|') && *p.pos != '|' && *p.pos != '=') {
            const char* operand_pos = p.pos;
            hop_whitespace(&p);

            // Same: generated unions of thousands of members exist (icon names)
            expect_type(&p, TYPE_PRECEDENCE_INTERSECTION, &type);
            check_unparenthesized_function_type(&p, operand_pos, &type, true);
            *left_and_ret = type_composite_without_simplify(TYPE_UNION, *left_and_ret, type);
            return parse_success(&p);
        }
    }

    return parse_fail(&p);
}

// `undefined.x`: a dotted type reference, like the other keyword types (tsc's parseNonArrayType)
static bool undefined_then_dot(Parser* p) {
    Tok t = tok_peek(p);
    return tok_is(t, "undefined") && tok_is(tok_next(p, t), ".");
}

static bool parse_identifier_type(Parser* parent, IdentifierType* ret) {
    Parser p = parser(parent);
    VecSpannedType generic_params = {0};

    Span name;
    AstSpans namespaces = ast_spans_init();
    Anchor word = anchor(&p);
    if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
        // (A private name is an identifier name to tsc, tokenIsIdentifierOrKeyword: parseEntityNameOfTypeReference
        // takes `#a` as the name, an Identifier: TS2304, never the binder's TS18012)
        p.pos++;
        parse_identifier_or_keyword(&p, nullptr);
        name = span_from(word);
    } else if (!parse_identifier(&p, &name)) {
        // (A type reference takes the reserved words parse_identifier() refuses for expressions: `x: var` is TS2304)
        if (!starts_with_followed_by_word_break(&p, "var")) return parse_fail(&p);
        parse_identifier_or_keyword(&p, nullptr);
        name = span_from(word);
    }
    note_top_level_await_identifier(&p, span_sv(name)); // (Any Identifier node: tsc's newIdentifier)
    const char* end = p.pos;

    hop_whitespace(&p);
    while (hop_char(&p, '.')) {
        if (*p.pos == '<') { // JSDoc's `Array.<T>` (outside JSDoc tsc's checkTypeReferenceNode complains)
            if (!p.shared->in_jsdoc && !p.shared->source->is_js) {
                grammar_error(&p, span_make(p.shared->source, p.pos - 1, p.pos), 8020, "JSDoc types can only be used inside documentation comments.");
            }
            break;
        }
        ast_spans_push(&namespaces, name);
        const char* after_dot = p.pos;
        hop_whitespace(&p);
        // After the dot any identifier name goes, keywords too (`AtRule.default`), but `a.` followed by two words on
        // the next line is missing its name, reported right after the dot (tsc's parseRightSideOfDot)
        Anchor member = anchor(&p);
        Tok t = p.whitespace == WHITESPACE_NEWLINE ? tok_peek(&p) : (Tok){0};
        if (t.newline_before && tok_is_word(t) && tok_is_word(tok_next(&p, t)) && !tok_next(&p, t).newline_before) {
            name = span_make(p.shared->source, after_dot, after_dot);
            parse_error(&p, name, 1003, "Identifier expected.");
            end = after_dot;
            break;
        }
        if (hop_private_name(&p)) {
            // A private name isn't one (parseRightSideOfDot without allowPrivateIdentifiers): consumed, then a missing
            // name right after it
            name = span_from_pos(&p);
            parse_error(&p, name, 1003, "Identifier expected.");
        } else if (parse_identifier_or_keyword(&p, nullptr)) {
            name = span_from(member);
        } else {
            expect_identifier(&p, &name);
        }
        end = p.pos;
        hop_whitespace(&p);
    }

    bool heritage_shift = p.shared->heritage_type_args && p.pos[0] == '<' && p.pos[1] == '<';
    p.shared->heritage_type_args = false;
    bool augments = p.shared->jsdoc_augments_type_args;
    p.shared->jsdoc_augments_type_args = false;
    // (The JSDoc scanner's `<` comes right after the name)
    if (augments && (p.pos != end || *p.pos != '<')) heritage_shift = true;
    if (p.whitespace != WHITESPACE_NEWLINE && !heritage_shift) {
        p.shared->single_less_than = augments;
        bool parsed = parse_generic_params(&p, false, &generic_params);
        p.shared->single_less_than = false;
        if (parsed) {
            end = p.pos;
        } else {
            generic_params = vec_spanned_type_init();
        }
    }

    p.pos = end;
    *ret = (IdentifierType){
        .name = name,
        .generic_params = generic_params,
    };
    if (ast_spans_size(&namespaces)) identifier_type_rare(ret)->namespaces = namespaces;
    else ast_spans_drop(&namespaces);
    return parse_success(&p);
}

static bool parse_inferred_type(Parser* parent, InferredType* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "infer")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    expect_identifier(&p, &name);
    Span full_span = span_from(a);
    hop_whitespace(&p);

    // tsc's tryParseConstraintOfInferType: `infer U extends C ?` where a conditional type may be is the check type of
    // one (`infer U` extends C), not a constraint
    TypeNode extends = type_any;
    Parser constraint = parser(&p);
    if (hop_start_if_ends_on_word_break(&constraint, "extends")) {
        hop_whitespace(&constraint);
        bool outer_disallow = p.shared->disallow_conditional;
        p.shared->disallow_conditional = true;
        TypeNode written;
        bool typed = expect_type(&constraint, TYPE_PRECEDENCE_NULL, &written);
        p.shared->disallow_conditional = outer_disallow;
        Parser next = parser(&constraint);
        hop_whitespace(&next);
        bool question = *next.pos == '?' && next.pos[1] != '?' && next.pos[1] != '.';
        parse_fail(&next);
        if (outer_disallow || !question) {
            if (typed) extends = written;
            full_span = span_make(p.shared->source, a.start, constraint.pos);
            parse_success(&constraint);
        } else {
            if (typed) type_drop(&written);
            parse_fail(&constraint);
        }
    } else {
        parse_fail(&constraint);
    }

    *ret = (InferredType){
        .name = name,
        .full_span = full_span,
        .extends = extends,
    };
    return parse_success(&p);
}

// `import("./a")`, `import("./a").B.C<T>`, `typeof import("./a")`: an IdentifierType whose names are looked up in the
// module instead of the scope
static bool parse_import_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    IdentifierType identifier = {0};
    IdentifierTypeRare rare = { .import_prop_start = -1 };
    if (hop_start_if_ends_on_word_break(&p, "typeof")) {
        rare.import_typeof = true;
        hop_whitespace(&p);
    }
    if (!hop_start_if_ends_on_word_break(&p, "import")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    // (`import` starts an import type whatever follows: tsc's parseNonArrayType goes to parseImportType on the keyword,
    // and parseExpected reports the missing `(`)
    expect_char(&p, '(');
    hop_whitespace(&p);
    cstr path = cstr_null;
    Anchor path_anchor = anchor(&p);
    bool has_path = parse_string(&p, &path);
    Span path_span = span_from(path_anchor);
    if (has_path) {
        // (A string literal that is only the start of the type, `import("m" | "n")`, isn't the specifier: tsc's
        // parseType takes the whole union, TS1141 from the checker)
        Parser after = parser(&p);
        hop_whitespace(&after);
        bool alone = *after.pos == ')' || *after.pos == ',' || at_eof(&after, after.pos);
        parse_fail(&after);
        if (!alone) {
            cstr_drop(&path);
            path = cstr_null;
            p.pos = path_anchor.start;
            p.whitespace = WHITESPACE_NONE;
            p.whitespace_end = nullptr;
            has_path = false;
        }
    }
    if (!has_path) {
        // Any type goes, the checker says TS1141 (tsc's parseImportType, checkImportType)
        TypeNode argument;
        if (parse_type(&p, TYPE_PRECEDENCE_NULL, &argument)) {
            rare.import_argument = ast_new(TypeNode, argument);
            rare.import_argument_span = span_from(path_anchor);
        } else if (*p.pos == '.') {
            // `import.defer(...)`: tsc's parseType makes an entity name with a missing first identifier of `.defer`
            // (its error at the same position as the missing `(`, dropped), which checkImportType says TS1141 about
            IdentifierType missing = { .name = span_make(p.shared->source, p.pos, p.pos) };
            AstSpans namespaces = ast_spans_init();
            while (*p.pos == '.') {
                Parser q = parser(&p);
                hop_char(&q, '.');
                hop_whitespace(&q);
                Span name;
                if (!parse_identifier_or_keyword(&q, &name)) {
                    parse_fail(&q);
                    break;
                }
                parse_success(&q);
                ast_spans_push(&namespaces, missing.name);
                missing.name = name;
            }
            identifier_type_rare(&missing)->namespaces = namespaces;
            rare.import_argument = ast_new(TypeNode, type_identifier(ast_new(IdentifierType, missing)));
            rare.import_argument_span = span_from(path_anchor);
        } else {
            // (A missing type, TS1110, which the checker says TS1141 about: `import(>`)
            expect_type(&p, TYPE_PRECEDENCE_NULL, &argument);
            checker_error(&p, span_from_token(&p), 1141, "String literal expected.");
            rare.import_argument = ast_new(TypeNode, argument);
            rare.import_argument_span = span_from(path_anchor);
        }
    }
    hop_whitespace(&p);
    if (hop_char(&p, ',')) {
        // Import attributes: `{ with: { "resolution-mode": "import" } }` (tsc's parseImportType, which goes on without
        // the `{`: its errors are at the same position, dropped)
        hop_whitespace(&p);
        const char* open_brace = p.pos;
        expect_char(&p, '{');
        hop_whitespace(&p);
        Anchor keyword = anchor(&p);
        bool is_assert = hop_start_if_ends_on_word_break(&p, "assert");
        if (is_assert) {
            parse_error(&p, span_from(keyword), 2880, "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
        }
        if (!is_assert && !hop_start_if_ends_on_word_break(&p, "with")) expected_token_error(&p, "with");
        ImportAttributes* attributes = ast_new(ImportAttributes, {
            .keyword = span_from(keyword), .is_assert = is_assert, .elements = vec_import_attribute_init(),
        });
        hop_whitespace(&p);
        expect_char(&p, ':');
        hop_whitespace(&p);
        Anchor body = anchor(&p);
        parse_import_attributes_body(&p, attributes);
        attributes->span = span_from(body);
        rare.import_attributes = attributes;
        hop_whitespace(&p);
        // (A trailing comma: tsc's parseOptional(SyntaxKind.CommaToken))
        if (hop_char(&p, ',')) hop_whitespace(&p);
        if (!hop_char(&p, '}')) {
            expect_char(&p, '}');
            relate_last_expected_to_open(&p, open_brace, '{', '}');
        }
        hop_whitespace(&p);
    }
    // (Without the `)` the node ends at the last token it has: tsc's finishNode)
    const char* end = p.pos;
    if (!expect_char(&p, ')')) {
        while (end > a.start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    } else {
        end = p.pos;
    }
    hop_whitespace(&p);
    while (hop_char(&p, '.')) {
        hop_whitespace(&p);
        Span name;
        Anchor name_a = anchor(&p);
        if (hop_private_name(&p)) {
            // (A private name is an identifier name: tsc's parseIdentifierName, then TS2694 for it)
            name = span_from(name_a);
        } else if (!parse_identifier_or_keyword(&p, &name)) {
            if (span_valid(identifier.name)) {
                expect_identifier(&p, &name);
            } else {
                // The qualifier's first name is a type reference's (tsc's parseEntityNameOfTypeReference): missing, it
                // is TS1110, the later ones parseRightSideOfDot's TS1003
                parse_error(&p, span_from_token_or_eof(&p), 1110, "Type expected.");
                name = span_from_pos(&p);
            }
        }
        if (span_valid(identifier.name)) ast_spans_push(&rare.namespaces, identifier.name);
        identifier.name = name;
        end = p.pos;
        hop_whitespace(&p);
    }
    p.shared->unchecked_type_args = true;
    char const* args_open = p.pos;
    if (p.whitespace != WHITESPACE_NEWLINE && parse_generic_params(&p, false, &identifier.generic_params)) {
        end = p.pos;
        rare.import_type_args = true;
        rare.import_type_args_open = args_open;
    }
    p.shared->unchecked_type_args = false;
    p.pos = end;

    if (has_path) {
        rare.import_ref = add_module_ref(&p, path, path_span);
        rare.import_ref->resolution_override = import_attributes_resolution_mode(rare.import_attributes, nullptr);
        rare.import_ref->import_type = true;
        module_ref_set_attributes(rare.import_ref, rare.import_attributes, nullptr);
    } else {
        cstr_drop(&path);
    }
    rare.import_span = span_from(a);
    identifier.jsdoc = p.shared->in_jsdoc;
    identifier.jsdoc_unchecked = p.shared->in_jsdoc && p.shared->jsdoc_unchecked;
    identifier.rare = ast_new(IdentifierTypeRare, rare);
    *ret = type_identifier(ast_new(IdentifierType, identifier));
    return parse_success(&p);
}

// `new.target` (tsc's MetaProperty), as one keyword identifier
static bool parse_new_target(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (!hop_start_if_ends_on_word_break(&p, "new")) return parse_fail(&p);
    hop_whitespace(&p);
    if (!hop_char(&p, '.')) return parse_fail(&p);
    hop_whitespace(&p);
    // (Any name: tsc's checkGrammarMetaProperty says TS17012 for another one)
    if (!parse_identifier_or_keyword(&p, nullptr)) return parse_fail(&p);
    *ret = expr_identifier(span_from(a), (IdentifierExpr){ .keyword = KEYWORD_IDENTIFIER_NEW_TARGET });
    return parse_success(&p);
}

// The `import` of `import("./a")` and `import.meta`, as a keyword identifier (the call and the property access are
// parsed like any other)
static bool parse_import_keyword_expr(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (!hop_start_if_ends_on_word_break(&p, "import")) {
        return parse_fail(&p);
    }
    *ret = expr_identifier(span_from(a), (IdentifierExpr){ .keyword = KEYWORD_IDENTIFIER_IMPORT });
    Parser ahead = parser(&p);
    hop_whitespace(&ahead);
    if (*ahead.pos == '.') {
        // `import.defer(...)`, `import.defer<T>(...)`, `import.source?.(...)`: a deferred or source phase import call
        // (tsgo's IsImportCall of a phase MetaProperty callee), one identifier like the `import` of `import(...)`
        Parser defer = parser(&ahead);
        defer.pos++;
        hop_whitespace(&defer);
        bool is_source = false;
        bool is_defer = hop_start_if_ends_on_word_break(&defer, "defer") ||
            (is_source = hop_start_if_ends_on_word_break(&defer, "source"));
        if (is_defer) {
            Anchor end = anchor(&defer);
            hop_whitespace(&defer);
            // (A JavaScript file has no type arguments: `import.source<T>(...)` isn't a call there, tsc's
            // parseCallExpressionRest)
            is_defer = *defer.pos == '(' || (*defer.pos == '<' && !p.shared->source->is_js) || tok_is(tok_peek(&defer), "?.");
            if (is_defer) {
                defer.pos = end.start;
                parse_success(&defer);
                parse_success(&ahead);
                *ret = expr_identifier(span_from(a), (IdentifierExpr){
                    .keyword = KEYWORD_IDENTIFIER_IMPORT, .import_defer = !is_source, .import_source = is_source,
                });
                ahead = parser(&p);
                hop_whitespace(&ahead);
            }
        }
        if (!is_defer) {
            parse_fail(&defer);
            parse_fail(&ahead);
            p.shared->saw_import_meta = true;
            return parse_success(&p);
        }
    }
    // (A source phase import resolves nothing, no module to load: tsgo's fileloader skips IsSourcePhaseImport)
    if (ret->identifier.import_source || *ahead.pos == '?' || *ahead.pos == '<') {
        // `import<T>(...)`, `import<T>`: type arguments, which the checker rejects (TS1326, tsc's parseCallExpressionRest)
        parse_fail(&ahead);
        return parse_success(&p);
    }
    if (!hop_char(&ahead, '(')) {
        parse_fail(&ahead);
        return parse_fail(&p);
    }
    hop_whitespace(&ahead);
    cstr path = cstr_null;
    Anchor path_anchor = anchor(&ahead);
    if (parse_string(&ahead, &path)) {
        Span path_span = span_from(path_anchor);
        hop_whitespace(&ahead);
        // (a template with substitutions isn't a module name anyone can resolve)
        bool is_constant = csview_find_sv(span_sv(path_span), c_sv("${")) == c_NPOS || *path_span.buf != '`';
        if (is_constant && (*ahead.pos == ')' || *ahead.pos == ',')) {
            ret->identifier.module_ref_ = add_module_ref(&p, path, path_span);
            ret->identifier.module_ref_->syntax = MODULE_REF_DYNAMIC;
        } else {
            cstr_drop(&path);
        }
    }
    parse_fail(&ahead);
    return parse_success(&p);
}

// `import` not followed by `(`, `.` or `<` (tsc's parseMemberExpressionOrHigher: not an import call or `import.meta`)
static bool import_keyword_not_expr(Parser* parent) {
    Tok t = tok_peek(parent);
    if (!tok_is(t, "import")) return false;
    Tok next = tok_next(parent, t);
    return !tok_is(next, "(") && !tok_is(next, ".") && !tok_is(next, "<");
}

// `unique` followed by a word on its line: the type operator (tsc's parseTypeOperatorOrHigher), not a type name
static bool unique_operator_follows(Parser* p) {
    Tok t = tok_peek(p);
    if (!tok_is(t, "unique")) return false;
    Tok next = tok_next(p, t);
    return !next.newline_before && tok_is_word(next) && *next.start != '#';
}

static bool parse_unique_symbol_type(Parser* parent) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "unique")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start_if_ends_on_word_break(&p, "symbol")) {
        return parse_fail(&p);
    }
    // `unique` is an operator on the postfix type after it (tsc's parseTypeOperator): `unique symbol[]` is
    // `unique (symbol[])`, `unique symbol.x` names a type `symbol.x`
    Tok next = tok_peek(&p);
    if ((tok_is(next, "[") && !next.newline_before) || tok_is(next, ".")) return parse_fail(&p);

    return parse_success(&p);
}

// A primitive type keyword not followed by `.` (tsc's parseKeywordAndNoDot): `string.x` names a namespace `string`
__attribute__((always_inline)) static inline bool hop_type_keyword(Parser* p, char const* keyword) {
    if (*p->pos != *keyword) return false;
    char const* start = p->pos;
    if (!hop_start_if_ends_on_word_break(p, keyword)) return false;
    char const* c = p->pos;
    while (*c == ' ' || *c == '\t') c++;
    // (Only a `.`, or line breaks and comments that may hide one, need the tokenizer)
    if ((*c == '.' || *c == '/' || *c == '\n' || *c == '\r' || (unsigned char)*c >= 0x80) && *tok_peek(p).start == '.') {
        p->pos = start;
        return false;
    }
    return true;
}

static bool parse_type_inner(Parser* parent, TypePrecedence min_precedence, TypeNode* ret);

// A primitive type keyword spelled with an escape, `\u0061ny` (the keyword all the same, TS1260: tsc's scanner)
static bool hop_escaped_type_keyword(Parser* p, TypeNode* ret) {
    static struct { const char* keyword; TypeNode const* type; } const keywords[] = {
        { "any", &type_any }, { "unknown", &type_unknown }, { "never", &type_never }, { "boolean", &type_boolean },
        { "number", &type_number }, { "string", &type_string }, { "symbol", &type_symbol }, { "bigint", &type_bigint },
    };
    for (size_t i = 0; i < c_arraylen(keywords); i++) {
        char const* start = p->pos;
        if (!hop_escaped_keyword(p, keywords[i].keyword)) continue;
        char const* c = p->pos;
        while (*c == ' ' || *c == '\t') c++;
        if (*tok_peek(p).start == '.') {
            p->pos = start;
            return false;
        }
        *ret = *keywords[i].type;
        return true;
    }
    return false;
}

// `this is T` and `asserts x (is T)` in any type position (tsc's parseNonArrayType; `x is T` is only a return type's):
// a TYPE_PREDICATE, which the checker reports where a return type's predicate (FunctionImpl.predicate) isn't
static bool parse_misplaced_type_predicate(Parser* parent, TypeNode* ret) {
    // (Before the tokenizer: most types starting with `t` or `a` are `typeof`, `true` or names)
    if (!starts_with_followed_by_word_break(parent, "this") && !starts_with_followed_by_word_break(parent, "asserts")) {
        return false;
    }
    Tok t = tok_peek(parent);
    Tok next = tok_next(parent, t);
    if (next.newline_before) return false;
    if (!(tok_is(t, "this") && tok_is(next, "is")) && !(tok_is(t, "asserts") && tok_is_word(next) && *next.start != '#')) {
        return false;
    }
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    FunctionImpl f = {0};
    if (!parse_type_predicate(&p, &f)) {
        function_impl_drop(&f);
        return parse_fail(&p);
    }
    TypePredicateNode* predicate = f.predicate;
    f.predicate = nullptr;
    function_impl_drop(&f);
    predicate->span = span_from(a);
    while (predicate->span.size && is_js_ascii_space(predicate->span.buf[predicate->span.size - 1])) {
        predicate->span.size--;
    }
    *ret = (TypeNode){ .tag = TYPE_PREDICATE, .predicate = predicate };
    return parse_success(&p);
}

// A function or constructor type is tsc's parseType's own branch (parseFunctionOrConstructorType), not a primary type:
// its parameters' types are in the context the function type is in (no conditional type in a conditional's extends
// type: `A extends (x: B extends C ? D : E) => 0 ? F : G` is TS1005 at the inner `extends`), its return type isn't
// (parseReturnType)
static bool parse_function_type_keeping_context(Parser* p, bool disallow_conditional, FunctionImpl* ret) {
    p->shared->disallow_conditional = disallow_conditional;
    bool parsed = parse_function_args(p, true, true, true, false, ret);
    p->shared->disallow_conditional = false;
    return parsed;
}

static bool parse_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    bool disallow_conditional = parent->shared->disallow_conditional;
    // (A type is outside the yield and await contexts, tsc's parseType: `@await` decorates a function type's parameter)
    bool parsed;
    if (__builtin_expect(!(parent->shared->in_async | parent->shared->outer_in_async | parent->shared->in_generator), 1)) {
        parsed = parse_type_inner(parent, min_precedence, ret);
    } else {
        bool in_async = parent->shared->in_async, outer_in_async = parent->shared->outer_in_async;
        bool in_generator = parent->shared->in_generator;
        parent->shared->in_async = parent->shared->outer_in_async = parent->shared->in_generator = false;
        parsed = parse_type_inner(parent, min_precedence, ret);
        parent->shared->in_async = in_async;
        parent->shared->outer_in_async = outer_in_async;
        parent->shared->in_generator = in_generator;
    }
    parent->shared->disallow_conditional = disallow_conditional;
    if (parsed && (ret->tag == TYPE_UNION || ret->tag == TYPE_INTERSECTION)) shrink_list(vec_type, &ret->composite);
    return parsed;
}

static bool parse_type_inner(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    union {
        IdentifierType identifier;
        InferredType inferred;
        FunctionImpl func;
        TypeNode type;
        VecTupleElement tuple_elements;
    } parsed;

    bool parenthesized = false;
    uint8_t paren_extra = 0;
    bool placeholder = false;
    bool operator_operand = p.shared->type_operator_operand;
    p.shared->type_operator_operand = false;
    Span readonly_span = span_null;
    if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        readonly_span = span_from(a);
        hop_whitespace(&p);
    }

    Anchor value_anchor = anchor(&p);
    char const* left_start = value_anchor.start;
    *ret = (TypeNode){0}; // (Some branches only fill in what they're about)
    // Every branch below needs a particular first character: test it before trying the branch
    char c = *p.pos;
    bool word = identifier_char_size(p.pos, true);
    // (What a primary type contains may be a conditional type again: tsc's parseTypeOperatorOrHigher)
    bool disallow_conditional = p.shared->disallow_conditional;
    bool type_operator = (c == 'k' && starts_with_followed_by_word_break(&p, "keyof")) ||
        (c == 'i' && starts_with_followed_by_word_break(&p, "infer"));
    if (!type_operator) p.shared->disallow_conditional = false;
    bool readonly_operator_operand = false;
    if (span_valid(readonly_span) && c == 'r' && starts_with_followed_by_word_break(&p, "readonly")) {
        // `readonly readonly T[]`: the operand is a type operator, no array literal (tsc's parseTypeOperator)
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, ret);
        readonly_operator_operand = true;
    } else if ((c == '*' || c == '?' || c == '!' || c == 'f') && parse_jsdoc_prefix_type(&p, ret)) {
    } else if (c == '\\' && hop_escaped_type_keyword(&p, ret)) {
    } else if (hop_type_keyword(&p, "any")) {
        *ret = type_any;
    } else if (hop_type_keyword(&p, "unknown")) {
        *ret = type_unknown;
    } else if (hop_type_keyword(&p, "never")) {
        *ret = type_never;
    } else if (hop_start_if_ends_on_word_break(&p, "void")) {
        *ret = type_void;
    } else if (hop_type_keyword(&p, "boolean")) {
        *ret = type_boolean;
    } else if (hop_type_keyword(&p, "number")) {
        *ret = type_number;
    } else if (hop_type_keyword(&p, "string")) {
        *ret = type_string;
    } else if (hop_type_keyword(&p, "symbol")) {
        *ret = type_symbol;
    } else if (hop_type_keyword(&p, "bigint")) {
        *ret = type_bigint;
    } else if ((c == 't' || c == 'a') && parse_misplaced_type_predicate(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "this")) {
        p.shared->this_count++;
        *ret = type_identifier(ast_new(IdentifierType, {
            .name = span_from(a),
            .keyword = KEYWORD_IDENTIFIER_THIS,
        }));
    } else if (hop_start_if_ends_on_word_break(&p, "super")) {
        // (A type reference to tsc, which finds no type called super: TS2304)
        *ret = type_identifier(ast_new(IdentifierType, { .name = span_from(a) }));
    } else if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        *ret = type_string;
    } else if (hop_start_if_ends_on_word_break(&p, "keyof")) {
        hop_whitespace(&p);
        p.shared->type_operator_operand = true;
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &parsed.type);
        p.shared->type_operator_operand = false;
        *ret = type_keyof(ast_new(KeyofType, {
            .interface = parsed.type,
        }));
    } else if ((c == 't' || c == 'i') && parse_import_type(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "typeof")) {
        hop_whitespace(&p);
        Expr expr;
        ExprCtx expr_ctx = {
            .reject_operators = true,
            .entity_name = true,
        };
        // tsc's parseTypeQuery: an entity name whose first name may be a reserved word (`typeof null` looks up
        // `null`), then type arguments
        Tok t = tok_peek(&p);
        Anchor expr_a = anchor(&p);
        Span name;
        // (`yield` and `await` too, which the expression parser would take for operators in a generator or async function)
        if (tok_is_word(t) && *t.start != '#' && !tok_is(t, "this") &&
            (is_reserved_word((csview){ t.start, t.end - t.start }) || tok_is(t, "yield") || tok_is(t, "await")) &&
            parse_identifier_or_keyword(&p, &name)
        ) {
            expr = expr_identifier(name, (IdentifierExpr){0});
            expr.inner_span = span_from(expr_a);
            expr_clear_outer_span(&expr);
            while (parse_expr_right_side(&p, expr_ctx, &expr)) {}
        } else if (tok_is_word(t) && *t.start == '#' && t.start[1] != '!') {
            // A private name is an identifier name to tsc's scanner (tokenIsIdentifierOrKeyword): parseEntityName takes
            // it, and the checker can't find it (`typeof #a`, TS2304)
            hop_private_name(&p);
            expr = expr_identifier(span_from(expr_a), (IdentifierExpr){0});
            expr.inner_span = span_from(expr_a);
            expr_clear_outer_span(&expr);
            while (parse_expr_right_side(&p, expr_ctx, &expr)) {}
        } else if (!tok_is_word(t) || !parse_expr(&p, expr_ctx, &expr)) {
            parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
            expr = expr_implicit_any(span_from_pos(&p));
        }
        *ret = type_typeof(ast_new(TypeofType, {
            .expr = ast_new(Expr, expr),
            .owns_expr = true,
        }));
    } else if (c == '`' && parse_template_type(&p, ret)) {
    } else if (c == '`' && parse_string_or_tagged(&p, &ret->value.string, false)) {
        // A no-substitution template literal type isn't rescanned (tsc's parseLiteralTypeNode): an invalid escape is no
        // error there (its scanner reports them only on a rescan) and stays as written
        ret->value.tag = VALUE_STRING;
        ast_cstr_seal(&ret->value.string);
        ret->tag = TYPE_VALUE;
        ret->value_span = span_from(value_anchor);
    } else if ((word || c == '"' || c == '\'' || is_number_start_char(c)) &&
        !(c == 'u' && undefined_then_dot(&p)) && parse_type_compat_literal(&p, &ret->value)
    ) {
        ret->tag = TYPE_VALUE;
        ret->value_span = span_from(value_anchor);
    } else if (hop_char(&p, '-')) {
        hop_whitespace(&p);
        if (parse_numeric_value(&p, &ret->value)) {
            ret->tag = TYPE_VALUE;
            if (ret->value.tag == VALUE_NUMBER) ret->value.number = -ret->value.number;
            else ret->value = value_bigint_negated(&ret->value);
            ret->value_span = span_from(value_anchor);
        } else {
            return parse_fail(&p);
        }
    } else if ((c == '(' || c == '<' ||
            // (A type operator's operand is no constructor type, tsc's parseTypeOperatorOrHigher: `keyof new () => Z`
            // is a reference to a type named `new`)
            ((c == 'a' || c == 'n') && !operator_operand && !span_valid(readonly_span))) &&
        parse_function_type_keeping_context(&p, disallow_conditional, &parsed.func)
    ) {
        parsed.func.full_span = span_from(value_anchor);
        *ret = function_impl_into_type(parsed.func);
    } else if (c == 'i' && parse_inferred_type(&p, &parsed.inferred)) {
        *ret = type_inferred(ast_new(InferredType, parsed.inferred));
    } else if (c == 'u' && parse_unique_symbol_type(&p)) {
        *ret = (TypeNode){ .tag = TYPE_UNIQUE_SYMBOL };
        if (value_anchor.start == p.shared->unique_allowed_at) {
            p.shared->unique_allowed_found = value_anchor.start;
        } else {
            grammar_error(&p, span_from(value_anchor), 1335, "'unique symbol' types are not allowed here.");
        }
    } else if (c == 'u' && starts_with_followed_by_word_break(&p, "unique") && unique_operator_follows(&p)) {
        // `unique T` for a T that isn't `symbol`: tsc's TypeOperator, whose grammar check says 'symbol' expected at T;
        // the type is any
        hop_start_if_ends_on_word_break(&p, "unique");
        hop_whitespace(&p);
        Anchor operand_a = anchor(&p);
        TypeNode operand;
        expect_type(&p, TYPE_PRECEDENCE_OPERATOR, &operand);
        grammar_error(&p, span_from(operand_a), 1005, "'symbol' expected.");
        type_drop(&operand);
        *ret = type_any;
    } else if ((word || (c == '#' && identifier_char_size(p.pos + 1, true))) && parse_identifier_type(&p, &parsed.identifier)) {
        parsed.identifier.jsdoc = p.shared->in_jsdoc;
        parsed.identifier.jsdoc_unchecked = p.shared->in_jsdoc && p.shared->jsdoc_unchecked;
        *ret = type_identifier(ast_new(IdentifierType, parsed.identifier));
    } else if (c == '[' && parse_tuple(&p, &parsed.tuple_elements)) {
        *ret = type_tuple(parsed.tuple_elements, false);
        ret->tuple.span = span_from(value_anchor);
    } else if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        if (value_anchor.start == p.shared->unique_allowed_at) p.shared->unique_allowed_at = p.pos;
        expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
        hop_whitespace(&p);
        expect_char(&p, ')');
        parenthesized = true;
        // (Nested: one more around one already parenthesized, `((T))`)
        if (ret->parenthesized) paren_extra = ret->paren_extra < 3 ? ret->paren_extra + 1 : 3;
    } else if (c == '{' && parse_interface(&p, false, &ret->interface)) {
        ret->tag = TYPE_INTERFACE;
    } else if (*p.pos == '|' || *p.pos == '&') {
        *ret = *p.pos == '|' ? type_never : type_any;
        placeholder = true;
    } else {
        return parse_fail(&p);
    }
    p.shared->disallow_conditional = disallow_conditional; // (parse_type() restores it after an early return)

    ret->parsed = true;
    // (What the right side makes takes it as its left side, which starts here: past a `readonly`)
    if (p.shared->text) ret->pos = type_pos(&p, left_start);
    // (Some branches above only write part of `ret`, over what a failed attempt may have left)
    ret->parenthesized = parenthesized;
    ret->paren_extra = parenthesized ? paren_extra : 0;
    ret->placeholder = placeholder;
    if (!p.shared->in_jsdoc || ret->tag != TYPE_UNION) ret->jsdoc_optional = false;
    ret->from_jsdoc = false;
    ret->synthesized = false;

    if (span_valid(readonly_span) && !readonly_operator_operand) {
        // `readonly` applies to the whole postfix chain: `readonly T[][]` is a readonly array of `T[]`
        while (parse_type_right_side(&p, TYPE_PRECEDENCE_OPERATOR, left_start, ret)) {}
        if (ret->tag == TYPE_ARRAY && !ret->jsdoc_non_null) {
            ret->array.readonly = true;
            readonly_span = span_null;
        } else if (ret->tag == TYPE_TUPLE && !ret->jsdoc_non_null) {
            ret->tuple.readonly = true;
            readonly_span = span_null;
        }
        // (The `readonly` operator's node starts at it: what the right side makes next takes that as its left side)
        if (p.shared->text) ret->pos = type_pos(&p, a.start);
    }
    while (parse_type_right_side(&p, min_precedence, left_start, ret)) {}
    ret->parsed = true; // (Also what the right side made)
    if (p.shared->text) ret->pos = type_pos(&p, a.start);

    if (span_valid(readonly_span)) {
        ret->readonly_invalid = true;
        // (tsc's checkGrammarTypeOperatorNode: the checker's, not the parser's)
        grammar_error(
            &p,
            readonly_span,
            1354,
            "'readonly' type modifier is only permitted on array and tuple literal types."
        );
    }

    return parse_success(&p);
}

static inline bool parse_type_or_set_to_default(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        *ret = (TypeNode){0};
        return false;
    }
}

// JSDoc (JavaScript files only). tsc parses the `/** */` comments in a node's leading trivia and attaches them to it;
// here whoever parses a host looks back from its first token for the comment that ends there (jsdoc_before()) and
// moves what the tags say into the node's own slots (an `@type` is the annotation, an `@param` a parameter's...), so
// the checker can't tell a JSDoc annotation from a written one. Types in tags are parsed by the type parser, pointed
// into the comment with `in_jsdoc` set

typedef enum JsDocTagKind {
    JSDOC_TAG_OTHER = 0,
    JSDOC_TAG_TYPE,
    JSDOC_TAG_PARAM, // Also `@arg`, `@argument`
    JSDOC_TAG_RETURNS, // Also `@return`
    JSDOC_TAG_TEMPLATE,
    JSDOC_TAG_TYPEDEF,
    JSDOC_TAG_CALLBACK,
    JSDOC_TAG_PROPERTY, // Also `@prop`
    JSDOC_TAG_THIS,
    JSDOC_TAG_EXTENDS, // Also `@augments`
    JSDOC_TAG_IMPLEMENTS,
    JSDOC_TAG_CLASS, // Also `@constructor`
    JSDOC_TAG_OVERLOAD,
    JSDOC_TAG_SATISFIES,
    JSDOC_TAG_IMPORT,
    JSDOC_TAG_THROWS,
    JSDOC_TAG_PRIVATE,
    JSDOC_TAG_PROTECTED,
    JSDOC_TAG_PUBLIC,
    JSDOC_TAG_READONLY,
    JSDOC_TAG_OVERRIDE,
} JsDocTagKind;

typedef struct JsDocTag {
    JsDocTagKind kind;
    Span tag_name; // `param` of `@param`
    bool has_type;
    TypeNode type; // Of the `{T}`, owned while has_type
    Span type_span; // Inside the braces
    bool variadic; // `{...T}`
    Span name; // `@param {T} name`, `@typedef {T} Name`, `@template T`: the name (maybe dotted: `o.a`)
    bool bracketed; // `[name]`, `[name=default]`: optional
    bool name_first; // `@param name {T}` or no type at all (tsc's isNameFirst)
    Span missing_name; // Zero width, where the name of an `@param` without one should be (tsc's missing identifier)
    bool is_const; // `@template const T`
    bool is_in, is_out; // `@template in T`, `@template out T`
    Span const_span, in_span, out_span; // Of those modifiers, for the checks that depend on the host (TS1277, TS1274)
    // A `@param`/`@returns` of a `@callback`, a `@property` of a `@typedef`: part of that, not about the host
    bool nested;
    // `@returns {x is T}`, `{asserts x}`: owned, `type` is then the return type the predicate goes with
    TypePredicateNode* predicate;
    bool has_default;
    TypeNode default_type; // `@template [T=D]`, owned while has_default
    const char* default_start; // Where D starts (to parse it again)
    // From a JSDoc comment before the one right before the host: only its `@overload` tags (with their children and
    // the comment's `@template`s) are kept, nothing else about the host (tsc's filterOwnedJSDocTags)
    bool earlier;
    const char* comment; // The start of the comment the tag is in
} JsDocTag;

static void jsdoc_tag_drop(JsDocTag* me) {
    if (my has_type) type_drop(&my type);
    if (my has_default) type_drop(&my default_type);
    if (my predicate) {
        if (my predicate->has_type) type_drop(&my predicate->type);
        ast_delete(my predicate);
    }
}

static bool parse_type_predicate(Parser* parent, FunctionImpl* ret);

#define g_class_key JsDocTag, jsdoc_tag
#define g_no_clone
#include <cleat/generic/vec.h>

static inline bool is_jsdoc_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Past whitespace, newlines and the leading `*`s of lines, not past `end`
static const char* jsdoc_skip_space(const char* c, const char* end) {
    while (c < end) {
        if (is_jsdoc_space(*c)) {
            c++;
            continue;
        }
        if (*c != '*') break;
        const char* line = c;
        while (line[-1] == ' ' || line[-1] == '\t') line--;
        if (line[-1] != '\n') break;
        c++;
    }
    return c;
}

// Parses `{T}` at `*pos` (which must be the `{`) into `tag`, moves `*pos` past it. Its parse errors go to the JSDoc
// errors
static void parse_jsdoc_type_expression(Parser* parent, const char** pos, const char* end, JsDocTag* tag) {
    Parser p = parser(parent);
    p.pos = *pos + 1;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    bool outer_unchecked = p.shared->jsdoc_unchecked;
    // (A `@returns` type is the function's return type annotation to tsgo's reparser, checked like any: only `@throws`'s
    // isn't visited)
    p.shared->jsdoc_unchecked = outer_unchecked || tag->kind == JSDOC_TAG_THROWS;
    hop_whitespace(&p);
    bool variadic = hop_start(&p, "...");
    Anchor a = anchor(&p);
    // (An `@type` or `@property` type may be a whole `unique symbol`, whose host decides: tsgo's reparser makes it a
    // variable's, a class property's or a property signature's annotation. Any other tag's is TS1335)
    char const* outer_allowed_at = p.shared->unique_allowed_at;
    p.shared->unique_allowed_at = tag->kind == JSDOC_TAG_TYPE || tag->kind == JSDOC_TAG_PROPERTY ? a.start : nullptr;
    p.shared->unique_allowed_found = nullptr;
    TypeNode type;
    FunctionImpl predicate = {0};
    if (tag->kind == JSDOC_TAG_RETURNS && parse_type_predicate(&p, &predicate)) {
        type = predicate.ret;
        if (tag->predicate) {
            if (tag->predicate->has_type) type_drop(&tag->predicate->type);
            ast_delete(tag->predicate);
        }
        tag->predicate = predicate.predicate;
        tag->predicate->jsdoc = true;
    } else if (tag->kind != JSDOC_TAG_RETURNS && tok_is_identifier(tok_peek(&p)) &&
        tok_is(tok_next(&p, tok_peek(&p)), "is") && parse_type_predicate(&p, &predicate)
    ) {
        // tsc's parseJSDocType is parseTypeOrTypePredicate: `@type {x is T}` is a misplaced predicate (TS1228)
        TypePredicateNode* node = predicate.predicate;
        node->span = span_from(a);
        while (node->span.size && is_js_ascii_space(node->span.buf[node->span.size - 1])) node->span.size--;
        type = (TypeNode){ .tag = TYPE_PREDICATE, .predicate = node };
        predicate.predicate = nullptr;
        goto parsed;
    }
    if (tag->predicate || parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
    parsed:
        // (Not after an `@augments`/`@implements` class, which is no type: tsc's parseExpressionWithTypeArgumentsForAugments)
        if (!tag->predicate && tag->kind != JSDOC_TAG_EXTENDS && tag->kind != JSDOC_TAG_IMPLEMENTS) hop_jsdoc_optional(&p, &type);
        Span type_span = span_from(a);
        hop_whitespace(&p);
        if (p.pos < end) {
            if (tag->has_type) type_drop(&tag->type);
            tag->has_type = true;
            tag->type = type;
            tag->type_span = type_span;
            tag->variadic = variadic;
            if (*p.pos == '}') p.pos++;
            else parse_error(&p, span_from_token(&p), 1005, "'}' expected.");
            *pos = p.pos;
        } else {
            // (Its type all the same: tsc's parseJSDocTypeExpression keeps it, `@type {T` at the comment's end)
            if (tag->has_type) type_drop(&tag->type);
            tag->has_type = true;
            tag->type = type;
            tag->type_span = type_span;
            tag->variadic = variadic;
            *pos = end;
            parse_error(&p, span_make(p.shared->source, end, end), 1005, "'}' expected.");
        }
    } else if (!p.error_count) {
        // (No type starts there: tsc's parseJSDocType reports it at the token, `{@import(...)}`, and the tag's type is
        // a missing name, which resolves to any: `@type {}`)
        parse_error(&p, span_from_token(&p), 1110, "Type expected.");
        if (tag->has_type) type_drop(&tag->type);
        tag->has_type = true;
        tag->type = (TypeNode){ .tag = TYPE_ANY, .any.unkown_symbol_span = span_from_pos(&p) };
        tag->type_span = span_from_pos(&p);
        tag->variadic = variadic;
        const char* c = p.pos;
        while (c < end && *c != '}' && *c != '\n') c++;
        *pos = c < end && *c == '}' ? c + 1 : c;
    }
    // Past the braces even when the type didn't parse (tsc: `{` starts a type expression that ends at `}`)
    if (!tag->has_type) {
        const char* c = *pos;
        while (c < end && *c != '}' && *c != '\n') c++;
        *pos = c < end && *c == '}' ? c + 1 : c;
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    p.shared->jsdoc_unchecked = outer_unchecked;
    char const* found = p.shared->unique_allowed_found;
    p.shared->unique_allowed_found = nullptr;
    p.shared->unique_allowed_at = outer_allowed_at;
    if (found && (!tag->has_type || tag->type.tag != TYPE_UNIQUE_SYMBOL)) unique_symbol_misplaced(&p, found);
    jsdoc_keep_errors(&p);
    parse_fail(&p);
}

// A type without braces at `*pos` (`@template [T=D]`'s default), moves `*pos` past it
static bool parse_jsdoc_bare_type(Parser* parent, const char** pos, const char* end, TypeNode* ret) {
    Parser p = parser(parent);
    p.pos = *pos;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    hop_whitespace(&p);
    // (An `@augments`/`@implements` class's parse errors are reported, braces or not: tsc's
    // parseExpressionWithTypeArgumentsForAugments)
    bool keep_errors = p.shared->jsdoc_augments_type_args;
    bool parsed = parse_type(&p, TYPE_PRECEDENCE_NULL, ret) && p.pos <= end;
    if (parsed) {
        *pos = p.pos;
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    if (parsed && keep_errors) jsdoc_keep_errors(&p);
    parse_fail(&p);
    return parsed;
}

static int identifier_escape_size(const char* c);
static cstr identifier_name_value(csview name);

// A JSDoc name is a parameter's name, `\u0061` for `a` (tsc's scanner decodes escapes in identifiers)
static bool jsdoc_name_is(csview jsdoc_name, csview name) {
    if (!memchr(jsdoc_name.buf, '\\', (size_t)jsdoc_name.size)) return csview_equals_sv(jsdoc_name, name);
    cstr decoded = identifier_name_value(jsdoc_name);
    bool ret = csview_equals_sv(cstr_sv(&decoded), name);
    cstr_drop(&decoded);
    return ret;
}

static const char* jsdoc_parse_name(const char* c, const char* end, Span* ret, SourceFile const* source) {
    const char* start = c;
    // (A `-` inside a name is part of it: tsc's scanJSDocToken, `@param {T} data-test-name`)
    while (c < end && (is_identifier_char(*c) || *c == '.' || (uint8_t)*c >= 0x80 ||
        (*c == '[' && c[1] == ']') || identifier_escape_size(c) || (*c == '-' && c > start && is_identifier_char(c[-1])))
    ) {
        int escape = identifier_escape_size(c);
        // (The messages that quote it decode its escapes, name_cooked())
        if (escape) names_register_source((SourceFile*)source);
        c += escape ? escape : *c == '[' ? 2 : 1;
    }
    *ret = c > start ? span_make(source, start, c) : span_null;
    return c;
}

// `@param {T} name`, `@param name {T}`, `@param {T} [name=default]`
static const char* parse_jsdoc_param_like(Parser* parent, const char* c, const char* end, JsDocTag* tag) {
    SourceFile const* source = parent->shared->source;
    c = jsdoc_skip_space(c, end);
    if (c < end && *c == '{') {
        parse_jsdoc_type_expression(parent, &c, end, tag);
        c = jsdoc_skip_space(c, end);
    }
    if (c < end && *c == '[') {
        tag->bracketed = true;
        c = jsdoc_skip_space(c + 1, end);
        c = jsdoc_parse_name(c, end, &tag->name, source);
        // The default, up to the closing bracket
        int depth = 0;
        while (c < end && (*c != ']' || depth)) {
            if (*c == '[') depth++;
            else if (*c == ']') depth--;
            c++;
        }
        if (c < end) c++;
    } else if (c < end && *c == '`') {
        // (tsc's parseBracketNameInPropertyAndParamTag: a name in backticks)
        c = jsdoc_parse_name(c + 1, end, &tag->name, source);
        if (c < end && *c == '`') c++;
    } else {
        c = jsdoc_parse_name(c, end, &tag->name, source);
    }
    if (!span_valid(tag->name)) {
        tag->missing_name = span_make(source, c, c);
        // tsc's parseJSDocIdentifierName: TS1003 where the name should start (not again where an error is), for a
        // `@property` only (tsgo's parseBracketNameInPropertyAndParamTag gives a parameter's no message)
        VecError const* errors = &parent->shared->jsdoc_errors;
        bool reported = errors->size && span_start_ptr(errors->data[errors->size - 1].span) == c;
        if (!reported && tag->kind != JSDOC_TAG_PARAM) {
            // (At the current token: a whitespace run, a line break, or a character like `#`)
            const char* token_end = c;
            if (c < end && (*c == ' ' || *c == '\t')) {
                while (token_end < end && (*token_end == ' ' || *token_end == '\t')) token_end++;
            } else if (c + 1 < end && c[0] == '\r' && c[1] == '\n') {
                token_end += 2;
            } else if (c < end) {
                token_end++;
            }
            Parser e = parser(parent);
            parse_error(&e, span_make(source, c, token_end), 1003, "Identifier expected.");
            jsdoc_keep_errors(&e);
            parse_fail(&e);
        }
    }
    tag->name_first = !tag->has_type;
    if (!tag->has_type) {
        const char* after = jsdoc_skip_space(c, end);
        if (after < end && *after == '{') {
            parse_jsdoc_type_expression(parent, &after, end, tag);
            c = after;
        }
    }
    return c;
}

static JsDocTagKind jsdoc_tag_kind(csview name) {
    static const struct { const char* name; JsDocTagKind kind; } kinds[] = {
        {"type", JSDOC_TAG_TYPE}, {"param", JSDOC_TAG_PARAM}, {"arg", JSDOC_TAG_PARAM}, {"argument", JSDOC_TAG_PARAM},
        {"returns", JSDOC_TAG_RETURNS}, {"return", JSDOC_TAG_RETURNS}, {"template", JSDOC_TAG_TEMPLATE},
        {"typedef", JSDOC_TAG_TYPEDEF}, {"callback", JSDOC_TAG_CALLBACK}, {"property", JSDOC_TAG_PROPERTY},
        {"prop", JSDOC_TAG_PROPERTY}, {"this", JSDOC_TAG_THIS},
        {"extends", JSDOC_TAG_EXTENDS}, {"augments", JSDOC_TAG_EXTENDS}, {"implements", JSDOC_TAG_IMPLEMENTS},
        {"class", JSDOC_TAG_CLASS}, {"constructor", JSDOC_TAG_CLASS}, {"overload", JSDOC_TAG_OVERLOAD},
        {"satisfies", JSDOC_TAG_SATISFIES}, {"import", JSDOC_TAG_IMPORT}, {"throws", JSDOC_TAG_THROWS},
        {"exception", JSDOC_TAG_THROWS}, {"private", JSDOC_TAG_PRIVATE}, {"protected", JSDOC_TAG_PROTECTED},
        {"public", JSDOC_TAG_PUBLIC}, {"readonly", JSDOC_TAG_READONLY}, {"override", JSDOC_TAG_OVERRIDE},
    };
    for (u32 i = 0; i < c_arraylen(kinds); i++) {
        if (csview_equals(name, kinds[i].name)) return kinds[i].kind;
    }
    return JSDOC_TAG_OTHER;
}

// The tag at `c` (its `@`): appends it, returns where its own syntax ends (what follows is its comment)
static const char* parse_jsdoc_tag(Parser* parent, const char* c, const char* end, VecJsDocTag* tags) {
    SourceFile const* source = parent->shared->source;
    const char* name_start = ++c;
    while (c < end && (is_identifier_char(*c) || *c == '-')) c++;
    JsDocTag tag = {
        .kind = jsdoc_tag_kind(csview_from_range(name_start, c)),
        .tag_name = span_make(source, name_start, c),
    };
    switch (tag.kind) {
        case JSDOC_TAG_PARAM:
        case JSDOC_TAG_PROPERTY:
            c = parse_jsdoc_param_like(parent, c, end, &tag);
            break;
        case JSDOC_TAG_TYPEDEF:
        case JSDOC_TAG_CALLBACK:
            c = jsdoc_skip_space(c, end);
            const char* type_end = nullptr;
            bool unclosed = false;
            if (tag.kind != JSDOC_TAG_CALLBACK && c < end && *c == '{') {
                parse_jsdoc_type_expression(parent, &c, end, &tag);
                type_end = c;
                // (Its `}` missing, TS1005 there: tsgo takes no name after it)
                unclosed = tag.has_type && c > tag.tag_name.buf && c[-1] != '}';
                c = jsdoc_skip_space(c, end);
            }
            // (A name is an identifier token: `{T} 1` has none)
            if (!unclosed && !(c < end && *c >= '0' && *c <= '9')) c = jsdoc_parse_name(c, end, &tag.name, source);
            if (type_end && !span_valid(tag.name) && tag.kind == JSDOC_TAG_TYPEDEF && tag.has_type) {
                // A typedef without a name still declares one, missing (tsgo's parseJSDocIdentifierName: `(Missing)`,
                // where the next token starts, TS1003 at that token unless the `}` was missing there already).
                // skipWhitespaceOrAsterisk keeps the whitespace before the comment's end: that is the token then
                const char* at = unclosed ? type_end : c;
                if (!unclosed) {
                    const char* token_end = at;
                    if (at >= end) {
                        at = type_end;
                        token_end = at;
                        if (at < end && *at == '\n') token_end++;
                        else if (at + 1 < end && at[0] == '\r' && at[1] == '\n') token_end += 2;
                        else while (token_end < end && (*token_end == ' ' || *token_end == '\t')) token_end++;
                    } else {
                        token_end++;
                    }
                    // (None at the comment's end: `{T}*/`)
                    if (source->is_js && token_end > at) {
                        vec_error_push(&parent->shared->jsdoc_errors, (Error){
                            .message = cstr_lit("Identifier expected."), .span = span_make(source, at, token_end),
                            .ts_code = 1003, .phase = ERROR_PHASE_RESOLVE_NAMES, .jsdoc = true, // (a JSDoc diagnostic)
                        });
                    }
                }
                tag.name = span_make(source, at, at);
            }
            if (type_end && (!span_valid(tag.name) || !tag.name.size)) {
                // A typedef needs a name (tsgo's parseTypedefTag: no Closure identifier-named typedefs). tsgo says it
                // at the character before where the missing name is, else the last one the type expression took: its
                // `}`, else the type's end
                const char* last = type_end;
                if (last[-1] != '}' && tag.has_type && span_valid(tag.type_span)) last = span_end_ptr(tag.type_span);
                if (span_valid(tag.name)) last = tag.name.buf;
                // (A syntactic error of the file in tsgo, not one of its jsdocDiagnostics: unchecked files have it too)
                if (source->is_js) {
                    vec_error_push(&parent->shared->jsdoc_errors, (Error){
                        .message = cstr_lit("Identifier expected."), .span = span_make(source, last - 1, last), .ts_code = 1003,
                        .phase = ERROR_PHASE_PARSE,
                    });
                }
            }
            break;
        case JSDOC_TAG_TEMPLATE:
            // `@template {C} T, U`, `@template [T=D]` (tsc's parseTemplateTag): a tag per name, the constraint is the
            // first one's (getEffectiveConstraintOfTypeParameter). Defaults aren't read
            c = jsdoc_skip_space(c, end);
            const char* ws_start = c;
            if (c < end && *c == '{') {
                parse_jsdoc_type_expression(parent, &c, end, &tag);
                ws_start = c;
                c = jsdoc_skip_space(c, end);
            }
            while (true) {
                bool bracketed = c < end && *c == '[';
                if (bracketed) c = jsdoc_skip_space(c + 1, end);
                // Modifiers (tsc's parseTemplateTagTypeParameter: parseModifiers): `const`, `in`, `out`; any other is
                // TS1273 (checkGrammarModifiers)
                bool is_const = false, is_in = false, is_out = false;
                Span const_span = span_null, in_span = span_null, out_span = span_null;
                while (true) {
                    const char* word = c;
                    while (c < end && (is_identifier_char(*c) || (uint8_t)*c >= 0x80)) c++;
                    csview w = { word, c - word };
                    static char const* const others[] = {
                        "public", "private", "protected", "readonly", "static", "abstract", "declare", "export",
                        "default", "async", "accessor", "override",
                    };
                    bool other = false;
                    for (u32 k = 0; k < c_arraylen(others) && !other; k++) other = csview_equals(w, others[k]);
                    bool modifier = other || csview_equals(w, "const") || csview_equals(w, "in") || csview_equals(w, "out");
                    const char* next = c;
                    while (next < end && (*next == ' ' || *next == '\t')) next++;
                    if (!modifier || next == c || next >= end || !(is_identifier_char(*next) || (uint8_t)*next >= 0x80)) {
                        c = word;
                        break;
                    }
                    Span w_span = span_make(source, word, c);
                    if (other) {
                        Parser e = parser(parent);
                        grammar_error(&e, w_span, 1273, "'", w, "' modifier cannot appear on a type parameter");
                        jsdoc_keep_errors(&e);
                        parse_fail(&e);
                    }
                    if (csview_equals(w, "const")) is_const = true, const_span = w_span;
                    if (csview_equals(w, "in")) is_in = true, in_span = w_span;
                    if (csview_equals(w, "out")) is_out = true, out_span = w_span;
                    c = jsdoc_skip_space(c, end);
                }
                const char* name_start = c;
                while (c < end && (is_identifier_char(*c) || (uint8_t)*c >= 0x80)) c++;
                if (c == name_start) {
                    // tsc's parseJSDocIdentifierName with its message, at the JSDoc scanner's token there: after
                    // whitespace and line breaks (not a line's `*`), unless only whitespace is left
                    const char* t = bracketed ? c : ws_start;
                    while (t < end && is_jsdoc_space(*t)) t++;
                    const char* token = t < end ? t : bracketed ? c : ws_start;
                    const char* token_end = token;
                    if (token < end && is_jsdoc_space(*token)) {
                        if (*token == '\n') token_end++;
                        else if (token[0] == '\r' && token[1] == '\n') token_end += 2;
                        else while (token_end < end && (*token_end == ' ' || *token_end == '\t')) token_end++;
                    } else if (token < end) {
                        token_end++;
                    }
                    Parser e = parser(parent);
                    parse_error(&e, span_make(source, token, token_end), 1069,
                        "Unexpected token. A type parameter name was expected without curly braces.");
                    jsdoc_keep_errors(&e);
                    parse_fail(&e);
                    break;
                }
                if (span_valid(tag.name)) {
                    vec_jsdoc_tag_push(tags, tag);
                    tag = (JsDocTag){ .kind = JSDOC_TAG_TEMPLATE, .tag_name = tag.tag_name };
                }
                tag.name = span_make(source, name_start, c);
                tag.bracketed = bracketed;
                tag.is_const = is_const;
                tag.is_in = is_in;
                tag.is_out = is_out;
                tag.const_span = const_span;
                tag.in_span = in_span;
                tag.out_span = out_span;
                if (bracketed) {
                    while (c < end && (*c == ' ' || *c == '\t')) c++;
                    // (tsc: parseExpected(EqualsToken), then a type, which is TS1110 when there's none)
                    Parser e = parser(parent);
                    if (c < end && *c == '=') {
                        c++;
                        TypeNode type;
                        const char* before = c;
                        if (parse_jsdoc_bare_type(parent, &c, end, &type)) {
                            if (tag.has_default) type_drop(&tag.default_type);
                            tag.default_type = type;
                            tag.has_default = true;
                            tag.default_start = before;
                        } else {
                            while (before < end && (*before == ' ' || *before == '\t')) before++;
                            parse_error(&e, span_make(source, before, before + (before < end)), 1110, "Type expected.");
                        }
                    } else {
                        parse_error(&e, span_make(source, c, c + (c < end)), 1005, "'=' expected.");
                    }
                    jsdoc_keep_errors(&e);
                    parse_fail(&e);
                    while (c < end && *c != ']' && *c != '\n') c++;
                    if (c < end && *c == ']') c++;
                }
                const char* after = c;
                while (after < end && (*after == ' ' || *after == '\t')) after++;
                if (after >= end || *after != ',') break;
                c = after + 1;
                ws_start = c;
                while (c < end && (*c == ' ' || *c == '\t')) c++;
            }
            break;
        case JSDOC_TAG_TYPE:
        case JSDOC_TAG_RETURNS:
        case JSDOC_TAG_THIS:
        case JSDOC_TAG_SATISFIES:
        case JSDOC_TAG_THROWS:
        case JSDOC_TAG_EXTENDS:
        case JSDOC_TAG_IMPLEMENTS: {
            const char* after = jsdoc_skip_space(c, end);
            bool augments = tag.kind == JSDOC_TAG_EXTENDS || tag.kind == JSDOC_TAG_IMPLEMENTS;
            parent->shared->jsdoc_augments_type_args = augments;
            if (after < end && *after == '{') {
                parse_jsdoc_type_expression(parent, &after, end, &tag);
                c = after;
            } else if (tag.kind == JSDOC_TAG_SATISFIES) {
                // Braces are required (tsc's parseSatisfiesTag: parseJSDocTypeExpression(mayOmitBraces: false))
                Parser e = parser(parent);
                e.pos = after;
                e.whitespace_end = nullptr;
                const char* token_end = after;
                while (token_end < end && (is_identifier_char(*token_end) || (uint8_t)*token_end >= 0x80)) token_end++;
                if (token_end == after && after < end) token_end++;
                parse_error(&e, span_make(source, after, token_end), 1005, "'{' expected.");
                const char* type_start = after;
                TypeNode type;
                if (after < end && parse_jsdoc_bare_type(parent, &after, end, &type)) {
                    tag.has_type = true;
                    tag.type = type;
                    tag.type_span = span_make(source, type_start, after);
                    c = after;
                }
                // (Without its `{`, no `}` is expected: parseExpected's false result is hasBrace)
                jsdoc_keep_errors(&e);
                parse_fail(&e);
            } else if ((tag.kind == JSDOC_TAG_TYPE || tag.kind == JSDOC_TAG_EXTENDS ||
                    tag.kind == JSDOC_TAG_IMPLEMENTS || tag.kind == JSDOC_TAG_THIS) && after < end && after > c &&
                !memchr(c, '\n', (size_t)(after - c)) && *after != '@'
            ) {
                // Braces may be omitted (tsc's parseJSDocTypeExpression(mayOmitBraces)): `@type object`,
                // `@this Node`
                const char* type_start = after;
                TypeNode type;
                if (parse_jsdoc_bare_type(parent, &after, end, &type)) {
                    tag.has_type = true;
                    tag.type = type;
                    tag.type_span = span_make(source, type_start, after);
                    while (tag.type_span.size && is_jsdoc_space(tag.type_span.buf[tag.type_span.size - 1])) tag.type_span.size--;
                    c = after;
                }
            }
            parent->shared->jsdoc_augments_type_args = false;
            if ((tag.kind == JSDOC_TAG_EXTENDS || tag.kind == JSDOC_TAG_IMPLEMENTS) && !tag.has_type &&
                (after >= end || *after == '@' || memchr(c, '\n', (size_t)(after - c)))
            ) {
                // Nothing after the tag: tsc's parseExpressionWithTypeArgumentsForAugments has a missing name, TS1003
                // right after the tag name (reportAtCurrentPosition). `@augments`'s is compared with the class's
                // `extends` (TS8023)
                Parser e = parser(parent);
                e.pos = c;
                e.whitespace_end = nullptr;
                Span missing = span_make(source, c, c);
                // (tsgo's error at the current JSDoc token: one character, the whitespace after the tag name too)
                parse_error(&e, *c && *c != '\n' && *c != '\r' ? span_make(source, c, c + 1) : missing, 1003, "Identifier expected.");
                jsdoc_keep_errors(&e);
                parse_fail(&e);
                if (tag.kind == JSDOC_TAG_EXTENDS) {
                    tag.has_type = true;
                    tag.type = type_identifier(ast_new(IdentifierType, { .name = missing }));
                    tag.type_span = missing;
                }
            }
        }   break;
        default:
            break;
    }
    vec_jsdoc_tag_push(tags, tag);
    return c;
}

// A `{@link name}` (`@linkcode`, `@linkplain`) at `c` (a `{`) in a JSDoc's text: its name's parse errors (tsc's
// parseJSDocLink, parseJSDocLinkName: `Map.` misses a name, `Map#delete` names a member with a reserved word, TS1359,
// `Map#}` has an invalid character). Returns where the link's text ends (its `}`, the line's end), null if `c` starts
// no link. ASCII names only
static const char* jsdoc_parse_link(Parser* parent, const char* c, const char* end) {
    SourceFile const* source = parent->shared->source;
    if (c + 1 >= end || c[1] != '@') return nullptr;
    // (The JSDoc scanner's identifiers take `-` too: `{@link-x}` is no link)
    const char* word = c + 2;
    const char* word_end = word;
    while (word_end < end && (is_identifier_char(*word_end) || *word_end == '-')) word_end++;
    csview kind = csview_from_range(word, word_end);
    if (!csview_equals(kind, "link") && !csview_equals(kind, "linkcode") && !csview_equals(kind, "linkplain")) return nullptr;
    bool report = source->is_js;
    const char* p = word_end;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p < end && is_identifier_start_char(*p)) {
        // The first name is the JSDoc scanner's token, the rest the ordinary scanner's (whitespace skipped)
        while (p < end && (is_identifier_char(*p) || *p == '-')) p++;
        for (;;) {
            const char* t = p;
            while (t < end && (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')) t++;
            if (t >= end || *t != '.') break;
            t++;
            while (t < end && (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')) t++;
            p = t;
            if (t < end && *t == '#') break; // (A private name after the dot: a missing name, no error)
            if (t < end && is_identifier_start_char(*t)) {
                while (p < end && is_identifier_char(*p)) p++;
            } else {
                if (report) {
                    const char* token_end = t < end ? t + 1 : t;
                    jsdoc_error(parent, span_make(source, t, token_end), 1003, "Identifier expected.");
                }
                break;
            }
        }
        for (;;) {
            const char* t = p;
            while (t < end && (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')) t++;
            if (t >= end || *t != '#') break;
            // (`#` not before a name is an invalid character, the ordinary scanner's private identifier all the same)
            if (report && !(t + 1 < end && is_identifier_start_char(t[1]))) {
                jsdoc_error(parent, span_make(source, t, t + 1), 1127, "Invalid character.");
            }
            // After the `#`, the JSDoc scanner's token, which parseIdentifier wants a (non-reserved) identifier of
            const char* name = t + 1;
            const char* name_end = name;
            if (name < end && is_identifier_start_char(*name)) {
                while (name_end < end && (is_identifier_char(*name_end) || *name_end == '-')) name_end++;
            }
            csview text = csview_from_range(name, name_end);
            if (name_end > name && !is_reserved_word(text)) {
                p = name_end;
                continue;
            }
            if (report) {
                if (name_end > name) {
                    jsdoc_error(parent, span_make(source, name, name_end), 1359, "Identifier expected. '", text,
                        "' is a reserved word that cannot be used here.");
                } else {
                    jsdoc_error(parent, span_make(source, name, name < end ? name + 1 : name), 1003, "Identifier expected.");
                }
            }
            p = name;
            break;
        }
    }
    while (p < end && *p != '}' && *p != '\n') p++;
    return p;
}

// The tags of the comment `range` (tsc's parseJSDocCommentWorker: a tag starts at an `@` at the start of a line, after
// whitespace and at most one `*`, or after whitespace and before something else anywhere; not in backticks of a tag's
// comment)
static bool jsdoc_is_object_type(JsDocTag const* tag, bool* array);
static bool jsdoc_name_is_under(csview name, csview prefix);

static void parse_jsdoc_comment(Parser* parent, JsDocRange range, VecJsDocTag* tags) {
    const char* c = range.start + 3;
    const char* end = range.end - 2;
    bool line_start = true;
    bool saw_asterisk = false;
    // Backticks keep an `@` from starting a tag, up to the end of the line (tsgo's jsdocStateSavingBackticks, in the
    // description and in tags' comments); a fenced code block (a run of three or more backticks opens or closes one)
    // until it closes (tsgo's inFencedCodeBlock, each tag's comment starting outside one)
    bool in_backticks = false;
    bool fenced = false;
    int backtick_run = 0;
    JsDocTagKind owner = JSDOC_TAG_OTHER; // The @typedef or @callback the tags are part of, if any
    bool typedef_children = false; // The @typedef has no type or `{Object}`: it takes an `@type` child
    bool typedef_typed = false; // It took one
    while (c < end) {
        char ch = *c;
        if (ch != '`' && backtick_run) {
            if (backtick_run >= 3) fenced = !fenced;
            backtick_run = 0;
        }
        if (ch == '\n') {
            line_start = true;
            saw_asterisk = false;
            in_backticks = false;
            c++;
        } else if (ch == ' ' || ch == '\t' || ch == '\r') {
            c++;
        } else if (ch == '*' && line_start && !saw_asterisk) {
            saw_asterisk = true;
            c++;
        } else if (ch == '@' && !in_backticks && !fenced && (line_start || ((c[-1] == ' ' || c[-1] == '\t') && !is_jsdoc_space(c[1])))) {
            isize before = tags->size;
            c = parse_jsdoc_tag(parent, c, end, tags);
            line_start = false;
            fenced = false; // (Its comment starts outside a fenced block)
            backtick_run = 0;
            for (isize i = before; i < tags->size; i++) tags->data[i].comment = range.start;
            // tsc's parseCallbackTag/parseTypedefTag/parseOverloadTag take the tags that follow as their children
            JsDocTag* tag = vec_jsdoc_tag_back_mut(tags);
            if (tag->kind == JSDOC_TAG_TYPEDEF || tag->kind == JSDOC_TAG_CALLBACK || tag->kind == JSDOC_TAG_OVERLOAD) {
                owner = tag->kind;
                bool array;
                typedef_children = tag->kind == JSDOC_TAG_TYPEDEF && jsdoc_is_object_type(tag, &array);
                typedef_typed = false;
            } else if (owner == JSDOC_TAG_TYPEDEF && tag->kind == JSDOC_TAG_TYPE && typedef_children && typedef_typed) {
                // A second `@type` child: tsc's parseTypedefTag reports it at the token after it (with the file's
                // start as the first one's place, a detached diagnostic) and the typedef ends; the tag is lost
                const char* after = span_valid(tag->type_span) ? span_end_ptr(tag->type_span) : c;
                while (after < end && (*after == '}' || *after == ' ' || *after == '\t')) after++;
                SourceFile const* source = parent->shared->source;
                Error error = {
                    .message = cstr_lit("A JSDoc '@typedef' comment may not contain multiple '@type' tags."),
                    // (the newline token after the tag)
                    .span = span_make(source, after, after + (*after == '\n' ? 1 : after[0] == '\r' && after[1] == '\n' ? 2 : 0)),
                    .ts_code = 8033,
                    .phase = ERROR_PHASE_RESOLVE_NAMES,
                    .jsdoc = true,
                };
                const char* file_start = source_file_start(source);
                error_add_related_d(&error, span_make(source, file_start, file_start), 8034, "The tag was first specified here.");
                vec_error_push(&parent->shared->jsdoc_errors, error);
                jsdoc_tag_drop(vec_jsdoc_tag_back_mut(tags));
                vec_jsdoc_tag_pop(tags);
                owner = JSDOC_TAG_OTHER;
            } else if (owner == JSDOC_TAG_TYPEDEF && tag->kind == JSDOC_TAG_TYPE && typedef_children) {
                // (tsc's parseTypedefTag: an `@type` child of an untyped or `{Object}` typedef is its type)
                tag->nested = true;
                typedef_typed = true;
            } else if (tag->kind == JSDOC_TAG_TEMPLATE && (owner == JSDOC_TAG_CALLBACK || owner == JSDOC_TAG_OVERLOAD ||
                (owner == JSDOC_TAG_TYPEDEF && typedef_children))
            ) {
                // A `@template` among the children is TS8039 and lost (tsgo's parseCallbackTagParameters,
                // parseTypedefTag): only the comment's own `@template`s are the type parameters
                vec_error_push(&parent->shared->jsdoc_errors, (Error){
                    .message = cstr_lit("A JSDoc '@template' tag may not follow a '@typedef', '@callback', or '@overload' tag"),
                    .span = tag->tag_name, .ts_code = 8039, .phase = ERROR_PHASE_RESOLVE_NAMES, .jsdoc = true,
                });
                jsdoc_tag_drop(tag);
                vec_jsdoc_tag_pop(tags);
            } else if (owner == JSDOC_TAG_CALLBACK && (tag->kind == JSDOC_TAG_PARAM || tag->kind == JSDOC_TAG_RETURNS ||
                tag->kind == JSDOC_TAG_THIS)
            ) {
                // (`@this` too: tsc's parseChildParameterOrPropertyTag takes it among a callback's parameters)
                tag->nested = true;
            } else if (owner == JSDOC_TAG_OVERLOAD && (tag->kind == JSDOC_TAG_PARAM || tag->kind == JSDOC_TAG_RETURNS)) {
                // (tsc's parseJSDocSignature: parameters, then one `@returns`, which ends it)
                tag->nested = true;
                if (tag->kind == JSDOC_TAG_RETURNS) owner = JSDOC_TAG_OTHER;
            } else if (owner == JSDOC_TAG_TYPEDEF && tag->kind == JSDOC_TAG_PROPERTY) {
                tag->nested = true;
                // (A property signature of the typedef's type literal to tsgo's reparser, never readonly)
                if (tag->has_type && tag->type.tag == TYPE_UNIQUE_SYMBOL && span_valid(tag->name)) {
                    grammar_error(parent, tag->name, 1330, "A property of an interface or type literal whose type is a "
                        "'unique symbol' type must be 'readonly'.");
                }
            } else {
                owner = JSDOC_TAG_OTHER;
            }
        } else {
            if (ch == '`') {
                in_backticks = !in_backticks;
                backtick_run++;
            }
            line_start = false;
            // (Not in backticks: tsc's comment text token runs past a `{` there, ScanJSDocCommentTextToken)
            const char* link_end = ch == '{' && !fenced && !in_backticks ? jsdoc_parse_link(parent, c, end) : nullptr;
            c = link_end ? link_end : c + 1;
        }
    }
}

// The JSDoc comment that ends right before `pos` (only whitespace between), parsed into `tags`: false if there is
// none. tsc attaches every JSDoc comment of the leading trivia, but takes the tags of the last one only
// (filterOwnedJSDocTags; the others' `@overload` tags aside)
// Where the line comment of the line from `line` to `end` starts (skipping block comments and string literals), null if
// it has none
static const char* jsdoc_line_comment_start(const char* line, const char* end) {
    // Most lines have no `//` at all: find that out with memchr before walking strings and block comments
    bool any = false;
    for (const char* p = line; p + 1 < end && (p = memchr(p, '/', end - p - 1)); p++) {
        if (p[1] == '/') {
            any = true;
            break;
        }
    }
    if (!any) return nullptr;
    for (const char* c = line; c + 1 < end; c++) {
        if (c[0] == '/' && c[1] == '/') return c;
        if (c[0] == '/' && c[1] == '*') {
            const char* close = c + 2;
            while (close + 1 < end && !(close[0] == '*' && close[1] == '/')) close++;
            if (close + 1 >= end) return nullptr;
            c = close + 1;
        } else if (*c == '"' || *c == '\'' || *c == '`') {
            char quote = *c;
            c++;
            while (c < end && *c != quote) c += *c == '\\' ? 2 : 1;
            if (c >= end) return nullptr;
        }
    }
    return nullptr;
}

// Where the line comment that ends at `c` starts, null if none does. `ws_end` is where the whitespace after `c` ends: a
// line comment runs to the end of its line, so one ends at `c` only if a line break follows it (a host in the middle of
// a minified line skips the walk back to the line's start)
static const char* jsdoc_line_comment_ending_at(const char* file_start, const char* c, const char* ws_end) {
    if (!memchr(c, '\n', ws_end - c) && !memchr(c, '\r', ws_end - c)) return nullptr;
    const char* line = c;
    while (line > file_start && line[-1] != '\n' && line[-1] != '\r') line--;
    return jsdoc_line_comment_start(line, c);
}

// Whether a comment starting at `start` is among the leading comments of what follows it (tsc's
// getLeadingCommentRanges): only those after the first line break since the previous token are (or everything at the
// start of the file); `{ /** @type {T} */ this.x = [] }` on one line is a trailing comment of the `{`
static bool jsdoc_comment_is_leading(const char* file_start, const char* start) {
    const char* c = start;
    while (true) {
        while (c > file_start && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\v' || c[-1] == '\f')) c--;
        if (c == file_start || c[-1] == '\n' || c[-1] == '\r') return true;
        if (c - file_start >= 4 && c[-1] == '/' && c[-2] == '*') {
            // (Another block comment before it on the line: its line breaks don't count)
            const char* open = c - 2;
            while (open > file_start + 1 && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open <= file_start + 1) return true;
            c = open - 2;
            continue;
        }
        return false;
    }
}

static bool jsdoc_before_ex(Parser* parent, const char* pos, VecJsDocTag* tags, bool trailing_ok);

// The JSDoc of a host that takes leading comments only (statements, class and object literal members: tsc's
// getJSDocCommentRanges, unlike parameters, variable declarations, function expressions and parenthesized expressions,
// whose trailing comments count too)
static bool jsdoc_before_leading(Parser* parent, const char* pos, VecJsDocTag* tags) {
    return jsdoc_before_ex(parent, pos, tags, false);
}

static bool jsdoc_before(Parser* parent, const char* pos, VecJsDocTag* tags) {
    return jsdoc_before_ex(parent, pos, tags, true);
}

// The index of the host's last JSDoc comment in jsdoc_comments, -1 if it has none (nothing is parsed)
static isize jsdoc_locate(Parser* parent, const char* pos, bool trailing_ok) {
    VecJsDocRange const* comments = &parent->shared->jsdoc_comments;
    if (!comments->size) return -1;
    const char* file_start = source_file_start(parent->shared->source);
    const char* c = pos;
    // The last JSDoc comment among the host's leading comments (tsc's getJSDocCommentRanges): other comments between
    // it and the host (`// eslint-disable-next-line`) are skipped
    isize lo;
    while (true) {
        const char* ws_end = c;
        while (c > file_start && is_js_ascii_space(c[-1])) c--;
        if (c - file_start >= 4 && c[-1] == '/' && c[-2] == '*') {
            // Binary search for the comment ending at c
            lo = 0;
            isize hi = comments->size;
            while (lo < hi) {
                isize mid = (lo + hi) / 2;
                if (comments->data[mid].end < c) lo = mid + 1;
                else hi = mid;
            }
            if (lo < comments->size && comments->data[lo].end == c) break;
            // A plain block comment: before its `/*`
            const char* open = c - 2;
            while (open > file_start + 1 && !(open[-2] == '/' && open[-1] == '*')) open--;
            if (open <= file_start + 1) return -1;
            c = open - 2;
            continue;
        }
        // A line comment ending the line: before its `//` (what comes before it on the line may be trivia too:
        // `/** @type {T} */ // why`)
        const char* comment = jsdoc_line_comment_ending_at(file_start, c, ws_end);
        if (comment) {
            c = comment;
            continue;
        }
        return -1;
    }
    if (!trailing_ok && !jsdoc_comment_is_leading(file_start, comments->data[lo].start)) return -1;
    return lo;
}

static bool jsdoc_before_ex(Parser* parent, const char* pos, VecJsDocTag* tags, bool trailing_ok) {
    isize lo = jsdoc_locate(parent, pos, trailing_ok);
    if (lo < 0) return false;
    VecJsDocRange const* comments = &parent->shared->jsdoc_comments;
    const char* file_start = source_file_start(parent->shared->source);
    // The JSDoc comments before it among the leading comments (other comments may come between) are the host's too:
    // their `@overload` tags count (tsc's getJSDocOverloadTags), and their errors (tsc parses and checks every one)
    isize first = lo;
    while (first > 0) {
        const char* start = comments->data[first].start;
        bool adjacent = false;
        while (true) {
            const char* ws_end = start;
            while (start > file_start && is_js_ascii_space(start[-1])) start--;
            if (start == comments->data[first - 1].end) {
                adjacent = true;
                break;
            }
            if (start - file_start >= 4 && start[-1] == '/' && start[-2] == '*') {
                const char* open = start - 2;
                while (open > file_start + 1 && !(open[-2] == '/' && open[-1] == '*')) open--;
                if (open <= file_start + 1) break;
                start = open - 2;
                continue;
            }
            const char* comment = jsdoc_line_comment_ending_at(file_start, start, ws_end);
            if (comment) {
                start = comment;
                continue;
            }
            break;
        }
        if (!adjacent) break;
        first--;
    }
    for (isize i = first; i < lo; i++) {
        JsDocRange range = comments->data[i];
        VecJsDocTag earlier = {0};
        // (Only the host's last comment is reparsed into the tree: the checker's grammar errors about an earlier
        // one's types, TS1099 or TS1009, aren't reported. Its parse errors are)
        bool outer_unchecked = parent->shared->jsdoc_unchecked;
        parent->shared->jsdoc_unchecked = true;
        parse_jsdoc_comment(parent, range, &earlier);
        parent->shared->jsdoc_unchecked = outer_unchecked;
        csview text = csview_from_range(range.start, range.end);
        bool overloads = csview_contains(text, "@overload");
        if (!overloads && !csview_contains(text, "@extends") && !csview_contains(text, "@augments")) {
            vec_jsdoc_tag_drop(&earlier);
            continue;
        }
        // (And its `@extends`/`@augments`, which only TS8023 checks: tsgo's checkJSDocAugmentsTagMatchesExtends reads
        // every comment)
        bool in_overload = false;
        for_each (t, vec_jsdoc_tag, &earlier) {
            if (t->kind == JSDOC_TAG_OVERLOAD) in_overload = true;
            else if (!t->nested) in_overload = false;
            if ((overloads && (in_overload || t->kind == JSDOC_TAG_TEMPLATE)) || (t->kind == JSDOC_TAG_EXTENDS && !t->nested)) {
                t->earlier = true;
                vec_jsdoc_tag_push(tags, *t);
                *t = (JsDocTag){0};
            }
        }
        vec_jsdoc_tag_drop(&earlier);
    }
    parse_jsdoc_comment(parent, comments->data[lo], tags);
    return true;
}

// The first tag of a kind that has a type, null if there is none
static JsDocTag* jsdoc_find_typed(VecJsDocTag* tags, JsDocTagKind kind) {
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind == kind && i->has_type && !i->nested) return i;
    }
    return nullptr;
}

// Moves a tag's type into an annotation slot
static TypeNode jsdoc_take_type(JsDocTag* tag, Span* span_ret) {
    tag->has_type = false;
    if (span_ret) *span_ret = tag->type_span;
    tag->type.from_jsdoc = true;
    return tag->type;
}

// `v` as `v satisfies T` from a JSDoc `@satisfies` (tsgo's reparser, makeNewCast), TS1360 at `at`
static void jsdoc_wrap_satisfies(Expr* value, Span at, TypeNode type) {
    Expr inner = *value;
    *value = expr_as_type(expr_outer_span(&inner), ast_new(AsTypeExpr, {
        .expr = inner,
        .keyword_span = at,
        .type = type,
        .is_satisfies = true,
        .is_jsdoc = true,
    }));
}

// The token after `span` (tsgo's GetRangeOfTokenAtPosition at SkipTrivia of its end), empty at the end of the file
static Span jsdoc_token_after(Span span) {
    char const* at = span_end_ptr(span);
    SourceFile const* source = span_source(span);
    char const* file_end = cstr_str(&source->contents) + cstr_size(&source->contents);
    while (at < file_end) {
        if (is_js_ascii_space(*at)) {
            at++;
        } else if (at + 1 < file_end && at[0] == '/' && at[1] == '/') {
            while (at < file_end && *at != '\n') at++;
        } else if (at + 1 < file_end && at[0] == '/' && at[1] == '*') {
            at += 2;
            while (at + 1 < file_end && !(at[0] == '*' && at[1] == '/')) at++;
            at = at + 2 < file_end ? at + 2 : file_end;
        } else {
            break;
        }
    }
    Span token = block_first_token(span_make(source, at, file_end));
    return span_valid(token) ? token : span_make(source, at, at);
}

// `@type` on a declaration with no annotation of its own. Whether it took one
static bool jsdoc_apply_type_to_binding(VecJsDocTag* tags, Binding* binding) {
    if (binding->has_explicit_type) return false;
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (!tag) return false;
    Span type_span;
    binding_set_type(binding, jsdoc_take_type(tag, &type_span));
    binding_set_explicit_type_span(binding, type_span);
    binding->has_explicit_type = true;
    return true;
}

// The type of a tag whose type a declaration took already, parsed again from its text (a pattern parameter matched by
// position to the tag of a parameter matched by name: tsc's two parameters share the type node). Its errors were
// reported the first time
static bool jsdoc_reparse_type(Parser* parent, JsDocTag const* tag, TypeNode* ret) {
    if (!span_valid(tag->type_span)) return false;
    Parser p = parser(parent);
    p.pos = span_start_ptr(tag->type_span);
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = span_end_ptr(tag->type_span);
    isize jsdoc_errors = vec_error_size(&p.shared->jsdoc_errors);
    bool parsed = parse_type(&p, TYPE_PRECEDENCE_NULL, ret);
    if (parsed) hop_jsdoc_optional(&p, ret);
    while (vec_error_size(&p.shared->jsdoc_errors) > jsdoc_errors) {
        error_drop(vec_error_back_mut(&p.shared->jsdoc_errors));
        vec_error_pop(&p.shared->jsdoc_errors);
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    parse_fail(&p);
    return parsed;
}

// The `@param` tag at a position among the tags, the `o.a` children of an `{Object} o` aside (tsc's parser makes them
// its type)
static JsDocTag* jsdoc_param_tag_at(VecJsDocTag* tags, isize index) {
    JsDocTag const* parent = nullptr;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_PARAM || i->nested) continue;
        if (parent && span_valid(i->name) && jsdoc_name_is_under(span_sv(i->name), span_sv(parent->name))) continue;
        bool array;
        parent = span_valid(i->name) && (jsdoc_is_object_type(i, &array) || i->type.tag == TYPE_INTERFACE) ? i : nullptr;
        if (!index--) return i;
    }
    return nullptr;
}

// tsgo's findMatchingParameter for a tag (at `tag_index` among the tags, jsdoc_param_tag_at()): the first parameter
// it names, or that is at its position and is a pattern or it is nameless (a parse error). -1 for none
static isize jsdoc_param_tag_owner(JsDocTag const* tag, isize tag_index, FunctionImpl const* function) {
    for_each (i, vec_function_arg, &function->args) {
        Binding const* binding = &i->initializer.binding;
        if (binding->tag == BINDING_IDENTIFIER) {
            if (span_valid(tag->name) ? jsdoc_name_is(span_sv(tag->name), span_sv(binding->content_span)) : i_index == tag_index) {
                return i_index;
            }
        } else if (i_index == tag_index) {
            return i_index;
        }
    }
    return -1;
}

// The `@param` tag for a function's parameter, with or without a type (tsgo's findMatchingParameter, which goes by
// tag): the first that names it, or a nameless one (a parse error) at its position; a pattern's by position. A tag
// an earlier parameter matches is that one's
static JsDocTag* jsdoc_param_tag_any(VecJsDocTag* tags, FunctionImpl const* function, FunctionArg const* arg) {
    Binding const* binding = &arg->initializer.binding;
    isize index = arg - function->args.data;
    isize tag_index = 0;
    JsDocTag const* parent = nullptr;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_PARAM || i->nested) continue;
        // (The `o.a` children of an `{Object} o` aside: tsc's parser makes them its type)
        if (parent && span_valid(i->name) && jsdoc_name_is_under(span_sv(i->name), span_sv(parent->name))) continue;
        bool array;
        parent = span_valid(i->name) && (jsdoc_is_object_type(i, &array) || i->type.tag == TYPE_INTERFACE) ? i : nullptr;
        isize this_index = tag_index++;
        bool candidate = binding->tag == BINDING_IDENTIFIER ?
            (span_valid(i->name) ? jsdoc_name_is(span_sv(i->name), span_sv(binding->content_span)) : this_index == index) :
            this_index == index;
        if (candidate && jsdoc_param_tag_owner(i, this_index, function) == index) return i;
    }
    return nullptr;
}

static JsDocTag* jsdoc_param_tag(VecJsDocTag* tags, FunctionImpl const* function, FunctionArg const* arg) {
    JsDocTag* tag = jsdoc_param_tag_any(tags, function, arg);
    if (!tag) return nullptr;
    // (By position, one a parameter took by name already is this one's too: see jsdoc_reparse_type())
    bool by_name = arg->initializer.binding.tag == BINDING_IDENTIFIER;
    return tag->has_type || (!by_name && span_valid(tag->type_span)) ? tag : nullptr;
}

// The function an expression is, directly (tsc: a parenthesized one has no JSDoc host above it)
static FunctionImpl* jsdoc_function_of(Expr* expr) {
    // (Through the `satisfies` a JSDoc `@satisfies` made: tsgo's getFunctionLikeHost skips them)
    while (expr->tag == EXPR_AS_TYPE && expr->as_type->is_jsdoc && expr->as_type->is_satisfies &&
        expr_outer_span(expr).size == expr->inner_span.size
    ) {
        expr = &expr->as_type->expr;
    }
    if (expr->tag != EXPR_FUNCTION || expr_outer_span(expr).size != expr->inner_span.size) return nullptr;
    return expr->function;
}

// `@type {(a: A) => R}` (or `{{(a: A): R}}`) on a function declares its signature (tsc's getSignatureOfTypeTag): the
// written signature's parameter and return types move into the function's slots
// The one signature a written function type or `{(...): R}` literal is, null for anything else
static FunctionImpl* jsdoc_single_signature(TypeNode* type) {
    if (type->tag == TYPE_CALLABLE) return type->callable->function;
    if (type->tag == TYPE_INTERFACE && vec_interface_member_size(&type->interface.members) == 1) {
        InterfaceMember* member = vec_interface_member_at_mut(&type->interface.members, 0);
        if (member->key.tag == INTERFACE_KEY_CALLABLE && member->type.tag == TYPE_CALLABLE) return member->type.callable->function;
    }
    return nullptr;
}


static void jsdoc_apply_type_signature(VecJsDocTag* tags, FunctionImpl* function, bool declaration) {
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (!tag) return;
    function_impl_rare_mut(function)->jsdoc_signature_span = tag->type_span;
    FunctionImpl* signature = jsdoc_single_signature(&tag->type);
    if (!signature && tag->type.tag == TYPE_INTERFACE) {
        // (A type literal without a call signature)
        bool callable = false;
        for_each (i, vec_interface_member, &tag->type.interface.members) callable |= i->key.tag == INTERFACE_KEY_CALLABLE;
        function->jsdoc_signature_mismatch = !callable;
    }
    if (signature && !signature->is_constructor) {
        u32 count = 0;
        bool rest = false;
        for_each (i, vec_function_arg, &signature->args) {
            Binding const* binding = &i->initializer.binding;
            if (!i_index && binding->tag == BINDING_IDENTIFIER && span_equals(binding->content_span, "this")) continue;
            rest |= i->rest;
            count++;
        }
        function->jsdoc_signature_mismatch = !rest && count < function_impl_jsdoc_required_params(function);
    }
    if (!signature || signature->is_constructor || declaration) return;
    // `(this: T) => R`: the function's `this` parameter, unless it has one (or is an arrow function), and not a
    // position to match the parameters by
    FunctionArg* signature_this = vec_function_arg_size(&signature->args) ? signature->args.data : nullptr;
    if (signature_this && !(signature_this->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(signature_this->initializer.binding.content_span, "this"))
    ) {
        signature_this = nullptr;
    }
    FunctionArg const* own_first = vec_function_arg_size(&function->args) ? function->args.data : nullptr;
    bool has_this = own_first && own_first->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(own_first->initializer.binding.content_span, "this");
    if (signature_this && !has_this && !function_impl_is_arrow(function) && signature_this->initializer.binding.has_explicit_type) {
        FunctionArg arg = {
            .jsdoc_this = true,
        };
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = signature_this->initializer.binding.content_span,
            .has_explicit_type = true,
        };
        binding_set_type(&arg.initializer.binding, binding_take_type(&signature_this->initializer.binding));
        binding_set_explicit_type_span(&arg.initializer.binding, binding_explicit_type_span(&signature_this->initializer.binding));
        arg.initializer.binding.explicit_type->from_jsdoc = true;
        signature_this->initializer.binding.has_explicit_type = false;
        vec_function_arg_insert_n(&function->args, 0, &arg, 1);
        has_this = true;
    }
    if (generic_empty(&function->generic) && !generic_empty(&signature->generic)) {
        function->generic = signature->generic;
        signature->generic = (Generic){0};
        for_each (i, vec_generic_item, &function->generic.items) i->jsdoc = true;
    }
    isize skip_own = has_this ? 1 : 0, skip_signature = signature_this ? 1 : 0;
    for_each (i, vec_function_arg, &function->args) {
        if (i_index < skip_own) continue;
        isize at = i_index - skip_own + skip_signature;
        if (at >= vec_function_arg_size(&signature->args)) break;
        FunctionArg* from = vec_function_arg_at_mut(&signature->args, at);
        Binding* binding = &i->initializer.binding;
        if (binding->has_explicit_type || !from->initializer.binding.has_explicit_type || from->rest != i->rest) continue;
        binding_set_type(binding, binding_take_type(&from->initializer.binding));
        binding->explicit_type->from_jsdoc = true;
        i->jsdoc_span = ast_new(Span, from->span);
        binding_set_explicit_type_span(binding, tag->type_span);
        binding->has_explicit_type = true;
        from->initializer.binding.has_explicit_type = false;
        if (from->explicit_optional && initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY) i->explicit_optional = true;
    }
    if (!function->ret_explicit && signature->ret_explicit && !function->is_constructor) {
        function->ret = signature->ret;
        function->ret.from_jsdoc = true;
        function_impl_set_ret_span(function, function_impl_ret_span(signature)); // (tsc's getJSDocReturnType: the signature's return type node)
        function->ret_explicit = true;
        signature->ret_explicit = false;
        signature->ret = (TypeNode){0};
        // (`(e: unknown) => e is T`: its predicate too, tsgo's FullSignature)
        if (signature->predicate && !function->predicate) {
            function->predicate = signature->predicate;
            function->predicate->jsdoc = true;
            signature->predicate = nullptr;
        }
    }
}

// `@` to the end of the tag's name: where tsc's declarations made by tags are
static Span jsdoc_tag_decl_span(JsDocTag const* tag) {
    const char* end = span_valid(tag->name) ? tag->name.buf + tag->name.size : tag->tag_name.buf + tag->tag_name.size;
    return span_make_in(tag->tag_name, tag->tag_name.buf - 1, end);
}

// A tag's node (tsgo's JSDoc tag Loc): from its `@` to the next tag's `@` (the line breaks and `*`s before it included)
// or the comment's `*/`
static Span jsdoc_tag_node_span(JsDocTag const* tag) {
    const char* at = tag->tag_name.buf - 1;
    const char* c = tag->tag_name.buf + tag->tag_name.size;
    if (span_valid(tag->name) && tag->name.buf + tag->name.size > c) c = tag->name.buf + tag->name.size;
    // (`[name=default]`: past its `]`)
    char const* bracket_end = nullptr;
    if (tag->bracketed && span_valid(tag->name)) {
        char const* close = c;
        while (*close && *close != ']' && *close != '\n' && !(close[0] == '*' && close[1] == '/')) close++;
        if (*close == ']') bracket_end = c = close + 1;
    }
    bool line_start = false;
    for (; *c && !(c[0] == '*' && c[1] == '/'); c++) {
        if (*c == '\n') line_start = true;
        else if (line_start && *c == '@') break;
        else if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '*') line_start = false;
    }
    if (bracket_end && *c == '*') {
        // (The comment's last tag, its name bracketed and no description after it: it ends at the `]`, the token the
        // regular scanner read last, the end of the comment's text its next one's full start)
        char const* last = c;
        while (last > bracket_end && (last[-1] == ' ' || last[-1] == '\t' || last[-1] == '\r' || last[-1] == '\n' ||
            last[-1] == '*')) {
            last--;
        }
        if (last == bracket_end) c = bracket_end;
    }
    return span_make_in(tag->tag_name, at, c);
}

// Where a `@typedef`/`@callback` tag ends in tsgo (parseTypedefTag, parseCallbackTag): with its properties' type literal
// or its signature, else past its description, else at its name
static char const* jsdoc_alias_end(JsDocTag const* tag, TypeNode const* type) {
    if (type->tag == TYPE_ARRAY && type->array.element) type = type->array.element;
    if (type->tag == TYPE_CALLABLE && type->callable->function->jsdoc_callback) return span_end_ptr(type->callable->function->full_span);
    if (type->tag == TYPE_INTERFACE && type->interface.jsdoc_literal && vec_interface_member_size(&type->interface.members)) {
        return span_end_ptr(vec_interface_member_back(&type->interface.members)->declaration);
    }
    Span name = jsdoc_tag_decl_span(tag);
    Span node = jsdoc_tag_node_span(tag);
    for (char const* c = span_end_ptr(name); c < span_end_ptr(node); c++) {
        if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n' && *c != '*') return span_end_ptr(node);
    }
    return span_end_ptr(name);
}

// `{Object}`, `{object}`, `{Object[]}`: a type a tag's nested `@property`/`@param` tags replace with a literal (tsc's
// isObjectOrObjectArrayTypeReference). array: whether it's the array form
static bool jsdoc_is_object_type(JsDocTag const* tag, bool* array) {
    *array = false;
    if (!tag->has_type) return true;
    TypeNode const* type = &tag->type;
    if (type->tag == TYPE_ARRAY) {
        *array = true;
        type = type->array.element;
    }
    return type->tag == TYPE_IDENTIFIER && !identifier_type_namespace_count(type->identifier) &&
        !vec_spanned_type_size(&type->identifier->generic_params) &&
        (span_equals(type->identifier->name, "Object") || span_equals(type->identifier->name, "object"));
}

// A JSDoc name's `[]` (`@param {string} opts[].x`, an element of an `{Object[]}` parameter) isn't part of the entity
// name (tsc's parseJSDocEntityName skips `[` `]`): `name` is `prefix.<rest>` with them left out. The rest without its
// leading `[]`, if `rest` isn't null
static bool jsdoc_name_rest(csview name, csview prefix, csview* rest) {
    isize i = 0, j = 0;
    for (;;) {
        while (i + 1 < name.size && name.buf[i] == '[' && name.buf[i + 1] == ']') i += 2;
        while (j + 1 < prefix.size && prefix.buf[j] == '[' && prefix.buf[j + 1] == ']') j += 2;
        if (j >= prefix.size) break;
        if (i >= name.size || name.buf[i] != prefix.buf[j]) return false;
        i++, j++;
    }
    if (i >= name.size || name.buf[i] != '.' || i + 1 >= name.size) return false;
    if (rest) *rest = csview_from_range(name.buf + i + 1, name.buf + name.size);
    return true;
}

static bool jsdoc_name_is_under(csview name, csview prefix) {
    return jsdoc_name_rest(name, prefix, nullptr);
}

// The same name with `[]`s left out
static bool jsdoc_names_equal(csview a, csview b) {
    isize i = 0, j = 0;
    for (;;) {
        while (i + 1 < a.size && a.buf[i] == '[' && a.buf[i + 1] == ']') i += 2;
        while (j + 1 < b.size && b.buf[j] == '[' && b.buf[j + 1] == ']') j += 2;
        if (i >= a.size || j >= b.size) return i >= a.size && j >= b.size;
        if (a.buf[i] != b.buf[j]) return false;
        i++, j++;
    }
}

// A tag's node span, the type literal its nested tags made included (tsgo's tag ends with its children)
static Span jsdoc_tag_node_span_with(JsDocTag const* tag, TypeNode const* type) {
    Span span = jsdoc_tag_node_span(tag);
    if (type->tag == TYPE_ARRAY && type->array.element) type = type->array.element;
    if (type->tag != TYPE_INTERFACE || !type->interface.jsdoc_literal || !vec_interface_member_size(&type->interface.members)) {
        return span;
    }
    Span last = vec_interface_member_back(&type->interface.members)->declaration;
    if (span_valid(last) && span_end_ptr(last) > span_end_ptr(span)) span = span_make_in(span, span.buf, span_end_ptr(last));
    return span;
}

// The type literal the `@property`/`@param` tags named `prefix.x` among `props` make (`x` when prefix is empty; tsc's
// JSDocTypeLiteral, nested through `o.a` names under an `{Object}` one)
static TypeNode jsdoc_type_literal(JsDocTag* props, isize count, csview prefix, Span span) {
    VecInterfaceMember members = {0};
    for (isize i = 0; i < count; i++) {
        JsDocTag* tag = &props[i];
        if (!span_valid(tag->name) && !prefix.size && span_valid(tag->missing_name) && tag->has_type) {
            // A typed tag whose name is missing, `@property {function(A): B} f` (tsgo's parser stops the type at the
            // `(`, TS1005): a property of the missing identifier, which tsgo names '(Missing)' and writes nameless
            TypeNode type = jsdoc_take_type(tag, nullptr);
            type.from_jsdoc = false;
            bool optional = type.jsdoc_optional;
            Span name_span = tag->missing_name;
            list_push(vec_interface_member, &members, interface_member_make(
                jsdoc_tag_node_span_with(tag, &type), nullptr, false, optional, cstr_from("(Missing)"), name_span, false, type
            ));
            continue;
        }
        if (!span_valid(tag->name)) continue;
        csview name = span_sv(tag->name);
        if (prefix.size && !jsdoc_name_rest(name, prefix, &name)) continue;
        if (memchr(name.buf, '.', (size_t)name.size) || memchr(name.buf, '[', (size_t)name.size)) continue;
        bool has_children = false;
        for (isize j = i + 1; j < count && !has_children; j++) {
            has_children = span_valid(props[j].name) && jsdoc_name_is_under(span_sv(props[j].name), span_sv(tag->name));
        }
        bool array;
        TypeNode type;
        if (has_children && jsdoc_is_object_type(tag, &array)) {
            type = jsdoc_type_literal(props + i + 1, count - i - 1, span_sv(tag->name), jsdoc_tag_decl_span(tag));
            if (array) type = type_array(ast_new(TypeNode, type), false), type.parsed = true;
        } else if (tag->has_type) {
            type = jsdoc_take_type(tag, nullptr);
            type.from_jsdoc = false;
        } else {
            type = type_any;
        }
        // (A bracketed property is `foo?: T`: tsgo's reparser gives the property signature a question token)
        bool optional = tag->bracketed || type.jsdoc_optional;
        Span name_span = span_make_in(tag->name, name.buf, name.buf + name.size);
        // (Spanned like tsgo's tag node: to the next tag, its description included)
        list_push(vec_interface_member, &members, interface_member_make(
            jsdoc_tag_node_span_with(tag, &type), nullptr, false, optional, cstr_from_sv(name), name_span, false, type
        ));
    }
    TypeNode ret = type_interface((Interface){
        .members = members, .parsed = true, .jsdoc_literal = true, .span = span,
    });
    ret.parsed = true;
    return ret;
}

// A declaration's `@template` tags as its type parameters, when it has none written (tsc's
// getJSDocTypeParameterDeclarations: not the templates of a comment with a `@typedef`/`@callback`, those are its)
static Generic jsdoc_template_generic(VecJsDocTag* tags);
static Generic jsdoc_comment_template_generic(VecJsDocTag* tags, const char* comment);
static TypeNode jsdoc_callback_type(JsDocTag* callback, JsDocTag* children, isize count);

// The modifiers of a host's `@template`s that its kind doesn't allow (tsc's checkGrammarModifiers of a type parameter):
// `in`/`out` are for classes and type aliases, `const` for functions and classes
static void jsdoc_check_template_modifiers(Parser* p, VecJsDocTag const* tags, bool in_out, bool is_const) {
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_TEMPLATE || i->earlier) continue;
        if (!in_out && span_valid(i->in_span)) {
            jsdoc_error(p, i->in_span, 1274, "'in' modifier can only appear on a type parameter of a class, interface or type alias");
        }
        if (!in_out && span_valid(i->out_span)) {
            jsdoc_error(p, i->out_span, 1274, "'out' modifier can only appear on a type parameter of a class, interface or type alias");
        }
        if (!is_const && span_valid(i->const_span)) {
            jsdoc_error(p, i->const_span, 1277, "'const' modifier can only appear on a type parameter of a function, method or class");
        }
    }
}

static void jsdoc_apply_template(Parser* p, VecJsDocTag* tags, Generic* generic, bool is_class) {
    if (!generic_empty(generic)) return;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind == JSDOC_TAG_TYPEDEF || i->kind == JSDOC_TAG_CALLBACK) return;
    }
    jsdoc_check_template_modifiers(p, tags, is_class, true);
    generic_drop(generic);
    *generic = jsdoc_template_generic(tags);
}

// A class's JSDoc: `@template`s, and the type arguments of `@extends {Base<T>}` (tsc's getEffectiveBaseTypeNode: the
// tag's ExpressionWithTypeArguments stands for the heritage clause's)
// The rightmost name of an entity name expression (tsc's getIdentifierFromEntityNameExpression), null if it isn't one
static Span jsdoc_entity_name_last(Expr const* expr) {
    if (expr->tag == EXPR_GENERIC_PARAMS) expr = &expr->generic_params->expr;
    if (expr->tag == EXPR_IDENTIFIER) return expr->inner_span;
    if (expr->tag == EXPR_PROPERTY && expr->property->form == PROPERTY_EXPR_DOT) return property_name(expr->property);
    return span_null;
}

static void jsdoc_apply_class(Parser* p, VecJsDocTag* tags, ClassDecl* class_decl) {
    jsdoc_apply_template(p, tags, &class_decl->generic, true);
    Span extends_name = jsdoc_entity_name_last(&class_decl->extends);
    bool has_base = class_decl->extends.tag != EXPR_IMPLICIT_ANY;
    bool null_base = class_decl->extends.tag == EXPR_LITERAL && class_decl->extends.literal.tag == VALUE_NULL;
    // The tag whose type arguments the clause takes when it has none (tsgo's reparser): the first naming the clause's
    // class, and since tsgo 7.1.0-beta the first at all when the clause isn't an entity name (`extends A.extend()`),
    // but not `null`
    JsDocTag* supplier = nullptr;
    if (has_base && !null_base && class_decl->extends.tag != EXPR_GENERIC_PARAMS) {
        for_each (i, vec_jsdoc_tag, tags) {
            if (i->kind != JSDOC_TAG_EXTENDS || !i->has_type || i->nested || i->earlier || i->type.tag != TYPE_IDENTIFIER) continue;
            if (span_valid(extends_name) && !name_equals(span_sv(i->type.identifier->name), span_sv(extends_name))) continue;
            supplier = i;
            break;
        }
    }
    // Every other tag is checked against the base type (tsgo's checkJSDocAugmentsTagMatchesExtends): identical to it
    // for an entity name clause, assignable from it otherwise, TS8023 if not
    if (has_base && !null_base) {
        class_decl->jsdoc_extends_expression = !span_valid(extends_name);
        class_decl->jsdoc_extends_name = span_valid(extends_name) ? extends_name : expr_outer_span(&class_decl->extends);
        for_each (i, vec_jsdoc_tag, tags) {
            if (i->kind != JSDOC_TAG_EXTENDS || !i->has_type || i->nested || i->type.tag != TYPE_IDENTIFIER) continue;
            if (i == supplier && span_valid(extends_name)) continue;
            JsDocAugments augments = { .name = i->type.identifier->name, .tag = i->tag_name, .args_from_base = i == supplier };
            if (i->type.identifier->name.size) {
                // (The supplier's type arguments go to the clause below, out of this node: its type is the reference
                // with the clause's, JsDocAugments.args_from_base)
                augments.type = ast_new(TypeNode, jsdoc_take_type(i, nullptr));
                // (Its arity is checked, not its constraints: getTypeFromTypeNode, not checkSourceElement)
                augments.type->identifier->jsdoc_unchecked = true;
            }
            class_decl->jsdoc_augments = realloc(class_decl->jsdoc_augments, sizeof(JsDocAugments) * (class_decl->jsdoc_augments_count + 1));
            panic_if(!class_decl->jsdoc_augments, "out of memory");
            class_decl->jsdoc_augments[class_decl->jsdoc_augments_count++] = augments;
        }
    }
    // `@implements {I}` (tsc's getEffectiveImplementsTypeNodes: in JavaScript the tags, not the clause)
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_IMPLEMENTS || !i->has_type || i->earlier) continue;
        Span span;
        TypeNode type = jsdoc_take_type(i, &span);
        if (type.tag == TYPE_IDENTIFIER && p->shared->source->is_js) type.identifier->jsdoc_implements = true;
        list_push(vec_spanned_type, &class_decl->implements, spanned_type(span, type));
    }
    if (!supplier) return;
    JsDocTag* tag = supplier;
    class_decl->jsdoc_extends = tag->type_span;
    if (!vec_spanned_type_size(&tag->type.identifier->generic_params)) return;
    VecSpannedType params = tag->type.identifier->generic_params;
    tag->type.identifier->generic_params = (VecSpannedType){0};
    for_each (i, vec_spanned_type, &params) i->type.from_jsdoc = true;
    Expr* base = &class_decl->extends;
    Span span = expr_outer_span(base);
    *base = expr_generic_params(span, ast_new(GenericParamsExpr, { .expr = *base, .params = params }));
}

// What a function's JSDoc says about its parameters and return type: `@param`, `@returns`
// The `@param` tags that name no parameter (see FunctionImpl.jsdoc_param_issues). A dotted tag right after the tag of
// its object (`{Object}`/`{object}`, maybe an array) is part of that object's type (tsc's parseNestedTypeLiteral),
// any other is a tag of its own
static void jsdoc_find_param_issues(VecJsDocTag* tags, FunctionImpl* function) {
    if (function->jsdoc_params_checked) return;
    function->jsdoc_params_checked = true;
    Span objects[8]; // The chain of object tags the next child may be under
    u32 depth = 0;
    isize index = 0;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_PARAM || i->nested || i->earlier) continue;
        if (!span_valid(i->name)) {
            // (A missing name is an identifier '' to tsc, which takes the parameter at its position and is no TS8024:
            // tsgo's findMatchingParameter, checkUnmatchedJSDocParameters skips it)
            if (!span_valid(i->missing_name) || i->name_first) continue;
            bool taken = false;
            for (isize a = 0; a < vec_function_arg_size(&function->args) && !taken; a++) {
                taken = jsdoc_param_tag_any(tags, function, &function->args.data[a]) == i;
            }
            FunctionImplRare* rare = function_impl_rare_mut(function);
            if (i->has_type && !taken) {
                // (Its type is still checked: tsc's checkJSDocParameterTag)
                rare->jsdoc_unmatched = realloc(rare->jsdoc_unmatched, sizeof(JsDocUnmatchedParam) * (rare->jsdoc_unmatched_count + 1));
                panic_if(!rare->jsdoc_unmatched, "out of memory");
                rare->jsdoc_unmatched[rare->jsdoc_unmatched_count++] = (JsDocUnmatchedParam){
                    .name = i->missing_name, .type = jsdoc_take_type(i, nullptr),
                };
            }
            continue;
        }
        csview name = span_sv(i->name);
        char const* dot = nullptr;
        for (isize k = name.size; k-- > 0 && !dot;) if (name.buf[k] == '.') dot = name.buf + k;
        bool array;
        bool object = i->has_type && jsdoc_is_object_type(i, &array);
        if (dot) {
            csview left = csview_from_range(name.buf, dot);
            while (depth && !jsdoc_names_equal(span_sv(objects[depth - 1]), left)) depth--;
            if (depth) {
                if (object && depth < c_arraylen(objects)) objects[depth++] = i->name;
                continue;
            }
        }
        depth = 0;
        if (object) objects[depth++] = i->name;
        isize at = index++;
        // (A pattern parameter goes by the tag at its position)
        if (at < vec_function_arg_size(&function->args) && function->args.data[at].initializer.binding.tag != BINDING_IDENTIFIER) continue;
        Span left = span_null;
        if (dot) {
            left = span_make_in(i->name, name.buf, dot);
        } else {
            // (An object tag with children is name-first to tsc: parseNestedTypeLiteral sets isNameFirst)
            JsDocTag const* next = i + 1 < tags->data + tags->size ? i + 1 : nullptr;
            bool has_children = object && next && next->kind == JSDOC_TAG_PARAM && span_valid(next->name) &&
                next->name.size > name.size && next->name.buf[name.size] == '.' &&
                csview_equals_sv(csview_from_n(next->name.buf, name.size), name);
            bool found = i->name_first || has_children;
            for_each (a, vec_function_arg, &function->args) {
                Binding const* binding = &a->initializer.binding;
                found |= binding->tag == BINDING_IDENTIFIER && jsdoc_name_is(name, span_sv(binding->content_span));
            }
            if (found) continue;
        }
        FunctionImplRare* rare = function_impl_rare_mut(function);
        rare->jsdoc_param_issues = realloc(rare->jsdoc_param_issues, sizeof(JsDocParamIssue) * (rare->jsdoc_param_issue_count + 1));
        panic_if(!rare->jsdoc_param_issues, "out of memory");
        rare->jsdoc_param_issues[rare->jsdoc_param_issue_count++] = (JsDocParamIssue){ i->name, left };
        if (!dot && i->has_type) {
            rare->jsdoc_unmatched = realloc(rare->jsdoc_unmatched, sizeof(JsDocUnmatchedParam) * (rare->jsdoc_unmatched_count + 1));
            panic_if(!rare->jsdoc_unmatched, "out of memory");
            TypeNode type = jsdoc_take_type(i, nullptr);
            // (The last tag: the one `arguments` could be)
            bool last = true;
            for (JsDocTag const* j = i + 1; j < tags->data + tags->size && last; j++) {
                last = !(j->kind == JSDOC_TAG_PARAM && !j->nested && !j->earlier);
            }
            if (last) rare->jsdoc_arguments_candidate = rare->jsdoc_unmatched_count + 1;
            rare->jsdoc_unmatched[rare->jsdoc_unmatched_count++] = (JsDocUnmatchedParam){
                .name = i->name, .type = type, .variadic = i->variadic,
            };
        }
    }
}

static void jsdoc_apply_function(Parser* p, VecJsDocTag* tags, FunctionImpl* function) {
    bool declaration = p->shared->jsdoc_declaration;
    p->shared->jsdoc_declaration = false;
    jsdoc_find_param_issues(tags, function);
    // `@overload` tags (of every comment before the host), each a signature of their own: the function's, whose
    // implementation isn't one (tsc's getSignaturesOfSymbol). The comment's `@template`s are the overload's
    // (getEffectiveTypeParameterDeclarations of a JSDocSignature). Only of a function declaration, class method or
    // constructor: tsgo's reparser ignores them elsewhere (arrows, function expressions, object literal methods)
    if (!function_impl_rare(function)->jsdoc_overload_count && declaration && !function->accessor) {
        for_each (i, vec_jsdoc_tag, tags) {
            if (i->kind != JSDOC_TAG_OVERLOAD) continue;
            isize children = 0;
            while (i + 1 + children < tags->data + tags->size && i[1 + children].nested) children++;
            TypeNode type = jsdoc_callback_type(i, i + 1, children);
            if (i->earlier) {
                type.callable->function->generic = jsdoc_comment_template_generic(tags, i->comment);
            } else {
                // (The host's own comment: its `@template`s, which each overload declares again, tsgo's
                // gatherTypeParameters: the host takes the tags' types later, the overloads parse them again)
                Generic* generic = &type.callable->function->generic;
                for_each (t, vec_jsdoc_tag, tags) {
                    if (t->kind != JSDOC_TAG_TEMPLATE || t->earlier || !span_valid(t->name)) continue;
                    GenericItem item = {
                        .name = t->name, .full_span = t->name, .jsdoc = true,
                        .is_const = t->is_const, .is_in = t->is_in, .is_out = t->is_out,
                    };
                    if (t->has_type && jsdoc_reparse_type(p, t, &item.extends)) {
                        item.extends.from_jsdoc = false;
                        item.extends_explicit = true;
                    }
                    if (t->has_default && t->default_start) {
                        const char* c = t->default_start;
                        TypeNode fallback;
                        isize errors = vec_error_size(&p->shared->jsdoc_errors);
                        if (parse_jsdoc_bare_type(p, &c, source_file_start(p->shared->source) + cstr_size(&p->shared->source->contents), &fallback)) {
                            item.fallback = ast_new(TypeNode, fallback);
                            item.fallback_explicit = true;
                        }
                        while (vec_error_size(&p->shared->jsdoc_errors) > errors) {
                            error_drop(vec_error_back_mut(&p->shared->jsdoc_errors));
                            vec_error_pop(&p->shared->jsdoc_errors);
                        }
                    }
                    list_push(vec_generic_item, &generic->items, item);
                }
            }
            FunctionImplRare* rare = function_impl_rare_mut(function);
            rare->jsdoc_overloads = realloc(rare->jsdoc_overloads, sizeof(TypeNode) * (rare->jsdoc_overload_count + 1));
            panic_if(!rare->jsdoc_overloads, "out of memory");
            rare->jsdoc_overloads[rare->jsdoc_overload_count++] = type;
        }
    }
    // tsgo's reparser goes through a function's tags in order: `@type` is its signature (FullSignature) only if nothing
    // typed it before (an `@param {T}`, `@return {T}`, `@template` or `@this`, a typed parameter); then the later
    // `@param`/`@return`/`@template` tags aren't applied. A `@type` after those is ignored
    bool type_wins = false;
    JsDocTag* first_type_tag = function->accessor ? nullptr : jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (first_type_tag && !first_type_tag->earlier) {
        bool typed = function->ret_explicit || !generic_empty(&function->generic);
        for_each (i, vec_function_arg, &function->args) typed |= i->initializer.binding.has_explicit_type;
        for (JsDocTag const* i = tags->data; i < first_type_tag; i++) {
            if (i->nested || i->earlier) continue;
            typed |= ((i->kind == JSDOC_TAG_PARAM || i->kind == JSDOC_TAG_RETURNS) && i->has_type) ||
                i->kind == JSDOC_TAG_TEMPLATE || i->kind == JSDOC_TAG_THIS;
        }
        if (typed) {
            type_drop(&first_type_tag->type);
            first_type_tag->has_type = false;
        } else {
            type_wins = true;
        }
    }
    if (!type_wins) jsdoc_apply_template(p, tags, &function->generic, false);
    for_each (i, vec_jsdoc_tag, tags) {
        function->jsdoc_typed |= (i->kind == JSDOC_TAG_PARAM || i->kind == JSDOC_TAG_TYPE) && !i->nested && !i->earlier;
        function->jsdoc_type_tagged |= i->kind == JSDOC_TAG_TYPE && !i->nested && !i->earlier;
    }
    // `@this {T}`: the function's `this` parameter (tsc's getJSDocThisTag in getSignatureFromDeclaration and
    // getTypeForThisExpressionFromJSDoc). Not of an arrow function, which has no `this` of its own
    // An arrow function gets one in its signature too, though its `this` stays the outer one (checkJSDocThisTag's
    // TS2730). An untyped `@this` is `this: any`
    JsDocTag* this_tag = nullptr;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind == JSDOC_TAG_THIS && !i->nested && !i->earlier) {
            this_tag = i;
            break;
        }
    }
    if (this_tag && function_impl_is_arrow(function)) {
        jsdoc_error(p, this_tag->tag_name, 2730, "An arrow function cannot have a 'this' parameter.");
    } else if (this_tag && function->accessor) {
        // (tsgo's reparsed `this` parameter, checkParameter)
        jsdoc_error(p, this_tag->tag_name, 2784, "'get' and 'set' accessors cannot declare 'this' parameters.");
    }
    FunctionArg const* first = vec_function_arg_size(&function->args) ? function->args.data : nullptr;
    bool has_this = first && first->initializer.binding.tag == BINDING_IDENTIFIER &&
        span_equals(first->initializer.binding.content_span, "this");
    if (this_tag && !has_this && !function->is_constructor) {
        // (No declaration span: tsc's this-parameter symbol of a JSDoc tag has no declaration, `Symbol(this)`)
        FunctionArg arg = {
            .jsdoc_this = true,
        };
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = this_tag->tag_name,
            .has_explicit_type = true,
        };
        if (this_tag->has_type) {
            Span type_span;
            binding_set_type(&arg.initializer.binding, jsdoc_take_type(this_tag, &type_span));
            binding_set_explicit_type_span(&arg.initializer.binding, type_span);
        } else {
            binding_set_type(&arg.initializer.binding, jsdoc_synthesized(type_any));
            arg.initializer.binding.explicit_type->from_jsdoc = true;
        }
        vec_function_arg_insert_n(&function->args, 0, &arg, 1);
    }
    jsdoc_apply_type_signature(tags, function, declaration);
    for_each (i, vec_function_arg, type_wins ? &(VecFunctionArg){0} : &function->args) {
        Binding* binding = &i->initializer.binding;
        if (binding->has_explicit_type) continue;
        JsDocTag* tag = jsdoc_param_tag(tags, function, i);
        if (!tag) {
            // `@param [b]` without a type still makes b optional (tsc's isJSDocOptionalParameter)
            JsDocTag const* untyped = jsdoc_param_tag_any(tags, function, i);
            if (untyped && untyped->bracketed && initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY) {
                i->explicit_optional = true;
            } else if (untyped && untyped->bracketed) {
                i->jsdoc_optional_initialized = true;
            }
            continue;
        }
        bool variadic = tag->variadic;
        bool bracketed = tag->bracketed;
        // `@param {Object} o` + `@param {string} o.a`: a literal of the nested ones
        isize children = 0;
        while (tag + 1 + children < tags->data + tags->size && tag[1 + children].kind == JSDOC_TAG_PARAM &&
            span_valid(tag[1 + children].name) && jsdoc_name_is_under(span_sv(tag[1 + children].name), span_sv(tag->name))
        ) {
            children++;
        }
        bool direct = false; // (`o.a.b` without `o.a` is TS8032, which isn't reported: o keeps its type)
        for (isize j = 1; j <= children && !direct; j++) {
            csview rest = {0};
            jsdoc_name_rest(span_sv(tag[j].name), span_sv(tag->name), &rest);
            direct = rest.size && !memchr(rest.buf, '.', (size_t)rest.size);
        }
        bool array;
        if (direct && jsdoc_is_object_type(tag, &array)) {
            Span type_span = tag->type_span;
            TypeNode literal = jsdoc_type_literal(tag + 1, children, span_sv(tag->name), jsdoc_tag_decl_span(tag));
            if (array) literal = type_array(ast_new(TypeNode, literal), false), literal.parsed = true;
            type_drop(&tag->type);
            tag->type = literal;
            tag->type_span = type_span;
        }
        Span type_span = tag->type_span;
        if (tag->has_type) {
            binding_set_type(binding, jsdoc_take_type(tag, &type_span));
        } else {
            TypeNode reparsed = {0};
            if (!jsdoc_reparse_type(p, tag, &reparsed)) continue;
            binding_set_type(binding, reparsed);
        }
        binding->explicit_type->from_jsdoc = true;
        binding_set_explicit_type_span(binding, type_span);
        binding->has_explicit_type = true;
        if (binding->explicit_type->jsdoc_optional || bracketed) {
            // `{T=}` (of type `T | undefined`, which tsc prints) and `[name]` are optional parameters
            if (initialized_binding_value(&i->initializer)->tag == EXPR_IMPLICIT_ANY) i->explicit_optional = true;
            else i->jsdoc_optional_initialized = true;
            if (!i->jsdoc_question) i->jsdoc_question = ast_new(Span, jsdoc_tag_node_span_with(tag, binding->explicit_type));
        }
        // `{...T=}` on a rest parameter makes it optional, which it can't be (tsgo's reparser gives it a question
        // token: checkGrammarParameterList's TS1047 at the whole tag)
        if (variadic && i->rest && binding->explicit_type->jsdoc_optional) {
            const char* at = tag->tag_name.buf - 1;
            const char* end = tag + 1 < tags->data + tags->size && tag[1].comment == tag->comment ? tag[1].tag_name.buf - 1 :
                strstr(at, "*/");
            if (end) jsdoc_error(p, span_make(p->shared->source, at, end), 1047, "A rest parameter cannot be optional.");
        }
        // `{...T}` is only `T[]` (tsgo's JSDocVariadicType): it makes no parameter a rest parameter. `{...T=}` is the
        // optional of it, `T[] | undefined`
        TypeNode* variadic_type = binding->explicit_type;
        if (variadic && variadic_type->jsdoc_optional && variadic_type->tag == TYPE_UNION &&
            vec_type_size(&variadic_type->composite) == 2
        ) {
            variadic_type = vec_type_at_mut(&variadic_type->composite, 0);
        }
        if (variadic) {
            *variadic_type = type_array(ast_new(TypeNode, *variadic_type), false);
            variadic_type->parsed = true;
        }
        binding->explicit_type->from_jsdoc = true;
    }
    // A getter's `@type` is the property's, its return type (tsc's getAnnotatedAccessorType; a setter's isn't looked at)
    JsDocTag* type_tag = jsdoc_find_typed(tags, JSDOC_TAG_TYPE);
    if (type_tag && function->accessor == 1 && !function->ret_explicit) {
        // (A return type annotation then, where a `unique symbol` isn't allowed: checkGrammarTypeOperatorNode)
        if (type_tag->type.tag == TYPE_UNIQUE_SYMBOL) {
            grammar_error(p, type_tag->type_span, 1335, "'unique symbol' types are not allowed here.");
        }
        Span ret_span;
        function->ret = jsdoc_take_type(type_tag, &ret_span);
        function_impl_set_ret_span(function, ret_span);
        function->ret_explicit = true;
        type_tag = nullptr;
    }
    // (A `@this` made a typed `this` parameter: tsgo's reparser applies `@type` only to a function without typed
    // parameters)
    if (type_tag && vec_function_arg_size(&function->args) && function->args.data[0].jsdoc_this) type_tag = nullptr;
    // A `@type` that isn't a written signature: its signature is resolved by the checker
    if (type_tag && (declaration || (type_tag->type.tag != TYPE_CALLABLE && type_tag->type.tag != TYPE_INTERFACE)) &&
        !function_impl_rare(function)->jsdoc_signature_type
    ) {
        function_impl_rare_mut(function)->jsdoc_signature_span = type_tag->type_span;
        function_impl_rare_mut(function)->jsdoc_signature_type = ast_new(TypeNode, jsdoc_take_type(type_tag, nullptr));
    }
    if (!function->ret_explicit && !function->is_constructor && !type_wins) {
        JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_RETURNS);
        if (tag) {
            Span ret_span;
            function->ret = jsdoc_take_type(tag, &ret_span);
            function_impl_set_ret_span(function, ret_span);
            function->ret_explicit = true;
            if (tag->predicate && !function->predicate) {
                function->predicate = tag->predicate;
                tag->predicate = nullptr;
            }
        }
    }
}

// The function type of a `@callback` tag and its `@param`/`@returns` tags (tsc's JSDocSignature)
static TypeNode jsdoc_callback_type(JsDocTag* callback, JsDocTag* children, isize count) {
    FunctionImpl function = { .args = vec_function_arg_init(), .jsdoc_type = true, .jsdoc_callback = true };
    char const* sig_start = span_end_ptr(jsdoc_tag_node_span(callback));
    char const* sig_end = sig_start;
    for (isize i = 0; i < count; i++) {
        JsDocTag* tag = &children[i];
        if (tag->kind == JSDOC_TAG_RETURNS && !function.ret_explicit && span_end_ptr(jsdoc_tag_node_span(tag)) > sig_end) {
            sig_end = span_end_ptr(jsdoc_tag_node_span(tag));
        }
        if (tag->kind == JSDOC_TAG_RETURNS) {
            if (tag->has_type && !function.ret_explicit) {
                Span ret_span;
                function.ret = jsdoc_take_type(tag, &ret_span);
                function_impl_set_ret_span(&function, ret_span);
                function.ret.from_jsdoc = false;
                function.ret_explicit = true;
                if (tag->predicate) {
                    function.predicate = tag->predicate;
                    tag->predicate = nullptr;
                }
            }
            continue;
        }
        if (tag->kind == JSDOC_TAG_THIS) {
            // The signature's `this` parameter (tsc's getSignatureFromDeclaration of a JSDocSignature)
            bool has_this = vec_function_arg_size(&function.args) && function.args.data[0].jsdoc_this;
            if (has_this) continue;
            FunctionArg arg = { .jsdoc_this = true };
            arg.initializer.binding = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = tag->tag_name, .has_explicit_type = true };
            if (tag->has_type) {
                Span type_span;
                binding_set_type(&arg.initializer.binding, jsdoc_take_type(tag, &type_span));
                binding_set_explicit_type_span(&arg.initializer.binding, type_span);
            } else {
                binding_set_type(&arg.initializer.binding, jsdoc_synthesized(type_any));
            }
            vec_function_arg_insert_n(&function.args, 0, &arg, 1);
            continue;
        }
        if (tag->kind != JSDOC_TAG_PARAM || !span_valid(tag->name) || memchr(tag->name.buf, '.', (size_t)tag->name.size)) continue;
        FunctionArg arg = { .span = jsdoc_tag_node_span(tag) }; // (The whole tag: tsgo's reparsed parameter)
        bool array;
        TypeNode type;
        bool has_children = false;
        for (isize j = i + 1; j < count && !has_children; j++) {
            has_children = children[j].kind == JSDOC_TAG_PARAM && span_valid(children[j].name) &&
                jsdoc_name_is_under(span_sv(children[j].name), span_sv(tag->name));
        }
        if (has_children && jsdoc_is_object_type(tag, &array)) {
            type = jsdoc_type_literal(children + i + 1, count - i - 1, span_sv(tag->name), jsdoc_tag_decl_span(tag));
            if (array) type = type_array(ast_new(TypeNode, type), false), type.parsed = true;
        } else if (tag->has_type) {
            type = jsdoc_take_type(tag, nullptr);
            type.from_jsdoc = false;
        } else {
            type = jsdoc_synthesized(type_any); // (No type node: tsc's printer makes an `any`)
        }
        arg.span = jsdoc_tag_node_span_with(tag, &type);
        if (span_end_ptr(arg.span) > sig_end) sig_end = span_end_ptr(arg.span);
        if (type.jsdoc_optional || tag->bracketed) {
            arg.explicit_optional = true;
            arg.jsdoc_question = ast_new(Span, jsdoc_tag_node_span(tag)); // (tsgo's reparser: the tag is its question token)
        }
        // (`{...T}` makes a rest parameter of type T: tsgo's reparser, which TS2370 complains about unless T is an array)
        if (tag->variadic) {
            arg.rest = true;
            arg.initializer.is_rest_parameter = true;
        }
        arg.initializer.binding = (Binding){
            .tag = BINDING_IDENTIFIER,
            .content_span = tag->name,
            .has_explicit_type = true,
            .explicit_type = ast_new(TypeNode, type),
        };
        list_push(vec_function_arg, &function.args, arg);
    }
    Span span = jsdoc_tag_decl_span(callback);
    function_impl_set_args_span(&function, span);
    // (The signature node: from where the callback's own text ends (its first child's `@`) to its last child's end,
    // tsgo's parseJSDocSignature)
    // (An `@overload` is a declaration, at its tag)
    if (sig_end < sig_start) sig_end = sig_start;
    function.full_span = callback->kind == JSDOC_TAG_CALLBACK ? span_make_in(span, sig_start, sig_end) : span;
    return function_impl_into_type(function);
}

// The type parameters of the `@typedef`/`@callback` tags of a comment: every `@template` in it (tsc's
// getEffectiveTypeParameterDeclarations of a JSDocTypeAlias)
static Generic jsdoc_template_generic(VecJsDocTag* tags) {
    return jsdoc_comment_template_generic(tags, nullptr);
}

// The `@template`s of one comment (null: of the host's, not an earlier one's)
static Generic jsdoc_comment_template_generic(VecJsDocTag* tags, const char* comment) {
    Generic ret = {0};
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->kind != JSDOC_TAG_TEMPLATE || !span_valid(i->name)) continue;
        if (comment ? i->comment != comment : i->earlier) continue;
        // The tag: from its `@` to the next tag's `@` (the line breaks and `*`s before it included) or the comment's end
        const char* at = i->tag_name.buf - 1;
        const char* tag_end = nullptr;
        for (JsDocTag const* j = i + 1; j < tags->data + tags->size && !tag_end; j++) {
            if (j->comment == i->comment && j->tag_name.buf != i->tag_name.buf) tag_end = j->tag_name.buf - 1;
        }
        if (!tag_end) tag_end = strstr(at, "*/");
        GenericItem item = {
            .name = i->name, .full_span = i->name, .jsdoc = true, .is_const = i->is_const, .is_in = i->is_in,
            .is_out = i->is_out, .jsdoc_tag = tag_end ? span_make_in(i->name, at, tag_end) : span_null,
        };
        if (i->has_type) {
            item.extends = jsdoc_take_type(i, nullptr);
            item.extends.from_jsdoc = false;
            item.extends_explicit = true;
        }
        if (i->has_default) {
            item.fallback = ast_new(TypeNode, i->default_type);
            item.fallback_explicit = true;
            i->has_default = false;
        } else if (i->bracketed) {
            item.jsdoc_default_missing = true;
        }
        list_push(vec_generic_item, &ret.items, item);
    }
    return ret;
}

static bool parse_import_export_statement(Parser* parent, Statement* ret);

// Where tsgo's parseImportTag goes on after a missing `from`: parseModuleSpecifier's expression at `c` (an operator there
// has a missing left operand: `= require("m")`, `* as`), which checkExternalImportOrExportDeclaration says TS1141
// about unless it's a string literal or missing. Invalid otherwise
static Span jsdoc_import_specifier_extent(Parser* parent, const char* c, const char* end) {
    if (c >= end || *c == '"' || *c == '\'') return span_null;
    Parser p = parser(parent);
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    p.pos = c;
    p.whitespace_end = nullptr;
    const char* start = c;
    const char* stop = c;
    // (A binary operator's characters: what follows is its right operand)
    while (stop < end && strchr("=*/%<>&|^!?,", *stop)) stop++;
    if (stop > c) {
        p.pos = stop;
        hop_whitespace(&p);
    }
    Expr operand;
    if (parse_expr(&p, expr_ctx_null, &operand)) {
        stop = span_end_ptr(expr_outer_span(&operand));
        expr_drop(&operand);
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    parse_fail(&p);
    // (The expression parser's end of a call takes the comment's `*/` along)
    if (stop > end) stop = end;
    while (stop > start && is_js_ascii_space(stop[-1])) stop--;
    return stop > start ? span_make(parent->shared->source, start, stop) : span_null;
}

// `@import { A } from "m"` (TS 5.5): a type-only import declaration, parsed by the statement parser from the tag's
// name on (tsc's JSDocImportTag). Parse errors are JSDoc errors
static void jsdoc_import_statement(Parser* parent, JsDocTag const* tag, const char* end, bool toplevel, VecStatement* out) {
    Parser p = parser(parent);
    SourceFile const* source = p.shared->source;
    // The statement parser doesn't stop at the `*/`: what tsc's parseImportTag says about a clause, `from` or module
    // specifier that is missing comes first
    const char* last = tag->tag_name.buf + tag->tag_name.size; // Past the last token
    const char* c = jsdoc_skip_space(last, end);
    bool clause = false;
    const char* word_end = c;
    while (word_end < end && is_identifier_char(*word_end)) word_end++;
    // (The JSDoc scanner's identifier, the first token after the tag name, takes `-` too: `@import a-b from "m"` is a
    // default import named `a-b`, tsc's scanJSDocToken)
    if (word_end > c && !(*c >= '0' && *c <= '9')) {
        while (word_end < end && (is_identifier_char(*word_end) || *word_end == '-')) word_end++;
    }
    // (A reserved word isn't a default import's name: `@import import { A } from "m"` has no clause)
    csview word = csview_from_range(c, word_end);
    bool name = word_end > c && !(*c >= '0' && *c <= '9') && !is_reserved_word(word);
    if ((p.shared->in_async && csview_equals(word, "await")) || (p.shared->in_generator && csview_equals(word, "yield"))) {
        // (Nor `await` in an async function, `yield` in a generator, tsc's isIdentifier: the module specifier is the
        // expression `await from "m"`, the tag imports nothing and nothing is said)
        parse_fail(&p);
        return;
    }
    if (c < end && (name || *c == '{' || *c == '*')) {
        clause = true;
        if (*c == '{') {
            while (c < end && *c != '}') c++;
            if (c < end) c++;
        } else if (*c == '*') {
            // `* as name`
            for (int word = 0; word < 2; word++) {
                c = jsdoc_skip_space(c + (word ? 0 : 1), end);
                while (c < end && is_identifier_char(*c)) c++;
            }
        } else {
            c = word_end;
            const char* comma = jsdoc_skip_space(c, end);
            if (comma < end && *comma == ',') clause = false; // `x, { y }`: left to the statement parser
        }
        last = c;
        c = jsdoc_skip_space(c, end);
    }
    bool bad = false;
    if (clause) {
        const char* word = c;
        while (c < end && is_identifier_char(*c)) c++;
        if (csview_equals(csview_from_range(word, c), "from")) {
            last = c;
            c = jsdoc_skip_space(c, end);
        } else {
            c = word;
            const char* token_end = c;
            while (token_end < end && is_identifier_char(*token_end)) token_end++;
            if (token_end == c && c < end) token_end++;
            parse_error(&p, span_make(source, c, token_end), 1005, "'from' expected.");
            bad = true;
            Span specifier = jsdoc_import_specifier_extent(&p, c, end);
            if (span_valid(specifier)) jsdoc_error(&p, specifier, 1141, "String literal expected.");
        }
    }
    if (!clause && !name && c < end && *c != '"' && *c != '\'') {
        // tsc's parseModuleSpecifier parses an expression where the string isn't: a keyword or number is none
        parse_error(&p, span_make(source, c, word_end > c ? word_end : c + 1), 1109, "Expression expected.");
        bad = true;
    } else if (c >= end) {
        // After the tag name, tsc's current token is the JSDoc scanner's whitespace or newline token there
        const char* token_end = last;
        if (!clause && token_end < end) {
            if (*token_end == '\n') token_end++;
            else if (token_end[0] == '\r' && token_end[1] == '\n') token_end += 2;
            else while (token_end < end && (*token_end == ' ' || *token_end == '\t')) token_end++;
        }
        parse_error(&p, span_make(source, last, token_end), 1109, "Expression expected.");
        bad = true;
    }
    if (bad) {
        jsdoc_keep_errors(&p);
        parse_fail(&p);
        return;
    }
    p.pos = tag->tag_name.buf;
    p.whitespace_end = nullptr;
    bool outer_in_jsdoc = p.shared->in_jsdoc;
    const char* outer_end = p.shared->jsdoc_end;
    p.shared->in_jsdoc = true;
    p.shared->jsdoc_end = end;
    Statement statement;
    if (parse_import_export_statement(&p, &statement)) {
        if (statement.tag == STATEMENT_IMPORT_EXPORT && statement.import_export->requires_target &&
            !port_item_is_export(&statement.import_export->main) && statement.import_export->main.tag != PORT_IMPORT_ALIAS
        ) {
            statement.import_export->main.type_only = true;
            // (Type-only, so its `resolution-mode` attribute counts, like an `import type`'s)
            if (statement.import_export->module_ref) {
                statement.import_export->module_ref->resolution_override =
                    import_attributes_resolution_mode(statement.import_export->attributes, nullptr);
            }
            statement.import_export->jsdoc = true;
            statement.import_export->main.jsdoc = true;
            // (In a block or function: tsc's collectModuleReferences never resolves it to a file, see
            // port_statement_add_names())
            if (!toplevel && statement.import_export->module_ref) statement.import_export->module_ref->ambient_only = true;
            for_each (i, vec_port_item, &statement.import_export->items) i->jsdoc = true;
            // (The tag's range, to the next tag or the comment's end past a line break, else its last token: tsgo's
            // reparsed declaration)
            const char* tag_end = span_end_ptr(jsdoc_tag_node_span(tag));
            const char* statement_end = p.pos;
            if (statement_end < tag_end && *statement_end == ';') statement_end++;
            const char* last_token_end = statement_end;
            while (statement_end < tag_end && (*statement_end == ' ' || *statement_end == '\t')) statement_end++;
            if (statement_end >= tag_end || (*statement_end != '\n' && *statement_end != '\r')) tag_end = last_token_end;
            statement.span = span_make(p.shared->source, tag->tag_name.buf - 1, tag_end);
            statement.flow = nullptr;
            vec_statement_push(out, statement);
        } else {
            statement_drop(&statement);
        }
    }
    p.shared->in_jsdoc = outer_in_jsdoc;
    p.shared->jsdoc_end = outer_end;
    jsdoc_keep_errors(&p);
    parse_fail(&p);
}

// The alias declaration `C` of a dotted JSDoc name `A.B.C` (`full_name`) wrapped in namespace declarations A and B,
// inside out. The same statement for a plain name
static Statement jsdoc_wrap_in_namespaces(Statement alias, Span full_name, Span name, Span span) {
    for (char const* end = span_start_ptr(name) - 1; end > span_start_ptr(full_name);) {
        char const* start = end;
        while (start > span_start_ptr(full_name) && start[-1] != '.') start--;
        Block contents = {0};
        alias.decl.props.exported = DECL_EXPORTED_YES;
        vec_statement_push(&contents.statements, alias);
        // (The declaration starts at its part of the name: tsc's JSDocNamespaceDeclaration)
        NamespaceDecl inner = {
            .name = span_make_in(full_name, start, end), .contents = contents, .jsdoc = true,
            .full_span = span_make_in(full_name, start, span_end_ptr(span)),
        };
        alias = (Statement){ .tag = STATEMENT_DECL_NAMESPACE, .decl.namespace = ast_new(NamespaceDecl, inner), .span = span };
        end = start - 1;
    }
    return alias;
}

// Whether there is nothing but whitespace and comments from `at` to `end`
static bool only_trivia_until(const char* at, const char* end) {
    while (at < end) {
        if (is_js_ascii_space(*at)) {
            at++;
        } else if (at + 1 < end && at[0] == '/' && at[1] == '/') {
            while (at < end && *at != '\n') at++;
        } else if (at + 1 < end && at[0] == '/' && at[1] == '*') {
            const char* close = strstr(at + 2, "*/");
            if (!close) return true;
            at = close + 2;
        } else {
            return false;
        }
    }
    return true;
}


// Whether a comment's text mentions `@typedef`, `@callback`, `@import` or `@template` (one pass over its `@`s)
static bool jsdoc_text_declares(csview text) {
    const char* end = text.buf + text.size;
    for (const char* at = text.buf; (at = memchr(at, '@', (size_t)(end - at))); at++) {
        csview rest = csview_from_range(at + 1, end);
        if (csview_starts_with(rest, "typedef") || csview_starts_with(rest, "callback") || csview_starts_with(rest, "import") ||
            csview_starts_with(rest, "template")
        ) {
            return true;
        }
    }
    return false;
}

// Binds the declaration tags (`@typedef`, `@callback`, `@import`) of the JSDoc comments from `from` to `to` that no
// statement list inside claimed yet, as type alias and import declarations appended to `out`, and claims them. tsc
// declares a typedef in the block scope of its host (delayedBindJSDocTypedefTag): the innermost statement list
// containing the comment. bind: false only claims them (the comments before a block's `}` have no host)
// eof: the comments are the end of the file's, whose JSDoc host is the end of file token (checked like any other)
static void jsdoc_bind_comments(
    Parser* parent, const char* from, const char* to, const char* inner_from, bool bind, bool toplevel,
    bool eof, VecStatement* out
) {
    VecJsDocRange* comments = &parent->shared->jsdoc_comments;
    isize lo = 0, hi = comments->size;
    while (lo < hi) {
        isize mid = (lo + hi) / 2;
        if (comments->data[mid].start < from) lo = mid + 1;
        else hi = mid;
    }
    for (isize c = lo; c < comments->size && comments->data[c].end <= to; c++) {
        JsDocRange range = comments->data[c];
        if (range.claimed) continue;
        comments->data[c].claimed = true;
        if (!bind) continue;
        csview text = csview_from_range(range.start, range.end);
        // A comment before another one right before the statement is the statement's too (tsc's getJSDocCommentRanges),
        // whose host takes nothing of it but `@overload`s: only its declarations are bound
        bool earlier = inner_from && c + 1 < comments->size && comments->data[c + 1].end <= inner_from &&
            only_trivia_until(range.end, inner_from);
        if (!eof && !earlier && !jsdoc_text_declares(text)) {
            continue;
        }
        VecJsDocTag tags = {0};
        bool outer_unchecked = parent->shared->jsdoc_unchecked;
        parent->shared->jsdoc_unchecked |= earlier; // (Its types aren't checked: no grammar errors)
        parse_jsdoc_comment(parent, range, &tags);
        parent->shared->jsdoc_unchecked = outer_unchecked;
        for_each (i, vec_jsdoc_tag, &tags) {
            if (i->kind == JSDOC_TAG_IMPORT) {
                jsdoc_import_statement(parent, i, range.end - 2, toplevel, out);
                continue;
            }
            if (i->kind != JSDOC_TAG_TYPEDEF && i->kind != JSDOC_TAG_CALLBACK) continue;
            Span tag_span = jsdoc_tag_decl_span(i);
            if (!span_valid(i->name)) continue;
            // `@typedef {T} A.B.C` declares C in namespace A.B (tsc's JSDocNamespaceDeclaration)
            Span full_name = i->name;
            if (memchr(i->name.buf, '.', (size_t)i->name.size)) {
                char const* last_dot = i->name.buf + i->name.size;
                while (last_dot[-1] != '.') last_dot--;
                i->name = span_make_in(i->name, last_dot, span_end_ptr(full_name));
                if (!i->name.size) continue;
            }
            isize children = 0;
            while (i + 1 + children < tags.data + tags.size && i[1 + children].nested) children++;
            TypeNode type;
            bool array;
            if (i->kind == JSDOC_TAG_CALLBACK) {
                type = jsdoc_callback_type(i, i + 1, children);
            } else {
                bool has_properties = false;
                JsDocTag* type_child = nullptr;
                for (isize j = 0; j < children; j++) {
                    has_properties |= i[1 + j].kind == JSDOC_TAG_PROPERTY;
                    if (!type_child && i[1 + j].kind == JSDOC_TAG_TYPE && i[1 + j].has_type) type_child = &i[1 + j];
                }
                bool child_array;
                if (type_child && !jsdoc_is_object_type(type_child, &child_array)) {
                    type = jsdoc_take_type(type_child, nullptr);
                    type.from_jsdoc = false;
                } else if (has_properties && jsdoc_is_object_type(i, &array)) {
                    type = jsdoc_type_literal(i + 1, children, (csview){0}, tag_span);
                    if (array) type = type_array(ast_new(TypeNode, type), false), type.parsed = true;
                } else if (i->has_type) {
                    type = jsdoc_take_type(i, nullptr);
                    type.from_jsdoc = false;
                } else {
                    type = type_any;
                }
            }
            jsdoc_check_template_modifiers(parent, &tags, true, false);
            Span span = tag_span;
            vec_statement_push(out, (Statement){
                .tag = STATEMENT_DECL_TYPE,
                .decl.type = ast_new(TypeDecl, {
                    .name = i->name,
                    .full_span = span,
                    .generic = jsdoc_template_generic(&tags),
                    .type = type,
                    .jsdoc = true,
                    .jsdoc_end = jsdoc_alias_end(i, &type),
                }),
                .span = span,
            });
            Statement alias = vec_statement_pull(out);
            vec_statement_push(out, jsdoc_wrap_in_namespaces(alias, full_name, i->name, span));
        }
        vec_jsdoc_tag_drop(&tags);
    }
}

// A function or arrow function expression is a JSDoc host of its own (`f(/** @param {T} x */ (x) => {})`)
static void jsdoc_apply_function_expression(Parser* p, Expr* function) {
    VecJsDocTag tags = {0};
    if (jsdoc_before(p, span_start_ptr(expr_outer_span(function)), &tags)) jsdoc_apply_function(p, &tags, function->function);
    vec_jsdoc_tag_drop(&tags);
}

static inline bool expect_type(Parser* parent, TypePrecedence min_precedence, TypeNode* ret) {
    if (parse_type(parent, min_precedence, ret)) {
        return true;
    } else {
        parse_error(parent, span_from_token_or_eof(parent), 1110, "Type expected.");
        *ret = (TypeNode){
            .tag = TYPE_ANY,
            .any.unkown_symbol_span = span_from_pos(parent),
        };
        return false;
    }
}

// `is_declaration`: an interface's body, else a type literal
// tsc's isTypeMemberStart: what a type literal's or interface's member list takes as an element. A name (after any
// modifiers) has to be followed by `(`, `<`, `?`, `:`, `,` or what ends a statement: `{ f2 = 1 }` starts none (TS1131)
static bool type_member_start_follows(Tok t) {
    char c = t.start == t.end ? 0 : *t.start;
    return c == '(' || c == '<' || c == '?' || c == ':' || c == ',' || c == ';' || c == '}' || !c || t.newline_before;
}
static bool is_type_member_start(Parser* p) {
    static char const* const modifiers[] = { "abstract", "accessor", "async", "const", "declare", "default", "export",
        "in", "out", "private", "protected", "public", "readonly", "static", "override" };
    Parser look = parser(p);
    hop_whitespace(&look);
    Tok t = tok_peek(&look);
    bool ret = true;
    // (`+readonly`/`-readonly` start a mapped type's member)
    if (t.start == t.end || *t.start == '(' || *t.start == '<' || *t.start == '[' || *t.start == '+' || *t.start == '-') {
        // (a signature, an index signature or a mapped type)
    } else if (tok_is_word(t) || *t.start == '"' || *t.start == '\'' || is_number_start_char(*t.start)) {
        // (The usual case first: a name and what follows it)
        Tok next = tok_next(&look, t);
        if (!type_member_start_follows(next) && tok_is_word(t) && !tok_is(t, "get") && !tok_is(t, "set")) {
            // Modifiers, then `[` or a name
            bool id_token = false;
            for (;;) {
                bool modifier = false;
                for (u32 i = 0; i < c_arraylen(modifiers) && !modifier; i++) modifier = tok_is(t, modifiers[i]);
                if (!modifier) break;
                id_token = true;
                t = tok_next(&look, t);
            }
            if (t.start < t.end && *t.start == '[') {
                ret = true;
            } else {
                if (t.start < t.end && (tok_is_word(t) || *t.start == '"' || *t.start == '\'' || is_number_start_char(*t.start))) {
                    id_token = true;
                    t = tok_next(&look, t);
                }
                ret = id_token && type_member_start_follows(t);
            }
        }
    } else {
        ret = false;
    }
    parse_fail(&look);
    return ret;
}

static bool parse_interface(Parser* parent, bool is_declaration, Interface* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    VecInterfaceMember members = {0};
    const char* prev_s = nullptr;
    // For TS7061: where the first two members are reported (tsc's getErrorSpanForNode), whether the first is a mapped
    // member and whether one is a mapped member where there can be none (a property named by an `in` expression)
    Span member_error_spans[2] = {0};
    bool member_whole[2] = {0};
    u32 member_count = 0;
    bool first_mapped = false, misplaced_mapped = false;
    bool outer_no_mapped_member = p.shared->no_mapped_member;

    u32 saved_context = list_enter(&p, PARSING_TYPE_MEMBERS);
    while (!at_eof(&p, p.pos) && *p.pos != '}' && p.pos != prev_s) {
        prev_s = p.pos;
        hop_whitespace(&p);

        ClassItem item;
        char const* member_start = p.pos;
        p.shared->no_mapped_member = is_declaration || member_count;
        bool accessor_body = false;
        Parser member_p = parser(&p);
        bool parsed_member = parse_list_member(&member_p, PARSING_TYPE_MEMBERS, true, true, &item);
        // (tsc's isTypeMemberStart rejects a name followed by something else than `(`, `<`, `?`, `:`, `,` or the end of
        // a statement, which only an untyped property of tttt's can be: `{ f2 = 1 }` is TS1131, the list ends)
        if (parsed_member && item.tag == CLASS_ITEM_FIELD && !item.field.has_explicit_type && !is_type_member_start(&p)) {
            class_item_drop(&item);
            parsed_member = false;
            parse_fail(&member_p);
        } else {
            parse_success(&member_p);
        }
        p.shared->no_mapped_member = outer_no_mapped_member;
        if (parsed_member) {
            // A property or accessor is reported at its name, anything else in full (up to its `;`, below)
            Span error_span = span_null;
            if (item.tag == CLASS_ITEM_FIELD) error_span = item.field.name_span;
            else if (item.tag == CLASS_ITEM_METHOD && item.method.accessor) error_span = item.method.name_span;
            else if (item.tag == CLASS_ITEM_INTERFACE_MEMBER && item.interface_memeber.key.tag == INTERFACE_KEY_MAPPED) {
                error_span = item.interface_memeber.key.span;
                first_mapped = true;
            } else if (item.tag == CLASS_ITEM_INTERFACE_MEMBER && interface_member_is_misplaced_mapped(&item.interface_memeber)) {
                error_span = item.interface_memeber.key.span;
                misplaced_mapped = true;
            }
            if (member_count < 2) {
                member_whole[member_count] = !span_valid(error_span);
                member_error_spans[member_count] = span_valid(error_span) ? error_span : span_make(p.shared->source, member_start, p.pos);
            }
            member_count++;
            // An accessor with a body ends there: no separator follows (tsc's parseFunctionBlockOrSemicolon; a `;` or
            // `,` after it starts no member, TS1131)
            accessor_body = item.tag == CLASS_ITEM_METHOD && item.method.accessor && item.method.func.has_body;
            switch (item.tag) {
                case CLASS_ITEM_METHOD: {
                    if (item.method.accessor) {
                        // `get x(): T` / `set x(v: T)` in a type: a member of its own, typed by the getter's return or
                        // the setter's parameter type (a getter and a setter of one symbol are one property, members.c)
                        bool is_getter = item.method.accessor == CLASS_ACCESSOR_GET;
                        TypeNode type = type_any;
                        bool annotated = false;
                        Span setter_param = span_null, setter_pattern = span_null;
                        // A setter's pattern parameter keeps its annotation, which the member borrows (see
                        // InterfaceAccessor.setter_function)
                        bool keep_setter = false;
                        if (is_getter) {
                            annotated = item.method.func.ret_explicit || item.method.func.has_body;
                            type = type_move(&item.method.func.ret);
                        } else if (vec_function_arg_size(&item.method.func.args)) {
                            FunctionArg* arg = &item.method.func.args.data[0];
                            annotated = arg->initializer.binding.has_explicit_type;
                            if (arg->initializer.binding.tag == BINDING_IDENTIFIER) setter_param = arg->initializer.binding.content_span;
                            else setter_pattern = arg->initializer.binding.content_span;
                            keep_setter = arg->initializer.binding.tag == BINDING_OBJECT || arg->initializer.binding.tag == BINDING_ARRAY;
                            if (keep_setter) {
                                if (annotated) type = *binding_type(&arg->initializer.binding);
                            } else {
                                type = binding_take_type(&arg->initializer.binding);
                            }
                        }
                        InterfaceMember member = interface_member_make(
                            item.span,
                            nullptr,
                            false,
                            false,
                            cstr_move(&item.method.name_string),
                            item.method.name_span,
                            item.method.name_is_value,
                            type
                        );
                        member.accessor_kind = is_getter ? 1 : 2;
                        InterfaceAccessor* sides = interface_member_accessor(&member);
                        sides->annotated = annotated;
                        sides->type_borrowed = keep_setter && annotated;
                        sides->setter_param = setter_param;
                        sides->setter_pattern = setter_pattern;
                        if (keep_setter) {
                            FunctionImpl* setter = ast_new(FunctionImpl, item.method.func);
                            function_impl_set_member_name(setter, item.method.name_span);
                            setter->accessor = (uint8_t)item.method.accessor;
                            sides->setter_function = ast_new(Expr, expr_function(setter->full_span, setter));
                            item.method.func = (FunctionImpl){0};
                        }
                        if (is_getter && item.method.func.has_body) {
                            // (Kept, TS1183: tsc binds and checks the body, and infers the getter's type from it)
                            FunctionImpl* body = ast_new(FunctionImpl, item.method.func);
                            bool explicit_ret = body->ret_explicit;
                            body->ret_explicit = false;
                            sides->getter_body = ast_new(Expr, expr_function(body->full_span, body));
                            sides->getter_body_infers = !explicit_ret;
                            item.method.func = (FunctionImpl){0};
                        }
                        list_push(vec_interface_member, &members, member);
                        break;
                    }
                    // (What a method signature is reported at, like a class method: its name)
                    function_impl_set_member_name(&item.method.func, item.method.name_span);
                    InterfaceMember member = interface_member_make(
                        item.span,
                        nullptr,
                        false,
                        item.method.is_optional,
                        cstr_move(&item.method.name_string),
                        item.method.name_span,
                        item.method.name_is_value,
                        function_impl_into_type(item.method.func)
                    );
                    check(member.key.tag == INTERFACE_KEY_NAME);
                    member.key.name_is_member_function = true;
                    list_push(vec_interface_member, &members, member);
                    item.method.func = (FunctionImpl){0};
                }   break;

                case CLASS_ITEM_FIELD:
                    list_push(vec_interface_member, &members, interface_member_make(
                        item.span,
                        nullptr,
                        item.field.is_readonly,
                        item.field.is_optional,
                        cstr_move(&item.field.name_string),
                        item.field.name_span,
                        item.field.name_is_value,
                        type_move(&item.field.explicit_type)
                    ));
                    vec_interface_member_back_mut(&members)->implicit_any = !item.field.has_explicit_type;
                    if (item.field.value.tag != EXPR_IMPLICIT_ANY || item.field.value.missing) { // (tsc's checkGrammarProperty)
                        Span at = expr_outer_span(&item.field.value);
                        if (item.field.value.missing && span_valid(at)) {
                            // (A missing initializer, `{ q: T = }`, reported only in JSDoc, which has no parse errors that
                            // hide grammar errors: at its position, right after the `=`, no trivia skipped)
                            char const* start = span_start_ptr(at);
                            while (start > source_file_start(p.shared->source) && is_js_ascii_space(start[-1])) start--;
                            at = span_make(p.shared->source, start, start);
                        }
                        if (is_declaration) {
                            grammar_error(&p, at, 1246, "An interface property cannot have an initializer.");
                        } else {
                            grammar_error(&p, at, 1247, "A type literal property cannot have an initializer.");
                        }
                        interface_member_accessor(vec_interface_member_back_mut(&members))->initializer = ast_new(Expr, item.field.value);
                        item.field.value = expr_implicit_any(span_null);
                    }
                    break;

                case CLASS_ITEM_INTERFACE_MEMBER:
                    if (item.interface_memeber.key.tag == INTERFACE_KEY_EXPR && item.interface_memeber.accessor &&
                        item.interface_memeber.accessor->initializer && !item.interface_memeber.accessor_kind
                    ) {
                        Span at = expr_outer_span(item.interface_memeber.accessor->initializer);
                        if (is_declaration) grammar_error(&p, at, 1246, "An interface property cannot have an initializer.");
                        else grammar_error(&p, at, 1247, "A type literal property cannot have an initializer.");
                    }
                    list_push(vec_interface_member, &members, item.interface_memeber);
                    item.interface_memeber = (InterfaceMember){0};
                    break;

                default:
                    parse_error(&p, span_from_pos(&p), TS_CODE_NONE, "Invalid class item for interface.");
                    break;
            }
            class_item_drop(&item);
        } else {
            if (list_abort_or_skip(&p, PARSING_TYPE_MEMBERS)) break;
            continue;
        }
        hop_whitespace(&p);

        if (accessor_body) {
            accessor_body = false;
            continue;
        }
        // Typescript accepts EITHER , ; or newlines in interfaces. You can MIX them! Not after a mapped type's
        // `[K in T]: X`, which ends at a `;`, a `}` or a newline (tsc's parseMappedType: parseSemicolon, then a `,`
        // starts no member, TS1131: `[K in T]: X\n,` is that alone)
        bool separated = !(first_mapped && member_count == 1) && hop_char(&p, ',');
        // (The end of the file ends a member too, tsc's canParseSemicolon: the list's `}` is what's missing there)
        if (!separated &&
            *p.pos != '}' && !at_eof(&p, p.pos) &&
            p.whitespace != WHITESPACE_NEWLINE
        ) {
            separated = expect_char(&p, ';');
        } else if (!separated) {
            separated = hop_char(&p, ';');
        }
        // (A member reported in full ends after its separator)
        if (separated && member_count && member_count <= 2 && member_whole[member_count - 1]) {
            member_error_spans[member_count - 1] = span_make(p.shared->source, member_start, p.pos);
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);

    expect_char(&p, '}');
    shrink_list(vec_interface_member, &members);
    interface_members_bind(&members);

    // tsc's checkGrammarMappedType / checkGrammarProperty: a type literal is a mapped type only if it starts like one,
    // and then may have no other members; anywhere else `[K in T]` is a property named by an `in` expression
    if (first_mapped && member_count > 1) {
        grammar_error(&p, member_error_spans[1], 7061, "A mapped type may not declare properties or methods.");
    } else if (misplaced_mapped) {
        grammar_error(&p, member_error_spans[0], 7061, "A mapped type may not declare properties or methods.");
    }

    *ret = (Interface) {
        .members = members,
        .parsed = true,
        .type_literal = !is_declaration,
        .span = span_from(a),
    };
    return parse_success(&p);
}

// after_declare: `declare type` commits to a type alias, a line break after `type` is TS1142 (tsc's isDeclaration)
static bool parse_type_decl(Parser* parent, bool after_declare, TypeDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // (After decorators tsc's parseTypeAliasDeclaration takes whatever follows: TS1142, a missing name)
    bool decorated = take_decl_committed(&p);
    if (!hop_start_if_ends_on_word_break(&p, "type")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        if (!after_declare && !decorated) return parse_fail(&p);
        parse_error(&p, span_from_token(&p), 1142, "Line break not permitted here.");
    }

    // (tsc's nextTokenIsIdentifierOnSameLine: `type void = I` is the expression statement `type`, then `void = I`)
    Span name;
    // (Committed, by decorators or `declare type`: whatever follows, a missing name)
    Tok name_t = tok_peek(&p);
    if ((decorated || after_declare) &&
        (!tok_is_identifier(name_t) || is_reserved_word((csview){ name_t.start, name_t.end - name_t.start }))
    ) {
        expect_non_reserved_identifier(&p, &name);
    } else if (!parse_identifier(&p, &name) || is_reserved_word(span_sv(name))) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, TYPE_PARAM_OF_TYPE, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    // (tsc's parseTypeAliasDeclaration: parseExpected('='), then a type whatever, but `intrinsic` not before a `.` is
    // the keyword alone: `type T = intrinsic["x"]` is TS1005 ';' expected at the `[`, and TS2795)
    TypeNode type;
    expect_char(&p, '=');
    hop_whitespace(&p);
    Parser intrinsic_p = parser(&p);
    Anchor intrinsic_a = anchor(&intrinsic_p);
    bool intrinsic = false;
    if (*p.pos == 'i' && hop_start_if_ends_on_word_break(&intrinsic_p, "intrinsic")) {
        const char* end = intrinsic_p.pos;
        hop_whitespace(&intrinsic_p);
        intrinsic = *intrinsic_p.pos != '.';
        intrinsic_p.pos = end;
    }
    if (intrinsic) {
        type = type_identifier(ast_new(IdentifierType, { .name = span_from(intrinsic_a) }));
        type.parsed = true;
        if (intrinsic_p.shared->text) type.pos = type_pos(&intrinsic_p, intrinsic_a.start);
        parse_success(&intrinsic_p);
    } else {
        parse_fail(&intrinsic_p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
    }

    *ret = (TypeDecl) {
        .name = name,
        .full_span = span_from(a),
        .generic = generic,
        .type = type,
    };
    return parse_success(&p);
}

// The heritage clauses of a class or interface, as tsc parses them: any number of `extends` and `implements` clauses
// in any order, each a list of expressions with type arguments. What tsc's checker doesn't look at (a second clause,
// a class's second base) is kept only for the baselines; checkGrammarClassDeclarationHeritageClauses and
// checkGrammarInterfaceDeclaration's complaints about it are grammar errors reported here
typedef struct HeritageClauses {
    bool is_class;
    Expr base; // A class's: the first element of its first `extends` clause
    Span base_clause; // `extends A<T>`
    VecExpr unchecked_exprs; // A class's other `extends` elements
    // Entity names (`A.B<T>`) of the checked clause: a class's first `implements` clause, an interface's first `extends`
    VecSpannedType types;
    VecTypeNode unchecked_types; // Entity names of the other clauses
    // Other expressions (`A?.B`, `f()`), which the checker rejects (TS2499/TS2500) in the checked clause, and only
    // resolves in the others
    VecExpr invalid;
    VecExpr unchecked_invalid;
    // tsc's checkGrammarClassDeclarationHeritageClauses/checkGrammarInterfaceDeclaration: none of their checks when
    // the declaration's modifiers fail (modifiers_failed); `stopped` when they end at a complaint about the clauses
    // (TS1172...), which ends checkGrammarClassLikeDeclaration (no TS1098 for the type parameters then)
    bool modifiers_failed;
    bool stopped;
    // The base's own type argument list's grammar errors (`extends A<B,>`, `extends A<>`), which only
    // checkGrammarHeritageClause reports, in its turn
    Span base_args_comma;
    Span base_args_empty;
} HeritageClauses;

// An `extends` element of a class or an interface
static bool heritage_is_entity_name(Expr const* e) {
    if (e->tag == EXPR_GENERIC_PARAMS) e = &e->generic_params->expr;
    while (e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT && !e->property->optional_chain) {
        e = &e->property->expr;
    }
    return e->tag == EXPR_IDENTIFIER && !e->identifier.keyword;
}

// `A?.B`, `A.B?.C<T>`: an entity name but for an optional chain's `?.`, which tsc's isEntityNameExpression ignores: its
// checker reports it (TS2499/TS2500) and resolves it as the type reference it would be all the same (getTypeReferenceName,
// getInterfaceBaseTypeNodes)
static bool heritage_is_optional_chain_name(Expr const* e) {
    if (e->tag == EXPR_GENERIC_PARAMS) e = &e->generic_params->expr;
    bool optional = false;
    while (e->tag == EXPR_PROPERTY && e->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(e) &&
        span_sv(property_name(e->property)).size && *span_sv(property_name(e->property)).buf != '#') {
        optional |= e->property->optional_chain;
        e = &e->property->expr;
    }
    return optional && e->tag == EXPR_IDENTIFIER && !e->identifier.keyword && !expr_is_perenthisized(e);
}

// The type reference an optional chain name (heritage_is_optional_chain_name()) means, flagged for TS2499/TS2500.
// Consumes `e`
static TypeNode heritage_optional_chain_type(Expr* e) {
    IdentifierType identifier = {0};
    Expr* name = e;
    if (e->tag == EXPR_GENERIC_PARAMS) {
        identifier.generic_params = e->generic_params->params;
        e->generic_params->params = (VecSpannedType){0};
        name = &e->generic_params->expr;
    }
    AstSpans reversed = {0};
    for (Expr const* n = name; n->tag == EXPR_PROPERTY; n = &n->property->expr) ast_spans_push(&reversed, property_name(n->property));
    Span first = name->inner_span;
    for (Expr const* n = name; n->tag == EXPR_PROPERTY; n = &n->property->expr) first = n->property->expr.inner_span;
    AstSpans namespaces = {0};
    ast_spans_push(&namespaces, first);
    for (isize i = ast_spans_size(&reversed) - 1; i > 0; i--) ast_spans_push(&namespaces, reversed.data[i]);
    identifier.name = reversed.data[0];
    ast_spans_drop(&reversed);
    identifier.heritage_optional_chain = true;
    IdentifierTypeRare* rare = identifier_type_rare(&identifier);
    rare->namespaces = namespaces;
    expr_drop(e);
    return type_identifier(ast_new(IdentifierType, identifier));
}

// tsc's parseExpressionWithTypeArguments. Where a type is meant (`implements`, an interface's `extends`) an entity
// name is parsed again as the type reference it means
// tsc's parseExpressionWithTypeArguments: a left-hand side expression and type arguments (in JavaScript too, where
// the expression parser leaves `<` alone)
static void parse_heritage_expr(Parser* p, Expr* ret) {
    ExprCtx ctx = expr_ctx_null;
    ctx.reject_operators = true;
    ctx.left_hand_side = true;
    Anchor a = anchor(p);
    expect_expr(p, ctx, ret);
    if (ret->tag == EXPR_GENERIC_PARAMS || !p->shared->source->is_js) return;
    Parser gp = parser(p);
    hop_whitespace(&gp);
    VecSpannedType params;
    if (*gp.pos == '<' && parse_generic_params(&gp, false, &params)) {
        parse_success(&gp);
        if (!vec_spanned_type_is_empty(&params)) {
            Span args = span_between(vec_spanned_type_front(&params)->span, vec_spanned_type_back(&params)->span);
            js_syntax_error(p, args, 8011, "Type arguments can only be used in TypeScript files.");
        }
        *ret = expr_generic_params(span_from(a), ast_new(GenericParamsExpr, { .expr = *ret, .params = params }));
    } else {
        parse_fail(&gp);
    }
}

// An element of a heritage clause. Where a type is meant (`implements`, an interface's `extends`) an entity name is
// parsed again as the type reference it means
// The grammar errors of an element tsc's checker never visits (a class's `extends` elements past the base, the
// elements of later clauses, an implemented or extended expression that isn't a name, TS2500/TS2499) are dropped
static void parse_heritage_element(Parser* p, HeritageClauses* h, bool is_extends, bool checked, Span keyword, u32 index) {
    intptr_t mark = error_mark(p);
    if (h->is_class && is_extends) {
        Expr e;
        parse_heritage_expr(p, &e);
        if (checked && index == 0) {
            if (e.tag == EXPR_GENERIC_PARAMS) {
                // The base's own list: its grammar errors are checkGrammarHeritageClause's (parse_heritage_clauses())
                Parser q = parser(p);
                q.pos = span_end_ptr(expr_outer_span(&e.generic_params->expr));
                hop_whitespace(&q);
                const char* open = q.pos;
                parse_fail(&q);
                const char* c = span_end_ptr(expr_outer_span(&e)) - 1;
                while (c > open && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
                if (c > open && c[-1] == ',') h->base_args_comma = grammar_drop_since(p, mark, 1009, c - 1);
                h->base_args_empty = grammar_drop_since(p, mark, 1099, open);
            }
            h->base = e;
            h->base_clause = span_make(p->shared->source, span_start_ptr(keyword), p->pos);
        } else {
            grammar_drop_since(p, mark, 0, nullptr);
            vec_expr_push(&h->unchecked_exprs, e);
        }
        return;
    }
    Parser ep = parser(p);
    const char* start = ep.pos;
    Expr e;
    parse_heritage_expr(&ep, &e);
    if (heritage_is_optional_chain_name(&e)) {
        Span span = span_make(p->shared->source, start, ep.pos);
        parse_success(&ep);
        TypeNode t = heritage_optional_chain_type(&e);
        if (checked) {
            list_push(vec_spanned_type, &h->types, spanned_type(span, t));
        } else {
            grammar_drop_since(p, mark, 0, nullptr);
            vec_type_push(&h->unchecked_types, t);
        }
        return;
    }
    if (heritage_is_entity_name(&e)) {
        const char* end = ep.pos;
        parse_fail(&ep);
        Parser tp = parser(p);
        TypeNode t;
        tp.shared->heritage_type_args = true;
        bool typed = parse_type(&tp, TYPE_PRECEDENCE_OPERATOR, &t);
        tp.shared->heritage_type_args = false;
        if (typed) {
            if (tp.pos == end) {
                Span span = span_make(p->shared->source, start, end);
                parse_success(&tp);
                expr_drop(&e);
                if (checked) {
                    list_push(vec_spanned_type, &h->types, spanned_type(span, t));
                } else {
                    grammar_drop_since(p, mark, 0, nullptr);
                    vec_type_push(&h->unchecked_types, t);
                }
                return;
            }
            type_drop(&t);
        }
        parse_fail(&tp);
        expr_drop(&e);
        parse_heritage_expr(p, &e);
    } else {
        parse_success(&ep);
    }
    grammar_drop_since(p, mark, 0, nullptr);
    vec_expr_push(checked ? &h->invalid : &h->unchecked_invalid, e);
}

// The expressions that aren't entity names: the checked clause's first (checked_ret of them), then the others'
static void heritage_take_invalid(HeritageClauses* h, VecExpr* ret, u32* checked_ret) {
    *checked_ret = (u32)vec_expr_size(&h->invalid);
    *ret = h->invalid;
    for_each (i, vec_expr, &h->unchecked_invalid) vec_expr_push(ret, *i);
    h->unchecked_invalid.size = 0; // (The expressions were moved)
    vec_expr_drop(&h->unchecked_invalid);
}

// tsc's parseHeritageClauses (parseList(HeritageClauses, parseHeritageClause))
static void parse_heritage_clauses(Parser* p, HeritageClauses* h) {
    hop_whitespace(p);
    if (*p->pos != 'e' && *p->pos != 'i') return;
    Tok first = tok_peek(p);
    if (!tok_is(first, "extends") && !tok_is(first, "implements")) return;
    bool grammar_done = h->modifiers_failed;
    bool seen_extends = false;
    bool seen_implements = false;
    bool first_extends = true;
    bool first_implements = true;
    u32 saved_clauses = list_enter(p, PARSING_HERITAGE_CLAUSES);
    while (true) {
        hop_whitespace(p);
        Tok t = tok_peek(p);
        if (list_is_terminator(PARSING_HERITAGE_CLAUSES, t)) break;
        bool is_extends = tok_is(t, "extends");
        if (!is_extends && !tok_is(t, "implements")) {
            if (list_abort_or_skip(p, PARSING_HERITAGE_CLAUSES)) break;
            continue;
        }
        Span keyword = span_make(p->shared->source, t.start, t.end);
        p->pos = t.end;
        // The clause the checker looks at: the first `extends` clause, a class's first `implements` clause
        bool checked = is_extends ? first_extends : (h->is_class && first_implements);
        if (is_extends) first_extends = false;
        else first_implements = false;

        // tsc's parseDelimitedList(HeritageClauseElement, parseExpressionWithTypeArguments)
        u32 count = 0;
        Span trailing_comma = span_null;
        Span second_element = span_null;
        // The first element `import<T>` (TS1326, checkGrammarExpressionWithTypeArguments in checkGrammarHeritageClause)
        Span import_element = span_null;
        intptr_t clause_mark = error_mark(p);
        const char* clause_end = span_end_ptr(keyword);
        u32 saved_elements = list_enter(p, PARSING_HERITAGE_CLAUSE_ELEMENT);
        while (true) {
            hop_whitespace(p);
            Tok e = tok_peek(p);
            if (tok_is_heritage_clause_element(p, e, false)) {
                const char* start = p->pos;
                if (count == 1) second_element = span_make(p->shared->source, e.start, e.end);
                parse_heritage_element(p, h, is_extends, checked, keyword, count);
                clause_end = p->pos;
                if (!span_valid(import_element) && tok_is(e, "import")) {
                    const char* after = e.end;
                    while (after < p->pos && is_js_ascii_space(*after)) after++;
                    if (after < p->pos && *after == '<') import_element = span_make(p->shared->source, e.start, p->pos);
                }
                count++;
                trailing_comma = span_null;
                hop_whitespace(p);
                if (hop_char(p, ',')) {
                    trailing_comma = span_from_previous_char(p);
                    continue;
                }
                if (list_is_terminator(PARSING_HERITAGE_CLAUSE_ELEMENT, tok_peek(p))) break;
                expect_char(p, ',');
                if (p->pos == start) hop_token(p);
                continue;
            }
            if (list_is_terminator(PARSING_HERITAGE_CLAUSE_ELEMENT, e)) break;
            if (list_abort_or_skip(p, PARSING_HERITAGE_CLAUSE_ELEMENT)) break;
        }
        list_leave(p, saved_elements);
        if (h->is_class && !is_extends && reports_js_syntax(p)) {
            js_syntax_skip_since(p, clause_mark);
            js_syntax_error(p, span_make(p->shared->source, span_start_ptr(keyword), clause_end), 8005,
                "'implements' clauses can only be used in TypeScript files.");
        }

        // checkGrammarClassDeclarationHeritageClauses, checkGrammarInterfaceDeclaration: the first complaint about
        // the clauses ends the check, then checkGrammarHeritageClause for each clause until then
        if (grammar_done) continue;
        if (is_extends) {
            if (seen_extends) {
                grammar_error(p, keyword, 1172, "'extends' clause already seen.");
                grammar_done = true;
            } else if (seen_implements) {
                grammar_error(p, keyword, 1173, "'extends' clause must precede 'implements' clause.");
                grammar_done = true;
            } else if (h->is_class && count > 1) {
                grammar_error(p, second_element, 1174, "Classes can only extend a single class.");
                grammar_done = true;
            }
            seen_extends = true;
        } else if (!h->is_class) {
            grammar_error(p, keyword, 1176, "Interface declaration cannot have 'implements' clause.");
            grammar_done = true;
        } else if (seen_implements) {
            grammar_error(p, keyword, 1175, "'implements' clause already seen.");
            grammar_done = true;
        } else {
            seen_implements = true;
        }
        if (grammar_done) {
            h->stopped = !h->modifiers_failed;
            continue;
        }
        if (span_valid(trailing_comma)) {
            grammar_error(p, trailing_comma, 1009, "Trailing comma not allowed.");
        } else if (count == 0) {
            Span at = span_make(p->shared->source, span_end_ptr(keyword), span_end_ptr(keyword));
            grammar_error(p, at, 1097, "'", keyword, "' list cannot be empty.");
        } else if (span_valid(import_element)) {
            grammar_error(p, import_element, 1326, "This use of 'import' is invalid. 'import()' calls can be written, but "
                "they must have parentheses and cannot have type arguments.");
        } else if (h->is_class && is_extends && span_valid(h->base_args_comma)) {
            grammar_error(p, h->base_args_comma, 1009, "Trailing comma not allowed.");
        } else if (h->is_class && is_extends && span_valid(h->base_args_empty)) {
            grammar_error(p, h->base_args_empty, 1099, "Type argument list cannot be empty.");
        }
    }
    list_leave(p, saved_clauses);
}

// committed: after `export default` (tsc's isStartOfDeclaration takes `export default` as a declaration whatever
// follows, its parseDeclarationWorker the interface): a missing name is TS1003
static bool parse_interface_decl(Parser* parent, bool committed, InterfaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool decorated = take_decl_committed(&p);
    bool modifiers_failed = take_decl_modifiers_failed(&p);
    if (!hop_start_if_ends_on_word_break(&p, "interface")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    // tsc's isStartOfDeclaration: `interface` followed by a name on the same line, else `interface` is an expression
    // (`interface { }`: TS1438 from parseErrorForMissingSemicolonAfter, `interface & {}` an expression). After
    // decorators parseInterfaceDeclaration takes any name, reserved words TS1359
    bool named = (decorated || p.whitespace != WHITESPACE_NEWLINE) && tok_is_identifier(tok_peek(&p));
    if (!named && !committed && !decorated) {
        return parse_fail(&p);
    }

    u32 this_count = p.shared->this_count;
    Span name;
    if (!named && decorated) {
        expect_non_reserved_identifier(&p, &name);
    } else if (!named) {
        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
        // (tsc's missing identifier, at the token's full start: the binder declares it, `(Missing)`)
        name = span_make(p.shared->source, token_full_start(&p), token_full_start(&p));
    } else if (!parse_identifier(&p, &name)) {
        parse_error(&p, span_from_pos(&p), 1438, "Interface must be given a name.");
        name = span_null;
    }
    hop_whitespace(&p);

    Generic generic;
    if (!parse_generic_decl(&p, TYPE_PARAM_OF_TYPE, &generic)) {
        generic = (Generic){0};
    }
    hop_whitespace(&p);

    HeritageClauses heritage = { .is_class = false, .modifiers_failed = modifiers_failed };
    parse_heritage_clauses(&p, &heritage);
    hop_whitespace(&p);
    Interface interface;
    if (!parse_interface(&p, true, &interface)) {
        interface = (Interface){0};
        expected_token_error(&p, "{");
    }

    *ret = (InterfaceDecl) {
        .name = name,
        .full_span = span_from(a),
        .generic = generic,
        .extends = heritage.types,
        .unchecked_extends = heritage.unchecked_types,
        .type = type_interface(interface),
        .contains_this = p.shared->this_count != this_count,
    };
    heritage_take_invalid(&heritage, &ret->invalid_extends, &ret->invalid_extends_checked);
    return parse_success(&p);
}

static bool parse_enum_decl(Parser* parent, EnumDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool is_const = false;
    if (hop_start_if_ends_on_word_break(&p, "const")) {
        is_const = true;
        hop_whitespace(&p);
    }
    if (!hop_start_if_ends_on_word_break(&p, "enum")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span name;
    Span word;
    Parser probe = parser(&p);
    bool is_word = parse_identifier_or_keyword(&probe, &word);
    bool reserved = is_word && is_reserved_word(span_sv(word));
    // (`await` in an async function or static block, `yield` in a generator: no identifier there, tsc's isIdentifier,
    // and no reserved word either, so TS1003)
    bool contextual = is_word && !reserved &&
        ((span_equals(word, "await") && (p.shared->in_async || p.shared->in_static_block)) ||
            (span_equals(word, "yield") && p.shared->in_generator));
    parse_fail(&probe);
    if (reserved) {
        // tsc's parseIdentifier: TS1359, and a missing name (the word stays for what follows to trip over)
        parse_error(&p, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
        name = span_make(p.shared->source, token_full_start(&p), token_full_start(&p)); // (tsc's missing node)
    } else if (contextual) {
        parse_error(&p, word, 1003, "Identifier expected.");
        name = span_make(p.shared->source, token_full_start(&p), token_full_start(&p)); // (tsc's missing node)
    } else {
        expect_identifier(&p, &name);
    }
    hop_whitespace(&p);

    // (Without its `{` the enum has no members: tsc's createMissingList)
    const char* before_body = p.pos;
    bool has_body = expect_char(&p, '{');
    hop_whitespace(&p);

    VecEnumCase cases = vec_enum_case_init();
    u32 saved_context = list_enter(&p, PARSING_ENUM_MEMBERS);
    // (tsc's parseEnumDeclaration: the members are outside the yield and await contexts)
    bool outer_async = p.shared->in_async, outer_generator = p.shared->in_generator, outer_static_block = p.shared->in_static_block;
    p.shared->in_async = p.shared->in_generator = p.shared->in_static_block = false;
    if (!has_body) p.pos = before_body;
    while (has_body && *p.pos && *p.pos != '}') {
        Tok t = tok_peek(&p);
        if (!list_is_element(&p, PARSING_ENUM_MEMBERS, t)) {
            if (list_abort_or_skip(&p, PARSING_ENUM_MEMBERS)) break;
            continue;
        }
        EnumCase enum_case = {0};
        cstr string_name;
        Anchor name_anchor = anchor(&p);
        if ((*p.pos == '"' || *p.pos == '\'') && parse_string(&p, &string_name)) {
            enum_case.full_name = span_from(name_anchor);
            enum_case.name = span_make(
                span_source(enum_case.full_name),
                span_start_ptr(enum_case.full_name) + 1,
                span_end_ptr(enum_case.full_name) - 1
            );
            enum_case.name_is_value = true;
            // (The symbol's name is the cooked value, where an escape makes it differ)
            if (!csview_equals_sv(cstr_sv(&string_name), span_sv(enum_case.name))) enum_case.canonical_name = string_name;
            else cstr_drop(&string_name);
        } else if (parse_identifier_or_keyword(&p, &enum_case.name)) {
            enum_case.full_name = enum_case.name;
            // `\u0041` names the member `A`
            if (memchr(enum_case.name.buf, '\\', (size_t)enum_case.name.size)) {
                enum_case.canonical_name = identifier_name_value(span_sv(enum_case.name));
            }
        } else if (isdigit((unsigned char)*p.pos) || (*p.pos == '.' && isdigit((unsigned char)p.pos[1]))) {
            // A numeric name (tsc's parsePropertyName): TS2452 from the checker
            Value value;
            if (parse_numeric_value(&p, &value)) {
                // (A bigint name too: tsc 6.0's computeEnumMemberValue)
                enum_case.numeric_name = (value.tag == VALUE_NUMBER && isfinite(value.number)) || value.tag == VALUE_BIGINT ? 1 : 2;
                // (Named by the number's canonical text, `1.0` is "1"; a bigint has no property name, tsc's binder
                // names it InternalSymbolName.Missing)
                if (value.tag == VALUE_BIGINT) enum_case.canonical_name = cstr_lit("__missing");
                else if (value.tag == VALUE_NUMBER) cstr_append_value_js(&enum_case.canonical_name, &value);
                value_drop(&value);
            }
            enum_case.name = enum_case.full_name = span_from(name_anchor);
            if (cstr_size(&enum_case.canonical_name) && csview_equals_sv(cstr_sv(&enum_case.canonical_name), span_sv(enum_case.name))) {
                cstr_clear(&enum_case.canonical_name);
            }
        } else if (*p.pos == '[') {
            // A computed name (tsc's parsePropertyName): `['a']` names the member `a`, anything else is TS1164
            p.pos++;
            hop_whitespace(&p);
            Anchor key_anchor = anchor(&p);
            Expr key;
            bool literal = false;
            Expr* kept_key = nullptr;
            bool saved_in = allow_in_enter(&p);
            bool has_key = expect_expr(&p, expr_ctx_null, &key);
            allow_in_leave(&p, saved_in);
            if (has_key) {
                if (key.tag == EXPR_LITERAL && (key.literal.tag == VALUE_STRING || key.literal.tag == VALUE_NUMBER)) literal = true;
                if (literal) cstr_append_value_js(&enum_case.canonical_name, &key.literal);
                if (literal) expr_drop(&key);
                else kept_key = ast_new(Expr, key);
            }
            Span key_span = span_from(key_anchor);
            hop_whitespace(&p);
            expect_char(&p, ']');
            enum_case.full_name = span_from(name_anchor);
            if (literal && strchr("\"'`", *span_start_ptr(key_span))) {
                enum_case.name = span_make_in(key_span, span_start_ptr(key_span) + 1, span_end_ptr(key_span) - 1);
                enum_case.name_is_value = true;
            } else if (literal) {
                enum_case.name = key_span;
            } else {
                cstr_clear(&enum_case.canonical_name);
                enum_case.name = enum_case.full_name;
                enum_case.computed_name = true;
                enum_case.computed_key = kept_key;
            }
            if (cstr_size(&enum_case.canonical_name) && csview_equals_sv(cstr_sv(&enum_case.canonical_name), span_sv(enum_case.name))) {
                cstr_clear(&enum_case.canonical_name);
            }
        } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
            // A private name (tsc's parsePropertyName): TS18024 from the checker
            p.pos++;
            parse_identifier_or_keyword(&p, nullptr);
            enum_case.name = enum_case.full_name = span_from(name_anchor);
        } else {
            expect_identifier(&p, &enum_case.name);
            enum_case.full_name = enum_case.name;
            hop_token(&p);
        }
        hop_whitespace(&p);
        if (hop_char(&p, '=')) {
            hop_whitespace(&p);
            bool saved_in = allow_in_enter(&p);
            expect_expr(&p, expr_ctx_null, &enum_case.value);
            allow_in_leave(&p, saved_in);
            hop_whitespace(&p);
        } else {
            enum_case.value = expr_implicit_any_no_span;
        }
        vec_enum_case_push(&cases, enum_case);
        // tsc's parseDelimitedList: a missing comma is reported (getExpectedCommaDiagnostic), and the list goes on
        if (!hop_char(&p, ',') && *p.pos && *p.pos != '}') {
            parse_error(&p, span_from_token(&p), 1357, "An enum member name must be followed by a ',', '=', or '}'.");
            if (p.pos == t.start) hop_token(&p);
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    p.shared->in_async = outer_async;
    p.shared->in_generator = outer_generator;
    p.shared->in_static_block = outer_static_block;

    if (has_body) expect_char(&p, '}');

    *ret = (EnumDecl) {
        .name = name,
        .full_span = span_from(a),
        .is_const = is_const,
        .cases = cases,
    };
    return parse_success(&p);
}

// `x is T`, `this is T`, `asserts x`, `asserts x is T` as a return type (`FunctionImpl.predicate`)
static bool parse_type_predicate(Parser* parent, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a_start = anchor(&p);

    Span name;
    if (!parse_identifier_or_keyword(&p, &name)) {
        return parse_fail(&p);
    }
    hop_whitespace_full(&p, true, false);
    bool asserts = false;
    // (Spelled with an escape it is the keyword all the same, TS1260: tsc's scanner makes it AssertsKeyword, which
    // parseAssertsTypePredicate consumes)
    bool asserts_word = span_equals(name, "asserts");
    bool escaped_asserts = false;
    if (!asserts_word && memchr(name.buf, '\\', (size_t)name.size)) {
        cstr cooked = identifier_name_value(span_sv(name));
        escaped_asserts = asserts_word = strcmp(cstr_str(&cooked), "asserts") == 0;
        cstr_drop(&cooked);
    }
    if (asserts_word && !starts_with_followed_by_word_break(&p, "is")) {
        Span keyword = name;
        Anchor name_a = anchor(&p);
        if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
            // (tsc's lookahead takes a private name for an identifier name, tokenIsIdentifierOrKeyword, then
            // parseIdentifier says TS18016 and takes it as the name)
            p.pos++;
            parse_identifier_or_keyword(&p, nullptr);
            name = span_from(name_a);
            parse_error(&p, name, 18016, "Private identifiers are not allowed outside class bodies.");
        } else if (!parse_identifier_or_keyword(&p, &name)) {
            return parse_fail(&p);
        }
        if (escaped_asserts) parse_error(&p, keyword, 1260, "Keywords cannot contain escape characters.");
        asserts = true;
        // (An `is` may follow on the next line, tsc's parseOptional, but the line break stays a member separator when
        // none does: `(v: unknown): asserts v` then a property on the next line)
        Parser q = parser(&p);
        hop_whitespace_full(&q, true, false);
        if (starts_with_followed_by_word_break(&q, "is")) parse_success(&q);
        else parse_fail(&q);
    }
    TypePredicateNode predicate = { .name = name, .is_this = span_equals(name, "this"), .asserts = asserts, .param_index = -1 };
    // (tsc's `this is T` is a ThisType: the interface isn't thisless)
    if (predicate.is_this) p.shared->this_count++;
    if (hop_start_if_ends_on_word_break(&p, "is")) {
        hop_whitespace(&p);
        Anchor a = anchor(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &predicate.type);
        predicate.type_span = span_from(a);
        // (`arg is infer R ? ...`: the `infer` looked past the space for an `extends`)
        while (predicate.type_span.size && is_js_ascii_space(predicate.type_span.buf[predicate.type_span.size - 1])) {
            predicate.type_span.size--;
        }
        predicate.has_type = true;
    } else if (!asserts) {
        return parse_fail(&p);
    }

    ret->ret = asserts ? type_void : type_boolean;
    predicate.span = span_from(a_start);
    while (predicate.span.size && is_js_ascii_space(predicate.span.buf[predicate.span.size - 1])) {
        predicate.span.size--;
    }
    ret->predicate = ast_new(TypePredicateNode, predicate);
    return parse_success(&p);
}

static bool parse_function_args_inner(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    u32 outer_context,
    FunctionImpl* ret
);

// commit_on_paren: once the `(` is there this is a parameter list whatever follows (tsc has chosen a function by then):
// the list recovers from what isn't a parameter instead of failing
static bool parse_function_args(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    FunctionImpl* ret
) {
    bool outer = parent->shared->in_parameters;
    parent->shared->in_parameters = true;
    u32 saved_context = list_enter(parent, PARSING_PARAMETERS);
    bool success = parse_function_args_inner(
        parent, parse_abstract_and_new, parse_type_return_syntax, parse_ret_type, commit_on_paren, saved_context, ret
    );
    if (success) shrink_list(vec_function_arg, &ret->args);
    list_leave(parent, saved_context);
    parent->shared->in_parameters = outer;
    return success;
}

static bool parse_function_args_inner(
    Parser* parent,
    bool parse_abstract_and_new,
    bool parse_type_return_syntax,
    bool parse_ret_type,
    bool commit_on_paren,
    u32 outer_context,
    FunctionImpl* ret
) {
    // (In a type or an ambient context, the initializers in the parameters, a binding pattern's too, are never checked:
    // only TS2371, tsc's checkVariableLikeDeclaration, and no parameter type from them. Their grammar errors are
    // dropped)
    bool unchecked_initializers = parse_type_return_syntax || parent->shared->type_member_args || parent->shared->in_ambient;
    parent->shared->type_member_args = false;
    // Without `abstract`/`new` a parameter list starts at its `(` or type parameters (member names come here first)
    if (!parse_abstract_and_new && *parent->pos != '(' && *parent->pos != '<') return false;
    Parser p = parser(parent);
    bool is_definitely_function = false;
    bool unambiguous_function_type = false;
    // (Type parameters in a type commit a function type, missing parameters, `=>` and return type and all: `<>}` is
    // '(' expected, tsc's isStartOfFunctionTypeOrConstructorType)
    bool generic_commits = false;

    *ret = (FunctionImpl){0};

    if (parse_abstract_and_new) {
        if (hop_start_if_ends_on_word_break(&p, "abstract")) {
            ret->is_abstract_constructor = true;
            hop_whitespace(&p);
        }

        if (hop_start_if_ends_on_word_break(&p, "new")) {
            hop_whitespace(&p);
            ret->is_constructor = true;
            // (In a type `new` starts a constructor type whatever follows, tsc's isStartOfFunctionTypeOrConstructorType:
            // `new>` is missing its `(`)
            if (parse_type_return_syntax) is_definitely_function = true;
        } else if (ret->is_abstract_constructor) {
            return parse_fail(&p);
        }
    }

    // (Before the parameter list: tsc's parseTypeParameters isn't in the Parameters context parse_function_args() entered)
    u32 parsing_context = p.shared->parsing_context;
    p.shared->parsing_context = outer_context;
    bool generic = parse_generic_decl(&p, TYPE_PARAM_OF_FUNCTION, &ret->generic);
    p.shared->parsing_context = parsing_context;
    if (generic) {
        hop_whitespace(&p);
        // An arrow function's type parameters don't commit it (tsc's parseParenthesizedArrowFunctionExpression
        // without allowAmbiguity): `<T>(x => y)` is a type assertion of a parenthesized arrow function
        is_definitely_function = parse_abstract_and_new || parse_type_return_syntax || commit_on_paren;
        generic_commits = parse_type_return_syntax;
    } else if (parse_abstract_and_new && *p.pos == '<') {
        // A `<` in a type or a type member is a signature's (tsc's isStartOfFunctionTypeOrConstructorType,
        // isTypeMemberStart): `<-` is missing its type parameters, the list's recovery leaves the token
        ret->generic = (Generic){0};
        char const* open = p.pos;
        p.pos++;
        hop_whitespace(&p);
        // (The list is there, empty: tsc's checkGrammarTypeParameterList, over the `<` to the token after it, which
        // only a JSDoc type's parse errors don't drop)
        grammar_error(&p, span_make(p.shared->source, open, p.pos + 1), 1098, "Type parameter list cannot be empty.");
        parse_error(&p, span_from_token(&p), 1139, "Type parameter declaration expected.");
        expected_token_error(&p, ">");
        is_definitely_function = true;
        unambiguous_function_type = parse_type_return_syntax;
    } else {
        ret->generic = (Generic){0};
    }

    Anchor args_start = anchor(&p);

    if (hop_char(&p, '(')) {
        hop_whitespace(&p);
        is_definitely_function |= commit_on_paren;
        // (tsc's isUnambiguouslyStartOfFunctionType: `()` and `(...` in a type)
        if (parse_type_return_syntax && (*p.pos == ')' || (p.pos[0] == '.' && p.pos[1] == '.' && p.pos[2] == '.'))) {
            is_definitely_function = true;
            unambiguous_function_type = true;
        }

        // (A `]` ends the list too, its `)` then missing: tsc's isListTerminator for Parameters)
        while (!at_eof(&p, p.pos) && *p.pos != ')' && *p.pos != ']') {
            if (hop_char(&p, ',')) {
                parse_error(&p, span_from_previous_char(&p), 1138, "Parameter declaration expected.");
            } else {
                InitializedBinding initializer = {0};
                const char* before_param = p.pos;
                // (Where the trivia before it starts: lookaheads below hop trivia of their own)
                const char* param_full_start = token_full_start(&p);
                u32 errors_before_param = p.shared->errors_attempted;
                bool explicit_optional = false;
                bool rest = false, this_param = false;
                Span rest_span = span_null;
                Anchor arg_start = anchor(&p);
                VecExpr decorators = {0};
                ignore parse_decorators(&p, &decorators);
                // (tsc's skipParameterStart: a decorator starts no parameter of a function type that isn't one yet, `(`
                // then `@` is a parenthesized type; after `new` or `<` it is one)
                if (vec_expr_size(&decorators) && parse_type_return_syntax && !is_definitely_function &&
                    !ret->is_constructor && vec_function_arg_is_empty(&ret->args)
                ) {
                    vec_expr_drop(&decorators);
                    goto drop_and_fail;
                }
                if (vec_expr_size(&decorators) && reports_js_syntax(&p) && !compiler_options()->experimental_decorators) {
                    // (A JavaScript file the checker doesn't check still says this, tsgo's
                    // getAdditionalJSSyntacticDiagnostics: from the `@` to the end of the first decorator)
                    Span first = span_make(p.shared->source, arg_start.start, span_end_ptr(expr_outer_span(vec_expr_at(&decorators, 0))));
                    add_error_to_parser(&p, (Error){ .message = cstr_from("Decorators are not valid here."), .span = first,
                        .ts_code = 1206, .phase = ERROR_PHASE_PARSE, .js_syntax = true, .unchecked_js = true });
                }

                ModifierList modifier_list;
                parse_modifier_list_ex(&p, true, false, &modifier_list);
                // The end of the first decorator or modifier (tsc's parseParameterWorker reports it on a `this`)
                const char* first_modifier_end = vec_expr_size(&decorators) ?
                    span_end_ptr(expr_outer_span(vec_expr_at(&decorators, 0))) :
                    vec_spanned_modifier_size(&modifier_list.spanned) ?
                    span_end_ptr(vec_spanned_modifier_at(&modifier_list.spanned, 0)->span) : nullptr;
                bool is_readonly_property = modifier_list.includes[MODIFIER_READONLY];
                bool is_override_property = modifier_list.includes[MODIFIER_OVERRIDE];
                bool is_parameter_property = is_readonly_property || modifier_list.includes[MODIFIER_PUBLIC] ||
                    modifier_list.includes[MODIFIER_PROTECTED] || modifier_list.includes[MODIFIER_PRIVATE] ||
                    modifier_list.includes[MODIFIER_OVERRIDE];
                uint8_t access = modifier_list.includes[MODIFIER_PRIVATE] ? ACCESS_MODIFIER_PRIVATE :
                    modifier_list.includes[MODIFIER_PROTECTED] ? ACCESS_MODIFIER_PROTECTED : ACCESS_MODIFIER_PUBLIC;
                check_modifiers(&p, &modifier_list, (ModifiedNode){ .kind = MODIFIED_PARAMETER, .parent = MODIFIED_IN_OTHER });
                modifier_list_drop(&modifier_list);
                hop_whitespace(&p);

                if (hop_start(&p, "...")) {
                    hop_whitespace(&p);
                    rest = true;
                    rest_span = span_from(arg_start);
                }

                intptr_t binding_mark = error_mark(&p);
                if (parse_binding_content_or_private_name(&p, &initializer.binding, 18009,
                        "Private identifiers cannot be used as parameters.")) {
                } else if (!rest && hop_start_if_ends_on_word_break(&p, "this")) {
                    // (A `this` parameter is one before any `...`: tsc's parseParameterWorker. `...this` is a rest
                    // parameter missing its name, TS1359)
                    this_param = true;
                    initializer.binding.tag = BINDING_IDENTIFIER;
                    initializer.binding.content_span = span_make(p.shared->source, p.pos - 4, p.pos);
                    if (first_modifier_end) {
                        // (On the modifier's range, from the end of the token before it: whitespace included)
                        const char* from = arg_start.start;
                        const char* file_start = source_file_start(p.shared->source);
                        while (from > file_start && strchr(" \t\r\n", from[-1])) from--;
                        parse_error(&p, span_make(p.shared->source, from, first_modifier_end), 1433,
                            "Neither decorators nor modifiers may be applied to 'this' parameters.");
                    }
                } else if (!is_definitely_function && !vec_function_arg_is_empty(&ret->args) && p.pos == before_param &&
                    !tok_is_start_of_parameter(tok_peek(&p))
                ) {
                    // After a first parameter the list's recovery skips what is no parameter even in a speculative
                    // arrow function (tsc's parseDelimitedList only calls parseParameter, which can fail it, on a
                    // parameter's start): `({ @dec b }) => 1`'s `}`
                    vec_expr_drop(&decorators);
                    if (list_abort_or_skip(&p, PARSING_PARAMETERS)) break;
                    continue;
                } else if (!is_definitely_function) {
                    vec_expr_drop(&decorators);
                    goto drop_and_fail;
                } else if (tok_is_word(tok_peek(&p)) && *p.pos != '#' &&
                    is_reserved_word((csview){ p.pos, tok_peek(&p).end - p.pos }) && tok_is_start_of_parameter(tok_peek(&p))
                ) {
                    // tsc's isStartOfParameter takes a reserved word that is a modifier or starts a type (`default`,
                    // `null`): parseNameOfParameter reports it (TS1359), the name is missing, and a modifier word
                    // is skipped (a type's is left for the list to trip over)
                    Tok word = tok_peek(&p);
                    bool at_start = p.pos == before_param;
                    expect_non_reserved_identifier(&p, &initializer.binding.content_span);
                    // (The missing name is at the parameter's full start, TS2300 '(Missing)' there)
                    if (at_start) initializer.binding.content_span = span_make(p.shared->source, param_full_start, param_full_start);
                    initializer.binding.tag = BINDING_IDENTIFIER;
                    if (tok_is(word, "default") || tok_is(word, "export") || tok_is(word, "const") || tok_is(word, "in")) {
                        p.pos = word.end;
                    }
                } else if (rest && tok_is_word(tok_peek(&p)) && *p.pos != '#' &&
                    is_reserved_word((csview){ p.pos, tok_peek(&p).end - p.pos })
                ) {
                    // `...while`: the `...` committed to a parameter, whose name (parseBindingIdentifier) is TS1359
                    expect_non_reserved_identifier(&p, &initializer.binding.content_span);
                    initializer.binding.tag = BINDING_IDENTIFIER;
                } else if (p.pos == before_param && !tok_is_start_of_parameter(tok_peek(&p))) {
                    // Nothing of a parameter: the list's recovery (tsc's parseDelimitedList). What tsc's
                    // isStartOfParameter takes (a type's start, `function f(\`x\`)`) is a parameter missing its name
                    vec_expr_drop(&decorators);
                    if (list_abort_or_skip(&p, PARSING_PARAMETERS)) break;
                    continue;
                } else if (tok_is_word(tok_peek(&p)) && *p.pos != '#' && is_reserved_word((csview){ p.pos, tok_peek(&p).end - p.pos })) {
                    // After modifiers or decorators a reserved word is the name tsc's parseBindingIdentifier rejects:
                    // TS1359, the name missing (`declare function` where a parameter was expected)
                    expect_non_reserved_identifier(&p, &initializer.binding.content_span);
                    initializer.binding.tag = BINDING_IDENTIFIER;
                } else {
                    // `(...)`, `(@dec)`: tsc's parseParameter with a missing name
                    parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
                    initializer.binding = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_from_pos(&p) };
                }
                if (unchecked_initializers && initializer.binding.tag != BINDING_IDENTIFIER) grammar_drop_since(&p, binding_mark, 0, nullptr);
                hop_whitespace(&p);

                Span question_span = span_null;
                // (A `this` parameter has a type annotation and nothing else: tsc's parseParameterWorker)
                if (!this_param && hop_char(&p, '?')) {
                    explicit_optional = true;
                    question_span = span_from_previous_char(&p);
                    js_syntax_error(&p, question_span, 8009, "The '?' modifier can only be used in TypeScript files.");
                    hop_whitespace(&p);
                }

                parse_binding_type_or_set_to_default(&p, &initializer.binding);
                hop_whitespace(&p);
                if (p.shared->source->is_js && !initializer.binding.has_explicit_type) {
                    // `function f(/** @type {T} */ a)`
                    VecJsDocTag tags = {0};
                    if (jsdoc_before(&p, arg_start.start, &tags)) jsdoc_apply_type_to_binding(&tags, &initializer.binding);
                    vec_jsdoc_tag_drop(&tags);
                }

                intptr_t initializer_mark = error_mark(&p);
                initializer.value = this_param ? nullptr : parse_initialization_boxed(&p, expr_ctx_null);
                if (unchecked_initializers) grammar_drop_since(&p, initializer_mark, 0, nullptr);
                if (initializer.binding.tag == BINDING_IDENTIFIER && !rest && !this_param) mark_named_evaluation(initialized_binding_value(&initializer), false);
                hop_whitespace(&p);

                // tsc's skipParameterStart: only a name or pattern that parses without errors counts. In a type
                // (isUnambiguouslyStartOfFunctionType) a `,`, `?` or `=` after it commits too
                if (vec_function_arg_is_empty(&ret->args) && p.shared->errors_attempted == errors_before_param &&
                    (initializer.binding.has_explicit_type || (parse_type_return_syntax && !rest &&
                        (explicit_optional || initialized_binding_value(&initializer)->tag != EXPR_IMPLICIT_ANY || initialized_binding_value(&initializer)->missing ||
                            *p.pos == ',' ||
                            // (A `this` parameter takes neither, they are left for the list: `(this?: T) => R`)
                            (this_param && (*p.pos == '?' || *p.pos == '=')))))
                ) {
                    is_definitely_function = true;
                }

                // tsc's isUnambiguouslyStartOfFunctionType and isParenthesizedArrowFunctionExpression: a first
                // parameter that didn't commit must be followed by `,` or `)` (`(readonly T[]) => {` in a return type
                // is a parenthesized type; an expression's modifier commits, a type's doesn't)
                // (An arrow function whose first parameter is a pattern, `({` or `([`, is tsc's Tristate.Unknown: the whole
                // arrow is parsed speculatively and stands if `=>` follows, `({ @dec b }) => 1`; the caller checks that)
                bool pattern_first = !parse_type_return_syntax && (*before_param == '{' || *before_param == '[');
                // (Nor an invalid character, which no list takes: tsc's parseDelimitedList skips it and the arrow
                // stands, its errors all on the TS1127, `(x = €) => x`)
                // (Nor after an initializer, `(a =` being Unknown too: `(a = 1 public b) => 1`, and `(a = x in b) => 1` in a
                // for statement's initializer, where the `in` ends the initializer, are arrow functions with errors)
                if (vec_function_arg_is_empty(&ret->args) && !is_definitely_function && *p.pos != ',' && *p.pos != ')' &&
                    (parse_type_return_syntax || first_modifier_end == nullptr) && !pattern_first &&
                    (parse_type_return_syntax || !initializer.value) &&
                    (parse_type_return_syntax || !invalid_char_size(&p, p.pos))
                ) {
                    vec_expr_drop(&decorators);
                    initialized_binding_drop(&initializer);
                    goto drop_and_fail;
                }

                initializer.is_rest_parameter = rest;
                Span arg_span = span_from(arg_start);
                // (A parameter the list's recovery ends at its next token doesn't take the whitespace before it:
                // `(a {`)
                while (arg_span.size && strchr(" \t\r\n", arg_span.buf[arg_span.size - 1])) arg_span.size--;
                // (One of nothing, `(this = 1)`'s `1`, is a missing node: where the token before it ends, no trivia skipped,
                // TS7006 there)
                if (!arg_span.size) {
                    char const* at = arg_span.buf;
                    while (at > source_file_start(p.shared->source) && is_js_ascii_space(at[-1])) at--;
                    arg_span = span_make(p.shared->source, at, at);
                }
                VecExpr* boxed_decorators = nullptr;
                if (vec_expr_size(&decorators)) boxed_decorators = ast_new(VecExpr, decorators);
                else vec_expr_drop(&decorators);
                list_push(vec_function_arg, &ret->args, (FunctionArg){
                    .span = arg_span,
                    .rest_length = rest ? (u32)rest_span.size : 0,
                    .initializer = initializer,
                    .explicit_optional = explicit_optional,
                    .question_offset = span_valid(question_span) ? (u32)(question_span.buf - arg_span.buf) : 0,
                    .rest = rest,
                    .decorators = boxed_decorators,
                    .is_parameter_property = is_parameter_property,
                    .is_readonly_property = is_readonly_property,
                    .is_override_property = is_override_property,
                    .access = access,
                });

                if (p.pos == before_param) {
                    if (is_definitely_function) {
                        // Error was already added? hopefully?
                        hop_token(&p);
                    } else {
                        goto drop_and_fail;
                    }
                } else {
                    hop_whitespace(&p);
                    Span comma = span_make(p.shared->source, p.pos, p.pos + 1);
                    // (tsc's parseDelimitedList: no ',' expected before what ends the list)
                    ignore( (*p.pos != ',' && list_is_terminator(PARSING_PARAMETERS, tok_peek(&p))) || expect_char(&p, ',') );
                    Parser after = parser(&p);
                    hop_whitespace(&after);
                    bool closes = *after.pos == ')';
                    parse_fail(&after);
                    // tsc's checkGrammarParameterList: checkGrammarForDisallowedTrailingComma after a rest parameter
                    // (Allowed in ambient declarations, GH#23070)
                    if (rest && closes && *comma.buf == ',' && !p.shared->in_ambient) {
                        grammar_error(&p, comma, 1013, "A rest parameter or binding pattern may not have a trailing comma.");
                    }
                }
            }

            hop_whitespace(&p);
        }

        // (A missing `)` is only an error: tsc's parseParameters keeps what it parsed and goes on. Not in a speculative
        // arrow function, which it ends: parseParenthesizedArrowFunctionExpression without allowAmbiguity,
        // `({ x: x => 1 })`'s pattern list stops at the `=>` that ends the variable declaration list)
        if (!expect_char(&p, ')')) {
            if (!is_definitely_function && !parse_type_return_syntax && !commit_on_paren) goto drop_and_fail;
            // (Without its `)` the list ends with its last token, tsc's finishNode at the next token's full start: a
            // signature ending there spans no trivia after it)
            unhop_trivia(&p);
        }
    } else if (is_definitely_function) {
        expected_token_error(&p, "(");
        //goto drop_and_fail;
    } else {
        goto drop_and_fail;
    }

    function_impl_set_args_span(ret, span_from(args_start));

    if (parse_type_return_syntax) {
        hop_whitespace(&p);

        if (!hop_start(&p, "=>")){
            if (is_definitely_function) {
                expected_token_error(&p, "=>");
            } else {
                goto drop_and_fail;
            }
        }
    } else {
        Parser type_p = parser(&p);
        hop_whitespace(&type_p);

        if (hop_start(&type_p, ":")) {
            parse_success(&type_p);
        } else {
            parse_fail(&type_p);
            parse_ret_type = false;
        }
    }

    // (No trivia is hopped after the last token: the node ends there)
    if (parse_ret_type) {
        hop_whitespace(&p);
        Anchor ret_a = anchor(&p);
        p.shared->disallow_conditional = false; // (tsc's parseReturnType; parse_type() restores it)
        if (parse_type_predicate(&p, ret)) {
            function_impl_set_ret_span(ret, span_from(ret_a));
            ret->ret_explicit = true;
        } else if (parse_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret)) {
            function_impl_set_ret_span(ret, span_from(ret_a));
            ret->ret_explicit = true;
        } else if (unambiguous_function_type ||
            (is_definitely_function && (parse_type_return_syntax ? *args_start.start == '(' || ret->is_constructor ||
                generic_commits : commit_on_paren))
        ) {
            // A function type is committed (tsc's parseFunctionOrConstructorType), so is a function whose parameter
            // list was (a definite arrow function, a declaration): its return type is missing (tsc's
            // parseTypeOrTypePredicate reports it; a missing type reference, an explicit any)
            expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret);
            function_impl_set_ret_span(ret, span_from_pos(&p));
            ret->ret_explicit = true;
        } else {
            goto drop_and_fail;
        }
    }

    return parse_success(&p);

drop_and_fail:
    function_impl_drop(ret);
    return parse_fail(&p);
}

// Where an `await` here is (the resolve walk reports what isn't allowed). A class `static {}` block is its own error
static AwaitPlace parser_await_place(Parser const* p) {
    if (p->shared->in_static_block) return AWAIT_IN_STATIC_BLOCK;
    // (tsc: an await expression in an await context has nothing to check, one the first pass made is checked)
    // (A namespace body isn't the top level: tsgo's IsInTopLevelContext stops at the ModuleDeclaration)
    if (p->shared->function_depth == 0) {
        return p->shared->in_async ? AWAIT_ALLOWED : p->shared->in_namespace_body ? AWAIT_IN_FUNCTION : AWAIT_AT_TOP_LEVEL;
    }
    return p->shared->in_async ? AWAIT_ALLOWED : AWAIT_IN_FUNCTION;
}

// Call before parsing a function's arguments and body, and function_ctx_leave() after (on success and failure)
static FunctionCtx function_ctx_enter(Parser* p, FunctionFlags flags) {
    FunctionCtx saved = {
        .in_async = p->shared->in_async, .in_generator = p->shared->in_generator,
        .in_static_block = p->shared->in_static_block, .in_parameters = p->shared->in_parameters,
        .outer_in_async = p->shared->outer_in_async,
    };
    p->shared->outer_in_async = p->shared->in_async;
    p->shared->in_static_block = false;
    p->shared->in_parameters = false;
    p->shared->in_async = flags.is_async;
    p->shared->in_generator = flags.is_generator;
    p->shared->function_depth++;
    return saved;
}

static void function_ctx_leave(Parser* p, FunctionCtx saved) {
    p->shared->in_async = saved.in_async;
    p->shared->in_generator = saved.in_generator;
    p->shared->in_static_block = saved.in_static_block;
    p->shared->in_parameters = saved.in_parameters;
    p->shared->outer_in_async = saved.outer_in_async;
    p->shared->function_depth--;
}

typedef enum Tristate {
    TRISTATE_FALSE,
    TRISTATE_TRUE,
    TRISTATE_UNKNOWN,
} Tristate;

// tsc's isParenthesizedArrowFunctionExpressionWorker, at `(`, `<` or `async`: TRUE is an arrow function whatever
// follows (`() {` is missing its `=>`, `(...)` its parameter name), FALSE is never one (`().x` is a parenthesized
// expression missing its contents), UNKNOWN is up to trying
static Tristate arrow_function_tristate(Parser* p) {
    Tok first = tok_peek(p);
    if (tok_is(first, "async")) {
        first = tok_next(p, first);
        if (first.newline_before || !(tok_is(first, "(") || tok_is(first, "<"))) return TRISTATE_FALSE;
    }
    Tok second = tok_next(p, first);
    if (tok_is(first, "(")) {
        if (tok_is(second, ")")) {
            Tok third = tok_next(p, second);
            return tok_is(third, "=>") || tok_is(third, ":") || tok_is(third, "{") ? TRISTATE_TRUE : TRISTATE_FALSE;
        }
        if (tok_is(second, "[") || tok_is(second, "{")) return TRISTATE_UNKNOWN;
        if (tok_is(second, "...")) return TRISTATE_TRUE;
        Tok third = tok_next(p, second);
        if (tok_is_modifier(second) && !tok_is(second, "async") && tok_is_identifier(third)) {
            return tok_is(third, "as") ? TRISTATE_FALSE : TRISTATE_TRUE;
        }
        if (!tok_is_identifier(second) && !tok_is(second, "this")) return TRISTATE_FALSE;
        // (tsc's isIdentifier(): not `yield` in a generator or `await` in an async function)
        if ((p->shared->in_generator && tok_is(second, "yield")) ||
            ((p->shared->in_async || p->shared->in_static_block) && tok_is(second, "await"))
        ) {
            return TRISTATE_FALSE;
        }
        if (tok_is(third, ":")) return TRISTATE_TRUE;
        if (tok_is(third, "?")) {
            Tok fourth = tok_next(p, third);
            return tok_is(fourth, ":") || tok_is(fourth, ",") || tok_is(fourth, "=") || tok_is(fourth, ")") ?
                TRISTATE_TRUE : TRISTATE_FALSE;
        }
        if (tok_is(third, ",") || tok_is(third, "=") || tok_is(third, ")")) return TRISTATE_UNKNOWN;
        return TRISTATE_FALSE;
    }
    if (tok_is(first, "<")) {
        // (tsc's isIdentifier() too: `<await>(x) => x` in an async function, or a module's top level reparsed in an
        // await context, is a type assertion)
        if ((p->shared->in_generator && tok_is(second, "yield")) ||
            ((p->shared->in_async || p->shared->in_static_block) && tok_is(second, "await"))
        ) {
            return TRISTATE_FALSE;
        }
        return tok_is_identifier(second) || tok_is(second, "const") ? TRISTATE_UNKNOWN : TRISTATE_FALSE;
    }
    return TRISTATE_UNKNOWN;
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, bool committed, FunctionImpl* ret);

// committed: arrow_function_tristate() said TRUE, so this parses as an arrow function with errors rather than failing
static bool parse_arrow_function(Parser* parent, ExprCtx ctx, FunctionFlags flags, bool committed, FunctionImpl* ret) {
    // Only an assignment expression is an arrow function (tsc's parseAssignmentExpressionOrHigher): not the operand of
    // a binary or unary operator, `b > (x: T) => x` is `b > (x` missing its `)`
    if (ctx.min_precedence >= ternary_precedence) return false;
    FunctionCtx saved = function_ctx_enter(parent, flags);
    // (A speculative arrow function that fails leaves no `await` identifier noted: tsc's lookAhead and tryParse restore
    // statementHasAwaitIdentifier, `({ await, a: await 1 })` isn't reparsed)
    bool saw_await = parent->shared->saw_top_level_await;
    bool success = parse_arrow_function_inner(parent, ctx, committed, ret);
    if (!success) parent->shared->saw_top_level_await = saw_await;
    function_ctx_leave(parent, saved);
    if (success) {
        ret->is_async = flags.is_async;
        js_syntax_function(parent, ret, true);
    }
    return success;
}

// tsc's typeHasArrowFunctionBlockingParseError: a return type that is a function type missing its parameter list (in
// parentheses or as a function type's own return type too) ends a speculative arrow function, `a ? (b) : (<C x={..`
// isn't `(b): (<C ...` (the type parameters `<C`, then no `(`), whatever recovery made of the rest
// (tsc's missing type reference name isn't checked: tttt's type parser fails there instead)
static bool type_blocks_arrow_function(TypeNode const* type) {
    if (type->tag != TYPE_CALLABLE) return false;
    FunctionImpl const* function = type->callable->function;
    return function_impl_args_span(function).size == 0 || (function->ret_explicit && type_blocks_arrow_function(&function->ret));
}

static bool parse_arrow_function_inner(Parser* parent, ExprCtx ctx, bool committed, FunctionImpl* ret) {
    Parser p = parser(parent);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_ARROW_FUNCTION);
    Anchor a = anchor(&p);
    bool is_definitely_function = false;
    Span lone_arg_name = span_null;

    // In a JSX file `<T>(x) => x` is an element: a generic arrow function is written `<T,>` or `<T extends X>`
    if (*p.pos == '<' && p.shared->source->is_jsx && !jsx_generic_arrow_start(&p)) {
        return parse_fail(&p);
    }

    if (parse_function_args(&p, false, false, true, committed, ret)) {
        if (!committed && ret->ret_explicit && type_blocks_arrow_function(&ret->ret)) {
            function_impl_drop(ret);
            return parse_fail(&p);
        }

        // (A type annotation, not a JSDoc type before the parameter: `(/** @type {T} */ [...s].sort())` in JavaScript
        // is a parenthesized expression)
        Binding const* first = vec_function_arg_is_empty(&ret->args) ? nullptr : &vec_function_arg_at(&ret->args, 0)->initializer.binding;
        // (Not after type parameters: tsc's isParenthesizedArrowFunctionExpression is Unknown after a `<`, `async
        // <T extends U>(x: any);` is a call with type arguments)
        // (A pattern first, `({` or `([`, is only ever Unknown to tsc: the arrow stands if `=>` follows, whatever its
        // annotation or a `?` after it, `v = ({ g: "b", t: w ? u : 1 })`)
        FunctionArg const* first_arg = vec_function_arg_is_empty(&ret->args) ? nullptr : vec_function_arg_at(&ret->args, 0);
        bool pattern_first = first_arg && !first_arg->rest &&
            (first->tag == BINDING_OBJECT || first->tag == BINDING_ARRAY);
        is_definitely_function = committed || (first && !pattern_first && first->has_explicit_type && *a.start != '<' &&
            span_start_ptr(binding_explicit_type_span(first)) > span_start_ptr(first->content_span));
        // (The rest of tsc's isParenthesizedArrowFunctionExpression's certain cases: `()` (then `=>`, `:` or `{`),
        // `(...`, `(a?`, `(public a`. A return type alone isn't: `(a, b): T = v` is a parenthesized expression)
        if (*a.start == '(' && (!first_arg || first_arg->rest || (first_arg->question_offset && !pattern_first) || first_arg->is_parameter_property)) {
            is_definitely_function = true;
        }


    } else if (committed && p.pos[0] == '=' && p.pos[1] == '>') {
        // A lone `=>`: tsc's parseParameters says '(' expected, the list is missing
        *ret = (FunctionImpl){0};
        expected_token_error(&p, "(");
        function_impl_set_args_span(ret, span_from_pos(&p));
    } else if (parse_identifier(&p, &lone_arg_name)) {
        *ret = (FunctionImpl){0};

        function_impl_set_args_span(ret, lone_arg_name);
        Binding binding = {
            .tag = BINDING_IDENTIFIER,
            .content_span = lone_arg_name,
        };
        list_push(vec_function_arg, &ret->args, (FunctionArg){
            .span = lone_arg_name,
            .initializer.binding = binding
        });

    } else {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    bool newline_after_args = p.whitespace == WHITESPACE_NEWLINE;

    Anchor arrow_a = anchor(&p);
    if (!hop_start(&p, "=>")) {
        // tsc's parseParenthesizedArrowFunctionExpression takes a parameter list followed by `{` as an arrow function
        // missing its `=>` even when the tri-state lookahead was unsure (`(x) { }`)
        // (only where tsc's parseAssignmentExpressionOrHigher looks for arrow functions: not in `extends (B) {}`)
        if (is_definitely_function || (*p.pos == '{' && *a.start == '(' && !span_valid(lone_arg_name) &&
                !ctx.reject_operators && ctx.min_precedence < ternary_precedence)
        ) {
            expected_token_error(&p, "=>");
        } else {
            function_impl_drop(ret);
            return parse_fail(&p);
        }
    }
    function_impl_set_arrow_span(ret, span_from(arrow_a));
    if (source_is_mts_or_cts(&p) && vec_generic_item_size(&ret->generic.items) == 1) {
        // tsc's checkGrammarArrowFunction: `<T>() =>` is reserved there, `<T,>` and `<T extends X>` aren't
        GenericItem const* item = vec_generic_item_at(&ret->generic.items, 0);
        const char* after = span_end_ptr(item->full_span);
        while (is_regex_space(*after)) after++;
        if (!item->extends_explicit && *after != ',') {
            grammar_error(&p, item->full_span, 7060,
                "This syntax is reserved in files with the .mts or .cts extension. Add a trailing comma or explicit constraint.");
        }
    }

    // (A missing `=>` is tsc's zero-width missing token, whose text has no line break: no TS1200)
    if (newline_after_args && function_impl_arrow_span(ret).size) {
        // This will generate an error in the typecheck phase, why it's not a parse error I have no idea.
        ret->newline_before_arrow = true;
    }
    hop_whitespace(&p);

    Tok body_start = tok_peek(&p);
    if (function_impl_arrow_span(ret).size == 0 && *p.pos != '{') {
        // No `=>`: the body is a missing identifier (tsc's parseParenthesizedArrowFunctionExpression), what follows
        // continues the arrow function (`(x: any).props`)
        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
        Expr expr = expr_implicit_any(span_from_pos(&p));
        p.pos = span_end_ptr(function_impl_arrow_span(ret));
        ret->body_ = (Block){0};
        vec_statement_push(&ret->body_.statements, (Statement){ .tag = STATEMENT_RETURN_EXPR, .expr = expr });
        ret->expression_body = true;
    } else if (*p.pos != '{' && !tok_is(body_start, ";") && !tok_is(body_start, "function") && !tok_is(body_start, "class") &&
        tok_is_start_of_statement(&p, body_start) && !tok_is_start_of_expression(&p, body_start)
    ) {
        // A statement that is no expression: a block missing its `{` (tsc's parseArrowFunctionExpressionBody,
        // `() => var k = 10; }`)
        expected_token_error(&p, "{");
        ret->body_ = (Block){0};
        const char* list_start = p.pos;
        parse_append_statement_sequence(&p, false, PARSING_BLOCK_STATEMENTS, list_start, &ret->body_.statements);
        hop_whitespace(&p);
        expect_char(&p, '}');
    } else if (!skip_body(&p, ret, true) && !parse_block(&p, false, &ret->body_)) {
        Expr expr;
        const char* body_pos = p.pos;
        if (!expect_expr(&p, ctx, &expr) && p.pos == body_pos) {
            p.pos = span_end_ptr(function_impl_arrow_span(ret));
        }
        ret->body_ = (Block){0};
        list_push(vec_statement, &ret->body_.statements, (Statement){
            .tag = STATEMENT_RETURN_EXPR,
            .expr = expr,
        });
        shrink_list(vec_statement, &ret->body_.statements);
        ret->expression_body = true;
    }

    // tsc's parseParenthesizedArrowFunctionExpression: `a ? (x): string => x : y` keeps its return type
    if (ctx.is_parsing_ternary && ret->ret_explicit) {
        Parser q = parser(&p);
        hop_whitespace(&q);
        bool colon = *q.pos == ':';
        parse_fail(&q);
        if (!colon) {
            function_impl_drop(ret);
            return parse_fail(&p);
        }
    }

    ret->has_body = true;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

// tsc's shouldParseReturnType(ColonToken, isType = true): a signature in a type takes a `=>` for its `:` (TS1005)
static void signature_arrow_return_type(Parser* parent, FunctionImpl* func) {
    if (func->ret_explicit) return;
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (p.pos[0] != '=' || p.pos[1] != '>') {
        parse_fail(&p);
        return;
    }
    parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 2), 1005, "':' expected.");
    p.pos += 2;
    hop_whitespace(&p);
    Anchor ret_a = anchor(&p);
    TypeNode type;
    if (parse_type(&p, TYPE_PRECEDENCE_NULL, &type)) {
        type_drop(&func->ret);
        func->ret = type;
        function_impl_set_ret_span(func, span_from(ret_a));
        func->ret_explicit = true;
    }
    parse_success(&p);
}

// A class member's or a type member's parameters: a signature's (`is_type`) are in no await context, their decorators
// in the one the signature is in (tsc's parseParametersWorker, parseParameterEx's inOuterAwaitContext)
static bool parse_signature_args(Parser* p, bool is_type, FunctionImpl* ret) {
    if (!is_type) return parse_function_args(p, true, false, true, false, ret);
    bool in_async = p->shared->in_async, outer_in_async = p->shared->outer_in_async;
    p->shared->outer_in_async = in_async;
    p->shared->in_async = false;
    p->shared->type_member_args = true;
    // (A type member's `(` or `<` commits it to a signature: tsc's parseSignatureMember, its parameter list recovers,
    // `{ (function () }` is a call signature whose list aborts at `function`, then a method)
    bool parsed = parse_function_args(p, true, false, true, true, ret);
    p->shared->type_member_args = false;
    p->shared->in_async = in_async;
    p->shared->outer_in_async = outer_in_async;
    return parsed;
}

static bool parse_function_header(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    ignore flags;
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // (A signature's parameters are in no await context, their decorators in the one the signature is in, as
    // parse_signature_args())
    bool in_async = p.shared->in_async, outer_in_async = p.shared->outer_in_async;
    p.shared->outer_in_async = in_async;
    p.shared->in_async = false;
    p.shared->type_member_args = true;
    bool parsed = parse_function_args(&p, false, false, true, true, ret);
    p.shared->type_member_args = false;
    p.shared->in_async = in_async;
    p.shared->outer_in_async = outer_in_async;
    if (!parsed) return parse_fail(&p);
    signature_arrow_return_type(&p, ret);

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_non_arrow_function_inner(Parser* parent, FunctionFlags flags, FunctionImpl* ret);

// An accessor in a type: a signature, with a body if one follows (tsc's parseFunctionBlockOrSemicolon with
// SignatureFlags.Type: no `{` means the member simply ends)
static bool parse_type_accessor(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    FunctionCtx saved = function_ctx_enter(parent, flags);
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    if (flags.committed && *p.pos != '(' && *p.pos != '<') {
        // `get foo: string;` in a type: the parameter list is missing (tsc's parseAccessorDeclaration, its
        // parseParameters' parseExpected), the annotation is the return type (like parse_non_arrow_function_inner())
        expected_token_error(&p, "(");
        *ret = (FunctionImpl){0};
        function_impl_set_args_span(ret, span_from_pos(&p));
        if (*p.pos == ':') {
            p.pos++;
            hop_whitespace(&p);
            Anchor ret_a = anchor(&p);
            if (parse_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret)) {
                function_impl_set_ret_span(ret, span_from(ret_a));
                ret->ret_explicit = true;
            }
        }
    } else if (!parse_function_args(&p, false, false, true, true, ret)) {
        function_ctx_leave(parent, saved);
        return parse_fail(&p);
    }
    Parser body = parser(&p);
    hop_whitespace(&body);
    if (*body.pos == '{' && parse_block(&body, false, &ret->body_)) {
        ret->has_body = true;
        parse_success(&body);
    } else {
        parse_fail(&body);
    }
    ret->full_span = span_from(a);
    function_ctx_leave(parent, saved);
    return parse_success(&p);
}

// The body skipper's character classes (skip_balanced_braces())
enum {
    SKIP_OTHER, SKIP_SPACE, SKIP_WORD, SKIP_DIGIT, SKIP_SLASH, SKIP_QUOTE, SKIP_OPEN_BRACE, SKIP_CLOSE_BRACE,
    SKIP_BACKTICK, SKIP_OPEN_PAREN, SKIP_CLOSE_PAREN, SKIP_END, SKIP_DOT, SKIP_CLOSE_BRACKET, SKIP_LESS,
};
static const uint8_t skip_char_class[256] = {
    11, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 5, 0, 2, 0, 0, 5, 9, 10, 0, 0, 0, 0, 12, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 14, 0, 0, 0,
    0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 13, 0, 2,
    8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6, 0, 7, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
};

// A letter, digit, `_`, `$`, `\` or non-ASCII byte (the classes SKIP_WORD and SKIP_DIGIT, a table of its own: the
// word loop is most of the skipper's instructions)
static const bool skip_word_table[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static inline bool skip_word_char(char c) {
    return skip_word_table[(uint8_t)c];
}

static inline bool skip_word_is(char const* start, char const* end, char const* keyword, size_t length) {
    return (size_t)(end - start) == length && !memcmp(start, keyword, length);
}

// Steps over the rest of a template from s (inside it): to past its closing '`' (returns false), or past its next
// `${` (returns true). *s is null where the file ends first
static bool skip_template_part(char const** s) {
    for (char const* at = *s;; at++) {
        if (!*at) {
            *s = nullptr;
            return false;
        }
        if (*at == '\\' && at[1]) {
            at++;
        } else if (*at == '`') {
            *s = at + 1;
            return false;
        } else if (*at == '$' && at[1] == '{') {
            *s = at + 2;
            return true;
        }
    }
}

// Where the code from the `{` at s ends: just past its `}`, or null where that isn't certain. Strings, comments,
// templates (with their `${}`s) and regular expressions are stepped over. A `/` is a regular expression after an
// operator, `(`, `[`, `,`... or an expression keyword, and a division after a name, a number, a string or `]`; after
// `)` a division, unless the parentheses are an `if`/`while`/`for`/`with` head (`if (a) /x/.test(b)`); after `}` (a
// block before a statement, or an object literal) or `++`/`--` it could be either, which gives up. So does `import` or
// `require` anywhere: they would add to the file's module names. The comments stepped over are recorded as the parser
// would have (note_line_comment()): a skipped body keeps its comment directives. In a JSX file a `<` where an expression
// starts is a JSX element (skip_jsx_element()); `level` counts the elements and braces it is nested in
static char const* skip_jsx_element(ParserShared* shared, char const* s, u32 level);

static char const* skip_balanced_braces_at(ParserShared* shared, char const* s, u32 level) {
    enum { MAX_DEPTH = 128, MAX_PARENS = 256 };
    bool template[MAX_DEPTH]; // Per open brace: whether it's a template's `${`
    bool paren_head[MAX_PARENS]; // Per open parenthesis: whether it's an `if`/`while`/`for`/`with` head
    int depth = 0, paren_depth = 0;
    enum { LAST_PUNCTUATOR, LAST_WORD, LAST_VALUE, LAST_CLOSE_PAREN, LAST_CLOSE_BRACE } last = LAST_PUNCTUATOR;
    bool closed_head = false; // The `)` last is closes a head
    csview word = {0}; // The last word, when last is LAST_WORD
    char last_char = 0, before_last_char = 0; // The last two punctuators
    static char const* const regex_after[] = {
        "return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw", "instanceof", "yield",
        "await",
    };
    while (true) {
        char c = *s;
        uint8_t class = skip_char_class[(uint8_t)c];
        // (The common classes first, as branches: the switch's jump is an indirect branch on the character class,
        // which mispredicts at nearly every token)
        if (class == SKIP_WORD) goto word;
        if (class == SKIP_SPACE) {
            s++;
            while (skip_char_class[(uint8_t)*s] == SKIP_SPACE) s++;
            continue;
        }
        if (class == SKIP_OTHER) goto punctuator;
        switch (class) {
            case SKIP_END:
                return nullptr;
            case SKIP_SPACE:
                s++;
                while (skip_char_class[(uint8_t)*s] == SKIP_SPACE) s++;
                continue;
            case SKIP_WORD: word: {
                char const* start = s;
                for (s++; skip_word_char(*s); s++) {}
                if (skip_word_is(start, s, "import", 6) || skip_word_is(start, s, "require", 7)) return nullptr;
                word = (csview){ start, s - start };
                last = LAST_WORD;
                continue;
            }
            case SKIP_DIGIT:
                for (s++; skip_word_char(*s) || *s == '.'; s++) {}
                last = LAST_VALUE;
                continue;
            case SKIP_DOT:
                if (s[1] >= '0' && s[1] <= '9') {
                    for (s++; skip_word_char(*s) || *s == '.'; s++) {}
                    last = LAST_VALUE;
                    continue;
                }
                break;
            case SKIP_QUOTE:
                for (s++; *s != c; s++) {
                    if (!*s || *s == '\n' || *s == '\r') return nullptr;
                    if (*s == '\\' && s[1]) s++;
                }
                s++;
                last = LAST_VALUE;
                continue;
            case SKIP_SLASH: {
                if (s[1] == '/') {
                    char const* start = s;
                    s += 2 + strcspn(s + 2, "\n\r");
                    note_line_comment(shared, start, s);
                    continue;
                }
                if (s[1] == '*') {
                    char const* close = strstr(s + 2, "*/");
                    if (!close) return nullptr;
                    char const* start = s;
                    s = close + 2;
                    char const* last_line_start = start;
                    if (memchr(start, '\n', (size_t)(s - start))) {
                        last_line_start = s;
                        while (last_line_start[-1] != '\n') last_line_start--;
                    }
                    note_block_comment(shared, start, last_line_start, s);
                    continue;
                }
                if (last == LAST_CLOSE_BRACE) return nullptr;
                bool regex = last == LAST_PUNCTUATOR || (last == LAST_CLOSE_PAREN && closed_head);
                if (regex && last == LAST_PUNCTUATOR && (last_char == '+' || last_char == '-') && before_last_char == last_char) {
                    return nullptr;
                }
                for (u32 i = 0; last == LAST_WORD && i < c_arraylen(regex_after); i++) {
                    if (csview_equals(word, regex_after[i])) regex = true;
                }
                if (!regex) break;
                bool in_class = false;
                for (s++; *s != '/' || in_class; s++) {
                    if (!*s || *s == '\n' || *s == '\r') return nullptr;
                    if (*s == '\\' && s[1]) s++;
                    else if (*s == '[') in_class = true;
                    else if (*s == ']') in_class = false;
                }
                for (s++; skip_word_char(*s); s++) {}
                last = LAST_VALUE;
                continue;
            }
            case SKIP_OPEN_BRACE:
                if (depth == MAX_DEPTH) return nullptr;
                template[depth++] = false;
                break;
            case SKIP_CLOSE_BRACE:
                if (!depth) return nullptr;
                s++;
                if (!template[--depth]) {
                    if (!depth) return s;
                    last = LAST_CLOSE_BRACE;
                    continue;
                }
                goto template_part;
            case SKIP_BACKTICK:
                s++;
            template_part:
                if (skip_template_part(&s)) {
                    if (depth == MAX_DEPTH) return nullptr;
                    template[depth++] = true;
                    last = LAST_PUNCTUATOR;
                    last_char = '{';
                } else {
                    if (!s) return nullptr;
                    last = LAST_VALUE;
                }
                continue;
            case SKIP_OPEN_PAREN:
                if (paren_depth == MAX_PARENS) return nullptr;
                paren_head[paren_depth++] = last == LAST_WORD && (csview_equals(word, "if") || csview_equals(word, "while") ||
                    csview_equals(word, "for") || csview_equals(word, "with"));
                break;
            case SKIP_CLOSE_PAREN:
                // (An unbalanced one: it can't be told)
                if (!paren_depth) return nullptr;
                closed_head = paren_head[--paren_depth];
                s++;
                last = LAST_CLOSE_PAREN;
                before_last_char = last_char;
                last_char = c;
                continue;
            case SKIP_CLOSE_BRACKET:
                s++;
                last = LAST_VALUE;
                before_last_char = last_char;
                last_char = c;
                continue;
            case SKIP_LESS: {
                if (!shared->source->is_jsx) break;
                bool expression = last == LAST_PUNCTUATOR ? last_char != '<' && (last_char != '>' || before_last_char == '=') : false;
                for (u32 i = 0; last == LAST_WORD && i < c_arraylen(regex_after); i++) {
                    if (csview_equals(word, regex_after[i])) expression = true;
                }
                if (!expression) break;
                s = skip_jsx_element(shared, s, level + 1);
                if (!s) return nullptr;
                last = LAST_VALUE;
                continue;
            }
        }
        // A punctuator
    punctuator:
        s++;
        last = LAST_PUNCTUATOR;
        before_last_char = last_char;
        last_char = c;
    }
}

static char const* skip_balanced_braces(ParserShared* shared, char const* s) {
    return skip_balanced_braces_at(shared, s, 0);
}

static char const* skip_jsx_space(char const* s) {
    while (skip_char_class[(uint8_t)*s] == SKIP_SPACE) s++;
    return s;
}

// A JSX tag or attribute name
static char const* skip_jsx_name(char const* s) {
    while (skip_word_char(*s) || *s == '.' || *s == ':' || *s == '-') s++;
    return s;
}

// Past the JSX element or fragment at the `<` at s, or null where that isn't certain. Its attributes' and children's
// `{}`s are code (skip_balanced_braces_at()), its text anything but `{`, `}`, `<` and `>` (errors). What isn't an
// element gives up: a generic arrow function's type parameters (`<T,>`, `<T extends U>`, `<T = U>`), a comment between
// attributes, an element as an attribute's value. A closing tag's name isn't compared: a mismatch is a parse error, which
// makes the materialization rerun the program without deferral. The file has JSX (moduleDetection: SourceFile.has_jsx)
static char const* skip_jsx_element(ParserShared* shared, char const* s, u32 level) {
    if (level > 64) return nullptr;
    s++;
    if (*s == '>') {
        s++;
    } else {
        if (skip_char_class[(uint8_t)*s] != SKIP_WORD) return nullptr;
        s = skip_jsx_name(s);
        for (bool first = true;; first = false) {
            s = skip_jsx_space(s);
            if (s[0] == '/' && s[1] == '>') {
                shared->saw_jsx = true;
                return s + 2;
            }
            if (*s == '>') {
                s++;
                break;
            }
            if (*s == '{') {
                if (!(s = skip_balanced_braces_at(shared, s, level + 1))) return nullptr;
                continue;
            }
            if (skip_char_class[(uint8_t)*s] != SKIP_WORD) return nullptr;
            char const* name = s;
            s = skip_jsx_name(s);
            if (first && skip_word_is(name, s, "extends", 7)) return nullptr;
            s = skip_jsx_space(s);
            if (*s != '=') continue;
            s = skip_jsx_space(s + 1);
            if (*s == '"' || *s == '\'') {
                // (No escapes in a JSX string)
                if (!(s = strchr(s + 1, *s))) return nullptr;
                s++;
            } else if (*s == '{') {
                if (!(s = skip_balanced_braces_at(shared, s, level + 1))) return nullptr;
            } else {
                return nullptr;
            }
        }
    }
    // The children, up to the closing tag
    while (true) {
        char c = *s;
        if (!c || c == '}' || c == '>') return nullptr;
        if (c == '{') {
            if (!(s = skip_balanced_braces_at(shared, s, level + 1))) return nullptr;
        } else if (c == '<' && s[1] == '/') {
            s = skip_jsx_space(skip_jsx_name(skip_jsx_space(s + 2)));
            if (*s != '>') return nullptr;
            shared->saw_jsx = true;
            return s + 1;
        } else if (c == '<') {
            if (!(s = skip_jsx_element(shared, s, level + 1))) return nullptr;
        } else {
            s++;
        }
    }
}

// Whether a parameter has an expression in it (a default value, a computed name in its pattern): its walk sees the body's
// names (parameters_see_body(), block_add_names() before the parameters), so the body isn't deferred
static bool binding_has_expressions(Binding const* me) {
    if (me->default_value) return true;
    switch (me->tag) {
        case BINDING_NOOP:
        case BINDING_IDENTIFIER:
            return false;
        case BINDING_ARRAY:
            for_each (i, vec_binding, &me->array) {
                if (binding_has_expressions(i)) return true;
            }
            return false;
        case BINDING_OBJECT:
            for_each (i, vec_object_binding_item, &me->object) {
                if (i->computed_key || binding_has_expressions(&i->target)) return true;
            }
            return false;
    }
    return false;
}

// Whether the body at `{` starts with a string (a directive prologue: `"use strict"` makes the function's scope strict
// before its parameters are walked, block_has_use_strict())
static bool body_starts_with_string(char const* open) {
    char const* s = open + 1;
    while (true) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (s[0] == '/' && s[1] == '/') {
            s += 2 + strcspn(s + 2, "\n\r");
        } else if (s[0] == '/' && s[1] == '*') {
            char const* close = strstr(s + 2, "*/");
            if (!close) return true;
            s = close + 2;
        } else {
            return *s == '"' || *s == '\'' || *s == '`';
        }
    }
}

// The body at `{` isn't parsed when that's safe (its end is certain: skip_balanced_braces()), the function keeps an
// empty body. Plan 78: an outermost function's body in a file that defers them (SourceFile.defer_bodies) is a
// DeferredBody, parsed and walked when a checker first needs it. Not where the walk of the function's parameters would see the body's
//   names (an expression in a parameter), not in a `with` statement, an ambient context or a DisallowIn one (errors), not an IIFE's (part
//   of the flow around it: undefer_body() when the call shows) nor a constructor's the class's walk looks into
//   (parse_class_decl()). Nor an expression's (`expression`) with a function in its signature (a parenthesis after
//   the parameter list's own): the walk of that function asks whether this one is context sensitive
//   (function_impl_lookup_names_as()'s contextual_ancestor), which may take its body
static bool skip_body(Parser* parent, FunctionImpl* func, bool expression) {
    ParserShared* shared = parent->shared;
    SourceFile const* source = shared->source;
    if (*parent->pos != '{') return false;
    bool defer = source->defer_bodies && !shared->materializing && shared->function_depth == 1 &&
        !shared->in_ambient && !shared->with_body_depth &&
        !shared->disallow_in; // (a for statement's initializer: the body's own `in`s are syntax errors, ParserShared.disallow_in)
    for (isize i = 0; defer && i < vec_function_arg_size(&func->args); i++) {
        FunctionArg const* arg = vec_function_arg_at(&func->args, i);
        defer = !arg->initializer.value && !binding_has_expressions(&arg->initializer.binding);
    }
    if (defer && body_starts_with_string(parent->pos)) defer = false;
    if (defer && func->ret_explicit) {
        // (A `typeof` in the return type sees the body's values: `(): typeof groups` with a body's `const groups`)
        Span ret = function_impl_ret_span(func);
        defer = !span_valid(ret) || !csview_contains(span_sv(ret), "typeof");
    }
    if (defer && expression) {
        Span args = function_impl_args_span(func);
        char const* from = span_valid(args) ? span_start_ptr(args) : parent->pos;
        if (*from == '(') from++;
        defer = !memchr(from, '(', (size_t)(parent->pos - from));
    }
    if (!defer) return false;
    char const* end = skip_balanced_braces(shared, parent->pos);
    if (!end) return false;
    Parser p = parser(parent);
    char const* open = p.pos;
    p.pos = end - 1;
    expect_closing(&p, '}', open);
    func->body_ = (Block){0};
    func->has_body = true;
    char const* text = source_file_start(source);
    if (shared->chunk) chunk_effect(shared, (ChunkEffect){ .kind = CHUNK_EFFECT_DEFERRED_BODIES });
    else ((SourceFile*)source)->has_deferred_bodies = true;
    defer_stat_add(DEFER_STAT_SKIPPED, 1);
    defer_stat_add(DEFER_STAT_SKIPPED_BYTES, end - open);
    func->deferred = true;
    func->body_.deferred = ast_new(DeferredBody, { .open = (u32)(open - text), .end = (u32)(end - text) });
    return parse_success(&p);
}

// A deferred body parsed after all (a function the parser learns is an IIFE, a constructor the class's walk looks into):
// at its recorded `{`, in the function's context, with what follows the function parsed already
static void undefer_body(Parser* parent, FunctionImpl* func) {
    if (!func->deferred) return;
    defer_stat_add(DEFER_STAT_UNDEFERRED, 1);
    DeferredBody* deferred = func->body_.deferred;
    Parser saved = *parent;
    FunctionCtx ctx = function_ctx_enter(parent, (FunctionFlags){ .is_async = func->is_async, .is_generator = func->is_generator });
    Parser p = parser(parent);
    p.pos = source_file_start(parent->shared->source) + deferred->open;
    p.whitespace = WHITESPACE_NONE;
    p.whitespace_end = nullptr;
    func->deferred = false;
    func->body_ = (Block){0};
    bool saved_in = allow_in_enter(&p); // (never deferred in a DisallowIn context)
    parse_block(&p, false, &func->body_);
    allow_in_leave(&p, saved_in);
    parse_success(&p);
    function_ctx_leave(parent, ctx);
    parent->pos = saved.pos;
    parent->whitespace = saved.whitespace;
    parent->whitespace_end = saved.whitespace_end;
    ast_delete(deferred);
}

static bool parse_non_arrow_function(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    FunctionCtx saved = function_ctx_enter(parent, flags);
    bool success = parse_non_arrow_function_inner(parent, flags, ret);
    function_ctx_leave(parent, saved);
    if (success) {
        ret->is_async = flags.is_async;
        ret->is_generator = flags.is_generator;
        if (span_valid(flags.asterisk)) function_impl_rare_mut(ret)->asterisk = flags.asterisk;
        js_syntax_function(parent, ret, true);
    }
    return success;
}

static bool parse_non_arrow_function_inner(Parser* parent, FunctionFlags flags, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (flags.committed && *p.pos != '(' && *p.pos != '<') {
        expected_token_error(&p, "(");
        *ret = (FunctionImpl){0};
        function_impl_set_args_span(ret, span_from_pos(&p));
        if (*p.pos == ':') {
            p.pos++;
            hop_whitespace(&p);
            Anchor ret_a = anchor(&p);
            if (parse_type(&p, TYPE_PRECEDENCE_NULL, &ret->ret)) {
                function_impl_set_ret_span(ret, span_from(ret_a));
                ret->ret_explicit = true;
            }
        }
    } else if (!parse_function_args(&p, false, false, true, true, ret)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (skip_body(&p, ret, flags.expression) || parse_block(&p, false, &ret->body_)) {
        ret->has_body = true;
    } else if (*p.pos == ';' || *p.pos == '}' || at_eof(&p, p.pos) || p.whitespace == WHITESPACE_NEWLINE) {
        ret->body_ = (Block){0};
    } else {
        // tsc's parseFunctionBlock: a body whose `{` is missing, empty
        if (flags.brace_expected) expected_token_error(&p, "{");
        else parse_error(&p, span_from_token(&p), 1144, "'{' or ';' expected.");
        ret->body_ = (Block){0};
        ret->body_missing = true;
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

// `name_required`: a declaration that isn't `export default` (tsc's parseFunctionDeclaration, TS1003)
static bool parse_function_keyword_function(Parser* parent, bool is_async, bool name_required, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "function")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    FunctionFlags flags = { .is_async = is_async, .committed = true, .expression = !name_required };
    if (hop_char(&p, '*')) {
        flags.is_generator = true;
        flags.asterisk = span_from_previous_char(&p);
        hop_whitespace(&p);
    }

    Span name;
    char const* name_start = p.pos;
    bool named = parse_identifier(&p, &name);
    if (named && is_reserved_word(span_sv(name))) {
        // (tsc's parseBindingIdentifier: a reserved word is no name, it stays for what follows)
        p.pos = name_start;
        named = false;
    }
    if (!named) {
        name = span_null;
        if (name_required) {
            expect_non_reserved_identifier(&p, &name);
            // (A missing name, which tsc still declares, at the token's full start)
            name = span_make(p.shared->source, token_full_start(&p), token_full_start(&p));
        }
    }
    hop_whitespace(&p);

    if (!parse_non_arrow_function(&p, flags, ret)) {
        parse_error(&p, span_from_token(&p), TS_CODE_NONE, "Expected function.");
        *ret = (FunctionImpl){0};
    }

    ret->name = name;
    ret->full_span = span_from(a);
    return parse_success(&p);
}

static inline bool parse_name_or_value(Parser* parent, Span* span_ret, cstr* name_ret, bool* name_is_value_ret) {
    Value value;
    Anchor a = anchor(parent);
    if (parse_identifier_or_keyword(parent, span_ret)) {
        *name_ret = identifier_name_value(span_sv(*span_ret));
        *name_is_value_ret = false;
        return true;
    } else if (*parent->pos != '`' && parse_literal(parent, &value)) { // (a template is no property name)
        if (value.tag == VALUE_STRING) {
            *name_ret = value.string;
            // Now value shouldn't be dropped
        } else {
            *name_ret = cstr_null;
            cstr_append_value_js(name_ret, &value);
            value_drop(&value);
        }
        *span_ret = span_from(a);
        *name_is_value_ret = true;
        return true;
    } else {
        return false;
    }
}

// Hops a contextual member keyword (`get`, `set`, `async`) if it is followed by a member name on the same line,
// rather than being the member name itself (`{ get: 1 }`, `{ get() {} }`, `{ get }`)
[[gnu::noinline]] static bool hop_member_keyword_slow(Parser* parent, const char* keyword, bool allow_newline);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool hop_member_keyword(Parser* parent, const char* keyword, bool allow_newline) {
    if (*parent->pos != keyword[0] && *parent->pos != '\\') GATE_FAIL(hop_member_keyword(&v_, keyword, allow_newline));
    return hop_member_keyword_slow(parent, keyword, allow_newline);
}

[[gnu::noinline]] static bool hop_member_keyword_slow(Parser* parent, const char* keyword, bool allow_newline) {
    Parser p = parser(parent);
    if (!hop_start_if_ends_on_word_break(&p, keyword)) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (!allow_newline && p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }
    // (A name may start with a non-ASCII letter or an escape: `get ɵx()`)
    if (is_identifier_char(*p.pos) || identifier_char_size(p.pos, true) || *p.pos == '[' || *p.pos == '"' ||
        *p.pos == '\'' || *p.pos == '#' || (*p.pos == '*' && strcmp(keyword, "async") == 0)
    ) {
        return parse_success(&p);
    }
    return parse_fail(&p);
}

static bool parse_object_item(Parser* parent, ObjectExprItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (hop_start(&p, "...")) {
        hop_whitespace(&p);
        Expr expr;
        expect_expr(&p, expr_ctx_null, &expr);
        *ret = (ObjectExprItem){
            .tag = OBJECT_ITEM_SPREAD,
            .span = span_from(a),
            .value = expr,
        };
        return parse_success(&p);
    }

    ObjectExprItem item = { .tag = OBJECT_ITEM_PROPERTY };
    bool must_be_method = false;
    FunctionFlags flags = { .brace_expected = true, .expression = true };
    // tsc parses modifiers in front of any member and reports them from the checker (a grammar error)
    ModifierList modifiers;
    parse_modifier_list_ex(&p, true, false, &modifiers);
    hop_whitespace(&p);
    // (An `async` modifier makes a method only if a parameter list follows: `{ async x }` is a property with a modifier
    // that can't be used there, tsc's parseObjectLiteralElement)
    if (modifiers.includes[MODIFIER_ASYNC]) flags.is_async = true;
    Span async_modifier = span_null;

    // tsc's checkGrammarObjectLiteralExpression: every modifier but a method's `async` (TS1184 on a method is
    // checkGrammarMethod), reported by object_check_grammar()
    bool report_modifiers_cannot_appear = false;
    for_each (i, vec_spanned_modifier, &modifiers.spanned) {
        if (i->tag == MODIFIER_ASYNC) {
            if (!span_valid(async_modifier)) async_modifier = i->span;
            continue;
        }
        report_modifiers_cannot_appear = true;
    }
    Span first_modifier = vec_spanned_modifier_is_empty(&modifiers.spanned) ?
        span_null :
        vec_spanned_modifier_front(&modifiers.spanned)->span;

    if (hop_member_keyword(&p, "async", false)) {
        must_be_method = true;
        flags.is_async = true;
    }
    if (hop_char(&p, '*')) {
        flags.asterisk = span_from_previous_char(&p);
        hop_whitespace(&p);
        must_be_method = true;
        flags.is_generator = true;
    }
    if (!must_be_method) {
        if (hop_member_keyword(&p, "get", true)) {
            item.tag = OBJECT_ITEM_GETTER;
            must_be_method = true;
        } else if (hop_member_keyword(&p, "set", true)) {
            item.tag = OBJECT_ITEM_SETTER;
            must_be_method = true;
        }
    }

    bool name_is_value = false;
    bool name_is_identifier = false;
    if (*p.pos == '[') {
        Anchor key_anchor = anchor(&p);
        hop_char(&p, '[');
        hop_whitespace(&p);
        Expr key;
        p.shared->in_property_name++;
        bool saved_in = allow_in_enter(&p);
        bool has_key = parse_expr(&p, expr_ctx_null, &key);
        if (has_key) parse_computed_key_comma(&p, &key);
        allow_in_leave(&p, saved_in);
        p.shared->in_property_name--;
        if (!has_key) {
            modifier_list_drop(&modifiers);
            return parse_fail(&p);
        }
        hop_whitespace(&p);
        // (tsc's parseComputedPropertyName: `{ [s: symbol]: 1 }` is missing its `]`)
        expect_char(&p, ']');
        item.computed_key = ast_new(Expr, key);
        item.name_span = span_from(key_anchor);
    } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
        // A private name (tsc's parsePropertyName), the checker reports it (not a shorthand: `{ #x }` misses a `:`)
        Anchor name_anchor = anchor(&p);
        p.pos++;
        parse_identifier_or_keyword(&p, nullptr);
        item.name_span = span_from(name_anchor);
        item.name_string = identifier_name_value(span_sv(item.name_span));
    } else {
        const char* name_start = p.pos;
        if (!parse_name_or_value(&p, &item.name_span, &item.name_string, &name_is_value)) {
            // `{ a.b }`: the `.` is a list element to tsc (a missing name, then a missing ':' and `<missing>.b`)
            bool dot = *p.pos == '.' && p.pos[1] != '.';
            if (!flags.is_generator && !dot) {
                modifier_list_drop(&modifiers);
                return parse_fail(&p);
            }
            // `{ *() {} }`: committed to a generator method by the `*`, the name is missing
            parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
            item.name_span = span_from_pos(&p);
            item.name_string = cstr_init();
        }
        item.name_is_value = name_is_value;
        name_is_identifier = !name_is_value;
        if (name_is_identifier) {
            // Keywords are fine as property names, but only real identifiers can be shorthand properties
            Parser probe = parser(&p);
            probe.pos = name_start;
            Span ignored;
            name_is_identifier = !yield_await_is_keyword_here(&probe) && parse_identifier(&probe, &ignored) &&
                !is_reserved_word(span_sv(ignored));
            parse_fail(&probe);
        }
    }
    hop_whitespace(&p);

    // tsc's parser takes a `?` and a `!` after any member name, its checker reports them (grammar errors, see
    // parse_file()). A property's `?` is reported by the checker (object_check_grammar(): not in a destructuring
    // target), a method's below
    Span question = span_null;
    if (*p.pos == '?' && p.pos[1] != '?' && p.pos[1] != '.') {
        question = span_make(p.shared->source, p.pos, p.pos + 1);
        p.pos++;
        hop_whitespace(&p);
    }
    if (*p.pos == '!' && p.pos[1] != '=') {
        grammar_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1255,
            "A definite assignment assertion '!' is not permitted in this context.");
        p.pos++;
        hop_whitespace(&p);
    }

    // (A property with an `async` modifier: its `async` too)
    bool async_reported = span_valid(async_modifier) && !must_be_method && *p.pos != '(' && *p.pos != '<';
    u32 reported = 0;
    for_each (i, vec_spanned_modifier, &modifiers.spanned) reported += i->tag != MODIFIER_ASYNC || async_reported;
    if (reported) {
        ObjectExprItemRare* rare = object_item_rare_mut(&item);
        rare->modifiers = ast_malloc(sizeof(Span) * reported);
        for_each (i, vec_spanned_modifier, &modifiers.spanned) {
            if (i->tag != MODIFIER_ASYNC || async_reported) rare->modifiers[rare->modifier_count++] = i->span;
        }
    }
    FunctionImpl func;
    if ((*p.pos == '(' || *p.pos == '<') && parse_non_arrow_function(&p, flags, &func)) {
        if (item.tag == OBJECT_ITEM_PROPERTY) {
            item.tag = OBJECT_ITEM_METHOD;
            item.optional_method = span_valid(question);
        }
        // (tsc's getJSSyntacticDiagnosticsForFile: a method declaration's `?`)
        if (span_valid(question)) js_syntax_error(&p, question, 8009, "The '?' modifier can only be used in TypeScript files.");
        // (tsc's parseFunctionBlockOrSemicolon: a method without body takes the `;`, which the grammar check of the
        // missing body is at)
        if (!func.has_body && !func.body_missing && *p.pos == ';') p.pos++;
        func.full_span = span_from(a);
        item.value = expr_function(span_from(a), ast_new(FunctionImpl, func));
    } else if (must_be_method && (flags.is_generator || item.tag != OBJECT_ITEM_PROPERTY)) {
        // `*` and `get`/`set` commit to a method (tsc's parseMethodDeclaration/parseAccessorDeclaration): the
        // parameter list is missing, a body may follow
        expected_token_error(&p, "(");
        func = (FunctionImpl){ .is_generator = flags.is_generator };
        if (span_valid(flags.asterisk)) function_impl_rare_mut(&func)->asterisk = flags.asterisk;
        function_impl_set_args_span(&func, span_from_pos(&p));
        if (*p.pos == ':') {
            // (tsc's parseReturnType still takes a return type: `async * x: 1;` is a method returning 1)
            p.pos++;
            hop_whitespace(&p);
            Anchor ret_a = anchor(&p);
            if (parse_type(&p, TYPE_PRECEDENCE_NULL, &func.ret)) {
                function_impl_set_ret_span(&func, span_from(ret_a));
                func.ret_explicit = true;
            }
            hop_whitespace(&p);
        }
        if (parse_block(&p, false, &func.body_)) func.has_body = true;
        else if (*p.pos == ';') p.pos++; // (parseFunctionBlockOrSemicolon)
        // (Where no `;` can go, tsc's parseFunctionBlock makes a missing, empty block: a getter's type comes from it)
        else if (!tok_can_parse_semicolon(tok_peek(&p))) func.body_missing = true;
        if (item.tag == OBJECT_ITEM_PROPERTY) item.tag = OBJECT_ITEM_METHOD;
        func.full_span = span_from(a);
        item.value = expr_function(span_from(a), ast_new(FunctionImpl, func));
    } else if (must_be_method) {
        object_expr_item_drop(&item);
        modifier_list_drop(&modifiers);
        return parse_fail(&p);
    } else if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        bool saved_in = allow_in_enter(&p);
        expect_expr(&p, expr_ctx_null, &item.value);
        allow_in_leave(&p, saved_in);
        // Not `__proto__: class {}`, which sets the prototype
        if (item.computed_key || !csview_equals(cstr_sv(&item.name_string), "__proto__")) {
            mark_named_evaluation(&item.value, item.computed_key);
        }
        mark_assigned_name(&item.value, item.name_span);
    } else if (name_is_identifier && !item.computed_key) {
        // tsc's parseObjectLiteralElement: an identifier not followed by `:` is a shorthand property, whatever comes
        // next (`{ a; b }`, `{ a b }`: the list reports the missing comma)
        item.tag = OBJECT_ITEM_SHORTHAND;
        // (`{ undefined }`: the value is the global undefined, which tsc's resolveName finds as undefinedSymbol and
        // tttt has as a literal)
        item.value = span_equals(item.name_span, "undefined") ? expr_literal(item.name_span, value_undefined) :
            expr_identifier(item.name_span, (IdentifierExpr){ .shorthand = true });
        if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
            // `{ a = 1 }` is only valid as a destructuring assignment target
            Anchor equals = anchor(&p);
            hop_char(&p, '=');
            object_item_rare_mut(&item)->shorthand_equals = span_from(equals);
            hop_whitespace(&p);
            Expr default_value;
            bool saved_in = allow_in_enter(&p);
            bool has_default = expect_expr(&p, expr_ctx_null, &default_value);
            allow_in_leave(&p, saved_in);
            if (has_default) {
                mark_named_evaluation(&default_value, false);
                // (A name right there is the shorthand property's child too: tsc's TS18004 when it isn't found)
                if (default_value.tag == EXPR_IDENTIFIER && !expr_is_perenthisized(&default_value)) {
                    default_value.identifier.shorthand = true;
                }
                object_item_rare_mut(&item)->shorthand_default = ast_new(Expr, default_value);
            } else {
                expr_drop(&default_value);
            }
        }
    } else {
        // Committed: `{ "" }`, `{ [e] }`, `{ this }` are `':' expected` and a missing value
        expect_char(&p, ':');
        hop_whitespace(&p);
        expect_expr(&p, expr_ctx_null, &item.value);
    }

    if (span_valid(question)) {
        if (item.tag != OBJECT_ITEM_PROPERTY && item.tag != OBJECT_ITEM_SHORTHAND) {
            grammar_error(&p, question, 1162, "An object member cannot be declared optional.");
        } else {
            isize offset = span_start_ptr(question) - span_end_ptr(item.name_span) + 1;
            item.question_offset = offset < UINT16_MAX ? (uint16_t)offset : 0; // (Past a 64k comment: not reported)
        }
    }
    item.span = span_from(a);
    // (A shorthand property ends with its name, not with the whitespace looked past for a `:`)
    if (item.tag == OBJECT_ITEM_SHORTHAND && !object_item_rare(&item)->shorthand_default) {
        item.span = span_make_in(item.span, span_start_ptr(item.span), span_end_ptr(item.name_span));
    }
    if (p.shared->source->is_js) {
        // The tags of a method, or of a property whose value is a function, are the function's
        VecJsDocTag tags = {0};
        FunctionImpl* function = item.tag == OBJECT_ITEM_SHORTHAND ? nullptr : jsdoc_function_of(&item.value);
        if ((function || item.tag == OBJECT_ITEM_PROPERTY || item.tag == OBJECT_ITEM_SHORTHAND) && jsdoc_before_leading(&p, a.start, &tags)) {
            // A `@type` that isn't a written signature (`{F["tap"]}`) is the declared type, which a function value is
            // contextually typed by
            JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
            bool signature = tag && (tag->type.tag == TYPE_CALLABLE || tag->type.tag == TYPE_INTERFACE);
            bool accessor = item.tag == OBJECT_ITEM_GETTER || item.tag == OBJECT_ITEM_SETTER; // (The function's, see below)
            // (A method's is its signature only, tsgo's FullSignature: no declared type, TS8030 if no call signature fits;
            // a property's is its declared type only, which a function value is contextually typed by)
            bool method = item.tag == OBJECT_ITEM_METHOD;
            if (tag && (!(function && signature) || item.tag == OBJECT_ITEM_PROPERTY) && !accessor && !method) {
                object_item_rare_mut(&item)->jsdoc_type = ast_new(TypeNode, jsdoc_take_type(tag, nullptr));
            }
            if (function && accessor) function->accessor = item.tag == OBJECT_ITEM_GETTER ? 1 : 2; // (Set again by the resolve walk)
            // (A method's `@type` is its FullSignature, like a declaration's: its signature, not moved into it)
            if (function && method) p.shared->jsdoc_declaration = true;
            if (function) jsdoc_apply_function(&p, &tags, function);
        }
        vec_jsdoc_tag_drop(&tags);
    }
    // (A method's or accessor's modifiers: tsc's checkGrammarMethod/checkGrammarAccessor, by way of
    // checkGrammarFunctionLikeDeclaration, check them as a class member's first, TS1184 for a method's only when they
    // pass; either ends the member's grammar checks, its missing body's '{' expected too)
    if (!vec_spanned_modifier_is_empty(&modifiers.spanned) && item.value.tag == EXPR_FUNCTION &&
        item.tag != OBJECT_ITEM_PROPERTY && item.tag != OBJECT_ITEM_SHORTHAND
    ) {
        ModifiedKind kind = item.tag == OBJECT_ITEM_GETTER ? MODIFIED_GET_ACCESSOR :
            item.tag == OBJECT_ITEM_SETTER ? MODIFIED_SET_ACCESSOR : MODIFIED_METHOD;
        ErrorCtx errs = { .phase = ERROR_PHASE_PARSE };
        bool failed = modifier_list_check_grammar(&modifiers, &(ModifiedNode){ .kind = kind, .parent = MODIFIED_IN_OTHER,
            .parent_ambient = p.shared->in_ambient, .name = item.name_span }, &errs);
        for_each (i, vec_error, &errs.errors) {
            i->grammar = true;
            add_error_to_parser(&p, *i);
            *i = (Error){0};
        }
        error_ctx_drop(&errs);
        if (!failed && kind == MODIFIED_METHOD && report_modifiers_cannot_appear) {
            grammar_error(&p, first_modifier, 1184, "Modifiers cannot appear here.");
            failed = true;
        }
        item.modifiers_failed = failed;
    }
    item.readonly_modifier = modifiers.includes[MODIFIER_READONLY];
    item.abstract_modifier = modifiers.includes[MODIFIER_ABSTRACT];
    item.access_modifier = modifiers.includes[MODIFIER_PRIVATE] ? 2 : modifiers.includes[MODIFIER_PROTECTED] ? 1 : 0;
    modifier_list_drop(&modifiers);
    ast_cstr_seal(&item.name_string);
    *ret = item;
    return parse_success(&p);
}

static bool parse_object_ex(Parser* parent, ObjectExpr* ret, bool brace_expected);

static bool parse_object(Parser* parent, ObjectExpr* ret) {
    return parse_object_ex(parent, ret, false);
}

// brace_expected: a missing `{` is TS1005 and the members are parsed anyway (tsc's parseObjectLiteralExpression
// parseExpected(OpenBraceToken), which parseJsonText gets to for anything that isn't another JSON value)
static bool parse_object_ex(Parser* parent, ObjectExpr* ret, bool brace_expected) {
    Parser p = parser(parent);
    *ret = vec_object_expr_item_init();
    ScratchList list = {0};

    const char* open = p.pos;
    bool open_parsed = hop_char(&p, '{');
    if (!open_parsed) {
        if (!brace_expected) return parse_fail(&p);
        parse_error(&p, span_from_token_or_eof(&p), 1005, "'{' expected.");
    }
    hop_whitespace(&p);

    // tsc's parseDelimitedList with considerSemicolonAsDelimiter
    list = scratch_list_begin();
    u32 saved_context = list_enter(&p, PARSING_OBJECT_LITERAL_MEMBERS);
    while (!at_eof(&p, p.pos) && *p.pos != '}') {
        ObjectExprItem item;
        const char* start = p.pos;
        if (!parse_object_item(&p, &item)) {
            if (list_abort_or_skip(&p, PARSING_OBJECT_LITERAL_MEMBERS)) break;
            continue;
        }
        scratch_list_push(&list, &item, sizeof(item));
        hop_whitespace(&p);
        if (hop_char(&p, ',') || at_eof(&p, p.pos) || *p.pos == '}') {
            hop_whitespace(&p);
            continue;
        }
        expect_char(&p, ',');
        if (*p.pos == ';' && p.whitespace != WHITESPACE_NEWLINE) p.pos++;
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    *ret = (ObjectExpr){ .data = scratch_list_finish(&list, sizeof(ObjectExprItem)), .size = list.count, .capacity = list.count };

    // (Without its `{`, no related information: parseExpectedMatchingBrackets with openParsed false)
    if (open_parsed ? !expect_closing(&p, '}', open) : !expect_char(&p, '}')) {
        // The literal ends with its last token, not the whitespace after it
        while (p.pos > open + 1 && is_js_ascii_space(p.pos[-1])) p.pos--;
    }

    return parse_success(&p);
}

// An expression that may be a spread (`...a`), for array literals and call arguments
static bool parse_expr_or_spread(Parser* parent, Expr* ret) {
    if (!(parent->pos[0] == '.' && parent->pos[1] == '.' && parent->pos[2] == '.')) {
        return parse_expr(parent, expr_ctx_null, ret);
    }
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    p.pos += 3;
    hop_whitespace(&p);
    Expr operand;
    // (A missing operand is reported where it is and the spread element stays, tsc's parseSpreadElement: `f(...)` is
    // TS1109 at the `)`)
    if (!expect_expr(&p, expr_ctx_null, &operand) && !(operand.tag == EXPR_IMPLICIT_ANY && operand.missing)) {
        return parse_fail(&p);
    }
    *ret = expr_spread(span_from(a), ast_new(Expr, operand));
    return parse_success(&p);
}

static bool parse_array(Parser* parent, VecExpr* ret) {
    Parser p = parser(parent);
    *ret = vec_expr_init();

    const char* open = p.pos;
    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // tsc's parseDelimitedList(ArrayLiteralMembers): an element at `,` is omitted
    u32 saved_context = list_enter(&p, PARSING_ARRAY_LITERAL_MEMBERS);
    while (!at_eof(&p, p.pos) && *p.pos != ']') {
        const char* start = p.pos;
        Expr expr;
        if (*p.pos == ',') {
            expr = expr_undefined(span_from_pos(&p));
        } else if ((p.pos[0] == '=' && p.pos[1] == '>') || !parse_expr_or_spread(&p, &expr)) {
            if (!list_is_element(&p, PARSING_ARRAY_LITERAL_MEMBERS, tok_peek(&p))) {
                if (list_abort_or_skip(&p, PARSING_ARRAY_LITERAL_MEMBERS)) break;
                continue;
            }
            expect_expr(&p, expr_ctx_null, &expr);
        }
        list_push(vec_expr, ret, expr);
        hop_whitespace(&p);
        if (hop_char(&p, ',')) {
            hop_whitespace(&p);
            continue;
        }
        if (at_eof(&p, p.pos) || *p.pos == ']') break;
        expect_char(&p, ',');
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);

    // (A missing `]` is at the next token's full start: the literal ends with its last token)
    if (!expect_closing(&p, ']', open)) unhop_trivia(&p);
    shrink_list(vec_expr, ret);

    return parse_success(&p);
}

static bool parse_mapped_interface_member(Parser* parent, InterfaceMember* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = (InterfaceMember){
        .key.tag = INTERFACE_KEY_MAPPED,
    };
    InterfaceMappedKey m = {0};

    if (hop_char(&p, '+')) {
        m.readonly = 1;
        hop_whitespace(&p);
    } else if (hop_char(&p, '-')) {
        m.readonly = -1;
        hop_whitespace(&p);
    }

    if (hop_start_if_ends_on_word_break(&p, "readonly")) {
        if (m.readonly == 0) {
            m.readonly = 1;
        }
        hop_whitespace(&p);
    } else if (m.readonly != 0) {
        return parse_fail(&p);
    }

    if (!expect_char(&p, '[') && m.readonly == 0) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!expect_identifier(&p, &m.type_name) && m.readonly == 0) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (!hop_start_if_ends_on_word_break(&p, "in")) {
        if (m.readonly == 0) {
            return parse_fail(&p);
        } else {
            expected_token_error(&p, "in");
        }
    }
    hop_whitespace(&p);

    Anchor in_type_a = anchor(&p);
    expect_type(&p, TYPE_PRECEDENCE_NULL, &m.in_type);
    m.in_type_span = span_from(in_type_a);
    hop_whitespace(&p);

    if (hop_start_if_ends_on_word_break(&p, "as")) {
        hop_whitespace(&p);
        m.has_as_type = true;

        Anchor as_type_a = anchor(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &m.as_type);
        m.as_type_span = span_from(as_type_a);
        hop_whitespace(&p);
    }

    expect_char(&p, ']');
    ret->key.span = span_from(a);
    hop_whitespace(&p);

   if (hop_char(&p, '+')) {
        m.optional = 1;
        ret->key.span = span_from(a);
        hop_whitespace(&p);
    } else if (hop_char(&p, '-')) {
        m.optional = -1;
        ret->key.span = span_from(a);
        hop_whitespace(&p);
    }

    if (m.optional == 0) {
        if (hop_char(&p, '?')) {
            m.optional = 1;
        }
    } else {
        expect_char(&p, '?');
    }
    ret->key.span = span_from(a);
    hop_whitespace(&p);

    // (tsc's parseMappedType: the type is optional, its template is then the error type)
    if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);
    } else {
        ret->type = type_any;
        ret->type.parsed = false;
    }

    ret->key.mapped = ast_new(InterfaceMappedKey, m);
    return parse_success(&p);
}

// tsc's isUnambiguouslyIndexSignature, at a `[`: `[x:`, `[x,`, `[x?:`, `[x?,`, `[x?]`, `[...`, `[]`, `[public x`
static bool is_index_signature_start(Parser* p) {
    if (*p->pos != '[') return false;
    Tok t = tok_next(p, tok_peek(p));
    if (tok_is(t, "...") || tok_is(t, "]")) return true;
    if (tok_is_modifier(t)) {
        t = tok_next(p, t);
        if (tok_is_identifier(t)) return true;
    } else if (!tok_is_identifier(t)) {
        return false;
    } else {
        t = tok_next(p, t);
    }
    if (tok_is(t, ":") || tok_is(t, ",")) return true;
    if (!tok_is(t, "?")) return false;
    t = tok_next(p, t);
    return tok_is(t, ":") || tok_is(t, ",") || tok_is(t, "]");
}

// An index signature (tsc's parseIndexSignatureDeclaration): once is_index_signature_start() says so, the brackets are
// a parameter list whatever is in them. The parameter grammar errors of tsc's checkGrammarIndexSignatureParameters are
// reported here (grammar errors); the type ones and TS1021 by interface_member_typecheck()
static bool parse_index_signature(Parser* parent, InterfaceMember* ret) {
    if (!is_index_signature_start(parent)) return false;
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    *ret = (InterfaceMember){ .key.tag = INTERFACE_KEY_INDEX, .key.index = ast_new(InterfaceIndexKey, {0}) };
    p.pos++;
    hop_whitespace(&p);

    u32 saved_context = list_enter(&p, PARSING_PARAMETERS);
    u32 count = 0;
    Span trailing_comma = span_null;
    Span first_rest = span_null, first_modifier_name = span_null, first_question = span_null;
    bool first_has_initializer = false, first_has_type = false;
    while (!at_eof(&p, p.pos) && *p.pos != ']') {
        const char* before = p.pos;
        Anchor param_start = anchor(&p);
        VecExpr decorators = {0};
        ignore parse_decorators(&p, &decorators);
        vec_expr_drop(&decorators);
        ModifierList modifiers;
        parse_modifier_list_ex(&p, true, false, &modifiers);
        bool has_modifiers = vec_spanned_modifier_size(&modifiers.spanned) != 0;
        bool is_property = modifiers.includes[MODIFIER_READONLY] || modifiers.includes[MODIFIER_PUBLIC] ||
            modifiers.includes[MODIFIER_PROTECTED] || modifiers.includes[MODIFIER_PRIVATE] ||
            modifiers.includes[MODIFIER_OVERRIDE];
        // (A parameter's modifiers all the same, tsc's checkParameter: `[in K: string]` is TS1274)
        if (has_modifiers) check_modifiers(&p, &modifiers, (ModifiedNode){ .kind = MODIFIED_PARAMETER, .parent = MODIFIED_IN_OTHER });
        modifier_list_drop(&modifiers);
        hop_whitespace(&p);

        Span rest = span_null;
        if (hop_start(&p, "...")) {
            rest = span_make(p.shared->source, p.pos - 3, p.pos);
            hop_whitespace(&p);
        }
        Span name;
        if (!parse_identifier(&p, &name)) {
            if (p.pos == before) {
                if (list_abort_or_skip(&p, PARSING_PARAMETERS)) break;
                continue;
            }
            parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
            name = span_from_pos(&p);
        }
        hop_whitespace(&p);
        Span question = span_null;
        if (hop_char(&p, '?')) {
            question = span_from_previous_char(&p);
            hop_whitespace(&p);
        }
        TypeNode type = {0};
        bool has_type = false;
        if (hop_char(&p, ':')) {
            hop_whitespace(&p);
            expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
            has_type = true;
            hop_whitespace(&p);
        } else if (span_valid(rest)) {
            type = type_array(ast_new(TypeNode, type_any), false);
        }
        Expr initializer;
        bool has_initializer = parse_initialization(&p, expr_ctx_null, &initializer);
        if (has_initializer) {
            isize n = vec_span_size(&ret->key.index->initialized);
            ret->key.index->initializer_values = realloc(ret->key.index->initializer_values, sizeof(Expr*) * (size_t)(n + 1));
            panic_if(!ret->key.index->initializer_values, "out of memory");
            ret->key.index->initializer_values[n] = ast_new(Expr, initializer);
            vec_span_push(&ret->key.index->initialized, span_from(param_start));
            hop_whitespace(&p);
        }

        if (span_valid(question) && count < 32) ret->key.index->optional_params |= 1u << count;
        if (count == 0) {
            ret->key.index->name = name;
            ret->key.index->decl = span_make(p.shared->source, param_start.start, span_end_ptr(name));
            ret->key.index->type = type;
            first_rest = rest;
            if (has_modifiers) first_modifier_name = name;
            if (is_property) ret->key.index->parameter_property = span_from(param_start);
            first_question = question;
            first_has_initializer = has_initializer;
            first_has_type = has_type;
        } else {
            vec_span_push(&ret->key.index->more_names, name);
            vec_span_push(&ret->key.index->more_decls, span_make(p.shared->source, param_start.start, span_end_ptr(name)));
            vec_type_push(&ret->key.index->more_types, type);
        }
        count++;

        trailing_comma = span_null;
        if (*p.pos == ']' || at_eof(&p, p.pos)) break; // (an end of file ends every list: tsc's isListTerminator)
        if (hop_char(&p, ',')) {
            trailing_comma = span_from_previous_char(&p);
            hop_whitespace(&p);
        } else {
            expect_char(&p, ',');
            hop_whitespace(&p);
        }
    }
    list_leave(&p, saved_context);
    expect_char(&p, ']');
    char const* bracket_end = p.pos; // (the trivia after it is no part of the node)
    hop_whitespace(&p);

    Span name = ret->key.index->name;
    bool reported = true;
    if (count != 1) {
        // (At the whole signature for `[]`: the span is finished below)
        if (count) grammar_error(&p, name, 1096, "An index signature must have exactly one parameter.");
    } else {
        if (span_valid(trailing_comma)) {
            grammar_error(&p, trailing_comma, 1025, "An index signature cannot have a trailing comma.");
        }
        if (span_valid(first_rest)) {
            grammar_error(&p, first_rest, 1017, "An index signature cannot have a rest parameter.");
        } else if (span_valid(first_modifier_name)) {
            grammar_error(&p, name, 1018, "An index signature parameter cannot have an accessibility modifier.");
        } else if (span_valid(first_question)) {
            grammar_error(&p, first_question, 1019, "An index signature parameter cannot have a question mark.");
        } else if (first_has_initializer) {
            grammar_error(&p, name, 1020, "An index signature parameter cannot have an initializer.");
        } else if (!first_has_type) {
            grammar_error(&p, name, 1022, "An index signature parameter must have a type annotation.");
        } else {
            reported = false;
        }
    }
    ret->key.index->valid = count == 1 && first_has_type;
    ret->key.index->grammar_reported = reported;
    ret->key.span = span_make(p.shared->source, a.start, bracket_end);

    if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);
        ret->key.index->has_value_type = true;
    }

    // The node ends after the member's `;` or `,` (tsc's parseTypeMemberSemicolon)
    Span node = ret->key.index->has_value_type ? span_from(a) : ret->key.span;
    {
        Parser q = parser(&p);
        hop_whitespace(&q);
        if (*q.pos == ';' || *q.pos == ',') node = span_make(q.shared->source, a.start, q.pos + 1);
        parse_fail(&q);
    }
    ret->key.index->node = node;
    if (count == 0) grammar_error(&p, node, 1096, "An index signature must have exactly one parameter.");

    return parse_success(&p);
}

static void expect_annotation_type(Parser* parent, char const* start, TypeNode* type);

static bool parse_expr_interface_member(Parser* parent, bool is_type, FunctionFlags flags, InterfaceMember* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    *ret = (InterfaceMember){ .key.tag = INTERFACE_KEY_EXPR };

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr expr;
    p.shared->in_property_name++;
    bool saved_in = allow_in_enter(&p);
    // (A missing expression is TS1109 and the member goes on, tsc's parseComputedPropertyName: `{ a: 1; [ }`)
    bool has_key = expect_expr(&p, expr_ctx_null, &expr) || (expr.tag == EXPR_IMPLICIT_ANY && expr.missing);
    // (TS1171 is the checker's: after TS1166-TS1170, interface_member_typecheck())
    if (has_key && !(expr.tag == EXPR_IMPLICIT_ANY && expr.missing)) parse_computed_key_comma(&p, &expr);
    allow_in_leave(&p, saved_in);
    p.shared->in_property_name--;
    if (!has_key) {
        return parse_fail(&p);
    }
    ret->key.expr = ast_new(Expr, expr);
    Parser before_close = p;
    hop_whitespace(&p);

    if (!expect_char(&p, ']')) {
        // (Without its `]` the name ends with its expression, before the trivia: tsc's finishNode)
        ret->key.span = span_from(a);
        if (span_end_ptr(ret->key.span) > before_close.pos) ret->key.span.size = (i32)(before_close.pos - a.start);
    } else {
        ret->key.span = span_from(a);
    }
    hop_whitespace(&p);

    if (hop_char(&p, '?')) {
        ret->optional = true;
        hop_whitespace(&p);
    }

    bool (*parse_function_fn)(Parser* parent, FunctionFlags flags, FunctionImpl* ret) =
        is_type ? parse_function_header : parse_non_arrow_function;

    FunctionImpl func;
    if (parse_function_fn(&p, flags, &func)) {
        ret->type = function_impl_into_type(func);
        ret->is_method = true;
    } else if (hop_char(&p, ':')) {
        hop_whitespace(&p);
        // (A property signature's annotation may be `unique symbol`: tsc's checkGrammarTypeOperatorNode, TS1330 unless
        // it is readonly, see the caller)
        if (is_type && (*p.pos == 'u' || *p.pos == '(')) expect_annotation_type(&p, p.pos, &ret->type);
        else expect_type(&p, TYPE_PRECEDENCE_NULL, &ret->type);
    } else {
        ret->implicit_any = true; // (TS7008 under noImplicitAny: `{ [k] }`)
    }
    if (is_type && !ret->is_method) {
        // An initializer is parsed (tsc's parsePropertyOrMethodSignature), the grammar check says TS1246/TS1247
        // (see the type literal loop, which knows which)
        Parser init = parser(&p);
        hop_whitespace(&init);
        Expr value;
        if (parse_initialization(&init, expr_ctx_null, &value)) {
            parse_success(&init);
            interface_member_accessor(ret)->initializer = ast_new(Expr, value);
        } else {
            parse_fail(&init);
        }
    }

    return parse_success(&p);
}

static bool parse_class_item_inner(Parser* parent, bool is_type, bool is_abstract, bool decorated, ClassItem* ret);

// In a type: `new` directly followed by a parameter or type parameter list
static bool is_construct_signature_start(const char* pos) {
    if (strncmp(pos, "new", 3) != 0) return false;
    pos += 3;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    return *pos == '(' || *pos == '<';
}

// The `[k]` of a class member, a type member or an object literal's member
// tsc's parseComputedPropertyName takes a whole expression, a comma expression too (checkGrammarComputedPropertyName:
// TS1171 on it, the checker's after the member's other grammar checks: class_computed_key_typecheck(),
// interface_member_typecheck(), object_check_grammar(), never in a destructuring assignment)
static void parse_computed_key_comma(Parser* p, Expr* key) {
    Parser comma = parser(p);
    hop_whitespace(&comma);
    bool is_comma = *comma.pos == ',';
    parse_fail(&comma);
    if (!is_comma) return;
    parse_comma_ops_right_side(p, key);
}

static bool parse_class_computed_key(Parser* parent, Expr** ret) {
    Parser p = parser(parent);
    if (*p.pos != '[' || is_index_signature_start(&p)) return parse_fail(&p);
    p.pos++;
    hop_whitespace(&p);
    Expr key;
    p.shared->in_property_name++;
    bool saved_in = allow_in_enter(&p);
    bool has_key = parse_expr(&p, expr_ctx_null, &key);
    if (has_key) parse_computed_key_comma(&p, &key);
    allow_in_leave(&p, saved_in);
    p.shared->in_property_name--;
    if (!has_key) return parse_fail(&p);
    hop_whitespace(&p);
    if (!hop_char(&p, ']')) {
        expr_drop(&key);
        return parse_fail(&p);
    }
    *ret = ast_new(Expr, key);
    return parse_success(&p);
}

// A class member's JSDoc: a property's `@type`, a method's (or a function-valued property's) `@param`s and `@returns`
// `@private`, `@protected`, `@readonly` of the host's own comment
static uint8_t jsdoc_modifiers(VecJsDocTag const* tags) {
    uint8_t ret = 0;
    for_each (i, vec_jsdoc_tag, tags) {
        if (i->earlier) continue;
        if (i->kind == JSDOC_TAG_PRIVATE) ret |= JSDOC_MODIFIER_PRIVATE;
        if (i->kind == JSDOC_TAG_PROTECTED) ret |= JSDOC_MODIFIER_PROTECTED;
        if (i->kind == JSDOC_TAG_READONLY) ret |= JSDOC_MODIFIER_READONLY;
        if (i->kind == JSDOC_TAG_OVERRIDE) ret |= JSDOC_MODIFIER_OVERRIDE;
    }
    return ret;
}

static void jsdoc_apply_class_item(Parser* p, const char* start, ClassItem* item) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before_leading(p, start, &tags)) return;
    // The modifier tags add to the written modifiers (tsc's getEffectiveModifierFlags)
    uint8_t modifiers = jsdoc_modifiers(&tags);
    if (item->access == ACCESS_MODIFIER_PUBLIC) {
        if (modifiers & JSDOC_MODIFIER_PRIVATE) item->access = ACCESS_MODIFIER_PRIVATE;
        else if (modifiers & JSDOC_MODIFIER_PROTECTED) item->access = ACCESS_MODIFIER_PROTECTED;
    }
    if (item->tag == CLASS_ITEM_FIELD && (modifiers & JSDOC_MODIFIER_READONLY)) item->field.is_readonly = true;
    if (modifiers & JSDOC_MODIFIER_OVERRIDE) item->is_override = true;
    if (span_valid(class_item_private_name(item))) {
        // (tsc's checkJSDocAccessibilityModifiers: the whole tag, up to the next one or the end of the comment)
        for_each (i, vec_jsdoc_tag, &tags) {
            if (i->earlier || (i->kind != JSDOC_TAG_PUBLIC && i->kind != JSDOC_TAG_PRIVATE && i->kind != JSDOC_TAG_PROTECTED)) continue;
            const char* at = i->tag_name.buf - 1;
            const char* end = i + 1 < tags.data + tags.size && i[1].comment == i->comment ? i[1].tag_name.buf - 1 : strstr(at, "*/");
            if (!end) continue;
            jsdoc_error(p, span_make(p->shared->source, at, end), 18010,
                "An accessibility modifier cannot be used with a private identifier.");
        }
    }
    if ((modifiers & JSDOC_MODIFIER_READONLY) && item->tag == CLASS_ITEM_CONSTRUCTOR) item->jsdoc_readonly = true;
    if ((modifiers & JSDOC_MODIFIER_READONLY) && (item->tag == CLASS_ITEM_CONSTRUCTOR || item->tag == CLASS_ITEM_METHOD)) {
        // (tsgo's reparser makes the tag a modifier, checkGrammarModifiers' TS1024 is at it: the whole tag)
        for_each (i, vec_jsdoc_tag, &tags) {
            if (i->earlier || i->kind != JSDOC_TAG_READONLY) continue;
            const char* at = i->tag_name.buf - 1;
            const char* end = i + 1 < tags.data + tags.size && i[1].comment == i->comment ? i[1].tag_name.buf - 1 : strstr(at, "*/");
            if (!end) continue;
            jsdoc_error(p, span_make(p->shared->source, at, end), 1024,
                "'readonly' modifier can only appear on a property declaration or index signature.");
            break;
        }
    }
    switch (item->tag) {
        case CLASS_ITEM_CONSTRUCTOR: {
            // (tsc's checkGrammarConstructorTypeAnnotation: the effective return type node)
            JsDocTag* return_tag = jsdoc_find_typed(&tags, JSDOC_TAG_RETURNS);
            if (return_tag && !item->constructor.func.ret_explicit) {
                jsdoc_error(p, return_tag->type_span, 1093, "Type annotation cannot appear on a constructor declaration.");
            }
            p->shared->jsdoc_declaration = true;
            jsdoc_apply_function(p, &tags, &item->constructor.func);
        }   break;
        case CLASS_ITEM_METHOD:
            item->method.func.accessor = (uint8_t)item->method.accessor; // (Set again by the resolve walk)
            p->shared->jsdoc_declaration = true;
            jsdoc_apply_function(p, &tags, &item->method.func);
            break;
        case CLASS_ITEM_FIELD: {
            if (!item->field.has_explicit_type) {
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                if (tag) {
                    item->field.explicit_type = jsdoc_take_type(tag, &item->field.explicit_type_span);
                    item->field.has_explicit_type = true;
                }
            }
            FunctionImpl* function = jsdoc_function_of(&item->field.value);
            if (function) jsdoc_apply_function(p, &tags, function);
        }   break;
        default: break;
    }
    vec_jsdoc_tag_drop(&tags);
}

static bool parse_class_item(Parser* parent, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    VecExpr decorators = {0};
    if (!is_type) {
        ignore parse_decorators(&p, &decorators);
    }
    if (!parse_class_item_inner(&p, is_type, is_abstract, !vec_expr_is_empty(&decorators), ret)) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    ret->decorators = decorators;
    ret->span = span_from(a);
    if (p.shared->source->is_js) jsdoc_apply_class_item(&p, a.start, ret);
    return parse_success(&p);
}

static bool parse_class_item_inner(Parser* parent, bool is_type, bool is_abstract, bool decorated, ClassItem* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    ModifierList modifiers;
    // (A type member's `const` isn't one: tsc's parseTypeMember has no permitConstAsModifier)
    p.shared->modifiers_in_type = is_type;
    bool has_modifiers = parse_modifier_list_ex(&p, true, !is_type, &modifiers);
    p.shared->modifiers_in_type = false;
    if (!has_modifiers) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    FunctionFlags function_flags = { .is_async = modifiers.includes[MODIFIER_ASYNC] };
    bool is_getter = false, is_setter = false;
    // (An accessor is never async nor a generator to tsc, whatever its modifiers say: getFunctionFlags, and its body
    // is parsed without the await context)
    if (hop_member_keyword(&p, "get", true)) {
        is_getter = true;
        function_flags.brace_expected = true;
        function_flags.is_async = false;
    } else if (hop_member_keyword(&p, "set", true)) {
        is_setter = true;
        function_flags.brace_expected = true;
        function_flags.is_async = false;
    } else if (!is_type && hop_char(&p, '*')) {
        function_flags.is_generator = true;
        function_flags.asterisk = span_from_previous_char(&p);
        function_flags.committed = true;
        hop_whitespace(&p);
    }

    Span name_span;
    cstr name_string;
    bool name_is_value;
    bool missing_name = false;
    Expr* computed_key = nullptr;
    Anchor name_anchor = anchor(&p);
    if (hop_private_name(&p)) {
        // Private name, the `#` is part of the name (in a type too, TS18016 from the checker)
        name_span = span_from(name_anchor);
        name_string = identifier_name_value(span_sv(name_span));
        name_is_value = false;
    } else if (is_type && is_construct_signature_start(p.pos)) {
        // `new (...): T` / `new <T>(...): T` is a construct signature, not a method called `new`
        name_span = span_null;
        name_string = cstr_null;
        name_is_value = false;
    } else if (!is_type && parse_class_computed_key(&p, &computed_key)) {
        // `[k]: T = x`, `[k]() {}`: the name is the key's source text, see ClassItem
        // (A literal key is bound early by its value, `["kk"]` is `kk`'s namesake: tsc's getDeclarationName)
        name_span = span_from(name_anchor);
        name_string = cstr_init();
        if (!computed_key_early_name(computed_key, &name_string)) name_string = cstr_from_span(name_span);
        name_is_value = false;
    } else if (!parse_name_or_value(&p, &name_span, &name_string, &name_is_value)) {
        name_span = span_null;
        name_string = cstr_null;
        name_is_value = false;
        if (function_flags.committed || ((is_getter || is_setter) && !is_type)) {
            // `*` then no name: tsc's parsePropertyName has a missing identifier (TS1003) and it's a method
            parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
            // (tsc's missing node sits at the token's full start, before the trivia)
            name_span = span_make(p.shared->source, token_full_start(&p), token_full_start(&p));
            name_string = cstr_init();
        } else if (is_type && !is_getter && !is_setter && !vec_spanned_modifier_is_empty(&modifiers.spanned) &&
            *p.pos != '[' && *p.pos != '(' && *p.pos != '<'
        ) {
            // `static` then a line break and `{`: tsc's isTypeMemberStart takes the modifier before what can end the
            // member, and parsePropertyName's name is missing (TS1003)
            parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
            name_span = span_make(p.shared->source, token_full_start(&p), token_full_start(&p));
            name_string = cstr_init();
            missing_name = true;
        } else if (!is_type && !is_getter && !is_setter && (decorated || !vec_spanned_modifier_is_empty(&modifiers.spanned)) &&
            *p.pos != '[' && *p.pos != '(' && *p.pos != '<'
        ) {
            // `public {}`, `@dec }`: tsc's parseClassElement, a property whose name is missing after the decorators and
            // modifiers (TS1146 where it would be, before the trivia)
            Parser missing = parser(&p);
            unhop_trivia(&missing);
            parse_error(&missing, span_from_pos(&missing), 1146, "Declaration expected.");
            name_span = span_from_pos(&missing);
            parse_success(&missing);
            name_string = cstr_init();
            missing_name = true;
        }
    }
    // A member with the `default` modifier (TS1070, TS1029) is bound as `default` whatever its name (tsc's
    // declareSymbol: a default export's name)
    if (modifiers.includes[MODIFIER_EXPORT_DEFAULT] && span_valid(name_span) && !computed_key) {
        cstr_drop(&name_string);
        name_string = cstr_lit("default");
        name_is_value = false;
    }
    hop_whitespace(&p);
    // (tsc's parseAccessorDeclaration: after `get`/`set` and a name the parameter list is committed)
    if (is_getter || is_setter) function_flags.committed = true;

    // (An accessor in a type may have a body: tsc's parseAccessorDeclaration, TS1183 from checkGrammarAccessor)
    bool (*parse_function_fn)(Parser* parent, FunctionFlags flags, FunctionImpl* ret) =
        is_type ? (is_getter || is_setter ? parse_type_accessor : parse_function_header) : parse_non_arrow_function;

    // A static block is no yield context (tsc's parseClassStaticBlockBody)
    bool outer_static_block = p.shared->in_static_block, outer_generator = p.shared->in_generator;
    p.shared->in_static_block = true;
    p.shared->in_generator = false;
    // (Spelled with escapes it is the keyword all the same, TS1260: tsc's scanner)
    char cooked_buf[16];
    Span cooked_name = name_span;
    bool escaped_name = span_valid(name_span) && __builtin_expect(p.shared->any_escaped_word, 0) &&
        cook_escaped_word(&cooked_name, cooked_buf);
    bool static_block = !is_type && !name_is_value && span_equals(cooked_name, "static") &&
        parse_block(&p, false, &ret->static_block);
    if (static_block && escaped_name) parse_error(&p, name_span, 1260, "Keywords cannot contain escape characters.");
    p.shared->in_static_block = outer_static_block;
    p.shared->in_generator = outer_generator;
    if (static_block) {
        check_modifiers(&p, &modifiers, (ModifiedNode){ .kind = MODIFIED_CLASS_STATIC_BLOCK, .parent = MODIFIED_IN_CLASS });
        ret->tag = CLASS_ITEM_STATIC_BLOCK;
    } else if (span_valid(name_span)) {
        // (Not for `""`, a string literal whose value is empty)
        if (span_valid(name_span) && cstr_is_empty(&name_string) && !name_is_value && !computed_key) {
            name_string = cstr_from_span(name_span);
        }

        bool optional = false;
        Span question = span_null;
        Span definite = span_null;
        if (hop_char(&p, '?')) {
            question = span_from_previous_char(&p);
            hop_whitespace(&p);
            optional = true;
            if (!is_type) js_syntax_error(&p, question, 8009, "The '?' modifier can only be used in TypeScript files.");
        } else if (!is_type && *p.pos == '!' && p.pos[1] != '=') {
            // Definite assignment assertion: see the field's grammar check below
            definite = span_make(p.shared->source, p.pos, p.pos + 1);
            p.pos++;
            hop_whitespace(&p);
        }

        FunctionImpl func;
        ModifiedNode modified = {
            .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
            .in_abstract_class = is_abstract,
            .private_name = *span_start_ptr(name_span) == '#',
            .name = name_span,
        };
        // (`"constructor"()` is one too: tsc's tryParseConstructorDeclaration takes a string literal followed by `(`)
        bool quoted_constructor = !is_type && name_is_value && name_span.size &&
            (name_span.buf[0] == '"' || name_span.buf[0] == '\'') && cstr_equals(&name_string, "constructor") &&
            !optional && !span_valid(definite) && *p.pos == '(';
        // (Not `*constructor()` nor `get constructor()`: tsc's parseClassElement looks for accessors first and the
        // `*` belongs to a method, TS1368/TS1341)
        bool plain = !is_getter && !is_setter && !function_flags.is_generator;
        // (Spelled with escapes it is the keyword all the same: tsc's parseExpected(ConstructorKeyword), TS1260)
        bool escaped_constructor = !is_type && plain && !name_is_value && name_span.size > 11 &&
            memchr(name_span.buf, '\\', (size_t)name_span.size) && tok_is_escaped_keyword(
                (Tok){ .start = name_span.buf, .end = name_span.buf + name_span.size }, "constructor");
        if (escaped_constructor) parse_error(&p, name_span, 1260, "Keywords cannot contain escape characters.");
        if (!is_type && plain && ((!name_is_value && (span_equals(name_span, "constructor") || escaped_constructor)) || quoted_constructor)) {
            modified.kind = MODIFIED_CONSTRUCTOR;
            check_modifiers(&p, &modifiers, modified);
            Span keyword = name_span;
            // From the modifiers to the keyword (tsc's getErrorSpanForNode of a constructor)
            name_span = span_from(a);
            if (span_valid(keyword) && span_end_ptr(keyword) >= span_start_ptr(name_span)) {
                name_span = span_make_in(name_span, span_start_ptr(name_span), span_end_ptr(keyword));
            }
            if (!parse_function_fn(&p, (FunctionFlags){ .committed = true }, &func)) {
                parse_error(&p, span_from_token(&p), TS_CODE_NONE, "constructor expected");
                func = (FunctionImpl){0};
            } else if (!func.has_body) {
                // (The whole of it: tsgo's checkJSSyntax, which checks what's inside too)
                js_syntax_error(&p, span_with_semicolon(span_from(a)), 8017, "Signature declarations can only be used in TypeScript files.");
            }
            func.full_span = span_from(a);
            func.is_constructor = true;
            *ret = (ClassItem){
                .tag = CLASS_ITEM_CONSTRUCTOR,
                .constructor = {
                    .name_span = name_span,
                    .func = func,
                }
            };
        } else if (!span_valid(definite) && parse_function_fn(&p, function_flags, &func)) {
            // (Not after a `!`: tsc's parsePropertyOrMethodDeclaration takes a method only right after the name (or its
            // `?`), `a!()` is a property whose `(` is TS1441)
            modified.kind = is_getter ? MODIFIED_GET_ACCESSOR :
                is_setter ? MODIFIED_SET_ACCESSOR :
                is_type ? MODIFIED_METHOD_SIGNATURE :
                MODIFIED_METHOD;
            bool modifier_reported = check_modifiers(&p, &modifiers, modified);
            if (is_type && func.has_body) {
                // (The body: from the `{` after the parameters and the return type to the end)
                char const* at = span_valid(function_impl_ret_span(&func)) && span_end_ptr(function_impl_ret_span(&func)) > span_end_ptr(function_impl_args_span(&func)) ?
                    span_end_ptr(function_impl_ret_span(&func)) : span_end_ptr(function_impl_args_span(&func));
                while (at < p.pos && *at != '{') at++;
                grammar_error(&p, span_make(p.shared->source, at, p.pos), 1183, "An implementation cannot be declared in ambient contexts.");
            }
            if (is_type && modified.kind == MODIFIED_METHOD_SIGNATURE) {
                js_syntax_function(&p, &func, false);
            } else if (!is_type && !func.has_body) {
                js_syntax_error(&p, span_with_semicolon(span_from(a)), 8017, "Signature declarations can only be used in TypeScript files.");
            }
            func.is_abstract_member = modifiers.includes[MODIFIER_ABSTRACT];
            *ret = (ClassItem){
                .tag = CLASS_ITEM_METHOD,
                .method = {
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .is_abstract = modifiers.includes[MODIFIER_ABSTRACT],
                    .is_declare = modifiers.includes[MODIFIER_DECLARE],
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .is_optional = optional,
                    .name_is_value = name_is_value,
                    .computed_key = computed_key,
                    .func = func,
                    .accessor = is_getter ? CLASS_ACCESSOR_GET : (is_setter ? CLASS_ACCESSOR_SET : CLASS_ACCESSOR_NONE),
                },
                .modifiers_reported = modifier_reported,
            };
        } else {
            bool is_definitely_field = false;

            modified.kind = is_type ? MODIFIED_PROPERTY_SIGNATURE : MODIFIED_PROPERTY;
            bool modifier_reported = check_modifiers(&p, &modifiers, modified);
            if (!modifier_reported && modifiers.includes[MODIFIER_ACCESSOR] && optional) {
                // (tsc's checkGrammarProperty)
                grammar_error(&p, question, 1276, "An 'accessor' property cannot be declared optional.");
            }
            // (tsc's checkGrammarProperty: a class field named by the string "constructor")
            if (!is_type && !computed_key && name_span.size == 13 && (name_span.buf[0] == '"' || name_span.buf[0] == '\'') &&
                !memcmp(name_span.buf + 1, "constructor", 11)
            ) {
                grammar_error(&p, name_span, 18006, "Classes may not have a field named 'constructor'.");
            }
            *ret = (ClassItem){
                .tag = CLASS_ITEM_FIELD,
                .field = {
                    .is_readonly = modifiers.includes[MODIFIER_READONLY],
                    .is_declare = modifiers.includes[MODIFIER_DECLARE],
                    .is_abstract = modifiers.includes[MODIFIER_ABSTRACT],
                    .is_auto_accessor = modifiers.includes[MODIFIER_ACCESSOR],
                    .is_static = modifiers.includes[MODIFIER_STATIC],
                    .is_optional = optional,
                    .is_definite = span_valid(definite),
                    .name_string = cstr_move(&name_string),
                    .name_span = name_span,
                    .name_is_value = name_is_value,
                    .computed_key = computed_key,
                },
                .modifiers_reported = modifier_reported,
            };

            Binding binding = { .content_span = name_span }; // (Only for the file of the type's span)
            p.shared->unique_annotation_next = true;
            bool has_type = parse_binding_type_ex(&p, &binding, !is_type, &ret->field.explicit_type);
            p.shared->unique_annotation_next = false;
            if (has_type && is_type && ret->field.explicit_type.tag == TYPE_UNIQUE_SYMBOL && !modifiers.includes[MODIFIER_READONLY]) {
                // (tsc's checkGrammarTypeOperatorNode for a PropertySignature; a class property's is TS1331)
                grammar_error(&p, name_span, 1330, "A property of an interface or type literal whose type is a 'unique "
                    "symbol' type must be 'readonly'.");
            }
            if (has_type) {
                ret->field.explicit_type_span = binding_explicit_type_span(&binding);
                ret->field.has_explicit_type = binding.has_explicit_type;
                is_definitely_field = true;
                hop_whitespace(&p);
            }

            // An initializer is outside the yield and await contexts, a static block's too (tsc's parsePropertyDeclaration)
            FunctionCtx outer = { .in_async = p.shared->in_async, .in_generator = p.shared->in_generator,
                .in_static_block = p.shared->in_static_block, .in_parameters = p.shared->in_parameters };
            p.shared->in_async = p.shared->in_generator = p.shared->in_static_block = false;
            // (A `declare` property is parsed in an ambient context, tsc's parseClassElement: `declare p = async () => {}`
            // is TS1040)
            bool outer_ambient = p.shared->in_ambient;
            p.shared->in_ambient |= ret->field.is_declare;
            bool saved_in = allow_in_enter(&p);
            bool initialized = parse_initialization_or_set_to_default(&p, expr_ctx_null, &ret->field.value);
            allow_in_leave(&p, saved_in);
            p.shared->in_ambient = outer_ambient;
            p.shared->in_async = outer.in_async;
            p.shared->in_generator = outer.in_generator;
            p.shared->in_static_block = outer.in_static_block;
            if (initialized) {
                mark_named_evaluation(&ret->field.value, ret->field.computed_key);
                is_definitely_field = true;
                hop_whitespace(&p);
            }
            // tsc's checkGrammarProperty
            if (span_valid(definite) && !modifier_reported && (!ret->field.has_explicit_type || initialized ||
                p.shared->in_ambient || ret->field.is_declare || ret->field.is_static || ret->field.is_abstract)
            ) {
                if (initialized) {
                    grammar_error(&p, definite, 1263, "Declarations with initializers cannot also have definite assignment assertions.");
                } else if (!ret->field.has_explicit_type) {
                    grammar_error(&p, definite, 1264, "Declarations with definite assignment assertions must also have type annotations.");
                } else {
                    grammar_error(&p, definite, 1255, "A definite assignment assertion '!' is not permitted in this context.");
                }
            }

            if (!is_type && *p.pos == '(') {
                // tsc's parseSemicolonAfterPropertyName: `a: super();`
                parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1441,
                    "Cannot start a function call in a type annotation.");
                p.pos++;
                hop_whitespace(&p);
            } else if (p.whitespace != WHITESPACE_NEWLINE) {
                switch (*p.pos) {
                    case ';': case '}': case '\0': break;
                    case ',':
                        // (A type member's separator; a class property's is only `;`: tsc's
                        // parseSemicolonAfterPropertyName)
                        if (is_type) break;
                        goto missing_semicolon;
                    case '@':
                        // tsc's parseSemicolonAfterPropertyName: `public @dec m()` is a property `public`
                        if (!is_type) {
                            parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1436,
                                "Decorators must precede the name and all keywords of property declarations.");
                            break;
                        }
                        // fallthrough
                    default:
                    missing_semicolon:
                        if (missing_name) {
                            parse_error(&p, span_from_token(&p), 1005, "';' expected.");
                        } else if (is_definitely_field) {
                            if (is_type) {
                                expect_char(&p, ';');
                            } else {
                                // (tsc's parseSemicolonAfterPropertyName: after an initializer, a missing `;`)
                                if (!hop_char(&p, ';')) {
                                    if (initialized) parse_error(&p, span_from_token(&p), 1005, "';' expected.");
                                    else parse_error(&p, span_from_token(&p), 1442, "Expected '=' for property initializer.");
                                }
                            }
                        } else if (span_equals(name_span, "function")) {
                            modifier_list_drop(&modifiers);
                            class_item_drop(ret);
                            return parse_fail(&p);
                        } else if (span_equals(name_span, "let") || span_equals(name_span, "var")) {
                            if (vec_spanned_modifier_size(&modifiers.spanned)) {
                                // If we have modifiers
                                parse_error(&p, name_span, 1440, "Variable declaration not allowed at this location.");
                            } else {
                                modifier_list_drop(&modifiers);
                                return parse_fail(&p);
                            }
                        } else if (ret->field.computed_key || !name_span.size ||
                            !(isalpha((unsigned char)*name_span.buf) || *name_span.buf == '_' || *name_span.buf == '$' ||
                                *name_span.buf == '\\' || (unsigned char)*name_span.buf >= 0x80)
                        ) {
                            // (Of a name that isn't an identifier, tsc's parseErrorForMissingSemicolonAfter only says
                            // ';' expected)
                            parse_error(&p, span_from_token(&p), 1005, "';' expected.");
                        } else if (is_type) {
                            // (A type member's is tsc's parseTypeMemberSemicolon, a plain parseSemicolon: no
                            // parseErrorForMissingSemicolonAfter, `interface X { default p: number }`)
                            parse_error(&p, span_from_token(&p), 1005, "';' expected.");
                        } else {
                            report_missing_semicolon_after_identifier(&p, name_span);
                        }
                }
            }
        }
        ret->is_override = modifiers.includes[MODIFIER_OVERRIDE];
        ret->access = modifiers.includes[MODIFIER_PRIVATE] ?
            ACCESS_MODIFIER_PRIVATE :
            (modifiers.includes[MODIFIER_PROTECTED] ?
                ACCESS_MODIFIER_PROTECTED :
                ACCESS_MODIFIER_PUBLIC);
    } else {
        FunctionImpl func;
        if ((!p.shared->no_mapped_member && !is_getter && !is_setter && parse_mapped_interface_member(&p, &ret->interface_memeber)) ||
            parse_index_signature(&p, &ret->interface_memeber) ||
            parse_expr_interface_member(&p, is_type, function_flags, &ret->interface_memeber)
        ) {
            // (A class's decorated index signature is TS1206 alone, which ends tsc's checkGrammarModifiers: class_decl.c)
            bool decorators_invalid = decorated && !is_type && ret->interface_memeber.key.tag == INTERFACE_KEY_INDEX;
            if (ret->interface_memeber.key.tag != INTERFACE_KEY_MAPPED && !decorators_invalid) {
                check_modifiers(&p, &modifiers, (ModifiedNode){
                    .kind = ret->interface_memeber.key.tag == INTERFACE_KEY_INDEX ? MODIFIED_INDEX_SIGNATURE :
                        ret->interface_memeber.key.tag == INTERFACE_KEY_CALLABLE ? MODIFIED_METHOD_SIGNATURE :
                        MODIFIED_PROPERTY_SIGNATURE,
                    .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
                });
            }
            if (!is_type && ret->interface_memeber.key.tag == INTERFACE_KEY_INDEX) {
                // (A class's index signature is a signature declaration, the whole of it: tsgo's checkJSSyntax.
                // Its parameter isn't checked, tsc parses it like a type's)
                Span node = ret->interface_memeber.key.index->node;
                js_syntax_error(&p, span_make(p.shared->source, a.start, span_end_ptr(node)), 8017,
                    "Signature declarations can only be used in TypeScript files.");
            }
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber.declaration = span_from(a);
            ret->interface_memeber.readonly = modifiers.includes[MODIFIER_READONLY];
            ret->interface_member_static = modifiers.includes[MODIFIER_STATIC];
            if (is_type && ret->interface_memeber.key.tag == INTERFACE_KEY_EXPR && !ret->interface_memeber.is_method &&
                ret->interface_memeber.type.tag == TYPE_UNIQUE_SYMBOL && !ret->interface_memeber.readonly
            ) {
                grammar_error(&p, ret->interface_memeber.key.span, 1330, "A property of an interface or type literal whose "
                    "type is a 'unique symbol' type must be 'readonly'.");
            }
            // `readonly [K in T]: X` (the modifier went to the list before the key was known to be mapped)
            InterfaceMember* member = &ret->interface_memeber;
            if (is_type && (is_getter || is_setter) && member->key.tag == INTERFACE_KEY_EXPR && member->is_method &&
                member->type.tag == TYPE_CALLABLE
            ) {
                // `get [k](): T` in a type is a property of type T, `set [k](v: T)` one of type T to write
                FunctionImpl* accessor = member->type.callable->function;
                TypeNode type = type_any;
                if (is_getter) {
                    if (accessor->ret_explicit) type = type_move(&accessor->ret);
                } else if (vec_function_arg_size(&accessor->args)) {
                    FunctionArg* arg = &accessor->args.data[0];
                    if (arg->initializer.binding.tag == BINDING_IDENTIFIER) {
                        interface_member_accessor(member)->setter_param = arg->initializer.binding.content_span;
                    }
                    if (arg->initializer.binding.has_explicit_type) type = binding_take_type(&arg->initializer.binding);
                }
                type_drop(&member->type);
                member->type = type;
                member->is_method = false;
                member->accessor_kind = is_getter ? 1 : 2;
            }
            if (member->key.tag == INTERFACE_KEY_MAPPED && member->readonly && !member->key.mapped->readonly) {
                member->key.mapped->readonly = 1;
            }
        } else if (parse_signature_args(&p, is_type, &func)) {
            if (is_type) signature_arrow_return_type(&p, &func);
            if (!span_valid(func.full_span)) func.full_span = span_from(a); // (What function_impl_typecheck_signature() checks)
            check_modifiers(&p, &modifiers, (ModifiedNode){
                .kind = MODIFIED_METHOD_SIGNATURE,
                .parent = is_type ? MODIFIED_IN_TYPE : MODIFIED_IN_CLASS,
            });
            ret->tag = CLASS_ITEM_INTERFACE_MEMBER;
            ret->interface_memeber = (InterfaceMember){
                .declaration = span_from(a),
                .readonly = modifiers.includes[MODIFIER_READONLY],
                .key.tag = INTERFACE_KEY_CALLABLE,
                .type = function_impl_into_type(func),
            };
        } else {
            modifier_list_drop(&modifiers);
            cstr_drop(&name_string);
            return parse_fail(&p);
        }

        hop_whitespace(&p);
        if (p.whitespace != WHITESPACE_NEWLINE && *p.pos != ';' && *p.pos != ',' && *p.pos != '}' && !at_eof(&p, p.pos)) {
            expect_char(&p, ';');
        }
    }
    cstr_drop(&name_string);

    modifier_list_drop(&modifiers);
    ret->span = span_from(a);
    return parse_success(&p);
}

// A class or type member where tsc's parseList would try one: its isClassMemberStart/isTypeMemberStart (`class C2 {`
// or `return 1;` in a class body aren't members, where parse_class_item() would make a field of the keyword). The
// predicate is only asked about members that parsed with errors, which keeps it off the path of valid code
static bool parse_list_member(Parser* parent, ParsingContext ctx, bool is_type, bool is_abstract, ClassItem* ret) {
    Parser p = parser(parent);
    u32 errors_before = p.shared->errors_attempted;
    if (!parse_class_item(&p, is_type, is_abstract, ret)) return parse_fail(&p);
    if (p.shared->errors_attempted != errors_before && !list_is_element(&p, ctx, tok_at(&p, parent->pos, false))) {
        class_item_drop(ret);
        return parse_fail(&p);
    }
    return parse_success(&p);
}

// abstract: the statement's modifiers had `abstract` (an expression's `abstract class` is taken here)
[[gnu::noinline]] static bool parse_class_decl_slow(Parser* parent, bool abstract, ClassDecl* ret);
static inline bool parse_class_decl(Parser* parent, bool abstract, ClassDecl* ret);

// `@dec public class {}`, `@dec export default class {}` in an expression: tsc's parseDecoratedExpression takes any
// modifiers after the decorators, and the class expression has them (its checkGrammarModifiers: TS1184 at the first,
// findFirstIllegalModifier; `abstract` still makes it abstract)
static bool parse_decorated_class_with_modifiers(Parser* parent, ClassDecl* ret) {
    Parser p = parser(parent);
    VecExpr decorators = {0};
    if (!parse_decorators(&p, &decorators)) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    ModifierList modifiers;
    parse_modifier_list(&p, false, &modifiers);
    hop_whitespace(&p);
    // (Its members are parsed as a non-abstract class's: an abstract one is TS1244, which tsc says unless the parent
    // is an abstract class declaration)
    if (vec_spanned_modifier_is_empty(&modifiers.spanned) || !starts_with_followed_by_word_break(&p, "class") ||
        !parse_class_decl(&p, false, ret)
    ) {
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    vec_expr_drop(&ret->decorators);
    ret->decorators = decorators;
    ret->is_abstract |= modifiers.includes[MODIFIER_ABSTRACT];
    grammar_error(&p, vec_spanned_modifier_front(&modifiers.spanned)->span, 1184, "Modifiers cannot appear here.");
    modifier_list_drop(&modifiers);
    return parse_success(&p);
}
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_class_decl(Parser* parent, bool abstract, ClassDecl* ret) {
    // (Decorators, `abstract`, `class`, an escaped keyword)
    switch (*parent->pos) {
        case '@': case '\\': break;
        case 'a': if (!may_start_keyword(parent->pos, "abstract")) GATE_FAIL(parse_class_decl(&v_, abstract, ret)); break;
        case 'c': if (!may_start_keyword(parent->pos, "class")) GATE_FAIL(parse_class_decl(&v_, abstract, ret)); break;
        default: GATE_FAIL(parse_class_decl(&v_, abstract, ret));
    }
    return parse_class_decl_slow(parent, abstract, ret);
}

[[gnu::noinline]] static bool parse_class_decl_slow(Parser* parent, bool abstract, ClassDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    bool modifiers_failed = take_decl_modifiers_failed(&p);
    bool leading_decorators = p.shared->decl_leading_decorators;
    p.shared->decl_leading_decorators = false;

    VecExpr decorators = {0};
    ignore parse_decorators(&p, &decorators);
    // (Decorators both before and after `export`: TS8038, one of checkGrammarModifiers' complaints)
    modifiers_failed |= leading_decorators && !vec_expr_is_empty(&decorators);

    bool is_abstract = abstract;
    // (A modifier only with what follows on its line: `abstract` then a newline is an expression statement)
    Parser abstract_p = parser(&p);
    if (hop_start_if_ends_on_word_break(&abstract_p, "abstract")) {
        hop_whitespace(&abstract_p);
        if (abstract_p.whitespace != WHITESPACE_NEWLINE) {
            parse_success(&abstract_p);
            is_abstract = true;
        } else {
            parse_fail(&abstract_p);
        }
    } else {
        parse_fail(&abstract_p);
    }

    if (!hop_start_if_ends_on_word_break(&p, "class")) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    *ret = (ClassDecl){
        .decorators = decorators,
        .is_abstract = is_abstract,
        .extends = expr_implicit_any_no_span,
    };
    // `class extends A {}` and `class implements A {}` are anonymous, but `class implements {}` is called implements
    bool starts_with_implements_clause = false;
    if (starts_with_followed_by_word_break(&p, "implements")) {
        const char* c = p.pos + strlen("implements");
        while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
            c++;
        }
        starts_with_implements_clause = identifier_char_size(c, true);
    }
    // (tsc's isBindingIdentifier: `class void {}` has no name, and then no `{`)
    const char* name_start = p.pos;
    if (starts_with_followed_by_word_break(&p, "extends") ||
        starts_with_implements_clause ||
        !parse_binding_identifier(&p)
    ) {
        ret->name = span_null;
    } else {
        ret->name = span_make(p.shared->source, name_start, p.pos);
    }
    hop_whitespace(&p);

    // (checkGrammarClassLikeDeclaration: the heritage clauses' complaints first, then the type parameters' TS1098)
    intptr_t generic_mark = error_mark(&p);
    const char* generic_open = p.pos;
    Span empty_generic = span_null;
    if (parse_generic_decl(&p, TYPE_PARAM_OF_CLASS, &ret->generic)) {
        empty_generic = grammar_drop_since(&p, generic_mark, 1098, generic_open);
        hop_whitespace(&p);
        if (!generic_empty(&ret->generic)) {
            Span span = span_between(vec_generic_item_front(&ret->generic.items)->full_span,
                vec_generic_item_back(&ret->generic.items)->full_span);
            js_syntax_error(&p, span, 8004, "Type parameter declarations can only be used in TypeScript files.");
        }
    } else {
        ret->generic = (Generic){0};
    }

    HeritageClauses heritage = { .is_class = true, .base = expr_implicit_any_no_span, .modifiers_failed = modifiers_failed };
    parse_heritage_clauses(&p, &heritage);
    if (span_valid(empty_generic) && !heritage.stopped) {
        grammar_error(&p, empty_generic, 1098, "Type parameter list cannot be empty.");
    }
    ret->extends = heritage.base;
    ret->extends_clause = heritage.base_clause;
    ret->implements = heritage.types;
    heritage_take_invalid(&heritage, &ret->invalid_implements, &ret->invalid_implements_checked);
    ret->unchecked_extends = heritage.unchecked_exprs;
    ret->unchecked_implements = heritage.unchecked_types;

    if (!expect_char(&p, '{')) {
        ret->full_span = span_from(a);
        return parse_success(&p);
    }
    hop_whitespace(&p);

    ClassItem item;
    u32 saved_context = list_enter(&p, PARSING_CLASS_MEMBERS);
    while (!at_eof(&p, p.pos) && *p.pos != '}') {
        if (hop_char(&p, ';')) {
            hop_whitespace(&p);
            continue;
        }
        if (!parse_list_member(&p, PARSING_CLASS_MEMBERS, false, is_abstract, &item)) {
            if (list_abort_or_skip(&p, PARSING_CLASS_MEMBERS)) break;
            continue;
        }
        if (item.tag == CLASS_ITEM_CONSTRUCTOR) {
            const char* after_name = span_end_ptr(item.constructor.name_span);
            while (is_regex_space(*after_name)) after_name++;
            if (!generic_empty(&item.constructor.func.generic)) {
                GenericItem const* first = item.constructor.func.generic.items.data;
                GenericItem const* last = vec_generic_item_back(&item.constructor.func.generic.items);
                // (`@template`s: the reparsed list spans the tags)
                Span generics_span = first->jsdoc && span_valid(first->jsdoc_tag) && span_valid(last->jsdoc_tag) ?
                    span_between(first->jsdoc_tag, last->jsdoc_tag) : span_between(first->full_span, last->full_span);
                grammar_error(&p, generics_span, 1092, "Type parameters cannot appear on a constructor declaration.");
            } else if (*after_name == '<') {
                // (`constructor<>()`: tsc's checkGrammarConstructorTypeParameters, on the empty list after the `<`)
                grammar_error(&p, span_make(p.shared->source, after_name + 1, after_name + 1), 1092,
                    "Type parameters cannot appear on a constructor declaration.");
            }
            if (is_abstract) {
                item.constructor.func.is_abstract_constructor = true;
            }
        }
        if (item.tag == CLASS_ITEM_METHOD) ast_cstr_seal(&item.method.name_string);
        if (item.tag == CLASS_ITEM_FIELD) ast_cstr_seal(&item.field.name_string);
        list_push(vec_class_item, &ret->items, item);
        hop_whitespace(&p);

        if (at_eof(&p, p.pos) || *p.pos == '}') {
            break;
        }

        do {
            hop_whitespace(&p);
        } while (hop_char(&p, ';'));
    }
    list_leave(&p, saved_context);
    expect_char(&p, '}');
    shrink_list(vec_class_item, &ret->items);
    // The class's walk looks at the names its constructor declares when instance fields are initialized in it
    // (constructor_locals() in class_decl.c), and the type of a field without a type or initializer is what the
    // constructor's walk finds assigned to it (field_type_from_assignments()): its body isn't deferred then (plan 78)
    CompilerOptions const* options = compiler_options();
    bool standard_fields = options->use_define_for_class_fields && options->target >= SCRIPT_TARGET_ES2022;
    bool constructor_eager = false;
    for_each (i, vec_class_item, &ret->items) {
        if (i->tag != CLASS_ITEM_FIELD || i->field.is_static) continue;
        constructor_eager |= !standard_fields || (!i->field.has_explicit_type && i->field.value.tag == EXPR_IMPLICIT_ANY);
    }
    for_each (i, vec_class_item, &ret->items) {
        if (constructor_eager && i->tag == CLASS_ITEM_CONSTRUCTOR) undefer_body(&p, &i->constructor.func);
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

[[gnu::noinline]] static bool parse_call_args_slow(Parser* parent, VecExpr* args);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_call_args(Parser* parent, VecExpr* args) {
    if (*parent->pos != '(') GATE_FAIL(parse_call_args(&v_, args));
    return parse_call_args_slow(parent, args);
}

[[gnu::noinline]] static bool parse_call_args_slow(Parser* parent, VecExpr* args) {
    Parser p = parser(parent);

    if (!hop_start(&p, "(")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // tsc's parseDelimitedList(ArgumentExpressions)
    ScratchList list = scratch_list_begin();
    u32 saved_context = list_enter(&p, PARSING_ARGUMENT_EXPRESSIONS);
    bool saved_in = allow_in_enter(&p); // (tsgo's parseArgumentExpression)
    while (!at_eof(&p, p.pos) && *p.pos != ')' && *p.pos != ';') {
        const char* start = p.pos;
        Expr arg = {0};
        // (A lone `=>` starts no element: the list checks isStartOfExpression before parsing one)
        if ((p.pos[0] == '=' && p.pos[1] == '>') || !parse_expr_or_spread(&p, &arg)) {
            // (The predicate only after the parser said no: it's what the list does on valid code that costs)
            if (!list_is_element(&p, PARSING_ARGUMENT_EXPRESSIONS, tok_peek(&p))) {
                if (list_abort_or_skip(&p, PARSING_ARGUMENT_EXPRESSIONS)) break;
                continue;
            }
            expect_expr(&p, expr_ctx_null, &arg);
        }
        if (arg.tag == EXPR_SPREAD && arg.spread->tag == EXPR_ARRAY) arg.spread->spread_into_call = true;
        scratch_list_push(&list, &arg, sizeof(arg));
        hop_whitespace(&p);
        if (hop_char(&p, ',')) {
            hop_whitespace(&p);
            continue;
        }
        if (at_eof(&p, p.pos) || *p.pos == ')' || *p.pos == ';') break;
        expect_char(&p, ',');
        if (p.pos == start) hop_token(&p);
        hop_whitespace(&p);
    }
    allow_in_leave(&p, saved_in);
    list_leave(&p, saved_context);
    if (!expect_char(&p, ')')) {
        // The call ends with its last token, not the whitespace after it
        const char* open = parent->pos;
        while (p.pos > open + 1 && is_js_ascii_space(p.pos[-1])) p.pos--;
    }
    *args = (VecExpr){ .data = scratch_list_finish(&list, sizeof(Expr)), .size = list.count, .capacity = list.count };

    return parse_success(&p);
}

static bool parse_right_unary_op(Parser* parent, UnaryOp* op) {
    Parser p = parser(parent);

    if (hop_start(&p, "++")) {
        *op = UNARY_OP_RIGHT_PLUS_PLUS;
    } else if (hop_start(&p, "--")) {
        *op = UNARY_OP_RIGHT_MINUS_MINUS;
    } else if (hop_start(&p, "!")) {
        if (*p.pos == '=') {
            return parse_fail(&p);
        } else {
            *op = UNARY_OP_RIGHT_BANG;
        }
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_dot_prop(Parser* parent, bool entity_name, bool* optional_chain_ret, Span* ret) {
    Parser p = parser(parent);

    bool optional_chain = hop_char(&p, '?');

    // (`...` is a token of its own: `a...b` is no property access)
    if (!optional_chain && p.pos[0] == '.' && p.pos[1] == '.' && p.pos[2] == '.') return parse_fail(&p);
    if (!hop_char(&p, '.')) {
        return parse_fail(&p);
    }
    // (`?.` is one token before anything but a digit, tsc's scanner: `a?..b` is `a?.` missing its name)
    // (In an entity name a `.` after the dot is a missing name, tsc's parseEntityName: `typeof a..b`)
    if ((*p.pos >= '0' && *p.pos <= '9') || (*p.pos == '.' && !optional_chain && !entity_name)) {
        return parse_fail(&p); // `a ? .5 : b`, `01.0`: a number; `@dec ...rest`: a spread
    }
    const char* after_dot = p.pos;
    Parser at_dot = p; // (a missing name ends the expression at the dot: the whitespace after it is the next token's)
    hop_whitespace(&p);

    // Committed after the dot (tsc's parseRightSideOfDot): a missing name is TS1003 and an empty name. `a.` followed
    // by two words on the next line is missing its name too, reported right after the dot
    Span prop;
    Tok t = tok_peek(&p);
    if (t.newline_before && tok_is_word(t) && tok_is_word(tok_next(&p, t)) && !tok_next(&p, t).newline_before) {
        parse_error(&p, span_make(p.shared->source, after_dot, after_dot), 1003, "Identifier expected.");
        // (tsc's missing node starts at the dot's end, before trivia: it's printed where the next token is)
        prop = span_make(p.shared->source, p.pos, p.pos);
        p.pos = at_dot.pos;
        p.whitespace = at_dot.whitespace;
        p.whitespace_end = at_dot.whitespace_end;
    } else if (*p.pos == '#' && p.pos[1] != '!') {
        // Private name, the `#` is part of the member's name
        Anchor name_anchor = anchor(&p);
        hop_private_name(&p);
        prop = span_from(name_anchor);
    } else if (!parse_identifier_or_keyword(&p, &prop)) {
        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
        // (printed where the next token is, like tsc's missing node that starts at the dot's end, before trivia; at
        // the end of the file that is right after the dot, tsc's reportAtCurrentPosition)
        prop = *p.pos ? span_from_pos(&p) : span_make(p.shared->source, after_dot, after_dot);
        // (a missing name ends the expression at the dot)
        p.pos = at_dot.pos;
        p.whitespace = at_dot.whitespace;
        p.whitespace_end = at_dot.whitespace_end;
    }

    *ret = prop;
    *optional_chain_ret = optional_chain;
    return parse_success(&p);
}

static bool parse_ternary_expr(Parser* parent, ExprCtx ctx, Expr* ret_yes, Expr* ret_no) {
    Parser p = parser(parent);
    ctx.min_precedence = ternary_precedence - 1;
    bool parent_ternary = ctx.is_parsing_ternary;
    ctx.is_parsing_ternary = true;

    if (!hop_char(&p, '?')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // (tsc's parseConditionalExpressionRest goes on after a missing branch or colon: a missing expression each; `in`
    // is allowed in the true branch, not in the false one)
    bool saved_in = allow_in_enter(&p);
    expect_expr(&p, ctx, ret_yes);
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);

    if (!expect_char(&p, ':')) {
        unhop_trivia(&p); // (tsc's missing colon and branch are at the next token's full start)
        *ret_no = expr_implicit_any(span_from_pos(&p));
        ret_no->missing = true;
        return parse_success(&p);
    }
    hop_whitespace(&p);

    // The false branch allows return types as much as the conditional itself does
    ctx.is_parsing_ternary = parent_ternary;
    if (!expect_expr(&p, ctx, ret_no)) {
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_bracket_prop(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    if (!hop_char(&p, '[')) {
        return parse_fail(&p);
    }
    const char* after_bracket = p.pos;
    hop_whitespace(&p);

    if (*p.pos == ']') {
        // `new T[]`: tsc's parseElementAccessExpressionRest, a missing argument reported where the `]` token's
        // trivia starts
        Span missing = span_make(p.shared->source, after_bracket, after_bracket);
        parse_error(&p, missing, 1011, "An element access expression should take an argument.");
        *ret = expr_implicit_any(missing);
        ret->missing = true;
        p.pos++;
        return parse_success(&p);
    }
    bool saved_in = allow_in_enter(&p);
    bool has_expr = expect_expr(&p, expr_ctx_null, ret);
    if (has_expr) parse_comma_ops_right_side(&p, ret); // (tsc's parseElementAccessExpressionRest: an Expression, commas too)
    allow_in_leave(&p, saved_in);
    if (!has_expr) {
        return parse_success(&p);
    }
    hop_whitespace(&p);

    if (!expect_char(&p, ']')) {
        return parse_success(&p);
    }

    return parse_success(&p);
}

static bool parse_as_type(Parser* parent, TypeNode* ret, Span* keyword_span_ret, bool* is_satisfies_ret) {
    Parser p = parser(parent);

    // Neither keyword can follow a line break
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }
    Anchor a = anchor(&p);
    if (hop_start_if_ends_on_word_break(&p, "as")) {
        *is_satisfies_ret = false;
    } else if (hop_start_if_ends_on_word_break(&p, "satisfies")) {
        *is_satisfies_ret = true;
    } else {
        return parse_fail(&p);
    }
    *keyword_span_ret = span_from(a);
    hop_whitespace(&p);

    Anchor type_start = anchor(&p);
    expect_type(&p, TYPE_PRECEDENCE_NULL, ret);
    if (*is_satisfies_ret) {
        js_syntax_error(&p, span_from(type_start), 8037, "Type satisfaction expressions can only be used in TypeScript files.");
    } else {
        js_syntax_error(&p, span_from(type_start), 8016, "Type assertion expressions can only be used in TypeScript files.");
    }

    return parse_success(&p);
}

// Longest match first within each first character
static bool parse_binary_op(Parser* parent, BinaryOp* op) {
    Parser p = parser(parent);
    bool matched = true;
    switch (*p.pos) {
        case '>':
            if (hop_start(&p, ">>>=")) *op = BINARY_OP_GREATER_GREATER_GREATER_EQUAL;
            else if (hop_start(&p, ">>>")) *op = BINARY_OP_GREATER_GREATER_GREATER;
            else if (hop_start(&p, ">>=")) *op = BINARY_OP_GREATER_GREATER_EQUAL;
            else if (hop_start(&p, ">>")) *op = BINARY_OP_GREATER_GREATER;
            else if (hop_start(&p, ">=")) *op = BINARY_OP_GREATER_EQUAL;
            else { p.pos++; *op = BINARY_OP_GREATER; }
            break;
        case '<':
            if (hop_start(&p, "<<=")) *op = BINARY_OP_LESS_LESS_EQUAL;
            else if (hop_start(&p, "<<")) *op = BINARY_OP_LESS_LESS;
            else if (hop_start(&p, "<=")) *op = BINARY_OP_LESS_EQUAL;
            // (In a JSX file `</` is one token, tsc's LessThanSlashToken, which is no operator)
            else if (p.pos[1] == '/' && p.pos[2] != '*' && p.shared->source->is_jsx) matched = false;
            else { p.pos++; *op = BINARY_OP_LESS; }
            break;
        case '=':
            if (hop_start(&p, "===")) *op = BINARY_OP_TRIPLE_EQUAL;
            else if (hop_start(&p, "==")) *op = BINARY_OP_DOUBLE_EQUAL;
            else if (hop_start(&p, "=>")) matched = false;
            else { p.pos++; *op = BINARY_OP_EQUAL; }
            break;
        case '!':
            if (hop_start(&p, "!==")) *op = BINARY_OP_TRIPLE_NOT_EQUAL;
            else if (hop_start(&p, "!=")) *op = BINARY_OP_NOT_EQUAL;
            else matched = false;
            break;
        case '&':
            if (hop_start(&p, "&&=")) *op = BINARY_OP_AND_AND_EQUAL;
            else if (hop_start(&p, "&&")) *op = BINARY_OP_AND_AND;
            else if (hop_start(&p, "&=")) *op = BINARY_OP_AND_EQUAL;
            else { p.pos++; *op = BINARY_OP_AND; }
            break;
        case '|':
            if (hop_start(&p, "||=")) *op = BINARY_OP_OR_OR_EQUAL;
            else if (hop_start(&p, "||")) *op = BINARY_OP_OR_OR;
            else if (hop_start(&p, "|=")) *op = BINARY_OP_OR_EQUAL;
            else { p.pos++; *op = BINARY_OP_OR; }
            break;
        case '?':
            if (hop_start(&p, "\?\?=")) *op = BINARY_OP_EH_EH_EQUAL;
            else if (hop_start(&p, "\?\?")) *op = BINARY_OP_EH_EH;
            else matched = false;
            break;
        case '*':
            if (hop_start(&p, "**=")) *op = BINARY_OP_STAR_STAR_EQUAL;
            else if (hop_start(&p, "**")) *op = BINARY_OP_STAR_STAR;
            else if (hop_start(&p, "*=")) *op = BINARY_OP_STAR_EQUAL;
            else { p.pos++; *op = BINARY_OP_STAR; }
            break;
        case '+':
            if (hop_start(&p, "++")) matched = false;
            else if (hop_start(&p, "+=")) *op = BINARY_OP_PLUS_EQUAL;
            else { p.pos++; *op = BINARY_OP_PLUS; }
            break;
        case '-':
            if (hop_start(&p, "--")) matched = false;
            else if (hop_start(&p, "-=")) *op = BINARY_OP_MINUS_EQUAL;
            else { p.pos++; *op = BINARY_OP_MINUS; }
            break;
        case '/':
            if (hop_start(&p, "/=")) *op = BINARY_OP_SLASH_EQUAL;
            else { p.pos++; *op = BINARY_OP_SLASH; }
            break;
        case '%':
            if (hop_start(&p, "%=")) *op = BINARY_OP_MOD_EQUAL;
            else { p.pos++; *op = BINARY_OP_MOD; }
            break;
        case '^':
            if (hop_start(&p, "^=")) *op = BINARY_OP_XOR_EQUAL;
            else { p.pos++; *op = BINARY_OP_XOR; }
            break;
        case 'i':
            if (hop_start_if_ends_on_word_break(&p, "in")) *op = BINARY_OP_IN;
            else if (hop_start_if_ends_on_word_break(&p, "instanceof")) *op = BINARY_OP_INSTANCEOF;
            else matched = false;
            break;
        // ',' is handled in parse_comma_ops_right_side()
        default:
            matched = false;
    }
    return matched ? parse_success(&p) : parse_fail(&p);
}


static bool parse_binary_op_expr(Parser* parent, ExprCtx ctx, Expr* left_and_ret) {
    Parser p = parser(parent);

    BinaryOp op;
    if (!parse_binary_op(&p, &op)) {
        return parse_fail(&p);
    }
    int precedence = binary_op_get_precedence(op);
    if (precedence <= ctx.min_precedence || (op == BINARY_OP_IN && p.shared->disallow_in)) {
        return parse_fail(&p);
    }
    // `a ## b as T $$ c`: ends before a $$ that would bind before ## once `as T` is erased (tsgo 7.1's
    // parseBinaryExpressionRest, TypeScript#63527). The binary under the assertions was made at this level
    Expr const* asserted = left_and_ret;
    while (asserted->tag == EXPR_AS_TYPE && !expr_is_perenthisized(asserted) &&
        span_start_ptr(asserted->as_type->keyword_span) > span_start_ptr(expr_outer_span(&asserted->as_type->expr))
    ) {
        asserted = &asserted->as_type->expr;
    }
    if (asserted != left_and_ret && asserted->tag == EXPR_BINARY_OP && !expr_is_perenthisized(asserted) &&
        !binary_op_is_assignment(asserted->binary_op->op) && asserted->binary_op->op != BINARY_OP_COMMA
    ) {
        int last = binary_op_get_precedence(asserted->binary_op->op);
        if (precedence > last || (precedence == last && op == BINARY_OP_STAR_STAR)) return parse_fail(&p);
    }
    // Only a left-hand side expression is assigned to (tsc's parseAssignmentExpressionOrHigher: isLeftHandSideExpression);
    // `await x = 1`, `a + b = c`, `x as T = y` end before the `=`
    if (precedence == binary_op_get_precedence(BINARY_OP_EQUAL) &&
        expr_outer_span(left_and_ret).size == left_and_ret->inner_span.size &&
        ((left_and_ret->tag == EXPR_UNARY_OP && left_and_ret->unary_op->op != UNARY_OP_RIGHT_BANG) ||
            left_and_ret->tag == EXPR_BINARY_OP ||
            // (A JSDoc `/** @type {T} */ (x)` is tsc's ParenthesizedExpression: its type comes before the expression)
            (left_and_ret->tag == EXPR_AS_TYPE && (left_and_ret->as_type->angle_bracket_syntax ||
                span_start_ptr(left_and_ret->as_type->keyword_span) > span_start_ptr(expr_outer_span(left_and_ret)))))) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Expr right;
    // Assignments and `**` are right associative: `a = b ??= c`
    bool right_associative = precedence == binary_op_get_precedence(BINARY_OP_EQUAL) || op == BINARY_OP_STAR_STAR;
    ctx.min_precedence = right_associative ? precedence - 1 : precedence;
    expect_expr(&p, ctx, &right);

    bool logical_or_plain = op == BINARY_OP_EQUAL || op == BINARY_OP_AND_AND_EQUAL || op == BINARY_OP_OR_OR_EQUAL ||
        op == BINARY_OP_EH_EH_EQUAL;
    if (logical_or_plain && left_and_ret->tag == EXPR_IDENTIFIER) mark_named_evaluation(&right, false);
    bool left_parenthesized = expr_outer_span(left_and_ret).size != left_and_ret->inner_span.size;
    if (left_and_ret->tag == EXPR_IDENTIFIER && !left_parenthesized) {
        mark_assigned_name(&right, left_and_ret->inner_span);
    } else if (left_and_ret->tag == EXPR_PROPERTY && !left_parenthesized) {
        // `a.C = class {}`: C. `a["C"] = class {}`: "C". `a[k] = class {}`: a[k]
        PropertyExpr const* access = left_and_ret->property;
        Span name = access->form == PROPERTY_EXPR_DOT ? property_name(access) :
            access->index.tag == EXPR_LITERAL ? access->index.inner_span : left_and_ret->inner_span;
        mark_assigned_name(&right, name);
    }
    // (tsc's setCommonJSModuleIndicator for an assignment declaration: `module.exports = exports` is none)
    if (op == BINARY_OP_EQUAL && p.shared->source->is_js && expr_commonjs_target(left_and_ret, nullptr) &&
        !(expr_is_module_exports(left_and_ret) && expr_is_plain_identifier(&right, "exports"))
    ) {
        p.shared->saw_commonjs = true;
    }
    expr_mark_binary_operand(left_and_ret);
    expr_mark_binary_operand(&right);
    // (A missing right operand ends the expression where its token's trivia starts, see expect_expr())
    Span right_end = right.missing ? span_from_pos(&p) : expr_outer_span(&right);
    *left_and_ret = expr_binary_op(span_between(expr_outer_span(left_and_ret), right_end), ast_new(BinaryOpExpr, {
        .left = *left_and_ret,
        .op = op,
        .right = right,
    }));
    return parse_success(&p);
}

// `await` is only a keyword in async functions and at the top level of modules. Elsewhere tsc still reads it as an
// operator (and then complains) if an operand obviously follows
static bool await_is_operator(Parser* parent) {
    if (!starts_with_followed_by_word_break(parent, "await")) {
        return false;
    } else if (parent->shared->in_async || parent->shared->in_static_block) {
        return true; // (A static block is an await context: tsc's parseClassStaticBlockBody)
    }
    // (The next token, after any comments: a comment spanning lines is a line break)
    Tok next = tok_at(parent, parent->pos + strlen("await"), false);
    if (next.newline_before) return false;
    const char* c = next.start;
    // (tsc's isAwaitExpression: an identifier, keyword or literal on the same line. In a module's top level every
    // `await` is an operator, see ParserShared.top_level_await)
    // (Not a template: `await \`\`` is a tagged template until a module reparses it, topLevelAwait.1. A `.` starts a
    // number only before a digit: `await.x` is a property of `await`)
    return identifier_char_size(c, true) || (is_number_start_char(*c) && (*c != '.' || (c[1] >= '0' && c[1] <= '9'))) ||
        *c == '"' || *c == '\'';
}

// tsc's isYieldExpression outside a yield context: `yield` followed by an identifier, keyword or literal on the same line
// is a yield expression (TS1163 from the checker), anything else makes it an identifier
static bool yield_is_operator_outside_generator(Parser* parent) {
    if (!starts_with_followed_by_word_break(parent, "yield")) return false;
    Tok next = tok_at(parent, parent->pos + strlen("yield"), false);
    if (next.newline_before) return false;
    const char* c = next.start;
    return identifier_char_size(c, true) || is_number_start_char(*c) || (unsigned char)*c >= 0x80 || *c == '"' ||
        *c == '\'' || *c == '`';
}

static bool parse_left_unary_op(Parser* parent, UnaryOp* op) {
    // (the tokens `!=`, `+=` and `-=` are no unary operators)
    if ((*parent->pos == '!' || *parent->pos == '+' || *parent->pos == '-') && parent->pos[1] == '=') return false;
    if (hop_start(parent, "!")) {
        *op = UNARY_OP_LEFT_BANG;
    } else if (hop_start_if_ends_on_word_break(parent, "delete")) {
        *op = UNARY_OP_DELETE;
    } else if (await_is_operator(parent) && hop_start_if_ends_on_word_break(parent, "await")) {
        *op = UNARY_OP_AWAIT;
    } else if ((parent->shared->in_generator || yield_is_operator_outside_generator(parent)) &&
        hop_start_if_ends_on_word_break(parent, "yield")) {
        *op = UNARY_OP_YIELD;
        const char* after_yield = parent->pos;
        // (The whitespace state too: the probe hops no line breaks, and a cached end there would hide them later)
        __typeof__(parent->whitespace) whitespace = parent->whitespace;
        const char* whitespace_end = parent->whitespace_end;
        hop_whitespace_full(parent, true, false);
        if (hop_char(parent, '*')) {
            *op = UNARY_OP_YIELD_STAR;
        } else {
            parent->pos = after_yield;
            parent->whitespace = whitespace;
            parent->whitespace_end = whitespace_end;
        }
    } else if (hop_start_if_ends_on_word_break(parent, "typeof")) {
        *op = UNARY_OP_TYPEOF;
    } else if (hop_start_if_ends_on_word_break(parent, "void")) {
        *op = UNARY_OP_VOID;
    } else if (hop_start(parent, "~")) {
        *op = UNARY_OP_TILDE;
    } else if (hop_start(parent, "++")) {
        *op = UNARY_OP_LEFT_PLUS_PLUS;
    } else if (hop_start(parent, "--")) {
        *op = UNARY_OP_LEFT_MINUS_MINUS;
    } else if (hop_start(parent, "+")) {
        *op = UNARY_OP_PLUS;
    } else if (hop_start(parent, "-")) {
        *op = UNARY_OP_MINUS;
    } else {
        return false;
    }
    return true;
}

[[gnu::noinline]] static bool parse_left_unary_op_expr_slow(Parser* parent, ExprCtx ctx, Expr* ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_left_unary_op_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    // (What parse_left_unary_op() can start with, an escaped keyword's `\` too)
    switch (*parent->pos) {
        case '!': case '+': case '-': case '~': case '\\': break;
        case 'a': if (!may_start_keyword(parent->pos, "await")) GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret)); break;
        case 'd': if (!may_start_keyword(parent->pos, "delete")) GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret)); break;
        case 't': if (!may_start_keyword(parent->pos, "typeof")) GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret)); break;
        case 'v': if (!may_start_keyword(parent->pos, "void")) GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret)); break;
        case 'y': if (!may_start_keyword(parent->pos, "yield")) GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret)); break;
        default: GATE_FAIL(parse_left_unary_op_expr(&v_, ctx, ret));
    }
    return parse_left_unary_op_expr_slow(parent, ctx, ret);
}

[[gnu::noinline]] static bool parse_left_unary_op_expr_slow(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);

    Anchor a = anchor(&p);
    UnaryOp op = 0;
    if (!parse_left_unary_op(&p, &op) || unary_op_get_precedence(op) < ctx.min_precedence) {
        return parse_fail(&p);
    }
    Span op_span = span_from(a);
    const char* op_end = p.pos;
    hop_whitespace(&p);

    bool in_parameters = p.shared->in_parameters;
    AwaitPlace await_place = op == UNARY_OP_AWAIT ? parser_await_place(&p) : AWAIT_ALLOWED;

    Expr expr;
    ctx.min_precedence = unary_op_get_precedence(op);
    // The operand of a prefix `++`/`--` is a left-hand side expression, no unary one (tsc's parseUpdateExpression):
    // `++-x`, `++await x` are missing it
    if (op == UNARY_OP_LEFT_PLUS_PLUS || op == UNARY_OP_LEFT_MINUS_MINUS) ctx.min_precedence++;
    // (A yield's operand is an assignment expression, arrow functions included: tsc's parseYieldExpression)
    if (op == UNARY_OP_YIELD || op == UNARY_OP_YIELD_STAR) ctx.min_precedence = ternary_precedence - 1;
    bool bare_yield = false;
    if (op == UNARY_OP_YIELD) {
        // tsc's parseYieldExpression(): an operand if one starts on the same line (isStartOfExpression)
        char c = *p.pos;
        bool starts_expression = identifier_char_size(p.pos, true) || is_number_start_char(c) || (unsigned char)c >= 0x80 ||
            strchr("\"'`([{!~+-/<@#", c) != nullptr;
        bare_yield = !c || !starts_expression || p.whitespace == WHITESPACE_NEWLINE;
    }
    if (bare_yield) {
        p.pos = op_end;
        expr = expr_implicit_any_no_span;
    } else {
        expect_expr(&p, ctx, &expr);
    }
    // (A `yield` or `await` in a parameter initializer is TS2523/TS2524 when the checker checks the expression:
    // tsc's checkYieldExpression and checkAwaitExpression, reported by the resolve walk)

    // tsc's parseUnaryExpressionOrHigher: `-x ** 2` is ambiguous, so an error (still read as `(-x) ** 2`)
    bool simple_unary = op == UNARY_OP_PLUS || op == UNARY_OP_MINUS || op == UNARY_OP_TILDE || op == UNARY_OP_LEFT_BANG ||
        op == UNARY_OP_DELETE || op == UNARY_OP_TYPEOF || op == UNARY_OP_VOID || op == UNARY_OP_AWAIT;
    if (simple_unary) {
        Parser lookahead = parser(&p);
        hop_whitespace(&lookahead);
        bool exponent = lookahead.pos[0] == '*' && lookahead.pos[1] == '*' && lookahead.pos[2] != '=';
        parse_fail(&lookahead);
        if (exponent) {
            csview op_text = csview_trim(zsview_sv(unary_op_get_zv(op)));
            parse_error(&p, span_between(op_span, expr_outer_span(&expr)), 17006, "An unary expression with the '",
                op_text, "' operator is not allowed in the left-hand side of an exponentiation expression. "
                "Consider enclosing the expression in parentheses.");
        }
    }

    *ret = expr_unary_op(
        op_span,
        ast_new(UnaryOpExpr, {
            .op = op,
            .await_place = (uint8_t)await_place,
            .outside_generator = (op == UNARY_OP_YIELD || op == UNARY_OP_YIELD_STAR) && !p.shared->in_generator,
            .in_parameter_initializer = in_parameters,
            .expr = expr,
        })
    );
    return parse_success(&p);
}

static inline bool is_digit_of_base(char c, int base) {
    int v = hex_digit_value(c);
    return v >= 0 && v < base;
}

// The digits of `base` at c and the numeric separators between them (tsc's scanNumberFragment, scanHexDigits and
// scanBinaryOrOctalDigits, which agree on where a `_` may be): appends the digits to `digits`, returns their end
static const char* scan_digit_fragment(Parser* p, const char* c, int base, cstr* digits) {
    bool allow_separator = false;
    bool previous_separator = false;
    const char* run = c; // (The digits since the last separator, appended at once)
    for (;; c++) {
        if (*c == '_') {
            if (c > run) cstr_append_n(digits, run, c - run);
            run = c + 1;
            if (allow_separator) {
                allow_separator = false;
                previous_separator = true;
            } else if (previous_separator) {
                parse_error(p, span_make(p->shared->source, c, c + 1), 6189,
                    "Multiple consecutive numeric separators are not permitted.");
            } else {
                parse_error(p, span_make(p->shared->source, c, c + 1), 6188, "Numeric separators are not allowed here.");
            }
        } else if (is_digit_of_base(*c, base)) {
            allow_separator = true;
            previous_separator = false;
        } else {
            break;
        }
    }
    if (c > run) cstr_append_n(digits, run, c - run);
    if (c[-1] == '_') {
        parse_error(p, span_make(p->shared->source, c - 1, c), 6188, "Numeric separators are not allowed here.");
    }
    return c;
}

__extension__ typedef unsigned __int128 DigitsBits;

static double digits_value(csview digits, int base) {
    // A power-of-two base: the digits' bits exactly (128 of them, and a sticky bit for any beyond), rounded once to
    // the nearest double, as tsgo's parsing of a hex/octal/binary literal does: `0x1000000000000081` is
    // 1152921504606847232, not the 1152921504606846976 rounding at every digit gives
    if (base == 16 || base == 8 || base == 2) {
        int bits = base == 16 ? 4 : base == 8 ? 3 : 1;
        DigitsBits acc = 0;
        int extra = 0; // bits beyond what acc holds
        bool sticky = false;
        for (intptr_t i = 0; i < digits.size; i++) {
            unsigned d = (unsigned)hex_digit_value(digits.buf[i]);
            if (extra || (acc >> (128 - bits)) != 0) {
                extra += bits;
                sticky |= d != 0;
            } else {
                acc = (acc << bits) | d;
            }
        }
        if (sticky) acc |= 1; // (Far below the double's 53 bits: only breaks a tie the dropped bits break)
        return ldexp((double)acc, extra);
    }
    double value = 0;
    for (intptr_t i = 0; i < digits.size; i++) value = value * base + hex_digit_value(digits.buf[i]);
    return value;
}

// After a numeric literal that ends at c (tsc's checkForIdentifierStartAfterNumericLiteral): an identifier right
// after it is TS1351 and a token of its own, but a lone `n` is a bigint suffix in the wrong place (consumed)
static const char* check_identifier_after_number(Parser* p, const char* start, isize first_error, const char* c, bool scientific) {
    // (tsc looks at the code point there: a `\` is no identifier start, even if an escape starts one)
    if (*c == '\\') return c;
    int size = identifier_char_size(c, true);
    if (!size) return c;
    const char* id_end = c + size;
    while ((size = identifier_char_size(id_end, false))) id_end += size;
    if (id_end - c == 1 && *c == 'n') {
        Span span = span_make(p->shared->source, start, id_end);
        if (scientific) parse_error(p, span, 1352, "A bigint literal cannot use exponential notation.");
        else parse_error(p, span, 1353, "A bigint literal must be an integer.");
        return id_end;
    }
    scanner_error(p, start, first_error, span_make(p->shared->source, c, id_end), 1351,
        "An identifier or keyword cannot immediately follow a numeric literal.");
    return c;
}

// A numeric literal with its scanner diagnostics (tsc's scanNumber and the `0x`/`0b`/`0o` cases of scan()); `bigint`
// is set for an `n` suffix (the value stays a double, see Value)
static bool scan_numeric_literal(Parser* parent, double* ret, bool* bigint) {
    const char* start = parent->pos;
    isize first_error = vec_error_size(&parent->shared->errors);
    const char* c = start;
    if (!(*c >= '0' && *c <= '9') && !(*c == '.' && c[1] >= '0' && c[1] <= '9')) return false;
    *bigint = false;
    cstr digits = cstr_init();
    char prefix = c[0] == '0' ? (char)(c[1] | 0x20) : 0;
    if ((prefix == 'x' || prefix == 'b' || prefix == 'o') && c[2]) {
        int base = prefix == 'x' ? 16 : prefix == 'b' ? 2 : 8;
        c = scan_digit_fragment(parent, c + 2, base, &digits);
        if (!cstr_size(&digits)) {
            Span at = span_make(parent->shared->source, c, c);
            if (base == 16) parse_error(parent, at, 1125, "Hexadecimal digit expected.");
            else if (base == 2) parse_error(parent, at, 1177, "Binary digit expected.");
            else parse_error(parent, at, 1178, "Octal digit expected.");
        }
        *ret = digits_value(cstr_sv(&digits), base);
        cstr_drop(&digits);
        if (*c == 'n') {
            *bigint = true;
            c++;
        }
        parent->pos = c;
        return true;
    }

    bool leading_zero = false;
    if (c[0] == '0' && c[1] >= '0' && c[1] <= '9') {
        // A legacy octal (TS1121), or a decimal with a leading zero (NonOctalDecimalIntegerLiteral: TS1489 below)
        const char* d = c + 1;
        bool octal = true;
        for (; *d >= '0' && *d <= '9'; d++) octal &= *d < '8';
        if (octal) {
            double value = digits_value(csview_from_range(c + 1, d), 8);
            *ret = value;
            parent->pos = d;
            cstr_drop(&digits);
            bool minus = start > cstr_str(&parent->shared->source->contents) && start[-1] == '-';
            char octal_text[64];
            snprintf(octal_text, sizeof(octal_text), "%s0o%llo", minus ? "-" : "", (unsigned long long)value);
            Span span = span_make(parent->shared->source, minus ? start - 1 : start, d);
            parse_error(parent, span, 1121, "Octal literals are not allowed. Use the syntax '", octal_text, "'.");
            return true;
        }
        leading_zero = true;
        cstr_append_n(&digits, c, d - c);
        c = d;
    } else if (c[0] == '0' && c[1] == '_') {
        // tsc reports this `_` and then reads the literal as if it wasn't a leading zero (the `_` is allowed there)
        parse_error(parent, span_make(parent->shared->source, c + 1, c + 2), 6188,
            "Numeric separators are not allowed here.");
        c = scan_digit_fragment(parent, c, 10, &digits);
    } else {
        c = scan_digit_fragment(parent, c, 10, &digits);
    }
    bool decimal = false;
    if (*c == '.') {
        decimal = true;
        cstr_append_n(&digits, ".", 1);
        c = scan_digit_fragment(parent, c + 1, 10, &digits);
    }
    const char* end = c;
    bool scientific = false;
    if (*c == 'e' || *c == 'E') {
        scientific = true;
        c++;
        char sign = 0;
        if (*c == '+' || *c == '-') sign = *c++;
        cstr exponent = cstr_init();
        c = scan_digit_fragment(parent, c, 10, &exponent);
        if (!cstr_size(&exponent)) {
            parse_error(parent, span_make(parent->shared->source, c, c), 1124, "Digit expected.");
        } else {
            cstr_append_n(&digits, "e", 1);
            if (sign) cstr_append_n(&digits, &sign, 1);
            cstr_append_s(&digits, exponent);
            end = c;
        }
        cstr_drop(&exponent);
    }
    *ret = strtod(cstr_str(&digits), nullptr);
    cstr_drop(&digits);
    if (leading_zero) {
        parse_error(parent, span_make(parent->shared->source, start, end), 1489,
            "Decimals with leading zeros are not allowed.");
    } else if (decimal || scientific) {
        c = check_identifier_after_number(parent, start, first_error, c, !decimal && scientific);
    } else {
        if (*c == 'n') {
            *bigint = true;
            c++;
        }
        c = check_identifier_after_number(parent, start, first_error, c, false);
    }
    parent->pos = c;
    return true;
}

// A number or, with an `n` suffix, a bigint literal (its value is kept as a double, see Value)
static bool parse_numeric_value(Parser* parent, Value* ret) {
    double number;
    bool bigint;
    char const* start = parent->pos;
    if (!scan_numeric_literal(parent, &number, &bigint)) return false;
    // (A bigint is its text: exact. The scanner left pos after its `n`)
    *ret = bigint ? value_bigint(start, (u32)(parent->pos - start - 1), false) : (Value){ .tag = VALUE_NUMBER, .number = number };
    return true;
}

// At a NUL byte: whether it is a character before the end of the file rather than the end (tsc's scanner takes it
// as part of a string literal). Only in the file's own text: a sub-parser's buffer ends at its NUL
__attribute__((noinline)) static bool nul_inside_file(Parser const* p) {
    char const* file_start = cstr_str(&p->shared->source->contents);
    char const* file_end = file_start + cstr_size(&p->shared->source->contents);
    return p->pos >= file_start && p->pos < file_end;
}

// `report_escapes`: false in a tagged template, where invalid escapes are allowed
[[gnu::noinline]] static bool parse_string_or_tagged_slow(Parser* parent, cstr* ret, bool report_escapes);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_string_or_tagged(Parser* parent, cstr* ret, bool report_escapes) {
    if (*parent->pos != '"' && *parent->pos != '\'' && *parent->pos != '`') GATE_FAIL(parse_string_or_tagged(&v_, ret, report_escapes));
    return parse_string_or_tagged_slow(parent, ret, report_escapes);
}

[[gnu::noinline]] static bool parse_string_or_tagged_slow(Parser* parent, cstr* ret, bool report_escapes) {
    Parser p = parser(parent);

    char quote = *p.pos;
    switch (quote) {
        case '"': case '\'': case '`': break;
        default: return parse_fail(&p);
    }
    const char* literal_start = p.pos;
    isize literal_errors = vec_error_size(&p.shared->errors);
    p.pos++;
    cstr str = cstr_null;
    while (true) {
        if (*p.pos == quote || (!*p.pos && !nul_inside_file(&p))) {
            break;
        } else if ((*p.pos == '\n' || *p.pos == '\r') && quote != '`') {
            break;
        } else if (*p.pos == '\\' && !p.pos[1]) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text."); // (in a tagged template too)
            break;
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            if (!parse_string_escape(&p, &str, &errors, quote == '`' && !report_escapes) && report_escapes) report_escape_errors(&p, &errors);
            continue;
        } else if (*p.pos == '\r') {
            // (A template's lone \r is a line break, \n in its value: tsc's scanner normalizes CR and CRLF)
            if (quote == '`' && p.pos[1] != '\n') cstr_append_lit(&str, "\n");
        } else {
            // The plain run up to whatever the loop has to look at, appended at once
            const char* run = p.pos++;
            while (*p.pos && *p.pos != quote && *p.pos != '\n' && *p.pos != '\\' && *p.pos != '\r') p.pos++;
            cstr_append_n(&str, run, p.pos - run);
            continue;
        }
        p.pos++;
    }
    *ret = str;
    if (*p.pos == quote) {
        p.pos++;
    } else if (quote == '`') {
        scanner_error(&p, literal_start, literal_errors, span_from_pos(&p), 1160, "Unterminated template literal.");
    } else {
        scanner_error(&p, literal_start, literal_errors, span_from_pos(&p), 1002, "Unterminated string literal.");
    }

    return parse_success(&p);
}

static bool parse_string(Parser* parent, cstr* ret) {
    return parse_string_or_tagged(parent, ret, true);
}

// The end tsc gives an unterminated regular expression literal (in its reScanSlashToken): the first unbalanced `)`,
// `]` or `}` outside a character class, else the line's end, without the whitespace and semicolons before it
static const char* unterminated_regex_end(const char* body, const char* line_end) {
    const char* c = body;
    bool in_escape = false;
    bool in_quantifier = false;
    int class_depth = 0;
    int group_depth = 0;
    for (; c < line_end; c++) {
        char ch = *c;
        if (in_escape) {
            in_escape = false;
        } else if (ch == '\\') {
            in_escape = true;
        } else if (ch == '[') {
            class_depth++;
        } else if (ch == ']' && class_depth) {
            class_depth--;
        } else if (!class_depth) {
            if (ch == '{') {
                in_quantifier = true;
            } else if (ch == '}' && in_quantifier) {
                in_quantifier = false;
            } else if (!in_quantifier) {
                if (ch == '(') group_depth++;
                else if (ch == ')' && group_depth) group_depth--;
                else if (ch == ')' || ch == ']' || ch == '}') break;
            }
        }
    }
    while (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\v' || c[-1] == '\f' || c[-1] == ';') c--;
    return c;
}

// A regular expression literal. Only called where an expression may start, which is what makes the `/` not a division
// (tsc rescans the slash token in the same situation). Its body and flags are checked by the checker
static bool parse_regex(Parser* parent) {
    Parser p = parser(parent);

    if (*p.pos != '/' || p.pos[1] == '/' || p.pos[1] == '*') {
        return parse_fail(&p);
    }
    const char* start = p.pos;
    p.pos++;
    bool in_class = false;
    bool in_escape = false;
    while (true) {
        char c = *p.pos;
        if (at_eof(&p, p.pos) || c == '\n' || c == '\r' || unicode_whitespace_size(p.pos) < 0) {
            p.pos = unterminated_regex_end(start + 1, p.pos);
            parse_error(&p, span_make(p.shared->source, start, p.pos), 1161, "Unterminated regular expression literal.");
            return parse_success(&p);
        }
        if (in_escape) {
            in_escape = false;
        } else if (c == '/' && !in_class) {
            break;
        } else if (c == '[') {
            in_class = true;
        } else if (c == '\\') {
            in_escape = true;
        } else if (c == ']') {
            in_class = false;
        }
        p.pos++;
    }
    p.pos++;
    int size;
    while ((size = identifier_char_size(p.pos, false))) p.pos += size;

    return parse_success(&p);
}

[[gnu::noinline]] static bool parse_type_compat_literal_slow(Parser* parent, Value* ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_type_compat_literal(Parser* parent, Value* ret) {
    // (What the branches below can start with: most types are names, which fail here without a child parser)
    switch (*parent->pos) {
        case '\\': case '"': case '\'': case '`': case '.':
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': break;
        case 'u': if (!may_start_keyword(parent->pos, "undefined")) GATE_FAIL(parse_type_compat_literal(&v_, ret)); break;
        case 'n': if (!may_start_keyword(parent->pos, "null")) GATE_FAIL(parse_type_compat_literal(&v_, ret)); break;
        case 't': if (!may_start_keyword(parent->pos, "true")) GATE_FAIL(parse_type_compat_literal(&v_, ret)); break;
        case 'f': if (!may_start_keyword(parent->pos, "false")) GATE_FAIL(parse_type_compat_literal(&v_, ret)); break;
        default: GATE_FAIL(parse_type_compat_literal(&v_, ret));
    }
    return parse_type_compat_literal_slow(parent, ret);
}

[[gnu::noinline]] static bool parse_type_compat_literal_slow(Parser* parent, Value* ret) {
    Parser p = parser(parent);

    if (hop_start_if_ends_on_word_break(&p, "undefined")) {
        *ret = value_undefined;
    } else if (hop_start_if_ends_on_word_break(&p, "null")) {
        *ret = value_null;
    } else if (hop_start_if_ends_on_word_break(&p, "true")) {
        *ret = value_boolean(true);
    } else if (hop_start_if_ends_on_word_break(&p, "false")) {
        *ret = value_boolean(false);
    } else if (parse_numeric_value(&p, ret)) {
    } else if (parse_string(&p, &ret->string)) {
        ret->tag = VALUE_STRING;
        ast_cstr_seal(&ret->string);
    } else {
        return parse_fail(&p);
    }

    return parse_success(&p);
}

// A template literal type with substitutions (`a${B}c`): the texts are cooked like a string's, the substitutions are
// types (which can be templates again). Fails if there are none, a plain template is a string literal type
static bool parse_template_type(Parser* parent, TypeNode* ret) {
    Parser p = parser(parent);

    if (*p.pos != '`') {
        return parse_fail(&p);
    }
    Anchor template_a = anchor(&p);
    p.pos++;

    TemplateType template = { .texts = ast_cstrs_init(), .types = vec_type_init(), .type_spans = ast_spans_init() };
    cstr text = cstr_null;
    bool missing_tail = false; // As in parse_template()
    while ((*p.pos || nul_inside_file(&p)) && *p.pos != '`') {
        if (*p.pos == '\\' && !p.pos[1]) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text.");
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            if (!parse_string_escape(&p, &text, &errors, false)) report_escape_errors(&p, &errors);
        } else if (p.pos[0] == '$' && p.pos[1] == '{') {
            p.pos += 2;
            ast_cstr_seal(&text);
            ast_cstrs_push(&template.texts, text);
            text = cstr_null;
            hop_whitespace(&p);
            Anchor type_a = anchor(&p);
            TypeNode type;
            expect_type(&p, TYPE_PRECEDENCE_NULL, &type);
            vec_type_push(&template.types, type);
            ast_spans_push(&template.type_spans, span_from(type_a));
            hop_whitespace(&p);
            if (!expect_char(&p, '}')) {
                missing_tail = true;
                unhop_trivia(&p); // (tsc's missing template tail is at the token's full start)
                break;
            }
        } else if (*p.pos == '\r') {
            p.pos++; // CRLF is LF in a template
        } else {
            const char* run = p.pos;
            p.pos++;
            while ((*p.pos || nul_inside_file(&p)) && *p.pos != '`' && *p.pos != '\\' && *p.pos != '$' && *p.pos != '\r') p.pos++;
            cstr_append_n(&text, run, p.pos - run);
        }
    }
    ast_cstr_seal(&text);
    ast_cstrs_push(&template.texts, text);

    if (vec_type_is_empty(&template.types)) {
        template_type_drop(&template);
        return parse_fail(&p);
    }

    if (*p.pos == '`') {
        p.pos++;
    } else if (!missing_tail) {
        parse_error(&p, span_from_pos(&p), 1160, "Unterminated template literal.");
    }

    template.span = span_from(template_a);
    *ret = (TypeNode){ .tag = TYPE_TEMPLATE, .template_ = ast_new(TemplateType, template) };
    return parse_success(&p);
}

static bool parse_literal(Parser* parent, Value* ret) {
    return parse_type_compat_literal(parent, ret);
}

// An access or call continues the chain its object may be part of: a `!` is then part of it (see
// UnaryOpExpr.continues_chain), and the access or call under it isn't the chain's end (PropertyExpr.continues_chain)
// An instantiation expression ends a chain (`a?.b<T>.c`, tsc 6), unless its type arguments are a call's (`a?.b<T>()`)
// A `?.` access (`optional`) right after a `!` starts a chain of its own: the `!` stays a plain non-null expression
// (tsc's tryReparseOptionalChain runs only without a question dot), `o?.a!?.b`
static void mark_chain_continues(Expr* object, bool is_call, bool optional) {
    if (optional && !expr_is_perenthisized(object) && object->tag == EXPR_UNARY_OP && object->unary_op->op == UNARY_OP_RIGHT_BANG) {
        return;
    }
    while (!expr_is_perenthisized(object)) {
        if (object->tag == EXPR_UNARY_OP && object->unary_op->op == UNARY_OP_RIGHT_BANG) {
            object->unary_op->continues_chain = true;
            object = &object->unary_op->expr;
        } else if (object->tag == EXPR_GENERIC_PARAMS) {
            if (!is_call) return;
            object = &object->generic_params->expr; // (`a?.b<T>()`: the type arguments are the call's)
        } else {
            if (object->tag == EXPR_PROPERTY) object->property->continues_chain = true;
            if (object->tag == EXPR_CALL && !object->call->uses_new) object->call->continues_chain = true;
            return;
        }
    }
}

[[gnu::noinline]] static bool parse_expr_right_side_slow(Parser* parent, ExprCtx ctx, Expr* left_and_ret);
// Whether a tagged template's tag is part of an optional chain (tsc's NodeFlags.OptionalChain, which property accesses,
// calls and tagged templates after a `?.` carry until parentheses or a `!` end the chain)
static bool tag_in_optional_chain(Expr const* e) {
    for (u32 depth = 0; depth < 256; depth++) {
        if (expr_is_perenthisized(e)) return false;
        if (e->tag == EXPR_GENERIC_PARAMS) {
            e = &e->generic_params->expr;
        } else if (e->tag == EXPR_TEMPLATE && e->template_->tag) {
            return e->template_->optional_chain;
        } else if (e->tag == EXPR_PROPERTY) {
            if (e->property->optional_chain) return true;
            e = &e->property->expr;
        } else if (e->tag == EXPR_CALL && !e->call->uses_new) {
            if (e->call->optional_chain) return true;
            e = &e->call->callable;
        } else {
            return false;
        }
    }
    return false;
}

// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_expr_right_side(Parser* parent, ExprCtx ctx, Expr* left_and_ret) {
    // (The common end of an expression, right after it: no child parser. With trivia before it the child below hops
    // it, which records where the trivia was)
    switch (*parent->pos) {
        case ';': case ',': case ')': case ']': case '}': case ':': GATE_FAIL(parse_expr_right_side(&v_, ctx, left_and_ret));
        default: break;
    }
    return parse_expr_right_side_slow(parent, ctx, left_and_ret);
}

[[gnu::noinline]] static bool parse_expr_right_side_slow(Parser* parent, ExprCtx ctx, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, expr_outer_span(left_and_ret));

    hop_whitespace(&p);
    bool hit_newline = p.whitespace == WHITESPACE_NEWLINE;
    // Where most expressions end: no branch below starts with these
    char c = *p.pos;
    switch (c) {
        case ';': case ',': case ')': case ']': case '}': case ':': case '\0': return parse_fail(&p);
        default: break;
    }
    // (A `?.` missing its name ends the member expression, tsc's parseMemberExpressionRest returns there: `(a?..b)` is
    // `(a?.` missing its `)`, then `.b`)
    if (c == '.' && left_and_ret->tag == EXPR_PROPERTY && left_and_ret->property->optional_chain &&
        left_and_ret->property->form == PROPERTY_EXPR_DOT && !left_and_ret->property->name_.size &&
        expr_outer_span(left_and_ret).size == left_and_ret->inner_span.size
    ) {
        return parse_fail(&p);
    }

    union {
        VecExpr args;
        struct {
            bool optional_chain;
            Expr expr_a;
            Expr expr_b;
        };
        TypeNode type;
        UnaryOp unary;
        struct {
            Expr expr;
            BinaryOp op;
        } binary;
        FunctionImpl function;
        VecSpannedType generic_params;
    } parsed;

    // `f?.(x)` and `a?.[i]`. `a?.b` is parse_dot_prop()'s, and `a ? .5 : b` is a ternary
    bool optional_chain = false;
    // No optional chain in the callee of a `new` (tsc's parseMemberExpressionRest with allowOptionalChain false)
    if (ctx.min_precedence == new_keyword_precedence && p.pos[0] == '?' && p.pos[1] == '.' && !(p.pos[2] >= '0' && p.pos[2] <= '9')) {
        return parse_fail(&p);
    }
    // tsc's parseUpdateExpression: `a++` is no left-hand side expression, only operators continue it (`a--.b` and
    // `a++ ++` are missing a `;`)
    if (left_and_ret->tag == EXPR_UNARY_OP && !expr_is_perenthisized(left_and_ret) &&
        (left_and_ret->unary_op->op == UNARY_OP_RIGHT_PLUS_PLUS || left_and_ret->unary_op->op == UNARY_OP_RIGHT_MINUS_MINUS)
    ) {
        goto binary_rest;
    }
    // tsc's parseUpdateExpression: a JSX element is no left-hand side expression either (`<a />.b` is missing a `;`)
    if ((left_and_ret->tag == EXPR_JSX_ELEMENT || left_and_ret->tag == EXPR_JSX_FRAGMENT) && !expr_is_perenthisized(left_and_ret) &&
        (c == '(' || c == '[' || c == '.' || c == '`' || (c == '?' && p.pos[1] == '.' && !(p.pos[2] >= '0' && p.pos[2] <= '9')) ||
            (c == '!' && p.pos[1] != '=') || (c == '+' && p.pos[1] == '+') || (c == '-' && p.pos[1] == '-'))
    ) {
        goto binary_rest;
    }
    // tsc's parseBinaryExpressionRest: `x as T` is no left-hand side expression either, a call, member access or
    // template doesn't continue it (`(x as any(1))` is missing a `)`). Not a JSDoc assertion, whose type comes first
    if (left_and_ret->tag == EXPR_AS_TYPE && !expr_is_perenthisized(left_and_ret) &&
        span_start_ptr(left_and_ret->as_type->keyword_span) > span_start_ptr(expr_outer_span(&left_and_ret->as_type->expr)) &&
        (c == '(' || c == '[' || c == '.' || c == '`' || c == '<' || (c == '?' && p.pos[1] == '.' && !(p.pos[2] >= '0' && p.pos[2] <= '9')))
    ) {
        // (Nor type arguments: `x as T < U > (y)` compares)
        goto operators;
    }
    bool missing_call_args = false;
    // `a?.(x)`, `a?.[i]`, also with whitespace or comments after the `?.` (`f?. (x)`: tsc's scanner makes `?.` one
    // token, and the call or element access follows it like any other token)
    bool generic_optional_template = false;
    char after_question_dot = 0;
    if (p.pos[0] == '?' && p.pos[1] == '.' && !(p.pos[2] >= '0' && p.pos[2] <= '9')) {
        Parser q = parser(&p);
        q.pos += 2;
        hop_whitespace(&q);
        after_question_dot = *q.pos;
        parse_fail(&q);
    }
    if (after_question_dot == '(' || after_question_dot == '[') {
        optional_chain = true;
        p.pos += 2;
        hop_whitespace(&p);
    } else if (p.pos[0] == '?' && p.pos[1] == '.' && p.pos[2] == '<' && !p.shared->source->is_js &&
        !ctx.entity_name && new_keyword_precedence > ctx.min_precedence
    ) {
        // `a?.<T>()`: an optional call with type arguments (tsc's parseCallExpressionRest)
        Parser q = parser(&p);
        q.pos += 2;
        Anchor args_a = anchor(&q);
        VecSpannedType params;
        if (parse_generic_params(&q, true, &params)) {
            hop_whitespace(&q);
            // (tsc's canFollowTypeArgumentsInExpression: then the type arguments make a call, its `(` maybe missing)
            Tok next = tok_peek(&q);
            bool can_follow = *q.pos == '(' || *q.pos == '`' || (!tok_is(next, "<") && !tok_is(next, ">") &&
                !tok_is(next, "+") && !tok_is(next, "-") && (q.whitespace == WHITESPACE_NEWLINE ||
                tok_is_binary_operator_here(&q, next) || !tok_is_start_of_expression(&q, next)));
            if (can_follow) {
                *left_and_ret = expr_generic_params(span_from(args_a), ast_new(GenericParamsExpr, {
                    .expr = *left_and_ret,
                    .params = params,
                }));
                parse_success(&q);
                // (Before a template: a tagged template with type arguments, in the chain, TS1358: tsc's
                // parseCallExpressionRest's parseTaggedTemplateRest)
                if (*p.pos == '`') {
                    generic_optional_template = true;
                } else {
                    optional_chain = true;
                    missing_call_args = *p.pos != '(';
                }
            } else {
                vec_spanned_type_drop(&params);
                parse_fail(&q);
            }
        } else {
            parse_fail(&q);
        }
    }

    if (missing_call_args) {
        // `f?.<T>;`: a call missing its argument list (tsc's parseArgumentList: '(' expected)
        expected_token_error(&p, "(");
        parsed.args = (VecExpr){0};
    }
    if (!ctx.entity_name && new_keyword_precedence > ctx.min_precedence && (missing_call_args || parse_call_args(&p, &parsed.args))) {
        mark_chain_continues(left_and_ret, true, optional_chain);
        *left_and_ret = expr_call(
            span_from(a),
            ast_new(CallExpr, {
                .uses_new = false,
                .optional_chain = optional_chain,
                .callable = *left_and_ret,
                .args = parsed.args,
            })
        );
        // (An immediately invoked function is part of the flow around it: its body isn't deferred, plan 78)
        Expr* callee = &left_and_ret->call->callable;
        while (callee->tag == EXPR_GENERIC_PARAMS) callee = &callee->generic_params->expr;
        if (callee->tag == EXPR_FUNCTION) undefer_body(&p, callee->function);
        // (`import("m", options)`: its attributes, binder_find_module())
        if (left_and_ret->call->callable.tag == EXPR_IDENTIFIER && left_and_ret->call->callable.identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
            identifier_module_ref(&left_and_ret->call->callable.identifier)
        ) {
            module_ref_set_attributes(identifier_module_ref(&left_and_ret->call->callable.identifier), nullptr, left_and_ret->call);
        }
        if (p.shared->source->is_js && !optional_chain && expr_is_require_call(left_and_ret)) {
            // tsc's collectRequireCalls: `require("m")` names a module in a JavaScript file, wherever it is
            p.shared->saw_commonjs = true;
            left_and_ret->call->callable.identifier.require_call = true;
            Expr const* arg = vec_expr_at(&left_and_ret->call->args, 0);
            if (arg->tag == EXPR_LITERAL && arg->literal.tag == VALUE_STRING) {
                ModuleRef* ref = add_module_ref(&p, cstr_clone(arg->literal.string), arg->inner_span);
                ref->syntax = MODULE_REF_REQUIRE;
                ref->require_call = true;
                left_and_ret->call->callable.identifier.module_ref_ = ref;
            }
        } else if (left_and_ret->call->callable.tag == EXPR_GENERIC_PARAMS && !optional_chain &&
            left_and_ret->call->callable.generic_params->expr.tag == EXPR_IDENTIFIER &&
            left_and_ret->call->callable.generic_params->expr.identifier.keyword == KEYWORD_IDENTIFIER_IMPORT &&
            !left_and_ret->call->callable.generic_params->expr.identifier.import_source &&
            vec_expr_size(&left_and_ret->call->args) &&
            vec_expr_at(&left_and_ret->call->args, 0)->tag == EXPR_LITERAL &&
            vec_expr_at(&left_and_ret->call->args, 0)->literal.tag == VALUE_STRING
        ) {
            // `import<T>("./a")` (TS1326) is an import call all the same (tsc's collectDynamicImportOrRequireOrJsDocImportCalls)
            Expr const* arg = vec_expr_at(&left_and_ret->call->args, 0);
            ModuleRef* ref = add_module_ref(&p, cstr_clone(arg->literal.string), arg->inner_span);
            ref->syntax = MODULE_REF_DYNAMIC;
            module_ref_set_attributes(ref, nullptr, left_and_ret->call);
            left_and_ret->call->callable.generic_params->expr.identifier.module_ref_ = ref;
        } else if (p.shared->source->is_js && expr_is_define_property_call(left_and_ret, nullptr) &&
            expr_is_exports_object(vec_expr_at(&left_and_ret->call->args, 0))
        ) {
            // `Object.defineProperty(exports, "x", {...})` makes it CommonJS too (tsc's bindObjectDefinePropertyExport)
            p.shared->saw_commonjs = true;
        }
        return parse_success(&p);
    }

    // (A tagged template is part of a member expression, `new` included: tsc's parseMemberExpressionRest). After `?.`
    // too, which the checker refuses (TS1358)
    bool optional_template = false;
    if (!ctx.entity_name && new_keyword_precedence >= ctx.min_precedence && p.pos[0] == '?' && p.pos[1] == '.') {
        Parser q = parser(&p);
        q.pos += 2;
        hop_whitespace(&q);
        optional_template = *q.pos == '`';
        if (optional_template) parse_success(&q);
        else parse_fail(&q);
    }
    if (!ctx.entity_name && new_keyword_precedence >= ctx.min_precedence && *p.pos == '`') {
        Anchor template_a = anchor(&p);
        cstr plain_string = cstr_null;
        if (left_and_ret->tag == EXPR_TEMPLATE && !expr_is_perenthisized(left_and_ret)) left_and_ret->template_->is_tag = true;
        if (!parse_template(&p, &parsed.args, nullptr)) {
            parsed.args = vec_expr_init();
            parse_string_or_tagged(&p, &plain_string, false);
            ast_cstr_seal(&plain_string);
        }
        // (tsc's checkGrammarTaggedTemplateChain: grammarErrorOnNode of the whole template. parseTaggedTemplateRest flags
        // the tagged template as part of a chain whose tag is: `o?.a\`x\``, `o?.a\`x\`.b\`y\``)
        bool chain = optional_template || generic_optional_template || tag_in_optional_chain(left_and_ret);
        if (chain) {
            grammar_error(&p, span_from(template_a), 1358, "Tagged template expressions are not permitted in an optional chain.");
            // (checkTaggedTemplateExpression checks the type arguments only without TS1358: `o?.a<>\`x\`` has no
            // TS1099. Rare, so the file's errors are searched)
            bool generic = left_and_ret->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(left_and_ret);
            Parser q = parser(&p);
            q.pos = span_end_ptr(expr_outer_span(generic ? &left_and_ret->generic_params->expr : left_and_ret));
            hop_whitespace(&q);
            const char* open = q.pos;
            parse_fail(&q);
            if (*open == '<') {
                const char* c = template_a.start;
                while (c > open && *c != '>') c--;
                while (c > open && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r')) c--;
                if (c > open && c[-1] == ',') grammar_drop_since(&p, 0, 1009, c - 1);
                grammar_drop_since(&p, 0, 1099, open);
            }
        }
        *left_and_ret = expr_template(
            span_from(a),
            ast_new(TemplateExpr, {
                .tag = ast_new(Expr, *left_and_ret),
                .template_span = span_from(template_a),
                .substitutions = parsed.args,
                .plain_string = plain_string,
                .incomplete = p.pos - 1 <= template_a.start || p.pos[-1] != '`',
                .optional_chain = chain,
            })
        );
        return parse_success(&p);
    }

    Span prop_name;
    if (parse_dot_prop(&p, ctx.entity_name, &parsed.optional_chain, &prop_name)) {
        if (left_and_ret->tag == EXPR_GENERIC_PARAMS && !expr_is_perenthisized(left_and_ret)) {
            // `f<T>.x`, `f<T>?.x` too (tsc's parsePropertyAccessExpressionRest): over the type arguments, `<` to `>`
            const char* open = span_end_ptr(expr_outer_span(&left_and_ret->generic_params->expr));
            while (*open && *open != '<') open++;
            parse_error(&p, span_make(p.shared->source, open, span_end_ptr(expr_outer_span(left_and_ret))), 1477,
                "An instantiation expression cannot be followed by a property access.");
        }
        mark_chain_continues(left_and_ret, false, parsed.optional_chain);
        *left_and_ret = expr_property(
            span_from(a),
            property_dot_new(&(PropertyExpr){
                .expr = *left_and_ret,
                .name_ = prop_name,
                .optional_chain = parsed.optional_chain,
            })
        );
        if (prop_name.size && prop_name.buf[0] == '#' && expr_is_optional_chain(left_and_ret)) {
            // tsc's parsePropertyAccessExpressionRest
            parse_error(&p, prop_name, 18030, "An optional chain cannot contain private identifiers.");
        }
        return parse_success(&p);
    }

    if (!ctx.entity_name && !(ctx.decorator && !optional_chain) && parse_bracket_prop(&p, &parsed.expr_a)) {
        mark_chain_continues(left_and_ret, false, optional_chain);
        *left_and_ret = expr_property(
            span_from(a),
            ast_new(PropertyExpr, {
                .expr = *left_and_ret,
                .index = parsed.expr_a,
                .form = PROPERTY_EXPR_BRACKET,
                .optional_chain = optional_chain,
            })
        );
        return parse_success(&p);
    }
    if (optional_chain) {
        return parse_fail(&p);
    }

    // (What tsc's parseBinaryExpressionRest continues: `x as T` and `x satisfies T`, then the operators. An update
    // expression or JSX element only takes these, `a++ as T`)
    bool binary_rest_only = false;
    if (false) {
binary_rest:
        binary_rest_only = true;
    }
    Span as_type_span;
    bool is_satisfies = false;
    if ((c == 'a' || c == 's' || c == '\\') && !ctx.reject_operators && as_type_precedence > ctx.min_precedence &&
        parse_as_type(&p, &parsed.type, &as_type_span, &is_satisfies)
    ) {
        *left_and_ret = expr_as_type(
            span_from(a),
            ast_new(AsTypeExpr, {
                .expr = *left_and_ret,
                .type = parsed.type,
                .keyword_span = as_type_span,
                .angle_bracket_syntax = false,
                .is_satisfies = is_satisfies,
            })
        );
        return parse_success(&p);
    }
    if (binary_rest_only) goto operators;

    // (Not in a left-hand side expression, a heritage element: tsc's parseLeftHandSideExpressionOrHigher parses no
    // arrow function, `implements x => void` is `x` then TS1005 ',' expected)
    if (left_and_ret->tag == EXPR_IDENTIFIER && *p.pos == '=' && *(p.pos + 1) == '>' && !ctx.entity_name &&
        !ctx.left_hand_side
    ) {
        Parser sub_p = parser(&p);
        sub_p.pos = span_start_ptr(expr_outer_span(left_and_ret));

        if (parse_arrow_function(&sub_p, ctx, function_flags_null, false, &parsed.function)) {
            parse_success(&sub_p);
            expr_drop(left_and_ret);
            *left_and_ret = expr_function(span_from(a), ast_new(FunctionImpl, parsed.function));
            if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, left_and_ret);
            return parse_success(&p);
        } else {
            parse_fail(&sub_p);
        }
    }

    Parser generic_p = parser(&p);
    // Never in JavaScript files, to avoid the ambiguity with `<` and `>` (tsc's parseTypeArgumentsInExpression), but
    // a `typeof` type's are a type's (a JSDoc `{typeof f<T>}`), on its line: tsc's parseTypeQuery
    bool outer_import_type_args = p.shared->import_type_args;
    p.shared->import_type_args = left_and_ret->tag == EXPR_IDENTIFIER && left_and_ret->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT;
    // (A heritage element's are tsc's tryParseTypeArguments, which wants a `<` token: `extends f<<T>() => T>` has a `<<`,
    // which only an expression's parseTypeArgumentsInExpression rescans)
    bool heritage_shift = ctx.reject_operators && !ctx.decorator && !ctx.entity_name && c == '<' && p.pos[1] == '<';
    bool type_args = c == '<' && !heritage_shift && (!p.shared->source->is_js || ctx.entity_name) &&
        !(ctx.entity_name && hit_newline) && parse_generic_params(&generic_p, true, &parsed.generic_params);
    p.shared->import_type_args = outer_import_type_args;
    if (type_args) {
        bool success = false;
        // (A decorator's type arguments are an expression's: `@g<T> class C {}` is `@g` and `<T> class C {}`)
        if (ctx.reject_operators && !ctx.decorator) {
            success = true;
        } else if (*generic_p.pos == '>' || *generic_p.pos == '=') {
            // Fail
        } else if (true) {
            Expr expr;
            Parser next_expr_p = parser(&generic_p);
            hop_whitespace(&next_expr_p);
            ExprCtx next_expr_ctx = ctx;
            next_expr_ctx.min_precedence = imax(ctx.min_precedence, binary_op_get_precedence(BINARY_OP_GREATER));
            // (tsc's canFollowTypeArgumentsInExpression: a `(` or a template, even on the next line, takes them, and
            // so does a binary operator, `as` and `satisfies` too, which would otherwise read as names)
            if (next_expr_p.whitespace != WHITESPACE_NEWLINE &&
                *next_expr_p.pos != '(' && *next_expr_p.pos != '`' &&
                !starts_with_followed_by_word_break(&next_expr_p, "as") &&
                !starts_with_followed_by_word_break(&next_expr_p, "satisfies") &&
                parse_expr(&next_expr_p, next_expr_ctx, &expr)
            ) {
                expr_drop(&expr);
            } else {
                success = true;
            }
            parse_fail(&next_expr_p);
        }
        if (success) {
            parse_success(&generic_p);
            // (`import<>()` keeps its empty list: TS1326, where an empty list is otherwise only TS1099)
            bool import_keyword = left_and_ret->tag == EXPR_IDENTIFIER && left_and_ret->identifier.keyword == KEYWORD_IDENTIFIER_IMPORT;
            // (An instantiation expression `f<>` keeps it too: its type is f's, tsc's getInstantiationExpressionType
            // without type arguments, but it is a node of its own in the types baseline. Not a call's)
            Parser after = parser(&generic_p);
            hop_whitespace(&after);
            // (A type query's `typeof f<>` too: tsc's parseTypeQuery keeps the empty list, which instantiates)
            bool instantiation = (*after.pos != '(' && *after.pos != '`' && !ctx.reject_operators) || ctx.entity_name;
            parse_fail(&after);
            if (vec_spanned_type_size(&parsed.generic_params) || import_keyword || instantiation) {
                *left_and_ret = expr_generic_params(
                    span_from(a),
                    ast_new(GenericParamsExpr, {
                        .expr = *left_and_ret,
                        .params = parsed.generic_params,
                    })
                );
            }
            return parse_success(&p);
        } else {
            parse_fail(&generic_p);
            vec_spanned_type_drop(&parsed.generic_params);
        }
    } else {
        parse_fail(&generic_p);
    }

    // (A left-hand side expression, a decorator or heritage element, still takes a non-null `!`: tsc's
    // parseMemberExpressionRest)
    bool non_null_only = ctx.reject_operators && !ctx.entity_name && *p.pos == '!' && p.pos[1] != '=';
    // (Not after a `?.` missing its name: tsc's parseMemberExpressionRest stops there, the `!` starts the next
    // statement, `a?.!b`)
    bool after_missing_optional_name = *p.pos == '!' && left_and_ret->tag == EXPR_PROPERTY && !expr_is_perenthisized(left_and_ret) &&
        left_and_ret->property->optional_chain && left_and_ret->property->form != PROPERTY_EXPR_BRACKET &&
        !left_and_ret->property->name_.size;
    if ((!ctx.reject_operators || non_null_only) && !hit_newline && !after_missing_optional_name) {
        if (parse_right_unary_op(&p, &parsed.unary) &&
            // A postfix `++`/`--` takes a left-hand side expression (tsc's parseUpdateExpression): not in the operand
            // of a prefix one, `--a--`, and not after an operator expression, `--a --`
            !((parsed.unary == UNARY_OP_RIGHT_PLUS_PLUS || parsed.unary == UNARY_OP_RIGHT_MINUS_MINUS) &&
                (ctx.min_precedence > unary_op_get_precedence(UNARY_OP_LEFT_PLUS_PLUS) ||
                    (!expr_is_perenthisized(left_and_ret) && ((left_and_ret->tag == EXPR_UNARY_OP &&
                        left_and_ret->unary_op->op != UNARY_OP_RIGHT_BANG) || left_and_ret->tag == EXPR_BINARY_OP ||
                        left_and_ret->tag == EXPR_TERNARY ||
                        // (Not a JSDoc assertion, `/** @type {T} */ (x)++`: parenthesized, its type comes first)
                        (left_and_ret->tag == EXPR_AS_TYPE && span_start_ptr(left_and_ret->as_type->keyword_span) >
                            span_start_ptr(expr_outer_span(&left_and_ret->as_type->expr)))))))
        ) {
            if (parsed.unary == UNARY_OP_RIGHT_BANG) {
                js_syntax_error(&p, span_from(a), 8013, "Non-null assertions can only be used in TypeScript files.");
            }
            *left_and_ret = expr_unary_op(
                span_from(a),
                ast_new(UnaryOpExpr, {
                    .op = parsed.unary,
                    .expr = *left_and_ret,
                })
            );
            return parse_success(&p);
        }
    }

operators:
    if (!ctx.reject_operators && parse_binary_op_expr(&p, ctx, left_and_ret)) {
        return parse_success(&p);
    }

    if (!ctx.reject_operators && ternary_precedence >= ctx.min_precedence && parse_ternary_expr(&p, ctx, &parsed.expr_a, &parsed.expr_b)) {
        *left_and_ret = expr_ternary(
            span_from(a),
            ast_new(TernaryExpr, {
                .condition = *left_and_ret,
                .yes = parsed.expr_a,
                .no = parsed.expr_b,
            })
        );
        return parse_success(&p);
    }

    return parse_fail(&p);
}

[[gnu::noinline]] static bool parse_comma_ops_right_side_slow(Parser* parent, Expr* left_and_ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_comma_ops_right_side(Parser* parent, Expr* left_and_ret) {
    // (No trivia first and no comma: no child parser)
    char first = *parent->pos;
    if ((unsigned char)first > ' ' && (unsigned char)first < 0x80 && first != '/' && first != ',') {
        GATE_FAIL(parse_comma_ops_right_side(&v_, left_and_ret));
    }
    return parse_comma_ops_right_side_slow(parent, left_and_ret);
}

[[gnu::noinline]] static bool parse_comma_ops_right_side_slow(Parser* parent, Expr* left_and_ret) {
    Parser p = parser(parent);
    Anchor a = span_starting_at(&p, expr_outer_span(left_and_ret));
    Expr expr;
    bool parsed_commas = false;

    hop_whitespace(&p);

    while (hop_char(&p, ',')) {
        parsed_commas = true;
        hop_whitespace(&p);

        // (A missing operand is still the right side, like tsc's missing identifier: `(a, )` checks `a`)
        bool missing = !expect_expr(&p, expr_ctx_null, &expr);

        expr_mark_binary_operand(left_and_ret);
        expr_mark_binary_operand(&expr);
        *left_and_ret = expr_binary_op(
            span_from(a),
            ast_new(BinaryOpExpr, {
                .left = *left_and_ret,
                .op = BINARY_OP_COMMA,
                .right = expr,
            })
        );
        if (missing) return parse_success(&p);
        // (The next comma may follow whitespace; what follows the last operand stays unparsed)
        Parser ahead = parser(&p);
        hop_whitespace(&ahead);
        if (*ahead.pos != ',') {
            parse_fail(&ahead);
            break;
        }
        parse_success(&ahead);
    }
    return parsed_commas ? parse_success(&p) : parse_fail(&p);
}

// A template literal with substitutions, and if `texts` isn't null the cooked texts around them (one more than there
// are substitutions). Fails if there are no substitutions, a plain template is a string literal (see parse_string())
static bool parse_template(Parser* parent, VecExpr* ret, AstCStrs* texts) {
    Parser p = parser(parent);

    if (*p.pos != '`') {
        return parse_fail(&p);
    }
    p.pos++;

    VecExpr substitutions = vec_expr_init();
    AstCStrs cooked = ast_cstrs_init();
    cstr text = cstr_null;
    const char* segment = p.pos - 1; // Where the token of the text being read starts: the backtick or a `}`
    isize segment_errors = vec_error_size(&p.shared->errors);
    // A missing `}` ends the template (tsc's parseTemplateSpan: a missing template tail)
    bool missing_tail = false;
    while ((*p.pos || nul_inside_file(&p)) && *p.pos != '`') {
        if (*p.pos == '\\' && !p.pos[1] && !nul_inside_file(&p)) {
            p.pos++;
            parse_error(&p, span_from_pos(&p), 1126, "Unexpected end of text.");
        } else if (*p.pos == '\\') {
            p.pos++;
            EscapeErrors errors = {0};
            // (An invalid escape is fine in a tagged template)
            if (!parse_string_escape(&p, &text, &errors, !texts) && texts) report_escape_errors(&p, &errors);
        } else if (*p.pos == '\r') {
            if (p.pos[1] != '\n') cstr_append_lit(&text, "\n"); // (a lone \r is a line break too)
            p.pos++;
        } else if (p.pos[0] == '$' && p.pos[1] == '{') {
            ast_cstr_seal(&text);
            ast_cstrs_push(&cooked, text);
            text = cstr_null;
            p.pos += 2;
            hop_whitespace(&p);
            // Committed from the `${` on: a missing expression is one
            Expr expr;
            bool saved_in = allow_in_enter(&p);
            if (expect_expr(&p, expr_ctx_null, &expr)) parse_comma_ops_right_side(&p, &expr);
            allow_in_leave(&p, saved_in);
            list_push(vec_expr, &substitutions, expr);
            hop_whitespace(&p);
            if (!expect_char(&p, '}')) {
                missing_tail = true;
                unhop_trivia(&p); // (tsc's missing template tail is at the token's full start)
                break;
            }
            segment = p.pos - 1;
            segment_errors = vec_error_size(&p.shared->errors);
        } else {
            // (This character and the plain ones after it at once: one append per character was 1.3% of excalidraw)
            const char* run = p.pos + 1;
            while (*run && *run != '\\' && *run != '\r' && *run != '`' && *run != '$') run++;
            cstr_append_n(&text, p.pos, run - p.pos);
            p.pos = run;
        }
    }
    ast_cstr_seal(&text);
    ast_cstrs_push(&cooked, text);

    if (vec_expr_is_empty(&substitutions)) {
        vec_expr_drop(&substitutions);
        ast_cstrs_drop(&cooked);
        return parse_fail(&p);
    }
    if (texts) *texts = cooked; else ast_cstrs_drop(&cooked);

    if (*p.pos == '`') {
        p.pos++;
    } else if (!missing_tail) {
        scanner_error(&p, segment, segment_errors, span_from_pos(&p), 1160, "Unterminated template literal.");
    }

    shrink_list(vec_expr, &substitutions);
    if (texts) shrink_list(ast_cstrs, texts);
    *ret = substitutions;
    return parse_success(&p);
}

// Zero or more decorators (`@a @b.c(d)`), fails if there are none. Whitespace after them is hopped
// The `?.` after an expression
static Span question_dot_after(Parser* p, Expr const* e) {
    const char* c = span_end_ptr(expr_outer_span(e));
    while (*c && *c != '?') c++;
    return span_make(p->shared->source, c, c + 2);
}

// tsc's checkGrammarDecorator: a decorator that isn't `(expr)` is an identifier and `.name`s, maybe called once at the
// end (non-null assertions and type arguments anywhere): TS1497, with TS1498 on the first part that isn't
static void decorator_check_grammar(Parser* p, Expr const* decorator) {
    if (expr_is_perenthisized(decorator)) return;
    Expr const* node = decorator;
    bool can_call = true;
    Span error_at = span_null;
    while (true) {
        bool paren = expr_is_perenthisized(node);
        if (!paren && node->tag == EXPR_GENERIC_PARAMS) {
            node = &node->generic_params->expr;
        } else if (!paren && node->tag == EXPR_UNARY_OP && node->unary_op->op == UNARY_OP_RIGHT_BANG) {
            node = &node->unary_op->expr;
        } else if (!paren && node->tag == EXPR_CALL && !node->call->uses_new) {
            if (!can_call) error_at = expr_outer_span(node);
            if (node->call->optional_chain) error_at = question_dot_after(p, &node->call->callable);
            node = &node->call->callable;
            can_call = false;
        } else if (!paren && node->tag == EXPR_PROPERTY && node->property->form == PROPERTY_EXPR_DOT) {
            if (node->property->optional_chain) error_at = question_dot_after(p, &node->property->expr);
            node = &node->property->expr;
            can_call = false;
        } else {
            if (paren || node->tag != EXPR_IDENTIFIER || node->identifier.keyword) error_at = expr_outer_span(node);
            break;
        }
    }
    if (!span_valid(error_at)) return;
    Error* e = grammar_error(p, expr_outer_span(decorator), 1497,
        "Expression must be enclosed in parentheses to be used as a decorator.");
    error_add_related_d(e, error_at, 1498, "Invalid syntax in decorator.");
}

static bool parse_decorators(Parser* parent, VecExpr* ret) {
    if (*parent->pos != '@') {
        return false;
    }
    Parser p = parser(parent);
    *ret = vec_expr_init();
    bool in_async = p.shared->in_async, in_parameters = p.shared->in_parameters;
    // (A parameter's decorators are in the function's surroundings: their `await`s are the outer function's)
    if (in_parameters) {
        p.shared->in_async = p.shared->outer_in_async;
        p.shared->function_depth--;
    }
    p.shared->in_parameters = false; // (A parameter's decorator is no initializer: no TS2524)
    while (hop_char(&p, '@')) {
        hop_whitespace(&p);
        // Member accesses and calls, anything more has to be in parentheses
        ExprCtx ctx = expr_ctx_null;
        ctx.reject_operators = true;
        ctx.decorator = true;
        Expr expr;
        // A decorator is a left-hand side expression (tsc's parseLeftHandSideExpressionOrHigher): a unary operator or
        // a type assertion starts none, it's a missing identifier, then whatever member accesses and calls follow
        // (`@!x` is a non-null assertion of the missing identifier)
        u32 unary_size = 0;
        if (strchr("!~+-<", *p.pos) && *p.pos) unary_size = 1;
        else if (starts_with_followed_by_word_break(&p, "typeof") || starts_with_followed_by_word_break(&p, "delete")) unary_size = 6;
        else if (starts_with_followed_by_word_break(&p, "void")) unary_size = 4;
        if (unary_size) {
            if ((p.pos[0] == '+' || p.pos[0] == '-') && p.pos[1] == p.pos[0]) unary_size = 2;
            parse_error(&p, span_make(p.shared->source, p.pos, p.pos + unary_size), 1109, "Expression expected.");
            expr = expr_implicit_any(span_make(p.shared->source, p.pos, p.pos));
            expr.missing = true;
            parse_expr_right_side(&p, ctx, &expr);
            vec_expr_push(ret, expr);
        } else if ((p.shared->in_async || p.shared->in_static_block) && starts_with_followed_by_word_break(&p, "await")) {
            // tsc's parseDecoratorExpression: `@await` in an await context is a missing identifier, then the rest
            parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 5), 1109, "Expression expected.");
            expr = expr_implicit_any(span_make(p.shared->source, p.pos, p.pos));
            p.pos += 5;
            parse_expr_right_side(&p, ctx, &expr);
            vec_expr_push(ret, expr);
        } else if (!p.shared->in_static_block && (starts_with_followed_by_word_break(&p, "await") ||
            (!p.shared->in_generator && starts_with_followed_by_word_break(&p, "yield")))
        ) {
            // (Outside an await context `await` is an identifier here, no operator: tsc's parseLeftHandSideExpressionOrHigher
            // has no unary expressions, `@await y` decorates `y`)
            Span name = span_make(p.shared->source, p.pos, p.pos + 5);
            p.pos += 5;
            note_top_level_await_identifier(&p, span_sv(name));
            expr = expr_identifier(name, (IdentifierExpr){0});
            parse_expr_right_side(&p, ctx, &expr);
            decorator_check_grammar(&p, &expr);
            vec_expr_push(ret, expr);
        } else if (expect_expr(&p, ctx, &expr)) {
            decorator_check_grammar(&p, &expr);
            vec_expr_push(ret, expr);
        } else if (expr.tag == EXPR_IMPLICIT_ANY && expr.missing) {
            vec_expr_push(ret, expr); // (tsc's missing identifier)
        }
        hop_whitespace(&p);
    }
    p.shared->in_async = in_async;
    p.shared->in_parameters = in_parameters;
    if (in_parameters) p.shared->function_depth++;
    return parse_success(&p);
}

// `async function () {}`, `async () => {}`, `async x => {}`
[[gnu::noinline]] static bool parse_async_function_expr_slow(Parser* parent, ExprCtx ctx, FunctionImpl* ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_async_function_expr(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    if (!may_start_keyword(parent->pos, "async")) GATE_FAIL(parse_async_function_expr(&v_, ctx, ret));
    return parse_async_function_expr_slow(parent, ctx, ret);
}

[[gnu::noinline]] static bool parse_async_function_expr_slow(Parser* parent, ExprCtx ctx, FunctionImpl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!starts_with_followed_by_word_break(&p, "async")) {
        return parse_fail(&p);
    }
    Tristate arrow_tristate = arrow_function_tristate(&p);
    hop_start_if_ends_on_word_break(&p, "async");
    hop_whitespace(&p);
    if (p.whitespace == WHITESPACE_NEWLINE) {
        return parse_fail(&p);
    }

    if (!parse_function_keyword_function(&p, true, false, ret) &&
        !parse_arrow_function(&p, ctx, (FunctionFlags){ .is_async = true }, arrow_tristate == TRISTATE_TRUE, ret)
    ) {
        return parse_fail(&p);
    }
    // (tsc's checkGrammarModifiers: `declare const f = async () => {}`)
    if (p.shared->in_ambient) {
        grammar_error(&p, span_make(p.shared->source, a.start, a.start + 5), 1040, "'async' modifier cannot be used in an ambient context.");
    }

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_identifier_expr(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    Span name;
    // (A reserved word is no identifier: where no other expression starts with it, the expression is missing, tsc's
    // parsePrimaryExpression)
    if (!parse_identifier(&p, &name) || is_reserved_word(span_sv(name))) {
        return parse_fail(&p);
    }
    // (Spelled with escapes it is the reserved word all the same: tsc's scanner makes `\u0065lse` ElseKeyword)
    if (memchr(name.buf, '\\', (size_t)name.size)) {
        cstr cooked = identifier_name_value(span_sv(name));
        bool reserved = is_reserved_word(cstr_sv(&cooked));
        cstr_drop(&cooked);
        if (reserved) return parse_fail(&p);
    }
    note_top_level_await_identifier(&p, span_sv(name));
    *ret = expr_identifier(name, (IdentifierExpr){0});

    return parse_success(&p);
}

// JSX (tsc's parseJsxElementOrSelfClosingElementOrFragment and friends). There is no JSX scanner mode: text between
// tags is read straight from the source, where `//` isn't a comment and `'` isn't a string

static bool earlier_binary_error(Parser const* me, intptr_t before) {
    for (intptr_t i = 0; i < before; i++) {
        if (vec_error_at(&my shared->errors, i)->ts_code == 1490) return true;
    }
    return false;
}

static bool last_error_starts_at(Parser* me, Span span) {
    for (intptr_t i = vec_error_size(&my shared->errors); i-- > 0;) {
        Error const* last = vec_error_at(&my shared->errors, i);
        if (is_js_syntax_code(last->ts_code) || last->grammar || is_semantic_code(last->ts_code)) continue;
        // (tsc scans the binary marker once, tttt's hops cross it again: only the first TS1490 counts)
        if (last->ts_code == 1490 && earlier_binary_error(me, i)) continue;
        // (A merge conflict marker is trivia, which tsc's scanner reports once it scans the token after it, later than
        // tttt's hops: one past the error's start isn't before it, `st\u0061tic` then a marker on the next line)
        if (last->ts_code == 1185 && span_source(last->span) == span_source(span) &&
            span_start_ptr(last->span) > span_start_ptr(span)) continue;
        // A scanner error came before the errors tttt reported at or after its token (tsc's scanner reports it when
        // the token is scanned): the latest of those is tsc's last error
        if (i == my shared->scanner_error_index && my shared->scanner_error_token && my shared->scanner_error_first <= i) {
            for (intptr_t j = my shared->scanner_error_first; j-- > 0;) {
                Error const* before = vec_error_at(&my shared->errors, j);
                if (is_js_syntax_code(before->ts_code) || before->grammar ||
                    is_semantic_code(before->ts_code)) continue;
                if (span_source(before->span) == span_source(last->span) && span_start_ptr(before->span) >= my shared->scanner_error_token) {
                    last = before;
                }
                break;
            }
        }
        return span_source(last->span) == span_source(span) && span_start_ptr(last->span) == span_start_ptr(span);
    }
    return false;
}

// A unicode escape in an identifier (`\u0061`, `\u{61}`) at c: its length, 0 if there is none
static int identifier_escape_size(const char* c) {
    if (c[0] != '\\' || c[1] != 'u') return 0;
    if (c[2] == '{') {
        int i = 3;
        while (hex_digit_value(c[i]) >= 0) i++;
        return i > 3 && c[i] == '}' ? i + 1 : 0;
    }
    for (int i = 2; i < 6; i++) {
        if (hex_digit_value(c[i]) < 0) return 0;
    }
    return 6;
}

// A JSX identifier: an identifier name (keywords too) that may go on with hyphens, `data-x`, `my-elem` (not after a
// dot: `hyphens`). Unicode escapes are read, and are TS17021
static bool parse_jsx_identifier_hyphens(Parser* parent, bool hyphens, Span* ret) {
    Anchor a = anchor(parent);
    int escape = identifier_escape_size(parent->pos);
    if (escape) {
        parent->pos += escape;
    } else if (!parse_identifier_or_keyword(parent, nullptr)) {
        return false;
    }
    while (true) {
        int size = hyphens && *parent->pos == '-' ? 1 : identifier_char_size(parent->pos, false);
        if (!size) size = identifier_escape_size(parent->pos);
        if (!size) break;
        parent->pos += size;
    }
    *ret = span_from(a);
    if (memchr(ret->buf, '\\', ret->size)) {
        parse_error(parent, *ret, 17021, "Unicode escape sequence cannot appear here.");
    }
    return true;
}

// A tag, namespace or attribute name: a private name is one too (tsc's parseIdentifierName takes any token
// tokenIsIdentifierOrKeyword says yes to, PrivateIdentifier included: `<#a />` is TS2304, `<a #b />` an attribute)
static bool parse_jsx_identifier(Parser* parent, Span* ret) {
    if (*parent->pos == '#' && identifier_char_size(parent->pos + 1, true)) {
        Anchor a = anchor(parent);
        parent->pos++;
        if (!parse_jsx_identifier_hyphens(parent, true, ret)) impossible;
        *ret = span_from(a);
        return true;
    }
    return parse_jsx_identifier_hyphens(parent, true, ret);
}

// `ns:name` after the first part of a tag or attribute name (at `:`, maybe after whitespace). False if there's no `:`
static bool parse_jsx_namespace_rest(Parser* parent) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (!hop_char(&p, ':')) return parse_fail(&p);
    hop_whitespace(&p);
    Span local;
    if (!parse_jsx_identifier(&p, &local)) {
        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
    }
    return parse_success(&p);
}

// The token at p, where `</` is one token in a JSX file
static Span jsx_span_from_token(Parser* p) {
    if (p->pos[0] == '<' && p->pos[1] == '/') return span_make(p->shared->source, p->pos, p->pos + 2);
    return span_from_token(p);
}

// tsc's parseJsxElementName: an identifier, `this`, a namespaced name `a:b`, or property accesses on the first two
static cstr identifier_name_value(csview name);

static void parse_jsx_tag_name(Parser* p, Expr* ret, bool* namespaced, bool* broken) {
    Anchor a = anchor(p);
    *namespaced = false;
    Span name;
    if (!parse_jsx_identifier(p, &name)) {
        parse_error(p, jsx_span_from_token(p), 1003, "Identifier expected.");
        *ret = expr_identifier(span_from_pos(p), (IdentifierExpr){0});
        return;
    }
    if (parse_jsx_namespace_rest(p)) {
        *namespaced = true;
        *ret = expr_identifier(span_from(a), (IdentifierExpr){0});
        return;
    }
    if (span_equals(name, "this")) {
        p->shared->this_count++;
        *ret = expr_identifier(name, (IdentifierExpr){ .keyword = KEYWORD_IDENTIFIER_THIS });
    } else {
        *ret = expr_identifier(name, (IdentifierExpr){0});
    }
    while (true) {
        Parser dot = parser(p);
        hop_whitespace(&dot);
        // (`...` is a token of its own, no dot: `<a ...x />`)
        if ((dot.pos[0] == '.' && dot.pos[1] == '.' && dot.pos[2] == '.') || !hop_char(&dot, '.')) {
            parse_fail(&dot);
            break;
        }
        hop_whitespace(&dot);
        Span prop;
        if (!parse_jsx_identifier_hyphens(&dot, false, &prop)) {
            *broken = true;
            // (a private name is read and dropped, tsc's parseRightSideOfDot, which reports after it)
            if (*dot.pos == '#' && identifier_char_size(dot.pos + 1, true)) {
                dot.pos++;
                parse_identifier_or_keyword(&dot, nullptr);
                parse_error(&dot, span_from_pos(&dot), 1003, "Identifier expected.");
                // (the access has a missing name after it: `this.#prop` is a property access to the types baseline)
                Span missing = span_from_pos(&dot);
                parse_success(&dot);
                *ret = expr_property(span_from(a), property_dot_new(&(PropertyExpr){ .expr = *ret, .name_ = missing }));
                break;
            } else {
                parse_error(&dot, span_from_token_or_eof(&dot), 1003, "Identifier expected.");
                // (a property access all the same, of a missing name: `<a.>` is no intrinsic element, tsc's
                // parseJsxElementName)
                Span missing = span_from_pos(&dot);
                parse_success(&dot);
                *ret = expr_property(span_from(a), property_dot_new(&(PropertyExpr){ .expr = *ret, .name_ = missing }));
                break;
            }
            parse_success(&dot);
            break;
        }
        parse_success(&dot);
        *ret = expr_property(span_from(a), property_dot_new(&(PropertyExpr){ .expr = *ret, .name_ = prop }));
    }
}

// A JSX name with its unicode escapes decoded
// An identifier's value, its unicode escapes decoded (tsc's escapedText)
static cstr identifier_name_value(csview name) {
    if (name.size <= 0 || !memchr(name.buf, '\\', (size_t)name.size)) return cstr_from_sv(name);
    cstr ret = cstr_null;
    for (isize i = 0; i < name.size;) {
        int escape = identifier_escape_size(name.buf + i);
        if (!escape) {
            cstr_append_n(&ret, name.buf + i, 1);
            i++;
            continue;
        }
        u32 code = 0;
        for (int j = 2; j < escape; j++) {
            int v = hex_digit_value(name.buf[i + j]);
            if (v >= 0) code = code * 16 + (u32)v;
        }
        append_code_point(&ret, code);
        i += escape;
    }
    return ret;
}

// tsc's tagNamesAreEquivalent
static bool jsx_tag_names_equal(Expr const* a, Expr const* b) {
    if (a->tag != b->tag) return false;
    switch (a->tag) {
        case EXPR_IDENTIFIER: {
            if (a->identifier.keyword != b->identifier.keyword) return false;
            csview x = span_sv(a->inner_span), y = span_sv(b->inner_span);
            if (!csview_contains(x, "\\") && !csview_contains(y, "\\")) return csview_equals_sv(x, y);
            // (by the names' values: `<\u0061></a>` is fine)
            cstr cx = identifier_name_value(x), cy = identifier_name_value(y);
            bool ret = cstr_eq(&cx, &cy);
            cstr_drop(&cx);
            cstr_drop(&cy);
            return ret;
        }
        case EXPR_PROPERTY:
            return csview_equals_sv(span_sv(property_name(a->property)), span_sv(property_name(b->property))) &&
                jsx_tag_names_equal(&a->property->expr, &b->property->expr);
        default:
            return false;
    }
}

// `{`, the expression (or nothing) and `}` of an attribute value or a child. False if there was no expression
static bool parse_jsx_braced_expr(Parser* p, bool allow_spread, bool* spread, Expr* ret) {
    p->pos++; // `{`
    hop_whitespace(p);
    bool has_expr = false;
    if (*p->pos != '}') {
        if (!allow_spread && !strncmp(p->pos, "...", 3)) {
            // `x={...a}`: no spread in an attribute value, the `...` is left for the attribute list to skip
            // (The JSX expression's is tsc's missing identifier)
            parse_error(p, span_make(p->shared->source, p->pos, p->pos + 3), 1109, "Expression expected.");
            parse_error(p, span_from_pos(p), 1005, "'}' expected.");
            *ret = expr_implicit_any(span_from_pos(p));
            ret->missing = true;
            return true;
        }
        if (allow_spread && hop_start(p, "...")) {
            *spread = true;
            hop_whitespace(p);
        }
        // Only an assignment expression is valid, but tsc parses (and later reports) a comma sequence
        has_expr = expect_expr(p, expr_ctx_null, ret);
        if (has_expr) parse_comma_ops_right_side(p, ret);
        // (A missing expression is tsc's missing identifier, still the JSX expression's)
        else has_expr = ret->tag == EXPR_IMPLICIT_ANY && ret->missing;
        hop_whitespace(p);
    }
    if (!hop_char(p, '}')) {
        parse_error(p, span_from_token(p), 1005, "'}' expected.");
    }
    return has_expr;
}

static bool parse_jsx_element(Parser* parent, bool in_expression_context, JsxElement const* enclosing,
    bool must_be_unary, const char* top_invalid_pos, Expr* ret);

// tsc's parseJsxAttributes: until `>` or `/`. Returns where the last of them ends (`end`, the tag's name's end, without)
static const char* parse_jsx_attributes(Parser* p, const char* end, VecJsxAttribute* ret) {
    bool first = true;
    while (true) {
        if (!first) end = p->pos;
        first = false;
        hop_whitespace(p);
        char c = *p->pos;
        if (!c || c == '>' || c == '/') break;
        Anchor a = anchor(p);
        JsxAttribute attr = {0};
        if (c == '{') {
            // tsc's parseJsxSpreadAttribute
            p->pos++;
            hop_whitespace(p);
            attr.tag = JSX_ATTRIBUTE_SPREAD;
            if (!hop_start(p, "...")) parse_error(p, span_from_token(p), 1005, "'...' expected.");
            hop_whitespace(p);
            if (expect_expr(p, expr_ctx_null, &attr.value)) parse_comma_ops_right_side(p, &attr.value);
            hop_whitespace(p);
            if (!hop_char(p, '}')) parse_error(p, span_from_token(p), 1005, "'}' expected.");
            attr.span = span_from(a);
            vec_jsx_attribute_push(ret, attr);
            continue;
        }
        Span name;
        if (!parse_jsx_identifier(p, &name)) {
            // tsc's parseList: skips what can't start an attribute, unless something around could use it
            // (isInSomeParsingContext: anything is a JSX child; elsewhere, roughly, whatever starts a statement ends
            // the tag: `<C<T> />` in a JS file)
            parse_error(p, jsx_span_from_token(p), 1003, "Identifier expected.");
            if (p->shared->jsx_children_depth || strchr("<([\"'`!-+~@#", c) || (c >= '0' && c <= '9')) break;
            if (!hop_start(p, "...")) hop_token(p);
            continue;
        }
        parse_jsx_namespace_rest(p);
        attr.name = span_from(a);
        attr.tag = JSX_ATTRIBUTE_NO_VALUE;
        Parser eq = parser(p);
        hop_whitespace(&eq);
        if (!hop_char(&eq, '=')) {
            parse_fail(&eq);
        } else {
            parse_success(&eq);
            // (tsgo's ScanJsxAttributeValue skips whitespace only: a quote after a comment starts an ordinary string
            // literal, escapes and all)
            char const* value_start = p->pos;
            while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r') value_start++;
            hop_whitespace(p);
            Anchor value_a = anchor(p);
            char quote = *p->pos;
            cstr js_string = cstr_null;
            if ((quote == '"' || quote == '\'') && p->pos != value_start) {
                if (!parse_string(p, &js_string)) js_string = cstr_init();
                ast_cstr_seal(&js_string);
                attr.tag = JSX_ATTRIBUTE_STRING;
                attr.value = expr_literal(span_from(value_a), value_string(js_string));
            } else if (quote == '"' || quote == '\'') {
                // A JSX string: no escapes, line breaks allowed
                p->pos++;
                const char* start = p->pos;
                while (*p->pos && *p->pos != quote) p->pos++;
                cstr value = cstr_from_sv(csview_from_range(start, p->pos));
                if (!hop_char(p, quote)) parse_error(p, span_from_pos(p), 1002, "Unterminated string literal.");
                attr.tag = JSX_ATTRIBUTE_STRING;
                attr.value = expr_literal(span_from(value_a), value_string(value));
            } else if (quote == '{') {
                bool spread = false;
                attr.tag = parse_jsx_braced_expr(p, false, &spread, &attr.value) ?
                    JSX_ATTRIBUTE_EXPR : JSX_ATTRIBUTE_EMPTY_EXPR;
                if (attr.tag == JSX_ATTRIBUTE_EMPTY_EXPR) attr.value = expr_implicit_any(span_from(value_a));
            } else if (quote == '<') {
                attr.tag = JSX_ATTRIBUTE_ELEMENT;
                // (Unary: no sibling-element recovery, tsgo 7.1's parseJsxAttributeValue)
                parse_jsx_element(p, true, nullptr, true, nullptr, &attr.value);
            } else {
                parse_error(p, span_from_token(p), 1145, "'{' or JSX element expected.");
            }
        }
        attr.span = span_from(a);
        vec_jsx_attribute_push(ret, attr);
    }
    return end;
}

// Where the trivia (whitespace and comments) before `pos` starts: the end of the token before it (tsc's full start)
static const char* trivia_start_before(const char* file_start, const char* pos) {
    while (true) {
        while (pos > file_start && strchr(" \t\r\n", pos[-1])) pos--;
        if (pos - file_start >= 2 && pos[-1] == '/' && pos[-2] == '*') {
            // A block comment ends here: back to its `/*`
            const char* c = pos - 2;
            while (c > file_start && !(c[-1] == '/' && c[0] == '*')) c--;
            if (c == file_start) return pos;
            pos = c - 1;
            continue;
        }
        // A line comment on this line: the first `//` outside a string or block comment
        const char* line = pos;
        while (line > file_start && line[-1] != '\n' && line[-1] != '\r') line--;
        char quote = 0;
        const char* comment = nullptr;
        for (const char* c = line; c < pos && !comment; c++) {
            if (quote) {
                if (*c == '\\') c++;
                else if (*c == quote) quote = 0;
            } else if (*c == '"' || *c == '\'' || *c == '`') {
                quote = *c;
            } else if (c[0] == '/' && c[1] == '*') {
                const char* end = strstr(c + 2, "*/");
                if (!end || end >= pos) break;
                c = end + 1;
            } else if (c[0] == '/' && c[1] == '/') {
                comment = c;
            }
        }
        if (!comment) return pos;
        pos = comment;
    }
}

static bool jsx_is_line_break(char c) {
    return c == '\n' || c == '\r';
}

// tsc's parseJsxChildren: until `</` (left for the caller) or the end of the file
// fragment_start: where a fragment's TS17014 starts (its full start in an expression, whitespace before it included)
static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start);

static void parse_jsx_children(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start) {
    p->shared->jsx_children_depth++;
    u32 saved = list_enter(p, PARSING_JSX_CHILDREN);
    parse_jsx_children_inner(p, el, is_fragment, fragment_start);
    list_leave(p, saved);
    p->shared->jsx_children_depth--;
}

static void parse_jsx_children_inner(Parser* p, JsxElement* el, bool is_fragment, const char* fragment_start) {
    while (true) {
        Anchor a = anchor(p);
        char c = *p->pos;
        if (!c) {
            // At the tag that lacks the closing element rather than at the end of the file
            if (is_fragment) {
                parse_error(p, span_make(p->shared->source, fragment_start, span_end_ptr(el->opening_span)), 17014,
                    "JSX fragment has no corresponding closing tag.");
            } else {
                parse_error(p, expr_outer_span(&el->tag_name), 17008, "JSX element '", expr_outer_span(&el->tag_name),
                    "' has no corresponding closing tag.");
            }
            return;
        }
        if (c == '<' && is_conflict_marker(p, p->pos)) {
            p->pos = hop_conflict_marker(p, p->pos);
            p->shared->jsx_conflict_start = a.start;
            return;
        }
        if (c == '<') {
            if (p->pos[1] == '/') return;
            JsxChild child = { .tag = JSX_CHILD_ELEMENT };
            parse_jsx_element(p, false, is_fragment ? nullptr : el, false, nullptr, &child.expr);
            child.span = span_from(a);
            vec_jsx_child_push(&el->children, child);
            // `<div>...(<span>...</div>)`: the `</div>` goes to the parent, stop here
            JsxElement const* inner = child.expr.tag == EXPR_JSX_ELEMENT ? child.expr.jsx : nullptr;
            if (!is_fragment && inner && !inner->self_closing && span_valid(inner->closing_span) &&
                !jsx_tag_names_equal(&inner->tag_name, &inner->closing_name) &&
                jsx_tag_names_equal(&el->tag_name, &inner->closing_name)
            ) {
                return;
            }
            continue;
        }
        if (c == '{') {
            JsxChild child = { .tag = JSX_CHILD_EXPR };
            if (!parse_jsx_braced_expr(p, true, &child.spread, &child.expr)) {
                child.tag = JSX_CHILD_EMPTY_EXPR;
                child.expr = expr_implicit_any(span_from(a));
            }
            child.span = span_from(a);
            vec_jsx_child_push(&el->children, child);
            continue;
        }
        // Text (tsc's scanJsxToken). Whitespace only with a line break before anything else is JsxTextAllWhiteSpaces
        int first_non_whitespace = 0;
        while (!at_eof(p, p->pos) && *p->pos != '{' && *p->pos != '<') {
            // (A `<` conflict marker ends the text, which is part of its token: tsc's scanJsxToken)
            const char* next_line = p->pos + 1 + (p->pos[0] == '\r' && p->pos[1] == '\n');
            if (jsx_is_line_break(*p->pos) && *next_line == '<' && is_conflict_marker(p, next_line)) {
                p->pos = hop_conflict_marker(p, next_line);
                p->shared->jsx_conflict_start = a.start;
                return;
            }
            char ch = *p->pos;
            if (ch == '>') {
                parse_error(p, span_make(p->shared->source, p->pos, p->pos + 1), 1382,
                    "Unexpected token. Did you mean `{'>'}` or `&gt;`?");
            } else if (ch == '}') {
                parse_error(p, span_make(p->shared->source, p->pos, p->pos + 1), 1381,
                    "Unexpected token. Did you mean `{'}'}` or `&rbrace;`?");
            }
            if (jsx_is_line_break(ch) && first_non_whitespace == 0) {
                first_non_whitespace = -1;
            } else if (!is_js_ascii_space(ch) && !unicode_whitespace_size(p->pos)) {
                first_non_whitespace = 1;
            }
            p->pos++;
        }
        vec_jsx_child_push(&el->children, (JsxChild){
            .tag = JSX_CHILD_TEXT,
            .span = span_from(a),
            .whitespace_only = first_non_whitespace == -1,
        });
    }
}

// tsc's parseJsxElementOrSelfClosingElementOrFragment. `enclosing` is the element whose children this is in, if any.
// Not inlined: parse_expr()'s frame is what deeply nested expressions pay for (binderBinaryExpressionStress)
__attribute__((noinline)) static bool parse_jsx_element(Parser* parent, bool in_expression_context, JsxElement const* enclosing,
    bool must_be_unary, const char* top_invalid_pos, Expr* ret
) {
    const char* conflict = nullptr; // Where a conflict marker's token that ended the children starts
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    // A child right after a child element that ended without its `>` at a `<<`, `<<=` or `<=` (no trivia between):
    // tsgo's parseJsxChildren rescans the scanner to a JSX `<` but leaves the parser's token the ordinary one, so the
    // element's parseExpected(`<`) fails (deduplicated with the previous element's error there) and doesn't advance, its
    // name is missing there (scanJsxIdentifier then takes the scanner's `<` as the token) and that same `<` opens its
    // type arguments: `(<a><c<<T>()=>void> x="1"/></a>)` is `<c` and `<` + `<<T>()=>void>` + ` x="1"/>` (bun 93-P04b/c/e)
    bool stale = !in_expression_context && p.shared->jsx_stale_end == p.pos && p.pos[0] == '<' &&
        (p.pos[1] == '<' || p.pos[1] == '=');
    if (!stale && !hop_char(&p, '<')) return parse_fail(&p);
    const char* name_full_start = p.pos;
    if (!stale) hop_whitespace(&p);

    JsxElement* el = ast_new(JsxElement, {
        .tag_name = expr_implicit_any_no_span,
        .closing_name = expr_implicit_any_no_span,
    });
    bool is_fragment = !stale && hop_char(&p, '>');
    Anchor attributes_a = anchor(&p);
    const char* attributes_last = nullptr; // The end of the last attribute
    VecJsxAttribute attributes = {0};
    bool unterminated = false; // The tag's final `>` is missing (jsx_stale_end)
    if (!is_fragment) {
        const char* name_end = p.pos;
        if (stale) {
            el->tag_name = expr_identifier(span_make(p.shared->source, p.pos, p.pos), (IdentifierExpr){0});
        } else {
            parse_jsx_tag_name(&p, &el->tag_name, &el->namespaced, &el->tag_name_broken);
            name_end = p.pos;
            if (el->tag_name.tag == EXPR_IDENTIFIER && !el->tag_name.inner_span.size) {
                // A missing name is where the trivia before it starts (tsc's missing node, its TS17008 is there)
                el->tag_name = expr_identifier(span_make(p.shared->source, name_full_start, name_full_start), (IdentifierExpr){0});
                name_end = name_full_start;
            }
            hop_whitespace(&p);
        }
        attributes_a = anchor(&p);
        // (A `<` token: tsc's scanner makes `<<` and `<=` tokens of their own, so they open no type arguments, but the
        // stale element's `<` does)
        if (!p.shared->source->is_js && *p.pos == '<' && (stale || (p.pos[1] != '/' && p.pos[1] != '<' && p.pos[1] != '='))) {
            // tsc's parseBracketedList(TypeArguments): types separated by commas, anything else ends the list
            Anchor type_args_a = anchor(&p);
            p.pos++;
            el->type_args = vec_spanned_type_init();
            hop_whitespace(&p);
            if (*p.pos == '>') {
                p.pos++;
                el->type_args_empty = span_from(type_args_a);
            } else while (true) {
                hop_whitespace(&p);
                // (tsc's parseDelimitedList(TypeArguments): what is neither a comma nor the start of a type ends it)
                Tok next = tok_peek(&p);
                if (!tok_is(next, ",") && !tok_is_start_of_type(&p, next)) {
                    if (!hop_char(&p, '>')) parse_error(&p, jsx_span_from_token(&p), 1005, "'>' expected.");
                    break;
                }
                Anchor type_a = anchor(&p);
                TypeNode t;
                if (tok_is(next, ",")) {
                    // (A comma is a list element too: a missing type, then the comma, `<c<,>/>`)
                    expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
                } else if (!parse_type(&p, TYPE_PRECEDENCE_NULL, &t)) {
                    parse_error(&p, jsx_span_from_token(&p), 1110, "Type expected.");
                    break;
                }
                list_push(vec_spanned_type, &el->type_args, spanned_type(span_from(type_a), t));
                hop_whitespace(&p);
                Anchor comma_a = anchor(&p);
                if (!hop_char(&p, ',')) {
                    if (!hop_char(&p, '>')) parse_error(&p, jsx_span_from_token(&p), 1005, "'>' expected.");
                    break;
                }
                el->type_args_trailing_comma = span_from(comma_a);
                hop_whitespace(&p);
                if (hop_char(&p, '>')) break;
                el->type_args_trailing_comma = span_null;
            }
            name_end = p.pos;
            hop_whitespace(&p);
            attributes_a = anchor(&p);
        }
        const char* attributes_end = parse_jsx_attributes(&p, name_end, &attributes);
        attributes_last = attributes_end;
        if (!hop_char(&p, '>')) {
            bool slash = hop_char(&p, '/');
            if (!slash) parse_error(&p, span_from_token(&p), 1005, "'/' expected.");
            const char* slash_end = p.pos;
            hop_whitespace(&p);
            if (!hop_char(&p, '>')) {
                parse_error(&p, span_from_token(&p), 1005, "'>' expected.");
                // Nothing of the tag's end: the element ends with its last token (`~< <`, `<a/`)
                p.pos = slash ? slash_end : attributes_end;
                unterminated = true;
            }
            el->self_closing = true;
        }
    }
    // (Up to its last attribute, like tsc's JsxAttributes node: not the tag's end. No attributes: empty, where they'd be)
    el->attributes = (Expr){
        .tag = EXPR_JSX_ATTRIBUTES,
        .jsx_attributes = attributes,
        .inner_span = span_make(p.shared->source, attributes_a.start,
            attributes_last && attributes_last > attributes_a.start ? attributes_last : attributes_a.start),
    };
    el->opening_span = span_from(a);

    if (!el->self_closing) {
        // tsc reports at the opening fragment node, whose range starts before the trivia in an expression (a child's
        // is part of the text before it)
        const char* fragment_start = a.start;
        if (in_expression_context) fragment_start = trivia_start_before(cstr_str(&p.shared->source->contents), a.start);
        parse_jsx_children(&p, el, is_fragment, fragment_start);
        JsxChild* last = vec_jsx_child_size(&el->children) ? vec_jsx_child_back_mut(&el->children) : nullptr;
        JsxElement* inner = last && last->expr.tag == EXPR_JSX_ELEMENT ? last->expr.jsx : nullptr;
        if (!is_fragment && inner && !inner->self_closing && span_valid(inner->closing_span) &&
            !jsx_tag_names_equal(&inner->tag_name, &inner->closing_name) &&
            jsx_tag_names_equal(&el->tag_name, &inner->closing_name)
        ) {
            // An unclosed child took our closing tag: `<div>(...<span>...</div>)` is `<div>(...<span>...</>)</div>`
            // (the child reported it)
            el->closing_name = inner->closing_name;
            el->closing_namespaced = inner->closing_namespaced;
            el->closing_span = inner->closing_span;
            inner->closing_name = expr_identifier(span_from_pos(&p), (IdentifierExpr){0});
            inner->closing_namespaced = false;
            inner->closing_span = span_from_pos(&p);
            // (and ends with its children)
            last->expr.inner_span = span_make(p.shared->source, span_start_ptr(last->expr.inner_span), span_start_ptr(el->closing_span));
        } else {
            Anchor closing_a = anchor(&p);
            if (!hop_start(&p, "</")) {
                conflict = p.shared->jsx_conflict_start;
                p.shared->jsx_conflict_start = nullptr;
                parse_error(&p, conflict ? span_make(p.shared->source, conflict, p.pos) : span_from_pos(&p), 1005, "'</' expected.");
                // tsc's parseJsxClosingElement goes on with a missing name
                if (!is_fragment) el->closing_name = expr_identifier(span_from_pos(&p), (IdentifierExpr){0});
            } else if (is_fragment) {
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) {
                    parse_error(&p, span_from_token(&p), 17015, "Expected corresponding closing tag for JSX fragment.");
                }
                el->closing_span = span_from(closing_a);
            } else {
                hop_whitespace(&p);
                bool broken = false;
                parse_jsx_tag_name(&p, &el->closing_name, &el->closing_namespaced, &broken);
                const char* name_end = p.pos;
                hop_whitespace(&p);
                if (!hop_char(&p, '>')) {
                    parse_error(&p, span_from_token(&p), 1005, "'>' expected.");
                    // (The closing element, and the element, end with its name: tsc's parseExpected doesn't advance)
                    p.pos = name_end;
                    unterminated = true;
                }
                el->closing_span = span_from(closing_a);
                if (!jsx_tag_names_equal(&el->tag_name, &el->closing_name)) {
                    if (enclosing && jsx_tag_names_equal(&el->closing_name, &enclosing->tag_name)) {
                        // Our parent's closing tag: the error is ours
                        parse_error(&p, expr_outer_span(&el->tag_name), 17008, "JSX element '", expr_outer_span(&el->tag_name),
                            "' has no corresponding closing tag.");
                    } else {
                        parse_error(&p, expr_outer_span(&el->closing_name), 17002,
                            "Expected corresponding JSX closing tag for '", expr_outer_span(&el->tag_name), "'.");
                    }
                }
            }
        }
    }

    p.shared->saw_jsx = true;
    // (A stale element in a JavaScript file opens no type arguments: its token is the JSX `<` then, which the next
    // child's rescan agrees with)
    if (!in_expression_context) p.shared->jsx_stale_end = unterminated && !(stale && p.shared->source->is_js) ? p.pos : nullptr;
    el->fragment = is_fragment;
    *ret = (Expr){
        .tag = is_fragment ? EXPR_JSX_FRAGMENT : EXPR_JSX_ELEMENT,
        .jsx = el,
        // (Without the conflict marker's token: the missing closing tag is before it)
        .inner_span = conflict ? span_make(p.shared->source, a.start, conflict) : span_from(a),
    };
    if (conflict) expr_set_outer_span(ret, ret->inner_span);

    // `<a></a><b></b>` where an expression is expected: the second element is parsed too, and the two are a comma
    // expression (tsc's recovery, so that the error is about both)
    if (!must_be_unary && in_expression_context) {
        Parser look = parser(&p);
        hop_whitespace(&look);
        // (A `<` token: not `<<` or `<=`, the scanner's tokens of their own)
        bool another = *look.pos == '<' && look.pos[1] != '<' && look.pos[1] != '=';
        parse_fail(&look);
        if (another) {
            const char* top = top_invalid_pos ? top_invalid_pos : a.start;
            hop_whitespace(&p);
            Expr invalid;
            parse_jsx_element(&p, true, nullptr, false, top, &invalid);
            parse_error(&p, span_make(p.shared->source, top, span_end_ptr(expr_outer_span(&invalid))), 2657,
                "JSX expressions must have one parent element.");
            *ret = expr_binary_op(span_from(a), ast_new(BinaryOpExpr, {
                .op = BINARY_OP_COMMA,
                .left = *ret,
                .right = invalid,
                .jsx_siblings = true,
            }));
        }
    }
    return parse_success(&p);
}

// tsc's parseUpdateExpression: in a JSX file, `<` followed by an identifier, keyword or `>` starts an element. As the
// operand of a unary operator any `<` does (tsc's parseSimpleUnaryExpression: `+ <1234> x`)
__attribute__((noinline)) static bool at_jsx_element_start(Parser* parent, bool unary_operand) {
    if (!parent->shared->source->is_jsx || *parent->pos != '<') return false;
    if (unary_operand) return true;
    Parser p = parser(parent);
    p.pos++;
    hop_whitespace(&p);
    bool ret = *p.pos == '>' || identifier_char_size(p.pos, true) || identifier_escape_size(p.pos) ||
        (*p.pos == '#' && identifier_char_size(p.pos + 1, true)); // (`<#a />`: parse_jsx_identifier())
    parse_fail(&p);
    return ret;
}

// tsc's isParenthesizedArrowFunctionExpressionWorker in a JSX file: `<T,>` and `<T extends X>` start a generic arrow
// function, anything else after `<` is an element
static bool jsx_generic_arrow_start(Parser* parent) {
    Parser p = parser(parent);
    p.pos++;
    hop_whitespace(&p);
    bool ret = false;
    Span name;
    if (parse_identifier_or_keyword(&p, &name)) {
        hop_whitespace(&p);
        if (span_equals(name, "const") && parse_identifier_or_keyword(&p, &name)) hop_whitespace(&p);
        if (hop_start_if_ends_on_word_break(&p, "extends")) {
            hop_whitespace(&p);
            ret = *p.pos != '=' && *p.pos != '>' && *p.pos != '/';
        } else {
            ret = *p.pos == ',' || *p.pos == '=';
        }
    }
    parse_fail(&p);
    return ret;
}

// `/** @type {T} */ (x)` is a type assertion, `/** @satisfies {T} */ (x)` a satisfies expression (tsc: a parenthesized
// expression with those tags). `span` is the parenthesized expression's
static void jsdoc_apply_parenthesized(Parser* p, const char* start, Span span, Expr* ret) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before(p, start, &tags)) return;
    JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
    bool is_satisfies = !tag;
    if (!tag) tag = jsdoc_find_typed(&tags, JSDOC_TAG_SATISFIES);
    if (tag && !is_satisfies && tag->type.tag == TYPE_UNIQUE_SYMBOL) {
        grammar_error(p, tag->type_span, 1335, "'unique symbol' types are not allowed here.");
    }
    if (tag) {
        // (The parentheses are the assertion's: tsc's ParenthesizedExpression has the asserted type)
        Expr inner = *ret;
        expr_clear_outer_span(&inner);
        *ret = expr_as_type(span, ast_new(AsTypeExpr, {
            .expr = inner,
            // (Where tsc reports what's wrong with a JSDoc assertion: its type; a `@satisfies` its tag)
            .keyword_span = is_satisfies ? tag->tag_name : tag->type_span,
            .type = jsdoc_take_type(tag, nullptr),
            .is_satisfies = is_satisfies,
            .is_jsdoc = true,
        }));
    }
    vec_jsdoc_tag_drop(&tags);
}

static bool parse_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    FAIL_CACHE_HOOK(&p, FAIL_CACHE_EXPR);

    union {
        FunctionImpl function;
        ClassDecl class_decl;
    } parsed;
    bool inner_span_set = false;
    Tristate arrow_tristate;

    VecExpr substitutions;
    AstCStrs texts;
    // (Some branches below fill in ret's fields one by one: ret isn't zeroed)
    ret->assignment_pattern = false;
    ret->missing = false;
    ret->spread_into_call = false;
    ret->undefined_assigned = false;
    ret->literal_ambient = false;
    ret->no_enclosing_type_params = false;
    ret->expando_container = false;
    // Most branches below need a particular first character: test it before trying the branch
    char c = *p.pos;
    bool word = identifier_char_size(p.pos, true);
    if (c == '`' && parse_template(&p, &substitutions, &texts)) {
        *ret = expr_template(span_from(a), ast_new(TemplateExpr, {
            .template_span = span_from(a),
            .substitutions = substitutions,
            .texts = texts,
        }));
    } else if ((c == 'u' || c == 'n' || c == 't' || c == 'f' || c == '"' || c == '\'' || c == '`' ||
            is_number_start_char(c)) && parse_literal(&p, &ret->literal)
    ) {
        ret->tag = EXPR_LITERAL;
    } else if (c == '/' && parse_regex(&p)) {
        *ret = expr_regex(span_from(a));
    } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
        p.pos++;
        Span ignored;
        parse_identifier_or_keyword(&p, &ignored);
        *ret = expr_private_name(span_from(a), ast_new(PrivateNameExpr, {0}));
    } else if (c == '{' && parse_object(&p, &ret->object)) {
        ret->tag = EXPR_OBJECT;
    } else if (c == '[' && parse_array(&p, &ret->array)) {
        ret->tag = EXPR_ARRAY;
    } else if ((c == '@' && parse_decorated_class_with_modifiers(&p, &parsed.class_decl)) ||
        ((c == '@' || c == 'a' || c == 'c') && parse_class_decl(&p, false, &parsed.class_decl))
    ) {
        parsed.class_decl.is_expression = true;
        *ret = expr_class_decl(span_from(a), ast_new(ClassDecl, parsed.class_decl));
    } else if (c == '@') {
        // Decorators on anything but a class expression (tsc's parseDecoratedExpression): a missing declaration after
        // them, "Expression expected" where it would be
        VecExpr decorators;
        parse_decorators(&p, &decorators);
        unhop_trivia(&p);
        parse_error(&p, span_from_pos(&p), 1109, "Expression expected.");
        *ret = expr_implicit_any(span_from_pos(&p));
        ret->missing_decorators = ast_new(VecExpr, decorators); // (Not `missing`: no expression to the baselines)
        // (What follows continues it, tsc's parsePrimaryExpression returns the MissingDeclaration to the member, call
        // and binary expression parsers: `@x as T`)
        // (Which then start at the decorators, where tsc's MissingDeclaration starts)
        Expr wide = *ret;
        wide.inner_span = span_from(a);
        expr_set_outer_span(&wide, span_from(a));
        if (parse_expr_right_side(&p, ctx, &wide)) {
            *ret = wide;
            while (parse_expr_right_side(&p, ctx, ret)) {}
        }
        return parse_success(&p); // (Its span is where it's missing)
    } else if (c == 'n' && parse_new_target(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "new")) {
        hop_whitespace(&p);
        ExprCtx sub_ctx = ctx;
        sub_ctx.min_precedence = new_keyword_precedence;
        CallExpr call = { .uses_new = true };
        Parser import_p = parser(&p);
        bool import_callee = *p.pos == 'i' && hop_start_if_ends_on_word_break(&import_p, "import");
        parse_fail(&import_p);
        if ((*p.pos == '<' && !p.shared->source->is_jsx) || import_callee) {
            // A type assertion is no member expression (tsc's parseMemberExpressionOrHigher): `new <T>F()` is `new`
            // of a missing expression, then `< T > F()`. Nor is `import` (tsc's parsePrimaryExpression: `new
            // import("m")`, `new import.meta` are `new` of a missing expression, then the import expression)
            expected_expression_error(&p);
            unhop_trivia(&p); // (tsc's missing node is at the `<`'s full start)
            call.callable = expr_implicit_any(span_from_pos(&p));
        } else {
            expect_expr(&p, sub_ctx, &call.callable);
        }
        Parser chain_p = parser(&p);
        hop_whitespace(&chain_p);
        if (chain_p.pos[0] == '?' && chain_p.pos[1] == '.' && !(chain_p.pos[2] >= '0' && chain_p.pos[2] <= '9')) {
            // (tsc's parseNewExpressionOrNewDotTarget: `new A?.b()` is `(new A)?.b()`)
            parse_error(&chain_p, span_make(p.shared->source, chain_p.pos, chain_p.pos + 2), 1209,
                "Invalid optional chain from new expression. Did you mean to call '", expr_outer_span(&call.callable), "()'?");
            parse_success(&chain_p);
        } else {
            parse_fail(&chain_p);
        }
        // (After whitespace too, line breaks included: tsc's parseNewExpressionOrNewDotTarget takes `(` as the
        // arguments wherever it is)
        Parser args_p = parser(&p);
        hop_whitespace(&args_p);
        if (*args_p.pos == '(' && parse_call_args(&args_p, &call.args)) {
            parse_success(&args_p);
        } else {
            parse_fail(&args_p);
            call.args = vec_expr_init();
        }
        *ret = expr_call(span_from(a), ast_new(CallExpr, call));
    } else if ((!word || c == 'd' || c == 'a' || c == 'y' || c == 't' || c == 'v' || c == '\\') && !ctx.left_hand_side &&
        parse_left_unary_op_expr(&p, ctx, ret)
    ) {
    } else if (c == 'f' && parse_function_keyword_function(&p, false, false, &parsed.function)) {
        *ret = expr_function(span_from(a), ast_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
    } else if (c == 'a' && parse_async_function_expr(&p, ctx, &parsed.function)) {
        *ret = expr_function(span_from(a), ast_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
    } else if (hop_start_if_ends_on_word_break(&p, "this")) {
        p.shared->this_count++;
        *ret = expr_identifier(span_from(a), (IdentifierExpr){ .keyword = KEYWORD_IDENTIFIER_THIS });
    } else if (hop_start_if_ends_on_word_break(&p, "super")) {
        *ret = expr_identifier(span_from(a), (IdentifierExpr){ .keyword = KEYWORD_IDENTIFIER_SUPER });
        // tsc's parseSuperExpression
        Parser args_p = parser(&p);
        hop_whitespace(&args_p);
        Anchor args_a = anchor(&args_p);
        VecSpannedType args;
        const char* super_end = span_end_ptr(ret->inner_span);
        if (*args_p.pos == '<' && parse_generic_params(&args_p, true, &args)) {
            vec_spanned_type_drop(&args);
            Span args_span = span_from(args_a);
            const char* args_end = args_p.pos;
            hop_whitespace(&args_p);
            bool is_call = *args_p.pos == '(';
            bool is_template = *args_p.pos == '`';
            parse_fail(&args_p);
            // (The call parses the type arguments again: the baselines walk them. Before a template tsc drops them)
            if (is_call || is_template) parse_error(&p, args_span, 2754, "'super' may not use type arguments.");
            if (is_template) p.pos = super_end = args_end;
        } else {
            parse_fail(&args_p);
        }
        Parser next_p = parser(&p);
        hop_whitespace(&next_p);
        char next = *next_p.pos;
        if (next != '(' && next != '.' && next != '[' && next != '<') {
            // A property access with a missing name (tsc's parseExpectedToken(DotToken) then a missing identifier)
            parse_error(&next_p, span_from_token(&next_p), 1034, "'super' must be followed by an argument list or member access.");
            parse_success(&next_p);
            p.pos = super_end;
            Span missing = span_make(p.shared->source, p.pos, p.pos);
            *ret = expr_property(span_from(a), property_dot_new(&(PropertyExpr){ .expr = *ret, .name_ = missing }));
        } else {
            parse_fail(&next_p);
        }
    } else if (c == 'i' && parse_import_keyword_expr(&p, ret)) {
    } else if (c == 'i' && import_keyword_not_expr(&p)) {
        // `import` starts no other expression (tsc's parseMemberExpressionOrHigher: the expression is missing)
        return parse_fail(&p);
    } else if (word && !yield_await_is_keyword_here(&p) && parse_identifier_expr(&p, ret)) {
        inner_span_set = true;
    } else if (((*p.pos == '(' || *p.pos == '<') && (arrow_tristate = arrow_function_tristate(&p)) != TRISTATE_FALSE &&
            parse_arrow_function(&p, ctx, function_flags_null, arrow_tristate == TRISTATE_TRUE, &parsed.function)) ||
        // A lone `=>` is an arrow function missing its parameter list (tsc's isParenthesizedArrowFunctionExpression,
        // "ERROR RECOVERY TWEAK")
        (c == '=' && p.pos[1] == '>' && parse_arrow_function(&p, ctx, function_flags_null, true, &parsed.function))
    ) {
        *ret = expr_function(span_from(a), ast_new(FunctionImpl, parsed.function));
        if (p.shared->source->is_js) jsdoc_apply_function_expression(&p, ret);
    } else if (hop_char(&p, '(')) {
        // Committed like tsc's parseParenthesizedExpression: `()` is missing its expression
        hop_whitespace(&p);
        bool saved_in = allow_in_enter(&p);
        expect_expr(&p, expr_ctx_null, ret);
        parse_comma_ops_right_side(&p, ret);
        allow_in_leave(&p, saved_in);
        hop_whitespace(&p);
        if (!expect_char(&p, ')')) {
            // Without its `)` it ends with its last token, not the whitespace after it (tsc's finishNode)
            // (never into the expression, which may end in whitespace itself: an unterminated `(function () {`)
            char const* floor = span_end_ptr(expr_outer_span(ret));
            while (p.pos > a.start + 1 && p.pos > floor && is_js_ascii_space(p.pos[-1])) p.pos--;
        }
        inner_span_set = true;
        if (p.shared->source->is_js) jsdoc_apply_parenthesized(&p, a.start, span_from(a), ret);
    } else if (at_jsx_element_start(&p, ctx.min_precedence == unary_op_get_precedence(UNARY_OP_LEFT_BANG))) {
        // The operand of a unary operator is a unary expression, which a comma expression isn't (tsc's mustBeUnary)
        bool must_be_unary = ctx.min_precedence >= unary_op_get_precedence(UNARY_OP_LEFT_BANG);
        parse_jsx_element(&p, true, nullptr, must_be_unary, nullptr, ret);
        inner_span_set = true;
    } else if (!p.shared->source->is_jsx && *p.pos == '<' && p.pos[1] != '<' && hop_char(&p, '<')) {
        // (`<<` is a shift operator token to tsc's scanner, never the start of a type assertion)
        hop_whitespace(&p);

        TypeNode t;
        expect_type(&p, TYPE_PRECEDENCE_NULL, &t);
        const char* after_type = p.pos;
        hop_whitespace(&p);

        // (Without its `>` the operand's trivia starts right after the type)
        bool has_gt = expect_char(&p, '>');
        const char* after_gt = p.pos;
        const char* operand_trivia = has_gt ? after_gt : after_type;
        hop_whitespace(&p);

        // The operand is a unary expression: `<T>a || b` is `(<T>a) || b`
        Expr e;
        ExprCtx operand_ctx = ctx;
        operand_ctx.min_precedence = unary_op_get_precedence(UNARY_OP_LEFT_BANG);
        if (!expect_expr(&p, operand_ctx, &e) && expr_outer_span(&e).size == 0 && p.pos == span_start_ptr(expr_outer_span(&e))) {
            p.pos = after_gt; // A missing operand ends the assertion at its `>` (tsc's missing node)
        }
        *ret = expr_as_type(span_from(a), ast_new(AsTypeExpr, {
            .expr = e,
            .type = t,
            .angle_bracket_syntax = true,
            .keyword_span = span_make(p.shared->source, a.start, operand_trivia),
        }));
        {
            // tsc's parseUnaryExpressionOrHigher: `<T>x ** 2` is ambiguous (still read as `(<T>x) ** 2`)
            Parser lookahead = parser(&p);
            hop_whitespace(&lookahead);
            bool exponent = lookahead.pos[0] == '*' && lookahead.pos[1] == '*' && lookahead.pos[2] != '=';
            parse_fail(&lookahead);
            if (exponent) {
                parse_error(&p, span_from(a), 17007, "A type assertion expression is not allowed in the left-hand side of "
                    "an exponentiation expression. Consider enclosing the expression in parentheses.");
            }
        }
        if (source_is_mts_or_cts(&p)) {
            // (tsc's checkAssertion)
            grammar_error(&p, span_from(a), 7059,
                "This syntax is reserved in files with the .mts or .cts extension. Use an `as` expression instead.");
        }
    } else {
        return parse_fail(&p);
    }

    if (!inner_span_set) {
        ret->inner_span = span_from(a);
    }
    expr_set_outer_span(ret, span_from(a));

    ret->lazy_type = (LazyType){0};

    // An arrow function is a whole assignment expression: nothing continues it (`() => {} || a` is missing a `;`),
    // nor what it ends as the right side of an assignment or the last branch of a conditional (`x = () => {}` and a
    // `(` on the next line)
    while (!expr_ends_with_arrow_or_yield(ret) && parse_expr_right_side(&p, ctx, ret)) {}

    return parse_success(&p);
}

static bool expr_ends_with_arrow_or_yield(Expr const* e) {
    while (!expr_is_perenthisized(e)) {
        if (e->tag == EXPR_FUNCTION) return function_impl_is_arrow(e->function);
        // (A yield expression is a whole assignment expression too, tsc's parseAssignmentExpressionOrHigher returns
        // parseYieldExpression's: `yield ? 1 : 2` and `yield\n[]` are a bare `yield` and what follows)
        if (e->tag == EXPR_UNARY_OP && (e->unary_op->op == UNARY_OP_YIELD || e->unary_op->op == UNARY_OP_YIELD_STAR)) {
            return true;
        }
        if (e->tag == EXPR_BINARY_OP && binary_op_is_assignment(e->binary_op->op)) {
            e = &e->binary_op->right;
        } else if (e->tag == EXPR_TERNARY) {
            e = &e->ternary->no;
        } else {
            return false;
        }
    }
    return false;
}

// The full start of the token at the parser's position (tsc's getTokenFullStart, where it puts missing nodes): the
// start of the trivia just hopped
static const char* token_full_start(Parser* me) {
    if (my shared->last_trivia_end == my pos && my shared->last_trivia_start) return my shared->last_trivia_start;
    return my pos;
}

// Moves the parser back to the full start of the token at its position, so what it parses ends at the previous token
static void unhop_trivia(Parser* me) {
    const char* start = token_full_start(me);
    if (start == my pos) return;
    my pos = start;
    my whitespace = WHITESPACE_NONE;
    my whitespace_end = nullptr;
}

static inline bool expect_expr(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (parse_expr(parent, ctx, ret)) {
        return true;
    }
    expected_expression_error(parent);
    *ret = expr_implicit_any(span_from_pos(parent));
    // What a missing expression ends ends at the next token's full start (tsc's missing node is there, and the
    // baselines show it at the token's start, where its span is)
    unhop_trivia(parent);
    // (At a decorator tsc has a MissingDeclaration, which is no expression: parseDecoratedExpression)
    ret->missing = *parent->pos != '@';
    // tsc's isStartOfExpression takes a binary operator for a missing left operand and goes on with the binary
    // expression (`{ ...*o }`, `x = * y`). Not `<` (a type assertion) or `/` (a regex). The missing name is a
    // left-hand side, so an assignment goes on too (`if (x ^= {})` missing its `)`: `<missing> ^= {}`)
    Tok t = tok_peek(parent);
    // (In a JSX file a `<` that starts no element is that binary operator too: `<:a` is `<missing> < <missing>`)
    if ((tok_is_binary_operator_here(parent, t) && !tok_is(t, "/") && (!tok_is(t, "<") || (parent->shared->source->is_jsx && t.start[1] != '/'))) ||
        tok_is_assignment_operator(t) || tok_is(t, ".")
    ) {
        while (parse_expr_right_side(parent, ctx, ret)) {}
    }
    // (An expression was made of the missing operand after all: callers treat false as nothing to drop)
    return ret->tag != EXPR_IMPLICIT_ANY;
}

// A `/// <reference .../>` pragma's argument (tsgo's PragmaArgument): its value without the quotes, null if absent
typedef struct PragmaArg {
    char const* value;
    char const* value_end;
} PragmaArg;

static char const* pragma_skip_blanks(char const* c, char const* end) {
    while (c < end && (*c == ' ' || *c == '\t')) c++;
    return c;
}

// tsgo's extractName: ASCII letters and dashes, compared lowercased
static char const* pragma_name_end(char const* c, char const* end) {
    while (c < end && ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '-')) c++;
    return c;
}

static bool pragma_name_is(char const* start, char const* end, char const* name) {
    size_t len = strlen(name);
    if ((size_t)(end - start) != len) return false;
    for (size_t i = 0; i < len; i++) {
        char x = start[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (x != name[i]) return false;
    }
    return true;
}

static bool pragma_value_is(PragmaArg arg, char const* text) {
    return arg.value && csview_equals(csview_from_range(arg.value, arg.value_end), text);
}

// The `/// <reference .../>` pragma of a single-line comment [start, end) and what tsgo's processPragmasIntoFields
// makes of it: false if the comment is no reference pragma; else *ret is set when it names a path, types or lib, and
// *invalid when it names none (TS1084, the whole comment). tsgo's extractPragmas: blanks are spaces and tabs, names are
// case-insensitive, every argument needs a quoted value (the first that doesn't ends the list, `/>` isn't required),
// a repeated argument's last value wins; `types` then `lib` then `path`, `no-default-lib="true"` is ignored
static bool parse_reference_pragma(
    Parser* parent, char const* start, char const* end, SourceDirectiveStatement* ret, bool* has_ret, bool* invalid
) {
    *has_ret = *invalid = false;
    if (end - start < 3 || memcmp(start, "///", 3) != 0) return false;
    char const* c = pragma_skip_blanks(start + 3, end);
    if (c >= end || *c != '<') return false;
    char const* name_end = pragma_name_end(c + 1, end);
    if (!pragma_name_is(c + 1, name_end, "reference")) return false;
    c = name_end;
    enum { ARG_TYPES, ARG_LIB, ARG_PATH, ARG_RESOLUTION_MODE, ARG_PRESERVE, ARG_NO_DEFAULT_LIB, ARG_COUNT };
    static char const* const arg_names[ARG_COUNT] = { "types", "lib", "path", "resolution-mode", "preserve", "no-default-lib" };
    PragmaArg args[ARG_COUNT] = {0};
    while (true) {
        c = pragma_skip_blanks(c, end);
        if (end - c >= 2 && c[0] == '/' && c[1] == '>') break;
        char const* arg = c;
        char const* arg_end = pragma_name_end(c, end);
        if (arg_end == arg) break;
        c = pragma_skip_blanks(arg_end, end);
        if (c >= end || *c != '=') break;
        c = pragma_skip_blanks(c + 1, end);
        if (c >= end || (*c != '"' && *c != '\'')) break;
        char const* value_end = memchr(c + 1, *c, (size_t)(end - c - 1));
        if (!value_end) break;
        for (u32 i = 0; i < ARG_COUNT; i++) {
            if (pragma_name_is(arg, arg_end, arg_names[i])) args[i] = (PragmaArg){ c + 1, value_end };
        }
        c = value_end + 1;
    }
    if (pragma_value_is(args[ARG_NO_DEFAULT_LIB], "true")) return true;
    u32 kind = args[ARG_TYPES].value ? ARG_TYPES : args[ARG_LIB].value ? ARG_LIB : args[ARG_PATH].value ? ARG_PATH : ARG_COUNT;
    if (kind == ARG_COUNT) {
        *invalid = true;
        return true;
    }
    SourceFile const* source = parent->shared->source;
    ImportResolutionOverride resolution = IMPORT_RESOLUTION_DEFAULT;
    PragmaArg mode = args[ARG_RESOLUTION_MODE];
    if (kind == ARG_TYPES && mode.value) {
        if (pragma_value_is(mode, "import")) resolution = IMPORT_RESOLUTION_IMPORT;
        else if (pragma_value_is(mode, "require")) resolution = IMPORT_RESOLUTION_REQUIRE;
        else parse_error(parent, span_make(source, mode.value, mode.value_end), 1453, "`resolution-mode` should be either `require` or `import`.");
    }
    PragmaArg value = args[kind];
    cstr path = cstr_from_sv(csview_from_range(value.value, value.value_end));
    // (tsc's resolveTripleslashReference: normalizeSlashes)
    if (kind == ARG_PATH) {
        for (isize k = 0; k < cstr_size(&path); k++) {
            if (cstr_data(&path)[k] == '\\') cstr_data(&path)[k] = '/';
        }
    }
    *ret = (SourceDirectiveStatement){
        .tag = kind == ARG_TYPES ? SOURCE_DIRECTIVE_REFERENCE_TYPES : kind == ARG_LIB ? SOURCE_DIRECTIVE_REFERENCE_LIB :
            SOURCE_DIRECTIVE_REFERENCE_PATH,
        .path = path,
        .path_span = span_make(source, value.value, value.value_end),
        .preserve = pragma_value_is(args[ARG_PRESERVE], "true"),
        .resolution_override = resolution,
    };
    *has_ret = true;
    return true;
}

static void parse_source_directives(Parser* parent, VecStatement* ret) {
    while (true) {
        hop_whitespace_full(parent, false, true);

        Parser p = parser(parent);
        if (!hop_comment(&p)) {
            parse_fail(&p);
            return;
        }
        const char* comment_end = p.pos;
        parse_fail(&p);

        SourceDirectiveStatement directive;
        bool is_directive, invalid;
        Anchor comment_a = anchor(parent);
        if (parse_reference_pragma(parent, comment_a.start, comment_end, &directive, &is_directive, &invalid) && invalid) {
            parent->pos = comment_end;
            parse_error(parent, span_from(comment_a), 1084, "Invalid 'reference' directive syntax.");
        }
        if (is_directive) {
            list_push(vec_statement, ret, (Statement){
                .tag = STATEMENT_SOURCE_DIRECTIVE,
                .source_directive = ast_new(SourceDirectiveStatement, directive),
            });
        }
        parent->pos = comment_end;
    }
}

// An import/export specifier's name (tsc's parseModuleExportName): any identifier name, or a string literal (kept with
// its quotes, see port_item_name())
static bool parse_module_export_name(Parser* parent, Span* ret) {
    if (parse_identifier_or_keyword(parent, ret)) return true;
    if (*parent->pos != '"' && *parent->pos != '\'') return false;
    Anchor a = anchor(parent);
    cstr ignored = cstr_null;
    bool parsed = parse_string(parent, &ignored);
    cstr_drop(&ignored);
    if (parsed) *ret = span_from(a);
    return parsed;
}

static bool parse_import_item_list(Parser* parent, bool is_import, PortStatement* ret) {
    Parser p = parser(parent);
    ret->items = vec_port_item_init();

    if (!hop_char(&p, '{')) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // tsc's parseDelimitedList(ImportOrExportSpecifiers)
    u32 saved_context = list_enter(&p, PARSING_IMPORT_OR_EXPORT_SPECIFIERS);
    while (true) {
        // (A word other than `from` is an element: most specifiers skip the tokenizer)
        Tok t = identifier_char_size(p.pos, true) && *p.pos != 'f' ? (Tok){0} : tok_peek(&p);
        if (t.start && !list_is_element(&p, PARSING_IMPORT_OR_EXPORT_SPECIFIERS, t)) {
            if (list_is_terminator(PARSING_IMPORT_OR_EXPORT_SPECIFIERS, t)) break;
            if (list_abort_or_skip(&p, PARSING_IMPORT_OR_EXPORT_SPECIFIERS)) break;
            continue;
        }
        PortItem item = {
            .type_only = ret->main.type_only,
        };
        Anchor a = anchor(&p);

        // `type` is a modifier unless it is the name: `{ type }`, `{ type, a }`, `{ type as x }` and `{ type as as }`
        // rename or take something called type, `{ type as }` and `{ type as as x }` are type-only (tsc's
        // parseImportOrExportSpecifier)
        // (Each of those is taken as an identifier name: escaped, it isn't TS1260)
        Parser type_p = parser(&p);
        if (hop_word_cooked(&type_p, "type")) {
            hop_whitespace(&type_p);
            Parser as_p = parser(&type_p);
            bool type_is_name = *type_p.pos == ',' || *type_p.pos == '}';
            if (hop_word_cooked(&as_p, "as")) {
                hop_whitespace(&as_p);
                bool ends = *as_p.pos == ',' || *as_p.pos == '}';
                if (hop_word_cooked(&as_p, "as")) {
                    hop_whitespace(&as_p);
                    type_is_name = *as_p.pos == ',' || *as_p.pos == '}';
                } else {
                    type_is_name = !ends;
                }
            }
            parse_fail(&as_p);
            if (type_is_name) {
                parse_fail(&type_p);
            } else {
                parse_success(&type_p);
                item.type_only = true;
                item.type_modifier = true;
            }
        } else {
            parse_fail(&type_p);
        }

        // Any identifier name will do: `default as a`, `a as default`
        if (parse_module_export_name(&p, &item.source_name) || expect_identifier(&p, &item.source_name)) {
            const char* end = p.pos;
            hop_whitespace(&p);

            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                if (!parse_module_export_name(&p, &item.decl_name)) {
                    expect_identifier(&p, &item.decl_name);
                }
                end = p.pos;
            } else {
                item.decl_name = item.source_name;
            }
            // tsc's parseImportOrExportSpecifier: an import binds an identifier, not a string or a reserved word
            if (is_import && span_valid(item.decl_name)) {
                if (*item.decl_name.buf == '"' || *item.decl_name.buf == '\'') {
                    parse_error(&p, item.decl_name, 1003, "Identifier expected.");
                    // (tsc keeps the string as the name node, which the binder declares as a missing name: shown as
                    // the string's text, `Duplicate identifier '"a b"'`)
                    item.decl_name_missing = true;
                    if (item.source_name.buf == item.decl_name.buf) {
                        // (a missing name imports nothing)
                        item.source_name = span_make(p.shared->source, item.decl_name.buf, item.decl_name.buf);
                    }
                } else if (is_reserved_word(span_sv(item.decl_name)) ||
                    (p.shared->in_async && span_equals(item.decl_name, "await")) ||
                    (p.shared->in_generator && span_equals(item.decl_name, "yield"))
                ) {
                    // (`await` in an async function and `yield` in a generator are no identifiers either: tsc's
                    // isIdentifier)
                    parse_error(&p, item.decl_name, 1003, "Identifier expected.");
                }
            }

            item.full_span = span_make(p.shared->source, a.start, end);
            list_push(vec_port_item, &ret->items, item);
        } else {
            hop_token(&p);
        }
        hop_whitespace(&p);

        if (hop_char(&p, ',')) {
            hop_whitespace(&p);
            continue;
        }
        if (*p.pos == '}' || list_is_terminator(PARSING_IMPORT_OR_EXPORT_SPECIFIERS, tok_peek(&p))) break;
        expect_char(&p, ',');
    }
    list_leave(&p, saved_context);
    char const* close = p.pos;
    expect_char(&p, '}');
    shrink_list(vec_port_item, &ret->items);
    // (`import type D, {} from "m"`: named bindings all the same, TS1363)
    if (vec_port_item_is_empty(&ret->items) && *close == '}') ret->empty_braces_end = close + 1;

    return parse_success(&p);
}

// Parses what comes after the import keyword in `import a = require("b")` and `import a = b.c`
// In a JSDoc `@import`, the name after the tag is the JSDoc scanner's token, which takes `-` too (tsc's
// scanJSDocToken): `@import a-b from "m"` is a default import named `a-b`. See jsdoc_import_statement()
static void jsdoc_extend_dashed_name(Parser* p, Span* name) {
    if (!p->shared->in_jsdoc || *p->pos != '-') return;
    while (p->pos < p->shared->jsdoc_end && (is_identifier_char(*p->pos) || *p->pos == '-')) p->pos++;
    *name = span_make_in(*name, span_start_ptr(*name), p->pos);
}

static bool parse_import_equals(Parser* parent, Anchor statement_start, PortStatement* statement) {
    Parser p = parser(parent);

    bool type_only = false;
    Span name;
    // (tsc's isIdentifier: a reserved word is no name, `import while = x` is an import of a missing specifier)
    if (!parse_identifier(&p, &name) || is_reserved_word(span_sv(name))) {
        return parse_fail(&p);
    }
    jsdoc_extend_dashed_name(&p, &name);
    hop_whitespace(&p);
    // (The name's text: an escaped `typ\u0065` is `type` too, tsc's identifier.Text())
    bool named_type = span_equals(name, "type");
    if (!named_type && memchr(name.buf, '\\', (size_t)name.size)) {
        cstr cooked = identifier_name_value(span_sv(name));
        named_type = strcmp(cstr_str(&cooked), "type") == 0;
        cstr_drop(&cooked);
    }
    if (named_type && *p.pos != '=') {
        // (`import type from "m"` imports a default named `type`: tsc's parseImportDeclarationOrImportEqualsDeclaration
        // takes `type` as the modifier before `from` only if `from` or `=` follows that)
        Tok t = tok_peek(&p);
        if (tok_is(t, "from")) {
            Tok next = tok_next(&p, t);
            if (!tok_is(next, "from") && !tok_is(next, "=")) return parse_fail(&p);
        }
        type_only = true;
        if (!parse_identifier(&p, &name)) {
            return parse_fail(&p);
        }
        hop_whitespace(&p);
    } else if ((span_equals(name, "defer") || span_equals(name, "source")) && *p.pos != '=' && *p.pos != ',' &&
        !tok_is(tok_peek(&p), "from")
    ) {
        // (`import defer * as ns from "m"`, `import source s from "m"`: the phase modifier, never an import-equals)
        return parse_fail(&p);
    }
    if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
        p.pos++;
    } else {
        // tsc's tokenAfterImportedIdentifierDefinitelyProducesImportDeclaration: only a `,` or `from` makes it an import
        // declaration, anything else is an import-equals missing its '=' (`import abstract class D {}`)
        Tok t = tok_peek(&p);
        if (tok_is(t, ",") || tok_is(t, "from") || tok_is(t, "=")) return parse_fail(&p);
        expected_token_error(&p, "=");
    }
    hop_whitespace(&p);

    statement->main.decl_name = name;
    statement->main.type_only = type_only;

    Parser require_p = parser(&p);
    bool is_require = false;
    if (hop_start_if_ends_on_word_break(&require_p, "require")) {
        hop_whitespace(&require_p);
        if (hop_char(&require_p, '(')) {
            is_require = true;
            hop_whitespace(&require_p);
            Anchor path_anchor = anchor(&require_p);
            Value path;
            if (parse_literal(&require_p, &path)) {
                if (path.tag == VALUE_STRING) {
                    port_statement_rare_mut(statement)->target_path = ast_cstr_take(&path.string);
                    statement->target_path_span = span_from(path_anchor);
                } else {
                    // (tsgo's checker says it, checkExternalImportOrExportDeclaration: no parse error of the file)
                    checker_error(&require_p, span_from(path_anchor), 1141, "String literal expected.");
                }
                value_drop(&path);
            } else {
                // (tsc's parseModuleSpecifier takes any expression, which isn't a string literal)
                Expr skipped;
                if (parse_expr(&require_p, expr_ctx_null, &skipped)) {
                    parse_comma_ops_right_side(&require_p, &skipped); // (An Expression: commas too)
                    checker_error(&require_p, expr_outer_span(&skipped), 1141, "String literal expected.");
                    if (skipped.tag == EXPR_IDENTIFIER) port_statement_rare_mut(statement)->missing_specifier = skipped.inner_span;
                    expr_drop(&skipped);
                } else {
                    parse_error(&require_p, span_from_token(&require_p), 1141, "String literal expected.");
                }
            }
            hop_whitespace(&require_p);
            expect_char(&require_p, ')');
        }
    }

    if (is_require) {
        parse_success(&require_p);
        statement->main.tag = PORT_IMPORT_GATHER;
        statement->requires_target = true;
        statement->is_import_require = true;
        statement->main.is_require = true;
    } else {
        parse_fail(&require_p);
        statement->main.tag = PORT_IMPORT_ALIAS;
        statement->main.alias_ = ast_new(PortItemAlias, {0});
        while (true) {
            Span part;
            // (tsc's parseModuleReference: parseEntityName without reserved words, after a dot too: `Y.if`, `this.A`)
            bool reserved = false;
            bool first = vec_span_is_empty(&statement->main.alias_->path);
            Anchor part_a = anchor(&p);
            if (p.pos[0] == '#' && p.pos[1] != '!' && identifier_char_size(p.pos + 1, true)) {
                // A private name: an identifier name to tsc's scanner, the first one's parseIdentifier says TS18016
                hop_private_name(&p);
                part = span_from(part_a);
                if (first) parse_error(&p, part, 18016, "Private identifiers are not allowed outside class bodies.");
            } else if (!parse_identifier_or_keyword(&p, &part) ||
                ((reserved = is_reserved_word(span_sv(part))) && (p.pos = span_start_ptr(part), true))
            ) {
                if (reserved) {
                    // A missing name, the word is left for what follows
                    parse_error(&p, part, 1359, "Identifier expected. '", part, "' is a reserved word that cannot be used here.");
                } else {
                    parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
                }
                vec_span_push(&statement->main.alias_->path, span_from_pos(&p)); // (a missing name)
                break;
            }
            vec_span_push(&statement->main.alias_->path, part);
            Parser dot = parser(&p);
            hop_whitespace(&dot);
            if (!hop_char(&dot, '.')) {
                parse_fail(&dot);
                break;
            }
            hop_whitespace(&dot);
            parse_success(&dot);
        }
    }

    // (Up to this parser's position: the anchor is the parent's, which hasn't moved yet)
    statement->main.full_span = span_make(p.shared->source, statement_start.start, p.pos);
    return parse_success(&p);
}

// The `{ type: "json" }` of import attributes, into ret->elements (tsc's parseImportAttributes)
static void parse_import_attributes_body(Parser* parent, ImportAttributes* ret) {
    Parser* p = parent;
    const char* open = p->pos;
    if (!expect_char(p, '{')) return;
    hop_whitespace(p);
    while (!at_eof(p, p->pos) && *p->pos != '}') {
        ImportAttribute attribute = {0};
        Anchor name_anchor = anchor(p);
        cstr ignored = cstr_null;
        if (*p->pos == '"' || *p->pos == '\'') {
            parse_string(p, &ignored);
            cstr_drop(&ignored);
        } else if (*p->pos == '#' && identifier_char_size(p->pos + 1, true)) {
            // (A private name is an identifier name to tsc's tokenIsIdentifierOrKeyword: `{ #a: "x" }` is a key, an
            // Identifier node, which the binder's TS18012 for `#constructor` doesn't look at)
            p->pos++;
            parse_identifier_or_keyword(p, nullptr);
        } else if (!parse_identifier_or_keyword(p, nullptr)) {
            // (tsc's parsingContextErrors for ImportAttributes)
            parse_error(p, span_from_token(p), 1478, "Identifier or string literal expected.");
            // A token no list being parsed wants is skipped (tsc's abortParsingListOrMoveToNextToken): `{ a: 1,, }`
            Tok t = tok_peek(p);
            if (at_eof(p, p->pos) || list_token_wanted(p, p->shared->parsing_context, t)) break;
            p->pos = t.end;
            hop_whitespace(p);
            continue;
        }
        attribute.name = span_from(name_anchor);
        hop_whitespace(p);
        expect_char(p, ':');
        hop_whitespace(p);
        expect_expr(p, expr_ctx_null, &attribute.value);
        vec_import_attribute_push(&ret->elements, attribute);
        hop_whitespace(p);
        if (*p->pos == '}' || !expect_char(p, ',')) break;
        hop_whitespace(p);
    }
    if (!hop_char(p, '}')) {
        expect_char(p, '}');
        relate_last_expected_to_open(p, open, '{', '}');
    }
}

// `with { type: "json" }` / `assert { ... }` after a module specifier, on the same line (an import's `with` may follow
// a line break: tsc's tryParseImportAttributes; an export's may not)
static ImportAttributes* parse_import_attributes(Parser* parent, bool is_import) {
    Parser p = parser(parent);
    hop_whitespace(&p);
    Anchor a = anchor(&p);
    bool is_assert = false;
    bool newline = p.whitespace == WHITESPACE_NEWLINE;
    if (newline && !(is_import && tok_is(tok_peek(&p), "with"))) {
        parse_fail(&p);
        return nullptr;
    } else if (hop_start_if_ends_on_word_break(&p, "assert")) {
        is_assert = true;
        parse_error(&p, span_from(a), 2880, "Import assertions have been replaced by import attributes. Use 'with' instead of 'assert'.");
    } else if (!hop_start_if_ends_on_word_break(&p, "with")) {
        parse_fail(&p);
        return nullptr;
    }
    ImportAttributes* ret = ast_new(ImportAttributes, { .keyword = span_from(a), .is_assert = is_assert });
    ret->elements = vec_import_attribute_init();
    char const* keyword_end = p.pos;
    hop_whitespace(&p);
    char const* body_start = p.pos;
    parse_import_attributes_body(&p, ret);
    // (Without its body the node ends at the keyword: tsc's finishNode)
    ret->span = span_make(p.shared->source, a.start, p.pos == body_start ? keyword_end : p.pos);
    parse_success(&p);
    return ret;
}

static bool parse_import_export_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    Anchor statement_start = a;
    PortStatement statement = {0};

    bool is_export = false, no_clause = false;
    bool committed = p.shared->import_export_committed;
    p.shared->import_export_committed = false;
    if (hop_start_if_ends_on_word_break(&p, "import")) {
    } else if (hop_start_if_ends_on_word_break(&p, "export")) {
        is_export = true;
    } else {
        return parse_fail(&p);
    }
    statement.keyword_span = span_from(a);
    hop_whitespace(&p);

    if (is_export && *p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
        // `export = x`
        p.pos++;
        hop_whitespace(&p);
        Expr expr;
        expect_expr(&p, expr_ctx_null, &expr);
        mark_named_evaluation(&expr, false);
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = expr,
            .export_equals = true,
        };
        js_syntax_error(&p, span_with_semicolon(span_from(statement_start)), 8003,
            "'export =' can only be used in TypeScript files.");
        return parse_success(&p);
    }

    if (is_export) {
        // `export import a = b.c`
        Parser import_p = parser(&p);
        if (hop_start_if_ends_on_word_break(&import_p, "import")) {
            hop_whitespace(&import_p);
            if (parse_import_equals(&import_p, a, &statement)) {
                statement.main.export_import = true;
                parse_success(&import_p);
                goto done;
            }
        }
        parse_fail(&import_p);
        if (starts_with_followed_by_word_break(&p, "import")) {
            // `export import x from "m"`: an import declaration with a modifier (tsc's checkGrammarModifiers)
            grammar_error(&p, statement.keyword_span, 1191, "An import declaration cannot have modifiers.");
            if (parse_import_export_statement(&p, ret)) return parse_success(&p);
            return parse_fail(&p);
        }
    } else if (parse_import_equals(&p, a, &statement)) {
        goto done;
    } else if (*p.pos == '"' || *p.pos == '\'') {
        // `import "./side-effects"`
        Anchor path_anchor = anchor(&p);
        Value path;
        if (parse_literal(&p, &path)) {
            if (path.tag == VALUE_STRING) {
                statement.main.tag = PORT_NOOP;
                statement.side_effect_only = true;
                statement.requires_target = true;
                port_statement_rare_mut(&statement)->target_path = ast_cstr_take(&path.string);
                statement.target_path_span = span_from(path_anchor);
                statement.attributes = parse_import_attributes(&p, true);
                statement.main.full_span = span_from(a);
                value_drop(&path);
                goto done;
            }
            value_drop(&path);
        }
        return parse_fail(&p);
    }

    a = anchor(&p);
    Parser type_p = parser(&p);
    if (!is_export && hop_word_cooked(&type_p, "type")) {
        // An import's `type` is an identifier first (an escaped one is no TS1260), the modifier where what follows
        // can't take it as the default import's name: `import type x from`, `import type {`, `import type *`, `import
        // type from from` (tsc's parseImportDeclarationOrImportEqualsDeclaration). `import type, { a } from` and
        // `import type from "m"` import a default named type
        hop_whitespace(&type_p);
        Tok t = tok_peek(&type_p);
        bool identifier = tok_is_identifier(t);
        bool modifier = false;
        if (!tok_is(t, "from")) {
            modifier = identifier || tok_is(t, "*") || tok_is(t, "{");
        } else {
            Tok next = tok_next(&type_p, t);
            modifier = tok_is(next, "from") || tok_is(next, "=");
        }
        if (modifier) {
            parse_success(&type_p);
            statement.main.type_only = true;
        } else {
            parse_fail(&type_p);
        }
    } else if (is_export && hop_start_if_ends_on_word_break(&type_p, "type")) {
        hop_whitespace(&type_p);
        // (Not `import type from "m"`, a default import named `type`, see parse_import_equals())
        Tok t = tok_peek(&type_p);
        bool default_named_type = false;
        if (tok_is(t, "from")) {
            Tok next = tok_next(&type_p, t);
            default_named_type = !tok_is(next, "from") && !tok_is(next, "=");
        }
        if (default_named_type) {
            parse_fail(&type_p);
        } else {
            parse_success(&type_p);
            statement.main.type_only = true;
        }
    } else {
        parse_fail(&type_p);
        // `import defer * as ns from "m"`, `import source s from "m"` (tsc's parseImportDeclarationOrImportEqualsDeclaration,
        // shouldParseImportPhaseModifier: not `import defer from "m"`, a default import named defer, nor `import defer,
        // ...` / `import defer = ...`)
        Parser defer_p = parser(&p);
        Anchor defer_start = anchor(&defer_p);
        bool is_source = false;
        // (Spelled out: an escaped `s\u006furce` is a name, tsc's currentImportPhaseModifier looks at the token's text)
        if (!is_export && *defer_p.pos != '\\' && (hop_start_if_ends_on_word_break(&defer_p, "defer") ||
            (is_source = hop_start_if_ends_on_word_break(&defer_p, "source"))) &&
            !memchr(defer_start.start, '\\', (size_t)(defer_p.pos - defer_start.start))
        ) {
            Tok t = tok_peek(&defer_p);
            bool is_defer = tok_is(t, "from") ? *tok_next(&defer_p, t).start != '"' && *tok_next(&defer_p, t).start != '\'' :
                !tok_is(t, ",") && !tok_is(t, "=");
            if (is_defer) {
                hop_whitespace(&defer_p);
                port_statement_rare_mut(&statement)->phase_clause = span_from(defer_start);
                statement.source_phase = is_source;
                parse_success(&defer_p);
            } else {
                parse_fail(&defer_p);
            }
        } else {
            parse_fail(&defer_p);
        }
    }

    Anchor star = anchor(&p);
    if (hop_char(&p, '*')) {
        a = star; // (tsc's NamespaceImport/NamespaceExport starts at the `*`, after `type`)
        hop_whitespace(&p);
        statement.main.tag = is_export ? PORT_REEXPORT_GATHER : PORT_IMPORT_GATHER;

        if (is_export) {
            if (hop_start_if_ends_on_word_break(&p, "as")) {
                hop_whitespace(&p);
                // `export * as default from`, `export * as "x" from`
                // (`export * as #a from`: a private name is an identifier name, tsc's parseModuleExportName takes it)
                Anchor name_a = anchor(&p);
                if (hop_private_name(&p)) {
                    statement.main.decl_name = span_from(name_a);
                } else if (!parse_module_export_name(&p, &statement.main.decl_name)) {
                    expect_identifier(&p, &statement.main.decl_name);
                }
            } else {
                statement.main.tag = PORT_REEXPORT_SPLAT;
                a = statement_start; // (What TS2308 underlines: tsc's ExportDeclaration)
            }
        } else {
            if (!hop_start_if_ends_on_word_break(&p, "as")) {
                expected_token_error(&p, "as");
            }
            hop_whitespace(&p);

            expect_non_reserved_identifier(&p, &statement.main.decl_name);
        }
    } else if (is_export && hop_start_if_ends_on_word_break(&p, "as")) {
        hop_whitespace(&p);
        if (!hop_start_if_ends_on_word_break(&p, "namespace")) {
            expected_token_error(&p, "namespace");
        }
        hop_whitespace(&p);
        expect_identifier(&p, &statement.main.decl_name);
        statement.main.tag = PORT_EXPORT_AS_NAMESPACE;
        statement.main.not_top_level = p.shared->statement_parent != MODIFIED_IN_SOURCE_FILE;
        a = statement_start; // (the statement declares the alias, `export` included)
    } else if (!is_export && tok_is_identifier(tok_peek(&p)) && parse_identifier(&p, &statement.main.decl_name)) {
        jsdoc_extend_dashed_name(&p, &statement.main.decl_name);
        hop_whitespace(&p);
        statement.main.tag = PORT_IMPORT_DEFAULT;

        if (hop_char(&p, ',')) {
            hop_whitespace(&p);

            Anchor gather = anchor(&p);
            if (hop_char(&p, '*')) {
                // `import d, * as ns from "m"`: the namespace import rides along with the (named) items
                hop_whitespace(&p);
                if (!hop_start_if_ends_on_word_break(&p, "as")) expected_token_error(&p, "as");
                hop_whitespace(&p);
                PortItem item = { .tag = PORT_IMPORT_GATHER, .type_only = statement.main.type_only };
                expect_identifier(&p, &item.decl_name);
                item.full_span = span_from(gather);
                statement.items = vec_port_item_init();
                list_push(vec_port_item, &statement.items, item);
            } else if (!parse_import_item_list(&p, true, &statement)) {
                statement.items = vec_port_item_init();
                expected_token_error(&p, "{");
            }
        }
    } else if (parse_import_item_list(&p, !is_export, &statement)) {
        statement.main.tag = PORT_NOOP; // Only the item list is imported, no main import
    } else if (span_valid(port_statement_rare(&statement)->phase_clause) && (*p.pos == '"' || *p.pos == '\'')) {
        // `import source "m"`: a phase modifier and no bindings (tsc's tryParseImportClause makes an empty clause)
        statement.main.tag = PORT_NOOP;
        no_clause = true;
    } else if (!is_export && !statement.main.type_only && tok_is_word(tok_peek(&p))) {
        // `import` and a reserved word (tsc's isStartOfDeclaration says it's an import declaration): no import
        // clause, the module specifier is an expression (tsc's parseModuleSpecifier)
        statement.main.tag = PORT_NOOP;
        no_clause = true;
    } else if (committed && !is_export) {
        // (After decorators, `import(` and `import.` too: see ParserShared.import_export_committed)
        statement.main.tag = PORT_NOOP;
        no_clause = true;
    } else if (committed) {
        // (`@dec export;`: tsc's parseNamedImportsOrExports expects the `{`)
        expected_token_error(&p, "{");
        statement.main.tag = PORT_NOOP;
        statement.items = vec_port_item_init();
    } else {
        return parse_fail(&p);
    }
    if (statement.main.tag == PORT_IMPORT_DEFAULT) {
        char const* end = p.pos;
        while (end > span_start_ptr(statement.main.decl_name) && isspace((unsigned char)end[-1])) end--;
        statement.main.clause = span_make_in(statement.main.decl_name, span_start_ptr(statement.main.decl_name), end);
    }
    if (span_valid(port_statement_rare(&statement)->phase_clause)) {
        char const* end = p.pos;
        while (end > span_start_ptr(port_statement_rare(&statement)->phase_clause) && isspace((unsigned char)end[-1])) end--;
        port_statement_rare_mut(&statement)->phase_clause = span_make_in(port_statement_rare(&statement)->phase_clause, span_start_ptr(port_statement_rare(&statement)->phase_clause), end);
    }
    hop_whitespace(&p);

    statement.requires_target = true;
    if (no_clause) {
    } else if (!hop_start_if_ends_on_word_break(&p, "from")) {
        if (is_export) {
            statement.requires_target = false;
        } else {
            expected_token_error(&p, "from");
        }
    }
    hop_whitespace(&p);

    if (statement.requires_target) {
        Expr path_expr;
        // The module specifier is a string literal token (`from "a"\n<div />` is two statements in a JSX file)
        Anchor path_a = anchor(&p);
        bool parsed = (*p.pos == '"' || *p.pos == '\'') && parse_literal(&p, &path_expr.literal);
        if (parsed) path_expr = expr_literal(span_from(path_a), path_expr.literal);
        bool complete = parsed || expect_expr(&p, expr_ctx_null, &path_expr);
        // (tsc's parseModuleSpecifier parses an expression: commas included, after a missing one too)
        if (!parsed) parse_comma_ops_right_side(&p, &path_expr);
        // (A parenthesized string is no string literal: `@dec import("./m")`, TS1141)
        if (path_expr.tag == EXPR_LITERAL && path_expr.literal.tag == VALUE_STRING && !expr_is_perenthisized(&path_expr)) {
            port_statement_rare_mut(&statement)->target_path = ast_cstr_take(&path_expr.literal.string);
            statement.target_path_span = path_expr.inner_span;
        } else if (path_expr.tag == EXPR_IMPLICIT_ANY) {
            port_statement_rare_mut(&statement)->missing_specifier = path_expr.inner_span;
        } else {
            if (path_expr.tag == EXPR_IDENTIFIER) port_statement_rare_mut(&statement)->missing_specifier = path_expr.inner_span;
            // The checker's (tsc's checkExternalImportOrExportDeclaration: any specifier that isn't missing, a
            // missing operand's binary expression `* }` included)
            port_statement_rare_mut(&statement)->non_string_specifier = expr_outer_span(&path_expr);
            if (path_expr.tag != EXPR_IDENTIFIER) {
                port_statement_rare_mut(&statement)->bad_specifier = ast_new(Expr, path_expr);
                path_expr = expr_implicit_any(span_null);
            }
        }
        expr_drop(&path_expr);
        if (complete) statement.attributes = parse_import_attributes(&p, !is_export);
    }

    auto item_tag = is_export ? (
        statement.requires_target ? PORT_REEXPORT_SPECIFIC : PORT_EXPORT_SPECIFIC
    ) : PORT_IMPORT_SPECIFIC;
    for_each (i, vec_port_item, &statement.items) {
        if (i->tag != PORT_IMPORT_GATHER) i->tag = item_tag;
        // tsc's checkModuleExportName: a local export names an identifier, not a string
        if (item_tag == PORT_EXPORT_SPECIFIC && span_valid(i->source_name) &&
            (*i->source_name.buf == '"' || *i->source_name.buf == '\'')
        ) {
            grammar_error(&p, i->source_name, 1003, "Identifier expected.");
        }
    }

    statement.main.full_span = span_from(a);

done:
    if (reports_js_syntax(&p)) {
        Span whole = span_with_semicolon(span_from(statement_start));
        if (statement.main.tag == PORT_IMPORT_ALIAS || statement.main.is_require) {
            js_syntax_error(&p, whole, 8002, "'import ... =' can only be used in TypeScript files.");
        } else if (statement.main.type_only) {
            js_syntax_error(&p, whole, 8006, "'", is_export ? "export type" : "import type",
                "' declarations can only be used in TypeScript files.");
        } else {
            for_each (i, vec_port_item, &statement.items) {
                if (!i->type_only) continue;
                js_syntax_error(&p, i->full_span, 8006, "'", is_export ? "export...type" : "import...type",
                    "' declarations can only be used in TypeScript files.");
            }
        }
    }
    if (statement.source_phase) {
        statement.main.source_phase = true;
        for_each (i, vec_port_item, &statement.items) i->source_phase = true;
    }
    // (A source phase import resolves nothing, no module to load: tsgo's fileloader skips IsSourcePhaseImport)
    if (span_valid(statement.target_path_span) && !statement.source_phase) {
        statement.module_ref = add_module_ref(&p, cstr_move(&port_statement_rare_mut(&statement)->target_path), statement.target_path_span);
        port_statement_rare_trim(&statement);
        if (statement.is_import_require) statement.module_ref->syntax = MODULE_REF_REQUIRE;
        // A relative name in an ambient module declaration is never resolved to a file (tsc's collectModuleReferences
        // skips it: only an ambient module can be it)
        if (p.shared->ambient_module_depth && is_relative_module_name(port_statement_target_path(&statement))) {
            statement.module_ref->ambient_only = true;
        }
        statement.module_ref->in_augmentation_body = p.shared->ambient_module_depth || p.shared->global_block_depth;
        statement.main.module_ref = statement.module_ref;
        module_ref_set_attributes(statement.module_ref, statement.attributes, nullptr);
        if (statement.main.type_only) {
            statement.module_ref->resolution_override = import_attributes_resolution_mode(statement.attributes, nullptr);
            statement.module_ref->type_only_import = span_equals(statement.keyword_span, "import");
        }
        for_each (i, vec_port_item, &statement.items) {
            i->module_ref = statement.module_ref;
        }
    }
    *ret = (Statement){
        .tag = STATEMENT_IMPORT_EXPORT,
        .import_export = ast_new(PortStatement, statement),
    };
    return parse_success(&p);
}

// Only parses content, sets type and value to defaults
// An element of an array or object pattern: `x`, `...x`, `x = 1`, `[a, b] = c`
// At `...` followed by `{` or `[` (an object pattern's rest element with a pattern)
static bool rest_then_pattern(Parser* parent) {
    Parser p = parser(parent);
    p.pos += 3;
    hop_whitespace(&p);
    bool ret = *p.pos == '{' || *p.pos == '[';
    parse_fail(&p);
    return ret;
}

static bool parse_binding_element(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    bool rest = hop_start(&p, "...");
    if (rest) {
        hop_whitespace(&p);
    }
    if (!parse_binding_content(&p, ret)) {
        return parse_fail(&p);
    }
    ret->rest = rest;

    Parser default_p = parser(&p);
    hop_whitespace(&default_p);
    if (*default_p.pos == '=' && default_p.pos[1] != '=' && default_p.pos[1] != '>') {
        default_p.pos++;
        hop_whitespace(&default_p);
        Expr default_value;
        expect_expr(&default_p, expr_ctx_null, &default_value);
        if (ret->tag == BINDING_IDENTIFIER && !rest) mark_named_evaluation(&default_value, false);
        if (ret->tag == BINDING_IDENTIFIER) mark_assigned_name(&default_value, ret->content_span);
        ret->default_value = ast_new(Expr, default_value);
        parse_success(&default_p);
    } else {
        parse_fail(&default_p);
    }

    binding_set_element_span(ret, span_from(a));
    return parse_success(&p);
}

// tsc's isBindingIdentifier: an identifier that isn't a reserved word (`yield`/`await` are the caller's business)
// An `await` identifier outside functions (tsc's ContainsPossibleTopLevelAwait), for the top-level await reparse
static void note_top_level_await_identifier(Parser* p, csview name) {
    if (name.size == 5 && !p->shared->in_property_name && !memcmp(name.buf, "await", 5)) {
        p->shared->saw_top_level_await = true;
    }
}

static bool parse_binding_identifier(Parser* parent) {
    const char* start = parent->pos;
    if (!parse_identifier(parent, nullptr)) return false;
    // (Spelled with escapes a reserved word is that word all the same: tsc's scanner, `var v\u0061r` is TS1389)
    bool reserved = is_reserved_word(csview_from_range(start, parent->pos));
    if (!reserved && __builtin_expect(parent->shared->any_escaped_word, 0) && memchr(start, '\\', (size_t)(parent->pos - start))) {
        cstr cooked = identifier_name_value(csview_from_range(start, parent->pos));
        reserved = is_reserved_word(cstr_sv(&cooked));
        cstr_drop(&cooked);
    }
    if (reserved) {
        parent->pos = start;
        return false;
    }
    note_top_level_await_identifier(parent, csview_from_range(start, parent->pos));
    return true;
}

static bool parse_binding_content_inner(Parser* parent, Binding* ret);

// (tsgo's array and object binding patterns allow `in` in their elements: an identifier has none)
static bool parse_binding_content(Parser* parent, Binding* ret) {
    bool saved_in = allow_in_enter(parent);
    bool parsed = parse_binding_content_inner(parent, ret);
    allow_in_leave(parent, saved_in);
    return parsed;
}

static bool parse_binding_content_inner(Parser* parent, Binding* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    Binding binding;

    if (parse_binding_identifier(&p)) {
        *ret = (Binding){ .tag = BINDING_IDENTIFIER };
        // Name is accessed via content_span, which is set at the end
    } else if (hop_char(&p, '[')) {
        hop_whitespace(&p);
        ret->tag = BINDING_ARRAY;
        ret->array = vec_binding_init();
        u32 saved_context = list_enter(&p, PARSING_ARRAY_BINDING_ELEMENTS);
        while (!at_eof(&p, p.pos) && *p.pos != ']') {
            binding = (Binding){0};
            if (parse_binding_element(&p, &binding)) {
                vec_binding_push(&ret->array, binding);
            } else if (*p.pos == ',') {
                vec_binding_push(&ret->array, (Binding){
                    .content_span = span_from_pos(&p),
                    .tag = BINDING_NOOP,
                });
            } else {
                if (list_abort_or_skip(&p, PARSING_ARRAY_BINDING_ELEMENTS)) break;
                continue;
            }
            hop_whitespace(&p);

            ignore( *p.pos == ']' || at_eof(&p, p.pos) || expect_char(&p, ',') ); // (tsc's isListTerminator: the end of file too)
            hop_whitespace(&p);
        }
        list_leave(&p, saved_context);
        expect_char(&p, ']');
    } else if (hop_char(&p, '{')) {
        hop_whitespace(&p);
        ret->tag = BINDING_OBJECT;
        ret->object = vec_object_binding_item_init();
        u32 saved_context = list_enter(&p, PARSING_OBJECT_BINDING_ELEMENTS);
        while (!at_eof(&p, p.pos) && *p.pos != '}') {
            Anchor key_anchor = anchor(&p);
            Span name;
            Value key;
            Expr* computed_key = nullptr;
            bool shorthand_allowed = false;
            bool private_name = false;
            binding = (Binding){0};
            if (p.pos[0] == '.' && p.pos[1] == '.' && p.pos[2] == '.' && rest_then_pattern(&p)) {
                // `...{ }` / `...[]`: tsgo's parseObjectBindingElement reads a property name there, missing (TS1003,
                // its ':' expected at the same place isn't reported), then the pattern as the name
                Parser q = parser(&p);
                q.pos += 3;
                hop_whitespace(&q);
                Span missing = span_make(q.shared->source, q.pos, q.pos);
                parse_error(&q, span_from_token_or_eof(&q), 1003, "Identifier expected.");
                Binding target = {0};
                if (parse_binding_element(&q, &target)) {
                    parse_success(&q);
                    target.rest = true;
                    target.rest_property_name = true;
                    binding_set_element_span(&target, span_from(key_anchor));
                    vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                        .source = value_string(cstr_init()),
                        .source_span = missing,
                        .target = target,
                    });
                    goto object_item_done;
                }
                parse_fail(&q);
            }
            if (*p.pos == '.' && parse_binding_element(&p, &binding)) {
                // `...rest`. `...rest: x` is a rest element with a property name to tsc's parseObjectBindingElement
                // (TS2566 from the checker)
                Parser colon = parser(&p);
                hop_whitespace(&colon);
                if (binding.tag == BINDING_IDENTIFIER && !binding.default_value && hop_char(&colon, ':')) {
                    hop_whitespace(&colon);
                    Binding target = {0};
                    if (parse_binding_element(&colon, &target)) {
                        parse_success(&colon);
                        target.rest = true;
                        target.rest_property_name = true;
                        binding_set_element_span(&target, span_from(key_anchor)); // (The element, from its `...`)
                        Span key_name = binding.content_span;
                        binding_drop(&binding);
                        vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                            .source = value_string(identifier_name_value(span_sv(key_name))),
                            .source_span = key_name,
                            .target = target,
                        });
                        goto object_item_done;
                    }
                }
                parse_fail(&colon);
                vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                    .source_span = binding.content_span,
                    .target = binding,
                });
                goto object_item_done;
            } else if (parse_identifier_or_keyword(&p, &name)) {
                key = value_string(identifier_name_value(span_sv(name)));
                shorthand_allowed = !is_reserved_word(span_sv(name));
            } else if (*p.pos == '[') {
                p.pos++;
                hop_whitespace(&p);
                Expr key_expr;
                p.shared->in_property_name++;
                expect_expr(&p, expr_ctx_null, &key_expr);
                p.shared->in_property_name--;
                hop_whitespace(&p);
                expect_char(&p, ']');
                computed_key = ast_new(Expr, key_expr);
                key = value_undefined;
                name = span_from(key_anchor);
            } else if (parse_type_compat_literal(&p, &key) && (key.tag == VALUE_STRING || key.tag == VALUE_NUMBER || key.tag == VALUE_BIGINT)) {
                // (A bigint is a property name to tsc's parser too, its token value: `{ 0x10n: a }` reads "0x10n",
                // tsgo's getLiteralTypeFromPropertyName)
                if (key.tag == VALUE_BIGINT) {
                    cstr text = cstr_init();
                    bigint_append_token_value(&text, key.bigint);
                    key = (Value){ .tag = VALUE_STRING, .string = text };
                }
                name = span_from(key_anchor);
            } else if (*p.pos == '#' && identifier_char_size(p.pos + 1, true)) {
                // `{ #a: x }`: TS18064 from the checker, the `:` is expected (not a binding identifier)
                p.pos++;
                parse_identifier_or_keyword(&p, nullptr);
                name = span_from(key_anchor);
                key = value_undefined;
                private_name = true;
            } else {
                if (list_abort_or_skip(&p, PARSING_OBJECT_BINDING_ELEMENTS)) break;
                continue;
            }
            hop_whitespace(&p);

            if (hop_char(&p, ':')) {
                const char* target_full_start = p.pos;
                hop_whitespace(&p);
                if (!parse_binding_element(&p, &binding)) {
                    Span word;
                    Parser probe = parser(&p);
                    bool reserved = parse_identifier_or_keyword(&probe, &word) && is_reserved_word(span_sv(word));
                    parse_fail(&probe);
                    if (reserved) {
                        // tsc's createIdentifier: a missing identifier, the word is left for the list to trip over
                        parse_error(&p, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
                        // (A missing name all the same, which tsc's binder declares: `(Missing)`)
                        const char* missing = token_full_start(&p);
                        binding = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_make(p.shared->source, missing, missing) };
                    } else {
                        // (tsc's parseIdentifierOrPattern: a missing identifier at the full start, `(Missing)` to the
                        // binder and the checker, TS2842 and TS7031)
                        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
                        binding = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_make(p.shared->source, target_full_start, target_full_start) };
                    }
                }
            } else if (!shorthand_allowed && object_binding_name_follows(&p)) {
                // `{ 102 A7 }`: tsc's parseObjectBindingElement expects the `:` and parses the name anyway
                expected_token_error(&p, ":");
                check(parse_binding_element(&p, &binding));
            } else {
                if (!shorthand_allowed) {
                    expected_token_error(&p, ":");
                }
                // (Not a shorthand: a missing name at the next token's full start, which tsc still declares)
                const char* missing = token_full_start(&p);
                binding = (Binding){
                    .tag = BINDING_IDENTIFIER,
                    .content_span = shorthand_allowed ? name : span_make(p.shared->source, missing, missing),
                };
                if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
                    p.pos++;
                    hop_whitespace(&p);
                    Expr default_value;
                    expect_expr(&p, expr_ctx_null, &default_value);
                    if (shorthand_allowed) {
                        mark_named_evaluation(&default_value, false);
                        mark_assigned_name(&default_value, name);
                    }
                    binding.default_value = ast_new(Expr, default_value);
                }
            }
            if (key.tag == VALUE_STRING) ast_cstr_seal(&key.string);
            vec_object_binding_item_push(&ret->object, (ObjectBindingItem){
                .source = key,
                .source_span = name,
                .computed_key = computed_key,
                .target = binding,
                .private_name = private_name,
            });
        object_item_done:
            hop_whitespace(&p);

            ignore( *p.pos == '}' || at_eof(&p, p.pos) || expect_char(&p, ',') );
            hop_whitespace(&p);
        }
        list_leave(&p, saved_context);
        expect_char(&p, '}');
    } else {
        return parse_fail(&p);
    }

    ret->content_span = span_from(a);
    return parse_success(&p);
}

// After an object binding element's key that isn't a shorthand, without its `:`: whether a name or pattern follows (tsc's
// parseIdentifierOrPattern takes it: a binding identifier, `{` or `[`)
static bool object_binding_name_follows(Parser* parent) {
    if (*parent->pos == '{' || *parent->pos == '[') return true;
    Parser probe = parser(parent);
    Span word;
    bool ret = parse_identifier_or_keyword(&probe, &word) && !is_reserved_word(span_sv(word));
    parse_fail(&probe);
    return ret;
}

// A variable's, parameter's or catch variable's binding: tsc's parseIdentifierOrPattern also takes a private name as an
// identifier, reporting code (TS18029 variables, TS18009 parameters)
static bool parse_binding_content_or_private_name(Parser* parent, Binding* ret, u32 code, char const* message) {
    if (*parent->pos != '#' || !identifier_char_size(parent->pos + 1, true)) return parse_binding_content(parent, ret);
    Anchor a = anchor(parent);
    parent->pos++;
    parse_identifier_or_keyword(parent, nullptr);
    *ret = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_from(a) };
    parse_error(parent, ret->content_span, code, message);
    return true;
}

// `report_js`: TS8010 in JavaScript (not for a property signature)
// TS1335 at the `unique symbol` at `found`, which was allowed where it is only as a whole annotation (`unique symbol[]`)
static void unique_symbol_misplaced(Parser* p, char const* found) {
    char const* end = found + strlen("unique");
    while (end < p->pos && !(end[0] == 's' && strncmp(end, "symbol", 6) == 0)) end++;
    grammar_error(p, span_make(p->shared->source, found, end + strlen("symbol")), 1335,
        "'unique symbol' types are not allowed here.");
}

// The type of an annotation that may be `unique symbol` (Parser.unique_annotation_next): TS1335 unless that is the
// whole type (`unique symbol[]` isn't)
__attribute__((noinline)) static void expect_annotation_type(Parser* parent, char const* start, TypeNode* type) {
    char const* outer_allowed_at = parent->shared->unique_allowed_at;
    parent->shared->unique_allowed_at = start;
    parent->shared->unique_allowed_found = nullptr;
    parent->shared->unique_annotation_next = false;
    expect_type(parent, TYPE_PRECEDENCE_NULL, type);
    char const* found = parent->shared->unique_allowed_found;
    parent->shared->unique_allowed_found = nullptr;
    if (found && type->tag != TYPE_UNIQUE_SYMBOL) unique_symbol_misplaced(parent, found);
    parent->shared->unique_allowed_at = outer_allowed_at;
}

// `into`: where the type goes instead of a box of ret's (a class field's or property signature's own node)
static bool parse_binding_type_ex(Parser* parent, Binding* ret, bool report_js, TypeNode* into) {
    if (!hop_char(parent, ':')) {
        return false;
    }
    hop_whitespace(parent);

    Anchor a = anchor(parent);

    intptr_t mark = error_mark(parent);
    TypeNode type;
    // (Only what starts with `unique` or a parenthesis can be a `unique symbol` there: the sites set the flag for
    // every annotation)
    if (parent->shared->unique_annotation_next && (*parent->pos == 'u' || *parent->pos == '(')) {
        expect_annotation_type(parent, a.start, &type);
    } else {
        parent->shared->unique_annotation_next = false; // (Not for the annotations nested in this one)
        expect_type(parent, TYPE_PRECEDENCE_NULL, &type);
    }
    ret->has_explicit_type = true;
    if (into) *into = type;
    else ret->explicit_type = ast_new(TypeNode, type);
    binding_set_explicit_type_span(ret, span_from(a));
    if (report_js) {
        js_syntax_skip_since(parent, mark);
        js_syntax_error(parent, binding_explicit_type_span(ret), 8010, "Type annotations can only be used in TypeScript files.");
    }

    return true;
}

static bool parse_binding_type(Parser* parent, Binding* ret) {
    return parse_binding_type_ex(parent, ret, true, nullptr);
}

static bool parse_binding_type_or_set_to_default(Parser* parent, Binding* ret) {
    if (parse_binding_type(parent, ret)) {
        return true;
    } else {
        ret->has_explicit_type_span = false;
        ret->has_explicit_type = false;
        ret->explicit_type = nullptr;
            return false;
    }
}

static bool parse_initialization(Parser* parent, ExprCtx ctx, Expr* ret) {
    // (the `=` token, not `==` or `=>`)
    if (*parent->pos != '=' || parent->pos[1] == '=' || parent->pos[1] == '>' || !hop_char(parent, '=')) {
        return false;
    }
    hop_whitespace(parent);

    expect_expr(parent, ctx, ret);

    return true;
}

static bool parse_initialization_or_set_to_default(Parser* parent, ExprCtx ctx, Expr* ret) {
    if (parse_initialization(parent, ctx, ret)) {
        return true;
    } else {
        *ret = expr_implicit_any(span_from_pos(parent));
        return false;
    }
}

// An initializer (owned), null if there's no `=`
static Expr* parse_initialization_boxed(Parser* parent, ExprCtx ctx) {
    Expr value;
    return parse_initialization(parent, ctx, &value) ? ast_new(Expr, value) : nullptr;
}

// ECMAScript's reserved words: never a binding identifier (strict mode ones are, with a grammar error)
static bool is_reserved_word(csview word) {
#define WORD_IS(lit) (word.size == sizeof(lit) - 1 && memcmp(word.buf, lit, sizeof(lit) - 1) == 0)
    if (word.size < 2 || word.size > 10) return false;
    switch (word.buf[0]) {
        case 'b': return WORD_IS("break");
        case 'c': return WORD_IS("case") || WORD_IS("catch") || WORD_IS("class") || WORD_IS("const") ||
            WORD_IS("continue");
        case 'd': return WORD_IS("debugger") || WORD_IS("default") || WORD_IS("delete") || WORD_IS("do");
        case 'e': return WORD_IS("else") || WORD_IS("enum") || WORD_IS("export") || WORD_IS("extends");
        case 'f': return WORD_IS("false") || WORD_IS("finally") || WORD_IS("for") || WORD_IS("function");
        case 'i': return WORD_IS("if") || WORD_IS("import") || WORD_IS("in") || WORD_IS("instanceof");
        case 'n': return WORD_IS("new") || WORD_IS("null");
        case 'r': return WORD_IS("return");
        case 's': return WORD_IS("super") || WORD_IS("switch");
        case 't': return WORD_IS("this") || WORD_IS("throw") || WORD_IS("true") || WORD_IS("try") || WORD_IS("typeof");
        case 'v': return WORD_IS("var") || WORD_IS("void");
        case 'w': return WORD_IS("while") || WORD_IS("with");
        default: return false;
    }
#undef WORD_IS
}

// After `using`: a declaration if a binding identifier or `{` follows on the same line (tsc's
// nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine; `[` would be an element access)
static bool using_declaration_follows(Parser const* parent, bool disallow_of) {
    Parser p = parser((Parser*)parent);
    hop_whitespace(&p);
    bool ret = false;
    Span word;
    if (p.whitespace == WHITESPACE_NEWLINE) {
        ret = false;
    } else if (*p.pos == '{') {
        ret = true;
    } else if (parse_identifier_or_keyword(&p, &word)) {
        if (disallow_of && span_equals(word, "of")) {
            // (`for (using of = x;;)`: a declaration of `of` when `=`, `;` or `:` follows, 6.0)
            hop_whitespace(&p);
            ret = *p.pos == ';' || *p.pos == ':' || (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>');
        } else {
            ret = !is_reserved_word(span_sv(word));
        }
    }
    parse_fail(&p);
    return ret;
}

// tsgo's isVariableDeclarationInitializedToRequire: in a JavaScript file `const x = require("m")` and
// `const { a, b: c } = require("m")` declare aliases, like imports (not with a type, a JSDoc tag's or a TypeScript
// annotation's, TS8010: `node.Type() == nil`). `const a = require("m").a` is a variable (TS 7 removed the alias)
static void note_require_declaration(InitializedBinding* initializer) {
    if (initializer->binding.has_explicit_type) return;
    Expr* value = initialized_binding_value(initializer);
    if (!expr_is_require_call(value) || expr_is_perenthisized(value)) return;
    // (One that declares no aliases still names its module in the symbols baseline)
    value->call->callable.identifier.require_call = 3;
    ModuleRef* ref = identifier_module_ref(&value->call->callable.identifier);
    if (!ref) return;
    Binding const* binding = &initializer->binding;
    for_each (i, vec_object_binding_item, binding->tag == BINDING_OBJECT ? &binding->object : &(VecObjectBindingItem){0}) {
        if (i->computed_key || i->target.tag != BINDING_IDENTIFIER || i->target.rest || i->target.default_value ||
            !span_valid(i->source_span)
        ) {
            return;
        }
    }
    if (binding->tag != BINDING_IDENTIFIER && (binding->tag != BINDING_OBJECT || !vec_object_binding_item_size(&binding->object))) return;
    ((Expr*)value)->call->callable.identifier.require_call = 2;
    if (binding->tag == BINDING_IDENTIFIER) {
        initialized_binding_rare_mut(initializer)->require_ports = ast_new(PortItem, {
            .tag = PORT_IMPORT_GATHER,
            .decl_name = binding->content_span,
            .full_span = binding->content_span,
            .is_require = true,
            .require_binding = true,
            .module_ref = ref,
        });
        initializer->rare->require_port_count = 1;
        return;
    }
    u32 count = (u32)vec_object_binding_item_size(&binding->object);
    initialized_binding_rare_mut(initializer)->require_ports = ast_calloc(count, c_sizeof(PortItem));
    initializer->rare->require_port_count = count;
    for_each (i, vec_object_binding_item, &binding->object) {
        Span name = i->target.content_span;
        initializer->rare->require_ports[i_index] = (PortItem){
            .tag = PORT_IMPORT_SPECIFIC,
            .decl_name = name,
            .full_span = span_start_ptr(i->source_span) < span_start_ptr(name) ? span_between(i->source_span, name) : name,
            .source_name = i->source_span,
            .require_binding = true,
            .module_ref = ref,
        };
    }
}

// in_for: the declaration of a `for` loop, where `using of` is `for (using of x)`
// tsc's checkGrammarTypeOperatorNode for a VariableDeclaration typed `unique symbol` (by its annotation or, in
// JavaScript, its JSDoc `@type`)
static void unique_symbol_variable_check(Parser* p, Binding const* binding, bool in_for, uint8_t keyword) {
    if (!binding->has_explicit_type || binding_type(binding)->tag != TYPE_UNIQUE_SYMBOL) return;
    Span node = binding_explicit_type_span(binding);
    if (binding->tag != BINDING_IDENTIFIER) {
        grammar_error(p, node, 1333, "'unique symbol' types may not be used on a variable declaration with a binding name.");
    } else if (in_for) {
        grammar_error(p, node, 1334, "'unique symbol' types are only allowed on variables in a variable statement.");
    } else if (keyword != VAR_INIT_CONST && keyword != VAR_INIT_AWAIT_USING) {
        grammar_error(p, binding->content_span, 1332, "A variable whose type is a 'unique symbol' type must be 'const'.");
    }
}

static bool parse_variable_statement(Parser* parent, ExprCtx ctx, bool in_for, VariableStatement* ret) {
    Parser p = parser(parent);
    // (tsc's checkVariableStatement checks the list only when the modifiers pass)
    bool list_unchecked = take_decl_modifiers_failed(&p);
    bool using_forced = p.shared->using_forced;
    p.shared->using_forced = false;
    bool let_forced = p.shared->let_forced;
    p.shared->let_forced = false;

    Anchor start = anchor(&p);
    ret->empty_in_for = ret->list_grammar_error = false;
    if (hop_start_if_ends_on_word_break(&p, "var")) {
        ret->keyword = VAR_INIT_VAR;
    } else if (hop_start_if_ends_on_word_break(&p, "let")) {
        ret->keyword = VAR_INIT_LET;
    } else if (hop_start_if_ends_on_word_break(&p, "const")) {
        ret->keyword = VAR_INIT_CONST;
    } else if (hop_start_if_ends_on_word_break(&p, "using") && (using_forced || using_declaration_follows(&p, in_for))) {
        ret->keyword = VAR_INIT_USING;
    } else if (hop_start_if_ends_on_word_break(&p, "await")) {
        hop_whitespace(&p);
        // Unlike `for (using of x)`, `for (await using of x)` declares nothing (tsc: TS1123)
        if (!hop_start_if_ends_on_word_break(&p, "using") || !using_declaration_follows(&p, false)) {
            return parse_fail(&p);
        }
        ret->keyword = VAR_INIT_AWAIT_USING;
        ret->at_top_level = p.shared->function_depth == 0;
        ret->in_static_block = p.shared->in_static_block;
        ret->in_sync_function = !p.shared->in_async && p.shared->function_depth > 0;
    } else {
        return parse_fail(&p);
    }
    Anchor end_of_keyword = anchor(&p);
    hop_whitespace(&p);

    if (ret->keyword == VAR_INIT_CONST && starts_with_followed_by_word_break(&p, "enum")) {
        // `const enum E {}`
        return parse_fail(&p);
    }

    Span comma_span = span_null;
    InitializedBinding initializer = {0};
    ret->initializers = vec_initialized_binding_init();

    // `for (var of x)`, `for (await using of x)`: `of` is the loop's (tsc's canFollowContextualOfKeyword)
    bool empty_for_of = false;
    if (in_for && starts_with_followed_by_word_break(&p, "of")) {
        Parser ahead = parser(&p);
        hop_start(&ahead, "of");
        hop_whitespace(&ahead);
        empty_for_of = parse_identifier(&ahead, nullptr);
        hop_whitespace(&ahead);
        empty_for_of = empty_for_of && *ahead.pos == ')';
        parse_fail(&ahead);
    }

    // tsc's parseDelimitedList for the declarators, but with the terminators checked after each (below), `in`
    // disallowed in a for statement's and allowed in any other (tsgo's parseVariableDeclarationList)
    u32 saved_context = list_enter(&p, PARSING_VARIABLE_DECLARATIONS);
    bool saved_in = p.shared->disallow_in;
    p.shared->disallow_in = in_for;
    while (true) {
        const char* declaration_start = p.pos;
        if (empty_for_of || !parse_binding_content_or_private_name(&p, &initializer.binding, 18029,
                "Private identifiers are not allowed in variable declarations.")) {
            if (span_valid(comma_span) && !list_is_terminator(PARSING_VARIABLE_DECLARATIONS, tok_peek(&p))) {
                // `var a, ""`: TS1134 (tsc's abortParsingListOrMoveToNextToken)
                if (!list_abort_or_skip(&p, PARSING_VARIABLE_DECLARATIONS)) {
                    comma_span = span_null;
                    continue;
                }
            } else if (span_valid(comma_span)) {
                if (!list_unchecked) grammar_error(&p, comma_span, 1009, "Trailing comma not allowed.");
                ret->list_grammar_error = !list_unchecked;
                p.pos = span_end_ptr(comma_span);
            } else if (vec_initialized_binding_is_empty(&ret->initializers)) {
                if (ret->keyword == VAR_INIT_LET && !empty_for_of && !in_for && !let_forced) {
                    // let can also be a variable name, fail so we then attempt to parse it as such (not in a `for`,
                    // where tsc's parseForOrForInOrForOfStatement always takes `let` for a declaration list)
                    allow_in_leave(&p, saved_in);
                    allow_in_leave(&p, saved_in);
    list_leave(&p, saved_context);
                    variable_statement_drop(ret);
                    return parse_fail(&p);
                } else if (!list_is_terminator(PARSING_VARIABLE_DECLARATIONS, tok_peek(&p))) {
                    // `var class`: TS1389 (the checker's TS1123 for the empty list is a grammar error, dropped)
                    if (!list_abort_or_skip(&p, PARSING_VARIABLE_DECLARATIONS)) continue;
                } else {
                    Span s = span_from(end_of_keyword);
                    s.size = 0;
                    if (in_for) ret->empty_in_for = !list_unchecked;
                    else if (!list_unchecked) grammar_error(&p, s, 1123, "Variable declaration list cannot be empty.");
                    ret->list_grammar_error = !in_for && !list_unchecked;
                }
            } else if (list_is_terminator(PARSING_VARIABLE_DECLARATIONS, tok_peek(&p))) {
                // After a missing `,` or a skipped token (`var v = (a): => {}` ends at the `=>`)
            } else if (!list_abort_or_skip(&p, PARSING_VARIABLE_DECLARATIONS)) {
                // (tsc's abortParsingListOrMoveToNextToken: `var x = {} : 1` skips the `:`)
                comma_span = span_null;
                continue;
            }
            break;
        }
        const char* end_of_initialization = p.pos;
        hop_whitespace(&p);

        // `let x!: T` (tsc's parseVariableDeclarationAllowExclamation: not in a `for`)
        Span definite = span_null;
        if (!in_for && initializer.binding.tag == BINDING_IDENTIFIER && *p.pos == '!' && p.pos[1] != '=' &&
            p.whitespace != WHITESPACE_NEWLINE
        ) {
            definite = span_make(p.shared->source, p.pos, p.pos + 1);
            p.pos++;
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        bool typed = false;
        p.shared->unique_annotation_next = true;
        bool has_type = parse_binding_type_or_set_to_default(&p, &initializer.binding);
        p.shared->unique_annotation_next = false;
        if (has_type) {
            typed = true;
            unique_symbol_variable_check(&p, &initializer.binding, in_for, ret->keyword);
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        initializer.value = parse_initialization_boxed(&p, ctx);
        bool initialized = initializer.value;
        // tsc's checkGrammarVariableDeclaration (after its TS1155 for a `const` without an initializer)
        if (span_valid(definite)) {
            bool const_like = ret->keyword != VAR_INIT_VAR && ret->keyword != VAR_INIT_LET;
            if (initialized) {
                grammar_error(&p, definite, 1263, "Declarations with initializers cannot also have definite assignment assertions.");
            } else if (const_like && !p.shared->in_ambient) {
            } else if (!typed) {
                grammar_error(&p, definite, 1264, "Declarations with definite assignment assertions must also have type annotations.");
            } else if (p.shared->in_ambient) {
                grammar_error(&p, definite, 1255, "A definite assignment assertion '!' is not permitted in this context.");
            } else {
                initializer.binding.definitely_assigned = true;
            }
        }
        if (initialized) {
            if (initializer.binding.tag == BINDING_IDENTIFIER) {
                mark_named_evaluation(initializer.value, false);
                mark_assigned_name(initializer.value, initializer.binding.content_span);
            }
            end_of_initialization = p.pos;
            hop_whitespace(&p);
        }

        if (p.shared->source->is_js) {
            // A declaration's own JSDoc (`var /** @type {T} */ x`: tsc's VariableDeclaration is a JSDoc host). A for...of
            // or for...in variable's is the TS2483/TS2404 grammar error a written annotation is (tsgo's reparser)
            if (!initializer.binding.has_explicit_type) {
                VecJsDocTag tags = {0};
                if (jsdoc_before(&p, declaration_start, &tags)) {
                    if (jsdoc_apply_type_to_binding(&tags, &initializer.binding)) {
                        unique_symbol_variable_check(&p, &initializer.binding, in_for, ret->keyword);
                    }
                    // Its own `@satisfies` casts its initializer (tsgo's reparser), inside the statement's
                    JsDocTag* satisfies = jsdoc_find_typed(&tags, JSDOC_TAG_SATISFIES);
                    if (satisfies && initializer.value && initializer.value->tag != EXPR_IMPLICIT_ANY) {
                        jsdoc_wrap_satisfies(initializer.value, satisfies->tag_name, jsdoc_take_type(satisfies, nullptr));
                    }
                }
                vec_jsdoc_tag_drop(&tags);
            }
            note_require_declaration(&initializer);
        }
        list_push(vec_initialized_binding, &ret->initializers, initializer);

        if (*p.pos == ';' ||
            *p.pos == '}' ||
            at_eof(&p, p.pos) ||
            (*p.pos == '=' && p.pos[1] == '>') || // (tsc's isVariableDeclaratorListTerminator)
            hop_start_if_ends_on_word_break(&p, "in") ||
            hop_start_if_ends_on_word_break(&p, "of")
        ) {
            p.pos = end_of_initialization;
            break;
        }

        if (p.whitespace == WHITESPACE_NEWLINE) {
            if (hop_char(&p, ',')) {
                comma_span = span_from_previous_char(&p);
            } else {
                comma_span = span_null;
                break;
            }
        } else if (expect_char(&p, ',')) {
            comma_span = span_from_previous_char(&p);
        } else {
            comma_span = span_null;
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    shrink_list(vec_initialized_binding, &ret->initializers);

    ret->span = span_from(start);
    while (ret->span.size && is_js_ascii_space(ret->span.buf[ret->span.size - 1])) ret->span.size--;
    return parse_success(&p);
}

static bool parse_if_arm(Parser* parent, Expr* condition, Statement* statement) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "if")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Span open_token;
    if (expect_char(&p, '(')) {
        open_token = span_from_previous_char(&p);
    } else {
        open_token = span_null;
    }
    hop_whitespace(&p);

    bool saved_in = allow_in_enter(&p);
    if (expect_expr(&p, expr_ctx_null, condition)) {
        parse_comma_ops_right_side(&p, condition);
    }
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);

    if (!hop_char(&p, ')')) {
        Error* error = expected_token_error(&p, ")");
        error_sub_error_d(error, open_token, 1007, "The parser expected to find a ')' to match the '(' token here.");
    }
    hop_whitespace(&p);

    expect_statement(&p, false, statement);
    return parse_success(&p);
}

static bool parse_if_else(Parser* parent, IfStatement* ret) {
    Parser p = parser(parent);

    VecExpr conditions = {0};
    VecStatement statements = {0};
    while (true) {
        Expr condition = {0};
        Statement statement = {0};
        if (!parse_if_arm(&p, &condition, &statement)) {
            if (vec_expr_is_empty(&conditions)) {
                return parse_fail(&p);
            }
            // (The else branch, a missing expression statement when there's none: `else }`)
            expect_statement(&p, false, &statement);
            list_push(vec_statement, &statements, statement);
            break;
        }

        list_push(vec_expr, &conditions, condition);
        list_push(vec_statement, &statements, statement);

        // (An arm's statement takes its own `;`, parse_statement_semicolon(): `if (a) b; else c`)
        Parser else_p = parser(&p);
        hop_whitespace(&else_p);
        if (!hop_start_if_ends_on_word_break(&else_p, "else")) {
            parse_fail(&else_p);
            break;
        }
        parse_success(&else_p);
        hop_whitespace(&p);
    }

    shrink_list(vec_expr, &conditions);
    shrink_list(vec_statement, &statements);
    *ret = (IfStatement){
        .conditions = conditions,
        .statements = statements,
    };
    return parse_success(&p);
}

static bool parse_for_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "for")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    // `for await (x of y)`
    Span await_span = span_from_sv(p.shared->source, csview_from_n(p.pos, 5));
    bool is_await = hop_start_if_ends_on_word_break(&p, "await");
    if (is_await) hop_whitespace(&p);

    expect_char(&p, '(');
    hop_whitespace(&p);

    bool is_for_each = false;
    Statement statement = {0};
    Expr left_expr;
    Expr right_expr;
    Statement body;
    bool keyword_is_in = false;

    // (The initializer in tsgo's DisallowIn context: ParserShared.disallow_in)
    bool saved_in = p.shared->disallow_in;
    p.shared->disallow_in = true;
    if (parse_variable_statement(&p, expr_ctx_null, true, &statement.decl.variable)) {
        statement.decl.props.exported = DECL_EXPORTED_NO;
        statement.tag = STATEMENT_DECL_VARIABLE;
    } else if (parse_expr(&p, expr_ctx_null, &statement.expr)) {
        parse_comma_ops_right_side(&p, &statement.expr);
        statement.tag = STATEMENT_EXPR;
        statement.expr_jsdoc_type = nullptr;
    } else if (*p.pos == ';') {
        statement = statement_noop;
    } else {
        // (tsc's parseExpression: a missing identifier)
        expected_expression_error(&p);
        statement.tag = STATEMENT_EXPR;
        statement.expr = expr_implicit_any(span_from_pos(&p));
        statement.expr.missing = true;
        statement.expr_jsdoc_type = nullptr;
        parse_comma_ops_right_side(&p, &statement.expr); // (`for , a`: a comma expression of it)
    }
    p.shared->disallow_in = false; // (the condition, the incrementor and the iterated expression allow it)
    hop_whitespace(&p);

    if ((statement.tag == STATEMENT_EXPR || statement.tag == STATEMENT_DECL_VARIABLE) && (
        hop_start_if_ends_on_word_break(&p, "of") ||
        (hop_start_if_ends_on_word_break(&p, "in") && (keyword_is_in = true)) // control flow is my passion
    )) {
        is_for_each = true;
        // tsc's checkGrammarForInOrForOfStatement: `for (async of x)` reads like the start of an async arrow function
        // (not in an await context: an async function or a static block)
        if (!keyword_is_in && !is_await && !p.shared->in_async && !p.shared->in_static_block && statement.tag == STATEMENT_EXPR && statement.expr.tag == EXPR_IDENTIFIER &&
            !expr_is_perenthisized(&statement.expr) && span_equals(statement.expr.inner_span, "async")
        ) {
            grammar_error(&p, statement.expr.inner_span, 1106, "The left-hand side of a 'for...of' statement may not be 'async'.");
        }
        if (is_await && keyword_is_in) {
            // `for await (x in y)`: still a for-in loop
            Span in = span_from_sv(p.shared->source, csview_from_n(p.pos - 2, 2));
            parse_error(&p, in, 1005, "'of' expected.");
        }

        hop_whitespace(&p);

        if (expect_expr(&p, expr_ctx_null, &right_expr)) {
            parse_comma_ops_right_side(&p, &right_expr);
        }
        hop_whitespace(&p);
    } else {
        // tsc's parseForOrForInOrForOfStatement: `;`, an optional condition, `;`, an optional incrementor, whatever
        // is missing
        expect_char(&p, ';');
        hop_whitespace(&p);
        if (*p.pos != ';' && *p.pos != ')') {
            if (expect_expr(&p, expr_ctx_null, &left_expr)) parse_comma_ops_right_side(&p, &left_expr);
        } else {
            left_expr = expr_literal(span_from_pos(&p), value_boolean(true));
        }
        hop_whitespace(&p);
        expect_char(&p, ';');
        hop_whitespace(&p);
        if (*p.pos != ')') {
            if (expect_expr(&p, expr_ctx_null, &right_expr)) parse_comma_ops_right_side(&p, &right_expr);
        } else {
            right_expr = expr_undefined(span_from_pos(&p));
        }
        hop_whitespace(&p);
    }
    hop_whitespace(&p);

    expect_char(&p, ')');
    hop_whitespace(&p);
    allow_in_leave(&p, saved_in); // (the body is in the context around the statement)
    expect_statement(&p, false, &body);

    if (is_for_each) {
        *ret = (Statement){
            .tag = STATEMENT_FOR_EACH_LOOP,
            .for_each_loop = ast_new(ForEachLoopStatement, {
                .keyword = keyword_is_in ? FOR_EACH_LOOP_IN : FOR_EACH_LOOP_OF,
                .is_await = is_await && !keyword_is_in,
                .await_place = (uint8_t)(is_await && !keyword_is_in ? parser_await_place(&p) : AWAIT_ALLOWED),
                .await_span = await_span,
                .iterable = right_expr,
                .body = body,
            })
        };

        switch (statement.tag) {
            case STATEMENT_DECL_VARIABLE:
                ret->for_each_loop->variable.tag = FOR_EACH_LOOP_STATEMENT;
                ret->for_each_loop->variable.statement = statement.decl.variable;
                break;

            case STATEMENT_EXPR:
                ret->for_each_loop->variable.tag = FOR_EACH_LOOP_EXPR;
                ret->for_each_loop->variable.expr = statement.expr;
                break;

            default: panic("Invalid statement type for for-each loop");
        }
    } else {
        *ret = (Statement){
            .tag = STATEMENT_FOR_LOOP,
            .for_loop = ast_new(ForLoopStatement, {
                .initialization = statement,
                .condition = left_expr,
                .afterthought = right_expr,
                .body = body,
            }),
        };
    }

    return parse_success(&p);
}

static bool parse_while_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "while")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    const char* open = p.pos;
    bool opened = expect_char(&p, '(');
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    Statement body = statement_noop;
    // (A missing operand goes on into a comma expression too: tsc's parseExpression)
    bool saved_in = allow_in_enter(&p);
    if (expect_expr(&p, expr_ctx_null, &condition) || (condition.tag == EXPR_IMPLICIT_ANY && condition.missing)) {
        parse_comma_ops_right_side(&p, &condition);
    }
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);

    if (opened) expect_closing(&p, ')', open);
    else expect_char(&p, ')');
    hop_whitespace(&p);

    expect_statement(&p, false, &body);

    *ret = (Statement){
        .tag = STATEMENT_FOR_LOOP,
        .for_loop = ast_new(ForLoopStatement, {
            .initialization = statement_noop,
            .condition = condition,
            .afterthought = expr_undefined(span_from_pos(&p)),
            .body = body,
            .is_while = true,
        }),
    };
    return parse_success(&p);
}

// `with (x) statement` (tsc's parseWithStatement)
static bool parse_with_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "with")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    const char* open = p.pos;
    bool opened = expect_char(&p, '(');
    hop_whitespace(&p);

    Expr object = expr_implicit_any(span_from_pos(&p));
    bool saved_in = allow_in_enter(&p);
    if (expect_expr(&p, expr_ctx_null, &object)) {
        parse_comma_ops_right_side(&p, &object);
    }
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);

    if (opened) expect_closing(&p, ')', open);
    else expect_char(&p, ')');
    Span header = span_from(a);
    hop_whitespace(&p);

    Statement body = statement_noop;
    p.shared->with_body_depth++;
    expect_statement(&p, false, &body);
    p.shared->with_body_depth--;

    *ret = (Statement){
        .tag = STATEMENT_WITH,
        .with = ast_new(WithStatement, {
            .object = object,
            .body = body,
            .header = header,
        }),
    };
    return parse_success(&p);
}

static bool parse_do_while_loop_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "do")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    Statement body = statement_noop;
    expect_statement(&p, false, &body);
    hop_whitespace(&p);
    ignore(hop_char(&p, ';'));
    hop_whitespace(&p);

    Expr condition = expr_literal(span_from_pos(&p), value_boolean(false));
    // (Without its `while`, tsc's parseDoStatement goes on with the `(`, the condition and the `)` all the same)
    if (hop_start_if_ends_on_word_break(&p, "while")) {
        hop_whitespace(&p);
    } else {
        expected_token_error(&p, "while");
    }
    const char* open = p.pos;
    bool opened = expect_char(&p, '(');
    hop_whitespace(&p);
    Expr parsed;
    bool saved_in = allow_in_enter(&p);
    if (expect_expr(&p, expr_ctx_null, &parsed)) {
        expr_drop(&condition);
        condition = parsed;
        parse_comma_ops_right_side(&p, &condition);
    } else {
        // (A missing expression)
        expr_drop(&condition);
        condition = parsed;
    }
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);
    if (opened) expect_closing(&p, ')', open);
    else expect_char(&p, ')');

    *ret = (Statement){
        .tag = STATEMENT_FOR_LOOP,
        .for_loop = ast_new(ForLoopStatement, {
            .initialization = statement_noop,
            .condition = condition,
            .afterthought = expr_undefined(span_from_pos(&p)),
            .body = body,
            .is_do_while = true,
        }),
    };
    return parse_success(&p);
}

// `label:`, the statement that follows is parsed by the caller
static bool parse_label(Parser* parent, Span* ret) {
    Parser p = parser(parent);
    Span label;
    // (a reserved word is no label: `while : x` is a while statement missing its '(')
    if (yield_await_is_keyword_here(&p) || !parse_identifier(&p, &label) || is_reserved_word(span_sv(label))) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (!hop_char(&p, ':')) {
        return parse_fail(&p);
    }
    *ret = label;
    return parse_success(&p);
}

// The optional label after break and continue
static void parse_jump_target(Parser* parent, Span* ret) {
    Parser p = parser(parent);
    hop_whitespace_full(&p, true, false);
    Span label;
    if (yield_await_is_keyword_here(&p)) {
        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
        *ret = span_from_pos(&p); // (tsc's missing identifier)
        parse_success(&p);
    } else if (parse_identifier(&p, &label)) {
        *ret = label;
        parse_success(&p);
    } else {
        parse_fail(&p);
    }
}

// tsc's parseErrorForMissingSemicolonAfter for an expression statement that is an identifier followed by more on the
// same line: `module {`, `type = 1`, `asynd function f()`...
static void report_missing_semicolon_after_identifier(Parser* p, Span name) {
    Tok t = tok_peek(p);
    // (Unknown tokens have their scanner errors, tsc says nothing more)
    bool unknown = invalid_char_size(p, t.start) != 0;
    Span token = span_make(p->shared->source, t.start, t.end);
    // (tsc's parseErrorForInvalidName says the scanner's TokenValue(), which punctuation leaves alone: then it's the
    // name's, the last token with a value)
    bool valued = t.end > t.start && (identifier_char_size(t.start, true) || is_number_start_char(*t.start) ||
        *t.start == '"' || *t.start == '\'' || *t.start == '`' || *t.start == '#' || (unsigned char)*t.start >= 0x80);
    Span value = valued ? token : name;
    // (a string literal's without its quotes)
    if (value.size >= 2 && (value.buf[0] == '"' || value.buf[0] == '\'') && value.buf[value.size - 1] == value.buf[0]) {
        value = span_make_in(value, value.buf + 1, value.buf + value.size - 1);
    }
    if (span_equals(name, "const") || span_equals(name, "let") || span_equals(name, "var")) {
        parse_error(p, name, 1440, "Variable declaration not allowed at this location.");
    } else if (span_equals(name, "declare")) {
        // A declaration that failed to parse has reported already
    } else if (span_equals(name, "interface")) {
        if (tok_is(t, "{")) {
            parse_error(p, token, 1438, "Interface must be given a name.");
        } else {
            parse_error(p, token, 2427, "Interface name cannot be '", span_sv(value), "'.");
        }
    } else if (span_equals(name, "is")) {
        parse_error(p, span_make(p->shared->source, span_start_ptr(name), t.start), 1228,
            "A type predicate is only allowed in return type position for functions and methods.");
    } else if (span_equals(name, "module") || span_equals(name, "namespace")) {
        if (tok_is(t, "{")) {
            parse_error(p, token, 1437, "Namespace must be given a name.");
        } else {
            parse_error(p, token, 2819, "Namespace name cannot be '", span_sv(value), "'.");
        }
    } else if (span_equals(name, "type")) {
        if (tok_is(t, "=")) {
            parse_error(p, token, 1439, "Type alias must be given a name.");
        } else {
            parse_error(p, token, 2457, "Type alias name cannot be '", span_sv(value), "'.");
        }
    } else {
        // A misspelled keyword (tsc's viableKeywordSuggestions: the keywords longer than two characters), or one
        // missing its space (`declareconst`)
        csview keywords[c_arraylen(all_keywords)];
        isize count = 0;
        for (const char* const* k = all_keywords; *k; k++) {
            if (strlen(*k) > 2) keywords[count++] = c_sv(*k, strlen(*k));
        }
        isize best = spelling_suggestion_ex(span_sv(name), keywords, count, spelling_compare_strings, keywords);
        if (best >= 0) {
            parse_error(p, name, 1435, "Unknown keyword or identifier. Did you mean '", keywords[best], "'?");
            return;
        }
        for (isize i = 0; i < count; i++) {
            if (name.size > keywords[i].size + 2 && memcmp(name.buf, keywords[i].buf, keywords[i].size) == 0) {
                csview rest = { name.buf + keywords[i].size, name.size - keywords[i].size };
                parse_error(p, name, 1435, "Unknown keyword or identifier. Did you mean '", keywords[i], " ", rest, "'?");
                return;
            }
        }
        if (!unknown) parse_error(p, name, 1434, "Unexpected keyword or identifier.");
    }
}

// Whether tsc ends the statement with parseSemicolon(), so that a `;` after it is part of it rather than an empty
// statement (`interface I {};` is two statements)
static bool statement_takes_semicolon(Statement const* me) {
    switch (my tag) {
        case STATEMENT_EXPR:
            // (Not decorators on no declaration: tsc's MissingDeclaration)
            return !(my expr.tag == EXPR_IMPLICIT_ANY && my expr.missing_decorators);
        case STATEMENT_EXPORT_DEFAULT_EXPR: case STATEMENT_RETURN_EXPR: case STATEMENT_THROW_EXPR:
        case STATEMENT_BREAK: case STATEMENT_CONTINUE: case STATEMENT_IMPORT_EXPORT: case STATEMENT_DECL_VARIABLE:
        case STATEMENT_DECL_TYPE:
            return true;
        case STATEMENT_NOOP: return span_valid(my span) && my span.size && my span.buf[0] == 'd'; // `debugger`
        case STATEMENT_DECL_FUNCTION: return !my decl.function.impl->has_body;
        case STATEMENT_DECL_NAMESPACE: return my decl.namespace->is_shorthand;
        case STATEMENT_FOR_LOOP: return my for_loop->is_do_while;
        default: return false;
    }
}

// tsc's parseSemicolon() at the end of a statement that takes one: a `;` is part of it (also after a newline or a brace,
// `x = {}\n;`, which is no empty statement), else it has to end at a newline, `}` or the end of the file. Statements that
// don't take one (blocks, declarations, compound statements whose bodies took theirs) leave what follows to the next
static void parse_statement_semicolon(Parser* parent, Statement* statement) {
    if (!statement_takes_semicolon(statement)) return;
    Parser p = parser(parent);
    hop_whitespace(&p);
    if (*p.pos == ';') {
        p.pos++;
        statement->span = span_make(p.shared->source, span_start_ptr(statement->span), p.pos);
        parse_success(&p);
        return;
    }
    // (a do-while's `;` is optional: tsc's parseOptional. The binary marker the hop went over is a token of its own in
    // tsc, NonTextFileMarkerTrivia: not the end of the file)
    bool marker_next = p.shared->binary_marker && p.shared->binary_marker >= span_end_ptr(statement->span);
    if (p.whitespace == WHITESPACE_NEWLINE || (at_eof(&p, p.pos) && !marker_next) || *p.pos == '}' || statement->tag == STATEMENT_FOR_LOOP) {
        parse_fail(&p);
        return;
    }
    if (marker_next) p.pos = p.shared->binary_marker;
    // (tsc's parseErrorForMissingSemicolonAfter: an expression statement's or a throw statement's expression)
    bool expression = statement->tag == STATEMENT_EXPR || statement->tag == STATEMENT_THROW_EXPR;
    if (expression && statement->expr.tag == EXPR_IDENTIFIER && !expr_is_perenthisized(&statement->expr)) {
        report_missing_semicolon_after_identifier(&p, statement->expr.inner_span);
    } else if (expression && statement->expr.tag == EXPR_TEMPLATE && statement->expr.template_->tag &&
        !expr_is_perenthisized(&statement->expr)
    ) {
        // (tsc's parseErrorForMissingSemicolonAfter: `module \`M\` {` is a tagged template)
        parse_error(&p, statement->expr.template_->template_span, 1443, "Module declaration names may only use ' or \" quoted strings.");
    } else if (marker_next) {
        // (The marker is one token to the end of the file)
        parse_error(&p, span_make(p.shared->source, p.pos, p.pos + strlen(p.pos)), 1005, "';' expected.");
    } else {
        expected_token_error(&p, ";");
    }
    // (the error stays, the position doesn't move)
    p.pos = parent->pos;
    p.whitespace = parent->whitespace;
    p.whitespace_end = parent->whitespace_end;
    parse_success(&p);
}

// ctx is the kind of list: source elements, block statements or a switch clause's statements

// Whether the statement at p could be one tsc's isStartOfStatement rejects but parse_statement() accepts (as an
// expression statement with a missing semicolon: `private x = 1;`, `export extension class C`, `import x`); anything
// else that isn't a statement (`const` without a declaration) fails to parse. Keeps the full predicate off the path of
// ordinary statements
static bool statement_start_needs_check(Parser* p) {
    switch (*p->pos) {
        case 'a': return starts_with_followed_by_word_break(p, "accessor");
        case 'e': {
            if (!starts_with_followed_by_word_break(p, "export")) return false;
            // The usual `export <declaration>` needs no check (the predicate's lookahead is most of a .d.ts statement)
            Parser next = parser(p);
            next.pos += strlen("export");
            hop_whitespace(&next);
            static const char* const usual[] = {
                "declare", "class", "function", "interface", "type", "const", "let", "var", "enum", "namespace",
                "module", "default", "abstract", "async", "import", nullptr,
            };
            bool usual_start = strchr("{*=@", *next.pos) != nullptr;
            for (int i = 0; !usual_start && usual[i]; i++) {
                usual_start = starts_with_followed_by_word_break(&next, usual[i]);
            }
            parse_fail(&next);
            return !usual_start;
        }
        case 'i': {
            if (!starts_with_followed_by_word_break(p, "import")) return false;
            // (tsc's isStartOfDeclaration and import calls/`import.meta`: nearly always a declaration)
            Parser next = parser(p);
            next.pos += strlen("import");
            hop_whitespace(&next);
            char c = *next.pos;
            bool word = identifier_char_size(next.pos, true);
            parse_fail(&next);
            return !(word || (c && strchr("\"'*{(<.", c)));
        }
        case 'p':
            return starts_with_followed_by_word_break(p, "public") || starts_with_followed_by_word_break(p, "private") ||
                starts_with_followed_by_word_break(p, "protected");
        case 'r': return starts_with_followed_by_word_break(p, "readonly");
        case 's': return starts_with_followed_by_word_break(p, "static");
        case '=': return p->pos[1] == '>'; // (An expression statement can't start with a lone `=>`, see parse_arrow_function_inner())
        default: return false;
    }
}

// list_start: where the list's own text starts (past the `{`), for the JSDoc comments it binds
static bool parse_append_statement_sequence(Parser* parent, bool toplevel, ParsingContext ctx, const char* list_start, VecStatement* ret) {
    Parser p = parser(parent);
    const char* jsdoc_from = list_start;

    u32 saved_context = list_enter(&p, ctx);
    ModifiedParent outer_parent = p.shared->statement_parent;
    p.shared->statement_parent = !toplevel ? MODIFIED_IN_OTHER :
        ctx == PARSING_SOURCE_ELEMENTS ? MODIFIED_IN_SOURCE_FILE :
        MODIFIED_IN_MODULE_BLOCK;
    bool file_level = ctx == PARSING_SOURCE_ELEMENTS;
    while(
        !at_eof(&p, p.pos) &&
        (ctx == PARSING_SOURCE_ELEMENTS || *p.pos != '}') &&
        !(ctx == PARSING_SWITCH_CLAUSE_STATEMENTS && (
            starts_with_followed_by_word_break(&p, "default") ||
            starts_with_followed_by_word_break(&p, "case")
        ))
    ) {
        Statement statement = {0};
        const char* statement_start = p.pos;
        bool outer_saw_await = p.shared->saw_top_level_await;
        if (file_level) {
            if (p.shared->top_level_await) {
                // (A run of the first pass's statements with an `await` identifier starts a section)
                ParserShared* sh = p.shared;
                while (!sh->in_await_section && sh->await_statement_cursor < sh->first_pass_count &&
                    sh->await_statements[sh->await_statement_cursor].start < p.pos
                ) {
                    sh->await_statement_cursor++;
                }
                if (!sh->in_await_section && sh->await_statement_cursor < sh->first_pass_count &&
                    sh->await_statements[sh->await_statement_cursor].start == p.pos && sh->await_statements[sh->await_statement_cursor].await
                ) {
                    u32 next = sh->await_statement_cursor;
                    while (next < sh->first_pass_count && sh->await_statements[next].await) next++;
                    sh->in_await_section = true;
                    sh->await_section_next = next < sh->first_pass_count ? (i32)next : -1;
                }
                sh->in_async = sh->in_await_section;
                // (tsc's reparseTopLevelAwait parses those one by one, outside the statement list: nothing inside
                // aborts at a token that could start a statement)
                sh->parsing_context = sh->in_async ? saved_context : saved_context | (1u << ctx);
            } else {
                p.shared->saw_top_level_await = false;
            }
        }
        if (*p.pos == ';') {
            // tsc's EmptyStatement
            statement = statement_noop;
            statement.span = span_make(p.shared->source, p.pos, p.pos + 1);
            list_push(vec_statement, ret, statement);
            p.pos++;
        } else if (
            (!statement_start_needs_check(&p) || list_is_element(&p, ctx, tok_peek(&p)) || (file_level && p.shared->in_await_section)) &&
            parse_statement(&p, toplevel, &statement)
        ) {
            // (Its index: the JSDoc declarations that follow it are pushed after it)
            isize statement_index = vec_statement_size(ret);
            list_push(vec_statement, ret, statement);
            if (p.shared->source->is_js) {
                jsdoc_bind_comments(&p, jsdoc_from, p.pos, span_start_ptr(ret->data[statement_index].span), true, toplevel,
                    false, ret);
                jsdoc_from = p.pos;
                // (The typedefs of a class body's comments are hoisted out of the class: tsgo resolves their names
                // where the class is, without its `@template`s)
            }
            parse_statement_semicolon(&p, vec_statement_at_mut(ret, statement_index));
        } else if (file_level && p.shared->in_await_section && *p.pos != '\\') {
            // (tsc's reparseTopLevelAwait calls parseStatement itself: what starts no statement is an expression
            // statement of a missing identifier, `}` too, and a token nothing took is skipped)
            expect_statement(&p, toplevel, &statement);
            list_push(vec_statement, ret, statement);
            if (p.pos == statement_start) hop_token(&p);
        } else if (*p.pos == '\\' && invalid_char_size(&p, p.pos)) { // (Not a word with escapes: `\u0065lse` is TS1128)
            parse_error(&p, span_make(p.shared->source, p.pos, p.pos + 1), 1127, "Invalid character.");
            p.pos++;
        } else if (list_abort_or_skip(&p, ctx)) {
            break;
        }
        if (file_level && !p.shared->top_level_await) {
            ParserShared* sh = p.shared;
            bool await = sh->saw_top_level_await && p.pos > statement_start;
            if ((await || sh->await_statement_count) && p.pos > statement_start) {
                sh->await_statements = realloc(sh->await_statements, sizeof(FirstPassStatement) * (sh->first_pass_count + 1));
                panic_if(!sh->await_statements, "out of memory");
                sh->await_statements[sh->first_pass_count++] = (FirstPassStatement){ .start = statement_start, .await = await };
                sh->await_statement_count += await;
            }
            sh->saw_top_level_await |= outer_saw_await;
        } else if (file_level && p.shared->in_await_section && p.shared->await_section_next >= 0) {
            // (Done where a statement ends at the next statement without one, else on past it: tsc compares the
            // statement's end with that one's full start)
            ParserShared* sh = p.shared;
            char const* end = p.pos;
            hop_whitespace(&p);
            char const* next = sh->await_statements[sh->await_section_next].start;
            if (p.pos == next) {
                sh->in_await_section = false;
            } else if (end > next || p.pos > next) {
                i32 later = sh->await_section_next + 1;
                while (later < (i32)sh->first_pass_count && sh->await_statements[later].await) later++;
                sh->await_section_next = later < (i32)sh->first_pass_count ? later : -1;
            }
        }
        // (A chunk ends where its last statement does, before the trivia the next chunk starts with)
        if (file_level && p.shared->chunk_stop && p.pos >= p.shared->chunk_stop) {
            p.shared->chunk_reached = p.pos;
            break;
        }
        hop_whitespace(&p); // TODO: is this needed?
    }
    if (p.shared->binary_marker && at_eof(&p, p.pos)) {
        char const* marker = p.shared->binary_marker;
        p.shared->binary_marker = nullptr;
        // (Not TS1127 for its first character: the scanner made the whole marker one token)
        parse_error_in_literal(&p, span_make(p.shared->source, marker, p.pos), 1128, "Declaration or statement expected.");
    }
    list_leave(&p, saved_context);
    shrink_list(vec_statement, ret);
    p.shared->statement_parent = outer_parent;
    // (the end of the file has JSDoc in tsc, a block's `}` doesn't)
    if (p.shared->source->is_js) {
        bool eof = ctx == PARSING_SOURCE_ELEMENTS && toplevel && at_eof(&p, p.pos);
        jsdoc_bind_comments(&p, jsdoc_from, p.pos, nullptr, ctx == PARSING_SOURCE_ELEMENTS, toplevel, eof, ret);
    }

    return parse_success(&p);
}

static bool parse_switch_case(Parser* parent, SwitchCase* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (hop_start_if_ends_on_word_break(&p, "case")) {
        hop_whitespace(&p);

        *ret = (SwitchCase){
            .is_pattern = true,
            .pattern = {0},
            .statements = {0},
        };

        bool saved_in = allow_in_enter(&p);
        if (expect_expr(&p, expr_ctx_null, &ret->pattern)) {
            parse_comma_ops_right_side(&p, &ret->pattern);
        }
        allow_in_leave(&p, saved_in);
        hop_whitespace(&p);

        expect_char(&p, ':');
        ret->head = span_from(a);
    } else if (hop_start_if_ends_on_word_break(&p, "default")) {
        hop_whitespace(&p);

        expect_char(&p, ':');

        *ret = (SwitchCase){
            .is_pattern = false,
            .default_span = span_from(a),
            .statements = {0},
            .head = span_from(a),
        };
    } else {
        return parse_fail(&p);
    }
    const char* list_start = p.pos;
    hop_whitespace(&p);

    if (!parse_append_statement_sequence(&p, false, PARSING_SWITCH_CLAUSE_STATEMENTS, list_start, &ret->statements)) {
        switch_case_drop(ret);
        return parse_fail(&p);
    }

    return parse_success(&p);
}

static bool parse_switch_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);
    SwitchStatement switch_ = {0};

    if (!hop_start_if_ends_on_word_break(&p, "switch")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    expect_char(&p, '(');
    hop_whitespace(&p);

    bool saved_in = allow_in_enter(&p);
    if (expect_expr(&p, expr_ctx_null, &switch_.needle)) {
        parse_comma_ops_right_side(&p, &switch_.needle);
    }
    allow_in_leave(&p, saved_in);
    hop_whitespace(&p);

    expect_char(&p, ')');
    hop_whitespace(&p);

    expect_char(&p, '{');
    hop_whitespace(&p);

    u32 saved_context = list_enter(&p, PARSING_SWITCH_CLAUSES);
    while(*p.pos && *p.pos != '}') {
        SwitchCase case_;
        if (parse_switch_case(&p, &case_)) {
            vec_switch_case_push(&switch_.cases, case_);
        } else if (list_abort_or_skip(&p, PARSING_SWITCH_CLAUSES)) {
            break;
        }
        hop_whitespace(&p);
    }
    list_leave(&p, saved_context);
    expect_char(&p, '}');

    *ret = (Statement){
        .tag = STATEMENT_SWITCH,
        .switch_ = ast_new(SwitchStatement, switch_),
    };
    return parse_success(&p);
}

static bool parse_try_catch_statement(Parser* parent, Statement* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "try")) {
        // tsc's parseStatement parses a try statement at `catch` and `finally` too, missing its `try` and block
        if (!starts_with_followed_by_word_break(&p, "catch") && !starts_with_followed_by_word_break(&p, "finally")) {
            return parse_fail(&p);
        }
        expected_token_error(&p, "try");
    }
    hop_whitespace(&p);

    TryCatchStatement try_catch = {0};
    bool has_catch = false;
    bool has_finally = false;

    bool has_try_block = parse_block(&p, false, &try_catch.try_block);
    if (!has_try_block) {
        expected_token_error(&p, "{");
        try_catch.try_block = (Block){0};
    }
    {
        hop_whitespace(&p);

        if (hop_start_if_ends_on_word_break(&p, "catch")) {
            has_catch = true;
            hop_whitespace(&p);
            if (hop_char(&p, '(')) {
                hop_whitespace(&p);

                const char* binding_start = p.pos;
                if (!parse_binding_content_or_private_name(&p, &try_catch.catch_var, 18029,
                        "Private identifiers are not allowed in variable declarations.")) {
                    // (tsc's parseIdentifierOrPattern -> parseBindingIdentifier: a reserved word is TS1359)
                    Tok t = tok_peek(&p);
                    if (tok_is_word(t) && *t.start != '#' && is_reserved_word((csview){ t.start, t.end - t.start })) {
                        Span word = span_make(p.shared->source, t.start, t.end);
                        parse_error(&p, word, 1359, "Identifier expected. '", word, "' is a reserved word that cannot be used here.");
                    } else {
                        parse_error(&p, span_from_token_or_eof(&p), 1003, "Identifier expected.");
                    }
                    // (A missing name, which tsc still declares)
                    try_catch.catch_var = (Binding){ .tag = BINDING_IDENTIFIER, .content_span = span_from_pos(&p) };
                    binding_set_element_span(&try_catch.catch_var, try_catch.catch_var.content_span);
                }
                hop_whitespace(&p);
                if (parse_binding_type(&p, &try_catch.catch_var)) hop_whitespace(&p);
                if (p.shared->source->is_js && !try_catch.catch_var.has_explicit_type) {
                    // `catch (/** @type {unknown} */ e)`: the declaration's JSDoc (a VariableDeclaration to tsc)
                    VecJsDocTag tags = {0};
                    if (jsdoc_before(&p, binding_start, &tags)) jsdoc_apply_type_to_binding(&tags, &try_catch.catch_var);
                    vec_jsdoc_tag_drop(&tags);
                }
                if (*p.pos == '=' && p.pos[1] != '=' && p.pos[1] != '>') {
                    // tsc parses a variable declaration there, initializer included (checkGrammarCatchClause:
                    // TS1197 on it)
                    p.pos++;
                    hop_whitespace(&p);
                    Expr initializer;
                    if (expect_expr(&p, expr_ctx_null, &initializer)) {
                        grammar_error(&p, expr_outer_span(&initializer), 1197, "Catch clause variable cannot have an initializer.");
                    }
                    try_catch.catch_initializer = ast_new(Expr, initializer);
                    hop_whitespace(&p);
                }

                expect_char(&p, ')');
                hop_whitespace(&p);
            }
            if (!parse_block(&p, false, &try_catch.catch_block)) {
                expected_token_error(&p, "{");
                try_catch.catch_block = (Block){0};
            }
            hop_whitespace(&p);
        } else {
            try_catch.catch_block = (Block){0};
        }

        if (hop_start_if_ends_on_word_break(&p, "finally")) {
            has_finally = true;
            hop_whitespace(&p);
            if (!parse_block(&p, false, &try_catch.finally_block)) {
                expected_token_error(&p, "{");
                try_catch.finally_block = (Block){0};
            }
        } else {
            try_catch.finally_block = (Block){0};
        }

        if (!has_catch && !has_finally) {
            parse_error(&p, span_from_token(&p), 1472, "'catch' or 'finally' expected.");
        }
    }

    try_catch.has_catch = has_catch;
    try_catch.has_finally = has_finally;
    *ret = (Statement){
        .tag = STATEMENT_TRY_CATCH,
        .try_catch = ast_new(TryCatchStatement, try_catch),
    };
    return parse_success(&p);
}

static bool parse_return(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    if (!hop_start_if_ends_on_word_break(&p, "return")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    // tsc's parseReturnStatement: no expression where a semicolon could go (`return` then a line break ends it, even
    // before `let x`)
    bool saved_in = allow_in_enter(&p);
    if (!tok_can_parse_semicolon(tok_peek(&p)) && parse_expr(&p, expr_ctx_null, ret)) {
        parse_comma_ops_right_side(&p, ret);
    } else {
        *ret = expr_undefined(span_from(a));
    }
    allow_in_leave(&p, saved_in);

    return parse_success(&p);
}

static bool parse_throw(Parser* parent, Expr* ret) {
    Parser p = parser(parent);

    if (!hop_start_if_ends_on_word_break(&p, "throw")) {
        return parse_fail(&p);
    }
    const char* keyword_end = p.pos;
    hop_whitespace(&p);

    if (p.whitespace == WHITESPACE_NEWLINE) {
        // tsc's parseThrowStatement: no line break after `throw`, the expression is missing (zero-width, where the next
        // token starts: the checker's TS1142 goes after the keyword)
        *ret = expr_implicit_any(span_make(p.shared->source, p.pos, p.pos));
        p.pos = keyword_end;
    } else {
        bool saved_in = allow_in_enter(&p);
        if (expect_expr(&p, expr_ctx_null, ret)) parse_comma_ops_right_side(&p, ret);
        allow_in_leave(&p, saved_in);
    }

    return parse_success(&p);
}

static bool parse_block(Parser* parent, bool toplevel, Block* ret);

// Handles `namespace A {}`, the legacy spelling `module A {}`, dotted names (`namespace A.B {}`, which become nested
// exported namespaces) and ambient external modules (`declare module "a" {}` and the body-less `declare module "a";`)
static bool parse_namespace(Parser* parent, NamespaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // (After decorators tsc's parseModuleDeclaration takes whatever follows: a name on the next line, a missing one)
    bool decorated = take_decl_committed(&p);
    bool is_module_keyword = false;
    if (hop_start_if_ends_on_word_break(&p, "module")) {
        is_module_keyword = true;
    } else if (!hop_start_if_ends_on_word_break(&p, "namespace")) {
        return parse_fail(&p);
    }
    hop_whitespace(&p);

    if (p.whitespace == WHITESPACE_NEWLINE && !decorated) {
        return parse_fail(&p);
    }

    *ret = (NamespaceDecl){0};

    VecSpan names = vec_span_init();
    Span name;
    if (decorated && *p.pos != '"' && *p.pos != '\'' && !tok_is_identifier_here(&p, tok_peek(&p))) {
        // (tsc's parseIdentifier: TS1359 or TS1003, a missing name; the body's '{' expected is at the same token)
        expect_non_reserved_identifier(&p, &name);
        vec_span_push(&names, name);
    } else if (parse_identifier(&p, &name)) {
        // (`await` in an async function or static block, `yield` in a generator: no identifier either, tsc's isIdentifier)
        bool contextual = (span_equals(name, "await") && (p.shared->in_async || p.shared->in_static_block)) ||
            (span_equals(name, "yield") && p.shared->in_generator);
        if (is_reserved_word(span_sv(name)) || contextual) {
            // tsc's nextTokenIsIdentifierOrStringLiteralOnSameLine: `module debugger {}` is an expression statement
            // `module` (TS2819, report_missing_semicolon_after_identifier())
            vec_span_drop(&names);
            return parse_fail(&p);
        }
        vec_span_push(&names, name);
        while (true) {
            Parser dot = parser(&p);
            hop_whitespace(&dot);
            if (!hop_char(&dot, '.')) {
                parse_fail(&dot);
                break;
            }
            hop_whitespace(&dot);
            if (!parse_identifier(&dot, &name)) {
                parse_error(&dot, span_from_token_or_eof(&dot), 1003, "Identifier expected.");
                parse_success(&dot);
                break;
            }
            vec_span_push(&names, name);
            parse_success(&dot);
        }
    } else {
        vec_span_drop(&names);
        Value val;
        Anchor name_anchor = anchor(&p);
        if (*p.pos != '"' && *p.pos != '\'') {
            // tsc's isStartOfDeclaration wants a name or a string: `namespace {}`, `module 100 {}` are expressions
            // followed by a block (TS1437, TS2819: report_missing_semicolon_after_identifier())
            return parse_fail(&p);
        } else if (parse_literal(&p, &val)) {
            bool is_string = val.tag == VALUE_STRING;
            if (is_string && is_module_keyword) {
                ret->is_ambient_module = true;
                ret->name = span_from(name_anchor);
                ret->module_ref = add_module_ref(&p, ast_cstr_take(&val.string), ret->name);
                ret->module_ref->is_declaration = true;
                ret->module_ref->nested_in_ambient_module = p.shared->ambient_module_depth > 0;
                value_drop(&val);
                hop_whitespace(&p);
                // `declare module "*.css" with { type: "css" } {}` (tsgo's parseAmbientExternalModuleDeclaration)
                if (hop_start_if_ends_on_word_break(&p, "with")) {
                    hop_whitespace(&p);
                    TypeNode attributes = {0};
                    if (*p.pos == '{' && parse_interface(&p, false, &attributes.interface)) {
                        attributes.tag = TYPE_INTERFACE;
                        attributes.parsed = true;
                        ret->attributes = ast_new(TypeNode, attributes);
                    } else {
                        expected_token_error(&p, "{");
                    }
                    hop_whitespace(&p);
                }
                bool outer_in_namespace = p.shared->in_namespace_body;
                p.shared->in_namespace_body = false;
                p.shared->ambient_module_depth++;
                bool has_block = parse_block(&p, true, &ret->contents);
                p.shared->ambient_module_depth--;
                p.shared->in_namespace_body = outer_in_namespace;
                if (!has_block) {
                    // `declare module "a";` is a shorthand ambient module
                    ret->contents = (Block){0};
                    ret->is_shorthand = true;
                }
                ret->full_span = span_from(a);
                return parse_success(&p);
            } else if (is_string) {
                parse_error(&p, span_from(name_anchor), 1003, "Identifier expected.");
            } else {
                parse_error(&p, span_from(name_anchor), 2819, "Namespace name cannot be '", &val, "'.");
            }
            value_drop(&val);
            return parse_success(&p);
        } else {
            return parse_fail(&p);
        }
        names = vec_span_init();
    }
    hop_whitespace(&p);

    Block contents;
    bool outer_in_namespace = p.shared->in_namespace_body;
    p.shared->in_namespace_body = true;
    bool has_block = parse_block(&p, true, &contents);
    p.shared->in_namespace_body = outer_in_namespace;
    if (!has_block) {
        contents = (Block){0};
        expected_token_error(&p, "{");
    }

    // Build dotted names inside out
    for (isize i = vec_span_size(&names) - 1; i > 0; i--) {
        Span inner_name = *vec_span_at(&names, i);
        NamespaceDecl inner = {
            .name = inner_name,
            .full_span = span_make(p.shared->source, span_start_ptr(inner_name), p.pos),
            .contents = contents,
            .module_keyword = is_module_keyword,
        };
        contents = (Block){0};
        vec_statement_push(&contents.statements, (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = { .exported = DECL_EXPORTED_YES },
                .namespace = ast_new(NamespaceDecl, inner),
            },
        });
    }
    if (vec_span_size(&names)) {
        ret->name = *vec_span_at(&names, 0);
    }
    ret->module_keyword = is_module_keyword;
    ret->contents = contents;
    vec_span_drop(&names);

    ret->full_span = span_from(a);
    return parse_success(&p);
}

static bool parse_global_block(Parser* parent, NamespaceDecl* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    // (Consumed as the augmentation's name, an identifier: escaped it is no TS1260, tsc's parseIdentifier)
    if (!hop_word_cooked(&p, "global")) {
        return parse_fail(&p);
    }

    *ret = (NamespaceDecl) {
        .name = span_from(a),
        .is_global = true,
    };

    hop_whitespace(&p);
    bool outer_in_namespace = p.shared->in_namespace_body;
    p.shared->in_namespace_body = false;
    p.shared->global_block_depth++;
    bool has_block = parse_block(&p, true, &ret->contents);
    p.shared->global_block_depth--;
    p.shared->in_namespace_body = outer_in_namespace;
    if (!has_block) {
        // tsc's isStartOfDeclaration takes `global` and a name (or `export`) as a declaration too, which is then
        // missing its block: `global x` (the statement list says ';' expected)
        Tok t = tok_peek(&p);
        if (!tok_is_identifier(t) && !tok_is(t, "export")) return parse_fail(&p);
        ret->contents = (Block){0};
        ret->is_shorthand = true;
    }

    return parse_success(&p);
}

// Declarations that only exist in TypeScript, reported at their name (tsgo's checkJSSyntax, which every node the
// parser finishes goes through: what's inside is reported too, but for the members of an interface or a type alias,
// which are types). A function without a body is a signature declaration, the whole of it from `start` (its
// modifiers). `keyword`: where the declaration starts after its modifiers
static void js_syntax_declaration(Parser* p, intptr_t mark, const char* start, const char* keyword, Statement const* s) {
    if (!reports_js_syntax(p)) return;
    Span name;
    switch (s->tag) {
        case STATEMENT_DECL_INTERFACE:
            js_syntax_skip_since(p, mark);
            name = s->decl.interface->name;
            if (span_valid(name)) js_syntax_error(p, name, 8006, "'interface' declarations can only be used in TypeScript files.");
            return;
        case STATEMENT_DECL_TYPE:
            js_syntax_skip_since(p, mark);
            name = s->decl.type->name;
            if (span_valid(name)) js_syntax_error(p, name, 8008, "Type aliases can only be used in TypeScript files.");
            return;
        case STATEMENT_DECL_ENUM:
            name = s->decl.enum_->name;
            if (span_valid(name)) js_syntax_error(p, name, 8006, "'enum' declarations can only be used in TypeScript files.");
            return;
        case STATEMENT_DECL_NAMESPACE: {
            NamespaceDecl const* ns = s->decl.namespace;
            // (Not `declare module "m"` nor `declare global`: tsgo's parseAmbientExternalModuleDeclaration doesn't check)
            if (ns->is_ambient_module || ns->is_global || ns->is_invalid_global || !span_valid(ns->name)) return;
            name = ns->name;
            // (The first name of `namespace A.B`)
            const char* dot = memchr(name.buf, '.', (size_t)name.size);
            if (dot) name = span_make_in(name, name.buf, dot);
            js_syntax_error(p, name, 8006, "'", strncmp(keyword, "namespace", 9) == 0 ? "namespace" : "module",
                "' declarations can only be used in TypeScript files.");
            return;
        }
        case STATEMENT_DECL_FUNCTION:
            if (s->decl.function.impl->has_body) return;
            js_syntax_error(p, span_with_semicolon(span_make(p->shared->source, start, span_end_ptr(s->decl.function.impl->full_span))), 8017,
                "Signature declarations can only be used in TypeScript files.");
            return;
        default: return;
    }
}

// parse_class_decl() for a statement's class, whose modifiers fail tsc's checkGrammarModifiers (`modifiers_failed`) or
// whose decorators came before `export` (`leading_decorators`): see ParserShared.decl_modifiers_failed
static bool parse_class_decl_after(Parser* p, bool abstract, bool modifiers_failed, bool leading_decorators, ClassDecl* ret) {
    p->shared->decl_modifiers_failed = modifiers_failed;
    p->shared->decl_leading_decorators = leading_decorators;
    bool parsed = parse_class_decl(p, abstract, ret);
    p->shared->decl_modifiers_failed = false;
    p->shared->decl_leading_decorators = false;
    return parsed;
}

static bool parse_decl_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);
    intptr_t mark = error_mark(&p);

    union {
        VariableStatement variable;
        FunctionImpl function;
        EnumDecl enum_decl;
        ClassDecl class;
        TypeDecl type_decl;
        InterfaceDecl interface_decl;
        NamespaceDecl namespace_decl;
        Expr expr;
        Block block;
    } parsed;

    // Decorators can come before `export`, the ones after it are handled by parse_class_decl()
    VecExpr decorators = {0};
    Span decorators_span = span_null;
    if (parse_decorators(&p, &decorators)) {
        decorators_span = span_from(a);
    }

    bool decorated = !vec_expr_is_empty(&decorators);
    ModifierList modifiers;
    p.shared->modifiers_after_decorators = decorated;
    bool has_modifiers = parse_modifier_list(&p, false, &modifiers);
    p.shared->modifiers_after_decorators = false;
    if (!has_modifiers) {
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    if (vec_expr_is_empty(&decorators) && vec_spanned_modifier_size(&modifiers.spanned) &&
        vec_spanned_modifier_front(&modifiers.spanned)->tag == MODIFIER_EXPORT_DEFAULT
    ) {
        // A statement doesn't start with `default` (tsc's isDeclaration; after decorators it's a modifier, TS1029):
        // the list's 'export' expected
        modifier_list_drop(&modifiers);
        return parse_fail(&p);
    }
    hop_whitespace(&p);
    if (modifiers.includes[MODIFIER_EXPORT] && !modifiers.includes[MODIFIER_EXPORT_DEFAULT] && *p.pos == '@') {
        // `export @dec default class C {}`: a default export whose decorators are where none are valid (tsc's
        // checkGrammarModifiers, in JavaScript getJSSyntacticDiagnosticsForFile: TS1206 at the first)
        Parser q = parser(&p);
        char const* at = q.pos;
        VecExpr between = {0};
        if (parse_decorators(&q, &between)) {
            hop_whitespace(&q);
            char const* default_at = q.pos;
            Parser c = parser(&q);
            bool is_default = hop_start_if_ends_on_word_break(&c, "default");
            hop_whitespace(&c);
            bool default_class = is_default && (starts_with_followed_by_word_break(&c, "class") || starts_with_followed_by_word_break(&c, "abstract"));
            if (is_default) {
                parse_success(&c);
                parse_success(&q);
                Span first = span_make(p.shared->source, at, span_end_ptr(expr_outer_span(vec_expr_at(&between, 0))));
                // (Another default export, `export @dec default function f() {}`, is TS1206 below, at the same `@`)
                if (default_class && p.shared->source->is_js) js_syntax_error(&p, first, 1206, "Decorators are not valid here.");
                // (Not in a namespace, where checkGrammarModifiers' `default` says TS1319 first, see below)
                else if (default_class && !(toplevel && p.shared->in_namespace_body && p.shared->statement_parent == MODIFIED_IN_MODULE_BLOCK)) {
                    grammar_error(&p, first, 1206, "Decorators are not valid here.");
                }
                if (vec_expr_is_empty(&decorators)) decorators_span = first;
                for_each (i, vec_expr, &between) vec_expr_push(&decorators, *i);
                between.size = 0;
                modifiers.includes[MODIFIER_EXPORT_DEFAULT] = true;
                // (The `default` is a modifier like any, which the modifier checks see: TS1319 in a namespace)
                vec_spanned_modifier_push(&modifiers.spanned, (SpannedModifier){ .tag = MODIFIER_EXPORT_DEFAULT,
                    .span = span_make(p.shared->source, default_at, default_at + strlen("default")) });
            } else {
                parse_fail(&c);
                parse_fail(&q);
            }
        } else {
            parse_fail(&q);
        }
        vec_expr_drop(&between);
    }
    if (vec_spanned_modifier_size(&modifiers.spanned) && *p.pos == '@') {
        // Decorators after the modifiers, then maybe more modifiers (tsc's parseModifiers with allowDecorators: leading
        // decorators, modifiers, trailing decorators, modifiers), before any declaration: `export @dec function f() {}`
        // is TS1206 like a leading one. A class's own are parse_class_decl()'s (TS8038 for both)
        Parser q = parser(&p);
        char const* at = q.pos;
        VecExpr trailing = {0};
        ModifierList more = {0};
        bool taken = false;
        if (parse_decorators(&q, &trailing)) {
            hop_whitespace(&q);
            if (parse_modifier_list(&q, false, &more)) hop_whitespace(&q);
            taken = vec_spanned_modifier_size(&more.spanned) || !starts_with_followed_by_word_break(&q, "class");
        }
        if (taken) {
            parse_success(&q);
            if (vec_expr_is_empty(&decorators)) decorators_span = span_make(p.shared->source, at, at + 1);
            for_each (i, vec_expr, &trailing) vec_expr_push(&decorators, *i);
            trailing.size = 0;
            for_each (i, vec_spanned_modifier, &more.spanned) {
                vec_spanned_modifier_push(&modifiers.spanned, *i);
                modifiers.includes[i->tag] = true;
            }
        } else {
            parse_fail(&q);
        }
        vec_expr_drop(&trailing);
        modifier_list_drop(&more);
    }
    decorated = !vec_expr_is_empty(&decorators); // (Trailing ones too: `export @dec type { A }` is a type alias)
    const char* keyword = p.pos;
    bool export_assignment_modifiers = false; // `declare export default x`: the modifiers before `export default`
    DeclStatementProps decl_props = {
        .exported = modifiers.includes[MODIFIER_EXPORT_DEFAULT] ?
            DECL_EXPORTED_YES_DEFAULT :
            (modifiers.includes[MODIFIER_EXPORT] ?
                DECL_EXPORTED_YES :
                DECL_EXPORTED_NO
            ),
        .declared = modifiers.includes[MODIFIER_DECLARE] || p.shared->source->is_declaration,
    };
    for_each (i, vec_spanned_modifier, &modifiers.spanned) {
        if (i->tag == MODIFIER_DECLARE) decl_props.declare_keyword = i->span;
    }

    // tsc's checkGrammarModifiers() once the kind of declaration is known. What's inside a `declare` is ambient
    ModifiedNode modified = {
        .parent = toplevel ? p.shared->statement_parent : MODIFIED_IN_OTHER,
        .parent_ambient = p.shared->in_ambient,
        .parent_is_namespace = p.shared->in_namespace_body,
    };
    bool outer_ambient = p.shared->in_ambient;
    p.shared->in_ambient |= modifiers.includes[MODIFIER_DECLARE];
    ModifiedNode dry = modified;
    dry.parent_ambient |= outer_ambient;

    // (After decorators `using` is a variable statement whatever follows: tsc's parseDeclarationWorker, reached
    // without isUsingDeclaration's lookahead, `@dec using 1` is TS1134)
    p.shared->using_forced = !vec_expr_is_empty(&decorators);
    p.shared->let_forced = vec_spanned_modifier_size(&modifiers.spanned) > 0;
    // (The dry checks below only for the kind the next word can start: one per declaration, not one per kind tried)
    Parser word_p = parser(&p);
    hop_whitespace(&word_p);
    char const* word = word_p.pos;
    parse_fail(&word_p);
    bool any_modifier = !vec_spanned_modifier_is_empty(&modifiers.spanned);
#define WORD_NEXT(w) (strncmp(word, w, sizeof(w) - 1) == 0)
    p.shared->decl_modifiers_failed = any_modifier && (WORD_NEXT("var") || WORD_NEXT("let") || WORD_NEXT("const") ||
        WORD_NEXT("using") || WORD_NEXT("await")) && modifiers_would_fail(&p, &modifiers, dry, MODIFIED_VARIABLE_STATEMENT);
    bool variable = decl_props.exported != DECL_EXPORTED_YES_DEFAULT &&
        parse_variable_statement(&p, expr_ctx_null, false, &parsed.variable);
    p.shared->decl_modifiers_failed = false;
    p.shared->using_forced = false;
    p.shared->let_forced = false;
    if (variable) {
        modified.kind = MODIFIED_VARIABLE_STATEMENT;
        modified.using_kind = parsed.variable.keyword == VAR_INIT_USING ? 1 :
            parsed.variable.keyword == VAR_INIT_AWAIT_USING ? 2 :
            0;
        *ret = (Statement) {
            .tag = STATEMENT_DECL_VARIABLE,
            .decl = {
                .props = decl_props,
                .variable = parsed.variable,
            },
        };
    } else if (parse_function_keyword_function(&p, modifiers.includes[MODIFIER_ASYNC],
        decl_props.exported != DECL_EXPORTED_YES_DEFAULT, &parsed.function)) {
        modified.kind = MODIFIED_FUNCTION;
        parsed.function.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_FUNCTION,
            .decl = {
                .props = decl_props,
                .function = {
                    .impl = ast_new(FunctionImpl, parsed.function),
                },
            },
        };
    } else if (parse_class_decl_after(&p, modifiers.includes[MODIFIER_ABSTRACT],
        any_modifier && WORD_NEXT("class") && modifiers_would_fail(&p, &modifiers, dry, MODIFIED_CLASS), decorated, &parsed.class)
    ) {
        modified.kind = MODIFIED_CLASS;
        parsed.class.full_span = span_from(a);
        if (!span_valid(parsed.class.name) && decl_props.exported != DECL_EXPORTED_YES_DEFAULT) {
            // tsc's checkClassDeclaration, on the declaration's first token
            Tok first = tok_at(&p, a.start, false);
            grammar_error(&p, span_make(p.shared->source, first.start, first.end), 1211,
                "A class declaration without the 'default' modifier must have a name.");
        }
        *ret = (Statement){
            .tag = STATEMENT_DECL_CLASS,
            .decl = {
                .props = decl_props,
                .class = ast_new(ClassDecl, parsed.class),
            },
        };
    } else if ((p.shared->decl_committed = decorated, parse_type_decl(&p, vec_spanned_modifier_size(&modifiers.spanned) &&
        vec_spanned_modifier_back(&modifiers.spanned)->tag == MODIFIER_DECLARE, &parsed.type_decl))
    ) {
        modified.kind = MODIFIED_TYPE_ALIAS;
        parsed.type_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_TYPE,
            .decl = {
                .props = decl_props,
                .type = ast_new(TypeDecl, parsed.type_decl),
            },
        };
    } else if ((p.shared->decl_committed = decorated,
        p.shared->decl_modifiers_failed = any_modifier && WORD_NEXT("interface") &&
            modifiers_would_fail(&p, &modifiers, dry, MODIFIED_INTERFACE),
        parse_interface_decl(&p, decl_props.exported == DECL_EXPORTED_YES_DEFAULT, &parsed.interface_decl))
    ) {
        modified.kind = MODIFIED_INTERFACE;
        parsed.interface_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_INTERFACE,
            .decl = {
                .props = decl_props,
                .interface = ast_new(InterfaceDecl, parsed.interface_decl),
            },
        };
    } else if (parse_enum_decl(&p, &parsed.enum_decl)) {
        modified.kind = MODIFIED_ENUM;
        parsed.enum_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_ENUM,
            .decl = {
                .props = decl_props,
                .enum_ = ast_new(EnumDecl, parsed.enum_decl),
            },
        };
    } else if (parse_global_block(&p, &parsed.namespace_decl)) {
        modified.kind = MODIFIED_MODULE;
        if (decl_props.declared) {
            for_each (i, vec_statement, &parsed.namespace_decl.contents.statements) {
                if (statement_tag_is_decl(i->tag)) {
                    i->decl.props.declared = true;
                }
            }
        }
        parsed.namespace_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = decl_props,
                .namespace = ast_new(NamespaceDecl, parsed.namespace_decl),
            },
        };
    } else if (decl_props.exported == DECL_EXPORTED_YES_DEFAULT && (parse_expr(&p, expr_ctx_null, &parsed.expr) ||
        // (`export default;`, `export default var a`: TS1109 and an export of nothing, like tsc's
        // parseExportAssignment. The rest of the line is a statement of its own)
        (expect_expr(&p, expr_ctx_null, &parsed.expr), true))
    ) {
        modified.kind = MODIFIED_EXPORT_ASSIGNMENT;
        mark_named_evaluation(&parsed.expr, false);
        *ret = (Statement){
            .tag = STATEMENT_EXPORT_DEFAULT_EXPR,
            .expr = parsed.expr,
        };
        // The `export default` before an expression is no pair of modifiers to tsc (nextTokenCanFollowDefaultKeyword):
        // it is the export assignment, which the modifiers before it are on (TS1120, and `declare` makes it ambient)
        isize n = vec_spanned_modifier_size(&modifiers.spanned);
        if (n >= 2 && vec_spanned_modifier_at(&modifiers.spanned, n - 1)->tag == MODIFIER_EXPORT_DEFAULT &&
            vec_spanned_modifier_at(&modifiers.spanned, n - 2)->tag == MODIFIER_EXPORT
        ) {
            modifiers.spanned.size -= 2;
            modifiers.includes[MODIFIER_EXPORT_DEFAULT] = false;
            modifiers.includes[MODIFIER_EXPORT] = false;
            for_each (i, vec_spanned_modifier, &modifiers.spanned) modifiers.includes[i->tag] = true;
            ret->export_declared = modifiers.includes[MODIFIER_DECLARE];
            export_assignment_modifiers = true;
        }
    } else if (decl_props.exported != DECL_EXPORTED_YES_DEFAULT &&
        (p.shared->decl_committed = decorated, parse_namespace(&p, &parsed.namespace_decl))
    ) {
        modified.kind = MODIFIED_MODULE;
        parsed.namespace_decl.modifiers_failed = modifiers_would_fail(&p, &modifiers, dry, MODIFIED_MODULE);
        parsed.namespace_decl.full_span = span_from(a);
        *ret = (Statement){
            .tag = STATEMENT_DECL_NAMESPACE,
            .decl = {
                .props = decl_props,
                .namespace = ast_new(NamespaceDecl, parsed.namespace_decl),
            },
        };
    } else if ((!vec_spanned_modifier_is_empty(&modifiers.spanned) || !vec_expr_is_empty(&decorators)) &&
        (*p.pos == 'i' || *p.pos == 'e') &&
        (p.shared->import_export_committed = !vec_expr_is_empty(&decorators) &&
            (starts_with_followed_by_word_break(&p, "import") || starts_with_followed_by_word_break(&p, "export")),
         parse_import_export_statement(&p, ret))
    ) {
        // `export public import a = b`, `export export = x`, `declare import x from "m"`: tsc parses modifiers in front
        // of any declaration and complains in the checker
        unsigned code = 0;
        if (ret->tag == STATEMENT_EXPORT_DEFAULT_EXPR) {
            modified.kind = MODIFIED_EXPORT_ASSIGNMENT;
            ret->export_declared = modifiers.includes[MODIFIER_DECLARE];
            code = 1120;
        } else {
            PortItem* main = &ret->import_export->main;
            bool is_import = span_equals(ret->import_export->keyword_span, "import");
            if (main->tag == PORT_IMPORT_ALIAS || main->is_require) {
                modified.kind = MODIFIED_IMPORT_EQUALS;
                main->export_import |= modifiers.includes[MODIFIER_EXPORT];
                main->declared |= modifiers.includes[MODIFIER_DECLARE];
                main->full_span = span_from(a); // (The declaration starts at its modifiers)
            } else if (is_import) {
                modified.kind = MODIFIED_IMPORT;
                code = 1191;
            } else if (main->tag == PORT_EXPORT_AS_NAMESPACE) {
                modified.kind = MODIFIED_NAMESPACE_EXPORT;
                main->full_span = span_from(a);
                main->modifiers = main->full_span;
            } else {
                modified.kind = MODIFIED_EXPORT_DECLARATION;
                main->declared |= modifiers.includes[MODIFIER_DECLARE];
                code = 1193;
            }
        }
        p.shared->in_ambient = outer_ambient;
        // (tsc's checkGrammarModifiers: decorators first, at their first token. Not in a block or function, where
        // checkGrammarModuleElementContext's TS1232 comes first and ends the check)
        bool misplaced = ret->tag == STATEMENT_IMPORT_EXPORT && p.shared->statement_parent == MODIFIED_IN_OTHER &&
            !ret->import_export->main.is_require;
        // (Nor on an `export as namespace`, whose modifiers the checker never checks: the binder's TS1184 alone)
        bool decorators_invalid = !vec_expr_is_empty(&decorators) && !misplaced && modified.kind != MODIFIED_NAMESPACE_EXPORT;
        if (decorators_invalid) {
            grammar_error(&p, span_make(p.shared->source, decorators_span.buf, decorators_span.buf + 1), 1206,
                "Decorators are not valid here.");
        }
        if (!vec_expr_is_empty(&decorators) && ret->tag == STATEMENT_IMPORT_EXPORT) {
            port_statement_rare_mut(ret->import_export)->decorators = decorators;
            decorators = (VecExpr){0};
        }
        // (TS1206 ends tsc's checkGrammarModifiers: no TS1191 for `export @dec import("./m")`)
        if (vec_spanned_modifier_is_empty(&modifiers.spanned) || decorators_invalid) {
            modifier_list_drop(&modifiers);
            vec_expr_drop(&decorators);
            return parse_success(&p);
        }
        Span first = vec_spanned_modifier_front(&modifiers.spanned)->span;
        // (tsc's checker never checks an `export as namespace`'s modifiers, its binder says TS1184: scope_add_port())
        if (modified.kind != MODIFIED_NAMESPACE_EXPORT && !check_modifiers(&p, &modifiers, modified)) {
            if (code == 1120) grammar_error(&p, first, 1120, "An export assignment cannot have modifiers.");
            if (code == 1191) grammar_error(&p, first, 1191, "An import declaration cannot have modifiers.");
            if (code == 1193) grammar_error(&p, first, 1193, "An export declaration cannot have modifiers.");
        }
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_success(&p);
    } else if (decorated && decl_props.exported != DECL_EXPORTED_YES_DEFAULT) {
        // Decorators and modifiers before no declaration (tsc's parseDeclarationWorker: a MissingDeclaration, TS1146
        // where its end is, before the trivia; parse_statement() keeps the decorators the same way)
        p.shared->in_ambient = outer_ambient;
        // (The end of the last decorator or modifier; a decorator missing its expression, `@ ;`, ends at its `@`)
        const char* end = nullptr;
        for_each (i, vec_expr, &decorators) {
            Span e = expr_outer_span(i);
            const char* e_end = span_end_ptr(e);
            if (!e.size) {
                const char* c = span_start_ptr(e);
                while (c > a.start && is_js_ascii_space(c[-1])) c--;
                if (c > a.start && c[-1] == '@') e_end = c;
            }
            if (!end || e_end > end) end = e_end;
        }
        for_each (i, vec_spanned_modifier, &modifiers.spanned) {
            if (span_end_ptr(i->span) > end) end = span_end_ptr(i->span);
        }
        modifier_list_drop(&modifiers);
        p.pos = end;
        p.whitespace = WHITESPACE_NONE;
        p.whitespace_end = nullptr;
        parse_error(&p, span_from_pos(&p), 1146, "Declaration expected.");
        *ret = (Statement){ .tag = STATEMENT_EXPR, .expr = expr_implicit_any(span_from_pos(&p)) };
        ret->expr.missing_decorators = ast_new(VecExpr, decorators);
        return parse_success(&p);
    } else {
        p.shared->in_ambient = outer_ambient;
        modifier_list_drop(&modifiers);
        vec_expr_drop(&decorators);
        return parse_fail(&p);
    }
    p.shared->in_ambient = outer_ambient;
    bool modifiers_reported = check_modifiers(&p, &modifiers, modified);
    if (export_assignment_modifiers && !modifiers_reported && !vec_spanned_modifier_is_empty(&modifiers.spanned)) {
        grammar_error(&p, vec_spanned_modifier_front(&modifiers.spanned)->span, 1120, "An export assignment cannot have modifiers.");
    }
    js_syntax_declaration(&p, mark, a.start, keyword, ret);

    if (!vec_expr_is_empty(&decorators)) {
        if (ret->tag == STATEMENT_DECL_CLASS) {
            if (!vec_expr_is_empty(&ret->decl.class->decorators)) {
                // Decorators both before and after `export` (tsc's checkGrammarModifiers: at the first after)
                char const* at = span_start_ptr(expr_outer_span(vec_expr_at(&ret->decl.class->decorators, 0)));
                char const* file_start = source_file_start(p.shared->source);
                while (at > file_start && *at != '@') at--;
                Span trailing = expr_outer_span(vec_expr_at(&ret->decl.class->decorators, 0));
                Span trailing_span = span_make(p.shared->source, at, span_end_ptr(trailing));
                cstr message = cstr_from("Decorators may not appear after 'export' or 'export default' if they also appear before 'export'.");
                // (In JavaScript one of tsc's getJSSyntacticDiagnosticsForFile, whatever else the file has)
                // (checkGrammarModifiers stops at its first complaint: one about a modifier before the trailing
                // decorators, `@a export export @b class`'s TS1030, is all it says)
                Error* error = !p.shared->source->is_js ? (modifiers_reported ? (cstr_drop(&message), nullptr) :
                        grammar_error_s(&p, trailing_span, 8038, message)) :
                    reports_js_syntax(&p) ? add_error_to_parser(&p, (Error){ .message = message, .span = trailing_span,
                        .ts_code = 8038, .phase = ERROR_PHASE_PARSE }) : (cstr_drop(&message), nullptr);
                if (error) {
                    Span leading = expr_outer_span(vec_expr_at(&decorators, 0));
                    error_add_related_d(error, span_make(p.shared->source, decorators_span.buf, span_end_ptr(leading)),
                        1486, "Decorator used before 'export' here.");
                }
            }
            // These come before any that parse_class_decl() found after `export`
            for_each (i, vec_expr, &ret->decl.class->decorators) {
                vec_expr_push(&decorators, *i);
            }
            ret->decl.class->decorators.size = 0; // The elements were moved
            vec_expr_drop(&ret->decl.class->decorators);
            ret->decl.class->decorators = decorators;
        } else {
            // (tsc's grammarErrorOnFirstToken: the `@`)
            grammar_error(&p, span_make(p.shared->source, decorators_span.buf, decorators_span.buf + 1), 1206,
                "Decorators are not valid here.");
            // (And in JavaScript, checked or not, tsgo's getJSSyntacticDiagnosticsForFile: over the first decorator, a
            // diagnostic of its own next to the checker's at the `@`. Not on `module "m"` or `global`, which tsgo's
            // parseAmbientExternalModuleDeclaration never gives checkJSSyntax)
            bool external_module = ret->tag == STATEMENT_DECL_NAMESPACE &&
                (ret->decl.namespace->is_ambient_module || ret->decl.namespace->is_global);
            if (reports_js_syntax(&p) && !external_module) {
                Span first = span_make(p.shared->source, decorators_span.buf,
                    span_end_ptr(expr_outer_span(vec_expr_at(&decorators, 0))));
                add_error_to_parser(&p, (Error){ .message = cstr_from("Decorators are not valid here."), .span = first,
                    .ts_code = 1206, .phase = ERROR_PHASE_PARSE, .js_syntax = true });
            }
            if (statement_declares_name(ret->tag) && !ret->decl.props.invalid_decorators) {
                ret->decl.props.invalid_decorators = ast_new(VecExpr, decorators);
            } else {
                vec_expr_drop(&decorators);
            }
        }
    }

    modifier_list_drop(&modifiers);
    return parse_success(&p);
}

// What a statement's JSDoc says (tsc's getJSDocCommentsAndTags / getNextJSDocCommentLocation, from the host down):
// a variable statement's `@type` is its single declaration's annotation, and like the tags of an assignment statement
// or `return` its `@param`s/`@returns` are for the function it gives
// `/** @satisfies {T} */ const x = v`: v is checked like `v satisfies T` (tsc's checkDeclarationInitializer and
// getContextualTypeForVariableLikeDeclaration: tryGetJSDocSatisfiesTypeNode), the first declaration with an initializer
// (tsgo's reparser). An initializer `/** @satisfies {U} */ (v)` is wrapped again (tsgo's reparser casts whatever the
// initializer is). TS1360 is at the tag, unless the declaration has JSDoc of its own (`const /** c */ x = v`, tsgo's
// findOriginatingJSDocSatisfiesTag stops at the first host with JSDoc): that JSDoc's `@satisfies`, else the token after v
static void jsdoc_apply_satisfies_initializer(Parser* p, VecJsDocTag* tags, VecInitializedBinding* list) {
    JsDocTag* tag = jsdoc_find_typed(tags, JSDOC_TAG_SATISFIES);
    if (!tag) return;
    for_each (i, vec_initialized_binding, list) {
        Expr* value = initialized_binding_value(i);
        // (Under a type annotation, `@type` or written, too, and an empty object literal: tsgo's reparser casts any
        // initializer)
        if (value->tag == EXPR_IMPLICIT_ANY) {
            if (value->missing) return;
            continue;
        }
        Span at = tag->tag_name;
        if (span_valid(i->binding.content_span) && jsdoc_locate(p, span_start_ptr(i->binding.content_span), true) >= 0) {
            if (value->tag == EXPR_AS_TYPE && value->as_type->is_satisfies && value->as_type->is_jsdoc &&
                span_start_ptr(value->as_type->keyword_span) < span_start_ptr(i->binding.content_span)
            ) {
                at = value->as_type->keyword_span;
            } else {
                at = jsdoc_token_after(expr_outer_span(value));
            }
        }
        jsdoc_wrap_satisfies(value, at, jsdoc_take_type(tag, nullptr));
        return;
    }
}

// Whether an assignment's target makes it a JavaScript assignment declaration (tsgo's GetAssignmentDeclarationKind):
// `module.exports`, a property of an entity name (`a.b.c`, `this.x`, `exports.x`), by name or element access
static bool jsdoc_assignment_declares(Expr const* left) {
    if (left->tag != EXPR_PROPERTY || expr_is_perenthisized(left)) return false;
    if (left->property->form == PROPERTY_EXPR_DOT && property_name(left->property).size &&
        property_name(left->property).buf[0] == '#'
    ) {
        return false;
    }
    Expr const* object = &left->property->expr;
    while (object->tag == EXPR_PROPERTY && object->property->form == PROPERTY_EXPR_DOT && !expr_is_perenthisized(object)) {
        object = &object->property->expr;
    }
    return object->tag == EXPR_IDENTIFIER && !expr_is_perenthisized(object) &&
        (!object->identifier.keyword || object->identifier.keyword == KEYWORD_IDENTIFIER_THIS);
}

static void jsdoc_apply_statement(Parser* p, const char* start, Statement* statement) {
    VecJsDocTag tags = {0};
    if (!jsdoc_before_leading(p, start, &tags)) return;
    switch (statement->tag) {
        case STATEMENT_DECL_VARIABLE: {
            VecInitializedBinding* list = &statement->decl.variable.initializers;
            if (vec_initialized_binding_size(list) > 1) {
                // The `@type` is the first declaration's, the function tags its initializer's (tsgo's reparser,
                // getFunctionLikeHost: the first declaration's initializer only)
                if (!list->data[0].binding.parent) {
                    if (jsdoc_apply_type_to_binding(&tags, &list->data[0].binding)) {
                        unique_symbol_variable_check(p, &list->data[0].binding, false, statement->decl.variable.keyword);
                    }
                }
                jsdoc_apply_satisfies_initializer(p, &tags, list);
                FunctionImpl* function = jsdoc_function_of(initialized_binding_value(&list->data[0]));
                if (function) jsdoc_apply_function(p, &tags, function);
                break;
            }
            if (vec_initialized_binding_size(list) != 1) break;
            InitializedBinding* initializer = list->data;
            bool type_tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
            if (jsdoc_apply_type_to_binding(&tags, &initializer->binding)) {
                unique_symbol_variable_check(p, &initializer->binding, false, statement->decl.variable.keyword);
            }
            if (type_tag && initialized_binding_rare(initializer)->require_port_count) {
                // `/** @type {T} */ const x = require("m")` is a variable, whose initializer is checked against T
                // (tsc's bindVariableDeclarationOrBindingElement: isVariableDeclarationInitializedToBareOrAccessedRequire
                // doesn't apply with a type tag)
                for (u32 i = 0; i < initializer->rare->require_port_count; i++) port_item_drop(&initializer->rare->require_ports[i]);
                ast_free(initializer->rare->require_ports, initializer->rare->require_port_count * c_sizeof(PortItem));
                initializer->rare->require_ports = nullptr;
                initializer->rare->require_port_count = 0;
            }
            Expr* call = initialized_binding_value(initializer);
            if (type_tag && call->tag == EXPR_CALL && call->call->callable.tag == EXPR_IDENTIFIER &&
                call->call->callable.identifier.require_call
            ) {
                call->call->callable.identifier.require_call = 1;
            }
            jsdoc_apply_satisfies_initializer(p, &tags, list);
            FunctionImpl* function = jsdoc_function_of(initialized_binding_value(initializer));
            // (A `@template` here has no host, tsgo's reparser drops it: its names are unresolved, TS2304)
            // (The variable's `@type` is its declared type only, the function's contextual type: tsgo has no
            // getReturnTypeOfTypeTag, the function's return type is inferred)
            if (function) jsdoc_apply_function(p, &tags, function);
            // (Not a class expression's: tsgo's reparser applies class tags to a class declaration only, getClassLikeData)
        }   break;
        case STATEMENT_DECL_FUNCTION: {
            p->shared->jsdoc_declaration = true;
            FunctionImpl const* impl = statement->decl.function.impl;
            // An `@overload` signature of a function missing its name (TS1003 already): tsgo's reparser reports the
            // missing name again, on the character before it (checkNonIdentifierName in reparseJSDocSignature)
            if (span_valid(impl->name) && !impl->name.size && p->shared->source->is_js) {
                bool overload = false;
                for_each (i, vec_jsdoc_tag, &tags) overload |= i->kind == JSDOC_TAG_OVERLOAD;
                char const* at = span_start_ptr(impl->name);
                if (overload && at > source_file_start(p->shared->source)) {
                    parse_error(p, span_make(p->shared->source, at - 1, at), 1003, "Identifier expected.");
                }
            }
            jsdoc_apply_function(p, &tags, statement->decl.function.impl);
        }   break;
        case STATEMENT_DECL_CLASS:
            jsdoc_apply_class(p, &tags, statement->decl.class);
            break;
        case STATEMENT_EXPR:
            if (statement->expr.tag == EXPR_BINARY_OP && statement->expr.binary_op->op == BINARY_OP_EQUAL) {
                BinaryOpExpr* assignment = statement->expr.binary_op;
                // (Through a chain `a = b = function () {}`: tsc's getNextJSDocCommentLocation goes up through
                // assignments)
                Expr* assigned = &assignment->right;
                while (assigned->tag == EXPR_BINARY_OP && assigned->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(assigned)) {
                    assigned = &assigned->binary_op->right;
                }
                // (Not through `a.b = a.b || function () {}`: tsgo's GetRightMostAssignedExpression only goes through
                // assignments)
                // (`/** @type {T} */ (a).b = v`: the comment is the parenthesized expression's cast, not the statement's)
                Expr const* leftmost = &assignment->left;
                while (leftmost->tag == EXPR_PROPERTY && !expr_is_perenthisized(leftmost)) leftmost = &leftmost->property->expr;
                if ((expr_is_perenthisized(leftmost) || (leftmost->tag == EXPR_AS_TYPE && leftmost->as_type->is_jsdoc)) &&
                    span_start_ptr(expr_outer_span(leftmost)) == span_start_ptr(expr_outer_span(&statement->expr))
                ) {
                    break;
                }
                // An assignment declaration (tsgo's GetAssignmentDeclarationKind, syntactic) takes the `@type` as its
                // declared type, which a function value is only contextually typed by; another assignment's function
                // takes it as its signature (getFunctionLikeHost: FullSignature)
                bool declares = jsdoc_assignment_declares(&assignment->left);
                JsDocTag* satisfies = declares ? jsdoc_find_typed(&tags, JSDOC_TAG_SATISFIES) : nullptr;
                if (satisfies && assignment->right.tag != EXPR_IMPLICIT_ANY) {
                    // An assignment declaration's `@satisfies` casts what it assigns (tsgo's reparser)
                    Expr inner = assignment->right;
                    assignment->right = expr_as_type(expr_outer_span(&inner), ast_new(AsTypeExpr, {
                        .expr = inner,
                        .keyword_span = satisfies->tag_name,
                        .type = jsdoc_take_type(satisfies, nullptr),
                        .is_satisfies = true,
                        .is_jsdoc = true,
                    }));
                    if (assigned == &assignment->right) assigned = &assignment->right;
                }
                FunctionImpl* function = jsdoc_function_of(assigned);
                if (function && declares) {
                    JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                    if (tag) assignment->jsdoc_type = ast_new(TypeNode, jsdoc_take_type(tag, nullptr));
                }
                if (function) {
                    // (The statement's comment isn't the function's for checkUnmatchedJSDocParameters: tsgo's
                    // GetNextJSDocCommentLocation doesn't go from it up through an assignment)
                    function->jsdoc_params_checked = true;
                    jsdoc_apply_function(p, &tags, function);
                } else if (assigned->tag == EXPR_CLASS_DECL && !expr_is_perenthisized(assigned)) {
                    // (A class expression takes no class tags: tsgo's getClassLikeData)
                } else if (declares) {
                    // The declared type of what the assignment declares (`this.x`, `exports.x`, `module.exports`)
                    JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                    if (tag) assignment->jsdoc_type = ast_new(TypeNode, jsdoc_take_type(tag, nullptr));
                    // (and of the assignments it assigns: `this.a = this.b = v`)
                    for (Expr* e = &assignment->right; assignment->jsdoc_type && e->tag == EXPR_BINARY_OP &&
                        e->binary_op->op == BINARY_OP_EQUAL && !expr_is_perenthisized(e) && !e->binary_op->jsdoc_type;
                        e = &e->binary_op->right
                    ) {
                        e->binary_op->jsdoc_type = assignment->jsdoc_type;
                        e->binary_op->jsdoc_type_borrowed = true;
                    }
                }
                assignment->jsdoc_modifiers = jsdoc_modifiers(&tags);
            } else if (statement->expr.tag == EXPR_BINARY_OP && binary_op_is_assignment(statement->expr.binary_op->op)) {
                // A compound assignment's function (`F.prototype.m ??= function () {}`): tsc's getNextJSDocCommentLocation
                // goes up from it through any assignment expression (isAssignmentExpression, compound ones too)
                FunctionImpl* function = jsdoc_function_of(&statement->expr.binary_op->right);
                if (function) {
                    jsdoc_apply_function(p, &tags, function);
                }
            }
            break;
        case STATEMENT_RETURN_EXPR:
        case STATEMENT_EXPORT_DEFAULT_EXPR: {
            if (statement->tag == STATEMENT_RETURN_EXPR) {
                // `/** @type {T} */ return e` is `return e as T` (tsgo's reparser casts the expression), which a
                // function expression is then contextually typed by: not its signature, nor its tags' host
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                Expr* e = &statement->expr;
                if (tag && e->tag != EXPR_IMPLICIT_ANY && !(e->tag == EXPR_AS_TYPE && e->as_type->is_jsdoc)) {
                    Expr inner = *e;
                    *e = expr_as_type(expr_outer_span(&inner), ast_new(AsTypeExpr, {
                        .expr = inner,
                        .keyword_span = tag->type_span,
                        .type = jsdoc_take_type(tag, nullptr),
                        .is_jsdoc = true,
                    }));
                    break;
                }
            }
            FunctionImpl* function = jsdoc_function_of(&statement->expr);
            if (function && statement->tag == STATEMENT_EXPORT_DEFAULT_EXPR && !statement->export_equals) {
                // `/** @type {T} */ export default () => {}`: T is the export assignment's type (tsgo's reparseHosted
                // sets it before getFunctionLikeHost could make it the function's full signature), which the
                // function is contextually typed by and declaration emit writes as `const _default: T`
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                if (tag) statement->expr_jsdoc_type = ast_new(TypeNode, jsdoc_take_type(tag, nullptr));
            }
            if (function) {
                jsdoc_apply_function(p, &tags, function);
            } else if (statement->tag == STATEMENT_EXPORT_DEFAULT_EXPR && !statement->export_equals) {
                // `/** @type {T} */ export default e`: e is checked against T and T is the export's type (tsc's
                // checkExportAssignment, getEffectiveTypeAnnotationNode). Not a cast of a parenthesized e: tsgo's
                // JSDoc is the statement's (`export default ([])` is `never[]` itself)
                JsDocTag* tag = jsdoc_find_typed(&tags, JSDOC_TAG_TYPE);
                if (tag) statement->expr_jsdoc_type = ast_new(TypeNode, jsdoc_take_type(tag, nullptr));
            }
        }   break;
        default:
            break;
    }
    // (The tags no host took aren't reparsed into tsgo's tree: nothing checks their types)
    vec_jsdoc_tag_drop(&tags);
}

// What the first word of a statement allows (statement_word()): only these words start a declaration (modifiers,
// variable keywords, declaration keywords: parse_decl_statement()), an import or export, a label
typedef struct StatementWord {
    bool may_label;
    bool may_declare;
    bool may_import_export;
} StatementWord;

static bool word_starts_declaration(csview word) {
#define WORD_IS(lit) (word.size == sizeof(lit) - 1 && memcmp(word.buf, lit, sizeof(lit) - 1) == 0)
    switch (word.buf[0]) {
        case 'a': return WORD_IS("abstract") || WORD_IS("accessor") || WORD_IS("async") || WORD_IS("await");
        case 'c': return WORD_IS("class") || WORD_IS("const");
        case 'd': return WORD_IS("declare") || WORD_IS("default");
        case 'e': return WORD_IS("enum") || WORD_IS("export");
        case 'f': return WORD_IS("function");
        case 'g': return WORD_IS("global");
        case 'i': return WORD_IS("interface");
        case 'l': return WORD_IS("let");
        case 'm': return WORD_IS("module");
        case 'n': return WORD_IS("namespace");
        case 'o': return WORD_IS("override");
        case 'p': return WORD_IS("private") || WORD_IS("protected") || WORD_IS("public");
        case 'r': return WORD_IS("readonly");
        case 's': return WORD_IS("static");
        case 't': return WORD_IS("type");
        case 'u': return WORD_IS("using");
        case 'v': return WORD_IS("var");
        default: return false;
    }
#undef WORD_IS
}

// At a character that starts an identifier
static StatementWord statement_word(const char* at) {
    const char* end = at;
    while (is_identifier_char(*end)) end++;
    // (An escape or a non-ASCII character: anything may follow, every branch is tried)
    if ((uint8_t)*end >= 0x80 || *end == '\\' || (uint8_t)*at >= 0x80 || *at == '\\') {
        return (StatementWord){ .may_label = true, .may_declare = true, .may_import_export = true };
    }
    csview word = { at, end - at };
    // What follows on the line after blanks: `:` for a label, a comment, line break or anything unusual leaves it open
    const char* next = end;
    while (*next == ' ' || *next == '\t') next++;
    bool next_open = *next == '/' || *next == '\\' || *next == '\n' || *next == '\r' || *next == '\v' || *next == '\f' || (uint8_t)*next >= 0x80;
    return (StatementWord){
        // (parse_label(): a word that isn't reserved, then the `:`)
        .may_label = (*next == ':' || next_open) && !is_reserved_word(word),
        // (`await` only starts a declaration as `await using`)
        .may_declare = word_starts_declaration(word) && (word.size != 5 || memcmp(at, "await", 5) != 0 || next_open ||
            (strncmp(next, "using", 5) == 0 && !is_identifier_char(next[5]))),
        .may_import_export = (word.size == 6 && (memcmp(at, "import", 6) == 0 || memcmp(at, "export", 6) == 0)),
    };
}

// The ASCII letter a unicode escape at `at` spells (`\u0077`, `\u{77}`), else 0
static char escaped_first_char(const char* at) {
    if (at[0] != '\\' || at[1] != 'u') return 0;
    const char* c = at + 2;
    bool braced = *c == '{';
    if (braced) c++;
    unsigned value = 0;
    int digits = 0;
    for (; isxdigit((unsigned char)*c) && (braced || digits < 4); c++, digits++) {
        value = value * 16 + (unsigned)(isdigit((unsigned char)*c) ? *c - '0' : (tolower((unsigned char)*c) - 'a' + 10));
        if (value >= 0x80) return 0;
    }
    if (!digits || (braced ? *c != '}' : digits != 4)) return 0;
    return (char)value;
}

static bool parse_statement(Parser* parent, bool toplevel, Statement* ret) {
    Parser p = parser(parent);
    Anchor a = anchor(&p);

    Span label = span_null;
    ret->flow = nullptr;
    // Every branch before the expression statement needs a particular first character: test it first
    // (A word's escaped first letter is that letter: `\u0077ith` is a with statement, TS1260, tsc's scanner)
    char c = *p.pos == '\\' ? escaped_first_char(p.pos) : *p.pos;
    bool word = identifier_char_size(p.pos, true);
    // A plain ASCII first word (no escape, no non-ASCII character in or right after it) decides which of the branches
    // led by a word can apply: most statements are expression statements and keyword statements, which would
    // otherwise go through a failed label and a failed declaration first
    StatementWord first = word ? statement_word(p.pos) : (StatementWord){0};
    if (c == ';') {
        // tsc's EmptyStatement, the `;` its own (`if (c); else d`)
        *ret = statement_noop;
        p.pos++;
    } else if (hop_start_if_ends_on_word_break(&p, "debugger")) {
        *ret = statement_noop;
    } else if (word && first.may_label && parse_label(&p, &label)) {
        hop_whitespace(&p);
        Statement body;
        expect_statement(&p, false, &body);
        *ret = (Statement){
            .tag = STATEMENT_LABELED,
            .labeled = {
                .name = label,
                .body = ast_new(Statement, body),
            },
        };
        label = span_null;
    } else if ((c == 'i' || c == 'e') && first.may_import_export && parse_import_export_statement(&p, ret)) {
    } else if (((word && first.may_declare) || c == '@') && parse_decl_statement(&p, toplevel, ret)) {
    } else if (c == '{' && parse_block(&p, false, &ret->block)) {
        ret->tag = STATEMENT_BLOCK;
    } else if (c == 'i' && parse_if_else(&p, &ret->if_else)) {
        ret->tag = STATEMENT_IF_ELSE;
    } else if (c == 'f' && parse_for_loop_statement(&p, ret)) {
    } else if (c == 'w' && parse_while_loop_statement(&p, ret)) {
    } else if (c == 'w' && parse_with_statement(&p, ret)) {
    } else if (c == 'd' && parse_do_while_loop_statement(&p, ret)) {
    } else if (c == 's' && parse_switch_statement(&p, ret)) {
    } else if ((c == 't' || c == 'c' || c == 'f') && parse_try_catch_statement(&p, ret)) {
    } else if (hop_start_if_ends_on_word_break(&p, "break")) {
        ret->tag = STATEMENT_BREAK;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (hop_start_if_ends_on_word_break(&p, "continue")) {
        ret->tag = STATEMENT_CONTINUE;
        ret->keyword_span = span_from(a);
        parse_jump_target(&p, &label);
    } else if (c == 'r' && parse_return(&p, &ret->expr)) {
        ret->tag = STATEMENT_RETURN_EXPR;
        ret->expr_jsdoc_type = nullptr;
    } else if (c == 't' && parse_throw(&p, &ret->expr)) {
        ret->tag = STATEMENT_THROW_EXPR;
        ret->expr_jsdoc_type = nullptr;
    } else if (c == '@') {
        // Decorators on no declaration (tsc's parseDeclarationWorker: a MissingDeclaration, an expression statement of
        // the missing expression that keeps them here)
        VecExpr decorators;
        parse_decorators(&p, &decorators);
        unhop_trivia(&p);
        parse_error(&p, span_from_pos(&p), 1146, "Declaration expected.");
        ret->tag = STATEMENT_EXPR;
        ret->expr_jsdoc_type = nullptr;
        ret->expr = expr_implicit_any(span_from_pos(&p));
        ret->expr.missing_decorators = ast_new(VecExpr, decorators);
    } else if (parse_expr(&p, expr_ctx_null, &ret->expr)) {
        ret->tag = STATEMENT_EXPR;
        ret->expr_jsdoc_type = nullptr;
        parse_comma_ops_right_side(&p, &ret->expr);
        if (ret->tag == STATEMENT_EXPR &&
            ret->expr.tag == EXPR_IDENTIFIER &&
            span_equals(ret->expr.inner_span, "static")
        ) {
            hop_whitespace(&p);
            if (p.whitespace != WHITESPACE_NEWLINE && *p.pos != ';' && *p.pos != '}') {
                statement_drop(ret);
                return parse_fail(&p);
            }
        }
    } else if (((strchr("|&^=!>*%?i", c) && c) || (c == '<' && p.shared->source->is_jsx && p.pos[1] != '/')) &&
        tok_is_binary_operator_here(&p, tok_peek(&p))
    ) {
        // (In a JSX file a `<` that starts no element too, tsc's parseUpdateExpression: `<:a />`)
        // A binary operator starts an expression with a missing left operand (tsc's isStartOfExpression): `|| a;`
        ret->tag = STATEMENT_EXPR;
        ret->expr_jsdoc_type = nullptr;
        expect_expr(&p, expr_ctx_null, &ret->expr);
        parse_comma_ops_right_side(&p, &ret->expr);
    } else {
        return parse_fail(&p);
    }

    if (ret->tag == STATEMENT_BREAK || ret->tag == STATEMENT_CONTINUE) ret->jump_label = label;
    ret->span = span_from(a);
    if (p.shared->source->is_js) {
        // (Not an initializer's own JSDoc, `var x = /** @type {T} */ v`: tsgo attaches it to the initializer, a
        // function's FullSignature, and the variable's type is the initializer's)
        jsdoc_apply_statement(&p, a.start, ret);
    }

    reset_fail_cache(&p);
    return parse_success(&p);
}

static inline bool expect_statement(Parser* parent, bool toplevel, Statement* ret) {
    if (parse_statement(parent, toplevel, ret)) {
        parse_statement_semicolon(parent, ret);
        return true;
    } else {
        // tsc's parseStatement has no failure: an expression statement whose expression is missing
        const char* start = parent->pos;
        *ret = (Statement){ .tag = STATEMENT_EXPR };
        expect_expr(parent, expr_ctx_null, &ret->expr);
        parse_comma_ops_right_side(parent, &ret->expr);
        ret->span = span_make(parent->shared->source, start, parent->pos);
        parse_statement_semicolon(parent, ret);
        return false;
    }
}

[[gnu::noinline]] static bool parse_block_slow(Parser* parent, bool toplevel, Block* ret);
// (The quick test inlined where it is called, the rest not: PGO inlined whole parse functions behind it)
static inline bool parse_block(Parser* parent, bool toplevel, Block* ret) {
    if (*parent->pos != '{') GATE_FAIL(parse_block(&v_, toplevel, ret));
    return parse_block_slow(parent, toplevel, ret);
}

[[gnu::noinline]] static bool parse_block_slow(Parser* parent, bool toplevel, Block* ret) {
    Parser p = parser(parent);

    const char* open = p.pos;
    if (!hop_char(&p, '{')) return parse_fail(&p);
    hop_whitespace(&p);

    *ret = (Block){0};
    const char* outer_block = p.shared->block_open;
    p.shared->block_open = toplevel ? nullptr : open; // (A namespace body is no Block: tsc's ModuleBlock)
    if (!parse_append_statement_sequence(&p, toplevel, PARSING_BLOCK_STATEMENTS, open + 1, &ret->statements)) {
        *ret = (Block){0};
    }
    p.shared->block_open = outer_block;
    hop_whitespace(&p);

    // (Without its `}` it ends with its last token, tsc's finishNode at the next token's full start: `{ x in b` in a
    // for statement's initializer, the list aborted at the `in`)
    // (A namespace body is tsc's parseModuleBlock: a plain parseExpected, no related TS1007 pointing at the `{`)
    if (!(toplevel ? expect_char(&p, '}') : expect_closing(&p, '}', open))) unhop_trivia(&p);
    // tsc's parseBlock: `{ a } = b` is a destructuring assignment missing its parentheses, the `=` is skipped
    Parser eq = parser(&p);
    hop_whitespace(&eq);
    if (*eq.pos == '=' && eq.pos[1] != '=' && eq.pos[1] != '>') {
        parse_error(&eq, span_from_token(&eq), 2809, "Declaration or statement expected. This '=' follows a block of "
            "statements, so if you intended to write a destructuring assignment, you might need to wrap the whole "
            "assignment in parentheses.");
        eq.pos++;
        parse_success(&eq);
    } else {
        parse_fail(&eq);
    }
    return parse_success(&p);
}

// tsc's convertToJson over a JSON file's value (parseSourceFile adds its errors to the parse diagnostics): only
// double-quoted strings, numbers (maybe negated), true, false, null, objects of property assignments and arrays
// (Appended whatever the last parse error is: tsgo's validateJsonValue adds to p.diagnostics directly, no
// parseErrorAtPosition deduplication: `{"x"}` is ':' expected and TS1328 at the same position)
static void json_error_s(Parser* p, Span span, unsigned ts_code, cstr message) {
    p->shared->errors_attempted++;
    add_error_to_parser(p, (Error){ .message = message, .span = span, .ts_code = ts_code, .phase = ERROR_PHASE_PARSE });
}
#define json_error(me, span, ts_code, ...) json_error_s(me, span, ts_code, cstr_d(__VA_ARGS__))

static void json_check_value(Parser* p, Expr const* value) {
    Span span = expr_outer_span(value);
    bool plain = !expr_is_perenthisized(value);
    if (plain && value->tag == EXPR_LITERAL) {
        switch (value->literal.tag) {
            case VALUE_NULL:
            case VALUE_BOOLEAN:
            case VALUE_NUMBER:
                return;
            case VALUE_STRING:
                if (!span.size || span.buf[0] != '"') {
                    // (A template isn't a string literal)
                    if (span.size && span.buf[0] == '`') break;
                    json_error(p, span, 1327, "String literal with double quotes expected.");
                }
                return;
            default:
                break;
        }
    } else if (plain && value->tag == EXPR_UNARY_OP && value->unary_op->op == UNARY_OP_MINUS &&
        value->unary_op->expr.tag == EXPR_LITERAL && value->unary_op->expr.literal.tag == VALUE_NUMBER &&
        !expr_is_perenthisized(&value->unary_op->expr)
    ) {
        return;
    } else if (plain && value->tag == EXPR_OBJECT) {
        for_each (i, vec_object_expr_item, &value->object) {
            if (i->tag != OBJECT_ITEM_PROPERTY) {
                json_error(p, i->span, 1136, "Property assignment expected.");
                continue;
            }
            if (i->computed_key || !i->name_span.size || i->name_span.buf[0] != '"') {
                json_error(p, i->name_span, 1327, "String literal with double quotes expected.");
            }
            json_check_value(p, &i->value);
        }
        return;
    } else if (plain && value->tag == EXPR_ARRAY) {
        for_each (i, vec_expr, &value->array) json_check_value(p, i);
        return;
    }
    // (tsc's getErrorSpanForNode: a missing value, or an omitted array element, is at its node's full start, the end of
    // what comes before it: back over the whitespace)
    if (!span.size || (value->tag == EXPR_IMPLICIT_ANY && value->missing)) {
        char const* at = span_start_ptr(span);
        char const* file_start = source_file_start(p->shared->source);
        while (at > file_start && (at[-1] == ' ' || at[-1] == '\t' || at[-1] == '\n' || at[-1] == '\r')) at--;
        span = span_make_in(span, at, at);
    }
    json_error(p, span, 1328,
        "Property value can only be string literal, numeric literal, 'true', 'false', 'null', object literal or array literal.");
}

// One value of a JSON file (tsc's parseJsonText): an array, `true`/`false`/`null`, a (negated) number or a string not
// followed by a `:`; anything else is an object literal whose `{` may be missing (`contents Not read` is `{ contents,
// Not, read }`: TS1005 '{' expected, then shorthand members, each TS1136 from json_check_value())
static bool json_parse_value(Parser* parent, Expr* ret) {
    Parser p = parser(parent);
    char c = *p.pos;
    bool value = c == '[' || hop_start_if_ends_on_word_break(&p, "true") || hop_start_if_ends_on_word_break(&p, "false") ||
        hop_start_if_ends_on_word_break(&p, "null");
    if (!value && (c == '-' || c == '"' || c == '\'' || is_number_start_char(c))) {
        // (Not followed by a `:`: that's a property name)
        Parser probe = parser(&p);
        if (c == '-') {
            probe.pos++;
            hop_whitespace(&probe);
        }
        Value literal;
        bool number_next = c != '-' || is_number_start_char(*probe.pos);
        if (number_next && (*probe.pos == '"' || *probe.pos == '\'' || is_number_start_char(*probe.pos)) &&
            parse_literal(&probe, &literal)
        ) {
            value_drop(&literal);
            hop_whitespace(&probe);
            value = *probe.pos != ':';
        }
        parse_fail(&probe);
    }
    p.pos = parent->pos;
    Anchor a = anchor(&p);
    if (c == '[') {
        // (The array alone, `[1] [2]` is two values: parseArrayLiteralExpression)
        VecExpr elements;
        if (!parse_array(&p, &elements)) return parse_fail(&p);
        *ret = (Expr){ .tag = EXPR_ARRAY, .array = elements, .inner_span = span_from(a), .lazy_type.tag = LAZY_TYPE_EXPR };
        return parse_success(&p);
    }
    if (value) {
        if (!parse_expr(&p, expr_ctx_null, ret)) return parse_fail(&p);
        return parse_success(&p);
    }
    ObjectExpr object;
    parse_object_ex(&p, &object, true);
    *ret = expr_object(span_from(a), object);
    return parse_success(&p);
}

static bool parse_file_pass(SourceFile* source, ErrorCtx* errs, bool top_level_await, FirstPassStatement** await_statements, u32* await_count);

// ---- A big declaration file's parse in chunks, on several threads ----
// lib.dom.d.ts (2.4 MB) is one parse the loader waits for. Cut near fixed offsets at a line that starts a top-level
// statement after a `}` or `;`, each chunk is parsed like the file from there (the top-level statement list, ending
// where a statement ends at or past the next cut) on whichever thread takes it, and the results stand for the file's
// parse only if every chunk ended exactly at the next cut and none has an error: then each chunk's parse is the file's
// from that statement on (chunk 0 starts where the file does; a chunk that ends where the next starts leaves the
// parser where the next one starts, the state carried between top-level statements being what parse_chunk_run() sets
// up), and their statements and logged effects, in chunk order, are the file's. Otherwise the file is parsed the
// usual way: a wrong cut (in a comment, a template, a nested body) costs one more parse, never a different one.
// TTTT_PARSE_CHUNK_BYTES=N: chunks of about N bytes in every declaration file over N (0: never), whether or not other
// threads help (the oracles' knob); TTTT_PARSE_CHUNK_STATS=1 counts them and names each fallback

enum {
    CHUNK_BYTES_DEFAULT = 64 * 1024,
    CHUNK_FILE_MIN_DEFAULT = 512 * 1024, // (lib.dom, lib.webworker, typescript.d.ts, vscode.d.ts...)
};

struct ParseChunkJob {
    SourceFile* source;
    CompilerOptions const* options;
    ParseChunk* chunks;
    u32 count;
    _Atomic u32 next; // The next chunk to take
    _Atomic bool failed; // A chunk's parse can't stand for the file's: the rest needn't be parsed
    pthread_mutex_t lock;
    pthread_cond_t finished;
    u32 active; // Helpers in parse_chunk_job_help()
};

static thread_local ParseHelpers const* parse_helpers;

void parse_set_helpers(ParseHelpers const* helpers) {
    parse_helpers = helpers;
}

static _Atomic i64 chunk_stat_files, chunk_stat_chunks, chunk_stat_helped, chunk_stat_fallbacks;

// TTTT_PARSE_CHUNK_STATS=/path appends there instead of stderr (sweeps that read tttt's output)
static FILE* chunk_stats_file;

static void chunk_stats_print(void) {
    fprintf(chunk_stats_file ? chunk_stats_file : stderr, "parse chunks: %lld files in %lld chunks (%lld parsed by helpers), %lld fallbacks\n",
        (long long)chunk_stat_files, (long long)chunk_stat_chunks, (long long)chunk_stat_helped, (long long)chunk_stat_fallbacks);
    if (chunk_stats_file) fclose(chunk_stats_file);
}

static struct {
    pthread_once_t once;
    i64 chunk_bytes, file_min;
    bool forced, stats;
} chunk_config = { .once = PTHREAD_ONCE_INIT };

static void chunk_config_read(void) {
    char const* bytes = getenv("TTTT_PARSE_CHUNK_BYTES");
    chunk_config.forced = bytes && *bytes;
    chunk_config.chunk_bytes = chunk_config.forced ? atoll(bytes) : CHUNK_BYTES_DEFAULT;
    chunk_config.file_min = chunk_config.forced ? chunk_config.chunk_bytes : CHUNK_FILE_MIN_DEFAULT;
    char const* stats = getenv("TTTT_PARSE_CHUNK_STATS");
    chunk_config.stats = stats && *stats && strcmp(stats, "0");
    if (chunk_config.stats && stats[0] == '/') chunk_stats_file = fopen(stats, "a");
    if (chunk_config.stats) atexit(chunk_stats_print);
}

// A line that starts like a top-level declaration (a line of prose in a comment mostly doesn't)
static bool starts_declaration(char const* at, char const* end) {
    static char const* const words[] = {
        "interface ", "declare ", "type ", "export ", "import ", "namespace ", "module ", "function ", "class ",
        "enum ", "const ", "let ", "var ", "abstract ",
    };
    for (size_t i = 0; i < c_arraylen(words); i++) {
        size_t n = strlen(words[i]);
        if ((size_t)(end - at) > n && !memcmp(at, words[i], n)) return true;
    }
    return false;
}

// Where chunks start after the first: a line starting like a declaration after a `}` or `;` (whitespace between),
// about chunk_bytes apart. The cut is right after the `}` or `;`
static u32 chunk_cuts(char const* text, isize size, i64 chunk_bytes, char const** cuts, u32 max) {
    u32 count = 0;
    char const* end = text + size;
    for (char const* at = text + chunk_bytes; at < end && count < max;) {
        char const* line = memchr(at, '\n', (size_t)(end - at));
        if (!line || line + 1 >= end) break;
        at = line + 1;
        if (!starts_declaration(at, end)) continue;
        char const* before = line;
        while (before > text && (*before == '\n' || *before == '\r' || *before == ' ' || *before == '\t')) before--;
        if (*before != '}' && *before != ';') continue;
        char const* cut = before + 1;
        if (count && cut <= cuts[count - 1]) continue;
        cuts[count++] = cut;
        at = cut + chunk_bytes;
    }
    return count;
}

static void parse_chunk_run(SourceFile* source, ParseChunk* chunk) {
    ParserShared shared = {
        .source = source,
        .text = !source->is_declaration && !source->is_stdlib && (compiler_options()->declaration || compiler_options()->composite) ?
            source_file_start(source) : nullptr,
        .fail_cache_start = chunk->start,
        .errors = vec_error_init(),
        .in_ambient = source->is_declaration,
        .chunk = chunk,
        .chunk_stop = chunk->stop,
    };
    Parser p = { .shared = &shared, .parent = nullptr, .pos = chunk->start };
    if (chunk->first) {
        p.pos = file_start_excluding_bom(p.pos);
        if (p.pos[0] == '#' && p.pos[1] == '!') {
            while (*p.pos && *p.pos != '\n') p.pos++;
        }
        hop_conflict_markers(&p);
        parse_source_directives(&p, &chunk->statements);
    }
    hop_whitespace(&p);
    parse_append_statement_sequence(&p, true, PARSING_SOURCE_ELEMENTS, chunk->start, &chunk->statements);
    chunk->failure = shared.errors.size || shared.jsdoc_errors.size ? "an error" :
        shared.unterminated_comment ? "an unterminated comment" :
        shared.binary_marker ? "a binary marker" :
        chunk->stop && shared.chunk_reached != chunk->stop ? "a statement across the cut" :
        nullptr;
    chunk->saw_import_meta = shared.saw_import_meta;
    chunk->saw_commonjs = shared.saw_commonjs;
    chunk->saw_jsx = shared.saw_jsx;
    parser_shared_drop(&shared);
}

// The next chunk not taken, or null
static ParseChunk* chunk_take(ParseChunkJob* job) {
    if (atomic_load_explicit(&job->failed, memory_order_relaxed)) return nullptr;
    u32 at = atomic_fetch_add_explicit(&job->next, 1, memory_order_relaxed);
    return at < job->count ? &job->chunks[at] : nullptr;
}

bool parse_chunk_job_join(ParseChunkJob* job) {
    if (atomic_load_explicit(&job->next, memory_order_relaxed) >= job->count) return false;
    pthread_mutex_lock(&job->lock);
    job->active++;
    pthread_mutex_unlock(&job->lock);
    return true;
}

void parse_chunk_job_help(ParseChunkJob* job) {
    CompilerOptions const* outer = compiler_options_private;
    compiler_options_private = job->options;
    for (ParseChunk* chunk; (chunk = chunk_take(job));) {
        chunk->helped = true;
        ast_arena_begin(&chunk->arena);
        lazy_slots_new_block();
        parse_chunk_run(job->source, chunk);
        ast_arena_end();
        if (chunk->failure) atomic_store_explicit(&job->failed, true, memory_order_relaxed);
    }
    compiler_options_private = outer;
    pthread_mutex_lock(&job->lock);
    job->active--;
    pthread_cond_broadcast(&job->finished);
    pthread_mutex_unlock(&job->lock);
}

static void chunk_drop_effects(ParseChunk* chunk) {
    for (u32 i = 0; i < chunk->effect_count; i++) {
        ModuleRef* ref = chunk->effects[i].ref;
        if (!ref) continue;
        cstr_drop(&ref->path);
        module_ref_attrs_free(ref->attrs);
        free(ref);
    }
    free(chunk->effects);
}

// The file's parse, from chunks: false (nothing done to the file) if it isn't one to chunk or a chunk's parse can't
// stand for the file's. In the file's arena (ast_arena_begin())
static bool parse_file_chunked(SourceFile* source) {
    pthread_once(&chunk_config.once, chunk_config_read);
    if (!source->is_declaration || source->is_js || source->is_json || chunk_config.chunk_bytes <= 0) return false;
    isize size = cstr_size(&source->contents);
    if (size < chunk_config.file_min) return false;
    ParseHelpers const* helpers = parse_helpers;
    if (!helpers && !chunk_config.forced) return false;
    enum { MAX_CUTS = 4096 };
    char const** cuts = malloc(sizeof(char const*) * MAX_CUTS);
    panic_if(!cuts, "out of memory");
    char const* text = source_file_start(source);
    u32 cut_count = chunk_cuts(text, size, chunk_config.chunk_bytes, cuts, MAX_CUTS);
    if (!cut_count) {
        free(cuts);
        return false;
    }
    check(!vec_statement_size(&source->ast.statements));
    ParseChunkJob job = {
        .source = source, .options = compiler_options(), .count = cut_count + 1,
        .chunks = c_new_n(ParseChunk, cut_count + 1),
    };
    for (u32 i = 0; i <= cut_count; i++) {
        job.chunks[i] = (ParseChunk){
            .start = i ? cuts[i - 1] : text, .stop = i < cut_count ? cuts[i] : nullptr, .first = !i,
            .statements = vec_statement_init(),
        };
    }
    free(cuts);
    pthread_mutex_init(&job.lock, nullptr);
    pthread_cond_init(&job.finished, nullptr);
    if (helpers) helpers->offer(helpers->ctx, &job);
    // (This thread's chunks go to the file's arena)
    for (ParseChunk* chunk; (chunk = chunk_take(&job));) {
        parse_chunk_run(source, chunk);
        if (chunk->failure) atomic_store_explicit(&job.failed, true, memory_order_relaxed);
    }
    if (helpers) helpers->retract(helpers->ctx, &job);
    pthread_mutex_lock(&job.lock);
    while (job.active) pthread_cond_wait(&job.finished, &job.lock);
    pthread_mutex_unlock(&job.lock);
    pthread_mutex_destroy(&job.lock);
    pthread_cond_destroy(&job.finished);

    char const* failure = nullptr;
    u32 failed_at = 0;
    for (u32 i = 0; i < job.count && !failure; i++) {
        failure = job.chunks[i].failure;
        failed_at = i;
    }
    i64 helped = 0;
    for (u32 i = 0; i < job.count; i++) {
        // (The helpers' memory is the file's either way: a failure's statements are dropped from this thread)
        if (job.chunks[i].helped) {
            ast_arena_absorb(&source->arena, &job.chunks[i].arena);
            helped++;
        }
    }
    if (chunk_config.stats) {
        atomic_fetch_add(&chunk_stat_files, 1);
        atomic_fetch_add(&chunk_stat_chunks, job.count);
        atomic_fetch_add(&chunk_stat_helped, helped);
        if (failure) {
            atomic_fetch_add(&chunk_stat_fallbacks, 1);
            int line = 1;
            for (char const* c = text; c < job.chunks[failed_at].start; c++) line += *c == '\n';
            fprintf(chunk_stats_file ? chunk_stats_file : stderr, "parse chunks: %s: chunk %u (line %d) has %s: parsed whole\n",
                cstr_str(&source->canonical_path), failed_at, line, failure);
        }
    }
    if (failure) {
        for (u32 i = 0; i < job.count; i++) {
            vec_statement_drop(&job.chunks[i].statements);
            chunk_drop_effects(&job.chunks[i]);
        }
        free(job.chunks);
        return false;
    }
    // The file's statements, then what the parse did to the file, in order
    isize total = 0;
    for (u32 i = 0; i < job.count; i++) total += vec_statement_size(&job.chunks[i].statements);
    source->ast.statements = job.chunks[0].statements;
    vec_statement_reserve(&source->ast.statements, total);
    for (u32 i = 1; i < job.count; i++) {
        for_each (statement, vec_statement, &job.chunks[i].statements) vec_statement_push(&source->ast.statements, *statement);
        // (Moved out: the buffer alone goes)
        job.chunks[i].statements.size = 0;
        vec_statement_drop(&job.chunks[i].statements);
    }
    shrink_list(vec_statement, &source->ast.statements);
    bool saw_import_meta = false, saw_commonjs = false, saw_jsx = false;
    for (u32 i = 0; i < job.count; i++) {
        ParseChunk* chunk = &job.chunks[i];
        saw_import_meta |= chunk->saw_import_meta;
        saw_commonjs |= chunk->saw_commonjs;
        saw_jsx |= chunk->saw_jsx;
        for (u32 j = 0; j < chunk->effect_count; j++) {
            ChunkEffect const* effect = &chunk->effects[j];
            switch ((ChunkEffectKind)effect->kind) {
                case CHUNK_EFFECT_MODULE_REF: source_file_push_module_ref(source, effect->ref); break;
                case CHUNK_EFFECT_COMMENT_DIRECTIVE:
                    source_file_add_comment_directive(source, effect->start, effect->end, effect->flag);
                    break;
                case CHUNK_EFFECT_DEPRECATED_JSDOC: source_file_add_deprecated_jsdoc(source, effect->end); break;
                case CHUNK_EFFECT_ESCAPED_NAMES: names_register_source(source); break;
                case CHUNK_EFFECT_DEFERRED_BODIES: source->has_deferred_bodies = true; break;
            }
        }
        free(chunk->effects);
    }
    free(job.chunks);
    // (parse_file_pass()'s end, for a file without errors)
    source->has_import_meta = saw_import_meta;
    source->has_commonjs = saw_commonjs;
    source->has_jsx = saw_jsx;
    source->has_parse_errors = false;
    free(source->parse_error_starts);
    source->parse_error_starts = nullptr;
    source->parse_error_count = 0;
    return true;
}

void parse_file(SourceFile* source, ErrorCtx* errs) {
    FirstPassStatement* await_statements = nullptr;
    u32 await_count = 0;
    ast_arena_begin(&source->arena);
    lazy_slots_new_block();
    if (!parse_file_chunked(source) && !parse_file_pass(source, errs, false, &await_statements, &await_count)) {
        parse_file_pass(source, errs, true, &await_statements, &await_count);
    }
    ast_arena_end();
    free(await_statements);
}

// Plan 78: parses a deferred body (the file's AST arena begun) into its DeferredBody. False, with nothing reported, if
// the parse can be told from the eager one: a syntax error (or one that isn't a grammar error), or an end other than the
// skipper's. Grammar errors go to errs, unless the file has parse errors (parse_file())
bool parse_deferred_body(SourceFile* source, FunctionImpl* func, ErrorCtx* errs) {
    DeferredBody* deferred = func->body_.deferred;
    char const* text = source_file_start(source);
    ParserShared shared = {
        .source = source,
        .text = !source->is_declaration && !source->is_stdlib && (compiler_options()->declaration || compiler_options()->composite) ?
            text : nullptr,
        .fail_cache_start = text,
        .errors = vec_error_init(),
        .materializing = true,
        .in_async = func->is_async,
        .in_generator = func->is_generator,
        .function_depth = 1,
    };
    Parser p = { .shared = &shared, .parent = nullptr, .pos = text + deferred->open };
    deferred->block = (Block){0};
    bool same = parse_block(&p, false, &deferred->block) && p.pos == text + deferred->end && !shared.unterminated_comment;
    // (JSX the skipper didn't see: moduleDetection would have seen it)
    same = same && (!shared.saw_jsx || source->has_jsx);
    for_each (i, vec_error, &shared.errors) {
        if (!(i->grammar || i->js_syntax || is_js_syntax_code(i->ts_code))) same = false;
    }
    same = same && !shared.jsdoc_errors.size && !shared.binary_marker;
    for_each (i, vec_error, &shared.errors) {
        if (!same || (source->has_parse_errors && i->grammar)) {
            error_drop(i);
        } else {
            error_ctx_append_error(errs, *i);
        }
        *i = (Error){0};
    }
    parser_shared_drop(&shared);
    return same;
}

// False if the file has to be parsed again with top_level_await (nothing of this pass is kept then, but the
// statements to parse as await contexts)
static bool parse_file_pass(SourceFile* source, ErrorCtx* errs, bool top_level_await, FirstPassStatement** await_statements, u32* await_count) {
    u32 module_refs_before = source->module_ref_count;
    ParserShared shared = {
        .source = source,
        // (Only declaration emit reads TypeNode.pos: its maps and the comments around reprinted nodes)
        .text = !source->is_declaration && !source->is_stdlib && (compiler_options()->declaration || compiler_options()->composite) ?
            source_file_start(source) : nullptr,
        .fail_cache_start = source_file_start(source),
        .errors = vec_error_init(),
        .in_ambient = source->is_declaration,
        .top_level_await = top_level_await,
        .await_statements = top_level_await ? *await_statements : nullptr,
        .first_pass_count = top_level_await ? *await_count : 0,
        .record_comments = !source->is_declaration && !source->is_stdlib &&
            (compiler_options()->declaration || compiler_options()->composite),
    };

    Parser p = {
        .shared = &shared,
        .parent = nullptr,
        .pos = source_file_start(source),
        .error_count = 0,
    };

    p.pos = file_start_excluding_bom(p.pos);
    if (p.pos[0] == '#' && p.pos[1] == '!') {
        // A shebang line is trivia (tsc's scanShebangTrivia)
        while (*p.pos && *p.pos != '\n') p.pos++;
    }
    hop_conflict_markers(&p);
    if (source->is_json) {
        // A JSON file is the value of one expression (tsc's parseJsonText), which is what the module is
        hop_whitespace(&p);
        Anchor a = anchor(&p);
        Expr value;
        if (*p.pos && json_parse_value(&p, &value)) {
            hop_whitespace(&p);
            if (*p.pos) {
                // More than one value: TS1012 at the second, and the file is an array of them all (tsc's parseJsonText
                // error recovery)
                parse_error(&p, span_from_token(&p), 1012, "Unexpected token.");
                VecExpr values = vec_expr_init();
                list_push(vec_expr, &values, value);
                char const* end = span_end_ptr(expr_outer_span(&value));
                while (*p.pos) {
                    Expr more;
                    char const* before = p.pos;
                    if (!json_parse_value(&p, &more)) break;
                    if (p.pos == before) {
                        expr_drop(&more);
                        hop_token(&p);
                        hop_whitespace(&p);
                        continue;
                    }
                    end = span_end_ptr(expr_outer_span(&more));
                    list_push(vec_expr, &values, more);
                    hop_whitespace(&p);
                }
                value = (Expr){ .tag = EXPR_ARRAY, .array = values, .inner_span = span_make(source, a.start, end), .lazy_type.tag = LAZY_TYPE_EXPR };
            }
            json_check_value(&p, &value);
            Statement statement = { .tag = STATEMENT_EXPORT_DEFAULT_EXPR, .expr = value, .export_equals = true };
            statement.span = span_from(a);
            vec_statement_push(&source->ast.statements, statement);
        } else if (at_eof(&p, p.pos)) {
            // An empty file is `{}` (tsc's getTypeOfVariableOrParameterOrPropertyWorker: emptyObjectType), an empty
            // span that the baselines don't show
            Statement statement = {
                .tag = STATEMENT_EXPORT_DEFAULT_EXPR, .expr = expr_object(span_from(a), vec_object_expr_item_init()),
                .export_equals = true,
            };
            statement.span = span_from(a);
            vec_statement_push(&source->ast.statements, statement);
        }
    } else {
        parse_source_directives(&p, &source->ast.statements);
        hop_whitespace(&p);
        parse_append_statement_sequence(&p, true, PARSING_SOURCE_ELEMENTS, source_file_start(source), &source->ast.statements);
    }
    if (shared.unterminated_comment) {
        const char* end = source_file_start(source) + cstr_size(&source->contents);
        parse_error(&p, span_make(source, end, end), 1010, "'*/' expected.");
    }

    // tsc's reparseTopLevelAwait, for the whole file rather than the statements with an `await`
    if (!top_level_await && shared.await_statement_count && !source->is_declaration && !source->is_json &&
        (statements_module_indicator(&source->ast.statements, &(Span){0}) ||
            source_file_module_by_detection(source, compiler_options(), shared.saw_import_meta, shared.saw_jsx))
    ) {
        *await_statements = shared.await_statements;
        *await_count = shared.first_pass_count;
        shared.await_statements = nullptr;
        vec_statement_drop(&source->ast.statements);
        source->ast.statements = vec_statement_init();
        for (u32 i = module_refs_before; i < source->module_ref_count; i++) {
            cstr_drop(&source->module_refs[i]->path);
            module_ref_attrs_free(source->module_refs[i]->attrs);
            free(source->module_refs[i]);
        }
        source->module_ref_count = module_refs_before;
        parser_shared_drop(&shared);
        return false;
    }

    // Grammar errors tsc's checker reports (checkGrammarModifiers, checkGrammarIndexSignatureParameters,
    // checkGrammarForInvalidQuestionMark...) that the parser is in a better place to report. They don't count as
    // parse errors, and like all grammar errors they aren't reported in a file with parse errors (hasParseDiagnostics)
    source->has_import_meta = shared.saw_import_meta;
    source->has_commonjs = shared.saw_commonjs;
    source->has_jsx = shared.saw_jsx;
    source->has_parse_errors = false;
    free(source->parse_error_starts);
    source->parse_error_starts = nullptr;
    source->parse_error_count = 0;
    for_each (i, vec_error, &shared.errors) {
        if (i->grammar || i->js_syntax || is_js_syntax_code(i->ts_code) || i->phase != ERROR_PHASE_PARSE) continue;
        source->has_parse_errors = true;
        source->parse_error_starts = realloc(source->parse_error_starts, sizeof(char const*) * (source->parse_error_count + 1));
        source->parse_error_starts[source->parse_error_count++] = span_start_ptr(i->span);
    }
    // (A JavaScript `@typedef` without a name too: tsgo's reparser reports it among the file's parse diagnostics,
    // checkNonIdentifierName, not its jsdocDiagnostics, so the file's grammar errors are dropped like for any other)
    for_each (i, vec_error, &shared.jsdoc_errors) {
        if (!i->jsdoc && i->phase == ERROR_PHASE_PARSE) source->has_parse_errors = true;
    }
    for_each (i, vec_error, &shared.errors) {
        if (source->has_parse_errors && i->grammar) {
            error_drop(i);
        } else {
            error_ctx_append_error(errs, *i);
        }
        *i = (Error){0};
    }
    // Every attempt that parsed a comment's host reported its errors: the same error at the same place once
    for (isize i = 0; i < shared.jsdoc_errors.size; i++) {
        Error* error = &shared.jsdoc_errors.data[i];
        bool seen = false;
        for (isize j = 0; j < i && !seen; j++) {
            Error const* other = &shared.jsdoc_errors.data[j];
            seen = other->ts_code == error->ts_code && other->span.buf == error->span.buf &&
                other->span.size == error->span.size;
        }
        if (seen) continue;
        if (!span_valid(error->span)) {
            error_ctx_global_error_s(errs, error->ts_code, error->message);
        } else {
            error_ctx_append_error(errs, *error);
        }
        *error = (Error){0};
    }

    if (top_level_await) shared.await_statements = nullptr; // (parse_file()'s)
    parser_shared_drop(&shared);
    return true;
}
