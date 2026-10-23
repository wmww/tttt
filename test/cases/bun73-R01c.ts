// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
type U = string | boolean;
function afterLoop(x: U) {
  if (x) { x = true; }
  while (c()) { c(); }
  const p: number = x;
}
function afterIf(x: U) {
  if (x) { x = true; }
  if (c()) { c(); }
  const p: number = x;
}
function afterTry(x: U) {
  if (x) { x = true; }
  try { c(); } catch { }
  const p: number = x;
}
interface Options { verbose?: Flag }
type Flag = boolean | "auto" | undefined;
function likely(o: Options, args: string[]) {
  let verbose: Flag = o.verbose;
  if (verbose === "auto") { verbose = false; }
  for (const a of args) { c(); }
  const n: number = verbose;
}
const sentinel: number = "";
