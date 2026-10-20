// An interface's property signature and a later class declaration's accessor of the same name are one symbol, typed
// by the accessor (tsc's getTypeOfSymbol: SymbolFlags.Accessor first): DOM's `interface Body { readonly body:
// ReadableStream<Uint8Array> | null }` merged with @cloudflare/workers-types' `declare abstract class Body { get
// body(): ReadableStream | null }` reads `ReadableStream<any>`
// @strict: true
// @filename: dom.d.ts
interface Bd { readonly body: Array<number> | null; }
interface Rs extends Bd { ok: boolean }
// @filename: workers.d.ts
declare abstract class Bd { get body(): Array<any> | null; }
// @filename: use.ts
declare const r: Rs;
const x: string = r.body!;
