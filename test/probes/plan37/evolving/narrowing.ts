// @strict: true
declare function assert(value: unknown): asserts value;
declare function isStrings(value: unknown): value is string[];
declare function cond(): boolean;

function f1() {
    let x = [];
    x.push(1);
    if (x.length) {
        x.push("a");
    }
    switch (x.length) {
        case 0: x.push(true); break;
    }
    assert(x);
    x.push(null);
    return x;
}

function f2() {
    let x = [];
    if (isStrings(x)) {
        x;
    }
    x.push(1);
    for (const k in x) {
        x.push(k);
    }
    return x;
}

function f3() {
    let s: string | undefined = undefined;
    s!;
    let t: string | undefined;
    t = undefined;
    t!;
}
