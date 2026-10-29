// A parameter's modifiers followed by a reserved word (`export const a`): the word is the name tsc's
// parseBindingIdentifier rejects (TS1359), left for the list, which makes it a parameter of its own, missing its name
// too (TS2300 for the two '(Missing)'). Only a parameter without modifiers skips a modifier word as its missing name
// (parseNameOfParameter's `!some(modifiers)`). Seed 20261116 --syntax, contextualTypingGenericFunction2 delete-stmt
declare function f(export const a): void;
declare function g(public const b): void;
declare function h(const c): void;
