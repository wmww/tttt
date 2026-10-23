// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022" } }
// @filename: a.ts
interface Array<T> { length: number } interface Boolean {} interface Function {} interface IArguments {} interface Number {} interface Object {} interface RegExp {} interface String {}
interface CallableFunction extends Function {} interface NewableFunction extends Function {}
declare function dec(target: any, context: { name: string }): void;
@dec class C {
  @dec accessor g = 1;
}
const sentinel: number = "";
