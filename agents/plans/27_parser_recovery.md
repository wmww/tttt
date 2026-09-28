# Plan 27: parser fidelity (recovery, permissive grammar, lexical diagnostics)

Written 2026-09-26 (planning session, todo entry 5).

## Status
**Stage 3 done (2026-09-28, todo entry 6, one session + one sub-agent for the regex port)**: 402 -> 263 tests whose
parser diagnostics differ (lexical clusters 92 -> 11; M 78 -> 31 with a sweep of `other:M`), suite 9729 -> 9833,
corpora unchanged, parse time on the tsc corpus 104 -> 109 ms (after clawing back a 116 ms regression, see
measurements.md). What's left of stage 3 is escaped identifier *names* (`bugs/escaped_identifiers.md`, with TS1260)
and NUL characters in source (hacks.md "Parser"); the stage 3 section says what was built. Tagged templates aren't
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
  tables by `unicode_identifiers.py`, by target; unicode escapes of either). Escaped names stay raw
  (`bugs/escaped_identifiers.md`). Columns count UTF-16 code units (`utf8_byte_utf16_units()`, `span_col_index`).
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

## Leftovers collected by triage (2026-09-27)
Parser items that were in triage.md; fold them into whichever stage they fit.
- Stage 1 follow-ups: `other:E` grew 78 -> 81 and `other:D` barely moved (tests whose first error agrees now but
  later recovery differs; sample `./syntaxstats.py --list other:E`). `parse_class_item_inner()` still fails where tsc
  commits (nameless `*`, `get`/`set` members); function types in type position backtrack freely (tsc's
  isUnambiguouslyStartOfFunctionType). The parser's `saw_jsx`/`saw_import_meta` are set by attempts that backtrack too.
- Permissive grammar (stage 2) shapes seen in other buckets: private names outside classes (object literals,
  interfaces, parameters, enums: TS18016/TS1451, `--syntax -f '^privateName.*'`; `{ #y: 1 }`: objectLiteralErrors);
  modifiers on object literal members (TS1042, modifiersInObjectLiterals); enum members named `1`/`[2]`/`"3"`
  (TS2452/TS1357: enumErrors, literalsInComputedProperties1, parserEnum5/7; TS1164 done); `class void {}` / `type void = I`
  (TS1005/TS2457); `import { as as as }` (importSpecifiers1);
  `this is T` predicates in more positions (TS1228, typeGuardOfFormThisMember); a bare `super` (TS1034); `let x!` misplaced (TS1263/1264); `const c;` in ambient files; malformed import types
  (nodeModulesImport*DeclarationEmitErrors); `bigintPropertyName` (a parse failure in a.ts line 12).
- Recovery fidelity where the harness typechecks next to parse errors: parserGreaterThanTokenAmbiguity12-14/17-19
  (`1 >> = 2` parses as an assignment and gets TS2364); arrow functions in `?:` recovery (`(a?) => {} ? b : c`) give
  TS1005 where tsc's tokens may differ; spurious TS2300/TS2304 after bad recovery (`with`, `await`, `yield`, `public`).
- Lexical (stage 3): done but for escaped identifier names (duplicateObjectLiteralProperty,
  `bugs/escaped_identifiers.md`) and conflictMarkerTrivia3/4 (a marker inside JSX).
- JSX long tail (plan 22 left it; `./failstats.py --jsx --syntax`): jsxInvalidEsprimaTestSuite,
  jsxNamespacePrefixInName(React) (`<:a>`, `<a:>`), tsxAttributeInvalidNames (`32data`: TS1351 and a number token), an
  unclosed fragment's TS17014 span (tsc starts it at the previous token's end: tsxFragmentErrors), jsxAndTypeAssertion
  (two errors at EOF where tsc's one-error-per-position rule should have dropped one: not understood).
- `infer T extends U ?` (tsc's speculative lookahead in parseTypeParameterOfInferType / tryParseConstraintOfInferType):
  `T extends (infer U extends number ? 1 : 0) ? 1 : 0` is a conditional in parentheses, and `infer U extends number)
  ?` (inferTypesWithExtends1 X12/X13) the other half of the rule; tttt gets TS1005s.
- Small: `parse_type` turns a leading `&`/`|` into an `any`/`never` operand that every consumer must skip (hacks.md).
  Overloads of `*()`-style nameless members. `override`/TS4112+.
- (triage 2026-09-28) Rest element grammar: TS2566 (`{ ...rest: x }`: tsc parses a property name after `...`, tttt
  doesn't), TS2501, TS2462, TS1186; `a?.\`x\`` (tsc TS1358, tttt TS1003); a comma expression as a computed key
  (`[a, b]: 1`: tsc parses it and reports TS1171, tttt TS1136). All in plainJSGrammarErrors, whose parse errors hide
  ~50 grammar errors.
- `interface void {}` / `type void = I` (interfacesWithPredefinedTypesAsNames, reservedNamesInAliases): tsc gives
  both TS2304 "Cannot find name 'interface'" and TS2427 at `void`: its statement-start lookahead refuses the
  declaration, but something still declares it. tttt reports only TS2427. Not understood.
- Tagged templates: `new tag<T> \`...\`<U>(...)` parses wrong (taggedTemplatesWithTypeArguments1/2: tttt TS7009/
  TS2347/TS2693, tsc TS2345s), `super<T> \`\``, incomplete templates' types (taggedTemplatesWithIncomplete
  TemplateExpressions4/5), taggedTemplateChain.
- TransportStream (a binary file, `G@` then bytes): tsc's TS1128 is at 1:4 spanning the rest of the file, tttt's at
  1:2 (TS1490 is right).
- `function f3(function) {}` (reservedWords3) makes TS2813/TS2814 with an invalid span (a checker `log_error`).
