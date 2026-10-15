// @declaration: true
// A local abstract class written as a type literal: its abstract construct signature alone is a function type (the
// class's prototype isn't written), parenthesized where the context needs it
export function F<T>(x: T) {
  abstract class P { data!: T[]; }
  return P;
}
export function G<T>(x: T) {
  class P { data!: T[]; }
  return P;
}
export function H<T>(x: T) {
  abstract class P { data!: T[]; }
  return [P];
}
export function J<T>(x: T) {
  abstract class P { data!: T[]; static s = 1; }
  return P;
}
export function K<T>(x: T) {
  class P { data!: T[]; }
  return x ? P : undefined;
}
export function L<T>(x: T) {
  abstract class P { data!: T[]; }
  return x ? P : undefined;
}
export function M<T>(x: T) {
  abstract class P { data!: T[]; static s = 1; }
  return x ? P : undefined;
}
