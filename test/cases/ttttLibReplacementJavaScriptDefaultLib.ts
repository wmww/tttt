// libReplacement resolving the target's default library to a package's JavaScript without allowJs: TS6504,
// explained as the default library (bun40-C7c is the `lib` option's), and the program has no globals
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "es2022", "libReplacement": true }, "files": ["a.ts"] }
// @filename: a.ts
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-es2022/full.js
module.exports = {}
// @filename: node_modules/@typescript/lib-es2022/package.json
{"name":"@typescript/lib-es2022","version":"1.0.0"}
