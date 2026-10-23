currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "compilerOptions": { "noEmit": true, "types": [] }, "files": ["a.ts"], "references": [{ "path": "./sub", "prepend": true }] }
//// [/home/src/workspaces/project/a.ts] *new* 
const sentinel: number = "";
//// [/home/src/workspaces/project/sub/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "types": [] }, "files": ["s.ts"] }
//// [/home/src/workspaces/project/sub/s.ts] *new* 
export const s = 1;

tsgo -p . --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
a.ts(1,7): error TS2322: Type 'string' is not assignable to type 'number'.

