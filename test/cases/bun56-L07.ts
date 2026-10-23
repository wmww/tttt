// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export {};
interface M<T> { m: T }
declare const K: new () => {};
type U = 1;
export class C implements (M)<Nope> {}
export interface X extends (M)<Nope2> {}
export class D implements K()<Nope3> {}
export interface Y extends M[0]<Nope4> {}
export class F implements (M)<Array<Nope5>, { a: typeof nope6 }> {}
export class G implements (M)<U> {}
export const sentinel: number = "";
