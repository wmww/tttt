const inputWaitMs = 50; // How long to wait after an input for more events to batch with it

const codeInput = document.getElementById('code-input');
codeInput.value = '';
const codeView = document.getElementById('code-view');
const errorList = document.getElementById('error-list');
const errorTemplate = document.getElementById('error-template');
const token = Array(6).fill().map(() => String.fromCharCode(Math.floor(Math.random() * 26) + 97)).join('');

let currentChannel;
const latestResults = {
  tttt: {},
  tsc: {},
}
let serverIsRunningTests = false;
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
let connectedToServer = true;
function updateErrors() {
  while (errorList.lastElementChild) {
    errorList.removeChild(errorList.lastElementChild);
  }
  const fullErrors = clientErrors.concat(serverErrors);
  if (!connectedToServer) {
    fullErrors.push('Disconnected from server');
  }
  for (let message of fullErrors) {
    const template = errorTemplate.content.cloneNode(true);
    template.querySelector('#error-message').textContent = message;
    errorList.append(template);
  }
}

function surfaceError(message) {
  console.error(message);
  clientErrors.push(message);
  updateErrors();
  setTimeout(() => {
    clientErrors.splice(clientErrors.indexOf(message), 1);
    updateErrors();
  }, 10000);
}

function codeChangedByServer(code) {
  codeInput.value = code;
  latestSubmittedCode = code;
  latestSubmittedSerial = null;
  renderTextView();
}

const channelButtons = Array.from(document.getElementsByClassName('channel-button'));
function onChannelSelected(channel) {
  if (channel == currentChannel) {
    return;
  }
  currentChannel = channel;
  for (const button of channelButtons) {
    button.classList.toggle('active', button.id.endsWith(channel));
  }
  codeInput.focus({preventScroll: true});
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

{
  let selectionAnchor = null;
  let selectionFocus = null;

  function tokenOffsetAtPoint(x, y) {
    let range;

    if (document.caretRangeFromPoint) {
      // Chromiums seem to like this one
      range = document.caretRangeFromPoint(x, y);
    } else if (document.caretPositionFromPoint) {
      // Geckos seem to like this one
      const position = document.caretPositionFromPoint(x, y);
      range = document.createRange();
      try {
        range.setStart(position.offsetNode, position.offset);
      } catch {
        return null;
      }
    }
    else {
      // Unsupported browser
      return null;
    }

    if (range.startContainer.tokenOffset !== undefined) {
      return range.startContainer.tokenOffset + range.startOffset;
    } else if (range.startContainer.parentElement.tokenOffset !== undefined) {
      return range.startContainer.parentElement.tokenOffset + range.startOffset;
    } else {
      return null;
    }
  }

  function selectTo(ev) {
    const offset = tokenOffsetAtPoint(ev.clientX, ev.clientY);
    if (offset === null) {
      return;
    }
    selectionFocus = offset;
    if (selectionAnchor === null) {
      selectionAnchor = offset;
    }
    let direction = 'forward';
    let start = selectionAnchor;
    let end = selectionFocus;
    if (selectionFocus < selectionAnchor) {
      direction = 'backward';
      end = selectionAnchor;
      start = selectionFocus;
    }
    codeInput.focus({preventScroll: true});
    codeInput.setSelectionRange(start, end, direction);
    renderTextView();
  }

  codeView.addEventListener('mousedown', ev => {
    mouseDown = true;
    selectTo(ev);
    codeView.addEventListener('mousemove', selectTo);
  });

  codeView.addEventListener('mouseup', ev => {
    codeView.removeEventListener('mousemove', selectTo);
    selectTo(ev);
    selectionAnchor = null;
    selectionFocus = null;
    mouseDown = false;
  });
}

function tokenOffsetFrom(node) {
  let offset = node?.anchorNode?.tokenOffset;
  if (offset === undefined) {
    offset = node?.anchorNode?.parentElement?.tokenOffset;
  }
  return offset;
}

const Token = Object.freeze({
  TEXT: 'token-text',
  LINE_NUMBER: 'token-line-number',
  CURSOR: 'token-cursor',
  NEWLINE: 'token-newline',
  ENDLINE: 'token-endline',
});

function addLineStartTokensTo(lineNumbers, indent, tokens) {
  if (lineNumbers) {
    tokens.push({type: Token.LINE_NUMBER});
  }
  if (indent) {
    tokens.push({
      type: Token.TEXT,
      text: indent,
      extraClass: 'token-indent',
    });
  }
}

const textTokenRegex = /([^\r\n]+)|(\r\n|\r|\n)/y;
function tokenizeStringInto(text, offset, extraClass, lineNumbers, indent, tokens) {
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
      addLineStartTokensTo(lineNumbers, indent, tokens);
    }
    prevLastIndex = textTokenRegex.lastIndex;
  }
}

function tokenizeTextArea(elem, indent) {
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
  addLineStartTokensTo(true, indent, tokens);
  tokenizeStringInto(beforeSelection, 0, undefined, true, indent, tokens);
  if (!selectionForward) { tokens.push(cursorToken); }
  tokenizeStringInto(selection, selectionStart, 'token-selected', true, indent, tokens);
  if (selectionForward) { tokens.push(cursorToken); }
  tokenizeStringInto(afterSelection, selectionEnd, undefined, true, indent, tokens);
  tokens.push({
    type: Token.ENDLINE,
    offset: rawValue.length,
  });
  return tokens;
}

function applyTokenToElem(token, elem) {
  elem.debugToken = token;
  if (token.offset !== undefined && token.offset !== null) {
    elem.tokenOffset = token.offset;
  }
  if (token.extraClass) {
    elem.classList.add(token.extraClass);
  }
}

function renderTokensTo(parent, tokens) {
  let pendingLine = null;
  let lineNumberTally = 0;
  let addLineNumber = false;

  function ensureLine() {
    if (!pendingLine) {
      pendingLine = document.createElement('div')
      pendingLine.classList.add('code-line');
    }
  }

  function finishLine(token) {
    if (pendingLine) {
      const div = document.createElement('div');
      div.classList.add('token-line-cap');
      applyTokenToElem(token, div);
      pendingLine.appendChild(div);

      if (addLineNumber) {
        const lineNumberSpan = document.createElement('span');
        lineNumberSpan.classList.add('token-line-number');
        lineNumberTally++;
        lineNumberSpan.textContent = lineNumberTally;
        pendingLine.prepend(lineNumberSpan);
      }

      parent.appendChild(pendingLine);

      pendingLine = null;
      addLineNumber = false;
    }
  }

  ensureLine();

  for (const token of tokens) {
    switch (token.type) {

      case Token.TEXT: {
        const span = document.createElement('span');
        span.textContent = token.text;
        applyTokenToElem(token, span);
        ensureLine();
        pendingLine.appendChild(span);
      } break;

      case Token.CURSOR: {
        const div = document.createElement('div');
        div.classList.add('token-cursor');
        applyTokenToElem(token, div);
        ensureLine();
        pendingLine.appendChild(div);
      } break;

      case Token.LINE_NUMBER: {
        addLineNumber = true;
      } break;

      case Token.NEWLINE: {
        finishLine(token);
        ensureLine();
      } break;

      case Token.ENDLINE: {
        finishLine(token);
      } break;
    }
  }

  if (pendingLine) {
    console.error('Token stream did not end with Token.ENDLINE');
    finishLine({});
  }
}

const newlineRegex = /\r?\n/g;
function getResultTokens(codeText, tscText, ttttText, indent) {
  let codeLines = codeText.split(newlineRegex);
  if (indent) {
    for (let i = 0; i < codeLines.length; i++) {
      codeLines[i] = indent + codeLines[i]
    }
  }
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

      for (const i of Diff.diffLines(ttttResult, tscResult)) {
        if (i.added) {
          tokenizeStringInto(i.value, null, 'token-diff-tsc', false, null, retTokens);
        } else if (i.removed) {
          tokenizeStringInto(i.value, null, 'token-diff-tttt', false, null, retTokens);
        } else {
          tokenizeStringInto(i.value, null, 'token-diff-both', false, null, retTokens);
        }
        if (retTokens && retTokens[retTokens.length - 1] && retTokens[retTokens.length - 1].type !== Token.NEWLINE) {
          retTokens.push({
            type: Token.NEWLINE,
            extraClass: 'token-diff-both',
            offset: null,
          });
        }
      }
    }
  }
  retTokens.push({
    type: Token.ENDLINE,
    extraClass: 'token-diff-both',
    offset: null,
  });
  return retTokens;
}

function integrateResultTokens(inputTokens, resultTokens) {
  let inputIndex = 0;
  const retTokens = [];

  for (const result of resultTokens) {
    if (result === null) {
      // Push a line of input (if available)
      while (inputIndex < inputTokens.length) {
        const token = inputTokens[inputIndex];
        retTokens.push(token);
        inputIndex++;
        if (token.type === Token.NEWLINE) {
          break;
        }
      }
    } else {
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

function updateTestInFlightState() {
  const outOfDate = (
    currentCodeSerial !== latestResults.tttt.serial ||
    currentCodeSerial !== latestResults.tsc.serial
  ) && latestSubmittedSerial !== null;
  codeView.classList.toggle('diff-out-of-date', outOfDate || serverIsRunningTests);
}

function renderTextView() {
  const inputTokens = tokenizeTextArea(codeInput, currentChannel == 'err' ? '    ' : null);
  while (codeView.lastElementChild) {
    codeView.removeChild(codeView.lastElementChild);
  }
  const unifiedTokens = integrateResultTokens(inputTokens, resultTokensWithNulls);
  renderTokensTo(codeView, unifiedTokens);
  updateTestInFlightState();
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
      latestResults.tttt[currentChannel],
      currentChannel == 'err' ? '    ' : null
    );
  }
}

function updateErrorCounts() {
  function getErrorCount(result) {
    if (!result) {
      return 0;
    }
    const match = result.match(errorCountRegex);
    if (match) {
      return parseInt(match[1]);
    } else {
      return null;
    }
  }
  const ttttErrorCount = getErrorCount(latestResults.tttt.err);
  const tscErrorCount = getErrorCount(latestResults.tsc.err);
  const count = tscErrorCount > ttttErrorCount ? tscErrorCount : ttttErrorCount;
  const errorsButton = document.getElementById('channel-button-err');
  errorsButton.textContent = 'Errors';
  if (count > 0) {
    errorsButton.textContent += ' [' + count + ']';
  }
}

const errorCountRegex = /customTest\.ts \((\d+) errors\) ===/;
function handleEvents(json) {
  let currentChannelUpdated = false;
  let anyChannelUpdated = false;
  let errorsUpdated = false;
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

        case 'running':
          serverIsRunningTests = json[key];
          updateTestInFlightState();
          break;

        case 'result':
          latestResults[keys[1]][keys[2]] = json[key];
          if (keys[2] == 'err') {
            errorsUpdated = true;
          }
          if (keys[2] == currentChannel || keys[2] == 'serial') {
            currentChannelUpdated = true;
          }
          anyChannelUpdated = true;
          break;
      }
    } catch (e) {
      surfaceError('Handling ' + key + ': ' + e.toString());
    }
  }
  if (errorsUpdated) {
    updateErrorCounts();
  }
  const serialsMatch = latestResults.tsc.serial === latestResults.tttt.serial &&
    (latestSubmittedSerial === null || latestResults.tttt.serial === latestSubmittedSerial);
  if (anyChannelUpdated && serialsMatch) {
    for (const channel of ['err', 'sym', 'typ']) {
      const passed = latestResults.tsc[channel] == latestResults.tttt[channel];
      const button = document.getElementById('channel-button-' + channel);
      button.classList.toggle('fail', !passed);
    }
  }
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
        if (!connectedToServer) {
          connectedToServer = true;
          updateErrors();
        }
        handleEvents(json);
        fetchEvents();
      } catch (e) {
        surfaceError(e);
        setTimeout(fetchEvents, 1000);
      }
    })
    .catch(err => {
      if (!isBeingUnloaded) {
        if (connectedToServer) {
          connectedToServer = false;
          updateErrors();
        }
        setTimeout(fetchEvents, 500);
      }
    });
}

onChannelSelected('typ');
fetchEvents();
