interface J { "": number; "a": number }
declare const j: J;
const n: number = j[""];
const o: J = { "": 1, a: 1 };
type T = { "": number };
const p: T = { "": 1 };
