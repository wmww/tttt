currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "./packages/app" }] }
//// [/home/src/workspaces/project/packages/app/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true }, "include": ["src"], "references": [{ "path": "../lib" }] }
//// [/home/src/workspaces/project/packages/app/src/a.ts] *new* 
import type { M } from "../../lib/src/index"
export const m: M = { src: "", nested: { a: 1 } }
export const n: M = { src: "", fsPath: "", nested: { a: 1, b: 2 } }
//// [/home/src/workspaces/project/packages/lib/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "stripInternal": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true }, "include": ["src"] }
//// [/home/src/workspaces/project/packages/lib/src/index.ts] *new* 
export type M = {
  src: string;
  /** @internal */
  fsPath: string;
  nested: {
    a: number;
    /** @internal */
    b: number;
  };
};

tsgo -b
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
packages/app/src/a.ts(3,60): error TS2353: Object literal may only specify known properties, and 'b' does not exist in type '{ a: number; }'.

