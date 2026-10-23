currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "a/tsconfig.config.json" }, { "path": "a/tsconfig.build.json" }, { "path": "b" }] }
//// [/home/src/workspaces/project/a/package.json] *new* 
{ "name": "a", "type": "module", "exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } } }
//// [/home/src/workspaces/project/a/bin.js] *new* 
import './dist/x.js'
//// [/home/src/workspaces/project/a/tsconfig.config.json] *new* 
{ "compilerOptions": { "module": "node18", "allowJs": true, "checkJs": true, "noEmit": true, "types": [] }, "include": ["./*.js"] }
//// [/home/src/workspaces/project/a/tsconfig.build.json] *new* 
{ "compilerOptions": { "module": "node18", "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"] }
//// [/home/src/workspaces/project/a/src/x.ts] *new* 
export const x = 1;
//// [/home/src/workspaces/project/a/src/index.ts] *new* 
export * from "./x.js";
//// [/home/src/workspaces/project/b/package.json] *new* 
{ "name": "b", "type": "module" }
//// [/home/src/workspaces/project/b/node_modules/a] -> /home/src/workspaces/project/a *new* 
//// [/home/src/workspaces/project/b/tsconfig.json] *new* 
{ "compilerOptions": { "module": "node18", "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"], "references": [{ "path": "../c" }] }
//// [/home/src/workspaces/project/b/src/b.ts] *new* 
import { x } from "a"; export const y: number = x;
//// [/home/src/workspaces/project/c/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"] }
//// [/home/src/workspaces/project/c/src/c.ts] *new* 
export const c = 1;

tsgo -b --builders 1
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
a/bin.js(1,8): error TS2882: Cannot find module or type declarations for side-effect import of './dist/x.js'.
a/dist/index.d.ts(1,15): error TS2307: Cannot find module './x.js' or its corresponding type declarations.
b/src/b.ts(1,10): error TS2305: Module '"a"' has no exported member 'x'.
