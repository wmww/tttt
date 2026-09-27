# Plan 34: error text fidelity (the "right codes, wrong text" bucket)

Written 2026-09-27 by the insights session. Tool first, then fixes; one or two execution sessions.

## Why
`./failstats.py` (2026-09-27) has **431 tests** whose errors component fails with the *same codes at the same
positions* as tsc: only the text differs. Nobody owns this bucket: `displaydiff.py` buckets the *types* component,
`syntaxstats.py` the parser, `missing_checks.md` the absent codes. A quick classification of the first differing
line per test (insights, ad hoc script, not committed):
- ~183: elaboration chain lines (the indented lines under a header): e.g. tttt says `Property 'two' is optional in
  type ... but required in type ...` where tsc says `Types of property 'two' are incompatible.`; `Types of parameters
  'x' and 'x'` vs `Types of construct signatures are incompatible.` That's the order and depth of tsc's
  reportRelationError chain (structuredTypeRelatedTo's reporting), not a display problem.
- ~132: the header message: type printing inside messages. Recurring shapes: `typeof import("m")` for module
  namespace objects (tttt prints `typeof Foo`; ~15 tests over TS2322/TS2339/TS2507), `{ "0": number }` quoted numeric
  names, `undefined[]` vs `any[]` for widened empty array literals in messages (TS2741/TS2507), alias names
  (`IntrinsicClassAttributesAlias<T>`), `Namespace 'foo.bar.baz'` qualified names in TS2694, `'export='` in TS2300,
  TS2720's "Did you mean to extend" suffix, TS2411's `'["get1"]'` computed names.
- ~60: underline spans (error node choice), ~40: related information (missing/extra/wrong position).

## Stage 1: a tool
Extend `displaydiff.py` (or add `./errtextdiff.py`, same conventions: after `./failstats.py`, `-k kind`, `-f regex`,
`--names`) to bucket the text-only tests by mechanism: first differing line kind (header/chain/underline/related),
the code, and for headers a normalized diff of the message (which type-printing difference), for chains which
elaboration line tsc has where tttt has another. Report "tests fixed by this kind alone" like displaydiff does.
Document it in CLAUDE.md's script list (one line) and `documentation.md` if it has a tools section.

## Stage 2: fix the top kinds
By tests-fixed-alone. Expected large ones: module namespace display (`typeof import("m")`, also a types-component
kind: check displaydiff's `import()` bucket, they may be the same rule), and the elaboration chain (read tsc's
`structuredTypeRelatedToWorker`/`propertiesRelatedTo`/`signaturesRelatedTo` reporting, `reportRelationError`'s
choices; tttt's elaboration is in relate.c/elaborate*: match which relation step reports, not the message strings).
Don't special-case message text: every fix is a rule about what tsc reports or prints.

## Validation
The tool committed and documented; the bucket's count in `measurements.md` before/after; `./fasttest.sh -s` with
no lost components; corpora unchanged (these are all error-message changes, a corpus count can't move, but check).
