// origin: large seed 20261401 bytecodealliance/jco@39b2b63e https://github.com/bytecodealliance/jco packages/jco-std/tsconfig.bench.json, src/wasi/0.2.x/node/24.x.x/zlib/host-worker.ts:88
// flip: yes
// @strict: true
// The zlib module of @types/node declares each stream as a class merged with an interface. Two classes' instances are a subtype of
// each other only when one derives from the other (tsc's removeSubtypes), and a class merged with an interface is still
// a class there: `factories[algorithm]()` is `Deflate | BrotliCompress`, so `"params" in stream` narrows to Deflate.
declare class Transform { on(event: string, listener: () => void): this; write(chunk: unknown): boolean }
interface Zlib { readonly bytesWritten: number; close(callback?: () => void): void; flush(callback?: () => void): void }
interface ZlibParams { params(level: number, strategy: number, callback: () => void): void }
interface ZlibReset { reset(): void }
declare namespace zlib {
    class BrotliCompress extends Transform { constructor(); }
    interface BrotliCompress extends Transform, Zlib {}
    class Deflate extends Transform { constructor(); }
    interface Deflate extends Transform, Zlib, ZlibReset, ZlibParams {}
    function createDeflate(): Deflate;
    function createBrotliCompress(): BrotliCompress;
}
const factories = { deflate: zlib.createDeflate, brotlicompress: zlib.createBrotliCompress };
declare const algorithm: keyof typeof factories;
export function run(level: number, strategy: number): void {
    const stream = factories[algorithm]();
    if (!("params" in stream)) {
        throw new TypeError("params is only supported by zlib streams");
    }
    stream.params(level, strategy, () => {});
}
declare const c: boolean;
declare const d: zlib.Deflate;
declare const b: zlib.BrotliCompress;
export const both = c ? d : b;
