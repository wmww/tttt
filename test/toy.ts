interface Foo { a: boolean, b: any, c: { a: undefined, c: null } }

interface Bar { xyz: boolean extends boolean ? string : number }

let a: boolean = true;
