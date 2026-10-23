// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
async function* a1() { yield { then(x: number) { } }; }
declare const abad: AsyncIterable<{ then(x: number): void }>;
async function* a2() { yield* abad; }
const sentinel: number = "";
