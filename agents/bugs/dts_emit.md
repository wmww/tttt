# Declaration emit: what still differs from tsgo (plan 64)

State 2026-10-15 (after todo entry 7): suite dts 1345 / 1408; `scripts/dtsdiff.py` 922 / 1233 projects, ~74300 / 76294
files byte-identical. `scripts/dtsdiff.py --last -k BUCKET` lists files, `--show PROJECT FILE` diffs one. Classes by
size on real code, each with an example:

- **JavaScript sources** ("missing file", ~690 files): JS declaration emit is plan 64 stage 6 (not started). crawlee,
  mermaid, astro/benchmark, query/*: `.js`/`.mjs` sources with allowJs.
- **Import specifier choice** ("type text"): tsgo names a re-exporting entry (`import("./index.ts").PgColumn`,
  `import("@apollo/client").HttpLink`) where tttt names the declaring file (`import("./columns/common.ts")`). Module
  specifier generation for symbols reached through `export *`/re-exports (tsgo's getSymbolChain prefers the chain
  through the package's or directory's index). corpus:drizzle `src/pg-core/utils.d.ts`, apollo-client.
- **Member order** (~170 files): tsgo's getNamedMembers sorts members by compareSymbols (declaration file order in the
  program, then position) in some types where tttt keeps own-then-inherited or declaration order: mixin class
  expressions' instance types (babel-parser `plugins/*.d.ts`), zod-style flattened mapped types
  (`{ [k in keyof T]: T[k] }` over `required & optional` keys: corpus:react `src/config/env.d.ts`), unions of object
  literals normalized with `?: undefined` members (excalidraw `actions/*`, angular-cli javascript-optimizer-worker).
  Probably a checker-wide property order question: check what tttt's display iterates against tsgo's
  getPropertiesOfType order before patching the printer.

- **Painting gaps** ("import", ~200 files): an import used only by a name the node builder prints from a type
  (not a reused node) isn't painted in some paths: excalidraw `actionProperties.d.ts` (ExcalidrawFreeDrawElement),
  effect `fiberRuntime.d.ts` (`import type * as Either`). Find which display path prints the name without
  `d.tracker->track_symbol()`.
- **Comments in lists without spans**: TypeNode has no span, so intervening comments inside unions
  (`A | /* B | */ C`) and tuples' line comments (`// readonly because contravariant`, effect `MetricPolling.d.ts`)
  aren't copied.
- **Unknown-symbol naming (tsgo quirk, matched)**: tsgo names an unresolved type through any import alias that also
  resolves to the unknown symbol (`node is isIdentifierChar`, babel-parser with unbuilt workspace deps) and paints
  that import; tttt prints the same name but doesn't paint the import.
- **Declaration maps** (stage 5 done): 551 of 20435 dtsdiff maps differ, 271 of them because their `.d.ts` does;
  `scripts/dtsdiff.py -k 'map:'` lists the rest by first differing mapping. Mostly tttt reusing a node tsgo prints
  anew (it then maps what tsgo doesn't: trpc's `infer T extends object` in inferred type arguments, mixin constructor
  types, `PromiseLike<void>` in testClientResource), `-0` literal types, singletons.
