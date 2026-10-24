import "@lumora/runtime";

console.warn("x", { a: 1 });
const o: any = { b: 2 };
o.self = o;
console.log(o);
