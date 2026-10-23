// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let f1: (a: number, ...rest: [...string[], boolean]) => void;
declare let f2: (...args: readonly [number, ...string[], boolean]) => void;
f2 = f1;
const sentinel: number = "";
