// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "paths": { "*": ["./lib/*"] } }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from ".\\x";
const x: 0 = which;
const sentinel: number = "";
// @filename: lib/x.d.ts
export declare const which: "lib/x";
// @filename: x.d.ts
export declare const which: "x";
