// @strict: true
interface ByteCtl { byob: number; close(): void }
interface DefCtl<R> { enqueue(r: R): void; close(): void }
interface ByteSrc { type: "bytes"; start?(controller: ByteCtl): void; cancel?(): void }
interface DefSrc<R> { start?(controller: DefCtl<R>): void; cancel?(): void }
declare var RS: {
  new (s: ByteSrc): { bytes: true }
  new <R = any>(s: DefSrc<R>): { r: R }
}
export const x = new RS({
  start(controller) { controller.close() },
  cancel() {},
})
declare function f(s: ByteSrc): 1
declare function f<R>(s: DefSrc<R>): 2
export const y = f({ start(c) { c.close() } })
