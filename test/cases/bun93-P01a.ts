// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "target": "es2022" } }
// @filename: a.ts
export {};
declare const p: Promise<string>, q: Promise<number>, x: any;
const o1 = { [await p]: 1 };
const o2 = { [await p]: await q };
const o3 = { [await p]: await (q) };
const { [await p]: a } = x;
class C { [await p] = 1 }
const sentinel: number = "";
