// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const x: { f(): void; g: any };
((0, x.f))();
((0), x.f)();
(0, (x.f))();
(0, (eval))("1");
((0, x.g))``;
const sentinel: number = "";
