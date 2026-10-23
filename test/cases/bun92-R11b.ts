// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T1 = [0,0,0,0,0,0,0,0,0,0]; type T2 = [...T1,...T1,...T1,...T1,...T1,...T1,...T1,...T1,...T1,...T1]; type T3 = [...T2,...T2,...T2,...T2,...T2,...T2,...T2,...T2,...T2,...T2];
type G<X extends string, Y extends any[] = T3> = [...Y,...Y,...Y,...Y,...Y,...Y,...Y,...Y,...Y,...Y, X];
declare function f<T>(x?: any): T;
declare function tag<T>(s: TemplateStringsArray): T;
declare class K<T> { k: T }
declare const e: any;
interface IG<T> { g: T }
class C33 implements IG<G<"33">> { g = e }
export const sentinel: number = "";
