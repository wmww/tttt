# Plan 27: parser fidelity (recovery, permissive grammar, lexical diagnostics)

Written 2026-09-26 (planning session, todo entry 5).

## Status
**Essentially done (2026-09-29, todo entry 8, parser session: four Opus sub-agents by syntaxstats cluster, recovery /
permissive / missing checks / commit points)**: 197 -> 34 tests whose parser diagnostics differ, spurious TS1xxx 196 ->
47 tests, suite 11274 -> 11472 (0 lost), corpora unchanged, +1-2.5% instructions (measurements.md; partly clawed
back). Built: list contexts for import/export specifiers, object and array binding patterns; `Expr.missing` and
`token_full_start()`/`unhop_trivia()` (missing nodes at tsc's full start); a `TYPE_PREDICATE` node (misplaced `this
is T`/`asserts x`), a `TYPE_JSDOC` wrapper (`?T`, `T!`... in TS files); tsc's top-level-await reparse (a whole-file
second pass); escaped keywords (TS1260); committed function types (isUnambiguouslyStartOfFunctionType) and return
types; `grammar_error()` for per-error grammar marking; many commit points (typeof entity names, try at
catch/finally, `import x`, `global x`, accessors after their name). **What is left is the "Leftovers" list at the end;
no further parser session is planned**: take its items one at a time from triage/checker sessions. The stage
sections below are history.
**Stage 3 done (2026-09-28, todo entry 6, one session + one sub-agent for the regex port)**: 402 -> 263 tests whose
parser diagnostics differ (lexical clusters 92 -> 11; M 78 -> 31 with a sweep of `other:M`), suite 9729 -> 9833,
corpora unchanged, parse time on the tsc corpus 104 -> 109 ms (after clawing back a 116 ms regression, see
measurements.md). Escaped identifier names resolve by value now (`bugs/escaped_identifiers.md` has the leftovers); what's left of
stage 3 is NUL characters in source (hacks.md "Parser"); the stage 3 section says what was built. Tagged templates aren't
checked as calls (triage.md), which is what several template tests still need.
**Stage 2 mostly done (2026-09-27, todo entry 8, one session with four sub-agents)**: 544 -> 402 tests whose parser
diagnostics differ (TS: A 175 -> 83, B 36 -> 25, C 58 -> 40, M 87 -> 78); permissive clusters 172 -> 44 (modifiers
60 -> 21, yield/await 22 -> 7, heritage 22 -> 0, index signatures 24 -> 4, `with` 17 -> 0, private names 17 -> 3,
decorators 10 -> 9); suite 9210 -> 9363. What's left of each is in the stage 2 section. Stages 3 (lexical, 92 tests)
and 4 (positions) are unscheduled; `other:M` (70) has small parser checks worth a sweep (list below).
**Stage 1 mostly done (2026-09-27, one session, 11 commits)**: 687 -> 583 tests whose parser diagnostics differ
(`recovery:*` 89 -> 31: list-context 56 -> 25, object literal 18 -> 6, TS1011 15 -> 0; `other:B` 23 -> 8), suite 8345 ->
8408, parse time within noise, corpora unchanged. The framework is described in documentation.md "Error recovery".
Built: the mask and `list_abort_or_skip()`, the one-error-per-position rule for every error, TS1127 at invalid
characters, lists for statements (all three kinds), class/type/enum/object literal members, switch clauses,
parameters, arguments, array literals (and the VariableDeclarations bit); commit points for arrows (tri-state),
parameter lists, `.name`, `[]`, object literal members, parenthesized expressions, missing left operands;
parseErrorForMissingSemicolonAfter; parser-reported grammar errors dropped in files with parse errors.
**Left of stage 1** (a second session if the numbers still justify it; `other:D`/`other:E` barely moved, 61 and 81):
- Lists not converted yet: import/export specifiers, heritage clauses (overlaps stage 2), type arguments/parameters,
  tuple element types, object/array binding patterns (`parse_binding_content()` breaks out of both at the first bad
  element), JSX attributes, import attributes; the variable declarator loop is only a mask bit, not tsc's list.
- Commit points: function types in type position (tsc's isStartOfFunctionTypeOrConstructorType /
  isUnambiguouslyStartOfFunctionType; today the type parser's `(` still backtracks freely), a committed arrow's
  return type (`(a): => {}`), `parse_class_item_inner()`'s own fallbacks (it still fails where tsc commits, e.g. a
  `get`/`set` or `*` class member without a name: MemberFunctionDeclaration5_es6, `async * get x()`).
- Step 6 (missing nodes in the checker): only names so far. Missing expressions are `expr_implicit_any()`, missing
  types the default; nothing checker-side had to change yet.

## Verdict: no rewrite, no scanner. One recovery framework, then three kinds of patching.
The question was whether tsc's list recovery fits the backtracking no-tokenizer parser or needs a tsc-shaped scanner.
It fits. What decides it is what the failing tests actually need (below): only about a third is recovery at all, and
the recovery part needs tsc's *decision rules* (which list am I in, what terminates it, when to skip a token, when a
construct is committed), none of which need tokens. The parser already has the pieces: `hop_token()` skips one token,
`expect_*()` report "X expected" and carry on, `parse_error_once()` is tsc's one-error-per-position rule, the fail
cache makes speculation cheap. What's missing is that every list is written ad hoc, and many constructs `parse_fail`
after the point where tsc has committed and would report and continue. That is a refactor of how lists and commit
points are written, done incrementally with the suite green at every step.

A scanner would not help the other two thirds: *permissive grammar* (tsc parses `with`, `class C extends A, B`,
`[...x]: T` index signatures, modifiers anywhere, and complains later) is a parser policy question, and the *lexical*
diagnostics (escapes, numeric literals, unterminated templates, unicode identifiers) are per-literal checks that the
existing char-level helpers can make. The perf argument doesn't carry a rewrite either: the parser is ~15% of the
tsc corpus' instructions and 113 of react's 310 ms (dependency `.d.ts` files, which are never erroneous); where that
time goes is the perf session's question (todo entry 9: profile parsing on react), and the answer is more likely a
hot helper (`hop_whitespace`, keyword checks, fail-cache hits) than tokens.

## The numbers (`./syntaxstats.py`, written for this plan; run `./failstats.py` first)
687 tests' parser diagnostics differ from tsc's (only codes tsc's parser.ts/scanner.ts report are counted, so this is a
bit wider than failstats' "613 with a spurious TS1xxx"). 48 are JS (plan 26 stage 4). The 639 TS tests:

| kind | tests | meaning |
|---|---|---|
| A | 183 | tsc's parser reports nothing, tttt rejects the file |
| B | 111 | tttt's first parse error comes before tsc's first |
| C | 103 | same first position, different code |
| D | 67 | tsc's first error comes first (tttt accepts what tsc rejects, then goes wrong) |
| E | 78 | first errors agree, recovery diverges later |
| M | 97 | tsc reports parse errors, tttt none |

By mechanism (a heuristic on the first differing error's code and source line; `./syntaxstats.py --list <cluster>`):

| cluster | tests | stage | examples |
|---|---|---|---|
| permissive: modifiers anywhere | 68 | 2 | `export public import a = x.c`, `declare export = x`, `{ public foo: 1 }`, `constructor(public ...rest)`, `public @dec m()` |
| permissive: yield/await as identifiers | 28 | 2 | `yield foo;` outside a generator, `a = await => await` |
| permissive: index signatures | 26 | 2 | done (2026-09-27, `parse_index_signature()`); left in the cluster: enum `[e]` types, class `[e] = x\n[e2]` ASI, `private [x]` in object literals |
| permissive: heritage clauses | 22 | 2 | `class C extends A, B`, `extends A extends B`, `interface I implements J`, `extends A?.B` |
| permissive: `with` | 17 | 2 | done: `STATEMENT_WITH` (all 17 pass their errors) |
| permissive: private names | 17 | 2 | `#x` in object literals, enums, variable names (TS18016/TS18029) |
| permissive: decorators | 9 | 2 | `var F = @dec () => {}`, `@dec` before `import =` |
| lexical: literals | 92 | 3 | unterminated templates (TS1160), `\u{...}` escapes (TS1198/1199/1125 positions), numeric separators (TS6188), `1.toString()` (TS1351), conflict markers (TS1185), binary files (TS1490), regex validation (TS1499...) |
| lexical: identifiers | 37 | 3 | `a` escapes in identifiers (and keywords: TS1260), non-ASCII identifiers (`is_identifier_char` is ASCII only), UTF-16 files (not decoded) |
| recovery: list context | 56 | 1 | TS1068 vs TS1434/TS1128 in class bodies (the class body gives up at the first bad member), `module {`, `declare module \`M\`` |
| recovery: object literal | 18 | 1 | `{ a; b }`, `{ *() {} }`, `{ [e] }`, `{ "" }` |
| recovery: TS1011 | 15 | 1 | `new number[]` (tsc: element access with a missing argument) |
| other A/B/C/D/E | 151 | 1, 4 | mostly error *positions*: backtracking reports at the start of what failed, tsc at the token where it committed and failed (`var a = () { }`: TS1005 `'=>' expected` at `{`, not TS1109 at `)`; `foo.` at EOF; `(...) => 1`) |
| other M | 83 | 1, 2 | tsc's parser checks tttt lacks: TS1359 reserved words as names, TS2754 `super<T>()`, TS1433/TS1146, `x++ = 4`, `class void {}` |

How much it's worth: 224 of the 639 already match tsc on every *non-parse* diagnostic, so they are candidates to pass
errors once their parse errors match; the others also have checker diffs, often downstream of the recovered AST.
Budget: stage 2 ~150 tests' errors (cheap: most are "parse, then report from where tsc does"), stage 3 ~100, stages 1
and 4 ~150 between them but they are what makes the rest of the recovery work local instead of whack-a-mole. Types and
symbols follow in the tests whose AST now matches tsc's.

## Stage 1: the recovery framework (lists and commit points)
tsc's model (parser.ts `parseList`, `parseDelimitedList`, `isListElement`, `isListTerminator`,
`abortParsingListOrMoveToNextToken`, `isInSomeParsingContext`, `parsingContextErrors`, `ParsingContext` at the end of
the file):
- `ParsingContext` is a **bitmask of every list being parsed** (source elements, block statements, class members,
  type members, enum members, object literal members, arguments, parameters, type parameters/arguments, heritage
  clauses, import/export specifiers, array literal, binding patterns, switch clauses...).
- A list loops until its terminator. An element that starts like an element is parsed. Anything else: report the
  list's message (`parsingContextErrors`: TS1068 for class members, TS1128 for statements, TS1131 for type members,
  TS1132 enum members, TS1136 object literal, TS1135 arguments, TS1138 parameters...) and then **if the current token
  is an element or terminator of *any* enclosing list, stop this list** (the enclosing one takes over), **else skip
  one token** and continue.
- Delimited lists do the same with commas: a missing comma is `',' expected` and parsing continues if the next thing
  starts an element.
- Once a production is chosen, it never fails: missing pieces are `parseExpected` errors plus a missing node. Errors
  at the same position as the previous one are dropped (parseErrorAtPosition).

In tttt:
1. Add `ParsingContext` to `ParserShared` (a `u32` mask, saved/restored around each list; it's per call stack like
   `in_async`, so the fail cache is unaffected: a position is always inside the same lists... **verify this**: two
   speculative attempts at the same position could be in different list contexts. If the fail cache is keyed by
   (position, flag) only, a failure recorded under one mask could be replayed under another; include the mask in the
   key or show it can't matter).
2. `parse_list(p, ctx, element_fn, ...)` / `parse_delimited_list(...)` with `list_is_element(ctx, p)` /
   `list_is_terminator(ctx, p)` ported from tsc as char-level predicates (`starts_with_followed_by_word_break`,
   `is_identifier_start_char`, ...; tsc's `isStartOfStatement`, `isStartOfExpression`, `isStartOfType`,
   `isClassMemberStart`, `isStartOfParameter` are all cheap lookahead and port as such). The abort rule needs "is the
   current token an element or terminator of any list in the mask", a loop over the set bits.
3. Make `parse_error()` drop an error at the start of the previous one by default (today only `parse_error_once()` does;
   check the JSX code still gets what it needs, and that errors dropped by a failed speculative parse don't leave a
   stale "previous error").
4. Convert the lists, highest count first, re-running `./syntaxstats.py` and `./fasttest.sh --summary` after each:
   class members (`parse_class_decl`'s loop: today it returns at the first bad member and the statement level
   reports the rest), statements (`parse_append_statement_sequence`: add the abort rule and TS1005 `'export'
   expected` for a leading `default`), object literal members, interface/type literal members, enum members,
   arguments, parameters, array literals, import/export specifiers, heritage clauses, type arguments/parameters,
   switch clauses, binding patterns.
5. Commit points. For each construct, find where tsc stops speculating (keyword seen, `isStartOf*` true, tri-state
   lookahead said True) and make the tttt function succeed from there on, with `expect_*` for the missing pieces.
   The big one is tsc's `isParenthesizedArrowFunctionExpression` / `isUnParenthesizedAsyncArrowFunction` tri-state
   (`()` followed by `=>`, `:` or `{` is an arrow function, definitely), which is why `var a = () { }` and `(...) => 1`
   disagree today. Port the tri-state as a predicate in front of `parse_arrow_function()` rather than trying the arrow
   and backtracking on any failure. Same for `new X[]` (tsc: element access with missing argument, TS1011), object
   literal members (`{ a; }`: shorthand then `',' expected`).
6. Missing nodes: an expression is `expr_implicit_any()` today; check there's an equivalent for types, names and
   statements, and that the binder/checker are quiet about them like tsc is about missing identifiers (tsc's checker
   never reports on a node with an empty/missing name).

Done when: `recovery:*` and most of `other:B/D/E` are gone from `./syntaxstats.py`; no test lost in `./fasttest.sh
--summary`; `./realworld.py` and the three corpora still have zero parse errors; react parse time within noise
(`./bench.sh`).

## Stage 2: permissive grammar
tsc's parser accepts more than the language and reports from the checker (`checkGrammar*`) or from a parser path that
doesn't abort. For each cluster, parse what tsc parses into the AST (a flag or list where the AST has no room), and
report tsc's code at tsc's position. Most of these reports are grammar checks in checker.ts; do them in the checker
walk if the parser has no position to do them from (and remember that tsc suppresses most grammar errors in a file that
has parse errors: `hasParseDiagnostics`, see how `SourceFile.has_parse_errors` is used today).
- Modifiers: done. `modifier_list_check_grammar()` is tsc's checkGrammarModifiers (with findFirstIllegalModifier
  and checkGrammarAsyncModifier) keyed by `ModifiedKind` and parent; the parser tracks the ambient context and the
  module block kind; modifiers are parsed in front of object literal members (TS1042/TS1184), parameters (`public
  ...rest`, `public {x}`), import/export statements (TS1120/1191/1193/1044), `abstract`/`default` anywhere tsc takes
  them. Left (the cluster's 21 are mostly misclassified import types and tagged templates): `public {}` in a class
  (TS1146), `export @dec default class`, decorators on import statements (TS1206; the statement has nowhere to keep the
  decorators for the baselines' walk), `declare module \`M\`` (TS1443), variance annotations (`in`/`out` on type
  parameters are parsed and dropped: TS1273/TS1274/TS2636/TS2637 and the variance check itself, varianceAnnotations),
  TS1314 for `export as namespace` in a script file (scope_add_port() isn't reached there).
- `yield`/`await`: mostly done (`yield x` outside generators is TS1163, `await` in static blocks TS18037, `yield`/`await`
  as missing expressions/labels where they're keywords, assignments need a left-hand side). Left:
  asyncArrowFunction9_* (`async (a = await => await)`: tsc's async arrow reparse), topLevelAwaitErrors.1 (reparsed
  top-level await calls), templateStringWithEmbeddedYieldKeyword (`function* gen {`: parse_function_keyword_function's
  TS0 "Expected function" should be tsc's '(' expected and a parsed body), classStaticBlock26 line 25
  (`(await) => {}` in a static block: arrow_function_tristate should refuse `await` there).
- Index signatures: done (`parse_index_signature()`, TS1017/1018/1019/1020/1022/1025/1096 from the parser as grammar
  codes, TS1337/1268/1021/2369 from `interface_member_typecheck()`). Enum computed names (TS1164) done too.
- ~~Heritage clauses~~ done (2026-09-27, `parse_heritage_clauses()`; the cluster went 22 -> 0). Left: TS2552's
  `Did you mean 'undefined'?` (scope_suggest() doesn't offer `undefined`; classExtendingPrimitive), tsc's
  checkGrammarExpressionWithTypeArguments (`import<T>` in heritage, empty `<>`).
- `with`: done (`STATEMENT_WITH`, `WithStatement`).
- Private names: done for object literals, type members, enums, variables/parameters/catch, a lone `#` (TS1127). Left:
  privateNamesAndIndexedAccess (`C[#bar]` recovery in a variable declaration list), `[#x]` nested binding elements
  (TS18016), manyCompilerErrorsInTheTwoFiles/`#!` mid-file (TS18026, lexical).
- Decorators: TS1436 (`public @dec m()`) done. Left: decorators before expressions (`var F = @dec () => {}`: tsc's
  TS1109 position), enum members, import statements (above).
- `other:M` parser checks: TS2754/TS1034 (super) done; superWithTypeArgument{,2,3} still get a spurious TS2348 (tsc's
  `super<T>()` callee is an ExpressionWithTypeArguments and reports nothing else). Done at the stage 3 session:
  postfix updates, templates as property names, TS1477, reserved words as bindings/class/enum names, `new <T>F()`,
  bodyless accessors, `throw` + line break. Left (`./syntaxstats.py --list other:M`, 30): TS1433 (decorators/modifiers
  on `this` parameters), TS1453 (`resolution-mode` in triple-slash references), TS2819 (`declare module debugger`),
  TS1146, TS1005 for `function* gen {`, `a = () => { } || a`, `a / finally`, `class C { public constructor; }`.

## Stage 3: lexical diagnostics (done)
What was built, for whoever touches the scanner-level code next:
- Numbers: `scan_numeric_literal()` is tsc's scanNumber (separators TS6188/TS6189, TS1351/1352/1353 for what follows,
  TS1177/1178/1125 for empty `0b`/`0o`/`0x`, TS1124 after the exponent sign, legacy octal TS1121, leading zeros
  TS1489); the value is strtod of the separator-free digits.
- Identifiers: `identifier_char_size()` (ASCII table first, then `unicode_identifiers.c`, generated from scanner.ts'
  tables by `unicode_identifiers.py`, by target; unicode escapes of either). Escaped names are keyed by
  their value (`name_cooked()`, `bugs/escaped_identifiers.md`). Columns count UTF-16 code units (`utf8_byte_utf16_units()`, `span_col_index`).
- Files: `text_decode()` at load (program.c and the harness): UTF-16 BOM files decoded, bytes that aren't UTF-8
  replaced by U+FFFD like node; a U+FFFD at a token start is TS1490 and the rest of the file trivia.
- Strings/templates: TS1160 unterminated templates, templates commit at `${` (a missing `}` ends them like tsc's
  missing template tail), TS1487/TS1488 octal and `\8` escapes, TS1126 `\` at EOF (`parse_error_in_literal()`).
- Trivia: conflict markers TS1185 (after a newline or at the file start), `#!` mid-file TS18026.
- Regex: `parse_regex()` TS1161 with tsc's end guess; everything else is `regex_check.c` (tsc's
  checkGrammarRegularExpressionLiteral, a checker grammar check, with `regex_tables.c` from `regex_tables.py`).

## Stage 4: positions
Whatever of `other:B/D/E` is left after stage 1: errors at the right token. Sample them; expect mostly per-construct
commit points (stage 1 step 5) that weren't reached yet. Stop when a cluster is a single test.

## Order and budget
Stage 1 is the architecture and goes first (one session, maybe two: the framework plus class members, statements
and object literals in the first, the other lists in the second). Stages 2 and 3 are independent of each other and
of stage 1 and parallelisable across sessions; stage 2 is the best tests per hour. Stage 4 is optional polish. Recovery
on invalid code matters less to users than checker correctness, so each stage is one session unless the counts say
it's paying off unusually well.

## Validation, overall
- `./syntaxstats.py` per cluster before/after each stage, recorded in `measurements.md`.
- `./fasttest.sh --summary`: no losses; `./resultdiff.py --lost` empty.
- `./realworld.py` and `--corpus type-fest|valibot|react`: still zero parse diagnostics, counts not worse.
- `./bench.sh`: parse time on react and the tsc corpus within noise (the list predicates run on every element).
- binderBinaryExpressionStress still passes at the default stack (don't add frames per list element).

## Leftovers (2026-09-29, after the parser session)
`./syntaxstats.py --list A` (etc.) for the current state. By cause:
- Not the parser: duplicatePackage(_withErrors) (tsc redirects a duplicate package's file and never parses it: program
  level); parseErrorIncorrectReturnToken and templateInsideCallback expect the harness's "TS-1 Pre-emit/post-emit"
  line (an excess emit-resolver diagnostic), which the harness can't produce; objectCreationOfElementAccessExpression.
- Escaped identifier names: done but for unicodeEscapesInNames02(target=es5) (`bugs/escaped_identifiers.md`).
- NUL bytes: the parser treats `\0` as end of file everywhere (scannerUnexpectedNullCharacter1,
  scannerStringLiteralWithContainingNullCharacter1); TransportStream (binary file, TS1128 span).
- Decorators with no declaration after them: tsc makes a MissingDeclaration node the baselines walk but the checker
  skips; tttt has none (esDecorators-decoratorExpression.3, esDecorators-classDeclaration-exportModifier.2,
  parseUnmatchedTypeAssertion). A first attempt lost symbols in other decorator tests.
- Reserved words: `type void = I` / `interface void {}` (reservedNamesInAliases: TS2304 'interface' plus TS2427),
  `function function()` (commonMissingSemicolons), `...this: C` (thisTypeInFunctionsNegative), `<in in>`
  (varianceAnnotationsWithCircularlyReferencesError), destructuringParameterDeclaration6 (TS1359 vs TS1003).
  reservedWords3 shows TS1390 where tsc has TS1359 (errors match anyway: worth a look).
- Recovery: variable declaration list skipping a token that made no progress (ambiguousGenericAssertion1), `<-` in a
  type literal (dontShowCompilerGeneratedMembers), `import<T>` / `import<...>("x")` (importWithTypeArguments,
  importCallExpressionWithTypeArgument), `import foo = require(x)` (importNonStringLiteral), `import type from`
  (importDefaultNamedType), `f?.<number>;` (instantiationExpressionErrors), `async * x: 1` in object literals,
  `var v: X<T \` (parserSkippedTokens20), `new X([), ...` (parser0_004152), `(await) => {}` in a static block
  (classStaticBlock26), `{ 0n: f }` (bigintPropertyName), labeledStatementDeclarationListInLoopNoCrash4.
- JSX: jsxAndTypeAssertion (two errors at EOF), tsxAttributeInvalidNames (`32data`), tsxFragmentErrors (TS17014 span
  from the previous token's end), jsxInvalidEsprimaTestSuite, conflictMarkerTrivia3 (a marker in JSX children),
  jsxNamespacePrefixInName.
- JSDoc: jsdocAugmentsMissingType (an @augments tag replaces the extends clause as the base type),
  jsdocFunctionTypeFalsePositive (`{<}`: type parameter list recovery).
- Downstream of now-correct parses, checker work: TS2775/TS2776 (assertion call targets need explicit types:
  assertionTypePredicates1), TS2833 namespace name suggestions, TS1230 predicate checks, TS2206, TS2580 next to TS2503
  (`import fs = module("fs")`), TS7031/TS2842 on missing names (parametersSyntaxErrorNoCrash1/2), TS2542
  (readonlyArraysAndTuples), accessor bodies in type literals don't infer a return type (accessorBodyInTypeContext),
  duplicate object literal methods don't merge (callSignaturesWithParameterInitializers2), overload checks skip
  literal computed names (`[1]`: TS2391).
- Types/symbols baselines of recovered code: a missing function type annotation prints as a reused `any` (tsc: not
  reused), an import's non-string module specifier isn't in the AST, `a<b>?.()` doesn't print `a<b>`, `extends (B)`
  prints `B : B` (tsc `typeof B`), the alias position of a decorated import-equals, literal property names in binding
  patterns (renamingDestructuredPropertyInFunctionType), `typeof foo<unknown>` / `...any` vs `...any[]` printing.
- Triage drain 2026-10-07 (agent D), re-checked, each one test:
  - varianceAnnotationsWithCircularlyReferencesError (`<in in>`): tsc reads the second `in` as a reserved-word name
    (TS1359, missing name, not consumed), then an empty type parameter with a missing name consumed by the list's
    no-progress rule; both are '(Missing)': TS2300, and TS2637 twice through the merged symbol. Needs type parameters
    with missing names (GenericItem without a name span) that the binder declares as `(Missing)`.
  - scannerUnexpectedNullCharacter1: still the NUL-as-EOF item above (an end pointer instead of the terminator, or a
    loader-side substitute byte the baseline printer maps back).
  - reachabilityChecksNoCrash1: everything matches but the final stray `}`, TS1109 (an ExpressionStatement of a
    missing identifier, typed `any` in the types baseline) in tsc, TS1128 in tttt: tsc reaches it inside an
    expression context left open by the earlier object-literal recovery (`> { const out = [] ... }`), not through the
    source-element list's abort. Find which construct still parses at that `}`.
  - manyCompilerErrorsInTheTwoFiles: tsc's scanner returns Unknown after only the `#` of a non-initial `#!` (pos++),
    so the `!` is scanned again and recovery reports TS1134 at it (`const a =!@#!@$`: (1,13)); tttt maps TS18026 in
    parse_error_inner() over both characters. Token-level rescan semantics, no cheap emulation found.
  - jsxNamespacePrefixInName(React): errors pass; `<a attr:=...>` recovery types `a` undefined in tsc, any in tttt.
  - requireOfJsonFileWithoutResolveJsonModule: 6.0 resolves the .json even without resolveJsonModule; tsc's JSON
    parser recovery on the non-JSON text gives TS1005/TS1136 and an object `{ contents: any; Not: any; read: any; }`;
    tttt says TS1328. Needs JSON files parsed with tsc's parseJsonText recovery (an object literal expression parse of
    arbitrary text).
  - parser0_004152 (`new X([), ...`), jsxInvalidEsprimaTestSuite, thisTypeInFunctionsNegative (`...this`): as listed
    above, unchanged.
  - allowUnescapedParagraphAndLineSeparatorsInStringLiteral with a closing backquote deleted (mutation): the
    unterminated template's recovery (TS1160 placement and what the rest of the file becomes).
  - typeofImportDefer: fixed (`import` in a type always starts an import type); its symbols/types still want the
    missing identifier of `.defer` printed as `Symbol(unknown)` / `> : any`.
