// origin: npm better-auth@1.7.12 https://github.com/better-auth/better-auth packages/better-auth/tsconfig.bench.json seed 20261035
// flip: no
// src/plugins/organization/adapter.ts:234: `filterOutputFields(...) as InferOrganization<O>`, a cast between
// InferOrganization<O, false> and InferOrganization<O, true>. Their alias type arguments differ only in a bivariant
// `isClientSide` (false vs true are not comparable), but the alias's SchemaName parameter is a mapped type's key in the
// conditional's extends type, and relating those identically reports it unreliable: the structural fallback relates the
// branches, which are comparable. tttt measured SchemaName without the flag and reported TS2352.
// @strict: true
type DBField = { type: string; returned?: boolean };
type F2O<F extends Record<string, DBField>> = { [K in keyof F]: string };
type RemoveRet<F extends Record<string, DBField>> = { [K in keyof F as F[K]['returned'] extends false ? never : K]: F[K] };
type IAFO<N extends string, O extends { schema?: any }, C extends boolean> =
  O['schema'] extends { [k in N]?: { additionalFields: infer F extends Record<string, DBField> } }
    ? C extends true ? F2O<RemoveRet<F>> : F2O<F>
    : {};
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type InferOrg<O extends { schema?: any }, C extends boolean = true> = Prettify<{ id: string } & IAFO<'org', O, C>>;
export function f<O extends { schema?: any }>(x: InferOrg<O, false>) { return x as InferOrg<O>; }
export function g<O extends { schema?: any }>(x: IAFO<'org', O, false>) { return x as IAFO<'org', O, true>; }
type C1<O extends { schema?: any }, C extends boolean> = O['schema'] extends { a: 1 } ? C extends true ? 1 : 2 : {};
export function h<O extends { schema?: any }>(x: C1<O, false>) { return x as C1<O, true>; }
