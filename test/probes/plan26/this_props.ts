// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js
class C {
    constructor() {
        this.a = 1;
        this.b = "x";
    }
    m() {
        this.c = true;
        this.a = 2;
    }
    static s() {
        this.st = 1;
    }
}
var c = new C();
c.a;
c.b;
c.c;
C.st;

function F() {
    this.x = 1;
}
F.prototype.m = function () {
    this.y = "";
};
var f = new F();
f.x;
f.y;

var G = function () { this.id = 1; };
var g = new G();
g.id;
