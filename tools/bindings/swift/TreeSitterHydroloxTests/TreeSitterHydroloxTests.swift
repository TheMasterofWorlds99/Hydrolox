import XCTest
import SwiftTreeSitter
import TreeSitterHydrolox

final class TreeSitterHydroloxTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_hydrolox())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Hydrolox grammar")
    }
}
