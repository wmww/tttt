// @filename: a.ts
export let foo = 3;
// @filename: b.ts
export {foo} from './a'
// @filename: c.ts
import {foo} from './b'
let x = foo;
