// @strict: true
// @exactOptionalPropertyTypes: true
// @noEmit: true
// Members of disjoint domains make an intersection never before any absorbs it (tsc's getIntersectionType checks the
// domains first), and a union key's write type intersects each optional property's write type without the missing
// type (getWriteTypeOfSymbol per key): sequelize's `existingIndex[key] = index[key]` over IndexOptions' keys, one of
// them any, is TS2322 to never
type X1 = string & boolean & any;
type X2 = string & any & number;
type X3 = any & string & boolean;
type X4 = (string | undefined) & (boolean | undefined) & any;
type X5 = null & { a: 1 } & any;
declare let x: { a?: string; b?: boolean; c?: any };
declare const k: 'a' | 'b' | 'c';
declare const v: any;
x[k] = v;
