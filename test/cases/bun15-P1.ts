// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type G1 = () => Generator<number, void, string>;
type G2 = () => Generator<number, void, boolean>;
declare function f(...args: [...G1[], G2]): void;
f(function* () { const x = yield 1; const b: boolean = x; });
const sentinel: number = "";
