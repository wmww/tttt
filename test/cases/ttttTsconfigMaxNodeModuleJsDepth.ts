// A number in a tsconfig's compilerOptions is read (maxNodeModuleJsDepth was ignored, the default 0 kept): a package's
// JavaScript reached through node_modules is part of the program under allowJs, so a missing export is TS2305 rather
// than the package being untyped (TS7016)
// @filename: tsconfig.json
{ "compilerOptions": { "module": "esnext", "moduleResolution": "bundler", "allowJs": true, "strict": true, "noEmit": true, "maxNodeModuleJsDepth": 1 } }
// @filename: node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "main": "index.js" }
// @filename: node_modules/pkg/index.js
export const a = 1;
// @filename: main.ts
import { a, b } from "pkg";
