// @strict: true
enum Flags { RO = '__ro', N = 'n' }
const K = 'k'
class C {
  readonly __ro: boolean
  n: number
  k: string
  constructor(s: boolean) {
    this[Flags.RO] = !s
    this[Flags.N] = 1
    this[K] = ''
  }
}
