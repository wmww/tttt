// With `"include": ["**/*.js", "**/*.ts"]`, a `.d.ts` found by the later pattern removes the `.js` beside it the
// earlier one found (tsc's removeWildcardFilesWithLowerPriorityExtension; markdown-magic's ansi-styles): the `.js` isn't
// a root file, nor checked. In the other order both are (the legacy `.d.ts` next to `.js` rule)
// @filename: tsconfig.json
{
    "compilerOptions": { "allowJs": true, "checkJs": true, "noEmit": true },
    "include": ["a/**/*.js", "a/**/*.ts", "b/**/*.ts", "b/**/*.js"]
}
// @filename: a/styles.js
const n = 1;
n.foo;
// @filename: a/styles.d.ts
export declare const n: number;
// @filename: b/styles.js
const m = 1;
m.foo;
// @filename: b/styles.d.ts
export declare const m: number;
