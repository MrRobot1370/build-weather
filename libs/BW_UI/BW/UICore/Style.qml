pragma Singleton
import QtQuick

QtObject {
    // 0 = follow system, 1 = light, 2 = dark
    property int themeMode: 2

    readonly property bool systemDark: {
        if (Qt.styleHints && Qt.styleHints.colorScheme !== undefined)
            return Qt.styleHints.colorScheme === Qt.ColorScheme.Dark
        return true
    }
    readonly property bool dark: themeMode === 1 ? false
                               : themeMode === 2 ? true
                               : systemDark

    // Surfaces
    readonly property color bg0:        dark ? "#0A0C10" : "#F3F5F9"   // window
    readonly property color bg1:        dark ? "#12161D" : "#FFFFFF"   // panes, dialogs
    readonly property color bg2:        dark ? "#171C24" : "#EDF0F5"   // raised controls
    readonly property color bg3:        dark ? "#212832" : "#DEE4ED"   // hover, pressed
    readonly property color bgTitle:    dark ? "#07090C" : "#E7EBF2"   // title bar
    readonly property color bgRail:     dark ? "#0D1016" : "#E9EDF4"   // tab rail, status bar
    readonly property color bgPanel:    dark ? "#0F131A" : "#FAFBFD"   // side panels
    readonly property color bgWell:     dark ? "#0B0E13" : "#E8ECF3"   // input wells, readouts
    readonly property color bgMap:      dark ? "#06080B" : "#E7E9ED"   // map backdrop

    readonly property color border:       dark ? "#222933" : "#D3DAE5"
    readonly property color borderSubtle: dark ? "#1A2028" : "#E1E6EE"
    readonly property color borderInput:  dark ? "#232B36" : "#C7D0DD"
    readonly property color borderHi:     dark ? "#35414F" : "#A7B1C1"
    readonly property color overlay:      dark ? Qt.rgba(0.02, 0.03, 0.04, 0.82)
                                              : Qt.rgba(0.22, 0.25, 0.30, 0.55)

    // Text
    readonly property color textPrimary:   dark ? "#E8EDF4" : "#18212E"
    readonly property color textBody:      dark ? "#C2CBD8" : "#394454"
    readonly property color textSecondary: dark ? "#98A3B2" : "#556078"
    readonly property color textMuted:     dark ? "#78838F" : "#8992A4"
    readonly property color textFaint:     dark ? "#5A6473" : "#A3ACBB"
    readonly property color textInverse:   dark ? "#0A0C10" : "#FFFFFF"

    // Brand and semantic
    readonly property color accent:      dark ? "#F2A03D" : "#C4700A"
    readonly property color accentHover: dark ? "#FFB65C" : "#DA8214"
    readonly property color accentDown:  dark ? "#D2842A" : "#A55C05"
    readonly property color accentText:  "#12100C"
    readonly property color accentSoft:  Qt.alpha(accent, 0.14)
    readonly property color accentLine:  Qt.alpha(accent, 0.50)

    readonly property color success: dark ? "#3FB97F" : "#1F9D5C"
    readonly property color warning: dark ? "#E8B33C" : "#B0780B"
    readonly property color danger:  dark ? "#E2545B" : "#C4323A"
    readonly property color info:    dark ? "#5AB6DE" : "#0E7FA8"

    // regression / improvement in the comparison view
    readonly property color worse:  danger
    readonly property color better: success

    // Heat ramp. Mirrored in color_ramp.h, which is what the scene graph
    // samples; these exist so the legend cannot drift from the map.
    readonly property var heatStopsDark: [
        "#10141C", "#241A2E", "#46203C", "#6E2436", "#9B3527",
        "#C55416", "#E27B0A", "#F3A81E", "#FBD35C", "#FDF3B8"
    ]
    readonly property var heatStopsLight: [
        "#FDF9E8", "#F9E7B4", "#F4CD7B", "#EFAB4D", "#E6852E",
        "#D3621F", "#B44119", "#8F2D19", "#6B1E18", "#4A1311"
    ]
    readonly property var heatStops: dark ? heatStopsDark : heatStopsLight

    function heat(t) {
        var clamped = t < 0 ? 0 : (t > 1 ? 1 : t)
        var scaled = clamped * (heatStops.length - 1)
        var i = Math.floor(scaled)
        if (i >= heatStops.length - 1)
            return heatStops[heatStops.length - 1]
        var f = scaled - i
        var a = Qt.color(heatStops[i])
        var b = Qt.color(heatStops[i + 1])
        return Qt.rgba(a.r + (b.r - a.r) * f,
                       a.g + (b.g - a.g) * f,
                       a.b + (b.b - a.b) * f, 1.0)
    }

    // Spacing and sizing
    readonly property int space0: 4
    readonly property int space1: 8
    readonly property int space2: 12
    readonly property int space3: 16
    readonly property int space4: 24
    readonly property int space5: 32
    readonly property int radius:  6
    readonly property int radiusS: 4
    readonly property int radiusM: 8
    readonly property int radiusL: 12

    // Type
    readonly property string fontFamily: "Segoe UI"
    readonly property string fontFamilyMono: "Cascadia Mono"
    readonly property int fontSizeXXS: 10
    readonly property int fontSizeXS:  11
    readonly property int fontSizeS:   12
    readonly property int fontSize:    13
    readonly property int fontSizeL:   15
    readonly property int fontSizeXL:  18
    readonly property int fontSizeXXL: 24
    readonly property real eyebrowSpacing: 0.9

    // Animation
    readonly property int durFast: 120
    readonly property int durMed:  200
    readonly property int durSlow: 320
    readonly property int durSettle: 400   // in-flight to final colour
}
