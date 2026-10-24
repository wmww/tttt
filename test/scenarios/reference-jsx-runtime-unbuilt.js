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
//// [/home/src/workspaces/project/app/src/index.tsx] *new* 
import { x } from "@r8s/core";
export const a = <div>{x}</div>;

tsgo -p app --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
app/src/index.tsx(1,19): error TS6305: Output file '/home/src/workspaces/project/core/dist/index.d.ts' has not been built from source file '/home/src/workspaces/project/core/src/index.ts'.
app/src/index.tsx(2,18): error TS7026: JSX element implicitly has type 'any' because no interface 'JSX.IntrinsicElements' exists.
app/src/index.tsx(2,18): error TS6305: Output file '/home/src/workspaces/project/core/dist/jsx-runtime.d.ts' has not been built from source file '/home/src/workspaces/project/core/src/jsx-runtime.ts'.
app/src/index.tsx(2,26): error TS7026: JSX element implicitly has type 'any' because no interface 'JSX.IntrinsicElements' exists.

