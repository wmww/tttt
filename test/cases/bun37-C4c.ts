// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"] }, "extends": "./sub/b.json" }
// @filename: sub/b.json
{ "include": [""] }
