class P {
    constructor(public x: number, private y: string, readonly z = 1) {}
    m() { return this.x + this.y.length + this.z; }
}
const p = new P(1, "a");
const a = p.x;
const b = p.z;
function f() {}
f.prop = 1;
f.prop;
