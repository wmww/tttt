// @strict: true
// Writing through a union's index signature: the union has one only for the key types its first member has and every member
// has exactly (tsc's getUnionIndexInfos): `string | { [k: string]: string }` is TS7053, not a readonly TS2542
declare let u1: string | { [k: string]: string };
u1[0] = "";
declare let u2: string | string[];
u2[0] = "";
declare let u3: { [k: string]: string } | readonly string[];
u3[0] = "";
