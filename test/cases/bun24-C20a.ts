// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const us: unique symbol; declare const ut: unique symbol;
declare let a1: (x: unknown) => x is typeof us; declare let a2: (x: unknown) => x is typeof ut;
a1 = a2;
const sentinel: number = "";
