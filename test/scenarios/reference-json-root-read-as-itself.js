currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "module": "nodenext", "resolveJsonModule": true, "rootDir": "lib", "outDir": "dist", "types": [], "strict": true }, "include": ["./lib/**/*", "./lib/schema.json"] }
//// [/home/src/workspaces/project/package.json] *new* 
{ "type": "module" }
//// [/home/src/workspaces/project/lib/schema.json] *new* 
{ "a": 1 }
//// [/home/src/workspaces/project/lib/index.ts] *new* 
import schema from "./schema.json" with { type: "json" };
export const s: typeof schema = schema;
//// [/home/src/workspaces/project/test/tsconfig.json] *new* 
{ "extends": "../tsconfig", "include": ["./**/*"], "compilerOptions": { "rootDir": "..", "noEmit": true }, "references": [{ "path": ".." }] }
//// [/home/src/workspaces/project/test/a.test.ts] *new* 
import { s } from "../lib/index.js";
const n: string = s.a;

tsgo -b test
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
test/a.test.ts(2,7): error TS2322: Type 'number' is not assignable to type 'string'.
