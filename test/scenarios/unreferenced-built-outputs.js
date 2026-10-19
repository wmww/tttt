currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "./packages/lib" }, { "path": "./packages/app/tsconfig.test.json" }] }
//// [/home/src/workspaces/project/node_modules/lib] -> /home/src/workspaces/project/packages/lib *new* 
//// [/home/src/workspaces/project/packages/app/package.json] *new* 
{ "name": "app", "type": "module" }
//// [/home/src/workspaces/project/packages/app/tsconfig.test.json] *new* 
{ "compilerOptions": { "noEmit": true, "module": "nodenext", "strict": true, "types": [] }, "include": ["test"] }
//// [/home/src/workspaces/project/packages/app/test/a.test.ts] *new* 
import { f, Box } from "lib"
import { g } from "../../lib/dist/g.js"
const n: number = f()
const s: string = f()
const b: Box = new Box()
g(1)
//// [/home/src/workspaces/project/packages/lib/package.json] *new* 
{ "name": "lib", "type": "module", "exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } } }
//// [/home/src/workspaces/project/packages/lib/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "module": "nodenext", "rootDir": "src", "outDir": "dist", "types": [] }, "include": ["src"] }
//// [/home/src/workspaces/project/packages/lib/src/g.ts] *new* 
export const g = (x: string) => x.length
//// [/home/src/workspaces/project/packages/lib/src/index.ts] *new* 
export function f(): number { return 1 }
export class Box { private v = 0 }

tsgo -b --builders 1
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
packages/app/test/a.test.ts(4,7): error TS2322: Type 'number' is not assignable to type 'string'.
packages/app/test/a.test.ts(6,3): error TS2345: Argument of type 'number' is not assignable to parameter of type 'string'.

