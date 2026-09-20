# Plan 04: persistent symbols, a new binder
Replace (don't patch) name binding: `scope.c`, `scope_stack.c`, `declaration.c`, `port_statement.c`, the
`*_add_names`/`*_lookup_names` plumbing and the bind-time `TypeNode` links (`TYPE_LAZY`, `TYPE_REF`, binding
`TYPE_INDEXED`, `MergedType`). 3-4 execution sessions, the suite and `./realworld.py` run after every stage.

## Why: what the research found
**The real corpus' biggest bug is not a cycle bug.** `utilities.ts` does `import { Node, Symbol, Type, Identifier,
Signature, Token, SourceMapSource } from "./_namespaces/ts.js"` *and* declares `function Node(this: Mutable<Node>,
...)` etc. (object allocators). In tsc the import is an alias symbol that merges with the local function; a lookup
for a *type* named `Node` finds the merged symbol, sees it is an alias, resolves it and accepts it because the
*target* has a type meaning. tttt copies the imported declaration into the scope, finds the name taken, reports TS2440
(10 of them on the corpus, all false) and drops the import; `Node` in a type position then falls through to the
global scope: the DOM `Node`. Every generic helper in `utilities.ts` (`setParent<T extends Node>`...) is poisoned,
hence ~4000 diagnostics in *callers* all over the corpus. Three-file repro:
`test/probes/plan04/aliasMergesWithLocalValue.ts`. A copy-based binder cannot express "alias merged with a value";
a symbol with `ALIAS | FUNCTION` flags and meaning-aware lookup gets it for free.

**The binder is wrong at the basics too** (`test/probes/plan04/scopingBasics.ts`, tsc reports 3 errors, tttt 14):
`var a; var a;` is TS2451, `var` isn't hoisted out of blocks/loops (TS2304 after a `for (var i...)`), a parameter and a
`var` of the same name clash, the second `namespace N {}` block can't see the first one's exports and its members are
lost ("Tried to add multiple namespaces", 485 log lines in 191 tests, `Intl`), function+namespace and enum+enum don't
merge, non-exported namespace members are visible from outside. Suite-wide signals (failstats @ 56f8a1a): spurious
TS2451 3245 in 447 tests, spurious TS2300 684/172, missing TS2300 573/124, "Imported source file is not a module" in
163 tests, ~1300 "Error added with invalid span" (duplicate/missing-name errors whose other half is in a lib).

**Profile** (callgrind, real corpus, parse + bind only = 1665 M instructions; wall: resolve names 91 ms, parse ~180 ms,
typecheck ~590 ms): parse 71%, `program_resolve_names` 17%, `program_destroy` 11% (node-by-node frees).
Inside name resolution: `scope_init_declaration` costs **~1900 instructions per declaration** (46k calls, 5.3% of the
run: an arc, a `cstr` key copy, a `vec_span` clone of the namespace path, `DeclProps` clone, `MergedType`), the hash map
lookup is ~100 instructions and runs once *per scope on the stack per identifier* (it rehashes the name each time:
230k lookups from `scope_stack_get_value/type`, 108k from `find_declaration_in_exports`, which re-walks all 60
`export *` of the barrel for every imported name of every file). Nothing exotic: arena symbols, hash the name once,
resolve a module's export table once. Expect name resolution to drop to a third; it is not where the big time is.

Other corpus name errors worth knowing (not binder design, fix when passing by): a mapped type's `as` clause can't see
the key parameter (`[K in T as K["kind"]]`, TS2304 x5), `typeof X[K]` parses `X[K]` as a value access (TS2693),
`process`/`require`/`Buffer` need `types: ["node"]` (module resolution, not this plan).

## Design
Follow tsc's *model* (it is what the symbols baseline, merging rules and error codes observably encode), not its
laziness: tttt keeps resolving identifiers ahead of checking, because it's faster and parallelises per file.
`thirdparty/tsc/src/compiler/binder.ts` (`declareSymbol`, `getContainerFlags`, `bindWorker`) and `types.ts`
(`SymbolFlags` and the `*Excludes` masks) are the reference.

### Symbol
One struct, arena allocated, never copied, never freed individually:
- `flags`: tsc's `SymbolFlags` bit for bit where they exist here (FunctionScopedVariable, BlockScopedVariable,
  Function, Class, Interface, RegularEnum, ConstEnum, EnumMember, ValueModule, NamespaceModule, TypeAlias,
  TypeParameter, Alias; composite masks `VALUE`, `TYPE`, `NAMESPACE`). A namespace is a ValueModule only if it is
  instantiated (contains a value), like tsc; that decides `N` being usable as a value and TS2708.
- `name`, `parent` (the container symbol: namespace, module, enum, class; gives `A.B.x` for `ty_qualified_name` and the
  symbols baseline, and is the start of accessibility), `declarations` (every contributing AST node as
  `{kind, ptr, name span}`: extend `DeclNode` to variables/bindings, parameters, functions, type aliases, type
  parameters incl. `infer`/mapped keys, namespaces, import/export specifiers), `value_declaration`.
- `exports` (namespaces, enums, modules: **one table per symbol, shared by all its declaration blocks**), and for
  aliases the declaration to resolve plus a memo `{state: unresolved|in flight|done, target}`.
- Merging is `declare(table, name, includes, excludes)`: if an existing symbol has `flags & excludes` it's a conflict
  (TS2300, or TS2451 when either side is block scoped, TS2567 for enums; report on *every* declaration of both, like
  tsc), otherwise the declaration is added to the existing symbol. Use tsc's exclude masks verbatim: they already say
  var+var ok, function+namespace, class+interface, class+namespace, enum+enum, enum+namespace, interface+anything
  non-type, alias+anything non-alias. Order rules (TS2434 namespace before class/function) stay checks on the side.
- Class/interface *members* do not become `Symbol`s in this plan: they're found through types, and `Prop` has the
  identity the checker needs. The symbols baseline prints them from `Prop.parent` as today. Revisit with plan 05 if
  narrowing of `this.x`/`a.b` references wants more than (reference symbol, property name path) keys.

### Scopes persist
`Scope { parent, kind, locals, container symbol }`, arena allocated per file, pointer kept on the AST node that opens
it (file, namespace block, function, block, for/for-in/of, catch, switch, class, type parameter lists, conditional
type for `infer`, mapped type). Lookup walks `parent`; in a namespace block or module it checks `locals`, then the
container's `exports` (this is what makes a second `namespace N {}` see the first one's exports, and what keeps
non-exported members out of `typeof N`). No `ScopeStack`, no `current_scope_stack`, no `currently_adding_import`.
`var` and (sloppy-mode rules aside) function declarations hoist to the nearest function/module/namespace scope through
a statement-only walk (`if`/`for`/`try`/`switch`/labels/blocks, never into functions); `let`/`const`/`class`/`enum`
and block-level functions stay in the block. Small scopes dominate (most blocks declare 0-3 names): use an inline
array up to ~8 entries and a hash table beyond; keys are name views into the source (no `cstr` copies) with the hash
computed once per lookup, not once per scope.

### Lookup is by meaning and sees through aliases
`scope_resolve(scope, name, meaning)`: a symbol matches when `flags & meaning`, or when it is an alias whose resolved
target matches (`getSymbol` in tsc). Identifiers store the symbol *found* (`decl_ref` becomes `Symbol const*`), which
may be an alias: the symbols baseline prints the alias' own declaration, the checker calls `symbol_resolve_alias()`.
Keep today's fallback diagnostics (TS2693 type used as value, TS2749 value used as type, TS2503, TS2694) by retrying
with the other meanings. TS2440 only when alias target and local declaration really share a meaning.

### Modules
Every module file has a module symbol whose `exports` are its export table; `export *` targets are a list on it.
`module_resolved_exports(module)` = own exports, plus star exports that aren't shadowed and aren't ambiguous (the same
name reaching two different symbols through two stars is dropped; tsc reports TS2308 only for explicit conflicts),
computed once per module with a visited set (tsc's `extendExportStars`); `test/probes/plan04/exportStarCycle.ts`.
Import/export specifiers, `import * as`, `export * as`, default imports, `import x = A.B.C` (resolved lexically from
its scope, first name by namespace meaning) and `import x = require()` are alias symbols. Resolution is memoised with
an in-flight state (TS2303 circular alias).

### Phases, and what may write where (this is the parallelism contract)
1. **Parse**, per file.
2. **Declare**, per file, writes only to the file's own AST and arena: file scope, module symbol and exports, or for a
   script its *global contributions* table; recursively the bodies of namespaces, `declare global`, ambient modules,
   enums (their members are exports). Same-file conflicts are reported here.
3. **Link**, sequential and small: merge script globals, `declare global` blocks and lib files into the program's
   global table in file order (cross-file conflicts reported here, with the lib half of the pair skipped instead of
   "invalid span"); register ambient modules; build resolved export tables; resolve every alias eagerly so that
   nothing after this writes to a symbol.
4. **Resolve**, per file, read-only on all symbol tables, writes only to the file's AST/arena: the deep walk of today's
   `*_lookup_names`, creating function/block scopes as it enters them and storing `Symbol*` on identifiers. **Plan 05
   hangs its flow graph construction on this walk** (it visits statements and expressions in source order with the
   scope at hand); flow nodes will be arena objects of the file, pointed to by identifier/`this`/access expressions and
   by statements that need reachability. What plan 05 gets from this plan: one stable `Symbol*` per reference
   (aliases resolved on demand), symbol flags to tell const/let/var/parameter apart, `value_declaration` and its
   scope's function to decide "captured and assigned in a closure".
5. **Check.** Per-symbol checker state lives in `Checker.links` keyed by `Symbol*` (as `DeclLinks` is today by
   `Declaration*`); nothing in a `Symbol` is written during checking.
Phases 2 and 4 are not made parallel in this plan; they must merely have no reason not to be (no process-wide
counters except atomics, no shared mutable tables, `ErrorCtx` use confined to a per-file buffer or clearly marked).

### Sharing the lib between programs (test harness, later a language server)
A program's global table has an optional read-only `parent` (the cached lib program's table). Declaring into a name
that lives in the parent clones the parent's symbol into the overlay first (shared declaration array + the new ones;
an `exports` table is overlaid the same way, which is what `Intl` augmentation needs). References inside the shared lib
AST keep pointing at the frozen symbol, so checker entry points that take a symbol (`ty_declared_type_of`,
`ty_type_of_symbol`, namespace member lookup) first map it through the program's `frozen -> overlay` table, consulted
only for symbols flagged as shared-lib globals and only when the table is non-empty. This replaces
`shared_between_scopes`/copy-on-write and *fixes* lib-internal references not seeing a test's augmentation. The CLI
has no parent table: lib files are ordinary script files of the program.

### The type side
`ty_type_of_symbol(checker, sym)` and `ty_declared_type_of(checker, sym)` switch on the symbol's flags/declaration
nodes and replace `Declaration.value_type`/`declared_type`, `MergedType`, `LAZY_TYPE_MERGED/EVALUATED`, `TYPE_LAZY`,
`TYPE_REF` and the binding `TYPE_INDEXED` link (a binding element's type comes from its `Binding` node + parent
pattern). A class or enum is one symbol with a value type (static side / enum object) and a declared type; the
`typeof_declaration` twins, `is_typeof`, `canonical_declaration`, `declaration_clone_in_place`, `type_clone`'s
binder callers and `wrapped_declaration` go. `ty_namespace_type` is built from `exports` (so `N.hidden` is TS2339).
`ty_merged_value_type` reads the symbol's value declarations. `registered_type_param` stops guessing: TypeParameter
is a flag.

## Stages
Each stage ends with `./fasttest.sh --summary` reviewed, `./fasttest.sh -s`, `./realworld.py`, the three probes
(`FAST=1 ./filetest.sh test/probes/plan04/X.ts -d`), notes in `measurements.md`, `hacks.md`/`documentation.md` updated,
commit. Pass counts may churn (correct merging stops forgiving things, as in the type core migration); a net loss
needs an explanation in the commit message, not a revert reflex.

**Stage 1: symbols, scopes, merging, aliases (the binder proper).** New `symbol.h/.c`, `binder.c` (declare + link),
resolve walk in place of `*_lookup_names`. To keep the stage one session, the type side stays: a non-alias symbol owns
exactly one `Declaration` (never copied) that keeps feeding the type core through the existing links, filled by the
same `declaration_init` calls; `decl_ref` fields become `Symbol const*` with an accessor that resolves aliases to the
target's `Declaration` for the checker. Namespace `Declaration.namespace` becomes the symbol (its `exports`). Delete
`scope.c`, `scope_stack.c`, the merge tests in `declaration.c`, `process_port`/`find_declaration_in_exports`/
`port_item_resolve_alias`, `declaration_clone_in_place`, `canonical_declaration`, `shared_between_scopes`. Lib overlay
with clone-on-merge (the `frozen -> overlay` redirect may wait for stage 2).
Done when: all three probes' errors components match tsc; on the corpus TS2440 = 0, no "baseURI" in
`/tmp/realworld.txt`, type errors well under 7k (from 9.7k); "Tried to add multiple namespaces" and "acquired multiple
times" gone from `./failstats.py`; spurious TS2451 under ~500 (from 3245); suite time not worse.

**Stage 1 as built** (2026-09-19; numbers in `measurements.md`, open ends in `hacks.md` "The binder"): everything
above landed in one session, in `scope.h`/`scope.c` (no separate `symbol.c`/`binder.c`: it is 900 lines). Deviations:
- The two-walk shape of the AST code was kept (`*_add_names(Scope*)` declares, `*_lookup_names(Scope*)` resolves with
  the current scope where the `ScopeStack*` used to be), so the diff in the AST files is mechanical.
- Identifiers carry `symbol` *and* `decl_ref` (the payload behind it, resolved by meaning at lookup time) instead of a
  `Symbol const*` with an accessor: the checker and the type core are untouched. Stage 2 folds them.
- Scripts declare directly into the program's global table (no per-file contribution tables + link merge yet), and
  aliases are resolved eagerly in `binder_link()` but *can* also resolve on demand (import-equals needs scopes, which
  all exist after declare). The contract audit is stage 4's.
- `import * as ns` is an alias to a per-import namespace symbol rather than to the module symbol (printing, stage 3).
- Also done on the way: default exports/imports (there were none), `export default x` as an alias, TS2306 instead of
  the "not a module" log_error, mapped type `as` clauses see the key parameter, class `static {}` blocks keep their
  `var`s, TS2339 on namespaces (their members are exactly their exports), `N.x` in the symbols baseline.
- "Type errors well under 7k" on the corpus did not happen (7758): the calls that the DOM `Node` used to break now
  fail on missing narrowing. Plan 05.

**Stage 2: the type side.** Delete `Declaration`, `MergedType`, the link `TypeNode` tags and the twins as described
above; `DeclLinks` -> symbol links; lib redirect table; `hacks.md` section "The AST-side walk and what's left of the
old type world" shrinks to the thread-local checker. Done when: `grep -c 'TYPE_LAZY\|TYPE_REF\|MergedType\|
Declaration\b' src` is 0 (or each survivor is justified in `hacks.md`), `scopingBasics` passes all three components,
no pass count loss versus stage 1.

**Stage 2 as built** (2026-09-19; numbers in `measurements.md`): `declaration.c/.h`, `Declaration`, `DeclHandle`,
`MergedType`, the `typeof_declaration` twins, `this_declaration`, `wrapped_declaration`, `registered_type_param` and
the LazyTypes of classes/enums/interfaces/namespace symbols are gone. A `Symbol` lists its declarations as
`SymbolDecl { kind, flags, node, name, full }`; `ty_declared_type_of`, `ty_static_type_of` (class static side, enum
object) and `ty_type_of_symbol` (`ty/lower.c`) switch on them, `SymbolLinks` replaces `DeclLinks`, `Prop.symbol` types
namespace members, `this` is a `ThisRef`. The lib redirect is `binder_redirect()` + `Binder.overlays` (no flag on the
frozen symbol: it may not be written to; only programs with overlays pay for the lookup). Deviations:
- Identifiers keep `symbol` *and* `target` (the accessor's result, cached), see `hacks.md`.
- `TYPE_LAZY`/`TYPE_REF`/binding `TYPE_INDEXED` survive as three AST-internal links (binding -> initializer/parent
  pattern, function-as-`CallableType`, class member -> `Prop.node`), listed in `hacks.md` with what each needs. None
  of them is how a symbol finds its type any more, so they're off this plan's critical path: `todo.md` has them as a
  cleanup session of their own.
- scopingBasics: errors and symbols pass; types fail only on the `^ ^^` underline lines of function types (the
  display underline bug in `triage.md`), nothing to do with binding.

**Stage 3: module symbols from everywhere, printing.** The parser records every module reference of a file (import/
export declarations, `import()` types and expressions, `require()` in import-equals) in a list on the `SourceFile`, and
`program_add_source_idempotent` loads from that instead of scanning toplevel statements. `import("./a").B<T>` and
`typeof import("./a")` resolve through the module symbol (`parse_import_type` stops returning `any`). Program-level
ambient module table (`declare module "m"`, then pattern modules if cheap), `export =` + `import x = require()`,
`export as namespace`, module augmentation and `declare global` merging through the link phase, TS2306 "is not a
module" instead of the log_error (163 tests). Symbol accessibility: `ty_qualified_name` and the symbols baseline name
qualify a symbol only as far as needed from the use site's scope chain, preferring an alias that is in scope (tsc's
`getAccessibleSymbolChain`, simplified); removes the written-type-name special case in
`baseline_test_case_add_type_node` if the numbers agree (it was worth +21 tests, compare). Done when: the corpus has no
TS2304 other than node globals, spurious TS2307/TS2694 counts are down and explained, symbols component count up.

**Stage 4 (may fold into 3): perf and the contract.** Per-file arenas for scopes/symbols/decl nodes (and drop them
wholesale: `program_destroy` is 11% of parse+bind), callgrind again, `./bench.sh`, update `measurements.md`. Audit
phases 2 and 4 against the write rules above and list every violation in `hacks.md`. Refresh `documentation.md`
(Scope, ScopeStack, Declarations, Resolve names sections are all rewritten by this plan).
Done when: resolve names on the corpus <= 40 ms (from 91), no RSS regression, docs match the code.

## Risks
- Stage 1 is big for one session because binder and ports can't be swapped separately (aliases *are* the fix). If it
  overruns, land the in-file half first with imports as alias symbols resolved through a plain recursive export lookup
  and do `module_resolved_exports`/eager alias resolution at the start of stage 2; say so in `todo.md`.
- Correct merging will surface checker gaps that the broken binder hid (merged enum objects, fundule value types,
  namespace value types without non-exported members, honest TS2339 on namespaces). Fix what's cheap, list the rest.
- The harness counts on one lib program shared by ~13k test programs: measure suite wall time early in stage 1; the
  overlay must make a test program's global table O(own declarations), not a copy of 20k lib names (today's
  `scope_copy_declarations_from` copies them all per test).
- Test-only behaviour to preserve: duplicate-identifier errors whose other declaration is in a lib are reported on the
  user's declaration only.
