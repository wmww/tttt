# `OmitConstructors<typeof Model>` is any in sequelize (imported alias), fine as a local copy

Source sweep, sequelize (`-TS2488`, `-TS2339` in `packages/core`): `target.sequelize` with `target: ModelStatic` is
`any` in tttt, `Sequelize<AbstractDialect<object, object>>` in tsc, so `for (const model of sequelize.models)` and
friends lose their errors. `ModelStatic<M> = OmitConstructors<typeof Model> & { new (): M }` (`src/model.d.ts`),
`OmitConstructors<T> = Pick<T, NonConstructorKeys<T>>` with
`NonConstructorKeys<T> = { [P in keyof T]: T[P] extends new () => any ? never : P }[keyof T]` (`src/utils/types.ts`).

Repro (a project under /tmp whose tsconfig extends `sequelize/packages/core/tsconfig.json`, `files: ["probe2.ts"]`,
`rootDir: "/"`):

```ts
import type { Model } from "<abs>/sequelize/packages/core/src/model";
import type { OmitConstructors } from "<abs>/sequelize/packages/core/src/utils/types.js";
declare const o: OmitConstructors<typeof Model>;
export const s3 = o.sequelize;   // tsc: Sequelize<AbstractDialect<object, object>>, tttt: any
```

Checked so far: `Pick<typeof Model, NCK>` written out in the probe file, and a local
`type OC<T> = Pick<T, NCK2<T>>` (same definition) both give the right type; `(typeof Model)["sequelize"]`,
`keyof typeof Model` (110 keys) and the mapped type's members match tsc. Only the imported alias is any, which smells
like an instantiation cached while `typeof Model`'s static side was still being resolved (model.d.ts uses
`ModelStatic` inside `Model` itself), or an alias-instantiation key collision. `TTTT_DEBUG_CYCLE=1` prints nothing.
utils/types.ts also has an unresolved import (`@sequelize/utils`, NonUndefined/PartialBy), possibly relevant.

Narrowed further: importing the same two aliases from a fresh file (`t2.ts`, copied definitions) gives the right
type, importing them from sequelize's `utils/types.ts` gives any. So it's the *shared* instantiation
`OmitConstructors<typeof Model>` that model.d.ts itself creates (through `ModelStatic`, used inside `Model`'s
statics) that is wrong: most likely computed while `typeof Model` was in flight and cached as any. Circularity
handling (agent C's area during the plan-52 fan-out).
