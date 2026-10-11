import AppKit
import Foundation
import PDFKit

enum ExtractionError: Error, CustomStringConvertible {
    case invalid(String)
    var description: String {
        switch self { case .invalid(let message): return message }
    }
}

func writeJSON(_ value: Any, to file: String) throws {
    var data = try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys])
    data.append(10)
    try data.write(to: URL(fileURLWithPath: file), options: .withoutOverwriting)
}

func extract(_ document: PDFDocument, to file: String) throws {
    var pages: [[String: Any]] = []
    for i in 0..<document.pageCount {
        try autoreleasepool {
            guard let page = document.page(at: i) else {
                throw ExtractionError.invalid("Cannot access PDF page \(i + 1)")
            }
            let bounds = page.bounds(for: .mediaBox)
            let text = page.string ?? ""
            let footer = CGRect(x: bounds.minX, y: bounds.minY,
                                width: bounds.width, height: bounds.height * 0.06)
            pages.append([
                "pdf_page": i + 1,
                "pdf_label": page.label as Any? ?? NSNull(),
                "text": text,
                "text_status": text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? "no_text" : "text",
                "footer_text": page.selection(for: footer)?.string as Any? ?? NSNull(),
                "width_points": bounds.width, "height_points": bounds.height,
                "rotation": page.rotation,
            ])
        }
    }
    var outline: [[String: Any]] = []
    if let root = document.outlineRoot {
        var pending: [(PDFOutline, Int)] = []
        for i in (0..<root.numberOfChildren).reversed() {
            if let child = root.child(at: i) { pending.append((child, 1)) }
        }
        while let (item, depth) = pending.popLast() {
            guard outline.count < 100000 else {
                throw ExtractionError.invalid("PDF outline exceeds 100000 entries")
            }
            var destination: Any = NSNull()
            if let page = item.destination?.page {
                let index = document.index(for: page)
                guard index >= 0 && index < document.pageCount else {
                    throw ExtractionError.invalid("Outline destination outside document")
                }
                destination = index + 1
            }
            outline.append(["title": item.label as Any? ?? NSNull(),
                            "depth": depth, "pdf_page": destination])
            for i in (0..<item.numberOfChildren).reversed() {
                if let child = item.child(at: i) { pending.append((child, depth + 1)) }
            }
        }
    }
    let iso = ISO8601DateFormatter()
    var attributes: [String: String] = [:]
    for (key, value) in document.documentAttributes ?? [:] {
        if let date = value as? Date { attributes[String(describing: key)] = iso.string(from: date) }
        else { attributes[String(describing: key)] = String(describing: value) }
    }
    let bundle = Bundle(for: PDFDocument.self)
    try writeJSON([
        "schema": 1, "pages": pages, "outline": outline, "attributes": attributes,
        "extractor": [
            "name": "PDFKit", "interface_version": 1,
            "framework_version": bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as Any? ?? NSNull(),
            "framework_build": bundle.object(forInfoDictionaryKey: "CFBundleVersion") as Any? ?? NSNull(),
            "os_version": ProcessInfo.processInfo.operatingSystemVersionString,
            "locale": Locale.current.identifier,
            "text_method": "PDFPage.string; UTF-8; no OCR or table reconstruction",
            "footer_region": "bottom 6% of mediaBox",
        ],
    ], to: file)
}

func run() throws {
    let args = Array(CommandLine.arguments.dropFirst())
    guard (args.count == 3 && args[0] == "extract") ||
          (args.count == 4 && args[0] == "render") else {
        throw ExtractionError.invalid("Usage: pdfkit-datasheet extract INPUT.pdf NEW.json | render INPUT.pdf PDF_PAGE NEW.png")
    }
    let data = try Data(contentsOf: URL(fileURLWithPath: args[1]))
    guard let document = PDFDocument(data: data), !document.isLocked, document.pageCount > 0 else {
        throw ExtractionError.invalid("Cannot read unlocked, nonempty PDF: \(args[1])")
    }
    if args[0] == "extract" {
        try extract(document, to: args[2])
    } else {
        guard let number = Int(args[2]), number > 0, number <= document.pageCount,
              let page = document.page(at: number - 1) else {
            throw ExtractionError.invalid("PDF page must be in 1...\(document.pageCount)")
        }
        let bounds = page.bounds(for: .mediaBox)
        guard bounds.width > 0 && bounds.height > 0 else {
            throw ExtractionError.invalid("Invalid PDF page dimensions")
        }
        let image = page.thumbnail(of: NSSize(width: 1800, height: 1800 * bounds.height / bounds.width),
                                   for: .mediaBox)
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let png = bitmap.representation(using: .png, properties: [:]) else {
            throw ExtractionError.invalid("Cannot render PDF page \(number)")
        }
        try png.write(to: URL(fileURLWithPath: args[3]), options: .withoutOverwriting)
    }
}

do {
    try run()
} catch {
    FileHandle.standardError.write(Data("pdfkit-datasheet: \(error)\n".utf8))
    exit(1)
}
