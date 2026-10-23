// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
interface Array<T> {} interface Boolean {} interface CallableFunction {} interface Function {} interface IArguments {} interface NewableFunction {} interface Number {} interface Object {} interface RegExp {} interface String {}
interface Promise<T> {} interface PromiseConstructor {} declare var Promise: PromiseConstructor;
interface ImportCallOptions<T> { with?: { f: (x: number) => void } }
import("./m", { with: { f: x => {} } });
const sentinel: number = "";
// @filename: m.ts
export const m = 1;
