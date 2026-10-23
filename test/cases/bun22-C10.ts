// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(a: T, b: (x: T) => void): T;
type Deep9 = ((x: number, y: string) => number)[][][][][][][][][];
const r9: Deep9 = f([[[[[[[[[(x: number, y) => y.length]]]]]]]]], x => {});
const sentinel: number = "";
