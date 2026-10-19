currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "files": [], "references": [{ "path": "./packages/app" }] }
//// [/home/src/workspaces/project/packages/app/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true, "target": "es2022" }, "include": ["src"], "references": [{ "path": "../lib" }] }
//// [/home/src/workspaces/project/packages/app/src/a.ts] *new* 
import { Box, Config } from "../../lib/src/index"
declare const n: Box<number>
export const s: Box<string> = n
const c = new Config()
c['config'] = new Map<string, unknown>()
const v: string = n['value']
//// [/home/src/workspaces/project/packages/lib/tsconfig.json] *new* 
{ "compilerOptions": { "composite": true, "rootDir": "src", "outDir": "dist", "types": [], "strict": true, "target": "es2022" }, "include": ["src"] }
//// [/home/src/workspaces/project/packages/lib/src/index.ts] *new* 
export class Box<T> {
  private value: T
  private read(): T { return this.value }
  #brand: T
  private get got(): T { return this.value }
  private static count: number = 0
  constructor(value: T) { this.value = value; this.#brand = value }
  size(): number { return 1 }
}
export class Config {
  private config: { project: string; dir: string } = { project: "", dir: "" }
}

tsgo -b
ExitStatus:: Success
Output::

