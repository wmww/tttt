// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const x: { f(): void };
(0, (x.f))();
((0), x.f)();
((0, x.f))();
const sentinel: number = "";
