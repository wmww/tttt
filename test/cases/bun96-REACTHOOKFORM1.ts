// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { Message, Rule } from './types';
declare function get<T>(rule: Rule<T> | Message<T>): void;
declare const rule: Rule<number> | undefined;
get<number>(rule);
const sentinel: number = "";
// @filename: b.ts
import type { Ctor, Dict, Fn, Message, Renamed, Rule, Two, ViaCtor, ViaDict, ViaFn, ViaTwo } from './barrel';
function f<T, K extends string>(
  literal: Rule<T> | Message<T>,
  renamed: Rule<T> | Renamed<T>,
  fn: ViaFn<T> | Fn<T>,
  ctor: ViaCtor<T> | Ctor<T>,
  dict: ViaDict<K, T> | Dict<K, T>,
  two: ViaTwo<T> | Two<T, T>,
  // Two types in tsc too: the alias type arguments are those written at the reference.
  one: ViaTwo<T> | Two<T>,
) {
  literal = null;
  renamed = null;
  fn = null;
  ctor = null;
  dict = null;
  two = null;
  one = null;
}
export const sentinel: number = "";
// @filename: barrel.ts
export * from './types';
export type { Message as Renamed } from './types';
// @filename: types.ts
export type Rule<T> = T | Message<T>;
export type Message<T> = { value: T };

// b.ts: the other kinds of alias body
export type Fn<T> = (x: T) => void;
export type Ctor<T> = new (x: T) => object;
export type Dict<K extends string, V> = { [P in K]: V };
export type Two<A, B = A> = { a: A; b: B };
export type ViaFn<T> = 0 | Fn<T>;
export type ViaCtor<T> = 0 | Ctor<T>;
export type ViaDict<K extends string, V> = 0 | Dict<K, V>;
export type ViaTwo<A> = 0 | Two<A>;
