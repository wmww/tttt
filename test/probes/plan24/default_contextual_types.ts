declare const o: { f?: (n: number) => number; t?: [string, number] };
const { f = x => x, t = ["a", 1] } = o;
function g({ f = x => x }: { f?: (n: number) => number }) { }
var [a2, [b2, { x12 }] = ["abc", { x12: 10 }]] = [1, ["hello", { x12: 5 }]];
const { k: [p, q] = [1, "s"] } = {} as { k?: [number, string] };
