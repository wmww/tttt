currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "module": "nodenext", "types": [], "outDir": "out", "rootDir": "src" }, "files": ["src/a.ts"] }
//// [/home/src/workspaces/project/src/a.ts] *new* 
type T = typeof import("./b.js");
export * from "./c.js";
//// [/home/src/workspaces/project/src/b.ts] *new* 
export * from "./d.js";
//// [/home/src/workspaces/project/src/c.ts] *new* 
export * from "./d.js";
//// [/home/src/workspaces/project/src/d.ts] *new* 
export const d = 1;

tsgo -p tsconfig.json --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
src/a.ts(1,24): error TS6307: File '/home/src/workspaces/project/src/b.ts' is not listed within the file list of project '/home/src/workspaces/project/tsconfig.json'. Projects must list all files or use an 'include' pattern.
  File is CommonJS module because 'package.json' was not found
src/a.ts(2,15): error TS6307: File '/home/src/workspaces/project/src/c.ts' is not listed within the file list of project '/home/src/workspaces/project/tsconfig.json'. Projects must list all files or use an 'include' pattern.
  File is CommonJS module because 'package.json' was not found
src/c.ts(1,15): error TS6307: File '/home/src/workspaces/project/src/d.ts' is not listed within the file list of project '/home/src/workspaces/project/tsconfig.json'. Projects must list all files or use an 'include' pattern.
  The file is in the program because:
    Imported via "./d.js" from file '/home/src/workspaces/project/src/c.ts'
    Imported via "./d.js" from file '/home/src/workspaces/project/src/b.ts'
  File is CommonJS module because 'package.json' was not found

