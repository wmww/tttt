// @noEmit: true
// @strict: true
// @target: esnext
// @module: esnext
export {};
interface BadP { then(cb: (v: BadP) => void): void }
interface NoCb { then(): void }
declare const bp: BadP, nc: NoCb, ai: AsyncIterable<NoCb>;
async function* g1() { yield bp; }
async function* g2() { yield nc; }
async function* g3() { yield* ai; }
const sentinel: number = "";

declare const un: string | NoCb;
async function* g5() { yield un; yield un; }
declare const sit: Iterable<BadP>;
async function g7() { for await (const y of sit) { y; } }
