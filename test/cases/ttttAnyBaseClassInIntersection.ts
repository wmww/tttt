// A class whose base is any is a non-augmenting subtype normalized to any, assignable to any object type on its own, but
// as a member of an intersection source the optional properties' extra check still asks for the target's required ones
// (tsc's propertiesRelatedTo calls getUnmatchedProperty for optionalsOnly too): TS2741 (seed 20261111)
class Other<T> { constructor(public other: T) {} }
declare const anyB: any;
class B extends anyB {}
declare const b: B;
const o1: Other<string> = b;
declare const b2: B & { x: 1 };
const o2: Other<string> = b2;
