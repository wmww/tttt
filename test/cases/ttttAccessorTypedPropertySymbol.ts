// @target: esnext
// @strict: true
// A property symbol with accessors (an auto-accessor's too) is typed by them, whatever declaration came first: a later
// field is TS2717 against the accessor type, an auto-accessor after an optional field is not
class C {
  y?: number = 2;
  accessor y: number = 3;
}
class D {
  accessor y: number = 3;
  y?: number = 2;
}
class E {
  a?: string;
  get a(): number { return 1; }
  a: boolean;
}
class F {
  b: string;
  b: number;
  set b(v: boolean) {}
}
