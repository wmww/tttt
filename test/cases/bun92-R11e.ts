// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type D = 0|1|2|3|4|5|6|7|8|9;
type G<X extends string, Y extends number = D> = `${Y}${Y}${Y}${Y}${Y}${Y}${X}`;
declare function f<T>(x?: any): T;
declare class K<T> { k: T }
declare const e: any;
interface IG<T> { g: T }
function h({ a = (p: G<"1">) => 1 }) {}
export const sentinel: number = "";
