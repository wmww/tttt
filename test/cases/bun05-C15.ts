// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const a: { b?: <T>(cb: (x: T) => void) => T };
const r1: string | undefined = a?.b(x => { const q: 0 = x; });
const sentinel: number = "";
