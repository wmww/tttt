// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Bad { then(): void }
type U = number[] | Bad;
declare const u: U;
async function f() {
  const x: U = await [];
  await u;
}
const sentinel: number = "";
