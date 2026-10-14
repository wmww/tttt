# Plan 68: `Iterator.from` prints its union sorted (builtinIterator, (await)usingDeclarationsWithIteratorObject)

Written 2026-10-14 (agent D, second fan-out task, time-boxed). Status: **the premise of the task was wrong**; the
real mechanism is not found yet. Next step is an instrumented tsgo, not an implementation.

## The three tests
They fail only on `Iterator.from`'s type: tsgo prints
`<T>(value: Iterable<T, unknown, undefined> | Iterator<T, unknown, undefined>) => IteratorObject<T, undefined, unknown>`,
tttt prints the annotation as written in lib.es2025.iterator.d.ts (`Iterator<...> | Iterable<...>`).

## What was tried, and what refutes it
The previous note (display.md, "Generic methods of a class or non-thisless interface get fresh type parameters")
said: `IteratorConstructor` has a `this` type (it extends `IteratorObjectConstructor`), `from` isn't thisless (a union
annotation never is: isThislessType), so its signature is instantiated through the reference with fresh type
parameters (instantiateSignature clones them) and the annotation isn't reused.
- Implemented in tttt the tsgo way for the part tttt lacked: instantiate_anonymous() of a declared method keeps the
  owner's type parameters and `this` when the owner has several declarations (isTypeParameterPossiblyReferenced wants
  exactly one; IteratorConstructor has two: es2025.iterator and es2026.iterator). No effect on the tests (tttt's
  instantiated signature still reuses the annotation), reverted.
- tsgo probes (scripts/tscbaseline.sh) refute the mechanism itself:
  - `type F = IteratorConstructor["from"]` (the declared members, no this-instantiation, no clone) prints sorted too.
  - User interfaces built the same way keep the written order in tsgo: two declarations, `extends Object`, `extends
    typeof SomeAbstractClass`, generic `from<T>(value: Iterator<T> | Iterable<T>)`: all print as written.
  - Lib signatures elsewhere are reused as written (`Array.from`'s `Iterable<T> | ArrayLike<T>`, without defaults).
So cloning (if it happens) doesn't stop reuse; the annotation in this one lib file isn't reusable.

## What is special about it
lib.es2025.iterator.d.ts is a module (`export {};`, one of two lib modules with es2026.json) whose `declare global`
block writes `Iterator<...>`: that name resolves to the module-local `declare abstract class Iterator` merged with the
module-local `interface Iterator ... extends globalThis.IteratorObject` (the block declares no `Iterator` itself),
while at the printing location `Iterator` is the global interface. Yet a user-file replica of exactly that layout
(a module with the local class + interface, `declare global { interface Con { from<T>(value: Iterator<T, unknown,
undefined> | Iterable<T, unknown, undefined>): T } var con: Con }`, printed from another file) keeps the written order
in tsgo. So it depends on the file being a default lib (or on how lib modules are bound/resolved), which a probe
can't vary.

## Next step (one session)
Instrument tsgo (memory: "Instrument tsgo", the test/oracle overlay) in the node builder's reuse decision for a
parameter annotation (tryReuseExistingTypeNode / serializeExistingTypeNode and the entity-name tracking it calls,
plus typeNodeIsEquivalentToType) and run builtinIterator: log why `value`'s annotation is rejected. Then port that
rule to ty_display's annotation reuse (identifier_reuse()/simple_node_fails()). If the reason is "a lib file that is a
module", check how tttt binds lib modules' `declare global` blocks first (the symbols baseline of these tests passes).

## Numbers
None worth recording: the attempted change was behaviour-neutral on the three tests and reverted before measuring.
