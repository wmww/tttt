// tsc: TS2353 on nonexistent (the SkipGenericFunctions round leaves C at its constraint, raise(1) being silentNever)
type AF<T> = { (x: T): void; _t?: T };
declare function raise<T>(t: T): AF<T>;
declare function cm<const C extends { states?: Record<string, { entry?: readonly AF<any>[]; after?: { long?: string } }> }>(c: C): C;
cm({ states: { a: { entry: [raise(1)] }, c: { after: { long: 'd', nonexistent: 'd' } } } });
