currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "./packages/app" }] }
//// [/home/src/workspaces/project/packages/app/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true }, "include": ["src"], "references": [{ "path": "../lib" }] }
//// [/home/src/workspaces/project/packages/app/src/a.ts] *new* 
import { visible, hidden, hiddenConst, Box, E, F, type Options } from "../../lib/src/index"
const o: Options = { name: "x", _logger: () => {} }
new Box().secret()
E.B
export const v = visible()
F.C
//// [/home/src/workspaces/project/packages/lib/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "stripInternal": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true }, "include": ["src"] }
//// [/home/src/workspaces/project/packages/lib/src/index.ts] *new* 
export function visible(): number { return 1 }
/** @internal */
export function hidden(): number { return 2 }
// @internal
export const hiddenConst = 3
export interface Options {
  name: string
  /** @internal */
  _logger?: (s: string) => void
}
export class Box {
  value = 1
  /** @internal */
  secret(): number { return 1 }
}
export enum E { A, /** @internal */ B }
export enum F {
  A,
  /** @internal */
  C,
}

tsgo -b
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
packages/app/src/a.ts(1,19): error TS2305: Module '"../../lib/src/index"' has no exported member 'hidden'.
packages/app/src/a.ts(1,27): error TS2305: Module '"../../lib/src/index"' has no exported member 'hiddenConst'.
packages/app/src/a.ts(2,33): error TS2353: Object literal may only specify known properties, and '_logger' does not exist in type 'Options'.
packages/app/src/a.ts(3,11): error TS2339: Property 'secret' does not exist on type 'Box'.
packages/app/src/a.ts(6,3): error TS2339: Property 'C' does not exist on type 'typeof F'.

