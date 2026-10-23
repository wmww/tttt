// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "target": "es2022" } }
// @filename: a.ts
export {};
declare const p: Promise<string>, q: Promise<number>;
const o = { [await (p)]: await (q) };
const o2 = [await (q), { [await (p)]: 1 }];
const o3 = { [(await (p))]: await (q) };
const sentinel: number = "";
