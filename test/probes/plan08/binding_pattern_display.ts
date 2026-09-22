function f({ a = 1, b: { c } = { c: 1 }, ["x"]: y, ...r }: any, [d = 2, , ...e]: any[], { g: gg = 3 }: any) {}
const h = ({ a = 1 }) => a;
