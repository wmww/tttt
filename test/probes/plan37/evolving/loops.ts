// @strict: true
declare function cond(): boolean;
declare const items: string[];

function f1() {
    const x = [];
    for (let i = 0; i < 10; i++) {
        x.push(i);
        if (cond()) x.push("a");
    }
    return x;
}

function f2() {
    let x = [];
    while (cond()) {
        if (x.length > 3) break;
        x.push(true);
    }
    const y = x;
    return y;
}

function f3() {
    let x;
    x = [];
    for (const s of items) {
        x.push(s.length);
    }
    x;
    x = 1;
    x;
}

function f4() {
    let x = [];
    do {
        x.unshift(null);
        x[x.length] = 1;
    } while (cond());
    return x;
}

function f5() {
    let x = [];
    x.push(...items);
    x.push({ a: 1 });
    return x;
}
