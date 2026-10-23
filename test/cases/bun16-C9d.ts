// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const p: { foo(): void };
p.foo && (({ foo: f } = p) => f);
p.foo && (() => { const { foo: f } = p; });
const sentinel: number = "";
