// An empty string option leaves what an extended config gave (tsgo merges the options' non-zero values): qwik's
// tsconfig.dts.json's `"sourceRoot": ""` over its base's "." is still TS5051 without source maps
// @filename: tsconfig.json
{
    "extends": "./base.json",
    "compilerOptions": { "sourceRoot": "", "sourceMap": false, "inlineSourceMap": false }
}
// @filename: base.json
{
    "compilerOptions": { "sourceRoot": "." }
}
// @filename: a.ts
export const x = 1;
