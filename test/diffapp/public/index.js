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
// An array of tokens representing the diff of the tttt and tsc results. It also contains nulls, each representing one
// line (not token!) of input. May be generated from an older state then latestResults.
let resultTokensWithNulls = [];
let latestSubmittedCode = '';
let latestSubmittedSerial = null;
let currentCodeSerial = Math.floor(Math.random() * (1000000 - 100) + 100);

function incrementCodeSerial() {
  currentCodeSerial = (currentCodeSerial + 1) % 1000000 + 100
}

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
  console.error(message);
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
  codeInput.focus();
  updateResultTokens();
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
  incrementCodeSerial();
  inputTimeout = setTimeout(() => {
    latestSubmittedCode = codeInput.value;
    latestSubmittedSerial = currentCodeSerial;
    fetch('/setCode?token=' + token + '&serial=' + latestSubmittedSerial, {
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

const newlineRegex = /\r?\n/g;
function getResultTokens(codeText, tscText, ttttText) {
  const codeLines = codeText.split(newlineRegex);
  const tscLines = tscText.split(newlineRegex);
  const ttttLines = ttttText.split(newlineRegex);
  const tscLineDiff = Diff.diffArrays(codeLines, tscLines);
  const ttttLineDiff = Diff.diffArrays(codeLines, ttttLines);

  // Returns added sets of lines joined and a single undefined for each non-added line
  function processLineDiff(lineDiff) {
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

  const tscProcessed = processLineDiff(tscLineDiff);
  const ttttProcessed = processLineDiff(ttttLineDiff);

  let tscIndex = 0;
  let ttttIndex = 0;
  const retTokens = [];
  while (tscIndex < tscProcessed.length || ttttIndex < ttttProcessed.length) {
    if (tscProcessed[tscIndex] === undefined && ttttProcessed[ttttIndex] === undefined) {
      retTokens.push(null);
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

function integrateResultTokens(inputTokens, resultTokens) {
  let inputIndex = 0;
  const retTokens = [];

  let inputEndedWithoutNewline = false;
  for (const result of resultTokens) {
    if (result === null) {
      // Push a line of input (if available)
      while (inputIndex < inputTokens.length) {
        const token = inputTokens[inputIndex];
        retTokens.push(token);
        inputIndex++;
        inputEndedWithoutNewline = true;
        if (token.type === Token.NEWLINE) {
          inputEndedWithoutNewline = false;
          break;
        }
      }
    } else {
      if (inputEndedWithoutNewline) {
        retTokens.push({
          type: Token.NEWLINE,
          offset: retTokens[retTokens.length - 1].offset,
        })
        inputEndedWithoutNewline = false;
      }
      retTokens.push(result);
    }
  }

  // Push any left over input
  while (inputIndex < inputTokens.length) {
    retTokens.push(inputTokens[inputIndex]);
    inputIndex++;
  }

  return retTokens;
}

function renderTextView() {
  const inputTokens = tokenizeTextArea(codeInput);
  while (codeView.lastElementChild) {
    codeView.removeChild(codeView.lastElementChild);
  }
  const unifiedTokens = integrateResultTokens(inputTokens, resultTokensWithNulls);
  renderTokensTo(codeView, unifiedTokens);
}

function updateResultTokens() {
  if (latestResults.tsc[currentChannel] === undefined ||
      latestResults.tttt[currentChannel] === undefined
  ) {
    resultTokensWithNulls = [];
  } else {
    resultTokensWithNulls = getResultTokens(
      latestSubmittedCode,
      latestResults.tsc[currentChannel],
      latestResults.tttt[currentChannel]
    );
  }
}

function handleEvents(json) {
  let currentChannelUpdated = false;
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
            currentChannelUpdated = true;
          }
          break;
      }
    } catch (e) {
      surfaceError('Handling ' + key + ': ' + e.toString());
    }
  }
  const serialsMatch = latestSubmittedSerial === null || (
    latestResults.tsc.serial === latestResults.tttt.serial &&
    parseInt(latestResults.tttt.serial) === latestSubmittedSerial);
  if (currentChannelUpdated && serialsMatch) {
    updateResultTokens();
    renderTextView();
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
      try {
        handleEvents(json);
        fetchEvents();
      } catch (e) {
        surfaceError(e);
      }
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
