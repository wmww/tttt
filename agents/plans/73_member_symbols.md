# Plan 73: one answer to "which class declarations make one member symbol"
Written by plan 71's audit (2026-10-15). Sizing: one to two execution sessions. Not urgent: every copy agrees on the
whole suite and every oracle today; this is debt that makes each new merge rule (tsgo 7.1's property/accessor merge,
interfaces merged with classes, late-bound merges) land in three places.

## State
The symbols baseline's copy is gone (commit "Symbols baseline: a class member's declarations are the checker's Prop
decl spans"): it asks `ty_class_item_prop()` (members.c), whose Prop.decl spans are the answer. Three copies remain:

1. **The binder** (`class_decl.c`): `bind_member_symbols()` sets `ClassItem.symbol_first` for written names and
   literal computed keys (tsc's declareSymbol with TS 7 excludes, the full-accessor state). Runs at name lookup, before
   any checker: `link_overload_implementations`, accessor pairing (`accessor_first`/`accessor_getter`,
   `function_impl_pair_setter`), `field.joined_accessor`, declaration_emit's private-member dedupe use it. This one is
   legitimate: tsc's binder is syntactic too. Keep it.
   `computed_symbol_first()` next to it is NOT legitimate: tsc's lateBindMember groups late-bound members by the
   *resolved* name (the key's type), this groups by the key's source text. Verified divergence:
   `const a = Symbol(); declare const b: typeof a; class C { get [a](): string {...} set [b](v) { v.length; } }`
   is clean under tsgo (one accessor pair, `v: string`); tttt reports TS7032 + TS7006 on the setter and TS2540 on
   `c[a] = "x"`. Users: the accessor pairing of computed accessors in
   `class_decl_lookup_names` and `overload_symbol_declarations()` (overload grouping of computed methods).
2. **The checker's MembersBuilder** (`members.c`, `builder_add_class_prop_joined`, `builder_add_class_items`): early
   joins from `symbol_first`, plus its own rules on top: a field after an interface's property joins, an accessor after
   an interface's property joins (tsgo 7.1), the `only_late` pass's meanings/excludes test
   (`class_item_meanings`/`prop_meanings`/`meanings_excluded`) for late-bound members merging into early ones. Interfaces
   have their own `member_kinds` copy in `builder_add_interface_member`. Its Prop.decl spans are what the symbols
   baseline and declaration emit show.
3. **check.c's MemberTables** (`ty_check_class_duplicate_members`, `check_class_interface_members`, the interface /
   type literal check near `declare_late_member` users): a third declareSymbol (`declare_symbol_flags`,
   `declare_late_member`, `merge_late_members`) that also models what the others don't need: namespace exports and the
   class's `prototype` in the static table, class + interface instance members in one table, every conflict reported
   (TS2300/TS2804, related info across files), "declared again" records for TS2717 (`check_subsequent_property`).
   ~800 lines, the most faithful copy.

## Design
One checker-side table per class declaration (lazily built, cached by ClassDecl in the checker like other per-node
caches) that is MemberTables' result kept instead of thrown away: entries (name, table, flags, declarations in tsc's
order: early first, late merged after) and per class item its entry (or -1: a symbol of its own), plus the list of
conflicts (pairs of spans and the display name) instead of reporting while declaring.
- `ty_check_class_duplicate_members` reports the conflicts from it (same output, now a replay).
- `builder_add_class_prop_joined` joins an item into the Prop of its entry's first item (instead of `symbol_first` +
  the interface rules + `only_late`'s excludes test). The class + interface table already answers the interface
  joins; check that cross-file class/interface merges (mergeSymbol, not one table) keep working (suite tests
  `*classAndInterfaceMerge*`, `*mergedInterfacesWith*`, `*accessorsOverrideProperty*`).
- Late-bound accessor pairing and overload grouping for computed names come from the table (entry identity), and
  `computed_symbol_first()` is deleted. This moves part of the pairing from name lookup to the checker:
  `function_impl_pair_setter` for computed accessors must be done lazily (the setter's contextual parameter type is
  only asked by the checker anyway); find every pre-checker reader of `accessor_first` for a computed accessor first.
- Interfaces: `builder_add_interface_member`'s `member_kinds` can take the same table built for interface
  declarations (check.c builds one for TS2300 already). Optional second stage.

Perf: the table is built once per class instead of re-derived by the builder per resolve_declared_members (base and
static side) and by check.c; MembersBuilder is hot (`scripts/instrcount.sh` before/after, flat or better).

## Validation
- Suite 0 lost (`scripts/fasttest.sh --summary`); the probe above (plus a literal-type variant: `const k = "x";
  declare const k2: typeof k;`) added to `test/cases/` and passing.
- `scripts/oracles.py` unchanged (corpora, sweep, `$ExpectType`, flagflip), `scripts/sourcesweep.py --sample 4`,
  `scripts/dtsdiff.py --sample 4` (declaration emit reads Prop decl spans and `symbol_first`).
- hacks.md's "Which declarations make one member symbol" entry deleted; `computed_symbol_first`, the joins rules in
  `builder_add_class_prop_joined` and `declare_symbol_flags`' class callers have one source.
