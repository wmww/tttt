// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const key: string;
function o1(a: {}): void; function o1({ [key]: a = 1 }) {}
class O2 { m(a: {}): void; m({ [key]: a = 1 }) {} }
// both report TS2537: a call before the declaration
e3({}); function e3({ [key]: a = 1 }) {}
// both are silent
function e4({ [key]: a = 1 }) {}
const sentinel: number = "";
