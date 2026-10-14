// Booleans made by a distributive conditional type go by the alias declaring the instantiation (tsc's mapTypeWithAlias
// makes a union of its own), not when the alias body is boolean itself
type If<C extends boolean, T, F> = C extends true ? T : F;
type X1 = If<boolean, 1, 2>;
type X2 = If<boolean, true, false>;
type D<T> = T extends string ? 1 : 2;
type X3 = D<string | number>;
type X4 = D<string | number | boolean>;
type B<T> = T extends string ? true : false;
type X5 = B<string | number>;
type B1 = true | false;
type BB<T> = boolean;
type B2 = BB<1>;
type B3<T> = T extends 1 ? boolean : string;
type B4 = B3<1>;
type B5 = B3<1 | 2>;
declare let b1: B1, b2: B2, b4: B4, b5: B5;
