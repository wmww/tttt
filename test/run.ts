function func() {
    console.log('hello from func');
    globe = 'global is now changed';
}

console.log('Hello, World');
let a = 2 + 2;
console.log('a:', a);
let globe = 'global set in global scope'
func();
console.log(globe);

// this is a comment

if (1 === 2) {
    console.log('if arm');
    /* multiline
     * comment
     */
} else if ('44' == 44) {
    console.log('else if arm');
} else {
    console.log('else arm');
}

for (let i = 0; i < 10; i++) {
    console.log(i);
}

while (a > 0) {
    console.log(String(a) + a);
    //console.log();
    --a;
}
