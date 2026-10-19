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
import { f } from "lib"
import { helper } from "../../lib/src/helper.js"
const s: string = f()
helper(1)
//// [/home/src/workspaces/project/packages/lib/package.json] *new* 
{ "name": "lib", "type": "module", "exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } } }
//// [/home/src/workspaces/project/packages/lib/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "module": "nodenext", "rootDir": "src", "outDir": "dist", "types": [], "strict": true }, "include": ["src"] }
//// [/home/src/workspaces/project/packages/lib/src/helper.ts] *new* 
export function helper(x) { return x }
const bad: number = "x"
//// [/home/src/workspaces/project/packages/lib/src/index.ts] *new* 
export { helper } from "./helper.js"
export function f(): number { return 1 }

tsgo -b --builders 1
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
packages/lib/src/helper.ts(1,24): error TS7006: Parameter 'x' implicitly has an 'any' type.
packages/lib/src/helper.ts(2,7): error TS2322: Type 'string' is not assignable to type 'number'.
packages/app/test/a.test.ts(3,7): error TS2322: Type 'number' is not assignable to type 'string'.
packages/lib/src/helper.ts(1,24): error TS7006: Parameter 'x' implicitly has an 'any' type.
packages/lib/src/helper.ts(2,7): error TS2322: Type 'string' is not assignable to type 'number'.

