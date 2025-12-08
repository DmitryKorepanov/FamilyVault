#!/usr/bin/env python3
"""
Генератор тестовых файлов для проверки TextExtractor.
Создаёт минимальные валидные PDF, DOCX, XLSX, PPTX, ODT файлы.
"""

import os
import zipfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent


def create_docx():
    """Создаёт минимальный валидный DOCX файл."""
    docx_path = SCRIPT_DIR / "document.docx"
    
    # Минимальная структура DOCX (OpenXML)
    content_types = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
    <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
    <Default Extension="xml" ContentType="application/xml"/>
    <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"""

    rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

    document = """<?xml version="1.0" encoding="UTF-8"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
    <w:body>
        <w:p>
            <w:r>
                <w:t>Hello from DOCX document</w:t>
            </w:r>
        </w:p>
        <w:p>
            <w:r>
                <w:t>This is test content for FamilyVault text extraction</w:t>
            </w:r>
        </w:p>
        <w:p>
            <w:r>
                <w:t>Тестовая строка на русском языке</w:t>
            </w:r>
        </w:p>
    </w:body>
</w:document>"""

    with zipfile.ZipFile(docx_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('[Content_Types].xml', content_types)
        zf.writestr('_rels/.rels', rels)
        zf.writestr('word/document.xml', document)
    
    print(f"Created: {docx_path}")


def create_xlsx():
    """Создаёт минимальный валидный XLSX файл."""
    xlsx_path = SCRIPT_DIR / "data.xlsx"
    
    content_types = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
    <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
    <Default Extension="xml" ContentType="application/xml"/>
    <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
    <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
    <Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
</Types>"""

    rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""

    workbook_rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
    <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>
</Relationships>"""

    workbook = """<?xml version="1.0" encoding="UTF-8"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
    <sheets>
        <sheet name="Sheet1" sheetId="1" r:id="rId1"/>
    </sheets>
</workbook>"""

    # Shared strings - текстовые значения ячеек
    shared_strings = """<?xml version="1.0" encoding="UTF-8"?>
<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4">
    <si><t>Name</t></si>
    <si><t>Value</t></si>
    <si><t>Excel test data</t></si>
    <si><t>FamilyVault spreadsheet</t></si>
</sst>"""

    # Sheet1 с данными
    sheet1 = """<?xml version="1.0" encoding="UTF-8"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
    <sheetData>
        <row r="1">
            <c r="A1" t="s"><v>0</v></c>
            <c r="B1" t="s"><v>1</v></c>
        </row>
        <row r="2">
            <c r="A2" t="s"><v>2</v></c>
            <c r="B2"><v>100</v></c>
        </row>
        <row r="3">
            <c r="A3" t="s"><v>3</v></c>
            <c r="B3"><v>200</v></c>
        </row>
    </sheetData>
</worksheet>"""

    with zipfile.ZipFile(xlsx_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('[Content_Types].xml', content_types)
        zf.writestr('_rels/.rels', rels)
        zf.writestr('xl/_rels/workbook.xml.rels', workbook_rels)
        zf.writestr('xl/workbook.xml', workbook)
        zf.writestr('xl/sharedStrings.xml', shared_strings)
        zf.writestr('xl/worksheets/sheet1.xml', sheet1)
    
    print(f"Created: {xlsx_path}")


def create_pptx():
    """Создаёт минимальный валидный PPTX файл."""
    pptx_path = SCRIPT_DIR / "presentation.pptx"
    
    content_types = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
    <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
    <Default Extension="xml" ContentType="application/xml"/>
    <Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>
    <Override PartName="/ppt/slides/slide1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>
</Types>"""

    rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="ppt/presentation.xml"/>
</Relationships>"""

    presentation_rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide1.xml"/>
</Relationships>"""

    presentation = """<?xml version="1.0" encoding="UTF-8"?>
<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
    <p:sldIdLst>
        <p:sldId id="256" r:id="rId1"/>
    </p:sldIdLst>
</p:presentation>"""

    slide1 = """<?xml version="1.0" encoding="UTF-8"?>
<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">
    <p:cSld>
        <p:spTree>
            <p:nvGrpSpPr>
                <p:cNvPr id="1" name=""/>
                <p:cNvGrpSpPr/>
                <p:nvPr/>
            </p:nvGrpSpPr>
            <p:grpSpPr/>
            <p:sp>
                <p:nvSpPr>
                    <p:cNvPr id="2" name="Title"/>
                    <p:cNvSpPr/>
                    <p:nvPr/>
                </p:nvSpPr>
                <p:spPr/>
                <p:txBody>
                    <a:bodyPr/>
                    <a:p>
                        <a:r>
                            <a:t>PowerPoint Test Slide</a:t>
                        </a:r>
                    </a:p>
                    <a:p>
                        <a:r>
                            <a:t>FamilyVault presentation extraction test</a:t>
                        </a:r>
                    </a:p>
                </p:txBody>
            </p:sp>
        </p:spTree>
    </p:cSld>
</p:sld>"""

    with zipfile.ZipFile(pptx_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('[Content_Types].xml', content_types)
        zf.writestr('_rels/.rels', rels)
        zf.writestr('ppt/_rels/presentation.xml.rels', presentation_rels)
        zf.writestr('ppt/presentation.xml', presentation)
        zf.writestr('ppt/slides/slide1.xml', slide1)
    
    print(f"Created: {pptx_path}")


def create_odt():
    """Создаёт минимальный валидный ODT файл."""
    odt_path = SCRIPT_DIR / "document.odt"
    
    mimetype = "application/vnd.oasis.opendocument.text"
    
    manifest = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
    <manifest:file-entry manifest:media-type="application/vnd.oasis.opendocument.text" manifest:full-path="/"/>
    <manifest:file-entry manifest:media-type="text/xml" manifest:full-path="content.xml"/>
</manifest:manifest>"""

    content = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
    <office:body>
        <office:text>
            <text:p>Hello from ODT document</text:p>
            <text:p>This is OpenDocument text for FamilyVault</text:p>
            <text:p>Текст на русском языке в ODT</text:p>
        </office:text>
    </office:body>
</office:document-content>"""

    with zipfile.ZipFile(odt_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        # mimetype должен быть первым и без сжатия
        zf.writestr('mimetype', mimetype, compress_type=zipfile.ZIP_STORED)
        zf.writestr('META-INF/manifest.xml', manifest)
        zf.writestr('content.xml', content)
    
    print(f"Created: {odt_path}")


def create_ods():
    """Создаёт минимальный валидный ODS файл."""
    ods_path = SCRIPT_DIR / "data.ods"
    
    mimetype = "application/vnd.oasis.opendocument.spreadsheet"
    
    manifest = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
    <manifest:file-entry manifest:media-type="application/vnd.oasis.opendocument.spreadsheet" manifest:full-path="/"/>
    <manifest:file-entry manifest:media-type="text/xml" manifest:full-path="content.xml"/>
</manifest:manifest>"""

    content = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0">
    <office:body>
        <office:spreadsheet>
            <table:table table:name="Sheet1">
                <table:table-row>
                    <table:table-cell><text:p>Name</text:p></table:table-cell>
                    <table:table-cell><text:p>Value</text:p></table:table-cell>
                </table:table-row>
                <table:table-row>
                    <table:table-cell><text:p>ODS test item</text:p></table:table-cell>
                    <table:table-cell><text:p>100</text:p></table:table-cell>
                </table:table-row>
                <table:table-row>
                    <table:table-cell><text:p>FamilyVault spreadsheet</text:p></table:table-cell>
                    <table:table-cell><text:p>200</text:p></table:table-cell>
                </table:table-row>
            </table:table>
        </office:spreadsheet>
    </office:body>
</office:document-content>"""

    with zipfile.ZipFile(ods_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('mimetype', mimetype, compress_type=zipfile.ZIP_STORED)
        zf.writestr('META-INF/manifest.xml', manifest)
        zf.writestr('content.xml', content)
    
    print(f"Created: {ods_path}")


def create_corrupted_docx():
    """Создаёт битый DOCX файл (невалидный ZIP)."""
    corrupted_path = SCRIPT_DIR / "corrupted.docx"
    
    # Просто записываем случайные байты
    with open(corrupted_path, 'wb') as f:
        f.write(b'PK\x03\x04corrupted zip data that is not valid')
    
    print(f"Created: {corrupted_path}")


def create_empty_docx():
    """Создаёт пустой DOCX файл (валидный, но без текста)."""
    empty_path = SCRIPT_DIR / "empty.docx"
    
    content_types = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
    <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
    <Default Extension="xml" ContentType="application/xml"/>
    <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"""

    rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
    <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

    # Документ без текстового содержимого
    document = """<?xml version="1.0" encoding="UTF-8"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
    <w:body>
    </w:body>
</w:document>"""

    with zipfile.ZipFile(empty_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('[Content_Types].xml', content_types)
        zf.writestr('_rels/.rels', rels)
        zf.writestr('word/document.xml', document)
    
    print(f"Created: {empty_path}")


def create_minimal_pdf():
    """Создаёт минимальный валидный PDF файл."""
    pdf_path = SCRIPT_DIR / "document.pdf"
    
    # Минимальный валидный PDF с текстом
    pdf_content = b"""%PDF-1.4
1 0 obj
<< /Type /Catalog /Pages 2 0 R >>
endobj

2 0 obj
<< /Type /Pages /Kids [3 0 R] /Count 1 >>
endobj

3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>
endobj

4 0 obj
<< /Length 89 >>
stream
BT
/F1 24 Tf
100 700 Td
(Hello from PDF document) Tj
0 -30 Td
(FamilyVault PDF test) Tj
ET
endstream
endobj

5 0 obj
<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>
endobj

xref
0 6
0000000000 65535 f 
0000000009 00000 n 
0000000058 00000 n 
0000000115 00000 n 
0000000266 00000 n 
0000000406 00000 n 

trailer
<< /Size 6 /Root 1 0 R >>
startxref
478
%%EOF
"""
    
    with open(pdf_path, 'wb') as f:
        f.write(pdf_content)
    
    print(f"Created: {pdf_path}")


def create_empty_pdf():
    """Создаёт пустой PDF файл (валидный, но без текста)."""
    pdf_path = SCRIPT_DIR / "empty.pdf"
    
    # PDF без текстового содержимого
    pdf_content = b"""%PDF-1.4
1 0 obj
<< /Type /Catalog /Pages 2 0 R >>
endobj

2 0 obj
<< /Type /Pages /Kids [3 0 R] /Count 1 >>
endobj

3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>
endobj

4 0 obj
<< /Length 0 >>
stream
endstream
endobj

xref
0 5
0000000000 65535 f 
0000000009 00000 n 
0000000058 00000 n 
0000000115 00000 n 
0000000206 00000 n 

trailer
<< /Size 5 /Root 1 0 R >>
startxref
258
%%EOF
"""
    
    with open(pdf_path, 'wb') as f:
        f.write(pdf_content)
    
    print(f"Created: {pdf_path}")


def create_corrupted_pdf():
    """Создаёт битый PDF файл."""
    pdf_path = SCRIPT_DIR / "corrupted.pdf"
    
    # Неполный PDF
    with open(pdf_path, 'wb') as f:
        f.write(b'%PDF-1.4\nThis is not a valid PDF content')
    
    print(f"Created: {pdf_path}")


def main():
    print("Generating text extraction test fixtures...")
    print(f"Output directory: {SCRIPT_DIR}")
    print()
    
    create_docx()
    create_xlsx()
    create_pptx()
    create_odt()
    create_ods()
    create_minimal_pdf()
    create_empty_pdf()
    create_empty_docx()
    create_corrupted_docx()
    create_corrupted_pdf()
    
    print()
    print("All fixtures generated successfully!")


if __name__ == '__main__':
    main()


