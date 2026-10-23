// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
declare function g(): any;
class C { [g(), "a"]() {} }
const sentinel: number = "";
