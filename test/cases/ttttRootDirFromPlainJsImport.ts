// TS6059 at an import in plain JavaScript (allowJs, no checkJs) is reported: it's a program diagnostic (tsgo's
// GetIncludeProcessorDiagnostics skips only files canIncludeBindAndCheckDiagnostics rejects), not one of the checker's
// that plain JavaScript filters by code (mermaid's themes/index.js importing theme-base.js outside rootDir)
// @filename: tsconfig.json
{
    "compilerOptions": { "allowJs": true, "rootDir": "./src", "outDir": "./dist", "noEmit": true, "module": "esnext", "moduleResolution": "bundler" },
    "include": ["./src/**/*.ts"]
}
// @filename: src/a.ts
import { x } from "../other/x.js";
export const a = x;
// @filename: other/x.js
import { y } from "./y.js";
export const x = y;
// @filename: other/y.js
export const y = 1;
