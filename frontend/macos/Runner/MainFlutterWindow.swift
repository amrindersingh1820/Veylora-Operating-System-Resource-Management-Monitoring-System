import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)

    let windowChannel = FlutterMethodChannel(
      name: "com.veylora.window",
      binaryMessenger: flutterViewController.engine.binaryMessenger
    )
    windowChannel.setMethodCallHandler { [weak self] (call, result) in
      guard let self = self else {
        result(nil)
        return
      }
      if call.method == "maximize" || call.method == "fullResolution" {
        DispatchQueue.main.async {
          if let screen = self.screen ?? NSScreen.main {
            let visibleFrame = screen.visibleFrame
            self.setFrame(visibleFrame, display: true, animate: true)
            if !self.isZoomed {
              self.zoom(nil)
            }
          }
        }
        result(true)
      } else if call.method == "toggleFullScreen" {
        DispatchQueue.main.async {
          self.toggleFullScreen(nil)
        }
        result(true)
      } else {
        result(FlutterMethodNotImplemented)
      }
    }

    super.awakeFromNib()
  }
}
