// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts", "array_method_for_cross.ts", "array_method_while_cross.ts", "array_chained_do_self.ts", "array_chained_while_cross.ts", "array_new_for_of_both.ts", "array_new_while_cross.ts", "array_property_do_cross.ts", "swapped_method_do_self.ts", "swapped_method_while_cross.ts", "swapped_chained_for_of_both.ts", "swapped_chained_while_cross.ts", "swapped_new_for_cross.ts", "swapped_new_while_cross.ts", "swapped_element_for_of_self.ts", "object_method_for_of_both.ts", "object_method_while_cross.ts", "object_chained_for_cross.ts", "object_chained_while_cross.ts", "object_new_do_self.ts", "object_new_while_cross.ts", "object_generic_while_both.ts", "renamed_method_for_cross.ts", "renamed_method_while_cross.ts", "renamed_chained_do_self.ts", "renamed_chained_while_cross.ts", "renamed_new_for_of_both.ts", "renamed_new_while_cross.ts", "renamed_overloaded_for_cross.ts", "nested_array_method_do_self.ts", "nested_array_method_while_cross.ts", "nested_array_chained_for_of_both.ts", "nested_array_chained_while_cross.ts", "nested_array_new_for_cross.ts", "nested_array_new_while_cross.ts", "nested_array_free_do_self.ts", "nested_object_method_for_of_both.ts", "nested_object_method_while_cross.ts", "nested_object_chained_for_cross.ts", "nested_object_chained_while_cross.ts", "nested_object_new_do_self.ts", "nested_object_new_while_cross.ts", "nested_object_property_for_of_both.ts", "mixed_method_for_cross.ts", "mixed_method_while_cross.ts", "mixed_chained_do_self.ts", "mixed_chained_while_cross.ts", "mixed_new_for_of_both.ts", "mixed_new_while_cross.ts", "mixed_element_while_cross.ts", "default_method_do_self.ts", "default_method_while_cross.ts", "default_chained_for_of_both.ts", "default_chained_while_cross.ts", "default_new_for_cross.ts", "default_new_while_cross.ts", "default_generic_for_self.ts", "rest_array_method_for_of_both.ts", "rest_array_method_while_cross.ts", "rest_array_chained_for_cross.ts", "rest_array_chained_while_cross.ts", "rest_array_new_do_self.ts", "rest_array_new_while_cross.ts", "rest_array_overloaded_do_both.ts", "rest_object_method_for_cross.ts", "rest_object_method_while_cross.ts", "rest_object_chained_do_self.ts", "rest_object_chained_while_cross.ts", "rest_object_new_for_of_both.ts", "rest_object_new_while_cross.ts", "rest_object_free_for_of_cross.ts", "three_method_do_self.ts", "three_method_while_cross.ts", "three_chained_for_of_both.ts", "three_chained_while_cross.ts", "three_new_for_cross.ts", "three_new_while_cross.ts", "three_property_while_self.ts", "hole_method_for_of_both.ts", "hole_method_while_cross.ts", "hole_chained_for_cross.ts", "hole_chained_while_cross.ts", "hole_new_do_self.ts", "hole_new_while_cross.ts", "hole_element_for_both.ts", "sentinel.ts"]}
// @filename: array_chained_do_self.ts
interface A { a: 1; q(): { limit(): [A | B, number] } }
interface B { b: 1; q(): { limit(): [B, string] } }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, other] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: array_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [B, number] } }
interface B { b: 1; q(): { limit(): [A, string] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: array_method_for_cross.ts
interface A { a: 1; limit(): [B, number] }
interface B { b: 1; limit(): [A, string] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [next, other] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: array_method_while_cross.ts
interface A { a: 1; limit(): [B, number] }
interface B { b: 1; limit(): [A, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: array_new_for_of_both.ts
interface A { a: 1; C: new () => [A | B, number] }
interface B { b: 1; C: new () => [A | B, string] }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [next, other] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: array_new_while_cross.ts
interface A { a: 1; C: new () => [B, number] }
interface B { b: 1; C: new () => [A, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: array_property_do_cross.ts
interface A { a: 1; readonly limit: [B, number] }
interface B { b: 1; readonly limit: [A, string] }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, other] = cursor.limit;
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: default_chained_for_of_both.ts
interface A { a: 1; q(): { limit(): [A | B, number | undefined] } }
interface B { b: 1; q(): { limit(): [A | B, string | undefined] } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [next, other = null] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: default_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [B, number | undefined] } }
interface B { b: 1; q(): { limit(): [A, string | undefined] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other = null] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: default_generic_for_self.ts
interface A { a: 1; limit<X>(x: X): [A | B, number | undefined] }
interface B { b: 1; limit<X>(x: X): [B, string | undefined] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [next, other = null] = cursor.limit(1);
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: default_method_do_self.ts
interface A { a: 1; limit(): [A | B, number | undefined] }
interface B { b: 1; limit(): [B, string | undefined] }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, other = null] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: default_method_while_cross.ts
interface A { a: 1; limit(): [B, number | undefined] }
interface B { b: 1; limit(): [A, string | undefined] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other = null] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: default_new_for_cross.ts
interface A { a: 1; C: new () => [B, number | undefined] }
interface B { b: 1; C: new () => [A, string | undefined] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [next, other = null] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: default_new_while_cross.ts
interface A { a: 1; C: new () => [B, number | undefined] }
interface B { b: 1; C: new () => [A, string | undefined] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other = null] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: hole_chained_for_cross.ts
interface A { a: 1; q(): { limit(): [number, number, B, number] } }
interface B { b: 1; q(): { limit(): [string, string, A, string] } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [, other, next, third] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: hole_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [number, number, B, number] } }
interface B { b: 1; q(): { limit(): [string, string, A, string] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [, other, next, third] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: hole_element_for_both.ts
interface A { a: 1; limit(): [number, number, A | B, number][] }
interface B { b: 1; limit(): [string, string, A | B, string][] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [, other, next, third] = cursor.limit()[0]!;
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: hole_method_for_of_both.ts
interface A { a: 1; limit(): [number, number, A | B, number] }
interface B { b: 1; limit(): [string, string, A | B, string] }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [, other, next, third] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: hole_method_while_cross.ts
interface A { a: 1; limit(): [number, number, B, number] }
interface B { b: 1; limit(): [string, string, A, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [, other, next, third] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: hole_new_do_self.ts
interface A { a: 1; C: new () => [number, number, A | B, number] }
interface B { b: 1; C: new () => [string, string, B, string] }
declare const a: A;
let cursor: A | B = a;
do {
  const [, other, next, third] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
} while (cursor);
export {};
// @filename: hole_new_while_cross.ts
interface A { a: 1; C: new () => [number, number, B, number] }
interface B { b: 1; C: new () => [string, string, A, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [, other, next, third] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: index.ts
interface A { limit(): [A | B, number] }
interface B { limit(): [A | B, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor.limit) {
  const [next, other] = cursor.limit();
  const r: never = other;
  cursor = next;
}
export {};
// @filename: mixed_chained_do_self.ts
interface A { a: 1; q(): { limit(): { a: [A | B, number] } } }
interface B { b: 1; q(): { limit(): { a: [B, string] } } }
declare const a: A;
let cursor: A | B = a;
do {
  const { a: [next, other] } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: mixed_chained_while_cross.ts
interface A { a: 1; q(): { limit(): { a: [B, number] } } }
interface B { b: 1; q(): { limit(): { a: [A, string] } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: [next, other] } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: mixed_element_while_cross.ts
interface A { a: 1; limit(): { a: [B, number] }[] }
interface B { b: 1; limit(): { a: [A, string] }[] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: [next, other] } = cursor.limit()[0]!;
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: mixed_method_for_cross.ts
interface A { a: 1; limit(): { a: [B, number] } }
interface B { b: 1; limit(): { a: [A, string] } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { a: [next, other] } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: mixed_method_while_cross.ts
interface A { a: 1; limit(): { a: [B, number] } }
interface B { b: 1; limit(): { a: [A, string] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: [next, other] } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: mixed_new_for_of_both.ts
interface A { a: 1; C: new () => { a: [A | B, number] } }
interface B { b: 1; C: new () => { a: [A | B, string] } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { a: [next, other] } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: mixed_new_while_cross.ts
interface A { a: 1; C: new () => { a: [B, number] } }
interface B { b: 1; C: new () => { a: [A, string] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: [next, other] } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_array_chained_for_of_both.ts
interface A { a: 1; q(): { limit(): [[A | B], [number]] } }
interface B { b: 1; q(): { limit(): [[A | B], [string]] } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [[next], [other]] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_array_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [[B], [number]] } }
interface B { b: 1; q(): { limit(): [[A], [string]] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [[next], [other]] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_array_free_do_self.ts
interface A { a: 1; tag: [[A | B], [number]] }
interface B { b: 1; tag: [[B], [string]] }
declare function pick(c: A): A["tag"];
declare function pick(c: B): B["tag"];
declare function pick(c: A | B): (A | B)["tag"];
declare const a: A;
let cursor: A | B = a;
do {
  const [[next], [other]] = pick(cursor);
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: nested_array_method_do_self.ts
interface A { a: 1; limit(): [[A | B], [number]] }
interface B { b: 1; limit(): [[B], [string]] }
declare const a: A;
let cursor: A | B = a;
do {
  const [[next], [other]] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: nested_array_method_while_cross.ts
interface A { a: 1; limit(): [[B], [number]] }
interface B { b: 1; limit(): [[A], [string]] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [[next], [other]] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_array_new_for_cross.ts
interface A { a: 1; C: new () => [[B], [number]] }
interface B { b: 1; C: new () => [[A], [string]] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [[next], [other]] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_array_new_while_cross.ts
interface A { a: 1; C: new () => [[B], [number]] }
interface B { b: 1; C: new () => [[A], [string]] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [[next], [other]] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_chained_for_cross.ts
interface A { a: 1; q(): { limit(): { a: { next: B }; b: { other: number } } } }
interface B { b: 1; q(): { limit(): { a: { next: A }; b: { other: string } } } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { a: { next }, b: { other } } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_chained_while_cross.ts
interface A { a: 1; q(): { limit(): { a: { next: B }; b: { other: number } } } }
interface B { b: 1; q(): { limit(): { a: { next: A }; b: { other: string } } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: { next }, b: { other } } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_method_for_of_both.ts
interface A { a: 1; limit(): { a: { next: A | B }; b: { other: number } } }
interface B { b: 1; limit(): { a: { next: A | B }; b: { other: string } } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { a: { next }, b: { other } } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_method_while_cross.ts
interface A { a: 1; limit(): { a: { next: B }; b: { other: number } } }
interface B { b: 1; limit(): { a: { next: A }; b: { other: string } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: { next }, b: { other } } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_new_do_self.ts
interface A { a: 1; C: new () => { a: { next: A | B }; b: { other: number } } }
interface B { b: 1; C: new () => { a: { next: B }; b: { other: string } } }
declare const a: A;
let cursor: A | B = a;
do {
  const { a: { next }, b: { other } } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: nested_object_new_while_cross.ts
interface A { a: 1; C: new () => { a: { next: B }; b: { other: number } } }
interface B { b: 1; C: new () => { a: { next: A }; b: { other: string } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { a: { next }, b: { other } } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: nested_object_property_for_of_both.ts
interface A { a: 1; readonly limit: { a: { next: A | B }; b: { other: number } } }
interface B { b: 1; readonly limit: { a: { next: A | B }; b: { other: string } } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { a: { next }, b: { other } } = cursor.limit;
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_chained_for_cross.ts
interface A { a: 1; q(): { limit(): { next: B; other: number } } }
interface B { b: 1; q(): { limit(): { next: A; other: string } } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { next, other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_chained_while_cross.ts
interface A { a: 1; q(): { limit(): { next: B; other: number } } }
interface B { b: 1; q(): { limit(): { next: A; other: string } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_generic_while_both.ts
interface A { a: 1; limit<X>(x: X): { next: A | B; other: number } }
interface B { b: 1; limit<X>(x: X): { next: A | B; other: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, other } = cursor.limit(1);
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_method_for_of_both.ts
interface A { a: 1; limit(): { next: A | B; other: number } }
interface B { b: 1; limit(): { next: A | B; other: string } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { next, other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_method_while_cross.ts
interface A { a: 1; limit(): { next: B; other: number } }
interface B { b: 1; limit(): { next: A; other: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: object_new_do_self.ts
interface A { a: 1; C: new () => { next: A | B; other: number } }
interface B { b: 1; C: new () => { next: B; other: string } }
declare const a: A;
let cursor: A | B = a;
do {
  const { next, other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: object_new_while_cross.ts
interface A { a: 1; C: new () => { next: B; other: number } }
interface B { b: 1; C: new () => { next: A; other: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_chained_do_self.ts
interface A { a: 1; q(): { limit(): { n: A | B; o: number } } }
interface B { b: 1; q(): { limit(): { n: B; o: string } } }
declare const a: A;
let cursor: A | B = a;
do {
  const { n: next, o: other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: renamed_chained_while_cross.ts
interface A { a: 1; q(): { limit(): { n: B; o: number } } }
interface B { b: 1; q(): { limit(): { n: A; o: string } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { n: next, o: other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_method_for_cross.ts
interface A { a: 1; limit(): { n: B; o: number } }
interface B { b: 1; limit(): { n: A; o: string } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { n: next, o: other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_method_while_cross.ts
interface A { a: 1; limit(): { n: B; o: number } }
interface B { b: 1; limit(): { n: A; o: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { n: next, o: other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_new_for_of_both.ts
interface A { a: 1; C: new () => { n: A | B; o: number } }
interface B { b: 1; C: new () => { n: A | B; o: string } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { n: next, o: other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_new_while_cross.ts
interface A { a: 1; C: new () => { n: B; o: number } }
interface B { b: 1; C: new () => { n: A; o: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { n: next, o: other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: renamed_overloaded_for_cross.ts
interface A { a: 1; limit(): { n: B; o: number }; limit(x: 1): { n: B; o: number } }
interface B { b: 1; limit(): { n: A; o: string }; limit(x: 1): { n: A; o: string } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { n: next, o: other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_chained_for_cross.ts
interface A { a: 1; q(): { limit(): [B, number, number] } }
interface B { b: 1; q(): { limit(): [A, string, string] } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [next, ...other] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [B, number, number] } }
interface B { b: 1; q(): { limit(): [A, string, string] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, ...other] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_method_for_of_both.ts
interface A { a: 1; limit(): [A | B, number, number] }
interface B { b: 1; limit(): [A | B, string, string] }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [next, ...other] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_method_while_cross.ts
interface A { a: 1; limit(): [B, number, number] }
interface B { b: 1; limit(): [A, string, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, ...other] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_new_do_self.ts
interface A { a: 1; C: new () => [A | B, number, number] }
interface B { b: 1; C: new () => [B, string, string] }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, ...other] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: rest_array_new_while_cross.ts
interface A { a: 1; C: new () => [B, number, number] }
interface B { b: 1; C: new () => [A, string, string] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, ...other] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_array_overloaded_do_both.ts
interface A { a: 1; limit(): [A | B, number, number]; limit(x: 1): [A | B, number, number] }
interface B { b: 1; limit(): [A | B, string, string]; limit(x: 1): [A | B, string, string] }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, ...other] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: rest_object_chained_do_self.ts
interface A { a: 1; q(): { limit(): { next: A | B; o: number; p: number } } }
interface B { b: 1; q(): { limit(): { next: B; o: string; p: string } } }
declare const a: A;
let cursor: A | B = a;
do {
  const { next, ...other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: rest_object_chained_while_cross.ts
interface A { a: 1; q(): { limit(): { next: B; o: number; p: number } } }
interface B { b: 1; q(): { limit(): { next: A; o: string; p: string } } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, ...other } = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_object_free_for_of_cross.ts
interface A { a: 1; tag: { next: B; o: number; p: number } }
interface B { b: 1; tag: { next: A; o: string; p: string } }
declare function pick(c: A): A["tag"];
declare function pick(c: B): B["tag"];
declare function pick(c: A | B): (A | B)["tag"];
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { next, ...other } = pick(cursor);
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_object_method_for_cross.ts
interface A { a: 1; limit(): { next: B; o: number; p: number } }
interface B { b: 1; limit(): { next: A; o: string; p: string } }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const { next, ...other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_object_method_while_cross.ts
interface A { a: 1; limit(): { next: B; o: number; p: number } }
interface B { b: 1; limit(): { next: A; o: string; p: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, ...other } = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_object_new_for_of_both.ts
interface A { a: 1; C: new () => { next: A | B; o: number; p: number } }
interface B { b: 1; C: new () => { next: A | B; o: string; p: string } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const { next, ...other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: rest_object_new_while_cross.ts
interface A { a: 1; C: new () => { next: B; o: number; p: number } }
interface B { b: 1; C: new () => { next: A; o: string; p: string } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const { next, ...other } = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: sentinel.ts
export const sentinel: number = "";
// @filename: swapped_chained_for_of_both.ts
interface A { a: 1; q(): { limit(): [number, A | B] } }
interface B { b: 1; q(): { limit(): [string, A | B] } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [other, next] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: swapped_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [number, B] } }
interface B { b: 1; q(): { limit(): [string, A] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [other, next] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: swapped_element_for_of_self.ts
interface A { a: 1; limit(): [number, A | B][] }
interface B { b: 1; limit(): [string, B][] }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [other, next] = cursor.limit()[0]!;
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: swapped_method_do_self.ts
interface A { a: 1; limit(): [number, A | B] }
interface B { b: 1; limit(): [string, B] }
declare const a: A;
let cursor: A | B = a;
do {
  const [other, next] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
} while (cursor);
export {};
// @filename: swapped_method_while_cross.ts
interface A { a: 1; limit(): [number, B] }
interface B { b: 1; limit(): [string, A] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [other, next] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: swapped_new_for_cross.ts
interface A { a: 1; C: new () => [number, B] }
interface B { b: 1; C: new () => [string, A] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [other, next] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: swapped_new_while_cross.ts
interface A { a: 1; C: new () => [number, B] }
interface B { b: 1; C: new () => [string, A] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [other, next] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  cursor = next;
}
export {};
// @filename: three_chained_for_of_both.ts
interface A { a: 1; q(): { limit(): [A | B, number, number[]] } }
interface B { b: 1; q(): { limit(): [A | B, string, string[]] } }
declare const a: A;
let cursor: A | B = a;
for (const _ of [1]) {
  const [next, other, third] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: three_chained_while_cross.ts
interface A { a: 1; q(): { limit(): [B, number, number[]] } }
interface B { b: 1; q(): { limit(): [A, string, string[]] } }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other, third] = cursor.q().limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: three_method_do_self.ts
interface A { a: 1; limit(): [A | B, number, number[]] }
interface B { b: 1; limit(): [B, string, string[]] }
declare const a: A;
let cursor: A | B = a;
do {
  const [next, other, third] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
} while (cursor);
export {};
// @filename: three_method_while_cross.ts
interface A { a: 1; limit(): [B, number, number[]] }
interface B { b: 1; limit(): [A, string, string[]] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other, third] = cursor.limit();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: three_new_for_cross.ts
interface A { a: 1; C: new () => [B, number, number[]] }
interface B { b: 1; C: new () => [A, string, string[]] }
declare const a: A;
let cursor: A | B = a;
for (;;) {
  const [next, other, third] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: three_new_while_cross.ts
interface A { a: 1; C: new () => [B, number, number[]] }
interface B { b: 1; C: new () => [A, string, string[]] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other, third] = new cursor.C();
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
// @filename: three_property_while_self.ts
interface A { a: 1; readonly limit: [A | B, number, number[]] }
interface B { b: 1; readonly limit: [B, string, string[]] }
declare const a: A;
let cursor: A | B = a;
while (cursor) {
  const [next, other, third] = cursor.limit;
  const probe_next: never = next;
  const probe_other: never = other;
  const probe_third: never = third;
  cursor = next;
}
export {};
