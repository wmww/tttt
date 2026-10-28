// @strict: true
// `T[P]` with T extends never and P extends a non-key type has no write constraint: never indexed by Object is
// nothing without an access node (tsc's getPropertyTypeForIndexType), so writing a string to it is TS2322
function a<T extends never, P extends Object>(s: string, tp: T[P]): void { tp = s; }
function b<T extends never, P extends string>(s: string, tp: T[P]): void { tp = s; }
function c<T extends never, P extends {}>(s: string, tp: T[P]): void { tp = s; }
function d<T extends {a: 1}, P extends Object>(s: string, tp: T[P]): void { tp = s; }
