// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function decN(target: any, context: { name: 0 }): void;
class C {
  @decN [1]() {}
  @decN [2] = 1;
  @decN get [3]() { return 1 }
}
const sentinel: number = "";
