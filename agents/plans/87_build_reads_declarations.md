# Plan 87: `-b` reads its references' emitted declarations (planning stub)

Written by the insights of 2026-10-19. **A planning session researches, prototypes and either writes the real plan
here or closes it with a no-go and the numbers.**

## Why
tttt's `-b` (and `-p` with `references`) types a referenced project by reading its *sources* under the reader's
options; tsc reads the reference's emitted `.d.ts` at its output path. The twenty-second window approximated the
`.d.ts` view three times on top of sources: the builder window (hacks.md, `build()` in main.c: an unreferenced
project's outputs read when it is 4 places earlier), a reference source's private members untyped to readers, and
`stripInternal` type literal members stripped. What is still missing can't be approximated from sources at all
(bugs/sourcesweep_build.md): errors *inside* emitted declarations (lit, lit-element, zigbee2mqtt/test: resolution from
the output path, the project's ambient inputs absent), imports only a body uses (astro: `.d.ts` elides them, so their
augmentations never load), and tsgo's emit bugs leaking downstream (better-auth's 258 TS7006). Each new divergence of
this family is another approximation; that is the pattern to stop.

tttt's emit is byte-identical on 100771 / 100826 dtsdiff files, so the mechanism exists: build in reference order,
emit each project's declarations in memory, hand them to downstream programs as files at their output paths (when
`--no-write`, too). Likely side effects: TS6305 and "typed under the reader's strictness" disappear as divergences
(documentation.md "Divergences of `-b` by design"); downstream checks get cheaper (declarations, not inferred
sources); the build gets an ordering constraint (a program waits on its references' emit, as tsgo's builders do).

## Questions for the planning session
- What does tsgo do exactly: `-b` vs `-p` with references (built outputs present or not: TS6305), what
  `useSourceOfProjectReferenceRedirect`-style behaviour survives in tsgo 7.1, and where the emitted file's diagnostics
  are reported.
- Parallelism: today all programs are built at once; with emit dependencies, how much wall does the critical path
  cost on tsc-src and typescript-eslint `-b` and the sweep's biggest solutions (atproto, astro)? Does checking against
  declarations win it back?
- Memory: emitted texts held in memory until the last reader; their ASTs parsed per reader or shared.
- What it deletes: the builder window, the reference-source private/internal rules, anything else in hacks.md and
  documentation.md that models "a reference read as sources". Prototype the deletion first (plan 47's rule).
- The 55 files tttt emits differently from tsgo: they'd now change downstream diagnostics. Measure on the `-b` sweep.

## Gate
Go if a prototype on the `-b` sweep (`scripts/sourcesweep.py --build`) gains without losing (400 / 416 today), the
scenarios hold (210 / 0), tsc-src and typescript-eslint `-b` stay clean, and wall at the defaults on those two is no
worse than today's by more than ~5%. Otherwise write the numbers here and close it.
