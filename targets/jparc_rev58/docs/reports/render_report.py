#!/usr/bin/env python3
"""Render the report HTML as A4 PDF. Requires PyMuPDF 2.0 (pip install PyMuPDF==2.0).
Usage: python render_report.py [report.html]
HTML source uses <!-- PAGE --> for explicit page boundaries. Fonts are embedded.
"""
from pathlib import Path
import re
import sys
import pymupdf as fitz

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name('2026-10-26_work_report_ja.html')
html = source.read_text(encoding='utf-8')
css = re.search(r'<style>(.*?)</style>', html, re.S).group(1)
pages = html.split('<!-- PAGE -->')[1:]
doc = fitz.open()
for number, content in enumerate(pages, 1):
    content = content.replace('</body></html>', '')
    page = doc.new_page(width=595.276, height=841.89)
    spare, scale = page.insert_htmlbox(fitz.Rect(43, 37, 552, 785), content, css=css, scale_low=1)
    if spare < 0:
        raise RuntimeError(f'Page {number} overflows; edit source instead of shrinking type')
    page.draw_line((43, 800), (552, 800), color=(0.72, 0.79, 0.84), width=0.5)
    page.insert_text((43, 816), 'JPARC rev58 | Work report | 2026-10-26', fontsize=8, color=(0.32, 0.4, 0.47))
    page.insert_text((510, 816), f'{number} / {len(pages)}', fontsize=8, color=(0.32, 0.4, 0.47))
    print(f'Page {number}: spare height {spare:.1f} pt, scale {scale}')
doc.set_metadata({'title': 'JPARC rev58 ソフトウェア開発・実機確認 作業報告書', 'subject': '実施した開発作業、基板書込み、試験結果および資料整備', 'creator': 'PyMuPDF report renderer', 'keywords': 'JPARC, rev58, STM32H745, Zephyr, SPI1, DMA, SDRAM'})
doc.subset_fonts()
output = source.with_suffix('.pdf')
doc.save(output, garbage=4, deflate=True)
print(output)
