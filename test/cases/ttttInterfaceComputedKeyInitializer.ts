// A computed-key interface or type literal member with an initializer is TS1246/TS1247 and the rest of the
// declaration still parses.
declare const K: any;
interface T { [K]: number | undefined = undefined; }
interface U { a: number = 1; }
interface V { [K]: number = 1; }
interface W { [K]: (() => 1) | undefined = undefined; }
type L = { [K]: string = ""; b: number };
