import m = require("m");
declare module "m" { function file(path: string): void; }
m.ok();
m.file("x");
