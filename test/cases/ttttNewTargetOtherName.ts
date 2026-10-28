// `new.targetX` (TS17012) is another reference than `new.target`: a narrowing of new.target.marked doesn't apply
// to it (tsc's isMatchingReference compares a MetaProperty's name)
// @target: es6
// @strict: true

function foo(x: true) { }

function f() {
  if (new.target.marked === true) {
    foo(new.targetX.marked);
  }
}

f.marked = true;
