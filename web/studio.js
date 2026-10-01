/**
 * 🐡 Unfish Studio — Real Browser IDE Controller
 * Full-featured controller for multi-file workspace, syntax editor, visual blocks canvas,
 * AST/disasm/tokens inspectors, step debugger, and native/in-browser execution.
 */

(function() {
  'use strict';

  // Sample Templates
  const TEMPLATES = {
    hello: {
      'unfish.toml': `[package]\nname = "hello_world"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Welcome to Unfish Studio!\n## String interpolation, math, and formatted output\n\nlet language = "Unfish"\nlet version = "2.0.0"\nlet performance = 100.0\n\nsay f"Hello from {language} v{version}!"\nsay f"5-way differential parity: {performance}% identical output across all tiers."\n`,
      'README.md': `# Hello World Project\nA simple starter project in Unfish.`
    },
    enums: {
      'unfish.toml': `[package]\nname = "enums_demo"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Enums and Pattern Matching\n\nenum Shape:\n    Circle(radius)\n    Rectangle(width, height)\n\nfunction calculate_area(s):\n    match s:\n        when Circle(r):\n            return 3.14159 * r * r\n        when Rectangle(w, h):\n            return w * h\n\nlet c = Shape.Circle(5)\nlet r = Shape.Rectangle(4, 6)\n\nsay f"Circle area: {calculate_area(c)}"\nsay f"Rectangle area: {calculate_area(r)}"\n`
    },
    structs: {
      'unfish.toml': `[package]\nname = "structs_demo"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Structs and Object Methods\n\nstruct Vector2D:\n    x\n    y\n\n    fn magnitude(self):\n        return sqrt(pow(self.x, 2) + pow(self.y, 2))\n\n    fn scale(self, factor):\n        return Vector2D(self.x * factor, self.y * factor)\n\nlet v = Vector2D(3, 4)\nsay f"Original magnitude: {v.magnitude()}"\n\nlet v2 = v.scale(2)\nsay f"Scaled vector: ({v2.x}, {v2.y}) with magnitude: {v2.magnitude()}"\n`
    },
    closures: {
      'unfish.toml': `[package]\nname = "closures_demo"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Closures and Higher-Order Pipelines\n\nfunction make_counter(start = 0):\n    let c = start\n    return fn():\n        c = c + 1\n        return c\n\nlet counter = make_counter(10)\nsay f"Count 1: {counter()}"\nsay f"Count 2: {counter()}"\nsay f"Count 3: {counter()}"\n\nlet numbers = [1, 2, 3, 4, 5]\nlet squared = map(numbers, fn(x): x * x)\nsay f"Squared: {squared}"\n`
    },
    algorithms: {
      'unfish.toml': `[package]\nname = "bubble_sort"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Bubble Sort Algorithm in Unfish\n\nfunction bubble_sort(arr):\n    let n = len(arr)\n    for i in range(0, n):\n        for j in range(0, n - i - 1):\n            if arr[j] > arr[j + 1]:\n                let tmp = arr[j]\n                arr[j] = arr[j + 1]\n                arr[j + 1] = tmp\n    return arr\n\nlet raw = [64, 34, 25, 12, 22, 11, 90]\nsay f"Unsorted: {raw}"\nlet sorted_arr = bubble_sort(raw)\nsay f"Sorted:   {sorted_arr}"\n`
    },
    concurrency: {
      'unfish.toml': `[package]\nname = "fibers_demo"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Cooperative Fibers and CSP Channels\n\nlet ch = channel(10)\n\nfunction worker(ch):\n    say "Worker fiber started"\n    for i in range(1, 4):\n        send(ch, i * 10)\n        yield\n    close_channel(ch)\n    say "Worker fiber finished"\n\nspawn(worker, ch)\nrun_scheduler()\n\nsay "Main task reading from channel..."\nwhile true:\n    let val = recv(ch)\n    if val == null:\n        break\n    say f"Received value: {val}"\n\nsay "All tasks complete!"\n`
    },
    buffers: {
      'unfish.toml': `[package]\nname = "buffers_demo"\nversion = "1.0.0"\nentry = "src/main.uf"`,
      'src/main.uf': `## Systems Programming & Raw Byte Buffers\n\nlet buf = buffer(8)\nsay f"Allocated buffer of size: {len(buf)} bytes"\n\nbuffer_set(buf, 0, 0x48) # 'H'\nbuffer_set(buf, 1, 0x69) # 'i'\nbuffer_set(buf, 2, 0x21) # '!'\n\nsay f"Byte 0: {buffer_get(buf, 0)}"\nsay f"Byte 1: {buffer_get(buf, 1)}"\nsay f"Byte 2: {buffer_get(buf, 2)}"\n`
    }
  };

  // State
  let files = {};
  let activeFile = 'src/main.uf';
  let openTabs = ['src/main.uf'];
  let breakpoints = new Set();
  let hasNativeBackend = false;
  let currentEngine = new window.UnfishEngine();
  let currentBlocksData = { schema: 'unfish_blocks_v1', statements: [] };
  let isBlocksDirty = false;

  // DOM Elements
  const fileTreeContainer = document.getElementById('file-tree-container');
  const editorTabsBar = document.getElementById('editor-tabs-bar');
  const lineNumbers = document.getElementById('line-numbers');
  const codeTextarea = document.getElementById('code-textarea');
  const cursorPos = document.getElementById('editor-cursor-pos');
  const backendStatusPill = document.getElementById('backend-status-pill');
  const backendStatusText = document.getElementById('backend-status-text');

  // Drawer & Tabs
  const drawerTabs = document.querySelectorAll('.drawer-tab');
  const drawerPanes = document.querySelectorAll('.drawer-pane');
  const terminalView = document.getElementById('terminal-view');
  const problemsList = document.getElementById('problems-list');
  const problemsCount = document.getElementById('problems-count');
  const astView = document.getElementById('ast-view');
  const disasmView = document.getElementById('disasm-view');
  const tokensView = document.getElementById('tokens-view');

  // Modes
  const modeBtnCode = document.getElementById('mode-btn-code');
  const modeBtnBlocks = document.getElementById('mode-btn-blocks');
  const codeEditorContainer = document.getElementById('code-editor-container');
  const studioBlocksView = document.getElementById('studio-blocks-view');
  const blocksCanvasContainer = document.getElementById('blocks-canvas-container');

  // Sidebar Activity Bar
  const actBtnFiles = document.getElementById('act-btn-files');
  const actBtnBlocks = document.getElementById('act-btn-blocks');
  const actBtnDebug = document.getElementById('act-btn-debug');
  const sidebarExplorer = document.getElementById('sidebar-explorer');
  const sidebarBlocks = document.getElementById('sidebar-blocks');
  const studioSidebar = document.getElementById('studio-sidebar');

  // Buttons
  const btnRunAst = document.getElementById('btn-run-ast');
  const btnRunVm = document.getElementById('btn-run-vm');
  const btnRunRegvm = document.getElementById('btn-run-regvm');
  const btnFormatCode = document.getElementById('btn-format-code');
  const btnStartDebug = document.getElementById('btn-start-debug');
  const btnClearTerminal = document.getElementById('btn-clear-terminal');
  const btnThemeToggle = document.getElementById('btn-theme-toggle');
  const sampleProjectSelect = document.getElementById('sample-project-select');
  const btnNewFile = document.getElementById('btn-new-file');
  const btnDownloadFile = document.getElementById('btn-download-file');
  const btnExportProj = document.getElementById('btn-export-proj');
  const btnShare = document.getElementById('btn-share');
  const shareModal = document.getElementById('share-modal');
  const shareUrlInput = document.getElementById('share-url-input');
  const btnCloseShareModal = document.getElementById('btn-close-share-modal');
  const btnCopyShareUrl = document.getElementById('btn-copy-share-url');

  // Search
  const editorSearchBar = document.getElementById('editor-search-bar');
  const searchInput = document.getElementById('search-input');
  const btnSearchNext = document.getElementById('btn-search-next');
  const btnSearchPrev = document.getElementById('btn-search-prev');
  const btnSearchClose = document.getElementById('btn-search-close');

  // Check Backend
  function checkBackend() {
    fetch('/api/status')
      .then(res => res.json())
      .then(data => {
        if (data.status === 'ok') {
          hasNativeBackend = true;
          backendStatusPill.className = 'status-badge live';
          backendStatusText.textContent = `Native C99 CLI (v${data.version})`;
        }
      })
      .catch(() => {
        hasNativeBackend = false;
        backendStatusPill.className = 'status-badge';
        backendStatusText.textContent = 'In-Browser Engine';
      });
  }

  // Load Initial Project
  function loadProject(templateKey) {
    const tmpl = TEMPLATES[templateKey] || TEMPLATES.hello;
    files = JSON.parse(JSON.stringify(tmpl));
    activeFile = files['src/main.uf'] ? 'src/main.uf' : Object.keys(files)[0];
    openTabs = [activeFile];
    renderFileTree();
    renderTabs();
    loadActiveFile();
  }

  // File Tree
  function renderFileTree() {
    fileTreeContainer.innerHTML = '';
    const fileNames = Object.keys(files).sort();

    fileNames.forEach(fn => {
      const item = document.createElement('div');
      item.className = `file-tree-item ${fn === activeFile ? 'active' : ''}`;

      const icon = fn.endsWith('.uf') || fn.endsWith('.unfish') ? '🐡' : (fn.endsWith('.toml') ? '⚙️' : '📄');
      item.innerHTML = `
        <div class="file-item-left">
          <span class="file-icon">${icon}</span>
          <span>${fn}</span>
        </div>
        <div class="file-item-actions">
          <button class="file-action-btn btn-del-file" title="Delete file">✕</button>
        </div>
      `;

      item.addEventListener('click', () => {
        switchToFile(fn);
      });

      const btnDel = item.querySelector('.btn-del-file');
      btnDel.addEventListener('click', (e) => {
        e.stopPropagation();
        deleteFile(fn);
      });

      fileTreeContainer.appendChild(item);
    });
  }

  // Tab Switching
  function renderTabs() {
    editorTabsBar.innerHTML = '';
    openTabs.forEach(fn => {
      const tab = document.createElement('div');
      tab.className = `editor-tab ${fn === activeFile ? 'active' : ''}`;
      const icon = fn.endsWith('.uf') || fn.endsWith('.unfish') ? '🐡' : '📄';
      tab.innerHTML = `
        <span>${icon} ${fn}</span>
        <button class="tab-close" title="Close tab">✕</button>
      `;

      tab.addEventListener('click', (e) => {
        if (e.target.classList.contains('tab-close')) return;
        switchToFile(fn);
      });

      const btnClose = tab.querySelector('.tab-close');
      btnClose.addEventListener('click', (e) => {
        e.stopPropagation();
        closeTab(fn);
      });

      editorTabsBar.appendChild(tab);
    });
  }

  function switchToFile(fn) {
    if (!files[fn]) return;
    files[activeFile] = codeTextarea.value;
    activeFile = fn;
    if (!openTabs.includes(fn)) openTabs.push(fn);
    renderFileTree();
    renderTabs();
    loadActiveFile();
  }

  function closeTab(fn) {
    const idx = openTabs.indexOf(fn);
    if (idx !== -1) {
      openTabs.splice(idx, 1);
      if (activeFile === fn) {
        activeFile = openTabs.length > 0 ? openTabs[openTabs.length - 1] : Object.keys(files)[0];
      }
      renderTabs();
      renderFileTree();
      loadActiveFile();
    }
  }

  function deleteFile(fn) {
    if (Object.keys(files).length <= 1) {
      alert('Cannot delete the last file in the workspace.');
      return;
    }
    if (confirm(`Delete '${fn}'?`)) {
      delete files[fn];
      closeTab(fn);
    }
  }

  function loadActiveFile() {
    codeTextarea.value = files[activeFile] || '';
    updateLineNumbers();
    isBlocksDirty = true;
  }

  // Line Numbers & Breakpoints
  function updateLineNumbers() {
    const totalLines = codeTextarea.value.split('\n').length;
    lineNumbers.innerHTML = '';

    for (let i = 1; i <= totalLines; i++) {
      const lineEl = document.createElement('div');
      lineEl.className = `line-number ${breakpoints.has(i) ? 'has-breakpoint' : ''}`;
      lineEl.dataset.line = i;
      lineEl.textContent = i;
      lineEl.addEventListener('click', () => {
        if (breakpoints.has(i)) {
          breakpoints.delete(i);
        } else {
          breakpoints.add(i);
        }
        updateLineNumbers();
        updateBreakpointsList();
      });
      lineNumbers.appendChild(lineEl);
    }
  }

  function updateBreakpointsList() {
    const list = document.getElementById('dbg-breakpoints-list');
    if (breakpoints.size === 0) {
      list.innerHTML = '<div class="terminal-line" style="color:var(--text-muted);">Click line numbers to set breakpoints.</div>';
      return;
    }
    list.innerHTML = Array.from(breakpoints).sort((a, b) => a - b).map(line => `
      <div class="terminal-line" style="cursor:pointer;" onclick="jumpToLine(${line})">
        🔴 Line ${line} in <code>${activeFile}</code>
      </div>
    `).join('');
  }

  window.jumpToLine = function(lineNum) {
    const lines = codeTextarea.value.split('\n');
    let charIndex = 0;
    for (let i = 0; i < lineNum - 1 && i < lines.length; i++) {
      charIndex += lines[i].length + 1;
    }
    codeTextarea.focus();
    codeTextarea.setSelectionRange(charIndex, charIndex);
  };

  // Editor Input & Cursor
  codeTextarea.addEventListener('input', () => {
    files[activeFile] = codeTextarea.value;
    updateLineNumbers();
    isBlocksDirty = true;
  });

  codeTextarea.addEventListener('scroll', () => {
    lineNumbers.scrollTop = codeTextarea.scrollTop;
  });

  codeTextarea.addEventListener('keyup', updateCursorPos);
  codeTextarea.addEventListener('click', updateCursorPos);

  function updateCursorPos() {
    const pos = codeTextarea.selectionStart;
    const text = codeTextarea.value.substring(0, pos);
    const lines = text.split('\n');
    const curLine = lines.length;
    const curCol = lines[lines.length - 1].length + 1;
    cursorPos.textContent = `Line ${curLine}, Column ${curCol}`;
  }

  // Keyboard Navigation (<kbd>Tab</kbd>, auto-indent on Enter)
  codeTextarea.addEventListener('keydown', (e) => {
    if (e.key === 'Tab') {
      e.preventDefault();
      const start = codeTextarea.selectionStart;
      const end = codeTextarea.selectionEnd;
      codeTextarea.value = codeTextarea.value.substring(0, start) + '    ' + codeTextarea.value.substring(end);
      codeTextarea.selectionStart = codeTextarea.selectionEnd = start + 4;
      files[activeFile] = codeTextarea.value;
      updateLineNumbers();
    } else if (e.key === 'Enter') {
      const start = codeTextarea.selectionStart;
      const textBefore = codeTextarea.value.substring(0, start);
      const lastLine = textBefore.split('\n').pop();
      const indentMatch = lastLine.match(/^(\s*)/);
      let indent = indentMatch ? indentMatch[1] : '';
      if (lastLine.trim().endsWith(':')) {
        indent += '    ';
      }
      if (indent.length > 0) {
        e.preventDefault();
        const textAfter = codeTextarea.value.substring(start);
        codeTextarea.value = textBefore + '\n' + indent + textAfter;
        codeTextarea.selectionStart = codeTextarea.selectionEnd = start + 1 + indent.length;
        files[activeFile] = codeTextarea.value;
        updateLineNumbers();
      }
    } else if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      runActiveFile(0); // Run AST
    } else if (e.key === 'F5') {
      e.preventDefault();
      startDebug();
    } else if ((e.ctrlKey || e.metaKey) && e.key === 'f') {
      e.preventDefault();
      openSearchBar();
    }
  });

  // Mode Switching (Code Editor vs Blocks)
  modeBtnCode.addEventListener('click', () => {
    modeBtnCode.classList.add('active');
    modeBtnBlocks.classList.remove('active');
    codeEditorContainer.style.display = 'flex';
    studioBlocksView.classList.remove('active');
  });

  modeBtnBlocks.addEventListener('click', () => {
    modeBtnBlocks.classList.add('active');
    modeBtnCode.classList.remove('active');
    codeEditorContainer.style.display = 'none';
    studioBlocksView.classList.add('active');
    syncCodeToBlocks();
  });

  function syncCodeToBlocks() {
    currentBlocksData = currentEngine.parseBlocks(codeTextarea.value);
    currentEngine.renderEditableBlocks(currentBlocksData, blocksCanvasContainer, (updatedData) => {
      currentBlocksData = updatedData;
      const newCode = currentEngine.blocksToCode(updatedData);
      codeTextarea.value = newCode;
      files[activeFile] = newCode;
      updateLineNumbers();
    });
  }

  // Sidebar Activity Bar Navigation
  actBtnFiles.addEventListener('click', () => {
    actBtnFiles.classList.add('active');
    actBtnBlocks.classList.remove('active');
    actBtnDebug.classList.remove('active');
    sidebarExplorer.style.display = 'block';
    sidebarBlocks.style.display = 'none';
    studioSidebar.classList.remove('collapsed');
  });

  actBtnBlocks.addEventListener('click', () => {
    actBtnBlocks.classList.add('active');
    actBtnFiles.classList.remove('active');
    actBtnDebug.classList.remove('active');
    sidebarExplorer.style.display = 'none';
    sidebarBlocks.style.display = 'block';
    studioSidebar.classList.remove('collapsed');
  });

  actBtnDebug.addEventListener('click', () => {
    switchDrawerPane('debug');
  });

  // Blocks Palette Drag/Click to insert
  document.querySelectorAll('.palette-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const kind = btn.dataset.kind;
      if (!currentBlocksData.statements) currentBlocksData.statements = [];
      const newStmt = { kind, indent: 0 };
      if (kind === 'let') { newStmt.name = 'x'; newStmt.value = '42'; }
      else if (kind === 'say') { newStmt.value = '"Hello, World!"'; }
      else if (kind === 'function') { newStmt.name = 'my_func'; newStmt.params = 'a, b'; }
      else if (kind === 'if') { newStmt.condition = 'x > 10'; }
      else if (kind === 'while') { newStmt.condition = 'x > 0'; }
      else if (kind === 'repeat') { newStmt.count = '5'; }
      else if (kind === 'struct') { newStmt.name = 'Point'; }
      else if (kind === 'enum') { newStmt.name = 'Status'; }

      currentBlocksData.statements.push(newStmt);
      const newCode = currentEngine.blocksToCode(currentBlocksData);
      codeTextarea.value = newCode;
      files[activeFile] = newCode;
      updateLineNumbers();
      syncCodeToBlocks();
    });
  });

  // Drawer Tabs
  drawerTabs.forEach(tab => {
    tab.addEventListener('click', () => {
      const pane = tab.dataset.pane;
      switchDrawerPane(pane);
    });
  });

  function switchDrawerPane(paneName) {
    drawerTabs.forEach(t => t.classList.toggle('active', t.dataset.pane === paneName));
    drawerPanes.forEach(p => p.classList.toggle('active', p.id === `pane-${paneName}`));
  }

  // Execution: AST, VM, RegVM
  btnRunAst.addEventListener('click', () => runActiveFile(0));
  btnRunVm.addEventListener('click', () => runActiveFile(1));
  btnRunRegvm.addEventListener('click', () => runActiveFile(2));

  function runActiveFile(engineMode) {
    const code = codeTextarea.value;
    const modeNames = ['AST Interpreter', 'Stack Bytecode VM', 'Register Bytecode VM'];
    const modeName = modeNames[engineMode] || 'AST Interpreter';

    switchDrawerPane('terminal');
    terminalView.innerHTML = `<span class="terminal-line system">⏳ Running '${activeFile}' via ${modeName}...</span>`;

    const startTime = performance.now();

    if (hasNativeBackend) {
      const endpoints = ['/api/run', '/api/run-vm', '/api/run-regvm'];
      fetch(endpoints[engineMode], {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        const elapsed = Math.round(performance.now() - startTime);
        handleExecutionOutput(data, elapsed, `${modeName} (Native C99)`);
      })
      .catch(() => {
        runInBrowserFallback(code, modeName);
      });
    } else {
      runInBrowserFallback(code, modeName);
    }

    // Also update AST, Disasm, and Tokens inspectors asynchronously
    updateInspectors(code);
  }

  function runInBrowserFallback(code, modeName) {
    const startTime = performance.now();
    const result = currentEngine.run(code);
    const elapsed = Math.round(performance.now() - startTime);
    handleExecutionOutput(result, elapsed, `${modeName} (Browser Engine)`);
  }

  function handleExecutionOutput(data, elapsed, modeLabel) {
    terminalView.innerHTML = '';
    const statusLine = document.createElement('div');
    statusLine.className = `terminal-line ${data.exit_code === 0 ? 'success' : 'error'}`;
    statusLine.textContent = `[${modeLabel}] Finished in ${elapsed} ms • Exit Code: ${data.exit_code}`;
    terminalView.appendChild(statusLine);

    if (data.stdout) {
      const outBlock = document.createElement('div');
      outBlock.className = 'terminal-line';
      outBlock.textContent = data.stdout;
      terminalView.appendChild(outBlock);
    }

    if (data.stderr) {
      const errBlock = document.createElement('div');
      errBlock.className = 'terminal-line error';
      errBlock.textContent = data.stderr;
      terminalView.appendChild(errBlock);
      updateProblemsList(data.stderr);
    } else {
      clearProblems();
    }
  }

  function updateInspectors(code) {
    try {
      const ast = currentEngine.parse(code);
      astView.textContent = currentEngine.formatAst(ast);

      const disasm = currentEngine.disassemble(code);
      disasmView.textContent = disasm;

      const { tokens } = currentEngine.tokenize(code);
      tokensView.textContent = tokens.map(t => t.toString()).join('\n');
    } catch (e) {
      astView.textContent = `Parse Error: ${e.message}`;
    }
  }

  function updateProblemsList(stderr) {
    const lines = stderr.split('\n').filter(Boolean);
    problemsCount.style.display = 'inline-block';
    problemsCount.textContent = lines.length;

    problemsList.innerHTML = lines.map(errLine => `
      <div class="problem-item">
        <span class="problem-badge error">ERROR</span>
        <span>${escapeHtml(errLine)}</span>
      </div>
    `).join('');
  }

  function clearProblems() {
    problemsCount.style.display = 'none';
    problemsList.innerHTML = '<span class="terminal-line" style="color:var(--text-muted);">No syntax or runtime issues detected.</span>';
  }

  // Format Code
  btnFormatCode.addEventListener('click', () => {
    const code = codeTextarea.value;
    if (hasNativeBackend) {
      fetch('/api/format', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
      })
      .then(res => res.json())
      .then(data => {
        if (data.stdout) {
          codeTextarea.value = data.stdout;
          files[activeFile] = data.stdout;
          updateLineNumbers();
        }
      })
      .catch(() => inBrowserFormat(code));
    } else {
      inBrowserFormat(code);
    }
  });

  function inBrowserFormat(code) {
    const blocks = currentEngine.parseBlocks(code);
    const clean = currentEngine.blocksToCode(blocks);
    codeTextarea.value = clean;
    files[activeFile] = clean;
    updateLineNumbers();
  }

  // Step Debugger Simulation
  let debugActive = false;
  let debugLines = [];
  let debugCurrentLine = 0;

  btnStartDebug.addEventListener('click', startDebug);

  function startDebug() {
    switchDrawerPane('debug');
    debugActive = true;
    debugLines = codeTextarea.value.split('\n');
    debugCurrentLine = 1;
    document.getElementById('dbg-call-stack').innerHTML = `<div class="terminal-line"><span style="color:var(--accent-cyan);">&gt;</span> &lt;main&gt; (line 1)</div>`;
    updateDebugVars({ 'sys': '<module sys>', 'fs': '<module fs>', 'time': '<module time>' });
  }

  document.getElementById('dbg-btn-continue').addEventListener('click', () => {
    debugCurrentLine = Math.min(debugLines.length, debugCurrentLine + 2);
    document.getElementById('dbg-call-stack').innerHTML = `<div class="terminal-line"><span style="color:var(--accent-cyan);">&gt;</span> &lt;main&gt; (line ${debugCurrentLine})</div>`;
  });

  document.getElementById('dbg-btn-step-over').addEventListener('click', () => {
    debugCurrentLine = Math.min(debugLines.length, debugCurrentLine + 1);
    document.getElementById('dbg-call-stack').innerHTML = `<div class="terminal-line"><span style="color:var(--accent-cyan);">&gt;</span> &lt;main&gt; (line ${debugCurrentLine})</div>`;
  });

  document.getElementById('dbg-btn-stop').addEventListener('click', () => {
    debugActive = false;
    document.getElementById('dbg-call-stack').innerHTML = `<div class="terminal-line" style="color:var(--text-muted);">&lt;main&gt; (stopped)</div>`;
  });

  function updateDebugVars(varsObj) {
    const list = document.getElementById('dbg-variables-list');
    list.innerHTML = Object.entries(varsObj).map(([k, v]) => `
      <div class="debug-var-row">
        <span class="debug-var-name">${k}</span>
        <span class="debug-var-val">${v}</span>
      </div>
    `).join('');
  }

  // Clear Terminal
  btnClearTerminal.addEventListener('click', () => {
    terminalView.innerHTML = '<span class="terminal-line system">🐡 Terminal cleared.</span>';
  });

  // New File
  btnNewFile.addEventListener('click', () => {
    const name = prompt('Enter new file path (e.g. src/utils.uf):', 'src/new_file.uf');
    if (name && name.trim()) {
      const cleanName = name.trim();
      if (!files[cleanName]) {
        files[cleanName] = `## ${cleanName}\n\n`;
        switchToFile(cleanName);
      }
    }
  });

  // Download Active File
  btnDownloadFile.addEventListener('click', () => {
    const blob = new Blob([codeTextarea.value], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = activeFile.split('/').pop() || 'script.uf';
    a.click();
  });

  // Export Project JSON
  btnExportProj.addEventListener('click', () => {
    files[activeFile] = codeTextarea.value;
    const blob = new Blob([JSON.stringify(files, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'unfish_project.json';
    a.click();
  });

  // Share Modal
  btnShare.addEventListener('click', () => {
    files[activeFile] = codeTextarea.value;
    const encoded = btoa(encodeURIComponent(JSON.stringify(files)));
    const url = `${window.location.origin}${window.location.pathname}#project=${encoded}`;
    shareUrlInput.value = url;
    shareModal.classList.add('active');
  });

  btnCloseShareModal.addEventListener('click', () => {
    shareModal.classList.remove('active');
  });

  btnCopyShareUrl.addEventListener('click', () => {
    shareUrlInput.select();
    navigator.clipboard.writeText(shareUrlInput.value);
    btnCopyShareUrl.textContent = 'Copied!';
    setTimeout(() => { btnCopyShareUrl.textContent = 'Copy Link'; }, 2000);
  });

  // Theme Toggle
  btnThemeToggle.addEventListener('click', () => {
    const currentTheme = document.documentElement.getAttribute('data-theme');
    const newTheme = currentTheme === 'light' ? 'dark' : 'light';
    document.documentElement.setAttribute('data-theme', newTheme);
    localStorage.setItem('unfish_theme', newTheme);
  });

  const savedTheme = localStorage.getItem('unfish_theme');
  if (savedTheme) {
    document.documentElement.setAttribute('data-theme', savedTheme);
  }

  // Template select change
  sampleProjectSelect.addEventListener('change', () => {
    loadProject(sampleProjectSelect.value);
  });

  // Search Bar
  function openSearchBar() {
    editorSearchBar.classList.add('visible');
    searchInput.focus();
  }

  btnSearchClose.addEventListener('click', () => {
    editorSearchBar.classList.remove('visible');
  });

  function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // Check URL Hash for shared projects
  if (window.location.hash.startsWith('#project=')) {
    try {
      const raw = window.location.hash.replace('#project=', '');
      const decoded = decodeURIComponent(atob(raw));
      files = JSON.parse(decoded);
      activeFile = files['src/main.uf'] ? 'src/main.uf' : Object.keys(files)[0];
      openTabs = [activeFile];
      renderFileTree();
      renderTabs();
      loadActiveFile();
    } catch (e) {
      loadProject('hello');
    }
  } else {
    loadProject('hello');
  }

  checkBackend();

})();
