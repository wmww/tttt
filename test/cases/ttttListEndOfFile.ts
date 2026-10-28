// @noEmit: true
// The end of the file ends every list (tsc's isListTerminator): the closing bracket is expected there, not a comma
// @filename: a.ts
type T<A
// @filename: b.ts
let x: { [k: string
// @filename: c.ts
const {
    a = 1,
    f = a  // error
// @filename: d.ts
const [
  b = 1,
  c
