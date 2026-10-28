// @noEmit: true
// `({` is only ever tsc's Unknown arrow lookahead: a parenthesized object literal whose pattern reading breaks off at a
// `?` (`t: w ? u : 1`) is no `(a?` arrow function, and without `=>` the speculation fails and it is reparsed as an
// expression (openclaw's reply-tool-authority.test.ts, a regression of 39c88f5ec)
declare let v: any, w: any, u: any, t: any;
v = ({ g: "b", t: w ? u : 1 });
v = ({ g: 1, u })(1);
v = ([w, u]);
declare const x: any;
x.map((w: any) => ({ w, gk: "bound", t: w ? undefined : 1 }));
