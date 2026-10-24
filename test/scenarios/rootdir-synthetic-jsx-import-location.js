currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/core/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "rootDir": "./src", "outDir": "./dist", "module": "nodenext", "types": [], "strict": true }, "include": ["src"] }
//// [/home/src/workspaces/project/core/src/index.ts] *new* 
export * from "./jsx-runtime.js";
export const x = 1;
//// [/home/src/workspaces/project/core/src/jsx-runtime.ts] *new* 
export declare namespace JSX { interface IntrinsicElements { [name: string]: any } type Element = string }
export function jsx(type: string, props: unknown): string { return type }
export const jsxs = jsx;
//// [/home/src/workspaces/project/app/tsconfig.json] *new* 
{ "compilerOptions": { "jsx": "react-jsx", "jsxImportSource": "@r8s/core", "paths": { "@r8s/core": ["../core/src/index.ts"], "@r8s/core/jsx-runtime": ["../core/src/jsx-runtime.ts"] }, "rootDir": "./src", "outDir": "./dist", "composite": true, "module": "nodenext", "types": [], "strict": true }, "include": ["src"], "references": [{ "path": "../core" }] }
//// [/home/src/workspaces/project/app/tsconfig.bench.json] *new* 
{ "extends": "./tsconfig.json", "compilerOptions": { "composite": false, "declaration": true } }
//// [/home/src/workspaces/project/app/src/index.tsx] *new* 
import { x } from "@r8s/core";
export const a = <div>{x}</div>;

tsgo -p app/tsconfig.bench.json --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
app/src/index.tsx(1,19): error TS6059: File '/home/src/workspaces/project/core/src/index.ts' is not under 'rootDir' '/home/src/workspaces/project/app/src'. 'rootDir' is expected to contain all source files.
  File is CommonJS module because 'package.json' was not found
core/src/index.ts(1,15): error TS6059: File '/home/src/workspaces/project/core/src/jsx-runtime.ts' is not under 'rootDir' '/home/src/workspaces/project/app/src'. 'rootDir' is expected to contain all source files.
  The file is in the program because:
    Imported via "@r8s/core/jsx-runtime" from file '/home/src/workspaces/project/app/src/index.tsx' to import 'jsx' and 'jsxs' factory functions
    Imported via "./jsx-runtime.js" from file '/home/src/workspaces/project/core/src/index.ts'
  File is CommonJS module because 'package.json' was not found

