interface Foo {
    a: boolean,
    b: any,
    c: {
        a: undefined,
        c: null
    }
}

interface Bar {
    xyz: extends boolean boolean ? string : number
}
