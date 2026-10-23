// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2015", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
function d(t: any, c: any) {}
class X { @d accessor a = 1; }
const sentinel: number = "";
// @filename: lib.d.ts
interface Array<T> {} interface Boolean {} interface CallableFunction {} interface Function {} interface IArguments {} interface NewableFunction {} interface Number {} interface Object {} interface RegExp {} interface String {}
