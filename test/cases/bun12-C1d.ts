// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Alt<A, B> = { [K in keyof A]: Alt<B, A> };
declare const x: Alt<{ a: 1 }, { b: 1 }>;
x.a.b;
const sentinel: number = "";
