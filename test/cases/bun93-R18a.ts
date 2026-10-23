// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Bad { then(): void }
declare const p: Promise<string | Bad>; declare const u: string | Bad;
async function f() { await p; await u; }
interface Bad2 { then(): void; x: 1 }
declare const p2: Promise<string | Bad2>; declare const u2: string | Bad2;
async function g() { await u2; await p2; }
type S = string | Promise<S>;
declare const s: S; declare const ps: Promise<S>;
async function h() { await ps; await s; }
const sentinel: number = "";
