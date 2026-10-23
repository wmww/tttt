// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference path="./node_modules/p/node_modules/x/index.d.ts" />
/// <reference path="./node_modules/q/node_modules/x/index.d.ts" />
const sentinel: number = "";
// @filename: node_modules/p/node_modules/x/index.d.ts
declare const g: 1;
// @filename: node_modules/p/node_modules/x/package.json
{"name":"x","version":"1.0.0"}
// @filename: node_modules/q/node_modules/x/index.d.ts
declare const g: 1;
// @filename: node_modules/q/node_modules/x/package.json
{"name":"x","version":"1.0.0"}
