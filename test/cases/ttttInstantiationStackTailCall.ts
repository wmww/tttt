// @noEmit: true
// The instantiation stack TS5114/TS5115 name the types of keeps an aliased conditional type's entry while its branch,
// a conditional type, is resolved as a tail call (tsgo's getConditionalType loop): the runaway is named 'Unwrap'
interface Box<T> { inner: Unwrap<T> }
type Unwrap<T> = T extends string ? "s" : T extends number ? "n" : Box<[T]>["inner"];
declare const b: Unwrap<boolean>;
