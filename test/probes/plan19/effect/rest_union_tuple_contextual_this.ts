// @target: es2022
// @strict: true
declare const gen: <Self, AEff>(
  ...args: [self: Self, body: (this: Self) => Generator<number, AEff, never>] | [body: () => Generator<number, AEff, never>]
) => AEff
class C {
  x = 1
  y: number = gen(this, function*() { return this.x })
}
declare const gen2: <Self, AEff>(self: Self, body: (this: Self) => Generator<number, AEff, never>) => AEff
class D {
  x = 1
  y: number = gen2(this, function*() { return this.x })
}
const o = { x: 1 }
const z: number = gen(o, function*() { return this.x })
export {}
