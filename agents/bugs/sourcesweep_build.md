# The source sweep's solutions: `tsc -b` against `tttt -b`
`scripts/sourcesweep.py --build`: every `tsconfig*.json` with `references` that no other config of its repo
references, built by tsgo with `-b --force` in a bwrap overlay of `thirdparty/sources` (cache
`test/sourcesweep/tsc-build.jsonl.gz`, `--build --tsc[-new]`) and checked by `tttt -b --no-write`. Since `-b` reads emitted declarations
stage 1 (2026-10-20) `-b` reads its references' emitted declarations like tsc -b (documentation.md "Project
references"), so the old "by design" verdicts (errors inside emitted declarations, body-only imports, tsgo emit bugs
leaking downstream) are gone: **409 / 416** (apollo cacheOverride and signatures/modern fixed since).

**First question for each: does `tttt -p THAT_CONFIG --noEmit` differ from tsc's over tsgo's built outputs?** Build
in a persistent overlay and run both CLIs in it (`bwrap ... --overlay-src $C --overlay UP WORK $C -- bash script.sh`,
script: `tsc -b --force` once, then `tsc -p X --noEmit` and `tttt -p X --noEmit`; UP keeps the outputs for later
runs). Then a project's leftovers are plain `-p` bugs: probe with a scratch config in UP next to the failing one,
`skipLibCheck: false` shows errors *in* the emitted `.d.ts` (apollo's TS2456 was only visible that way).

Left (2026-10-20, by project; + spurious, - missing):
- atproto: +TS2307 1 (oauth-client's `@atproto-labs/identity-resolver`: the builder-window race, below).
- better-auth: -TS2345 2 (lynx-store.ts:65, react-store.ts:67, a `-p` bug too): `listenKeys(store as any, keys,
  emitChange)` in an arrow passed to `useCallback<T extends Function>`. tsgo computes the arrow's return type before
  its deferred body check; that resolves the call, whose contextual return type is the arrow's own (T inferred as the
  arrow), and isResolvingReturnTypeOfSignature doesn't see the in-flight return type below the call's resolutionStart,
  so the return type is inferred again and the call resolved recursively: with one candidate that is
  NoConstraintChecks (checker.go chooseOverload, #64192), S stays the outer SomeStore and fails; the nested result
  wins (getResolvedSignature) and its TS2345 is reported. tttt computes a function's return context eagerly
  (function_impl_enter_return_context()), never re-enters, and drops nested resolutions' errors. Matching it needs
  lazy return contexts under the call's barrier, the NoConstraintChecks flag and nested results superseding the outer
  (tried and reverted 2026-10-19: the last two alone change nothing, nothing re-enters). Probe:
  `useCallback(() => listenKeys(store as any, keys, emitChange))` with `declare function useCallback<T>(cb: T): T`.
  The same family as the apollo fix (tsc re-entering what is in flight below a barrier; tttt re-enters only aliases
  below a variance measurement, `alias_reenters()`).
  Re-entrancy (notes/reentrancy.md, 2026-10-20) has the model and a census: this RETINFLIGHT -> REENTER 3 -> NESTEDSIG chain is the one
  re-entry real code does at volume (6% of projects), tttt never takes it (eager return contexts) and still matches
  everywhere else; fixing it is stages B-D (pulled return contexts), deferred.
- hono (tsconfig.build.json): +TS2769 2 (request.ts:214, a `-p` bug): `parseBody(this, options)` with `this` against
  `HonoRequest | Request`. The plain check fails in tsgo too (HonoRequest's variances [independent, bivariant],
  `I extends Input['out']` isn't `{}`); tsgo then relates getEffectiveConstraintOfIntersection's `HonoRequest<P, I> &
  this`, whose `valid` property is an intersection of two distinct function types (the declared type's own method type
  and its identity instantiation through `this`'s apparent type, which tsgo doesn't intern into one), so
  signaturesRelatedTo takes the N x M path with erasure and passes. tttt gets one signature and compares unerased:
  matching needs tsgo's non-interned identity instantiations. Reduced tree: request.ts with only parseBody
  (overloads), valid (overload returning InputToDataByTarget<I, T>) and a getter.
- hono (runtime-tests/bun): -TS7026 10, -TS7016 1: tsgo's builder resolves `hono/jsx/jsx-runtime` to the freshly
  emitted dist/types/jsx/jsx-runtime.js and finds no declaration; after the same build, `tsc -p` of that config reports
  nothing, like tttt. A tsgo builder quirk (tttt emits no JavaScript): verdict, not modelled.
- sunmao-ui: -TS2590 (chakra Button.tsx:85, Dialog.tsx:160, editor's AddTraitButton.tsx:59; `-p` too). Verdict
  (2026-10-20, instrumented tsgo): inferring JSX.LibraryManagedAttributes' conditional types from chakra's
  `ComponentWithAs` props, getReducedType -> somePropertyReducesToNever makes the intersection's `as` property, and
  createUnionOrIntersectionProperty intersects two constituents' types eagerly (exactly two: no deferred type):
  `As<any> & As` and `As<any>`, ElementType unions of ~394 members from two copies of @chakra-ui/system (distinct alias
  symbols, so distinct union objects), a 155583 cross product -> TS2590. tttt's never_reduced_prop() only intersects
  literal-like property types (a deliberate shortcut). Tried: a property exactly two constituents share whose types'
  cross product is >= 100k asks for the intersection: it doesn't fire, tttt's `as` types there are both one 178-member
  union (31684), tsc's are `As<any> & As` (an unreduced intersection of the twins) and `As<any>` (177 x 879): tttt's
  intersection of the two twin unions isn't tsc's shape. That is the next step. Repro: a probe with a node_modules
  symlink to sunmao's, `import type { ComponentWithAs, HTMLChakraProps } from '@chakra-ui/react'; interface P extends
  HTMLChakraProps<"button"> {} declare const B: ComponentWithAs<'button', P>; <B />` (the alias `type P =
  HTMLChakraProps<"button">` doesn't).

**The builder window is a race in tsgo** (atproto +TS2307 1): tsgo's 4 builders take projects in build order and wait
only on references, so whether an unreferenced project's `dist` exists is timing (atproto's oauth-client alone at
default builders reports TS2307s the whole-solution build doesn't; `--builders 1` none). tttt's window (4 places
earlier, hacks.md) gets all of atproto but this one; not worth chasing.

Perf: jest's `__tests__` builds (every package gated by TS5108) are declaration-walked only
(`program_set_declarations_only()`). Sharing parsed declaration files across a build's programs (todo.md) is
the next `-b` perf step.
