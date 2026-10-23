// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "esnext", "module": "esnext" } }
// @filename: a.ts
export {};
interface BadP { then(cb: (v: BadP) => void): void }
declare const it: Iterable<BadP>;
for await (const x of it) {}
const sentinel: number = "";
