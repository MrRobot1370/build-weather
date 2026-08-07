import QtQuick
import BW.UICore

// Themed text primitive. Colour, family, size and weight are bindings on
// `variant` plus the active Style palette, so text re-themes live. Any of
// them can still be overridden per instance.
Text {
    id: root
    renderType: Text.NativeRendering

    enum Variant {
        Body, Secondary, Muted, Faint, Heading, SubHeading, Title,
        Mono, Metric, Eyebrow
    }
    property int variant: TextBW.Body

    color: {
        switch (variant) {
        case TextBW.Secondary: return Style.textSecondary
        case TextBW.Muted:     return Style.textMuted
        case TextBW.Faint:
        case TextBW.Eyebrow:   return Style.textFaint
        case TextBW.Metric:    return Style.textPrimary
        default:               return Style.textPrimary
        }
    }
    font.family: (variant === TextBW.Mono || variant === TextBW.Metric)
                 ? Style.fontFamilyMono : Style.fontFamily
    font.pixelSize: {
        switch (variant) {
        case TextBW.Muted:      return Style.fontSizeS
        case TextBW.Faint:      return Style.fontSizeXS
        case TextBW.Eyebrow:    return Style.fontSizeXXS
        case TextBW.SubHeading: return Style.fontSizeL
        case TextBW.Heading:    return Style.fontSizeXL
        case TextBW.Metric:     return Style.fontSizeXL
        case TextBW.Title:      return Style.fontSizeXXL
        default:                return Style.fontSize
        }
    }
    font.weight: {
        switch (variant) {
        case TextBW.Heading:    return Font.DemiBold
        case TextBW.SubHeading: return Font.Medium
        case TextBW.Title:      return Font.Bold
        case TextBW.Metric:     return Font.Medium
        case TextBW.Eyebrow:    return Font.DemiBold
        default:                return Font.Normal
        }
    }
    font.letterSpacing: variant === TextBW.Eyebrow ? Style.eyebrowSpacing : 0
    font.capitalization: variant === TextBW.Eyebrow ? Font.AllUppercase
                                                    : Font.MixedCase
}
