# Source sweep checker leftovers of agent ss-b (round 2)

Unreduced or order-dependent findings on the +TS2339/+TS7006/+TS2349/+TS2698 keys, for a future session.

- **+TS2698 `{ ...where }` with `where: FilterQuery<Entity>`** (mikro-orm: 7 in each of ~20 projects, trpc's heyapi
  `client.gen.ts:248` `{ ..._config, ...options }` in 12). tsc's isValidSpreadType takes the base constraint of each
  union member; mikro-orm's `FilterQuery<T> = ObjectQuery<T> | NonNullable<ExpandScalar<Primary<T>>> | ...`. A cut-down
  copy of those aliases (`/tmp` probe sp1.ts in the session) is clean in both, so it depends on the real
  ObjectQuery/Primary. Reduce from `mikro-orm/packages/core/src/EntityManager.ts:287`.
- **react-navigation `example/__typechecks__/common.check.tsx:1501` TS2349** (`expectTypeOf(navigation.setParams)
  .parameter(0).toBeUndefined()` not callable) only in the root project: checking common.check.tsx alone (a mirror
  project with only that file) matches tsc, so it depends on what's checked before it.
- **discord.js root `REST.ts:128` and `structures/__tests__/types/channels.test-d.ts:8`**: only in the broken root
  project (no jsx setting, everything included).
- **redux-toolkit `combineSlices.ts:120` +TS2344 `Sl` vs `AnySliceLike`** (relations, agent A's key).
- Display: a method's instantiated type parameter defaults print uninstantiated (`<EO = O>` for `<EO = any>`), a
  default type argument is left out (`E<EO>` for `E<EO, Cfg<EO>>`), and `PayloadAction<..., never>` prints a defaulted
  `never` argument tsc leaves out.
