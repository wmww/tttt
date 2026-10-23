// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import("./m", { extra: (x) => {} });
const sentinel: number = "";
export {};
// @filename: lib.d.ts
interface Array<T> { length: number } interface Boolean {} interface CallableFunction {} interface Function {} interface IArguments {} interface NewableFunction {} interface Number {} interface Object {} interface RegExp {} interface String {} interface Promise<T> { then(): void } interface PromiseConstructor { new <T>(f: any): Promise<T> } declare var Promise: PromiseConstructor;
type ImportCallOptions = { extra?: (x: number) => void };
// @filename: m.ts
export {};
