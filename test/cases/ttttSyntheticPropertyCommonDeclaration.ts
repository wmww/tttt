// @strict: true
// A union's property whose members' properties are non-public exists when they have a declaration in common (tsc's
// hasCommonDeclaration), each member's property counting with all its declarations: an instantiated class property
// is declared where its target is, and an intersection's synthetic property wherever its members' are
declare class D { protected readonly id: string | number; other: 1 }
declare const u: Pick<D, "id"> | D;
u.id = "x";

interface J { id: string | number }
declare class E { protected id: string | number; other: 1 }
type W = { +readonly [K in keyof E]: E[K] }
// `(E & J) | (E & W)`: E's `id` is in both, and public in `E & J` (J's is), so the union's is public
declare const v: E & (J | W);
v.id = "x";
const s: string | number = v.id;
// Nor is it TS4105 on a type parameter (tsc's getConstituentProperty: the intersection's property is public), but not
// a key either
function f<T extends typeof v>(x: T["id"]) {}
