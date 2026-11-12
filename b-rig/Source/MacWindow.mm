// Barra del titolo di macOS dello stesso colore dell'app e senza scritta (restano i tre pulsanti della finestra).
#include <juce_gui_basics/juce_gui_basics.h>
#import <Cocoa/Cocoa.h>

void rigStyleTitleBar (juce::Component& window);
void rigStyleTitleBar (juce::Component& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr) return;
    NSView* view = (NSView*) peer->getNativeHandle();
    NSWindow* w = [view window];
    if (w == nil) return;
    w.titlebarAppearsTransparent = YES;
    w.titleVisibility = NSWindowTitleHidden;
    w.appearance = [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua];
    w.backgroundColor = [NSColor colorWithSRGBRed: 0x12 / 255.0 green: 0x12 / 255.0 blue: 0x12 / 255.0 alpha: 1.0];   // ui::background
}
