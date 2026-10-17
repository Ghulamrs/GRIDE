// swift make-icon.swift DIR && iconutil -c icns DIR -o RIDE.icns (DIR ending .iconset)
import AppKit
// RIDE's icon for macOS: ride.ico's artwork (256 units) drawn at any size,
// inset on Apple's 1024 grid (824 of art, 100 margin) with the usual shadow.
func render(_ px: Int, _ path: String) {
  let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
      samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
  NSGraphicsContext.saveGraphicsState()
  NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
  let ctx = NSGraphicsContext.current!.cgContext
  let s = CGFloat(px) / 1024
  ctx.scaleBy(x: s, y: s)
  ctx.translateBy(x: 100, y: 100 + 824); ctx.scaleBy(x: 824/256, y: -824/256)   // ico units, y down
  let rect = CGRect(x: 0, y: 0, width: 256, height: 256)
  let shape = CGPath(roundedRect: rect, cornerWidth: 50, cornerHeight: 50, transform: nil)
  ctx.saveGState()
  ctx.setShadow(offset: CGSize(width: 0, height: -3), blur: 8, color: NSColor(white: 0, alpha: 0.35).cgColor)
  ctx.addPath(shape); ctx.setFillColor(NSColor(red: 41/255, green: 107/255, blue: 194/255, alpha: 1).cgColor); ctx.fillPath()
  ctx.restoreGState()
  ctx.saveGState(); ctx.addPath(shape); ctx.clip()
  let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: [
      NSColor(red: 52/255, green: 132/255, blue: 228/255, alpha: 1).cgColor,
      NSColor(red: 17/255, green: 54/255, blue: 120/255, alpha: 1).cgColor] as CFArray, locations: [0, 1])!
  ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 256, y: 256), options: [])
  ctx.restoreGState()
  // the light rim along the top edge
  ctx.saveGState(); ctx.addPath(CGPath(roundedRect: rect.insetBy(dx: 1, dy: 1), cornerWidth: 49, cornerHeight: 49, transform: nil))
  ctx.setStrokeColor(NSColor(red: 120/255, green: 170/255, blue: 235/255, alpha: 0.55).cgColor); ctx.setLineWidth(2); ctx.strokePath(); ctx.restoreGState()
  // the R, fitted to the ico's box 67..179 x 63..192
  let font = NSFont.systemFont(ofSize: 100, weight: .heavy)
  let line = CTLineCreateWithAttributedString(NSAttributedString(string: "R", attributes: [.font: font]))
  let run = (CTLineGetGlyphRuns(line) as! [CTRun])[0]
  var glyph = CGGlyph(); CTRunGetGlyphs(run, CFRange(location: 0, length: 1), &glyph)
  let gp = CTFontCreatePathForGlyph(font, glyph, nil)!
  let b = gp.boundingBoxOfPath
  var t = CGAffineTransform(translationX: 67, y: 192).scaledBy(x: 112 / b.width, y: -129 / b.height).translatedBy(x: -b.minX, y: -b.minY)
  ctx.addPath(gp.copy(using: &t)!); ctx.setFillColor(.white); ctx.fillPath()
  // the cursor
  ctx.addPath(CGPath(roundedRect: CGRect(x: 179, y: 169, width: 33, height: 50), cornerWidth: 5, cornerHeight: 5, transform: nil))
  ctx.setFillColor(NSColor(red: 1, green: 195/255, blue: 65/255, alpha: 1).cgColor); ctx.fillPath()
  NSGraphicsContext.restoreGraphicsState()
  try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: path))
}
let dir = CommandLine.arguments[1]
for (n, px) in [("16x16",16),("16x16@2x",32),("32x32",32),("32x32@2x",64),("128x128",128),("128x128@2x",256),
                ("256x256",256),("256x256@2x",512),("512x512",512),("512x512@2x",1024)] {
  render(px, "\(dir)/icon_\(n).png")
}
