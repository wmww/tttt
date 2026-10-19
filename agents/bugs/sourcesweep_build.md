# The source sweep's solutions: `tsc -b` against `tttt -b`
`scripts/sourcesweep.py --build` (2026-10-19, refresh 3): every `tsconfig*.json` with `references` that no other
config of its repo references, built by tsgo with `-b --force` in a bwrap overlay of `thirdparty/sources` (cache
`test/sourcesweep/tsc-build.jsonl.gz`, `--build --tsc[-new]`) and checked by `tttt -b --no-write`. **394 / 416** after
the day-one fixes (TS18003 for inherited `files`, TS18002 with inputs, the sandbox's writes outside a repo).

What it sees that the `-p` sweep doesn't: configs not named `tsconfig.json` (`tsconfig.build.json`, `.spec.json`,
`.lib.json`, `.app.json`: the `-p` sweep only walks `tsconfig.json`), and downstream projects reading upstream ones.
**First question for each: does `tttt -p THAT_CONFIG --noEmit` differ from tsc's too?** Then it is a plain checker bug
the `-p` sweep never reached (hono's `tsconfig.build.json`: jwt.ts's TS2339, request.ts's TS2769, css.test.tsx's
TS2783 are all `-p` mismatches). Otherwise it's `-b`'s design (documentation.md "Divergences of `-b` by design":
references read as sources typed under the referencing program's options, never as tsc's freshly emitted `.d.ts`):
decide whether tsc's emitted-declaration reading shows (TS2883/TS4058 naming, TS2307 into outputs, errors *in*
`dist/*.d.ts`) and whether the design should change.

Day one, by project (+ spurious, - missing, counts):
- apollo-client/integration-tests/type-tests: +TS2344 11, +TS2578 6, -TS2578 4, -TS2769 4 (type tests)
- astro: +TS2578 23, +TS2307 15 (`@astrojs/prism/dist/highlighter`: an output path), +TS2322 5, +TS2741 4, +TS6307 1
- atproto: +TS2307 36, +TS7006 9, +TS2883 3, +TS4112 3, +TS2339 (`never`), +TS2551, +TS2366, +TS2882 (dev-env/bin.js
  `./dist/bin.js`)
- babel: +TS2775 13 (assertion signatures need explicit annotations: read from source, not .d.ts), +TS2590 1
- base-ui: +TS2322 7, +TS2590 1
- better-auth: -TS7006 258, +TS2883 150, -TS2578 18, -TS2554 4
- commitlint: +TS2307 2; dyad: -TS6142 1; graphql-inspector: +TS2883 1; ghostfolio/apps/client +TS2322 6, libs/ui +TS4058 1
- hono (both roots): -TS7026 10 + -TS7016 (runtime-tests/bun), +TS2339 4, +TS2769 2, +TS2783 1 (the `-p` bugs above)
- lightweight-charts/tsconfig.composite.json: +TS2339 8, +TS2322 7
- lit: -TS2307 16 in `lit2/.../*.d.ts` (four test configs), lit-element -TS2882 2 (`development/polyfill-support.d.ts`)
- sunmao-ui: -TS2590 7 (chakra Button.tsx; the `-p` sweep misses it too)
- vue-i18n/examples/lazy-loading/vite: +TS2339 2, -TS2551 2
- zigbee2mqtt/test: -TS2304 129, -TS2503 77, -TS2833 1 in `dist/*.d.ts` (tsc reads the outputs it just emitted, with
  the test config's options), -TS7006 10

Perf: jest's `__tests__` builds (every package gated by TS5108) were fully checked for their declarations (1.5M types,
~10 s); now declaration-walked only (`program_set_declarations_only()`, 10K types, ~0.75 s against tsc's 0.5 s: the
rest is parsing and binding 25 programs). atproto 47 s / 30 s, astro 30 s / 20 s (single-threaded tttt against tsgo's default -b).
