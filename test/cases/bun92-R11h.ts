// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M<T> = { [K in keyof T]: M<T>[K] };
type G<X extends string, T = { a: X }> = M<T>[keyof T];
declare function f<T>(x?: any): T;
declare function tag<T>(s: TemplateStringsArray): T;
declare class K<T> { k: T }
declare const e: any;
interface IG<T> { g: T }
interface I34 extends IG<G<"34">> {}
export const sentinel: number = "";
