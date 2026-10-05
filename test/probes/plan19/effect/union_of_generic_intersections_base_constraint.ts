// @strict: true
type NoExcess<T, U> = T & { readonly [K in Exclude<keyof U, keyof T>]: never };
declare function make(l: (key: string) => number): void;
declare function fromRecord(l: Record<string, number>): void;
function f<const O extends NoExcess<{ readonly lookup: (key: string) => number; readonly d?: number }, O> | NoExcess<{ readonly layers: Record<string, number>; readonly d?: number }, O>>(o: O) {
	"lookup" in o ? make(o.lookup) : fromRecord(o.layers);
}
function g<const O extends { readonly lookup: (key: string) => number } | { readonly layers: Record<string, number> }>(o: O) {
	"lookup" in o ? make(o.lookup) : fromRecord(o.layers);
}
