// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "skipLibCheck": true, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "pkg";
declare module "pkg" { enum E { B } enum F { B = 2 } const enum G { B = 2 } }
const sentinel: number = "";
// @filename: node_modules/pkg/index.d.ts
export declare enum E { A }
export declare const enum F { A = 1 }
export declare enum G { A = 1 }
// @filename: node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts" }
