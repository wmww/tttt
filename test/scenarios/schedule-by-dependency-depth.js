currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "y" }, { "path": "x" }] }
//// [/home/src/workspaces/project/x/package.json] *new* 
{ "name": "x", "type": "module", "exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } } }
//// [/home/src/workspaces/project/x/tsconfig.json] *new* 
{ "compilerOptions": { "module": "nodenext", "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"] }
//// [/home/src/workspaces/project/x/src/index.ts] *new* 
export const x: number = 1;
//// [/home/src/workspaces/project/y/package.json] *new* 
{ "name": "y", "type": "module" }
//// [/home/src/workspaces/project/y/node_modules/x] -> /home/src/workspaces/project/x *new* 
//// [/home/src/workspaces/project/y/tsconfig.json] *new* 
{ "compilerOptions": { "module": "nodenext", "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"], "references": [{ "path": "../z" }] }
//// [/home/src/workspaces/project/y/src/y.ts] *new* 
import { x } from "x"; export const y: string = x;
//// [/home/src/workspaces/project/z/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "outDir": "dist", "rootDir": "src", "types": [] }, "include": ["src"] }
//// [/home/src/workspaces/project/z/src/z.ts] *new* 
export const z = 1;

tsgo -b --builders 1
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
y/src/y.ts(1,37): error TS2322: Type 'number' is not assignable to type 'string'.
