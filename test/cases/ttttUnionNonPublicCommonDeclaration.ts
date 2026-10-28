// @strict: true
// A union's property that is protected or private in some member exists only if every member has it from the same
// declaration (tsc's hasCommonDeclaration): a homomorphic mapped type's property is declared where the property it
// maps is, so `Pick<D, "id"> | D` has `id`; and a mapped property has no value declaration of its own, so it is public
// (getDeclarationModifierFlagsFromSymbol): the union's `id` is readonly (TS2540), not protected
declare class D { protected readonly id: string; x: number }
declare class E { protected readonly id: string }
declare const u: Pick<D, "id"> | D;
u.id = "x";
declare const v: Pick<D, "id">;
v.id;
declare const w: D | E;
w.id;
