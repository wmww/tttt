# Plan 82: optional members' types carry their undefined
Written by the hacks audit of 2026-10-17 (todo entry 6). The biggest representation debt left on the checker side:
tttt stores an optional property's (and optional parameter's) type *without* `undefined` and every reader adds it
(`ty_optional_prop_type()`, `value_entry_type()`, `ty_optional_param_type()`, `find_prop()`/`find_prop_in()` callers:
~55 sites in access.c, relate.c, infer.c, lower.c, members.c, checker.c, expr.c, call.c, check.c, flow.c). tsc's
getTypeOfSymbol has it (addOptionality, with missingType under exactOptionalPropertyTypes). Hacks that hang off it
(hacks.md): "Optional property types lack their undefined" (all-optional intersection parts intersect read types only
in one shape, react-select; a non-member part drops PROP_OPTIONAL; always intersecting gives a spurious TS2590 in
tiptap), "Optional parameters are `T` in the signature" (Declarations). Every new reader is a chance to forget it, and
it's the kind of bug flagflip's exactOptionalPropertyTypes cells find.

## Stage 1: investigation (one session, may end with the plan rewritten)
1. Inventory every reader that adds undefined, and every reader that relies on its absence (write types, `-?`
   mapped modifiers stripping it, declaration emit printing the written type, `Partial`/`Required` homomorphic
   instantiation, inference from optional properties, excess property / weak type checks, JSX attributes). Classify:
   read type, write type, display, relation.
2. Read tsgo: where addOptionality/getOptionalType/removeMissingType/missingType run (getTypeOfVariableOrParameterOrProperty,
   resolveMappedTypeMembers' modifiers, getTypeOfPropertyOfContextualType, isOptionalTypeVariable...), and what
   `Prop` would need (a read type with undefined, a write type under exactOptionalPropertyTypes: missingType vs
   undefinedType).
3. Decide the representation (likely: `Prop.type` is tsc's getTypeOfSymbol; the written type kept only where display
   and declaration emit need it), the migration order (a debug cross-check mode comparing old and new answers per read
   is the safe way: both computed, `log_error` on a difference, over the suite and corpora), and whether parameters go
   in the same stage.
4. Measure the cost risk: unions made per optional property at lowering instead of per read (types count, perfsweep
   types ratio). The per-read unions are probably interned anyway.

## Stage 2+: the migration (sized by stage 1)

## Validation
Suite 0 lost (gains expected in exactOptionalPropertyTypes tests), every oracle unchanged or better
(`scripts/oracles.py`; flagflip's exactOptionalPropertyTypes and strictNullChecks cells are the sharp ones), the two
hacks.md entries deleted, react-select's special case and tiptap's TS2590 resolved by the general rule,
instructions flat (`scripts/instrcount.sh`).
