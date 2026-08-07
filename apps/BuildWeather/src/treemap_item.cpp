#include "treemap_item.h"

#include "color_ramp.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>

namespace BW::UI
{

namespace {

constexpr int kSettleMs = 400; ///< matches Style.durSettle
constexpr int kPulsePeriodMs = 900;
constexpr double kMinLabelWidth = 46.0;
constexpr double kMinLabelHeight = 13.0;

auto nowMs() -> qint64
{
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

void appendQuad(
    QSGGeometry::ColoredPoint2D *&cursor,
    const Treemap::Rect &rect,
    const QColor &color)
{
    const auto r = static_cast<uchar>(color.red());
    const auto g = static_cast<uchar>(color.green());
    const auto b = static_cast<uchar>(color.blue());
    const auto a = static_cast<uchar>(color.alpha());

    const auto x0 = static_cast<float>(rect.x);
    const auto y0 = static_cast<float>(rect.y);
    const auto x1 = static_cast<float>(rect.x + rect.w);
    const auto y1 = static_cast<float>(rect.y + rect.h);

    cursor[0].set(x0, y0, r, g, b, a);
    cursor[1].set(x1, y0, r, g, b, a);
    cursor[2].set(x0, y1, r, g, b, a);
    cursor[3].set(x1, y0, r, g, b, a);
    cursor[4].set(x1, y1, r, g, b, a);
    cursor[5].set(x0, y1, r, g, b, a);
    cursor += 6;
}

void appendOutline(
    QSGGeometry::ColoredPoint2D *&cursor,
    const Treemap::Rect &rect,
    const QColor &color,
    float inset = 0.5f)
{
    const auto r = static_cast<uchar>(color.red());
    const auto g = static_cast<uchar>(color.green());
    const auto b = static_cast<uchar>(color.blue());
    const auto a = static_cast<uchar>(color.alpha());

    const float x0 = static_cast<float>(rect.x) + inset;
    const float y0 = static_cast<float>(rect.y) + inset;
    const float x1 = static_cast<float>(rect.x + rect.w) - inset;
    const float y1 = static_cast<float>(rect.y + rect.h) - inset;

    const auto line = [&](float ax, float ay, float bx, float by) {
        cursor[0].set(ax, ay, r, g, b, a);
        cursor[1].set(bx, by, r, g, b, a);
        cursor += 2;
    };
    line(x0, y0, x1, y0);
    line(x1, y0, x1, y1);
    line(x1, y1, x0, y1);
    line(x0, y1, x0, y0);
}

/// Two concentric one-pixel rings. The scene graph does not honour line
/// widths above 1 on every graphics API (D3D11 among them), so a thick
/// outline has to be drawn rather than asked for.
void appendDoubleOutline(
    QSGGeometry::ColoredPoint2D *&cursor,
    const Treemap::Rect &rect,
    const QColor &color)
{
    appendOutline(cursor, rect, color, 0.5f);
    appendOutline(cursor, rect, color, 1.5f);
}

constexpr int kOutlineVertices = 8;
constexpr int kDoubleOutlineVertices = 2 * kOutlineVertices;

auto mix(const QColor &a, const QColor &b, double t) -> QColor
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * t,
        a.greenF() + (b.greenF() - a.greenF()) * t,
        a.blueF() + (b.blueF() - a.blueF()) * t);
}

}

TreemapItem::TreemapItem(QQuickItem *parent)
    : QQuickItem { parent }
{
    setFlag(ItemHasContents, true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setClip(true);

    // One timer for every animation. It only runs while something is
    // actually moving, so an idle post-mortem map costs zero frames.
    m_animationTimer.setInterval(16);
    connect(
        &m_animationTimer,
        &QTimer::timeout,
        this,
        &TreemapItem::onAnimationTick);
}

TreemapItem::~TreemapItem() = default;

void TreemapItem::setModel(BuildModel *model)
{
    if (m_model == model) {
        return;
    }
    if (m_model != nullptr) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model != nullptr) {
        connect(
            m_model,
            &BuildModel::treeChanged,
            this,
            &TreemapItem::onTreeChanged);
        connect(
            m_model,
            &BuildModel::valuesChanged,
            this,
            &TreemapItem::onValuesChanged);
    }
    m_layoutDirty = true;
    m_labelsDirty = true;
    Q_EMIT modelChanged();
    update();
}

void TreemapItem::setColorMode(int mode)
{
    if (m_colorMode == mode) {
        return;
    }
    m_colorMode = mode;
    Q_EMIT colorModeChanged();
    update();
}

void TreemapItem::setFocusPath(const QString &path)
{
    if (m_focusPath == path) {
        return;
    }
    m_focusPath = path;
    m_layoutDirty = true;
    m_labelsDirty = true;
    clearHover();
    Q_EMIT focusPathChanged();
    update();
}

void TreemapItem::setStableOrder(bool stable)
{
    if (m_stableOrder == stable) {
        return;
    }
    m_stableOrder = stable;
    m_layoutDirty = true;
    m_labelsDirty = true;
    Q_EMIT stableOrderChanged();
    update();
}

void TreemapItem::setShowLabels(bool show)
{
    if (m_showLabels == show) {
        return;
    }
    m_showLabels = show;
    m_labelsDirty = true;
    Q_EMIT showLabelsChanged();
    update();
}

void TreemapItem::setSelectedIndex(int index)
{
    if (m_selectedLeaf == index) {
        return;
    }
    m_selectedLeaf = index;
    Q_EMIT selectionChanged();
    update();
}

auto TreemapItem::focusParent() -> bool
{
    if (m_focusPath.isEmpty()) {
        return false;
    }
    const int slash = m_focusPath.lastIndexOf('/');
    setFocusPath(slash <= 0 ? QString {} : m_focusPath.left(slash));
    return true;
}

void TreemapItem::onTreeChanged()
{
    m_layoutDirty = true;
    m_labelsDirty = true;
    m_selectedLeaf = -1;
    clearHover();
    // A new tree can arrive with leaves still settling (endLive rebuilds the
    // model). Without this the clock never starts and the map freezes on
    // whatever frame the change happened to produce.
    syncAnimationTimer();
    update();
}

void TreemapItem::onValuesChanged()
{
    syncAnimationTimer();
    update();
}

void TreemapItem::onAnimationTick()
{
    syncAnimationTimer();
    update();
}

void TreemapItem::syncAnimationTimer()
{
    const bool animating = needsAnimation(nowMs());
    if (animating && !m_animationTimer.isActive()) {
        m_animationTimer.start();
    }
    else if (!animating && m_animationTimer.isActive()) {
        m_animationTimer.stop();
    }
}

void TreemapItem::geometryChange(
    const QRectF &newGeometry,
    const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        m_layoutDirty = true;
        m_labelsDirty = true;
        update();
    }
}

auto TreemapItem::rootNode() const -> const Treemap::Node *
{
    if (m_model == nullptr) {
        return nullptr;
    }
    const Treemap::Node *root = &m_model->tree();
    if (m_focusPath.isEmpty()) {
        return root;
    }

    const Treemap::Node *found = nullptr;
    const std::string wanted = m_focusPath.toStdString();
    Treemap::walk(*root, [&](const Treemap::Node &node, int) {
        if (found == nullptr && node.path == wanted) {
            found = &node;
        }
    });
    return found != nullptr ? found : root;
}

void TreemapItem::relayout()
{
    m_layout.clear();
    m_cellCount = 0;
    m_layoutDirty = false;

    const Treemap::Node *root = rootNode();
    if (root == nullptr || width() <= 2.0 || height() <= 2.0) {
        Q_EMIT layoutChanged();
        return;
    }

    Treemap::LayoutOptions options;
    options.order = m_stableOrder ? Treemap::Order::ByName
                                  : Treemap::Order::ByValueDesc;
    options.padding = 2.0;
    options.headerHeight = m_showLabels ? 14.0 : 3.0;
    options.minRecurseSize = 26.0;

    QElapsedTimer timer;
    timer.start();
    Treemap::layout(
        *root,
        { 0.0, 0.0, width(), height() },
        options,
        m_layout);
    m_lastLayoutMicros = static_cast<int>(timer.nsecsElapsed() / 1000);

    for (const auto &item : m_layout) {
        if (item.isCell) {
            ++m_cellCount;
        }
    }
    m_labelsDirty = true;
    Q_EMIT layoutChanged();
}

auto TreemapItem::colorFor(const Treemap::LayoutItem &item, qint64 now) const
    -> QColor
{
    static const QColor kPending { 22, 26, 33 };
    static const QColor kUnknown { 30, 34, 42 };
    static const QColor kFlash { 255, 250, 228 };

    if (m_model == nullptr || item.node->leafIndex < 0) {
        return kUnknown;
    }
    const auto index = static_cast<std::size_t>(item.node->leafIndex);
    if (index >= m_model->leaves().size()) {
        return kUnknown;
    }
    const LeafVisual &leaf = m_model->leaves()[index];

    QColor base;
    if (m_colorMode == 1) {
        const double scale = std::max(1.0, m_model->scaleMaxDeltaMs());
        base = toQColor(
            delta(static_cast<float>(
                static_cast<double>(leaf.deltaMs) / scale)));
    }
    else {
        base = toQColor(heat(heatPosition(
            static_cast<double>(leaf.durationMs),
            m_model->scaleMaxMs())));
    }

    switch (leaf.state) {
    case LeafState::Pending :
        return kPending;
    case LeafState::Running : {
        // Subtle pulse: the outline carries the "in flight" signal, the fill
        // just breathes so a wall of active files reads as shimmer.
        const double phase = static_cast<double>(now % kPulsePeriodMs)
            / kPulsePeriodMs;
        const double amount
            = 0.35 + 0.25 * std::sin(phase * 2.0 * M_PI);
        return mix(base, kFlash, amount);
    }
    case LeafState::Finished : {
        const qint64 age = now - leaf.changedAtMs;
        if (age >= kSettleMs) {
            return base;
        }
        // Ease out over 400 ms from the completion flash to the final colour.
        const double t = static_cast<double>(age) / kSettleMs;
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        return mix(kFlash, base, eased);
    }
    }
    return base;
}

auto TreemapItem::needsAnimation(qint64 now) const -> bool
{
    if (m_model == nullptr) {
        return false;
    }
    for (const auto &leaf : m_model->leaves()) {
        if (leaf.state == LeafState::Running) {
            return true;
        }
        if (leaf.state == LeafState::Finished
            && now - leaf.changedAtMs < kSettleMs) {
            return true;
        }
    }
    return false;
}

void TreemapItem::renderLabels()
{
    m_labelsDirty = false;

    const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio()
                                          : 1.0;
    const QSize pixelSize {
        std::max(1, static_cast<int>(std::ceil(width() * dpr))),
        std::max(1, static_cast<int>(std::ceil(height() * dpr)))
    };
    if (m_labelImage.size() != pixelSize) {
        m_labelImage = QImage { pixelSize, QImage::Format_RGBA8888_Premultiplied };
    }
    m_labelImage.setDevicePixelRatio(dpr);
    m_labelImage.fill(Qt::transparent);
    if (!m_showLabels || m_layout.empty()) {
        return;
    }

    QPainter painter { &m_labelImage };
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont directoryFont = QGuiApplication::font();
    directoryFont.setPixelSize(10);
    directoryFont.setWeight(QFont::DemiBold);
    QFont leafFont = QGuiApplication::font();
    leafFont.setPixelSize(10);

    for (const auto &item : m_layout) {
        const Treemap::Rect &rect = item.rect;
        if (rect.w < kMinLabelWidth || rect.h < kMinLabelHeight) {
            continue;
        }

        if (!item.isCell) {
            // Directory header band.
            painter.setFont(directoryFont);
            painter.setPen(QColor { 168, 180, 196 });
            const QRectF band {
                rect.x + 4.0,
                rect.y + 1.0,
                rect.w - 8.0,
                12.0
            };
            const QString text = painter.fontMetrics().elidedText(
                QString::fromStdString(item.node->name),
                Qt::ElideLeft,
                static_cast<int>(band.width()));
            painter.drawText(band, Qt::AlignVCenter | Qt::AlignLeft, text);
            continue;
        }

        // Leaf label, only when the cell is genuinely big enough to read.
        if (rect.w < 70.0 || rect.h < 20.0) {
            continue;
        }
        painter.setFont(leafFont);
        painter.setPen(QColor { 12, 14, 18, 210 });
        const QRectF box {
            rect.x + 4.0,
            rect.y + 3.0,
            rect.w - 8.0,
            rect.h - 6.0
        };
        const QString text = painter.fontMetrics().elidedText(
            QString::fromStdString(item.node->name),
            Qt::ElideMiddle,
            static_cast<int>(box.width()));
        painter.drawText(box, Qt::AlignTop | Qt::AlignLeft, text);
    }
}

auto TreemapItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
    -> QSGNode *
{
    if (m_model == nullptr || width() <= 0.0 || height() <= 0.0) {
        delete old;
        return nullptr;
    }
    if (m_layoutDirty) {
        relayout();
    }
    if (m_layout.empty()) {
        delete old;
        return nullptr;
    }

    auto *root = static_cast<QSGNode *>(old);
    QSGGeometryNode *fills = nullptr;
    QSGGeometryNode *borders = nullptr;
    QSGGeometryNode *highlight = nullptr;
    QSGSimpleTextureNode *labels = nullptr;

    if (root == nullptr) {
        root = new QSGNode;

        const auto makeGeometryNode = [&](QSGGeometry::DrawingMode mode,
                                          float lineWidth) {
            auto *node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(
                QSGGeometry::defaultAttributes_ColoredPoint2D(),
                0);
            geometry->setDrawingMode(mode);
            geometry->setLineWidth(lineWidth);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry, true);
            auto *material = new QSGVertexColorMaterial;
            node->setMaterial(material);
            node->setFlag(QSGNode::OwnsMaterial, true);
            return node;
        };

        fills = makeGeometryNode(QSGGeometry::DrawTriangles, 1.0f);
        borders = makeGeometryNode(QSGGeometry::DrawLines, 1.0f);
        highlight = makeGeometryNode(QSGGeometry::DrawLines, 1.0f);
        labels = new QSGSimpleTextureNode;
        labels->setOwnsTexture(true);
        labels->setFiltering(QSGTexture::Nearest);

        root->appendChildNode(fills);
        root->appendChildNode(borders);
        root->appendChildNode(highlight);
        root->appendChildNode(labels);
    }
    else {
        fills = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        borders = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
        highlight = static_cast<QSGGeometryNode *>(root->childAtIndex(2));
        labels = static_cast<QSGSimpleTextureNode *>(root->childAtIndex(3));
    }

    const qint64 now = nowMs();

    // ---- fills ----------------------------------------------------------
    int cellCount = 0;
    int directoryCount = 0;
    for (const auto &item : m_layout) {
        if (item.isCell) {
            ++cellCount;
        }
        else if (item.depth > 0) {
            ++directoryCount;
        }
    }

    QSGGeometry *fillGeometry = fills->geometry();
    const int fillVertices = cellCount * 6;
    if (fillGeometry->vertexCount() != fillVertices) {
        fillGeometry->allocate(fillVertices);
    }
    auto *fillCursor = fillGeometry->vertexDataAsColoredPoint2D();
    for (const auto &item : m_layout) {
        if (item.isCell) {
            appendQuad(fillCursor, item.rect, colorFor(item, now));
        }
    }
    fills->markDirty(QSGNode::DirtyGeometry);

    // ---- directory borders ----------------------------------------------
    QSGGeometry *borderGeometry = borders->geometry();
    const int borderVertices = directoryCount * kOutlineVertices;
    if (borderGeometry->vertexCount() != borderVertices) {
        borderGeometry->allocate(borderVertices);
    }
    auto *borderCursor = borderGeometry->vertexDataAsColoredPoint2D();
    for (const auto &item : m_layout) {
        if (item.isCell || item.depth == 0) {
            continue;
        }
        // Shallower directories get a brighter frame, so the nesting reads
        // as a hierarchy rather than as noise.
        const int alpha = std::max(40, 190 - 34 * (item.depth - 1));
        appendOutline(
            borderCursor,
            item.rect,
            QColor { 128, 146, 170, alpha });
    }
    borders->markDirty(QSGNode::DirtyGeometry);

    // ---- in-flight, hover and selection ----------------------------------
    int highlightCount = 0;
    for (const auto &item : m_layout) {
        if (!item.isCell || item.node->leafIndex < 0) {
            continue;
        }
        const auto index = static_cast<std::size_t>(item.node->leafIndex);
        if (index < m_model->leaves().size()
            && m_model->leaves()[index].state == LeafState::Running) {
            ++highlightCount;
        }
    }
    if (m_hoveredLeaf >= 0 || m_hoveredIsDirectory) {
        ++highlightCount;
    }
    if (m_selectedLeaf >= 0) {
        ++highlightCount;
    }

    QSGGeometry *highlightGeometry = highlight->geometry();
    const int highlightVertices = highlightCount * kDoubleOutlineVertices;
    if (highlightGeometry->vertexCount() != highlightVertices) {
        highlightGeometry->allocate(highlightVertices);
    }
    if (highlightVertices > 0) {
        auto *cursor = highlightGeometry->vertexDataAsColoredPoint2D();
        const double phase
            = static_cast<double>(now % kPulsePeriodMs) / kPulsePeriodMs;
        const int pulseAlpha = static_cast<int>(
            180.0 + 75.0 * std::sin(phase * 2.0 * M_PI));

        for (const auto &item : m_layout) {
            if (!item.isCell || item.node->leafIndex < 0) {
                continue;
            }
            const auto index = static_cast<std::size_t>(item.node->leafIndex);
            if (index < m_model->leaves().size()
                && m_model->leaves()[index].state == LeafState::Running) {
                appendDoubleOutline(
                    cursor,
                    item.rect,
                    QColor { 255, 244, 214, std::clamp(pulseAlpha, 0, 255) });
            }
        }

        const auto outlineOf = [&](const QString &path, const QColor &color) {
            for (const auto &item : m_layout) {
                if (QString::fromStdString(item.node->path) == path) {
                    appendDoubleOutline(cursor, item.rect, color);
                    return;
                }
            }
            // Nothing matched: emit a degenerate outline so the vertex
            // count still lines up with what we allocated.
            appendDoubleOutline(cursor, Treemap::Rect {}, QColor { 0, 0, 0, 0 });
        };

        if (m_hoveredLeaf >= 0 || m_hoveredIsDirectory) {
            outlineOf(m_hoveredPath, QColor { 236, 242, 250, 235 });
        }
        if (m_selectedLeaf >= 0
            && static_cast<std::size_t>(m_selectedLeaf)
                < m_model->targets().size()) {
            outlineOf(
                QString::fromStdString(
                    m_model->targets()[static_cast<std::size_t>(
                                           m_selectedLeaf)]
                        .treePath),
                QColor { 242, 160, 61, 255 });
        }
    }
    highlight->markDirty(QSGNode::DirtyGeometry);

    // ---- labels -----------------------------------------------------------
    if (m_labelsDirty) {
        renderLabels();
        if (window() != nullptr) {
            // The label layer is drawn on top of the cells and is almost
            // entirely transparent. Marking it opaque paints the whole map
            // black.
            labels->setTexture(window()->createTextureFromImage(
                m_labelImage,
                QQuickWindow::TextureHasAlphaChannel));
        }
    }
    labels->setRect(0.0, 0.0, width(), height());
    labels->markDirty(QSGNode::DirtyMaterial);

    // NOTE: the animation timer is NOT touched here. This function runs on
    // the render thread, and QTimer must be started and stopped from the
    // thread that owns it; syncAnimationTimer() does it from the GUI thread.
    return root;
}

// --- hit testing ---------------------------------------------------------------

auto TreemapItem::cellAt(const QPointF &position) const
    -> const Treemap::LayoutItem *
{
    // Children come after their parent in draw order, so the last match wins
    // and a leaf beats the directory it sits inside.
    const Treemap::LayoutItem *hit = nullptr;
    for (const auto &item : m_layout) {
        if (item.rect.contains(position.x(), position.y())) {
            hit = &item;
        }
    }
    return hit;
}

void TreemapItem::updateHover(const QPointF &position)
{
    const Treemap::LayoutItem *item = cellAt(position);
    if (item == nullptr) {
        clearHover();
        return;
    }

    const QString path = QString::fromStdString(item->node->path);
    const int leafIndex = item->isCell ? item->node->leafIndex : -1;
    if (path == m_hoveredPath && leafIndex == m_hoveredLeaf) {
        return;
    }

    m_hoveredPath = path;
    m_hoveredLeaf = leafIndex;
    m_hoveredIsDirectory = !item->isCell || item->node->leafIndex < 0;
    m_hoveredLeafCount = item->node->leafCount;
    m_hoveredAnchor = QPointF {
        item->rect.x + item->rect.w * 0.5,
        item->rect.y
    };

    m_hoveredDurationMs = 0;
    m_hoveredDeltaMs = 0;
    m_hoveredRank = 0;
    if (m_model != nullptr) {
        if (leafIndex >= 0
            && static_cast<std::size_t>(leafIndex)
                < m_model->targets().size()) {
            const auto &target
                = m_model->targets()[static_cast<std::size_t>(leafIndex)];
            m_hoveredDurationMs = target.durationMs;
            m_hoveredRank = target.rank;
            m_hoveredDeltaMs
                = m_model->leaves()[static_cast<std::size_t>(leafIndex)]
                      .deltaMs;
        }
        else {
            // A directory shows the cost of everything under it, which is
            // the number you actually want when scanning for a hot module.
            m_hoveredDurationMs = static_cast<qint64>(item->node->value);
        }
    }

    Q_EMIT hoverChanged();
    update();
}

void TreemapItem::clearHover()
{
    if (m_hoveredLeaf == -1 && m_hoveredPath.isEmpty()) {
        return;
    }
    m_hoveredLeaf = -1;
    m_hoveredPath.clear();
    m_hoveredIsDirectory = false;
    m_hoveredDurationMs = 0;
    m_hoveredDeltaMs = 0;
    m_hoveredRank = 0;
    m_hoveredLeafCount = 0;
    Q_EMIT hoverChanged();
    update();
}

void TreemapItem::hoverMoveEvent(QHoverEvent *event)
{
    updateHover(event->position());
}

void TreemapItem::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event)
    clearHover();
}

void TreemapItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (!focusParent()) {
            event->ignore();
        }
        return;
    }

    const Treemap::LayoutItem *item = cellAt(event->position());
    if (item == nullptr) {
        event->ignore();
        return;
    }

    if (item->isCell && item->node->leafIndex >= 0) {
        setSelectedIndex(item->node->leafIndex);
        Q_EMIT leafActivated(
            item->node->leafIndex,
            QString::fromStdString(item->node->path));
    }
    else {
        Q_EMIT directoryActivated(
            QString::fromStdString(item->node->path));
    }
    event->accept();
}

void TreemapItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    const Treemap::LayoutItem *item = cellAt(event->position());
    if (item == nullptr) {
        event->ignore();
        return;
    }
    // Double click drills into whatever directory contains the cursor.
    const Treemap::Node *node = item->node;
    QString target = QString::fromStdString(node->path);
    if (item->isCell && node->leafIndex >= 0) {
        const int slash = target.lastIndexOf('/');
        target = slash <= 0 ? QString {} : target.left(slash);
    }
    setFocusPath(target);
    event->accept();
}

}
