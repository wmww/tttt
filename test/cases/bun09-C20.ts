// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "foo";
interface I6 { [k](): number }
interface I6 { foo: string }
declare const i6: I6;
const a6: never = i6.foo;
const sentinel: number = "";
