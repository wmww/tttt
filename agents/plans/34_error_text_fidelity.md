# Plan 34: error text fidelity (the "right codes, wrong text" bucket)

Written 2026-09-27 by the insights session; stages 1-2 executed the same day (todo entry 3). What's left is a queue
for later "error text" sessions, not a blocker for anything.

## Status
`./errtextdiff.py` (stage 1, after `./failstats.py`) buckets the tests whose errors.txt differs with the same codes at
the same positions by the first differing part of each diagnostic: `header:<code> <mechanism>`, `chain:<tsc line> /
<our line>` (or `missing:`/`extra:`/`deeper:`/`shallower:`), `related:<what> <code>`, `underline:<code> <how>`;
`--coarse` for the parts only, "tests fixed by one kind" ranks them. Type-printing heuristics are shared with
`displaydiff.py` (`displaykinds.py`).

Stage 2 took the bucket from 432 to **302** tests (suite 8958 -> 9090), all by rules, no message special cases:
tsc's incompatible stack (`reportIncompatibleStack`: "The types of 'a.b' are incompatible", "The types returned by
'f(...)'", signature return types 2202-2205), the primitive/wrapper elaboration, no elaboration from a type
parameter's constraint to another type parameter, invariant type arguments explained structurally (the variance
fallback), optional-vs-required after the property types, "has no call/construct signatures" about the apparent
type, TS2719 (two types with the same name), TS2720's suffix, TS2694 fully qualified names, `typeof import("m")` for
namespace imports in messages, property names as written (`["a"]`, `2.0`, `'1'`), related-info spans (parameter
properties, suggestions, type-only imports), and the harness dropping the comments before the first `@filename`.

## Left, by tests fixed alone (2026-09-27 numbers)
- **Non-augmenting subtypes** (tsc's `getSingleBaseForNonAugmentingSubtype` in `getNormalizedType`): a class or
  interface that declares no members and has a single base relates as its base, so structural messages name the base
  (`Property 'name' is private in type 'C2'` for `class C5 extends C2 {}`; the heads keep the original). Needs a
  per-declared-type "own member count" and "single base" (tsc's rule: class base expression a plain (dotted) name).
  ~6-8 tests (`chain:text: Property _ is private...`), and it affects TS2741/TS2739 wording elsewhere.
- **`header:TS2322 other`** (12): mixed; look at them one by one (some are `keyof A` for private names, some the
  union/intersection target choice in the chain).
- **Underlines** (60 over ~20 codes): error node choice per diagnostic, mostly parser codes (TS1127/TS1115/TS1206/
  TS1116) and TS2345/TS2348/TS7013. Each code is its own small rule.
- **`outside diagnostics`** (12): the errors.txt differs outside the per-diagnostic parts: diagnostics in lib files
  (`lib.es5.d.ts(--,--)`, duplicateNumericIndexers), global diagnostics order/text (importAssertion1,
  packageJsonImportsExportsOptionCompat x5), requireOfAnEmptyFile1. Diff them directly.
- `chain:deeper: Type _ is not assignable to type _.` (7): tttt elaborates one level further than tsc somewhere
  other than type parameter constraints; `chain:missing: Property _ is missing` (6).
- TS2696 "The 'Object' type is assignable to very few other types": tsc reports it as the *head* in some places
  (assigningFromObjectToAnythingElse, intTypeCheck line 99) and not at all in others (a type parameter target,
  typeParametersShouldNotBeEqual); tried below the head in `report_relation_error()` and reverted (+3 -1). Work out
  where tsc's head replacement comes from first.
- Unions with non-callable members: "Not all constituents of type 'X' are callable" (`resolve_call()` TODO).
- Related info still missing: TS2793 (overload implementation), TS6203, TS2771, TS2738, TS2728 on computed
  symbol-named members (`[Symbol.iterator]` declared here, TS2411).

## Validation
Each fix: `./fasttest.sh -s` with no lost components (`./resultdiff.py --lost`), the corpora unchanged (a message
change can crash, see the `ty_prop_written_name` mapped-property bug the suite didn't catch but type-fest did), and
the bucket count from `./failstats.py` into `measurements.md`.
