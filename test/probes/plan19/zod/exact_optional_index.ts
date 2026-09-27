// @strict: true
// @exactOptionalPropertyTypes: true
type O1 = { type?: "a"; a: string };
type O2 = { type: "b"; b: string };
type T1 = O1["type"];
type DV<Options extends readonly unknown[], Disc extends string> = {
  [I in keyof Options]: Options[I] extends { output: infer Out } ? Disc extends keyof Out ? Out[Disc] : never : never;
}[number];
type T2 = DV<[{ output: O1 }, { output: O2 }], "type">;
declare function get<const V extends DV<[{ output: O1 }, { output: O2 }], "type">>(v: V): V;
get(undefined);
const t: [T1, T2] = null!;
