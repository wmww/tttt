// @strict: true
// An interface whose base reads its derived interface through Pick (`P extends Pick<G, 'k'>`, `G extends P`): the
// Pick asks for G's members while P's are resolved, which finds what G has so far, its declared members and the bases
// added before (tsc's resolveObjectTypeMembers sets the members table before it adds the inherited ones), not a base
// cycle: G's bases don't resolve their members (getBaseTypes, hasBaseType). In each order: the declarations checked
// first, a use of G first, and an interface extending G first (discord-api-types' APIGuild, which made the output
// depend on which file a checker saw first)
// @filename: a.ts
interface U1 { id: string; unavailable: boolean; }
interface P1 extends Omit<U1, 'unavailable'>, Pick<G1, 'screen'> { name: string; icon: string | null; }
interface G1 extends P1 { owner: boolean; screen?: { description: string }; }
const k1: keyof G1 = "icon";
export type First = Pick<G1, 'id' | 'name' | 'icon' | 'owner'>;
export const s1: P1["screen"] = undefined;
// @filename: b.ts
export type Second = Pick<G2, 'id' | 'name' | 'icon' | 'owner'>;
interface U2 { id: string; unavailable: boolean; }
interface P2 extends Omit<U2, 'unavailable'>, Pick<G2, 'screen'> { name: string; icon: string | null; }
interface G2 extends P2 { owner: boolean; screen?: { description: string }; }
const k2: keyof G2 = "icon";
// @filename: c.ts
interface Derived extends G3 { extra: number }
declare const d: Derived;
d.extra;
interface U3 { id: string; unavailable: boolean; }
interface P3 extends Omit<U3, 'unavailable'>, Pick<G3, 'screen'> { name: string; icon: string | null; }
interface G3 extends P3 { owner: boolean; screen?: { description: string }; }
export const k3: keyof G3 = "icon";
