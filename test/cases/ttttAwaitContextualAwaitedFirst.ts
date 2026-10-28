// @strict: true
// @target: es2022
interface Bad { then(): void }
type U = number[] | Bad;
declare const u: U;
async function f() {
  const x: U = await [];
  await u;
}
const sentinel: number = "";

interface Bad2 { then(): void }
type U2 = number[] | Bad2;
declare const u2: U2;
async function g() {
  const x: U2 = await u2;
  await u2;
}
