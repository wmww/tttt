// @allowUnreachableCode: false
// An `else if` is an if statement of its own, where unreachable code can start (TS7027 from its `if` to the end;
// hacks.md: tttt had no Statement for it and reported the branch's block instead)
declare var a: boolean, b: boolean;
function f() {
    if (true) { return; } else if (b) { a; }
}
function g() {
    if (a) { } else if (true) { return; } else /* c */ if (b) { a; } else { b; }
}
function h() {
    while (true) { }
    if (a) { } else if (b) { }
}
function k() {
    if (true) { return; } else if (b) { a; } else if (a) { b; }
    a;
}
