# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-20 (triage session, todo entry 5 of the twenty-third window). A truncated mapped source no
longer SIGBUSes (`text_sigbus()`: zeros over the page, one warning; verified by truncating a 6 MB file mid-run).
pgobuild.sh serializes builds (flock), waits out running pgo/tttt processes and kills its training run with itself
(the corrupt .gcda). dokploy at `kdiff -m 0`: slow, not quadratic (kdiff.py's docstring). perfsweep's summary line
was already fixed (e76f67ca); its wall/CPU gap was load (nuqs docs 0.33 s wall quiet). The bwrap flake went to
bugs/harness.md. measurements.md condensed 2139 -> 408 lines, hacks.md audited (no entry gone, many narrowed).


- (2026-10-20, agent B, ghostfolio probe) tsc's combined import symbol (SymbolAlias.combined_type, `import { Big }
  from "big.js"`): its members are the interface's own symbols in tsc (Members cloned), so `a.plus` is
  `Symbol(Big.Big.plus)`; tttt's combined has its own decls and names it `Big.plus` (symbols baseline only). And the
  union tie `c ? arr[0] : new Big(0)` keeps Big.Big in tttt (`import("big.js").Big`) where tsc keeps the combined
  type (`Big`): Big.Big's declared type gets a lower id in tttt, likely because resolving the import's value half
  (ty_export_equals_property_missing -> BigConstructor's members) lowers signature return types eagerly where tsc's
  are lazy. Probe: test/cases/ttttCombinedImportNamedByAlias.ts plus `export function u(c: boolean) { return c ?
  arr[0] : new Big(0); }`. ghostfolio itself matches.
- (2026-10-20, agent B) Property access on an intersection whose apparent type is any (a generic member constrained
  to any) finds the object member's symbol in tttt; tsc's getApparentTypeOfIntersectionType makes it any, no symbol
  (symbols baseline only; see ttttMappedOverApparentlyAnyIntersection's probe with `i: InferOutput<TShape> & {
  $type: TType }`, `i.$type`). find_prop_in() isn't the path property access takes.
- (2026-10-20, agent B) never_reduced_literal_prop() (the type-variable path of never-reduction) may want the
  HasNeverType rule some_prop_is_never() now gives the known-objects path.
- (2026-10-20, coordinator) scripts/variancetrace.py's instrumented tsgo build failed for agent B: its `go build`
  uses the shared `thirdparty/typescript-gopath/cache`, which has missing entries ("could not import context ... no
  such file"). reentrytrace.py has `--gocache DIR`; give variancetrace.py the same (a /tmp cache, mind the disk) or
  repair the shared cache from the main tree (ensure-typescript.sh caps it). It blocks the mikro-orm IndexFilterQuery
  perf lead (perf_leads.md) and apollo vite's chain truncation (display.md).
