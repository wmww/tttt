# Plan 18: template literal types, string mapping types, mapped `as` clauses

## Why
Plan 17 measured the second corpora (`./realworld.py --corpus type-fest|valibot`, see `measurements.md`). The
single type-core feature under most of what is left is **template literal types**, which tttt doesn't have at all:
`` `a${B}` `` in a type parses as the string literal `"a${B}"` (`parse_type_compat_literal`), so an `infer X` inside is
never declared (type-fest: 87 TS2304 in `source/`, every one of them this), a nested template (`` `${F<`[${M}]`>}` ``)
is a parse error (type-fest `get.d.ts`, expect-type `utils.d.ts`, which every `expectTypeOf` in valibot goes
through), and type-level arithmetic (`Sum`, `LessThan`, `ArraySlice`, `StringToNumber`: `` `${N}` extends `-${infer
M}` ``) evaluates wrong, which is a big part of type-fest's ~500 test-d diagnostics. expect-type turns every mismatch
into a template literal message constraint (`"Expected: ${PrintType<Expected>}, ..."`), valibot's 1166 TS2344.
Next to it, mapped type **`as` clauses** (key remapping: type-fest 23 uses in `source/`, expect-type) and
**`Uppercase`/`Lowercase`/`Capitalize`/`Uncapitalize`** (type-fest 20 uses) are unimplemented.

This is type-core work that the suite underweights (~80 tests) but every modern library leans on. Follow tsc
(checker.ts: `getTemplateLiteralType`, `getStringMappingType`, `isTypeMatchedByTemplateLiteralType`,
`inferTypesFromTemplateLiteralType`, `templateLiteralTypesDefinitelyUnrelated`, `getNameTypeFromMappedType`,
`resolveMappedTypeMembers`) for the observable rules.

## Stages
Each stage ends with `./fasttest.sh --summary` clean (nothing lost), `./realworld.py` at 0, and the two corpus counts
recorded in the commit message.

### 1. Parse and represent
- `TypeNode`: a template tag with `n+1` text spans (cooked, escapes as in expressions: reuse the expression template
  scanner, which handles nesting) and `n` type nodes. Nested templates inside `${}` must parse (a type parser
  reentry, not a string scan). The symbols/types baseline walk (`type_compile_test_info`) has to visit the inner
  types.
- `Ty`: a new deferred kind `TY_TEMPLATE_LITERAL` (texts as atoms + types), interned by content. `ty_template_literal()`
  normalises like tsc's getTemplateLiteralType: literal/`null`/`undefined`/boolean/number/bigint spans fold into the
  text (number formatting: tsc's `numberToString`, JS rules), a union span distributes (cross product; over 100000
  results is TS2590 and `any`... check tsc), `never` anywhere gives `never`, `any` gives `any`? (check), all-text is a
  string literal, `` `${string}` `` alone is `string`. A lone `${T}` with `T` generic stays a template.
- `TY_STRING_MAPPING` for `Uppercase<T>` & co on a generic operand; on literals compute directly (ASCII-only
  `toUpperCase` is wrong for some inputs, tsc uses JS's full Unicode mapping; ASCII + the common Latin-1 range is
  probably fine, note the rest in hacks.md). The lib declares them `type Uppercase<S extends string> = intrinsic;`:
  recognise the `intrinsic` keyword body in lowering (today it's probably an unresolved name).
- Instantiation, display (`` `a${B}` ``, string mappings as `Uppercase<T>`), `keyof`/apparent type (the apparent type
  of a template/string mapping is `String`), `typeof` in narrowing (`"string"`).

### 2. Relations
- A string literal to a template: tsc's isTypeMatchedByTemplateLiteralType / inferTypesFromTemplateLiteralType
  (greedy-from-the-left matching of the text parts, placeholders `${number}` accept what `+s` round-trips,
  `${bigint}`, `${boolean}`, literal unions).
- Template to template (`templateLiteralTypesDefinitelyUnrelated` as the fast no, then inference-based matching),
  template to `string` (always), string mapping relations.
- Pattern literal index signatures (`[k: `data-${string}`]: T`) if cheap. They share the matching code.

### 3. Inference and conditional types
- `infer X` inside a template target (inferTypesFromTemplateLiteralType), including `infer N extends number` (tsc
  converts the matched text to a number literal when it round-trips) and the other primitive constraints.
- Conditional types whose check type is a template/string mapping: the definitely-false test must not resolve too early
  (generic template = deferred).
- Template literal *expressions*: tsc types `` `a${x}` `` as a template literal type when the contextual type is a
  template/string-literal-ish type or in a const context (`as const`), else `string`. hacks.md `compute` lists this as
  missing.

### 4. Mapped type `as` clauses
- `expand_mapped` with `MappedInfo.name_type`: each key is instantiated through the `as` type, `never` drops the
  property, several source keys can map to one name (union of templates, tsc merges them). A mapped type with an `as`
  clause is not homomorphic for arrays/tuples (`instantiate_mapped_over_list` must bail out), but modifiers still come
  from `T` when the constraint is `keyof T` (tsc's getModifiersTypeFromMappedType).
- `keyof` of a generic mapped type with an `as` clause is the `as` type over the constraint (tsc's
  getIndexTypeForMappedType), and relations to a generic mapped type with an `as` clause go the non-homomorphic way.

### 5. Measure
Rerun both corpora, rank what is left by root cause (sample ~30), and write it into plan 19 (the "corpus 2 to zero"
loop), next to its stage 0 (module augmentation of aliased exports, see there). Substitution types
(`T extends string ? Foo<T> : never` checks `Foo<T & string>`' constraint) are a candidate: see whether TS2344 in
type-fest `source/` shows up once templates work.

## Validation
- Probes in `test/probes/plan18/` (one per rule above) match `./tscbaseline.sh` on all three components.
- The suite's template literal tests: `-f '^templateLiteral.*'`, `-f '.*[mM]appedType.*[aA]s.*'`, `-f
  '^stringMapping.*'` / `intrinsicTypes` (list them in the commit message with before/after), nothing lost overall.
- type-fest `source/` has no TS2304 and no parse errors; expect-type's `utils.d.ts` parses; both corpus totals go
  down (report them); the tsc corpus stays at 0 and `./bench.sh` shows no regression on it.

## Risks
- Cross-product blowup (`${A}${B}${C}` of big unions): keep tsc's 100k limit and intern early.
- Template-to-template relations are the recursion-heavy part of type-fest's numeric utilities: watch TS2589 and the
  instantiation counter (`Checker.instantiation_count`, reset per expression/statement as in tsc since plan 17).
