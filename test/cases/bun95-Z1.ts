// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noErrorTruncation": true } }
// @filename: S136.txt
a.ts(13,7): error TS2322: Type 'null' is not assignable to type '(U | {})[] | U'.
a.ts(38,7): error TS2322: Type 'null' is not assignable to type '{ [x: string]: any; [x: number]: any; [x: symbol]: any; } | ((never[] | {} | { [x: string]: any; [x: number]: any; [x: symbol]: any; }) & {})'.
a.ts(39,7): error TS2322: Type 'string' is not assignable to type 'number'.
// @filename: a.ts
// `ReturnType<typeof f>` is `any` in tsc: `f`, instantiated in the context of `(...args: any) => R` (O = any), returns `M<any>`,
// which is not assignable to R = `M<'a' | 'b'>`, so `T extends (...args: any) => infer R` is definitely false.
type U = { [k: string]: unknown };
type M<S> = S extends 1 ? { [P in keyof S]: M<S>[] }[keyof S] & {} : U;
type K = 'a' | 'b';
declare function f<O extends K>(op: O): M<O>;
const p1: ReturnType<typeof f> = 0;
declare function g<O extends K>(op: O): <Z>(o: Z) => M<O>;
const p2: ReturnType<typeof g> = 0;
declare const o: { m<O extends K>(op: O): M<O>; a: <O extends K>(op: O) => M<O> };
const p3: ReturnType<typeof o.m> = 0;
const p4: ReturnType<typeof o.a> = 0;
const p5: M<any> = null;

// The shape it was found in: the header that graphql-zeus generates.
type SD = Record<string, { decode?: (s: unknown) => unknown }>;
type DeepAnify<T> = { [P in keyof T]?: any };
type IsArray<T, V, SCLR extends SD> = T extends Array<infer R> ? MapType<R, V, SCLR>[] : MapType<T, V, SCLR>;
type FlattenArray<T> = T extends Array<infer R> ? R : T;
type IsInterfaced<SRC extends DeepAnify<DST>, DST, SCLR extends SD> = FlattenArray<SRC> extends never
  ? { [P in keyof SRC]: SRC[P] extends '__union' & infer R
        ? P extends keyof DST ? IsArray<R, '__typename' extends keyof DST ? DST[P] & { __typename: true } : DST[P], SCLR>
                              : IsArray<R, '__typename' extends keyof DST ? { __typename: true } : Record<string, never>, SCLR>
        : never }[keyof SRC] &
    { [P in keyof Omit<Pick<SRC, { [P in keyof DST]: SRC[P] extends '__union' & infer R ? never : P }[keyof DST]>, '__typename'>]:
        DST[P] extends boolean ? SRC[P] : IsArray<SRC[P], DST[P], SCLR> }
  : { [P in keyof Pick<SRC, keyof DST>]: DST[P] extends boolean ? SRC[P] : IsArray<SRC[P], DST[P], SCLR> };
type MapType<SRC, DST, SCLR extends SD> = SRC extends DeepAnify<DST> ? IsInterfaced<SRC, DST, SCLR> : never;
type Q = {
  ['Query']: { __typename: 'Query'; viewer: Q['User'] };
  ['Mutation']: { __typename: 'Mutation'; addReview?: Q['User'] | undefined };
  ['User']: { __typename: 'User'; login: string };
};
type Ops = { query: 'Query', mutation: 'Mutation' };
type G<O> = O extends keyof Ops ? Ops[O] : never;
declare function c6<O extends keyof Ops>(op: O): <Z>(o: Z) => MapType<Q[G<O>], Z, {}>;
const p6: ReturnType<typeof c6> = 0;
const p7: MapType<any, any, {}> = null;
const sentinel: number = '';
