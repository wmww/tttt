// @strict: true
// @noEmit: true
// tsc: clean. b.d.ts's `declare global` overload group is tried first (reorderCandidates: a later declaration
// parent of the same symbol is spliced in front), like @types/node's setTimeout before lib.dom's
// @filename: a.d.ts
declare function f(x: any): number;
declare function f(x: string, y: string): boolean;
// @filename: b.d.ts
export {};
declare global {
    function f<T>(x: () => T): string;
}
// @filename: c.ts
const r = f(() => 1);
const s: string = r;
