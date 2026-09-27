# Plan 31: a fourth corpus (server-side, Node, decorators)

Written by the insights of 2026-09-27. Short on purpose: the executor picks the project and writes the findings into
plan 19, like plan 25 did for React.

## Why
The three corpora are nearly drained (tsc 0, react 1) or blocked on the type-core ports of plans 29/30 (type-fest,
valibot). A drained corpus stops finding bugs; the React corpus found 362 diagnostics and five type-core rules
(R10-R14) on its first run. Nothing we check today is server-side code: `@types/node` (huge overload sets, `Buffer`,
`EventEmitter` generics, module augmentation of globals), an ORM's or validator's types, `experimentalDecorators` +
`emitDecoratorMetadata`, `module: nodenext` / CommonJS output settings, class-heavy code (DI containers, abstract
bases, mixins). Decorator checking is listed as absent in vision.md; a real project tells us which of it matters.

## What
1. Pick a project: an application, not a library (applications use libraries the way users do), clean under tsc 5.8
   with its own tsconfig, ~20k-100k lines with dependencies, class/decorator heavy, `@types/node`. Candidates to look
   at: a NestJS app (the NestJS "realworld" examples, TypeORM or Prisma based), an Express/Fastify + zod/tRPC backend.
   Avoid projects whose type-checking needs a code generation step (Prisma client generation needs `prisma generate`,
   an install script we skip: pick one that commits its generated code or doesn't need it).
2. Add it to `thirdparty/ensure-corpora.sh` (pinned commit + committed lockfile), `./realworld.py --corpus NAME`,
   `./bench.sh`. Check that tsc 5.8 reports zero errors with the project's tsconfig (`--noEmit`); if it doesn't, fix
   the setup or pick another project.
3. First run: group by root cause (sample, reduce to probes in `test/probes/plan31/`, find tsc's rule), write the
   ranking into plan 19 as R22+ with counts, like the React section. Fix what is cheap in the same session (plan 25
   took 362 -> 80 on the first day); leave the rest ranked.
4. Record tttt vs tsc vs tsgo times and RSS on it in `measurements.md`.

## Validation
The corpus is fetched by `ensure-corpora.sh` on a fresh checkout, `./realworld.py --corpus NAME` runs, tsc is clean on
it, plan 19 has its ranked root causes, `measurements.md` has its numbers, vision.md's corpus line includes it.
