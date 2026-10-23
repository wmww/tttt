// @strict: true
// An imported generic alias whose body is an intersection its type arguments don't change is the declared type itself,
// `Opts<T>` by its own parameter: tsgo's instantiateTypeWorker returns the type when the members are the same and the
// alias passed down (the import's resolved one) is its own. Inference then reads T from its alias arguments, and the
// call fails (tanstack-ai's ai-angular inject-web-mcp-tools test)
// @filename: src.ts
export type Opts<T extends readonly any[]> = { [x: string]: any } & { onError?: 1 }
export declare function reg<const T extends readonly any[]>(tools: T, options: Opts<T>): T
// @filename: t.ts
import { reg, type Opts } from './src'
declare const tool: any
declare const options: Opts<readonly [any]>
const r = reg([tool], options)
type Local<T> = { a: 1 } | { b: T }
declare const local: Local<string>
