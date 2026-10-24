// A tsconfig `extends` through a package's `exports`: an exact subpath key wins over a pattern listed before it, and
// of the patterns the one with the longer prefix wins (tsc's loadModuleFromExportsOrImports, comparePatternKeys)
// @filename: tsconfig.json
{ "extends": ["cfg/typescript", "cfg/configs/implicit"], "compilerOptions": { "noEmit": true }, "files": ["a.ts"] }
// @filename: node_modules/cfg/package.json
{"name": "cfg", "version": "1.0.0", "exports": {"./*": "./dist/*.json", "./configs/*": "./configs/*.json", "./typescript": "./dist/base.json"}}
// @filename: node_modules/cfg/dist/base.json
{ "compilerOptions": { "strict": true } }
// @filename: node_modules/cfg/dist/configs/implicit.json
{ "compilerOptions": { "noImplicitAny": false } }
// @filename: node_modules/cfg/configs/implicit.json
{ "compilerOptions": { "noImplicitReturns": true } }
// @filename: a.ts
export const f = (x: number | undefined) => x.toFixed();
export function g(b: boolean) { if (b) return 1; }
export const h = (y) => y;
