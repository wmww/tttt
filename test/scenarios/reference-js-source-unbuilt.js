currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/scripts/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "allowJs": true, "declaration": true, "emitDeclarationOnly": true, "module": "nodenext", "types": [], "outDir": "out" }, "include": ["testing"] }
//// [/home/src/workspaces/project/scripts/testing/utils.js] *new* 
export function log(x) { return x }
//// [/home/src/workspaces/project/app/tsconfig.json] *new* 
{ "compilerOptions": { "allowJs": true, "strict": true, "noEmit": true, "module": "nodenext", "types": [] }, "include": ["test"], "references": [{ "path": "../scripts" }] }
//// [/home/src/workspaces/project/app/test/a.ts] *new* 
import { log } from "../../scripts/testing/utils.js";
log(1);

tsgo -p app --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
app/test/a.ts(1,21): error TS7016: Could not find a declaration file for module '../../scripts/testing/utils.js'. '/home/src/workspaces/project/scripts/testing/utils.js' implicitly has an 'any' type.

