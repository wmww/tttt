// A yield* operand is elaborated against the yield type like any assigned expression (tsc's checkYieldExpression
// passes node.Expression to checkTypeAssignableToAndOptionallyElaborate): an array literal element by element
function* g1(): Generator<number[]> { yield* [[1], ["a"]]; }
function* g2(): Generator<number> { yield* [1, "a"]; }
function* g3(): Generator<number> { yield* ["a"]; }
function* g4(): Generator<{ a: number }> { yield* [{ a: "x" }]; }
declare const xs: string[];
function* g5(): Generator<number> { yield* xs; }
async function* g6(): AsyncGenerator<number> { yield* [1, "a"]; }
declare const c: boolean;
function* h1(): Generator<number[]> { yield* c ? [[1]] : [["a"]]; }
function* h2(): Generator<{ a: number }[]> { yield* [[{ a: "x" }]]; }
function* h3(): Generator<[number, string]> { yield* [[1, 2]]; }
function* h4(): Generator<number[]> { yield* ([[1], ["a"]]); }
async function* h6(): AsyncGenerator<number[]> { yield* [[1], ["a"]]; }
function* h7(): Generator<string> { yield* "abc"; yield* [..."abc", 1]; }
function* h8(): Generator<(x: number) => void> { yield* [(x: string) => {}]; }
