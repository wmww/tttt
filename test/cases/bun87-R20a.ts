// @filename: tsconfig.json
{"compilerOptions":{"target":"es2018","lib":["es5"],"noEmit":true,"module":"commonjs"},"files":["a.ts","b.ts"]}
// @filename: a.ts
export async function f() {}
// @filename: b.ts
import { f } from "./a";
f();
const sentinel: number = "";
