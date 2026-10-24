// @strict: true
// A null or undefined member of a spread union is spread like an object without members (tsc's getSpreadType makes a
// new object for it, keeping the index signatures both sides have): after nothing it is no longer the empty object
// type, so a later spread's index signatures aren't copied into it whole
type A = { [k: string]: number };
type B = { [k: string]: string };
declare const ab: A | B | undefined;
declare const abn: A | B | null;
declare const rec: Record<string, true>;
declare const r0: { [k: string]: boolean };
const s1 = { ...ab, ...rec };
const s2 = { ...r0, ...ab };
const s3 = { ...abn, x: 1 };
const s4 = { ...ab, ...rec, y: "" };
const s5 = { ...ab };
function g<T extends object>(t: T) {
  const s6 = { ...t, ...ab };
  const s7 = { ...ab, ...t };
  return [s6, s7];
}
