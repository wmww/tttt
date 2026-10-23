// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C<T> {
  m1(x = 1): void {}
}
type U = C<string>["m1"] | C<number>["m1"];
const n: never = null! as U;
const sentinel: number = "";
