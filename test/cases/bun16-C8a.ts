// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F = () => void;
declare const r1: Record<string, F>;
if (r1.foo) {}
declare const r3: { [k: string]: F } | { [k: string]: () => number };
if (r3.foo) {}
function g<T extends Record<string, F>>(t: T) { if (t.foo) {} }
const sentinel: number = "";
