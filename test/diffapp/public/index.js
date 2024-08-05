const mainText = document.getElementById('main-text');

async function onRefresh() {
    let input = await (await fetch('/input')).text();
    let tscErr = await (await fetch('/tsc/err')).text();
    let tscTyp = await (await fetch('/tsc/typ')).text();
    let tscSym = await (await fetch('/tsc/sym')).text();
    mainText.value = (
        'Input:\n' + input + '\n' +
        'Errors:\n' + tscErr + '\n' +
        'Types:\n' + tscTyp + '\n' +
        'Symbols:\n' + tscSym + '\n'
    );
}
