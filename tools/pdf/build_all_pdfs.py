#!/usr/bin/env python3
"""
Unfish PDF Builder & Publication Pipeline
Compiles markdown manuals, pitch decks, tutorials, and textbooks into professional PDFs.
"""

import os
import re
import subprocess
import sys
import time

DOCS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../docs"))
PDF_OUTPUT_DIR = os.path.join(DOCS_DIR, "pdf")

DOCUMENTS = [
    {
        "name": "Executive Pitch Deck",
        "src": os.path.join(DOCS_DIR, "pitch/UNFISH_PITCH_DECK.md"),
        "dest": os.path.join(PDF_OUTPUT_DIR, "Unfish_Executive_Pitch_Deck.pdf"),
        "toc": False,
        "color": "NavyBlue"
    },
    {
        "name": "Beginner's Guide & Tutorial",
        "src": os.path.join(DOCS_DIR, "tutorials/BEGINNERS_GUIDE.md"),
        "dest": os.path.join(PDF_OUTPUT_DIR, "Unfish_Beginners_Guide.pdf"),
        "toc": True,
        "color": "RoyalBlue"
    },
    {
        "name": "Technical Architecture Specification",
        "src": os.path.join(DOCS_DIR, "technical/TECHNICAL_SPECIFICATION.md"),
        "dest": os.path.join(PDF_OUTPUT_DIR, "Unfish_Technical_Specification.pdf"),
        "toc": True,
        "color": "MidnightBlue"
    },
    {
        "name": "The Unfish Programming Language (The Book)",
        "src": os.path.join(DOCS_DIR, "book/THE_UNFISH_BOOK.md"),
        "dest": os.path.join(PDF_OUTPUT_DIR, "The_Unfish_Book.pdf"),
        "toc": True,
        "color": "Black"
    }
]

EMOJI_MAP = {
    '🐠': '[Fish]',
    '🌊': '[Wave]',
    '🏊': '[Swim]',
    '🚀': '[Fast]',
    '⚡': '[Speed]',
    '🛡️': '[Secure]',
    '🛡': '[Secure]',
    '📦': '[Package]',
    '🔍': '[Inspect]',
    '🔧': '[Tool]',
    '⚙️': '[Engine]',
    '⚙': '[Engine]',
    '✅': '[PASS]',
    '❌': '[FAIL]',
    '💡': '[Note]',
    '📌': '[Reference]',
    '•': '-',
    '–': '--',
    '—': '---',
    '“': '"',
    '”': '"',
    '‘': "'",
    '’': "'",
    '≈': '~=',
    '±': '+/-',
    '≤': '<=',
    '≥': '>=',
    '≠': '!=',
    '→': '->',
    '←': '<-',
    '↔': '<->',
    '⇒': '=>',
    '─': '-',
    '│': '|',
    '┌': '+',
    '┐': '+',
    '└': '+',
    '┘': '+',
    '├': '+',
    '┤': '+',
    '┬': '+',
    '┴': '+',
    '┼': '+',
    '═': '=',
    '║': '|',
    '╔': '+',
    '╗': '+',
    '╚': '+',
    '╝': '+',
    '╠': '+',
    '╣': '+',
    '╦': '+',
    '╩': '+',
    '╬': '+',
    '▲': '^',
    '▼': 'v',
    '►': '>',
    '◄': '<',
}

def clean_markdown_for_pdflatex(text):
    for k, v in EMOJI_MAP.items():
        text = text.replace(k, v)
    # Remove any remaining 4-byte surrogate Unicode characters
    text = re.sub(r'[\U00010000-\U0010ffff]', '', text)
    return text

def build_pdf(doc):
    name = doc["name"]
    src = doc["src"]
    dest = doc["dest"]
    has_toc = doc["toc"]

    print(f"[*] Building PDF: {name} ...")
    if not os.path.exists(src):
        print(f"[!] Error: Source file '{src}' does not exist.")
        return False

    with open(src, "r", encoding="utf-8") as f:
        raw_text = f.read()

    cleaned_text = clean_markdown_for_pdflatex(raw_text)
    tmp_clean_md = src + ".tmp.md"
    with open(tmp_clean_md, "w", encoding="utf-8") as f:
        f.write(cleaned_text)

    cmd = [
        "pandoc",
        tmp_clean_md,
        "-o", dest,
        "--pdf-engine=pdflatex",
        "-V", "geometry:margin=1in",
        "-V", "documentclass=article",
        "--highlight-style=tango"
    ]
    if has_toc:
        cmd.extend(["--toc", "--number-sections"])

    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    if os.path.exists(tmp_clean_md):
        os.remove(tmp_clean_md)

    elapsed = time.time() - t0

    if result.returncode != 0:
        print(f"[!] Failed to compile {name}:")
        print(result.stderr)
        return False

    size_kb = os.path.getsize(dest) / 1024.0
    print(f"[+] Successfully generated: {dest} ({size_kb:.1f} KB in {elapsed:.2f}s)")
    return True

def main():
    os.makedirs(PDF_OUTPUT_DIR, exist_ok=True)
    print("=" * 70)
    print("UNFISH DOCUMENTATION & BOOK PUBLISHING PIPELINE")
    print(f"Output Directory: {PDF_OUTPUT_DIR}")
    print("=" * 70)

    success_count = 0
    for doc in DOCUMENTS:
        if build_pdf(doc):
            success_count += 1
        print("-" * 70)

    print(f"\nCompleted: {success_count}/{len(DOCUMENTS)} PDFs successfully generated.")
    if success_count == len(DOCUMENTS):
        print("All publications are up-to-date and ready for distribution!")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
