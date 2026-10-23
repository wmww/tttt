// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "pkg/sub/";
const sentinel: number = "";
// @filename: node_modules/pkg/package.json
{ "exports": { "./sub": "./sub.d.ts" } }
// @filename: node_modules/pkg/sub.d.ts
export const x: number;
