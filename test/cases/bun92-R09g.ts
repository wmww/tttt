// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Primitive = string | number | boolean | bigint | symbol | null | undefined;
type Json<T> = T extends Primitive ? T : { [K in keyof T]: Json<T[K]> };
class Model<Shape> { declare shape: Shape; toJSON(): Shape { return this.shape; } }
class User extends Model<Json<User>> { name = ""; age = 0; }
class Post extends Model<Json<Post>> { author = new User(); }
const u = new User().toJSON(); const n: number = u.name;
const p = new Post().toJSON().author.age; const q: string = p;
const sentinel: number = "";
