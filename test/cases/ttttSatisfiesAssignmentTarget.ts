// `(x satisfies T) = v`: tsc's assignment-target walk doesn't go through `satisfies`, so x is read with its narrowed
// type and the value is checked against that (TS2322), whatever x is declared as. Seed 20261117,
// referenceSatisfiesExpression undefined-init
let e = undefined;
({ d: (e satisfies number) } = { d: 10 });
let f = undefined;
(f satisfies number) = 10;
let g = undefined;
[(g satisfies number)] = [10];
let n: number | string = 1;
(n satisfies number) = 2;
(n satisfies number) = "a";
