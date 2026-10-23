// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022" } }
// @filename: a.ts
interface Array<T> { } interface Boolean { } interface CallableFunction { } interface Function { } interface IArguments { } interface NewableFunction { } interface Number { } interface Object { } interface RegExp { } interface String { }
declare const o: { a: 1 }; declare let k: "a";
for (k in o) {}
const sentinel: number = "";
