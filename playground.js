/**
 * 🐡 Unfish Playground & Visual Blocks Studio Controller
 */

const EXAMPLES = {
  hello: `## Welcome to Unfish v2.0!
## String interpolation and basic output

let name = "Developer"
let version = "2.0.0"
let speed_rating = 99.8

say f"Hello, {name}! Welcome to Unfish v{version}."
say f"Performance score: {speed_rating}% native C99 execution."
`,

  enums: `## Enums & Sum Types with Pattern Matching

enum Shape:
    Circle(radius)
    Rectangle(width, height)

function calculate_area(shape):
    match shape:
        when Circle(r):
            return 3.14159 * r * r
        when Rectangle(w, h):
            return w * h

let c = Shape.Circle(5)
let r = Shape.Rectangle(4, 6)

say f"Circle area: {calculate_area(c)}"
say f"Rectangle area: {calculate_area(r)}"
`,

  structs: `## Structs and Object Methods

struct Vector2D:
    x
    y

    fn magnitude(self):
        return sqrt(pow(self.x, 2) + pow(self.y, 2))

    fn scale(self, factor):
        return Vector2D(self.x * factor, self.y * factor)

let vec = Vector2D(3, 4)
say f"Original magnitude: {vec.magnitude()}"

let scaled = vec.scale(2)
say f"Scaled vector: ({scaled.x}, {scaled.y}) with magnitude: {scaled.magnitude()}"
`,

  destructure: `## Destructuring & Rest Operators

let numbers = [10, 20, 30, 40, 50]
let [first, second, ...rest] = numbers

say f"First: {first}, Second: {second}"
say f"Rest count: {len(rest)}, Rest: {rest}"

let point = {"px": 100, "py": 200, "label": "Origin"}
let {px, py, label} = point
say f"Extracted {label} at ({px}, {py})"
`,

  closures: `## Closures and Higher-Order Functions

function make_multiplier(factor):
    return fn(n): n * factor

let triple = make_multiplier(3)
let tenfold = make_multiplier(10)

say f"triple(7) = {triple(7)}"
say f"tenfold(42) = {tenfold(42)}"

let values = [1, 2, 3, 4, 5]
let squared = map(values, fn(x): x * x)
say f"Squared numbers: {squared}"
`,

  fibonacci: `## Recursive Fibonacci

function fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

for i in [0, 1, 2, 3, 4, 5, 6, 7, 8]:
    say f"fib({i}) = {fib(i)}"
`,

  sorting: `## Bubble Sort Algorithm in Unfish

function bubble_sort(arr):
    let n = len(arr)
    for i in range(0, n):
        for j in range(0, n - i - 1):
            if arr[j] > arr[j + 1]:
                let tmp = arr[j]
                arr[j] = arr[j + 1]
                arr[j + 1] = tmp
    return arr

let items = [64, 34, 25, 12, 22, 11, 90]
say f"Original: {items}"
let sorted_items = bubble_sort(items)
say f"Sorted:   {sorted_items}"
`
};

document.addEventListener('DOMContentLoaded', () => {
  const codeEditor = document.getElementById('code-editor');
  const lineNumbers = document.getElementById('line-numbers');
  const consoleOutput = document.getElementById('console-output');
  const executionStats = document.getElementById('execution-stats');
  const blocksContainer = document.getElementById('blocks-container');
  const astOutput = document.getElementById('ast-output');
  const exampleSelect = document.getElementById('example-select');
  const backendStatus = document.getElementById('backend-status');

  const btnRun = document.getElementById('btn-run');
  const btnVm = document.getElementById('btn-vm');
  const btnFormat = document.getElementById('btn-format');
  const btnClear = document.getElementById('btn-clear');

  const blocksSyncStatus = document.getElementById('blocks-sync-status');
  const btnBlocksSync = document.getElementById('btn-blocks-sync');
  const btnBlocksFormat = document.getElementById('btn-blocks-format');
  const btnBlocksExport = document.getElementById('btn-blocks-export');
  const btnBlocksClear = document.getElementById('btn-blocks-clear');
  const paletteItems = document.querySelectorAll('.palette-item');

  const tabButtons = document.querySelectorAll('.tab-btn');
  const tabContents = document.querySelectorAll('.tab-content');

  const engine = new window.UnfishEngine();
  let hasBackend = false;
  let currentBlocksData = { schema: 'unfish_blocks_v1', statements: [] };
  let isBlocksDirty = false;

  // Check backend server status
  fetch('/api/status')
    .then(res => res.json())
    .then(data => {
      if (data.status === 'ok') {
        hasBackend = true;
        backendStatus.textContent = '● Native CLI Connected (v' + data.version + ')';
        backendStatus.className = 'status-badge live';
      }
    })
    .catch(() => {
      hasBackend = false;
      backendStatus.textContent = '○ In-Browser WebAssembly Engine';
      window.UnfishRunner.preload();
      backendStatus.className = 'status-badge';
    });

  // Editor line numbers and input handling
  function updateLineNumbers() {
    const lines = codeEditor.value.split('\n').length;
    let lineStr = '';
    for (let i = 1; i <= lines; i++) {
      lineStr += i + '\n';
    }
    lineNumbers.textContent = lineStr;
  }

  codeEditor.addEventListener('input', () => {
    updateLineNumbers();
    isBlocksDirty = true;
    setSyncStatus(false, 'Code changed • Sync needed');
  });

  codeEditor.addEventListener('scroll', () => {
    lineNumbers.scrollTop = codeEditor.scrollTop;
  });

  codeEditor.addEventListener('keydown', (e) => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const start = codeEditor.selectionStart;
      const end = codeEditor.selectionEnd;
      codeEditor.value = codeEditor.value.substring(0, start) + '    ' + codeEditor.value.substring(end);
      codeEditor.selectionStart = codeEditor.selectionEnd = start + 4;
      updateLineNumbers();
    } else if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      runCode(false);
    }
  });

  // Example selector
  exampleSelect.addEventListener('change', () => {
    const key = exampleSelect.value;
    if (EXAMPLES[key]) {
      codeEditor.value = EXAMPLES[key];
      updateLineNumbers();
      clearConsole();
      runCode(false);
    }
  });

  // Load default example
  codeEditor.value = EXAMPLES.hello;
  updateLineNumbers();

  // Tab switching
  tabButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      const tabId = btn.dataset.tab;
      tabButtons.forEach(b => b.classList.remove('active'));
      tabContents.forEach(c => c.classList.remove('active'));

      btn.classList.add('active');
      const targetContent = document.getElementById(`tab-${tabId}`);
      if (targetContent) targetContent.classList.add('active');

      if (tabId === 'blocks') {
        updateVisualBlocks();
      } else if (tabId === 'ast') {
        updateAstInspector();
      }
    });
  });

  function clearConsole() {
    consoleOutput.innerHTML = '';
  }

  btnClear.addEventListener('click', clearConsole);

  // Execution
  btnRun.addEventListener('click', () => runCode(false));
  btnVm.addEventListener('click', () => runCode(true));

  function runCode(useVm) {
    const code = codeEditor.value;
    const modeName = useVm ? 'Bytecode VM' : 'AST Interpreter';
    executionStats.textContent = `Running with ${modeName}...`;

    const startTime = performance.now();

    if (hasBackend) {
      const endpoint = useVm ? '/api/run-vm' : '/api/run';
      fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        const elapsed = Math.round(performance.now() - startTime);
        handleExecutionResult(data, elapsed, modeName);
      })
      .catch(err => {
        runInBrowser(code, useVm, modeName);
      });
    } else {
      runInBrowser(code, useVm, modeName);
    }
  }

  // Without the local `unfish playground` server, run the real engines
  // compiled to WebAssembly in a worker (see unfish_wasm.js).
  async function runInBrowser(code, useVm, modeName) {
    const startTime = performance.now();
    const result = await window.UnfishRunner.run(code, { engine: useVm ? 'vm' : 'interp' });
    const elapsed = Math.round(performance.now() - startTime);
    const where = result.engine === 'wasm' ? 'WebAssembly' : 'JavaScript fallback';
    handleExecutionResult(result, elapsed, `${modeName} (${where})`);
  }

  function handleExecutionResult(data, elapsed, mode) {
    clearConsole();
    executionStats.textContent = `Completed in ${elapsed} ms (${mode}) • Exit Code: ${data.exit_code}`;

    if (data.stdout) {
      const outLines = data.stdout.split('\n');
      for (const line of outLines) {
        if (!line && outLines[outLines.length - 1] === line) continue;
        const lineEl = document.createElement('span');
        lineEl.className = 'terminal-line stdout';
        lineEl.textContent = line;
        consoleOutput.appendChild(lineEl);
      }
    }

    if (data.stderr) {
      const errLines = data.stderr.split('\n');
      for (const line of errLines) {
        if (!line && errLines[errLines.length - 1] === line) continue;
        const lineEl = document.createElement('span');
        lineEl.className = 'terminal-line stderr';
        lineEl.textContent = line;
        consoleOutput.appendChild(lineEl);
      }
    }

    if (data.exit_code === 0) {
      const okLine = document.createElement('span');
      okLine.className = 'terminal-line success';
      okLine.textContent = `✓ Program exited successfully (code 0)`;
      consoleOutput.appendChild(okLine);
    }

    consoleOutput.scrollTop = consoleOutput.scrollHeight;
  }

  // Visual Blocks Studio Logic
  function setSyncStatus(isSynced, msg) {
    if (blocksSyncStatus) {
      if (isSynced) {
        blocksSyncStatus.className = 'sync-status-pill';
        blocksSyncStatus.textContent = msg || '● Synced with Code';
      } else {
        blocksSyncStatus.className = 'sync-status-pill dirty';
        blocksSyncStatus.textContent = msg || '● Unsynced Changes';
      }
    }
  }

  function onBlockDataModified(newData) {
    currentBlocksData = newData;
    isBlocksDirty = true;
    setSyncStatus(false, '● Unsynced Changes');
    syncBlocksToCode(false);
  }

  function updateVisualBlocks() {
    const code = codeEditor.value;
    if (hasBackend) {
      fetch('/api/blocks', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        if (data.stdout && data.exit_code === 0) {
          try {
            const blocksData = JSON.parse(data.stdout);
            currentBlocksData = blocksData;
            engine.renderEditableBlocks(currentBlocksData, blocksContainer, onBlockDataModified);
            setSyncStatus(true, '● Synced with Code');
            isBlocksDirty = false;
            return;
          } catch(e) {}
        }
        fallbackBlocks(code);
      })
      .catch(() => fallbackBlocks(code));
    } else {
      fallbackBlocks(code);
    }
  }

  function fallbackBlocks(code) {
    currentBlocksData = engine.parseBlocks(code);
    engine.renderEditableBlocks(currentBlocksData, blocksContainer, onBlockDataModified);
    setSyncStatus(true, '● Synced with Code (Client)');
    isBlocksDirty = false;
  }

  function syncBlocksToCode(andFormat) {
    const jsonStr = JSON.stringify(currentBlocksData, null, 2);

    if (hasBackend) {
      fetch('/api/blocks-import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: jsonStr
      })
      .then(res => res.json())
      .then(data => {
        if (data.stdout && data.exit_code === 0) {
          codeEditor.value = data.stdout;
          updateLineNumbers();
          setSyncStatus(true, '● Synced with Code');
          isBlocksDirty = false;
          if (andFormat) {
            formatSourceCode();
          }
          return;
        }
        fallbackSyncToCode();
      })
      .catch(() => fallbackSyncToCode());
    } else {
      fallbackSyncToCode();
    }
  }

  function fallbackSyncToCode() {
    const code = engine.blocksToCode(currentBlocksData);
    codeEditor.value = code;
    updateLineNumbers();
    setSyncStatus(true, '● Synced with Code (Client)');
    isBlocksDirty = false;
  }

  // Palette Item Click Handlers
  const blockTemplates = {
    say: { kind: 'say', value: '"Hello from Blocks!"', indent: 0 },
    let: { kind: 'let', name: 'counter', value: '42', indent: 0 },
    set: { kind: 'set', name: 'counter', value: 'counter + 1', indent: 0 },
    if: { kind: 'if', condition: 'counter > 0', indent: 0 },
    while: { kind: 'while', condition: 'counter < 10', indent: 0 },
    for: { kind: 'for', iterator: 'item in [1, 2, 3]', indent: 0 },
    repeat: { kind: 'repeat', count: '5', indent: 0 },
    function: { kind: 'function', name: 'greet', params: 'name', indent: 0 },
    return: { kind: 'return', value: 'name', indent: 0 },
    struct: { kind: 'struct', name: 'Point', indent: 0 },
    enum: { kind: 'enum', name: 'Color', indent: 0 },
    match: { kind: 'match', target: 'val', indent: 0 },
    when: { kind: 'when', pattern: 'Red', indent: 0 },
    try: { kind: 'try', indent: 0 },
    catch: { kind: 'catch', variable: 'err', indent: 0 },
    finally: { kind: 'finally', indent: 0 },
    spawn: { kind: 'spawn', expr: 'fn(): null', indent: 0 },
    yield: { kind: 'yield', indent: 0 }
  };

  paletteItems.forEach(item => {
    item.addEventListener('click', () => {
      const kind = item.dataset.kind;
      const tpl = blockTemplates[kind] || { kind: 'expr', expression: 'pass', indent: 0 };
      const newBlock = JSON.parse(JSON.stringify(tpl));

      if (!currentBlocksData.statements) {
        currentBlocksData.statements = [];
      }
      currentBlocksData.statements.push(newBlock);
      engine.renderEditableBlocks(currentBlocksData, blocksContainer, onBlockDataModified);
      syncBlocksToCode(false);

      // Scroll to new block
      blocksContainer.scrollTop = blocksContainer.scrollHeight;
    });
  });

  if (btnBlocksSync) {
    btnBlocksSync.addEventListener('click', () => syncBlocksToCode(false));
  }

  if (btnBlocksFormat) {
    btnBlocksFormat.addEventListener('click', () => syncBlocksToCode(true));
  }

  if (btnBlocksExport) {
    btnBlocksExport.addEventListener('click', () => {
      const jsonStr = JSON.stringify(currentBlocksData, null, 2);
      navigator.clipboard.writeText(jsonStr).then(() => {
        alert('Blocks JSON copied to clipboard!');
      }).catch(() => {
        prompt('Copy blocks JSON:', jsonStr);
      });
    });
  }

  if (btnBlocksClear) {
    btnBlocksClear.addEventListener('click', () => {
      if (confirm('Clear all blocks from canvas?')) {
        currentBlocksData.statements = [];
        engine.renderEditableBlocks(currentBlocksData, blocksContainer, onBlockDataModified);
        syncBlocksToCode(false);
      }
    });
  }

  function updateAstInspector() {
    const code = codeEditor.value;
    if (hasBackend) {
      fetch('/api/ast', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        astOutput.textContent = data.stdout || data.stderr || 'No AST output.';
      })
      .catch(() => {
        const blocksData = engine.parseBlocks(code);
        astOutput.textContent = JSON.stringify(blocksData, null, 2);
      });
    } else {
      const blocksData = engine.parseBlocks(code);
      astOutput.textContent = JSON.stringify(blocksData, null, 2);
    }
  }

  function formatSourceCode() {
    const code = codeEditor.value;
    if (hasBackend) {
      fetch('/api/format', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        if (data.stdout && data.exit_code === 0) {
          codeEditor.value = data.stdout;
          updateLineNumbers();
        }
      });
    }
  }

  btnFormat.addEventListener('click', formatSourceCode);

  // Run initial code on load
  runCode(false);
});
