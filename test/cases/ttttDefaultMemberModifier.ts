// `default` is a member's modifier before what can follow it (tsc's nextTokenCanFollowDefaultKeyword): TS1070 on a type
// member, TS1029 on a class member, and the member is bound as `default` (declareSymbol's default export name)
export {};
interface I { default class: 1 } declare const i: I; i.class; i.default;
type T = { default function: 1 }; declare const t: T; t.function; t.default;
class C { default class = 1 } declare const c: C; c.class; c.default;
interface L { default class: 1; default: 2 }
