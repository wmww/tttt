var foo: {id:number;} = <{id:number;}>({ }); foo = {id: 5};
var v: {A: number; <T>; };
var r = <T>(x: T) => x;
var g = a => <any>{};
let h = ():void {};
let dot;
dot = <T, S>(f: (_: T) => S) => <U>(g: (_: U) => T): (r:U) => S => (x) => f(g(x));
