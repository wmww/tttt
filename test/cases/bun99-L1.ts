// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
interface Matcher<T> { match(other: T): boolean }
type With<T> = T | Matcher<unknown>;
type Deep<T> = T extends Array<infer E> ? With<T> | Deep<E>[] : T extends object ? With<T> | { [K in keyof T]: Deep<T[K]> } : With<T>;
declare function containing<T = any>(expected: Deep<T>): any;
declare function strict<T>(expected: Deep<T>): T;
declare const four: { a: string; b?: undefined; c?: undefined } | { b: string; a?: undefined; c?: undefined } | { c: string; a?: undefined; b?: undefined } | { a?: undefined; b?: undefined; c?: undefined };
declare const two: { a: string } | { a?: undefined };
containing({ id: "x", empty: {}, ...four });
containing({ empty: {}, ...two });
containing({ ...two, empty: {} });
containing({ nested: { empty: {} }, ...two });
containing({ list: [{}], ...two });
export const v1: never = strict({ empty: {}, ...two });
export const v2: never = strict({ id: "x", empty: {}, other: {}, ...four });
const sentinel: number = "";
