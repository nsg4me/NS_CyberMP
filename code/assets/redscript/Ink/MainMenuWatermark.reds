module CyberpunkMP.Ink

// "NS-CyberMP" in the top-right corner of the title screen, so it is obvious the mod is installed and loaded.
@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();

    let root = this.GetRootCompoundWidget();
    if IsDefined(root) {
        let label = new inkText();
        label.SetName(n"NSCyberMPWatermark");
        label.SetText("NS-CyberMP");
        label.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
        label.SetFontStyle(n"Semi-Bold");
        label.SetFontSize(42);
        label.SetTintColor(new HDRColor(1.1761, 0.3809, 0.3476, 1.0)); // the game's UI red
        label.SetOpacity(0.9);
        label.SetAnchor(inkEAnchor.TopRight);
        label.SetAnchorPoint(new Vector2(1.0, 0.0));
        label.SetMargin(new inkMargin(0.0, 40.0, 60.0, 0.0));
        label.Reparent(root);
    }

    return result;
}
