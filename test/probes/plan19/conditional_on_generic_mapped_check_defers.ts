// tsc: all "full". A conditional whose check type is a generic mapped type (Required<U> with U a type parameter,
// here reached through `Required<T> extends infer U`) is deferred (isDeferredType -> isGenericObjectType)
declare const emptyObjectSymbol: unique symbol;
type EmptyObject = {[emptyObjectSymbol]?: never};
type B1 = Required<{a: string}> extends EmptyObject ? 'empty' : 'full';
declare const b1: B1;
type V6<T> = T extends infer U ? U extends EmptyObject ? 'empty' : 'full' : never;
type B2 = V6<{a: string}>;
declare const b2: B2;
type V7<T> = Required<T> extends infer U ? U extends EmptyObject ? 'empty' : 'full' : never;
type B3 = V7<{a: string}>;
declare const b3: B3;
type V8<T> = Required<T> extends infer U ? [U] extends [EmptyObject] ? 'empty' : 'full' : never;
type B4 = V8<{a: string}>;
declare const b4: B4;
type V9<T> = Partial<T> extends infer U ? U extends {x: 1} ? 'empty' : 'full' : never;
type B5 = V9<{a: string}>;
declare const b5: B5;
