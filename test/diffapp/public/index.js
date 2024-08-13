const inputWaitMs = 50; // How long to wait after an input for more events to batch with it

const codeInput = document.getElementById('code-input');
const codeView = document.getElementById('code-view');
const errorList = document.getElementById('error-list');
const errorTemplate = document.getElementById('error-template');
const token = Array(6).fill().map(() => String.fromCharCode(Math.floor(Math.random() * 26) + 97)).join('');

let currentChannel;
const latestResults = {
  tttt: {},
  tsc: {},
}
let latestSubmittedCode = '';

let clientErrors = [];
let serverErrors = [];
function updateErrors() {
  while (errorList.lastElementChild) {
    errorList.removeChild(errorList.lastElementChild);
  }
  for (let message of clientErrors.concat(serverErrors)) {
    const template = errorTemplate.content.cloneNode(true);
    template.querySelector('#error-message').textContent = message;
    errorList.append(template);
  }
}

function surfaceError(message, permanent) {
  clientErrors.push(message);
  updateErrors();
  if (!permanent) {
    setTimeout(() => {
      clientErrors.splice(clientErrors.indexOf(message), 1);
      updateErrors();
    }, 10000);
  }
}

function codeChangedByServer(code) {
  codeInput.value = code;
  latestSubmittedCode = code;
  renderTextView();
}

const channelButtons = Array.from(document.getElementsByClassName('channel-button'));
function onChannelSelected(channel) {
  if (channel == currentChannel) {
    return;
  }
  currentChannel = channel;
  channelButtons.forEach(button => {
    button.classList.toggle('active', button.id.endsWith(channel));
  });
  renderTextView();
}
channelButtons.forEach(button => {
  button.onclick = () => {
    onChannelSelected(button.id.split('-').pop());
  }
});

let inputTimeout;
codeInput.oninput = () => {
  clearTimeout(inputTimeout);
  inputTimeout = setTimeout(() => {
    latestSubmittedCode = codeInput.value;
    fetch('/setCode?token=' + token, {
      method: 'POST',
      body: latestSubmittedCode,
    });
  }, inputWaitMs);
  renderTextView();
}

addEventListener('selectionchange', () => {
  if (document.activeElement == codeInput) {
    renderTextView();
  }
});

codeView.onclick = () => {
  const sel = document.getSelection();
  let anchorOffset = sel?.anchorNode?.parentElement?.tokenOffset;
  let focusOffset = sel?.focusNode?.parentElement?.tokenOffset;
  const selectionForward = sel?.direction !== 'backward';
  if (anchorOffset !== undefined || focusOffset !== undefined) {
    if (anchorOffset !== undefined) {
      anchorOffset += sel.anchorOffset;
    }
    if (focusOffset !== undefined) {
      focusOffset += sel.focusOffset;
    }
    const selectionStart = selectionForward ? anchorOffset : focusOffset;
    const selectionEnd = selectionForward ? focusOffset : anchorOffset;
    if (selectionStart === undefined) {
      selectionStart = 0;
    }
    if (selectionForward === undefined) {
      selectionEnd = codeInput.value.length;
    }
    codeInput.focus();
    codeInput.setSelectionRange(selectionStart, selectionEnd, selectionForward ? 'forward' : 'backward');
  } else {
    codeInput.focus();
    codeInput.setSelectionRange(codeInput.value.length, codeInput.value.length);
  }
  renderTextView();
}

const Token = Object.freeze({
  TEXT: 'token-text',
  CURSOR: 'token-cursor',
  NEWLINE: 'token-newline',
});

const textTokenRegex = /([^\r\n]+)|(\r\n|\r|\n)/y;
function tokenizeStringInto(text, offset, extraClass, tokens) {
  let prevLastIndex = textTokenRegex.lastIndex = 0;
  let match;
  while (match = textTokenRegex.exec(text)) {
    if (match[1]) {
      tokens.push({
        type: Token.TEXT,
        text: match[1],
        offset: offset === null ? null : offset + prevLastIndex,
        extraClass: extraClass,
      });
    } else if (match[2]) {
      tokens.push({
        type: Token.NEWLINE,
        offset: offset === null ? null : offset + prevLastIndex,
        extraClass: extraClass,
      });
    }
    prevLastIndex = textTokenRegex.lastIndex;
  }
}

function tokenizeTextArea(elem) {
  const rawValue = elem.value;
  const selectionStart = elem.selectionStart;
  const selectionEnd = elem.selectionEnd;
  const selectionForward = elem.selectionDirection !== 'backward';
  const beforeSelection = rawValue.slice(0, selectionStart);
  const selection = rawValue.slice(selectionStart, selectionEnd);
  const afterSelection = rawValue.slice(selectionEnd);
  const cursorToken = {
    type: Token.CURSOR,
    offset: selectionForward ? selectionEnd : selectionStart,
  };
  const tokens = [];
  tokenizeStringInto(beforeSelection, 0, undefined, tokens);
  if (!selectionForward) { tokens.push(cursorToken); }
  tokenizeStringInto(selection, selectionStart, 'token-selected', tokens);
  if (selectionForward) { tokens.push(cursorToken); }
  tokenizeStringInto(afterSelection, selectionEnd, undefined, tokens);
  return tokens;
}

function applyTokenToElem(token, elem) {
  if (token.offset !== undefined) {
    elem.tokenOffset = token.offset;
  }
  if (token.extraClass) {
    elem.classList.add(token.extraClass);
  }
}

function renderTokensTo(parent, tokens) {
  for (const token of tokens) {
    switch (token.type) {

      case Token.TEXT: {
        const span = document.createElement('span');
        span.textContent = token.text;
        applyTokenToElem(token, span);
        parent.appendChild(span);
      } break;

      case Token.CURSOR: {
        const div = document.createElement('div');
        div.classList.add('token-cursor')
        applyTokenToElem(token, div);
        parent.appendChild(div);
      } break;

      case Token.NEWLINE: {
        const br = document.createElement('br');
        applyTokenToElem(token, br);
        parent.appendChild(br);
      } break;
    }
  }
}

function processLineDiff(lineDiff) {
  // Returns added sets of lines joined and a single undefined for each non-added line
  let ret = [];
  for (const elem of lineDiff) {
    if (elem.added) {
      ret.push(elem.value.join('\n'));
    } else {
      for (let i = 0; i < elem.value.length; i++) {
        ret.push(undefined);
      }
    }
  }
  return ret;
}

const newlineRegex = /\r?\n/g;
function insertResultTokens(inputTokens) {
  if (latestResults.tsc[currentChannel] === undefined || latestResults.tttt[currentChannel] === undefined) {
    return inputTokens;
  }

  const codeLines = latestSubmittedCode.split(newlineRegex);
  const tscLines = latestResults.tsc[currentChannel].split(newlineRegex);
  const ttttLines = latestResults.tttt[currentChannel].split(newlineRegex);
  const tscLineDiff = Diff.diffArrays(codeLines, tscLines);
  const ttttLineDiff = Diff.diffArrays(codeLines, ttttLines);
  const tscProcessed = processLineDiff(tscLineDiff);
  const ttttProcessed = processLineDiff(ttttLineDiff);

  let inputTokenIndex = 0;
  let tscIndex = 0;
  let ttttIndex = 0;
  const retTokens = [];
  while (tscIndex < tscProcessed.length || ttttIndex < ttttProcessed.length) {
    if (tscProcessed[tscIndex] === undefined && ttttProcessed[ttttIndex] === undefined) {
      while (inputTokenIndex < inputTokens.length) {
        const token = inputTokens[inputTokenIndex];
        retTokens.push(token);
        inputTokenIndex++;
        if (token.type === Token.NEWLINE) {
          break;
        }
      }
      tscIndex++;
      ttttIndex++;
    } else {
      let tscResult = '';
      if (tscProcessed[tscIndex] !== undefined) {
        tscResult = tscProcessed[tscIndex];
        tscIndex++;
      }
      let ttttResult = '';
      if (ttttProcessed[ttttIndex] !== undefined) {
        ttttResult = ttttProcessed[ttttIndex];
        ttttIndex++;
      }
      if (tscResult === ttttResult) {
        tokenizeStringInto(tscResult, null, 'token-diff-both', retTokens);
      } else {
        if (ttttResult) {
          tokenizeStringInto(ttttResult, null, 'token-diff-tttt', retTokens);
        }
        if (ttttResult && tscResult) {
          retTokens.push({
            type: Token.NEWLINE,
            offset: null,
          });
        }
        if (tscResult) {
          tokenizeStringInto(tscResult, null, 'token-diff-tsc', retTokens);
        }
      }
      retTokens.push({
        type: Token.NEWLINE,
        offset: null,
      });
    }
  }
  return retTokens;
}

function renderTextView() {
  const inputTokens = tokenizeTextArea(codeInput);
  while (codeView.lastElementChild) {
    codeView.removeChild(codeView.lastElementChild);
  }
  const unifiedTokens = insertResultTokens(inputTokens)
  renderTokensTo(codeView, unifiedTokens);
}

function handleEvents(json) {
  let render = false;
  for (const key in json) {
    try {
      const keys = key.split('.');
      switch (keys[0]) {
        case 'errors':
          serverErrors = json[key];
          updateErrors();
          break;

        case 'code':
          codeChangedByServer(json[key]);
          break;

        case 'result':
          latestResults[keys[1]][keys[2]] = json[key];
          if (currentChannel == keys[2]) {
            render = true;
          }
          break;
      }
    } catch (e) {
      surfaceError('Handling ' + key + ': ' + e.toString());
    }
  }
  if (render) {
    try {
      renderTextView();
    } catch {
      surfaceError('Rendering text view: ' + e.toString());
    }
  }
}

let isBeingUnloaded = false;
addEventListener('beforeunload', () => {
  isBeingUnloaded = true;
});

function fetchEvents() {
  fetch('/events?token=' + token)
    .then(resp => resp.json())
    .then(json => {
      // console.log('Got message: ', json);
      handleEvents(json);
      fetchEvents();
    })
    .catch(err => {
      if (!isBeingUnloaded) {
        surfaceError(err.toString());
        surfaceError('Server connection closed, please refresh', true);
      }
    });
}

onChannelSelected('typ');
fetchEvents();
