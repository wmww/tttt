// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "lib": ["esnext"], "types": []}, "files": ["arrow_property__const__closure.ts", "arrow_property__const__direct.ts", "arrow_property__const__id.ts", "arrow_property__const__pair.ts", "arrow_property__const__typeof.ts", "arrow_property__const__wrap.ts", "arrow_property__method__chain.ts", "arrow_property__nested__chain.ts", "arrow_property__plain__chain.ts", "arrow_property__rest__chain.ts", "arrow_property__spread__chain.ts", "arrow_property__spread_t__chain.ts", "arrow_property__twice__chain.ts", "arrow_property__widened__chain.ts", "callback__const__chain.ts", "callback__spread_t__id.ts", "function__const__chain.ts", "generic__method__chain.ts", "generic__spread_t__wrap.ts", "generic_class__method__chain.ts", "generic_class__spread_t__pair.ts", "generic_method__method__chain.ts", "generic_method__rest__typeof.ts", "generic_obj__method__chain.ts", "generic_obj__spread_of_t__chain.ts", "generic_obj__spread_of_t__wrap.ts", "method__method__chain.ts", "nested__method__chain.ts", "nested__nested__closure.ts"]}
// @filename: arrow_property__const__closure.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = () => ({ ...value, more: value });
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__const__direct.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = value;
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__const__id.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = id(value);
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__const__pair.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = pair(value, id(value));
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__const__typeof.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = null! as { v: typeof value; k: keyof typeof value }[];
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__const__wrap.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t } as const;
    const result = wrap(value);
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__nested__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { o: { ...input, t }, l: [t] };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__plain__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { a: 1, t };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__rest__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const { kind, ...value } = { ...input, t };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__spread__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__spread_t__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, t };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__twice__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { ...input, ...{ t }, ...input };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: arrow_property__widened__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run = (input: Input, t: T) => {
    const value = { a: null, b: undefined, t };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: callback__const__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T>(input: Input, t: T) {
  let out!: unknown;
  on(t, t => {
    const value = { ...input, t } as const;
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  });
  return out;
}
const r1: never = run({}, 1);
const r2: never = run({}, "s");
export const sentinel: number = "";
// @filename: callback__spread_t__id.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T>(input: Input, t: T) {
  let out!: unknown;
  on(t, t => {
    const value = { ...input, t };
    const result = id(value);
    const inside: never = result;
    return result;
  });
  return out;
}
const r1: never = run({}, 1);
const r2: never = run({}, "s");
export const sentinel: number = "";
// @filename: function__const__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run(input: Input, t: string) {
    const value = { ...input, t } as const;
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
}
const r1: never = run({}, "s");
const r2: never = run({}, "s");
export const sentinel: number = "";
// @filename: generic__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T>(input: Input, t: T) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
}
const r1: never = run({}, 1);
const r2: never = run({}, "s");
export const sentinel: number = "";
// @filename: generic__spread_t__wrap.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T>(input: Input, t: T) {
    const value = { ...input, t };
    const result = wrap(value);
    const inside: never = result;
    return result;
}
const r1: never = run({}, 1);
const r2: never = run({}, "s");
export const sentinel: number = "";
// @filename: generic_class__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run(input: Input, t: T) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: generic_class__spread_t__pair.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<T> {
  constructor(readonly t0: T) {}
  run(input: Input, t: T) {
    const value = { ...input, t };
    const result = pair(value, id(value));
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, 1);
const r2: never = new C("s").run({}, "s");
export const sentinel: number = "";
// @filename: generic_method__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<U> {
  constructor(readonly u: U) {}
  run<T>(input: Input, t: T) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, true);
const r2: never = new C("s").run({}, 2);
export const sentinel: number = "";
// @filename: generic_method__rest__typeof.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C<U> {
  constructor(readonly u: U) {}
  run<T>(input: Input, t: T) {
    const { kind, ...value } = { ...input, t };
    const result = null! as { v: typeof value; k: keyof typeof value }[];
    const inside: never = result;
    return result;
  }
}
const r1: never = new C(1).run({}, true);
const r2: never = new C("s").run({}, 2);
export const sentinel: number = "";
// @filename: generic_obj__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T extends object>(input: Input, t: T) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
}
const r1: never = run({}, { x: 1 });
const r2: never = run({}, { y: "s" });
export const sentinel: number = "";
// @filename: generic_obj__spread_of_t__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T extends object>(input: Input, t: T) {
    const value = { ...t, a: 1 };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
}
const r1: never = run({}, { x: 1 });
const r2: never = run({}, { y: "s" });
export const sentinel: number = "";
// @filename: generic_obj__spread_of_t__wrap.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function run<T extends object>(input: Input, t: T) {
    const value = { ...t, a: 1 };
    const result = wrap(value);
    const inside: never = result;
    return result;
}
const r1: never = run({}, { x: 1 });
const r2: never = run({}, { y: "s" });
export const sentinel: number = "";
// @filename: method__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export class C {
  run(input: Input, t: string) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = new C().run({}, "s");
const r2: never = new C().run({}, "s");
export const sentinel: number = "";
// @filename: nested__method__chain.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function outer<T>(t: T) {
  return function run<U>(input: Input, u: U) {
    const value = { ...input, m() { return t } };
    const result = start(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .step(value)
      .done();
    const inside: never = result;
    return result;
  }
}
const r1: never = outer(1)({}, "s");
const r2: never = outer("s")({}, 1);
export const sentinel: number = "";
// @filename: nested__nested__closure.ts
interface Chain<I> { step<P>(x: P): Chain<I>; done(): I }
declare function start<I>(value: I): Chain<I>;
declare function id<X>(x: X): X;
declare function wrap<X>(x: X): { [K in keyof X]: X[K][] };
declare function pair<X, Y>(x: X, y: Y): [X, Y];
declare function on<E>(e: E, f: (e: E) => unknown): void;
type Input = { kind?: 'k1' | 'k2'; sub?: 's1' };
export function outer<T>(t: T) {
  return function run<U>(input: Input, u: U) {
    const value = { o: { ...input, t }, l: [t] };
    const result = () => ({ ...value, more: value });
    const inside: never = result;
    return result;
  }
}
const r1: never = outer(1)({}, "s");
const r2: never = outer("s")({}, 1);
export const sentinel: number = "";
