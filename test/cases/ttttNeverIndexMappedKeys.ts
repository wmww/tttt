// A mapped type over keys with an index signature indexed by never reads it, its keys known from its declaration
// instantiated (tsc's getIndexedAccessType reads the index signature: `Partial<Record<string, V>>[never]` is V | undefined)
type R<T> = { [K in keyof T]: number };
type X = R<{ [k: string]: 1 }>[never];
type Y = R<{ a: 1 }>[never];
type Z = Partial<Record<string, boolean>>[never];
