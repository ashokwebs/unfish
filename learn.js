/**
 * 🐡 Unfish Learn — Documentation & Education Platform Controller
 */

(function() {
  'use strict';

  const engine = new window.UnfishEngine();

  // Sidebar navigation
  const navItems = document.querySelectorAll('.nav-item');
  const chapters = document.querySelectorAll('.doc-chapter');

  navItems.forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      const target = item.dataset.target;
      switchChapter(target);
    });
  });

  function switchChapter(targetId) {
    navItems.forEach(n => n.classList.toggle('active', n.dataset.target === targetId));
    chapters.forEach(c => c.classList.toggle('active', c.id === `doc-${targetId}`));
    window.location.hash = targetId;
    document.getElementById('learn-content').scrollTop = 0;
  }

  // Handle URL hash on load
  if (window.location.hash) {
    const hash = window.location.hash.substring(1);
    const match = document.getElementById(`doc-${hash}`);
    if (match) {
      switchChapter(hash);
    }
  }

  // Interactive Code Runners
  document.querySelectorAll('.code-runner').forEach(runner => {
    const btnRun = runner.querySelector('.btn-run-example');
    const btnOpenStudio = runner.querySelector('.btn-open-studio');
    const editor = runner.querySelector('.runner-editor');
    const output = runner.querySelector('.runner-output');

    if (btnRun && editor && output) {
      btnRun.addEventListener('click', () => {
        const code = editor.value;
        btnRun.textContent = 'Running...';
        btnRun.disabled = true;

        setTimeout(() => {
          const res = engine.run(code);
          output.classList.add('active');
          if (res.exit_code === 0) {
            output.textContent = res.stdout || '(Program completed with no output)';
            output.style.color = 'var(--accent-green)';
          } else {
            output.textContent = res.stderr || 'Execution failed';
            output.style.color = 'var(--accent-red)';
          }
          btnRun.textContent = '▶ Run in Place';
          btnRun.disabled = false;
        }, 30);
      });
    }

    if (btnOpenStudio && editor) {
      btnOpenStudio.addEventListener('click', () => {
        const code = editor.value;
        const proj = {
          'unfish.toml': '[package]\nname = "learn_example"\nversion = "1.0.0"\nentry = "src/main.uf"',
          'src/main.uf': code
        };
        const encoded = btoa(encodeURIComponent(JSON.stringify(proj)));
        const studioUrl = window.location.pathname.includes('/docs') ? '../studio.html' : 'studio.html';
        window.open(`${studioUrl}#project=${encoded}`, '_blank');
      });
    }
  });

  // "Learn the Computer" Pipeline Visualizer
  const pipelineCodeInput = document.getElementById('pipeline-code-input');
  const btnPipelineAnalyze = document.getElementById('btn-pipeline-analyze');
  const pipelineSteps = document.querySelectorAll('.pipeline-step');
  const pipelineOutputView = document.getElementById('pipeline-output-view');

  let currentPipelineData = null;
  let activePipelineStage = 'tokens';

  function analyzePipeline() {
    const code = pipelineCodeInput.value;
    try {
      const { tokens } = engine.tokenize(code);
      const ast = engine.parse(code);
      const disasm = engine.disassemble(code);
      const runRes = engine.run(code);

      currentPipelineData = {
        tokens: tokens.map(t => t.toString()).join('\n'),
        ast: engine.formatAst(ast),
        disasm: disasm,
        output: runRes.exit_code === 0 ? (runRes.stdout || '(no output)') : `Error:\n${runRes.stderr}`,
        memory: `[UNFISH RUNTIME HEAP SNAPSHOT]
• Program Execution Status: ${runRes.exit_code === 0 ? 'Success (Exit 0)' : 'Runtime Error'}
• Value Representation: 16-byte Tagged Union (UfValue)
• Object Headers: UfObj (next, type, is_marked)
• Active Call Frames: 1 (<main>)
• Temporary GC Roots: protected in rt->temp_roots
• Active Scheduler Fibers: 0 pending
• Total Parsed Statements: ${ast.body ? ast.body.length : 0}`
      };

      updatePipelineDisplay();
    } catch (err) {
      pipelineOutputView.textContent = `Pipeline Error: ${err.message}`;
    }
  }

  function updatePipelineDisplay() {
    if (!currentPipelineData) return;
    pipelineOutputView.textContent = currentPipelineData[activePipelineStage] || '(no data)';
  }

  if (btnPipelineAnalyze) {
    btnPipelineAnalyze.addEventListener('click', analyzePipeline);
  }

  pipelineSteps.forEach(step => {
    step.addEventListener('click', () => {
      pipelineSteps.forEach(s => s.classList.remove('active'));
      step.classList.add('active');
      activePipelineStage = step.dataset.stage;
      updatePipelineDisplay();
    });
  });

  // Initial analysis
  if (pipelineCodeInput) {
    analyzePipeline();
  }

  // Global Search
  const searchInput = document.getElementById('global-search-input');
  const searchModal = document.getElementById('search-modal');
  const modalSearchInput = document.getElementById('modal-search-input');
  const searchResultsList = document.getElementById('search-results-list');

  const SEARCH_INDEX = [
    { target: 'downloads', title: 'Official Downloads (v2.0.0)', text: 'download linux binary tarball vscode vsix extension runtime unfish_runtime.h curl installer' },
    { target: 'quickstart', title: 'Quick Start & CLI Reference', text: 'cli run vm regvm build wasm debug check test lsp repl studio playground' },
    { target: 'intro', title: 'What is Unfish?', text: 'general-purpose programming language pure ANSI C99 zero dependencies philosophy 5 execution backends' },
    { target: 'install', title: 'Installation & Setup', text: 'make gcc clang build from source test-asan benchmarks' },
    { target: 'first-program', title: 'Your First Program', text: 'fibonacci recursion say print function let' },
    { target: 'repl-cli', title: 'REPL & Toolchain', text: 'unfish run vm regvm build wasm debug format studio lsp repl' },
    { target: 'learn-computer', title: 'Learn the Computer', text: 'pipeline tokens ast bytecode isa memory gc layout virtual machine' },
    { target: 'tut-01', title: '01. Hello & Text Output', text: 'say print string interpolation f-string formatted text' },
    { target: 'tut-02', title: '02. Values & Types', text: 'number string boolean null array map type_of tagged union 16 bytes' },
    { target: 'tut-03', title: '03. Variables & Scope', text: 'let assignment scoping lexical blocks shadowing' },
    { target: 'tut-04', title: '04. Expressions & Operators', text: 'pratt parser arithmetic comparison logical and or not pipe' },
    { target: 'tut-05', title: '05. Conditions & Logic', text: 'if else off-side rule indentation colon' },
    { target: 'tut-06', title: '06. Loops & Iteration', text: 'while for in repeat times break continue' },
    { target: 'tut-07', title: '07. Functions & Defaults', text: 'function fn return default parameters recursion' },
    { target: 'tut-08', title: '08. Higher-Order Functions', text: 'map filter reduce find some every higher order functions' },
    { target: 'tut-09', title: '09. Closures & Pipelines', text: 'lambda higher order functions map filter reduce first class pipe forward operator' },
    { target: 'tut-10', title: '10. Arrays, Maps & Buffers', text: 'array push pop concat slice map dictionary key value buffer binary' },
    { target: 'tut-11', title: '11. Destructuring', text: 'array destructure map destructure pattern let rest spread' },
    { target: 'tut-12', title: '12. Structs & Methods', text: 'struct fields methods self object oriented OOP classes' },
    { target: 'tut-13', title: '13. Enums & Matching', text: 'enum sum types pattern matching match when variants algebraic data types ADT' },
    { target: 'tut-14', title: '14. Error Recovery', text: 'try catch finally error handling exceptions throw assertion' },
    { target: 'tut-15', title: '15. Memory Model & GC', text: 'arenas mark sweep garbage collection memory layout heap tagged union' },
    { target: 'tut-16', title: '16. VM & Native Compilation', text: 'stack vm register vm c99 transpiler standalone native executable wasm' },
    { target: 'tut-17', title: '17. Fibers & Concurrency', text: 'cooperative concurrency spawn yield channel send recv scheduler csp green threads' },
    { target: 'stdlib-core', title: 'Core Builtins', text: 'len push pop range concat flatten zip pad_start pad_end trim clock assert sqrt pow min max' },
    { target: 'stdlib-sys', title: 'Module: sys', text: 'platform exit args cwd set_env exec system' },
    { target: 'stdlib-fs', title: 'Module: fs', text: 'read_text write_text append_text exists delete_file list_dir mkdir is_file' },
    { target: 'stdlib-time', title: 'Module: time', text: 'clock sleep timestamp format iso time date' },
    { target: 'stdlib-random', title: 'Module: random', text: 'random random_int prng seed shuffle' },
    { target: 'stdlib-json', title: 'Module: json', text: 'json parse serialize stringify json_parse json_stringify' },
    { target: 'stdlib-testing', title: 'Module: testing', text: 'testing assert assert_eq test suites unit testing' },
    { target: 'spec-ebnf', title: 'Formal Grammar & EBNF Specification', text: 'ebnf grammar syntax parser pratt precedence associativity ast formal specification' },
    { target: 'spec-vm-isa', title: '57-Opcode Stack VM ISA Specification', text: 'vm isa opcode stack instructions bytecode binary ufc container assembly disasm' },
    { target: 'spec-regvm', title: '256-Register VM Architecture', text: 'register vm regvm 3-address code instructions register allocation windowing throughput' },
    { target: 'spec-embed', title: 'C99 AOT & Embedding Guide', text: 'c99 aot compiler embedding unfish_runtime.h host integration native c api baremetal' },
    { target: 'spec-comparison', title: 'Architectural Comparison Matrix', text: 'comparison benchmark python lua go rust javascript tradeoffs performance' }
  ];

  function openSearch() {
    searchModal.classList.add('active');
    modalSearchInput.value = '';
    renderSearchResults('');
    modalSearchInput.focus();
  }

  function closeSearch() {
    searchModal.classList.remove('active');
  }

  if (searchInput) {
    searchInput.addEventListener('focus', openSearch);
  }

  window.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
      e.preventDefault();
      openSearch();
    } else if (e.key === 'Escape') {
      closeSearch();
    }
  });

  searchModal.addEventListener('click', (e) => {
    if (e.target === searchModal) closeSearch();
  });

  modalSearchInput.addEventListener('input', () => {
    renderSearchResults(modalSearchInput.value.trim().toLowerCase());
  });

  function renderSearchResults(query) {
    searchResultsList.innerHTML = '';
    if (!query) {
      searchResultsList.innerHTML = '<div style="padding:12px 16px; color:var(--text-muted); font-size:0.85rem;">Type to search documentation and tutorials...</div>';
      return;
    }

    const matches = SEARCH_INDEX.filter(item => {
      return item.title.toLowerCase().includes(query) || item.text.toLowerCase().includes(query);
    });

    if (matches.length === 0) {
      searchResultsList.innerHTML = '<div style="padding:12px 16px; color:var(--text-muted); font-size:0.85rem;">No results found.</div>';
      return;
    }

    matches.forEach(m => {
      const el = document.createElement('div');
      el.className = 'search-result-item';
      el.innerHTML = `
        <span class="result-title">${m.title}</span>
        <span class="result-snippet">${m.text}</span>
      `;
      el.addEventListener('click', () => {
        closeSearch();
        switchChapter(m.target);
      });
      searchResultsList.appendChild(el);
    });
  }

  // Inject Next / Previous Chapter Navigation Footers
  function injectChapterNavigation() {
    const navItemEls = Array.from(document.querySelectorAll('.learn-sidebar .nav-item'));
    const chaptersList = navItemEls.map(item => ({
      target: item.dataset.target,
      title: item.textContent.trim().replace(/^[\p{Emoji}\s]+/u, '') || item.textContent.trim()
    })).filter(c => c.target);

    chaptersList.forEach((chap, idx) => {
      const art = document.getElementById(`doc-${chap.target}`);
      if (!art) return;
      if (art.querySelector('.chapter-nav-footer')) return;

      const footer = document.createElement('div');
      footer.className = 'chapter-nav-footer';

      if (idx > 0) {
        const prev = chaptersList[idx - 1];
        const btnPrev = document.createElement('a');
        btnPrev.className = 'btn-doc-nav prev';
        btnPrev.href = `#${prev.target}`;
        btnPrev.dataset.target = prev.target;
        btnPrev.innerHTML = `
          <span class="nav-direction-label">← Previous Chapter</span>
          <span class="nav-chapter-title">${prev.title}</span>
        `;
        btnPrev.addEventListener('click', (e) => {
          e.preventDefault();
          switchChapter(prev.target);
        });
        footer.appendChild(btnPrev);
      } else {
        const spacer = document.createElement('div');
        footer.appendChild(spacer);
      }

      if (idx < chaptersList.length - 1) {
        const next = chaptersList[idx + 1];
        const btnNext = document.createElement('a');
        btnNext.className = 'btn-doc-nav next';
        btnNext.href = `#${next.target}`;
        btnNext.dataset.target = next.target;
        btnNext.innerHTML = `
          <span class="nav-direction-label">Next Chapter →</span>
          <span class="nav-chapter-title">${next.title}</span>
        `;
        btnNext.addEventListener('click', (e) => {
          e.preventDefault();
          switchChapter(next.target);
        });
        footer.appendChild(btnNext);
      }

      art.appendChild(footer);
    });
  }

  // Run on load
  injectChapterNavigation();

  // Theme toggle
  const btnThemeToggle = document.getElementById('btn-theme-toggle');
  if (btnThemeToggle) {
    btnThemeToggle.addEventListener('click', () => {
      const cur = document.documentElement.getAttribute('data-theme');
      const next = cur === 'light' ? 'dark' : 'light';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('unfish_theme', next);
    });
  }

  const savedTheme = localStorage.getItem('unfish_theme');
  if (savedTheme) {
    document.documentElement.setAttribute('data-theme', savedTheme);
  }

})();
