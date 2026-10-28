// @strict: true
// @target: esnext
// @noEmit: true
// A field without a type or initializer that the constructor or static blocks only assign null or undefined infers no
// type from them (tsc's getFlowTypeInConstructor, getFlowTypeInStaticBlocks): TS7008; still the auto type at the end
// of the constructor is TS7008 too (found by mutation of controlFlowAutoAccessor1)
declare var n: number;
declare var a: any;
class Example4 { static accessor value; static { if (n < 0) { this.value = null; } } }
class Example5 { static value; static { if (n < 0) { this.value = null; } } }
class Example6 { accessor value; constructor() { if (n < 0) { this.value = null; } } }
class A1 { x; constructor() { this.x = undefined; } }
class A2 { x; constructor() { this.x = []; } }
class A3 { x; constructor() { this.x = []; this.x.push(1); } }
class A4 { x; constructor() { this.x = a; } }
class A5 { x; constructor() { throw 1; this.x = 1; } }
class A6 { x; constructor() { if (n) this.x = 1; else this.x = null; } }
class A7 { static x; static { this.x = null; } static { this.x = 1; } }
class A8 { x; y; constructor() { this.y = this.x; this.x = null; } }
