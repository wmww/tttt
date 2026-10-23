// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext" } }
// @filename: a.ts
export {};
type P6<T> = Promise<Promise<Promise<Promise<Promise<Promise<T>>>>>>;
declare const p42: P6<P6<P6<P6<P6<P6<P6<number>>>>>>>;
const a: string = await p42;
const sentinel: number = "";
