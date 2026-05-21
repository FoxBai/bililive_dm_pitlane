import AppKit

private enum ApplicationDelegateHolder {
    static let delegate = AppDelegate()
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
app.delegate = ApplicationDelegateHolder.delegate
app.run()
