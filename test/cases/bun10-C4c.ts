// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface G<T> { (x: T): void }
interface A4 extends G<string>, G<number>, G<string> {}
declare const a4: A4;
declare function call<T>(f: (x: T) => void): T;
const t: boolean = call(a4);
const sentinel: number = "";
