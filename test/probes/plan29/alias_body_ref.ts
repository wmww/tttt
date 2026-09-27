interface Box<T> { v: T }
type B<T> = Box<T>;
type BS = Box<string>;
type M = Map<string, number>;
declare const b: B<string>;
declare const bs: BS;
declare const m: M;
