// origin: npm @abrevior/apollo-federation-upload@4.2.0 https://github.com/abrevior/apollo-federation-file-upload tsconfig.json seed 20261201
// flip: no
// A JavaScript package file (.mjs, no declarations) reached through node_modules under allowJs + maxNodeModuleJsDepth
// while `paths` has a "*" wildcard (what the bench config made of baseUrl "."): tsgo loads it into the program (TS2305
// on the missing export, and the original checks the package's JS); tttt reports TS7016 instead. Without the paths it
// agrees with tsgo.
// @filename: tsconfig.json
{ "compilerOptions": { "module": "esnext", "moduleResolution": "bundler", "allowJs": true, "strict": true, "noEmit": true, "maxNodeModuleJsDepth": 10, "paths": { "*": ["./*"] } } }
// @filename: node_modules/graphql-upload/package.json
{"name": "graphql-upload", "version": "16.0.2", "exports": {"./GraphQLUpload.mjs": "./GraphQLUpload.mjs"}}
// @filename: lib/a.ts
import type { FileUpload } from 'graphql-upload/GraphQLUpload.mjs';
// @filename: node_modules/graphql-upload/GraphQLUpload.mjs
export {};
