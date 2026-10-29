// An any (or error) key of a union or intersection of objects is TS2538 unless the whole has an applicable index
// signature: tsc's getPropertyTypeForIndexType on the union, not member by member
declare const k: any;
declare const s: string;
declare const ab: { n: number } | { n: string };
const { [k]: a1 } = ab;
const { [k]: a2 } = { n: 1 };
const { [s]: a3 } = ab;
declare const r: Record<string, number>;
const { [k]: a4 } = r;
const { [missing]: a5 } = { n: 1 };
const x = ab[k];
type T1 = ({ n: number } | { n: string })[any];
type T2 = (string | { n: string })[any];
type T3 = (Record<string, 1> | Record<string, 2>)[any];
type T4 = ({ a: 1 } & { b: 2 })[any];
const { [k]: a6 } = "str" as string | { n: string };
const { [k]: a7 } = r as Record<string, 1> | Record<string, 2>;
