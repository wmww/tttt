// An object literal property found on its contextual type makes no literal type of its name (tsc makes one only for
// index signatures and generic mapped types): k prints "calendar" | "timeZone"
declare function f(o: { timeZone?: string; calendar?: string }): void;
f({ timeZone: 'x' });
let k: 'calendar' | 'timeZone';
