// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Item { value: number; next: Item | null; items: Item[] }
declare const item: Item;
declare const more: boolean;
declare function id<T>(x: T): T;
export function copy(first: Item | null) {
  var newFirst = null, newLast = null;
  var at = first;
  if (null !== at) {
    do {
      var clone: Item = { value: at.value, next: null, items: [] };
      null === newLast ? (newFirst = newLast = clone) : (newLast = newLast.next = clone);
      at = at.next;
    } while (null !== at);
  }
  return newFirst;
}
export function declared() {
  let last: Item | null = null;
  while (more) {
    if (last === null) last = item;
    else last = last.next = item;
  }
}
export function conditional() {
  var last = null;
  while (more) null === last ? (last = item) : (last = last.next = item);
}
export function forever() {
  let last = null;
  for (;;) {
    if (last !== null) last = last.next = item;
    else last = item;
  }
}
export function comma() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = (last.next = item, item);
  }
}
export function argument() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = id(last.next = item);
  }
}
export function elementAccess() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = last["next"] = item;
  }
}
export function destructuring() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = ([last.next] = [item])[0];
  }
}
export function fromUndefined() {
  var last;
  while (more) {
    if (last === undefined) last = item;
    else last = last.next = item;
  }
}
export function truthiness() {
  var last = null;
  while (more) {
    if (!last) last = item;
    else last = last.next = item;
  }
}
// No error in these.
export function twoStatements() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else { last.next = item; last = item; }
  }
}
export function compound() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = (last.value += 1, item);
  }
}
export function read() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = last.next;
  }
}
export function call() {
  var last = null;
  while (more) {
    if (last === null) last = item;
    else last = id(last.items.push(item) ? item : item);
  }
}
export function noLoop() {
  var last = null;
  if (more) last = item;
  if (last !== null) last = last.next = item;
}
const sentinel: number = "";
