// @noTypesAndSymbols: true
// A project whose files are all declaration files: its computed common source directory is the project's own (tsgo's
// program base directory), not the working directory, so the inferred root didn't move (no TS5011)
// @filename: /p/tsconfig.json
{ "compilerOptions": { "declaration": true, "declarationDir": "typings", "emitDeclarationOnly": true }, "include": ["src"] }
// @filename: /p/src/env.d.ts
declare const x: number;
