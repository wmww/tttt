# Plan 82: optional members' types carry their undefined
Written by the hacks audit of 2026-10-17. Stage 1 (the investigation) and stage 2 (properties) were done 2026-10-18.
Stage 3 (tuples and parameters) is one session, scheduled after the insights entry.

## The problem
tttt stores an optional property's type (`Prop.type`, `prop_type()`) and an optional parameter's type
(`SigParam.type`) *without* `undefined`, and readers add it (`ty_optional_prop_type()`, `ty_optional_param_type()`,
`ty_sig_param_type()`). tsc stores it: getTypeOfSymbol's addOptionality adds `undefinedOrMissingType` for properties
and tuple elements, and a real `undefined` for parameters and parameter properties. Two hacks.md entries hang off
this. "Optional property types lack their undefined" covers the all-optional intersection special case (react-select)
and the non-member part that drops PROP_OPTIONAL. "Optional parameters are `T` in the signature" is the second. Every
new reader is a chance to forget the undefined.

## What stage 1 found
**tttt already mixes both representations.** Tuple element types, tuple-member props, partial spreads (non-exact),
`?`/`-?` over arrays and tuples, array-literal holes and union signatures' parameters already store `T | undefined`
(tsc's shape). Declared props, mapped props, FoundProp (`find_prop()`), most tuples built from patterns and SigParams
store bare `T`. Readers cope because the adders are idempotent: `ty_optional_prop_type()` returns a type that already
has it unchanged, and `ty_union` drops missing next to a real undefined like tsgo's getUnionTypeWorker. **That
idempotence is what makes the migration cheap.** Once the construct sites store the read type, every existing reader's
add becomes a no-op. Only the sites that rely on the bare type need work.

**A naive flip already nearly works.** Making `prop_type_slow()` store `ty_optional_prop_type(type)` for every
PROP_OPTIONAL non-pattern prop (one line, worktree experiment) loses only 36 / 13829 suite tests. Corpora: type-fest
gets 18 spurious diagnostics, zod 1, effect 1, everything else is unchanged. The failures fall into the classes the
inventory predicted:
- **Optional methods print as properties**: `baz?: (() => string) | undefined` instead of `baz?(): string`
  (typesWithOptionalProperty, methodSignaturesWithOverloads*, for-of29's TS2488 text, assignmentCompatBug2,
  declarationEmitOptionalMethod). tsgo's printer drops Undefined before it takes the call signatures
  (addPropertyToElementList).
- **exactOptionalPropertyTypes display and relations**: they need missing stripped (getNonMissingTypeOfSymbol):
  strictOptionalProperties1-3, exactOptionalPropertyTypes*, inferenceExactOptionalProperties*, spreadDuplicateExact,
  spreadObjectPermutations(exact), stripMembersOptionality, ttttIntersectionOptionalPropsExactOptional.
- **Index constraints and index-signature relations under non-exact**: type-fest's 15 TS2411s, its TS2344/TS2345 on
  `Record<PropertyKey, …>` and JsonObject, and probably zod's and effect's. tsc uses
  `getTypeWithFacts(propType, NEUndefined)` for an optional prop against a string index (membersRelatedToIndexInfo,
  checkIndexConstraintForProperty). tttt's `ty_check_index_constraints()` (check.c) and `index_signatures_related()`
  (relate.c) did this by *not* adding undefined.
- **TS2327 ordering** (regexpExecAndMatchTypeUsages: "optional in type … but required" expected, a type mismatch
  reported). `props_related_impl()` compares the read types before its optional-vs-required check, so a read type with
  undefined fails first. Check where tsgo's propertyRelatedTo/isPropertySymbolTypeRelated get their types for a
  required target (getNonMissingTypeOfSymbol on both).
- **Union reduction and narrowing** (unionTypeReduction2, unionReductionMutualSubtypes, strictTypeofUnionNarrowing,
  weakType*, the optionalChaining chain tests): the subtype relation's optional handling and `is_reducible_intersection()`
  (checker.c), which stops on TYF_NULLABLE in a one-side-optional prop's type.

**Cost**: the experiment that builds the read-type union for every resolved optional prop adds 0.01-0.3% types on
every corpus except effect, which gets +4.2% (+23k), all of it from *instantiated* optional props whose type is
resolved but never read with undefined. Effect's typecheck wall goes +1% (1202 -> 1213/1222 ms, noise-level). Declared
props alone: at most +0.1% anywhere. Acceptable inside the window's 3% wall rule. Measure instructions at the end
anyway (`scripts/instrcount.sh`).

**tsgo's rules** (internal/checker, worth reading before starting):
- addOptionalityEx and getOptionalType(t, isProperty): properties and tuple elements get undefinedOrMissing,
  parameters plain undefined.
- getNonMissingTypeOfSymbol = removeMissingType(getTypeOfSymbol, optional), used by relations (propertiesRelatedTo,
  isPropertySymbolTypeRelated, compareProperties, membersRelatedToIndexInfo, typeRelatedToDiscriminatedType), index
  constraints and the printer (addPropertyToElementList).
- getWriteTypeOfSymbol = removeMissingType too.
- getTypeOfMappedSymbol: `?` adds getOptionalType *unless the type maybeTypeOfKind(Undefined|Void)*; StripOptional
  (`-?` over an optional prop) does removeMissingOrUndefinedType.
- inferFromProperties uses removeMissingType on both sides, inferFromIndexTypes removeMissingOrUndefinedType.
- createUnionOrIntersectionProperty unions/intersects getTypeOfSymbol of every part, undefined included. That is the
  general rule that replaces the react-select special case.
- getSpreadType: an optional right prop over an existing left gives `left ∪ removeMissingOrUndefinedType(right)`, or
  `left` when the two are equal without undefined.
- Kept bare (no addOptionality) by tsc: object-literal props made optional by a destructuring default
  (PROP_PATTERN_OPTIONAL), getTypeFromObjectBindingPattern's props, padObjectLiteralType's.

## Decisions
- **Representation**: `prop_type()` is tsc's getTypeOfSymbol. An optional prop's type carries `undefined` (missing
  under exactOptionalPropertyTypes, a real undefined for a parameter property), except for the kinds tsc keeps bare.
  Put those behind one explicit flag (PROP_PATTERN_OPTIONAL extended to `binding_pattern_type()` and
  `ty_pad_initializer_type()`'s props in lower.c, which today lack it so readers add undefined there: check each
  against tsgo, it may be a latent divergence either way). No separate written type is kept. Display strips missing
  (tsgo prints `x?: T | undefined` without exact, `x?: T` with it). Declaration emit reuses the annotation (AST) as it
  does now.
- **Helpers**: add `prop_non_missing_type(me, prop)` (getNonMissingTypeOfSymbol). Every relation, index-constraint and
  display site that compares or prints uses it. `prop_write_type()` and `ty_remove_missing()` stay.
- **FoundProp.type is the read type too.** `find_prop_in()`'s intersection branch always intersects the parts' read
  types (tsc). That deletes the react-select collapse, the `keeps_undefined`/non-member PROP_OPTIONAL drop, and the
  per-part "add when the result is required" logic, and likewise `intersection_prop_type()` and
  `intersection_props_related()` (relate.c). The hacks.md entry predicts a spurious TS2590 in tiptap's
  NodeViewContent.tsx:19 (source sweep). Find out why tsgo doesn't hit it rather than special-casing it again.
- **Mapped props**: `instantiated_prop_type()` implements getTypeOfMappedSymbol (the void/undefined exemption,
  StripOptional = removeMissingOrUndefined) and drops its final `ty_remove_missing`. Plain instantiations just
  instantiate the stored read type.
- **Migration with a cross-check**:
  1. Flip the construct sites.
  2. Replace every reader's `flags & PROP_OPTIONAL ? ty_optional_prop_type(x) : x` with a checking helper. Under
     `TTTT_CHECK_OPTIONALITY=1` it `log_error`s when adding would change `x`, which means a construct site forgot
     it.
  3. Run the suite and the corpora with the env var until it's silent.
  4. Delete the helper calls, so readers just read.

  This keeps the "every reader adds" pattern from surviving the migration as dead code.
- **Parameters and tuples go in stage 3**, not stage 2: they're a separate representation (SigParam,
  TupleShape.flags), and stage 2 is already a full session.

## Stage 2: properties (done 2026-10-18)
`prop_type()` is tsc's getTypeOfSymbol for properties. What was done, and what differed from the plan:
- **Construct sites**: `prop_type_slow()` adds for a declared optional prop (`ty_optional_param_type()`, a real
  undefined, for a parameter property), not for PROP_PATTERN_OPTIONAL. `instantiated_prop_type()` applies
  getTypeOfMappedSymbol when its target is a mapped template (`Prop.is_template`, set on the three template props in
  members.c): add unless the type may be undefined/void, else StripOptional; a plain instantiation inherits. Optional
  object-literal methods add in `check_object()`. Partial spreads add `ty_optional_prop_type()` (missing under exact),
  exact spreads keep the read type, `spread_into()` uses removeMissingOrUndefined on both sides.
  `widen_object_literal()`'s stand-in is `ty_undefined_or_missing()`.
- **Binding-pattern props were already right**: `binding_element_implied_type()` adds a real undefined for an
  initialized element, which is tsgo's getTypeFromBindingElement (`addOptionality`), so they did *not* need the
  PROP_PATTERN_OPTIONAL flag the plan proposed (tsc prints `{ a?: number | undefined }` for them).
- **FoundProp**: `find_prop_in()`'s intersection branch intersects the read types; the react-select collapse, the
  per-part adds and the `keeps_undefined` drop are gone, and so is `intersection_prop_type()`'s add. The predicted
  tiptap TS2590 is checked by the source sweep (see the commit).
- **Readers**: relations (`props_related_impl()`, the discriminated paths, `intersection_props_related()`, identity),
  inference (removeMissing; index part removeMissingOrUndefined), index constraints (both sides) and the printer take
  missing out. The printer takes an optional method's signatures from its type without undefined (structure, pseudo
  member, the harness's method lines). Heritage checks, discriminators, `prop_or_index_type()`, reverse mapped
  sources, flow's dotted names and access read the stored type.
- **Cross-check**: `TTTT_CHECK_OPTIONALITY` fired only where tsc keeps the type bare too (destructuring-default
  props; `typeof globalThis & { x?: T }`, whose namespace variable has no say), on the suite and every corpus; the
  helper and every reader add are deleted. `optional_adds_undefined()` is gone.
- Left as is: the annotation-based adds (class fields' contextual/declared types, interface members, computed index
  signatures from AST types), `optional_type()` comparisons in `spread_props_of()`'s annotation reuse (normalizing,
  correct either way), `is_reducible_intersection()` strips an optional literal's undefined (hacks.md).

## Stage 3: tuples and parameters (one session, after the insights entry)
- **Tuples**: put addOptionality in the tuple normalizer (`normalized_tuple()` and the builder in checker.c: tsc's
  TupleNormalizer.add for every createNormalizedTupleType). That makes `lower.c` `binding_pattern_type()` (3310),
  `ty_pad_initializer_type()` (3373) and `infer.c` `reverse_mapped_array_like()` (680), which build bare optional
  elements today, right by construction. It also makes the optional-before-required demotion in `normalized_tuple()`
  (2660) keep the undefined, as tsc's does. Then the per-site adds (`lower.c` 2414, `instantiate.c` 395, `expr.c`
  4920, `infer.c` 245, `access.c` 508, `call.c` 272) go.
- **Parameters**: SigParam.type of an optional (`?` or initialized) parameter is getTypeOfParameter (with a real
  undefined), set in members.c's signature-from-function (5503-5530) and its instantiation. Then
  `ty_sig_param_type()` is the identity, `optionality_included` and `KEY_OPTIONAL_PARAM` go, and
  `value_entry_type()` (lower.c 1676), `binding.c` 530 and `function_impl.c` 1067 just read. Bare patterns stay
  bare (ty_display.c 3879). About 74 raw `param->type` reads exist; nearly all are rest or tuple-shape probes. Check
  each with the same cross-check helper.
- Delete hacks.md's "Optional parameters are `T` in the signature" entry.

## Validation
Per stage:
- suite 0 lost. Gains are possible in the exactOptionalPropertyTypes tests.
- every oracle unchanged or better (`scripts/oracles.py`). The sharp ones are flagflip's eopt cells (vscode's TS2412
  pair in extensionsActions.ts:578 and stateToProgressAdapter.ts:453, spreads of optional props; xstate's TS2322 in
  input.test.ts:272) and the strictNullChecks cells, plus the source sweep for tiptap.
- `TTTT_CHECK_OPTIONALITY=1` silent on the suite and corpora before the reader adds are deleted.
- instructions within ~3% (`scripts/instrcount.sh`; effect is the one to watch).
- the hacks.md entries deleted.

## Inventory (stage 1, 2026-10-18; line numbers at 68076fd5, stale since stage 2)
~140 sites. READ ~27 (all become no-ops). WRITE ~5 (already `ty_remove_missing`). DISPLAY ~18 (no-ops without exact;
the strip in `display_structure()` with it). RELATION ~30 (no-ops without exact; ~7 exact-mode sites need the strip,
listed in stage 2). CONSTRUCT ~37 (~10 must add, 4 keep bare, the rest already tsc-shaped or copies). The rest are
flag-only. Files by weight: relate.c, members.c, expr.c (spreads), ty_display.c, infer.c, check.c, lower.c,
checker.c, access.c, instantiate.c, flow.c, iterate.c, call.c, class_decl.c, test/baseline_test_traversal.c (the
harness's own adds are idempotent, they can stay).
