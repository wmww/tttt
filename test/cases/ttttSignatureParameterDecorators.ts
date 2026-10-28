// @target: esnext
// @noEmit: true
// A signature's parameters take decorators like a function's (tsc's parseParameter), which can't decorate them:
// TS1206 from the checker. A type is outside the await context, so `@await` there decorates with an identifier
declare const dec: any;
let a: new (@dec y: any) => void;
let b: <T>(@dec y: T) => void;
let c: { m(@dec y: any): void; (@dec y: any): void; new (@dec y: any): void };
interface I { m(@dec y: any): void; (@dec y: any): void; new (@dec y: any): void }
async function f() {
    let x: new (@await y: any) => void;
    let x2: abstract new (a: any, @await.b y: any) => void;
    let x3: { m(@await y: any): void };
}
