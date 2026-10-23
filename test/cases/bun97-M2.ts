// @filename: tsconfig.json
{
 "compilerOptions": {
  "strict": true,
  "noEmit": true,
  "target": "esnext",
  "module": "esnext",
  "moduleResolution": "bundler",
  "lib": [
   "esnext"
  ],
  "types": [],
  "skipLibCheck": true
 }
}
// @filename: a.ts
import { FieldPath, Values, get } from "./lib";
// Used as it is written: the alias of the import.
export function f<V extends Values>(b: FieldPath<V>) {
  const n: number = b;
}
// @filename: b.ts
import { FieldPath, Values, get } from "./lib";
// Instantiated, by a mapper that does not map V: the alias of the conditional type itself.
export function f<V extends Values>(bs: ReadonlyArray<FieldPath<V>>) {
  bs.map(name => get({}, name));
}
// @filename: c.ts
import { FieldPath, Values, get } from "./lib";
declare function isString(v: unknown): v is string;
export function f<V extends Values>() {
  return (names?: FieldPath<V> | ReadonlyArray<FieldPath<V>>) =>
    names === undefined ? 1 : isString(names) ? get({}, names) : names.map(name => get({}, name));
}
// @filename: f.ts
import { FieldPath, Values, get } from "./lib";
export function f<V extends Values>(names: FieldPath<V> | ReadonlyArray<FieldPath<V>>) {
  return typeof names === "string" ? 1 : names.map(name => get({}, name));
}
// @filename: g.ts
import { FieldPath, Values, get } from "./lib";
export function f<V extends Values>(names: FieldPath<V> | ReadonlyArray<FieldPath<V>>) {
  get({}, names);
}
// @filename: lib.ts
type Inner<T> = keyof T & string;
export type Values = Record<string, any>;
export type Path<T> = T extends any ? Inner<T> : never;
export type FieldPath<T extends Values> = Path<T>;
export declare function get(o: unknown, p?: number | null): unknown;
// @filename: z.ts
export const sentinel: number = "";
