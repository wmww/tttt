// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"] } }
// @filename: a.ts
type A = { kind: "a"; x: 1 };
type B = { kind: "b"; y: 2 };
declare function id<T>(value: T): T;
declare function use<T>(select: () => T): { data: NoInfer<T> };
declare function both(): A & B;
declare const n: NoInfer<A & B>;
declare const m: NoInfer<A & B> | undefined;
const { data } = use(both);
type Cond<T> = T extends unknown ? A & B : never;
declare const c: NoInfer<Cond<1>>;
export const v1: never = { id: 1, ...n };
export const v2: never = id({ id: 1, ...n });
export const v3: never = { ...n, id: 1 };
export const v4: never = id(m ? { id: 1, ...m } : undefined);
export const v5: never = id({ id: 1, ...data });
export const v6: never = { id: 1, ...c };
export const v7: { id: string } = { id: 1, ...n };
export const v8: { id: string } = id({ id: 1, ...n });
export const v9 = { id: 1, ...n }.nope;
const sentinel: number = "";
