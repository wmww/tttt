// @strict: true
// An interface whose base reads its derived interface through Pick (`P extends Pick<G, 'k'>`, `G extends P`): the
// Pick asks for G's members while P's are resolved, which finds what G has so far, its declared members and the bases
// added before (tsc's resolveObjectTypeMembers sets the members table before it adds the inherited ones), not a base
// cycle: G's bases don't resolve their members (getBaseTypes)
interface U2 { id: string; unavailable: boolean; }
interface P2 extends Omit<U2, 'unavailable'>, Pick<G2, 'screen'> { name: string; icon: string | null; }
interface G2 extends P2 { owner: boolean; screen?: { description: string }; }
const k: keyof G2 = "icon";
export type Second = Pick<G2, 'id' | 'name' | 'icon' | 'owner'>;
export const s: P2["screen"] = undefined;
