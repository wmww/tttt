// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022", "experimentalDecorators": true } }
// @filename: a.ts
interface Array<T> { length: number } interface Boolean {} interface Function {} interface IArguments {} interface Number {} interface Object {} interface RegExp {} interface String {}
interface CallableFunction extends Function {} interface NewableFunction extends Function {}
declare function dec3(target: any, key: any, d: { value: number }): void;
class C {
  @dec3 m() {}
}
const sentinel: number = "";
