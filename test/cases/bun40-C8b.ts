// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "importHelpers": true, "experimentalDecorators": true, "module": "commonjs", "moduleResolution": "bundler", "allowJs": true, "maxNodeModuleJsDepth": 1 } }
// @filename: a.ts
declare function dec(x: any): void;
@dec class A {}
export const sentinel: number = "";
// @filename: node_modules/tslib/package.json
{"name":"tslib","version":"2.0.0","main":"tslib.js"}
// @filename: node_modules/tslib/tslib.js
exports.__extends = function () {};
