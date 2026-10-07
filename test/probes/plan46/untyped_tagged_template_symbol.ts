let x!: symbol;
declare let f: any;
f`${x}`;
undefinedTag`${x}`;
declare function g(t: TemplateStringsArray, v: symbol): void;
g`${x}`;
