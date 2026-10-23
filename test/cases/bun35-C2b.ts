// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "tv/sub/";
const sentinel: number = "";
// @filename: node_modules/tv/package.json
{ "typesVersions": { "*": { "sub": ["ts/sub.d.ts"] } } }
// @filename: node_modules/tv/ts/sub.d.ts
export const x: number;
