// List members whose names collide are reprinted fully qualified with the message's flags (tsc's mapToTypeNodes keeps
// UseAliasDefinedOutsideCurrentScope, so getSymbolChain doesn't yield a module): an ambient module's export stays
// its bare name, `WritableStream<any>`, next to `NodeJS.WritableStream`
// @filename: /node_modules/@types/node/index.d.ts
declare namespace NodeJS {
    interface WritableStream { node: true }
}
declare module "stream/web" {
    interface WritableStream<W = any> { web: W }
}
// @filename: /a.ts
import type * as web from "stream/web";
declare const x: NodeJS.WritableStream | web.WritableStream | number;
export const y: string = x;
