// @strict: true
// @useDefineForClassFields: false
// @target: es2016
interface Link { a: number }
interface Sub { deps?: Link }
export class E implements Sub {
  deps?: Link = undefined
  readonly r: boolean
}
