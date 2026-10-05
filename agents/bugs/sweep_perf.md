# Sweep packages where tttt is slower than tsc (plan 44)
`./sweep.py` lists them under "slower than tsc". Found 2026-10-05:

## types/carbon-components-react: 25 s (tsc 5.9 s)
`tttt --time`: libs/parse/names ~50 ms, **typecheck 21.9 s with only 31841 types and a 48 MB arena**, so an
algorithmic hot spot, not type explosion. Three gdb samples (perf isn't permitted on this machine; run under
`gdb -batch -ex run` and `pkill -INT` the child) are all in:

    intersection < ty_keyof < ty_instantiate < instantiate_inner < ... (nested instantiate_list / instantiate_alias_args)
      < ty_jsx_signature_props / check_jsx_attributes < resolve_with_signatures < ty_resolve_jsx_element

i.e. `keyof` of something whose keys are intersected (keyof of a union = the intersection of the members' keyofs),
under JSX props instantiation. `intersection()` has tsc's intersect_unions_of_primitives, so the cost is either a
missing memo (the same keyof recomputed per instantiation: tsc caches index types per type, `resolvedIndexType`) or a
quadratic step in it. Find the hot JSX element in carbon's tests (bisect the test file), callgrind that.

## types/d3-geo: 1.85 s (tsc 0.62 s)
Not looked at.
