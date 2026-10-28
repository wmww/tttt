// @noEmit: true
// tsc's isTypeMemberStart: a name in a type literal or interface (after any modifiers) followed by something else
// than `(`, `<`, `?`, `:`, `,` or the end of a statement starts no member: the list ends there (TS1131) and what
// follows is a statement (found by mutating contextualTypingWithGenericSignature)
var f2: {
    <T, U>(x: T, y: U): T

f2 = (x, y) => { return x }
interface I { a: number; b = 1 }
type M<T> = { -readonly [K in keyof T]-?: T[K] };
