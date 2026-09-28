// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
/** @returns {any} */
function read() { return null; }
class Base {
    /** @param {{ a: string, b: RegExp | null }} options */
    constructor(options) {
        this.options = options;
        if (this.options.b) {
            this.options = { ...this.options, b: null };
        }
    }
    deserialize() {
        this.options = read();
    }
}
class Sub extends Base {
    get() {
        /** @type {number} */
        const a = this.options;
    }
}
/** @type {number} */
const b = new Base({ a: "", b: null }).options;
