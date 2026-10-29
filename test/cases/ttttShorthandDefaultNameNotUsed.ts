// A shorthand property with an initializer outside a destructuring target (TS1312) checks only the initializer: its
// name isn't a use, not even before its declaration (no TS2448)
const Category = {
    name: "a",
    Category = 1,
};
const other = { later = 2 };
let later = 1;
