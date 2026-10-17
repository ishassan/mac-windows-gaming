// mkalias <target> <alias path>: write a Finder alias file to the target.
// It uses the bookmark API of Foundation, so Finder does not open or come to
// the front.
import Foundation

let args = CommandLine.arguments
guard args.count == 3 else {
    FileHandle.standardError.write("usage: mkalias <target> <alias path>\n".data(using: .utf8)!)
    exit(2)
}
let target = URL(fileURLWithPath: args[1])
let alias = URL(fileURLWithPath: args[2])
do {
    let data = try target.bookmarkData(options: .suitableForBookmarkFile,
                                       includingResourceValuesForKeys: nil, relativeTo: nil)
    try URL.writeBookmarkData(data, to: alias)
} catch {
    FileHandle.standardError.write("\(error)\n".data(using: .utf8)!)
    exit(1)
}
