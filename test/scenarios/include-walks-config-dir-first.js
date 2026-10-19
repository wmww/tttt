currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.lint.json] *new* 
{ "compilerOptions": { "composite": true, "strict": true, "types": [] }, "include": ["./packages/**/*.ts"] }
//// [/home/src/workspaces/project/packages/b/tsconfig.json] *new* 
{ "extends": "../../tsconfig.lint.json" }
//// [/home/src/workspaces/project/config/shared.ts] *new* 
export const shared = 1
//// [/home/src/workspaces/project/packages/a/a.ts] *new* 
import { shared } from "../../config/shared"
export const a = shared
//// [/home/src/workspaces/project/packages/b/b.ts] *new* 
import { shared } from "../../config/shared"
export const b = shared

tsgo -p packages/b --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
packages/b/b.ts(1,24): error TS6307: File '/home/src/workspaces/project/config/shared.ts' is not listed within the file list of project '/home/src/workspaces/project/packages/b/tsconfig.json'. Projects must list all files or use an 'include' pattern.
  The file is in the program because:
    Imported via "../../config/shared" from file '/home/src/workspaces/project/packages/b/b.ts'
    Imported via "../../config/shared" from file '/home/src/workspaces/project/packages/a/a.ts'

