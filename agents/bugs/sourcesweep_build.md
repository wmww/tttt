# The source sweep's solutions: `tsc -b` against `tttt -b`
`scripts/sourcesweep.py --build` (2026-10-19, refresh 3): every `tsconfig*.json` with `references` that no other
config of its repo references, built by tsgo with `-b --force` in a bwrap overlay of `thirdparty/sources` (cache
`test/sourcesweep/tsc-build.jsonl.gz`, `--build --tsc[-new]`) and checked by `tttt -b --no-write`. 394 / 416 after
the day-one fixes (TS18003 for inherited `files`, TS18002 with inputs, the sandbox's writes outside a repo), **399 / 416**
after the 2026-10-19 fan-out (the builder window, reference sources' private members and `@internal` type literal
members, declaration naming, and plain checker bugs).

**Plan 87 (2026-10-19) supersedes the "by design" verdicts below**: under `TTTT_BUILD_EMITTED=1` (stage 1 makes it
the only `-b`) better-auth, lit ×5, zigbee2mqtt/test and astro's remark tests match (406 / 416). Re-triage what's left
under that model after stage 1; the verdicts on emitted declarations and body-only imports are kept until then.

What it sees that the `-p` sweep doesn't: downstream projects reading upstream ones (the `-p` sweep takes every
`tsconfig*.json` since 2026-10-19).
**First question for each: does `tttt -p THAT_CONFIG --noEmit` differ from tsc's too?** Then it is a plain checker bug
the `-p` sweep never reached (hono's `tsconfig.build.json`: jwt.ts's TS2339, request.ts's TS2769, css.test.tsx's
TS2783 are all `-p` mismatches). Otherwise it's `-b`'s design (documentation.md "Divergences of `-b` by design":
references read as sources typed under the referencing program's options, never as tsc's freshly emitted `.d.ts`):
decide whether tsc's emitted-declaration reading shows (TS2307 into outputs, errors *in* `dist/*.d.ts`). The
TS2883/TS4058 naming ones (better-auth, graphql-inspector, atproto, ghostfolio) were plain declaration emit bugs only
`-b` reached (`-p` is implicitly `--noEmit`): a referenced project's output path as a module specifier, a
destructuring's elements typed one by one, a reused name renamed through a non-portable `import()` and whether the design should change.

Day one, by project (+ spurious, - missing, counts):
- apollo-client/integration-tests/type-tests: +TS2344 11, +TS2578 6, -TS2578 4, -TS2769 4, +TS2322 3, +TS7006 2: `-b`
  only (`-p` of tsconfig.src.json and cacheOverride/classicSignature match tsc). The cacheOverride tests swap Apollo's
  cache type through module augmentation in a referenced project (cacheOverride/shared): which augmentations a program
  sees differs between referenced sources (tttt) and their emitted `.d.ts` (tsc). Not investigated further
- astro: +TS2322 1, +TS2741 2, -TS18048 2 (agent B's verdicts below)
- atproto: +TS2554 2, +TS2322 1, +TS2307 1 (below)
- babel: matches since 2026-10-20 (the +TS2590 at babel-traverse visitors.ts:168 is gone).
- base-ui: matches (`-p` and `-b`) since 2026-10-20 (the createItems.test.tsx +TS2322 is gone). Fixed: TS2590
- better-auth: -TS7006 258, -TS2578 18, -TS2554 4, -TS2339 3, -TS7031, -TS2349: one cause, `-b` by design. tsgo's
  declaration emit of `router` (packages/better-auth/src/api/index.ts) leaks an unbound type parameter name (`{ [K in
  keyof T_2 as K extends keyof T_1 ? never : K]: Base[K] }`, from `OverrideMerge<Base, ...>`), so with skipLibCheck
  `Auth<O>["api"]` reads as `any` in every downstream program (the 258 implicit-any callback parameters, the type tests
  asserting not-any). tttt -p reading tsc's emitted outputs (bwrap overlay) reports the same 29 TS7006 on api-key's
  test config: the checker agrees. A tsgo emit bug only reading emitted declarations would reproduce.
  -TS2345 2 (lynx-store.ts:65, react-store.ts:67, a `-p` bug too): `listenKeys(store as any, keys, emitChange)` in an
  arrow passed to `useCallback<T extends Function>`. tsgo computes the arrow's return type before its deferred body
  check; that resolves the call, whose contextual return type is the arrow's own (T inferred as the arrow), and
  isResolvingReturnTypeOfSignature doesn't see the in-flight return type below the call's resolutionStart, so the
  return type is inferred again and the call resolved recursively: with one candidate that is NoConstraintChecks
  (checker.go chooseOverload, #64192), S stays the outer SomeStore and fails; the nested result wins
  (getResolvedSignature) and its TS2345 is reported. tttt computes a function's return context eagerly
  (function_impl_enter_return_context()), never re-enters, and drops nested resolutions' errors. Matching it needs
  lazy return contexts under the call's barrier, the NoConstraintChecks flag and nested results superseding the outer
  (tried and reverted 2026-10-19: the last two alone change nothing, nothing re-enters). Probe:
  `useCallback(() => listenKeys(store as any, keys, emitChange))` with `declare function useCallback<T>(cb: T): T`.
  -TS5115 2 (to-zod.ts, tsc prints it once per program typing the file): appeared with the day's merges (not agent A's
  commits: ai-main 36c9407c9 already shows it)
- hono: fixed TS2339 (jwt.ts), TS2783 (css.test.tsx). Left: +TS2769 2 (request.ts:214, a `-p` bug of
  tsconfig.build.json): `parseBody(this, options)` with `this` against `HonoRequest | Request`. The plain check fails in
  tsgo too (HonoRequest's variances [independent, bivariant], `I extends Input['out']` isn't `{}`); tsgo then relates
  getEffectiveConstraintOfIntersection's `HonoRequest<P, I> & this`, whose `valid` property is an intersection of two
  distinct function types (the declared type's own method type and its identity instantiation through `this`'s
  apparent type, which tsgo doesn't intern into one), so signaturesRelatedTo takes the N x M path with erasure and
  passes. tttt gets one signature and compares unerased: matching needs tsgo's non-interned identity instantiations.
  Reduced tree: request.ts with only parseBody (overloads), valid (overload returning InputToDataByTarget<I, T>) and a
  getter. -TS7026 10 + -TS7016 (runtime-tests/bun, `-b` only): tsgo's builder resolves `hono/jsx/jsx-runtime` to the
  freshly emitted dist/types/jsx/jsx-runtime.js and finds no declaration; after the same build, `tsc -p` of that config
  reports nothing, like tttt. A tsgo builder quirk: verdict, not modelled
- sunmao-ui: -TS2590 (chakra Button.tsx:85, Dialog.tsx:160, editor's AddTraitButton.tsx:59; `-p` too). Verdict
  (agent B 2026-10-20, instrumented tsgo): inferring JSX.LibraryManagedAttributes' conditional types from chakra's
  `ComponentWithAs` props, getReducedType -> somePropertyReducesToNever makes the intersection's `as` property, and
  createUnionOrIntersectionProperty intersects two constituents' types eagerly (exactly two: no deferred type): `As<any>
  & As` and `As<any>`, ElementType unions of ~394 members from two copies of @chakra-ui/system (distinct alias symbols,
  so distinct union objects), a 155583 cross product -> TS2590. tttt's never_reduced_prop() only intersects literal-like
  property types (a deliberate shortcut). Tried: in never_reduced_prop(), a property exactly two constituents share
  whose types' cross product is >= 100k asks for the intersection (reports TS2590): it doesn't fire, tttt's `as` types
  there are both one 178-member union (31684), tsc's are `As<any> & As` (an unreduced intersection of the twins) and
  `As<any>` (177 x 879 = 155583): tttt's intersection of the two twin unions isn't tsc's shape. That is the next step. Repro: /tmp probe with a node_modules symlink to sunmao's, `import type { ComponentWithAs,
  HTMLChakraProps } from '@chakra-ui/react'; interface P extends HTMLChakraProps<"button"> {} declare const B:
  ComponentWithAs<'button', P>; <B />` (the alias `type P = HTMLChakraProps<"button">` doesn't).

Perf: jest's `__tests__` builds (every package gated by TS5108) were fully checked for their declarations (1.5M types,
~10 s); now declaration-walked only (`program_set_declarations_only()`, 10K types, ~0.75 s against tsc's 0.5 s: the
rest is parsing and binding 25 programs). atproto 47 s / 30 s, astro 30 s / 20 s (single-threaded tttt against tsgo's default -b).

**Verdict on errors inside emitted declarations (lit, lit-element, zigbee2mqtt/test; 2026-10-19): not modelled, by
design for now.** All three are tsc reading a referenced (or built) project's emitted `.d.ts` *as a file at its output
path*, checked under the reader's options (`skipLibCheck` false):
- lit/packages/context `tsconfig.lit2-tests.json` (and three like it): -TS2307 16 in
  `lit2/lit2-scope/context/lib/**/*.d.ts`: `@lit/reactive-element` re-resolved from the outDir, where nothing is
  installed.
- lit-element `tsconfig.json`: -TS2882 2 in `development/polyfill-support.d.ts`: its side-effect imports re-resolved
  from `development/`.
- zigbee2mqtt/test: -TS2304 129, -TS2503 77, -TS2833 1 in `dist/*.d.ts`: the root project's global types (`Group`,
  `Device`, `KeyValue`, namespace `eventdata`) live in its own ambient `.d.ts` inputs, which aren't copied to `dist`; the
  -TS7006 10 in `test/extensions/homeassistant.test.ts` are the cascade (callbacks typed by the then-`any` names).
tttt reads a reference's (and, since the builder window, a built project's) outputs as its sources, so none of this
can show: resolution from the source location, the project's own ambient inputs present, nothing reported in the
reference. Modelling it means feeding each project's emitted declaration text (in memory: tttt's emit is 100771 / 100826
files byte-identical on dtsdiff) to the programs downstream as files at their output paths. That's a different `-b`
(programs wait on their references' emit like tsgo's, not all built at once; the window would read emitted text too;
TS6305 and "typed under the reader's strictness" go away; downstream checks likely get cheaper: declarations, not
inferred sources), and the payoff in the sweep is these three repos. Worth a plan if `-b` fidelity becomes a goal of
its own (the design question for an insights session: documentation.md "Divergences of `-b` by design"); not a fix to
bolt onto the window.

**Fixed 2026-10-19 (agent B):** outputs of unreferenced projects built earlier (the builder window: atproto's and
commitlint's TS2307 and cascades, astro's `dist` imports), `stripInternal` references (astro's TS2578 x23), the
`x.constructor === C` narrowing of `this` (atproto's TS2339 `never`), `this is T` predicates (babel's TS2775), TS6142 on
import types (dyad), `.json` includes under module node20/nodenext (astro's TS6307, a `-p` bug too); then a built
project's file reached by its own path is the program's own (better-auth's TS5115 x3), a reference source's
private and #private members are untyped to its readers (relations skip their types, `x['p']` from outside is any;
astro telemetry's TS2740, base-ui's MenuHandle demos), stripInternal strips type literal members too (astro's
ImageMetadata.fsPath: the image tests' TS2322/TS2345).

**What's left of atproto, astro (agent B's verdicts):**
- **Imports only a body uses** (astro `markdown/remark` tests: +TS2322/+TS2741 on `ElementData`): tsc reads
  `dist/index.d.ts`, whose import of `rehype-raw` is elided (only `src/index.ts`'s body uses it); tttt reads
  `src/index.ts` and follows every import, so `hast-util-from-parse5` comes in and its `declare module 'hast'`
  augmentation makes `ElementData.position` required. Modelling it means loading a reference source's imports the way
  its `.d.ts` keeps them (side-effect imports, names its declarations mention, and whatever `import("...")` inferred types
  add), which needs the declaration emit's view of the file: not a cheap approximation. The same family as the verdict
  above (reading emitted declarations, not sources).
- **The builder window is a race in tsgo** (atproto +TS2307 1, oauth-client's `@atproto-labs/identity-resolver`): tsgo's
  4 builders take projects in build order and wait only on references, so whether an unreferenced project's `dist`
  exists is timing (atproto's oauth-client alone at default builders reports TS2307s the whole-solution build doesn't;
  `--builders 1` none). tttt's window (4 places earlier) gets all of atproto but this one; not worth chasing.
- atproto +TS2554 2 (lex/lex-schema/src/schema/string.test.ts:767, 777 `expectTypeOf<...>().not.toMatchObjectType<...>()`):
  `-p` against unbuilt references matches tsc (types `any`), so it shows only where `@atproto/lex-schema`'s types are
  real: either the reading of its sources differs from its `.d.ts` or it's a checker bug in expect-type's machinery.
  Not looked into. Same for atproto +TS2322 1 (oauth-provider-ui/src/components/sign-up-view.tsx:64).
