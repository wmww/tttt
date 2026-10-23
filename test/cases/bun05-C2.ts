// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface AL<T> extends Array<T> {
  filter(cb: (x: T, i: number, a: T[]) => unknown, thisArg?: any): AL<T>;
  filter<S extends T>(cb: (x: T, i: number, a: T[]) => x is S, thisArg?: any): AL<S>;
}
declare const u8: AL<number> | string[];
u8.filter(x => true);
const sentinel: number = "";
