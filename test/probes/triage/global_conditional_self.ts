// @types/node's crypto.d.ts: a global whose declared type asks whether globalThis has properties, one of them
// itself. A property globalThis lacks makes it false before the variable's own type is needed (no TS2502)
export {};
declare global {
    var cryptoLike: typeof globalThis extends { cryptoLike: infer T; onmessageLike: any } ? T : number;
}
const n: number = cryptoLike;
