// @strict: true
// The counterpart of ttttThisInstantiatedMethodErased: with the implementation's return type annotated, no declaration
// of `valid` possibly references `this` (tsc's isTypeParameterPossiblyReferenced), so the declared type's `valid` is
// the declared method type itself, `this` related to the union compares the signatures unerased, and it fails: TS2345
type Input = { in?: {}; out?: {} };
type VT = { json: any; form: string };
type ToData<T extends Input['out'], Target extends keyof VT> = T extends { [K in Target]: infer R } ? R : never;
interface R { r: string }
declare function parseBody(request: HonoRequest | R): void;

export class HonoRequest<I extends Input['out'] = {}> {
  parseBody() {
    return parseBody(this)
  }
  valid<T extends keyof I & keyof VT>(target: T): ToData<I, T>
  valid(target: keyof VT): any {
    return 0 as any
  }
}
