#pragma once

// The map.
//
// One QQuickItem for the whole tree, never one QML Item per file: at several
// thousand leaves that would be several thousand QObjects, bindings and
// batches. Instead the layout is computed in plain C++ and turned into four
// scene graph nodes:
//
//   fills      triangles, vertex-coloured, one quad per cell
//   borders    lines, directory outlines, rebuilt only on relayout
//   highlight  lines, in-flight outlines plus hover and selection
//   labels     one cached texture painted with QPainter
//
// Only `fills` and `highlight` are touched while the build animates, so a
// frame during a live build costs a vertex buffer refill and nothing else.
//
// ZOOM: the layout is recomputed over a rectangle of `zoom` times the item
// size and offset by the pan, rather than the finished picture being scaled up.
// That costs a relayout per zoom step and is worth it: cells get genuinely
// bigger, so labels re-render crisply instead of turning into blurry pixels,
// more of them pass the legibility threshold, and directories that were too
// small to recurse into open up. On a 1600-file project that is the difference
// between a pretty picture and a usable one.

#include "BW/Treemap/squarify.h"
#include "build_model.h"

#include <QColor>
#include <QImage>
#include <QQuickItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace BW::UI
{

class TreemapItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(BW::UI::BuildModel *model READ model WRITE setModel
                   NOTIFY modelChanged)
    /// 0 = compile time heat, 1 = delta against the baseline build.
    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode
                   NOTIFY colorModeChanged)
    /// Drill-down root. Empty means the whole tree.
    Q_PROPERTY(QString focusPath READ focusPath WRITE setFocusPath
                   NOTIFY focusPathChanged)
    /// Order::ByName keeps a file in the same place between builds, which is
    /// what makes two runs comparable. Off trades that for tighter squares.
    Q_PROPERTY(bool stableOrder READ stableOrder WRITE setStableOrder
                   NOTIFY stableOrderChanged)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels
                   NOTIFY showLabelsChanged)
    /// Follows Style.dark. The map has its own palette because it is the one
    /// surface whose colours carry data rather than decoration.
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme
                   NOTIFY darkThemeChanged)

    /// 1.0 fits the whole tree; larger zooms in. Pan is in item pixels and is
    /// clamped so the content cannot be dragged off screen.
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(qreal panX READ panX WRITE setPanX NOTIFY viewChanged)
    Q_PROPERTY(qreal panY READ panY WRITE setPanY NOTIFY viewChanged)
    Q_PROPERTY(qreal minZoom READ minZoom CONSTANT)
    Q_PROPERTY(qreal maxZoom READ maxZoom CONSTANT)
    Q_PROPERTY(bool panning READ panning NOTIFY panningChanged)

    Q_PROPERTY(int hoveredIndex READ hoveredIndex NOTIFY hoverChanged)
    Q_PROPERTY(QString hoveredPath READ hoveredPath NOTIFY hoverChanged)
    Q_PROPERTY(bool hoveredIsDirectory READ hoveredIsDirectory
                   NOTIFY hoverChanged)
    Q_PROPERTY(qint64 hoveredDurationMs READ hoveredDurationMs
                   NOTIFY hoverChanged)
    Q_PROPERTY(qint64 hoveredDeltaMs READ hoveredDeltaMs NOTIFY hoverChanged)
    Q_PROPERTY(int hoveredRank READ hoveredRank NOTIFY hoverChanged)
    Q_PROPERTY(int hoveredLeafCount READ hoveredLeafCount NOTIFY hoverChanged)
    Q_PROPERTY(QPointF hoveredAnchor READ hoveredAnchor NOTIFY hoverChanged)

    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex
                   NOTIFY selectionChanged)
    Q_PROPERTY(int cellCount READ cellCount NOTIFY layoutChanged)
    Q_PROPERTY(int lastLayoutMicros READ lastLayoutMicros NOTIFY layoutChanged)

public:
    explicit TreemapItem(QQuickItem *parent = nullptr);
    ~TreemapItem() override;

    [[nodiscard]]
    auto model() const -> BuildModel *
    {
        return m_model;
    }

    void setModel(BuildModel *model);

    [[nodiscard]]
    auto colorMode() const -> int
    {
        return m_colorMode;
    }

    void setColorMode(int mode);

    [[nodiscard]]
    auto focusPath() const -> QString
    {
        return m_focusPath;
    }

    void setFocusPath(const QString &path);

    [[nodiscard]]
    auto stableOrder() const -> bool
    {
        return m_stableOrder;
    }

    void setStableOrder(bool stable);

    [[nodiscard]]
    auto showLabels() const -> bool
    {
        return m_showLabels;
    }

    void setShowLabels(bool show);

    [[nodiscard]]
    auto darkTheme() const -> bool
    {
        return m_darkTheme;
    }

    void setDarkTheme(bool dark);

    [[nodiscard]]
    auto zoom() const -> qreal
    {
        return m_zoom;
    }

    void setZoom(qreal zoom);

    [[nodiscard]]
    auto panX() const -> qreal
    {
        return m_panX;
    }

    void setPanX(qreal pan);

    [[nodiscard]]
    auto panY() const -> qreal
    {
        return m_panY;
    }

    void setPanY(qreal pan);

    [[nodiscard]]
    auto minZoom() const -> qreal
    {
        return kMinZoom;
    }

    [[nodiscard]]
    auto maxZoom() const -> qreal
    {
        return kMaxZoom;
    }

    [[nodiscard]]
    auto panning() const -> bool
    {
        return m_panning;
    }

    /// Back to showing the whole tree.
    Q_INVOKABLE void fitToView();

    /// Multiplies the zoom, keeping the content under (`x`, `y`) in place.
    /// Pass the item centre to zoom without a focus point.
    Q_INVOKABLE void zoomAt(qreal x, qreal y, qreal factor);

    /// Zooms about the centre of the view.
    Q_INVOKABLE void zoomBy(qreal factor);

    [[nodiscard]]
    auto hoveredIndex() const -> int
    {
        return m_hoveredLeaf;
    }

    [[nodiscard]]
    auto hoveredPath() const -> QString
    {
        return m_hoveredPath;
    }

    [[nodiscard]]
    auto hoveredIsDirectory() const -> bool
    {
        return m_hoveredIsDirectory;
    }

    [[nodiscard]]
    auto hoveredDurationMs() const -> qint64
    {
        return m_hoveredDurationMs;
    }

    [[nodiscard]]
    auto hoveredDeltaMs() const -> qint64
    {
        return m_hoveredDeltaMs;
    }

    [[nodiscard]]
    auto hoveredRank() const -> int
    {
        return m_hoveredRank;
    }

    [[nodiscard]]
    auto hoveredLeafCount() const -> int
    {
        return m_hoveredLeafCount;
    }

    [[nodiscard]]
    auto hoveredAnchor() const -> QPointF
    {
        return m_hoveredAnchor;
    }

    [[nodiscard]]
    auto selectedIndex() const -> int
    {
        return m_selectedLeaf;
    }

    void setSelectedIndex(int index);

    [[nodiscard]]
    auto cellCount() const -> int
    {
        return m_cellCount;
    }

    [[nodiscard]]
    auto lastLayoutMicros() const -> int
    {
        return m_lastLayoutMicros;
    }

    /// Moves the drill-down root one level up. Returns false at the top.
    Q_INVOKABLE bool focusParent();

Q_SIGNALS:
    void modelChanged();
    void colorModeChanged();
    void focusPathChanged();
    void stableOrderChanged();
    void showLabelsChanged();
    void darkThemeChanged();
    void viewChanged();
    void panningChanged();
    void hoverChanged();
    void selectionChanged();
    void layoutChanged();

    /// A leaf was clicked: `index` is the index into BuildModel::targets().
    void leafActivated(int index, const QString &path);
    /// A directory was clicked; the caller usually drills into it.
    void directoryActivated(const QString &path);

protected:
    auto updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
        -> QSGNode * override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
        override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private Q_SLOTS:
    void onTreeChanged();
    void onValuesChanged();
    void onAnimationTick();

private:
    void relayout();
    void updateHover(const QPointF &position);
    void clearHover();
    /// Starts or stops the 60 Hz clock. GUI thread only: updatePaintNode
    /// runs on the render thread and must not touch a QTimer.
    void syncAnimationTimer();
    /// Keeps the pan inside the content, which grows with the zoom.
    void clampPan();

    [[nodiscard]]
    auto rootNode() const -> const Treemap::Node *;

    [[nodiscard]]
    auto cellAt(const QPointF &position) const -> const Treemap::LayoutItem *;

    [[nodiscard]]
    auto colorFor(const Treemap::LayoutItem &item, qint64 now) const
        -> QColor;

    [[nodiscard]]
    auto needsAnimation(qint64 now) const -> bool;

    void renderLabels(qint64 now);

    BuildModel *m_model { nullptr };

    std::vector<Treemap::LayoutItem> m_layout;
    QImage m_labelImage;
    QTimer m_animationTimer;

    QString m_focusPath;
    QString m_hoveredPath;
    QPointF m_hoveredAnchor;

    qreal m_zoom { 1.0 };
    qreal m_panX { 0.0 };
    qreal m_panY { 0.0 };
    /// Press position and pan at press, so a drag is a delta rather than an
    /// accumulation of rounding errors.
    QPointF m_pressPos;
    QPointF m_panAtPress;
    bool m_panning { false };
    bool m_pressWasOnCell { false };

    quint64 m_seenTreeRevision { 0 };
    int m_colorMode { 0 };
    int m_hoveredLeaf { -1 };
    int m_hoveredRank { 0 };
    int m_hoveredLeafCount { 0 };
    qint64 m_hoveredDurationMs { 0 };
    qint64 m_hoveredDeltaMs { 0 };
    int m_selectedLeaf { -1 };
    int m_cellCount { 0 };
    int m_lastLayoutMicros { 0 };
    bool m_hoveredIsDirectory { false };
    bool m_stableOrder { true };
    bool m_showLabels { true };
    bool m_layoutDirty { true };
    bool m_labelsDirty { true };
    bool m_darkTheme { true };

    static constexpr qreal kMinZoom = 1.0;
    static constexpr qreal kMaxZoom = 64.0;
    /// A press has to move this far before it counts as a pan rather than a
    /// click, so selecting a small cell still works.
    static constexpr qreal kDragThreshold = 4.0;
};

}
