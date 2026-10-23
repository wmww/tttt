// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f1(a: number): void;
declare function f2(a: number): void;
declare const u4: Parameters<typeof f1> | Parameters<typeof f2>;
f1(...u4);
const sentinel: number = "";
