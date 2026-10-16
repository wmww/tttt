// An indexed access type whose key the object lacks is reported while the alias resolves, the object printed into the
// message: that resolves the object's member types, so a member referencing the alias is circular (TS2456, TS2502)
// (mutation of arrayFakeFlatNoCrashInferenceDeclarations)
// @strict: true
type Bad<Arr, D extends number> = {obj: {
    "done": Arr,
    "recur": Bad<Arr, [-1, 0, 1][D]>
}[D extends -1 ? "done" : "recur"]}[42];
declare const x: Bad<string, 1>;

type Bad2 = { obj: Bad2 }["x"];
type Fine = { obj: string }["x"];
interface I { obj: J }
type J = I["x"];
