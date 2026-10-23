// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
// ky: source/types/common.ts (`_?: never` became `_: never`), source/types/options.ts, source/core/Ky.ts(397,4)
type LiteralUnion<L extends B, B extends string> = L | (B & { _: never });
type HttpMethod = LiteralUnion<"get" | "post", string>;
type KyOptions = { json?: unknown };
interface Options extends KyOptions {
  method?: LiteralUnion<HttpMethod, string> | undefined;
}
declare const s: string;
export const ky: Options = { method: s };

// The mechanism: an alias applied to a union that already has everything the alias adds.
type Maybe<T> = T | undefined;
type Handler = Maybe<() => void>;
namespace N {
  export type Local = Handler | undefined;
}
type Wrap<T> = { x: T };
interface G<T> {
  t: T;
  a: Maybe<Handler>;
  b: Maybe<Maybe<{ a: 1 }>>;
  c: Maybe<Maybe<Maybe<{ a: 1 }[]>>>;
  d: Wrap<Maybe<Handler>>;
  e: N.Local;
  // Controls, the same in both tools.
  f: Maybe<Maybe<{ a: T }>>;
  g: Maybe<Maybe<string>>;
  h: { x: Maybe<Handler> };
}
declare const g: G<number>;
export const a: 0 = g.a;
export const b: 0 = g.b;
export const c: 0 = g.c;
export const d: 0 = g.d;
export const e: 0 = g.e;
export const f: 0 = g.f;
export const g1: 0 = g.g;
export const h: 0 = g.h;

// A class has a `this` type, so every member whose type node is not "thisless" is instantiated.
class C {
  a: readonly Maybe<Handler>[] = [];
  b: [Maybe<Handler>] = [undefined];
  c: Promise<Maybe<Handler>> | null = null;
}
declare const o: C;
o.a = 1;
o.b = 1;
o.c = 1;

// The signature of a generic function, instantiated by a call.
declare function ret<T>(t: T): Maybe<Handler>;
declare function par<T>(t: T, m: Maybe<Handler>): void;
export const r: 0 = ret(1);
par(1, 1);
const sentinel: number = "";
