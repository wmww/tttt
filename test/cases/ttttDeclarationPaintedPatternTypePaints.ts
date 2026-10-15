// A destructuring declaration that painting emits (`declare const { Readable }: typeof Stream`) is written with its
// whole type, which is checked in the late pass like the declaration it is: the import it names is kept (hexo's
// router.d.ts, graphql-codegen's file-system.d.ts)
// @declaration: true
// @esModuleInterop: true
// @filename: /node_modules/stream/index.d.ts
declare namespace internal { class Readable { read(): void } }
export = internal;
// @filename: /router.ts
import Stream from "stream";
const { Readable } = Stream;
export class Router { make(): InstanceType<typeof Readable> { return null!; } }
