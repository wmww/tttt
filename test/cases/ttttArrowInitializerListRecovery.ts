// @noEmit: true
// `(a =` is tsc's Tristate.Unknown: the arrow function is parsed speculatively, its parameter list recovers from what
// follows the initializer, and it stands because `=>` follows
let h = (a = 1 public g) => 1;
let k = (a = 1 b) => a;
