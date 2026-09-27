// @strict: true
type Types = {
    first: { a1: true };
    second: { a2: true };
    third: { a3: true };
}
class Test {
    entries: { [T in keyof Types]?: Types[T][] };
    constructor() {
        this.entries = {};
    }
    addEntry<T extends keyof Types>(name: T, entry: Types[T]) {
        const e1 = this.entries[name];
        this.entries[name]?.push(entry);
    }
}
