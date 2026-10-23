// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Bad { then(): void }
declare const u: string | Bad;
async function f() {
  await u;
  await u;
}
const sentinel: number = "";
