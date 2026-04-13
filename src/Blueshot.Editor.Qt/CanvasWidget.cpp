#include "CanvasWidget.h"

#include <QtCore/QSize>
#include <QtCore/QSizeF>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QFontMetricsF>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtGui/QTransform>
#include <QtGui/QWheelEvent>
#include <QtGui/QClipboard>
#include <QtCore/QRandomGenerator>
#include <QtCore/QtMath>
#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QInputDialog>

#include <algorithm>

namespace {
constexpr int kPlaceholderWidth = 960;
constexpr int kPlaceholderHeight = 540;
constexpr qreal kMinimumRectangleSide = 3.0;
constexpr qreal kMinimumLineLength = 3.0;
constexpr qreal kHitTolerance = 8.0;
constexpr qreal kHandleSize = 8.0;
constexpr auto kClipboardMimeType = "application/x-blueshot-editor-annotations";

QPointF mappedPoint(const QTransform& transform, const QPointF& point) {
    return transform.map(point);
}

QRectF normalizedRect(const QPointF& start, const QPointF& end) {
    return QRectF(start, end).normalized();
}

QRectF scaledRect(const QRectF& rect, double scale) {
    return QRectF(rect.left() * scale, rect.top() * scale, rect.width() * scale, rect.height() * scale);
}

QColor averageImageColor(const QImage& image, const QRect& rect) {
    const QRect boundedRect = rect.intersected(image.rect());
    if (boundedRect.isEmpty()) {
        return QColor(QStringLiteral("#9d9d9d"));
    }

    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 alpha = 0;
    qint64 count = 0;
    for (int y = boundedRect.top(); y <= boundedRect.bottom(); ++y) {
        for (int x = boundedRect.left(); x <= boundedRect.right(); ++x) {
            const QColor color = QColor::fromRgba(image.pixel(x, y));
            red += color.red();
            green += color.green();
            blue += color.blue();
            alpha += color.alpha();
            ++count;
        }
    }

    if (count <= 0) {
        return QColor(QStringLiteral("#9d9d9d"));
    }
    return QColor(static_cast<int>(red / count), static_cast<int>(green / count), static_cast<int>(blue / count), static_cast<int>(alpha / count));
}

QImage createRedactionFill(const QImage& source, const QRect& sourceRect, quint32 seed) {
    if (sourceRect.isEmpty()) {
        return QImage();
    }

    QImage output(sourceRect.size(), QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::transparent);

    const int sampleBand = qMax(4, qMin(18, qMin(sourceRect.width(), sourceRect.height()) / 5));
    const QColor topColor = averageImageColor(source, QRect(sourceRect.left(), sourceRect.top() - sampleBand, sourceRect.width(), sampleBand));
    const QColor bottomColor = averageImageColor(source, QRect(sourceRect.left(), sourceRect.bottom() + 1, sourceRect.width(), sampleBand));
    const QColor leftColor = averageImageColor(source, QRect(sourceRect.left() - sampleBand, sourceRect.top(), sampleBand, sourceRect.height()));
    const QColor rightColor = averageImageColor(source, QRect(sourceRect.right() + 1, sourceRect.top(), sampleBand, sourceRect.height()));
    const QColor fallbackColor = averageImageColor(source, sourceRect.adjusted(-sampleBand, -sampleBand, sampleBand, sampleBand));

    QRandomGenerator generator(seed == 0 ? 0x6d2f4a1bu : seed);
    for (int y = 0; y < output.height(); ++y) {
        const qreal verticalRatio = output.height() <= 1 ? 0.5 : static_cast<qreal>(y) / static_cast<qreal>(output.height() - 1);
        for (int x = 0; x < output.width(); ++x) {
            const qreal horizontalRatio = output.width() <= 1 ? 0.5 : static_cast<qreal>(x) / static_cast<qreal>(output.width() - 1);

            const qreal topWeight = (1.0 - verticalRatio) * 0.5;
            const qreal bottomWeight = verticalRatio * 0.5;
            const qreal leftWeight = (1.0 - horizontalRatio) * 0.5;
            const qreal rightWeight = horizontalRatio * 0.5;
            const qreal totalWeight = topWeight + bottomWeight + leftWeight + rightWeight;

            qreal red = (topColor.redF() * topWeight) + (bottomColor.redF() * bottomWeight) + (leftColor.redF() * leftWeight) + (rightColor.redF() * rightWeight);
            qreal green = (topColor.greenF() * topWeight) + (bottomColor.greenF() * bottomWeight) + (leftColor.greenF() * leftWeight) + (rightColor.greenF() * rightWeight);
            qreal blue = (topColor.blueF() * topWeight) + (bottomColor.blueF() * bottomWeight) + (leftColor.blueF() * leftWeight) + (rightColor.blueF() * rightWeight);
            if (totalWeight <= 0.0) {
                red = fallbackColor.redF();
                green = fallbackColor.greenF();
                blue = fallbackColor.blueF();
            } else {
                red /= totalWeight;
                green /= totalWeight;
                blue /= totalWeight;
            }

            const int noise = generator.bounded(31) - 15;
            const int grain = ((x * 13) + (y * 7) + static_cast<int>(seed & 0xffu)) % 11 - 5;
            QColor color(
                qBound(0, qRound(red * 255.0) + noise + grain, 255),
                qBound(0, qRound(green * 255.0) + noise / 2 + grain, 255),
                qBound(0, qRound(blue * 255.0) + noise + grain / 2, 255),
                255);
            output.setPixelColor(x, y, color);
        }
    }

    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (int index = 0; index < qMax(8, (output.width() * output.height()) / 1600); ++index) {
        const QColor accent(
            qBound(0, fallbackColor.red() + generator.bounded(35) - 17, 255),
            qBound(0, fallbackColor.green() + generator.bounded(35) - 17, 255),
            qBound(0, fallbackColor.blue() + generator.bounded(35) - 17, 255),
            110 + generator.bounded(60));
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        const int width = qMax(6, generator.bounded(qMax(7, output.width() / 3)));
        const int height = qMax(4, generator.bounded(qMax(5, output.height() / 4)));
        painter.drawRoundedRect(generator.bounded(output.width()), generator.bounded(output.height()), width, height, 3.0, 3.0);
    }

    painter.setPen(QPen(QColor(255, 255, 255, 32), 1));
    for (int index = 0; index < qMax(5, output.height() / 14); ++index) {
        const int y = generator.bounded(output.height());
        painter.drawLine(0, y, output.width(), y + generator.bounded(5) - 2);
    }

    return output;
}

QPainterPath createTornEdgePath(const QRectF& bounds, int toothHeight, int horizontalRange, int verticalRange, QRandomGenerator& generator) {
    auto jitter = [&](int range) {
        return generator.bounded(qMax(1, range) + 1);
    };

    QPainterPath path;
    path.moveTo(bounds.left(), bounds.top() + jitter(verticalRange));

    for (qreal x = bounds.left(); x < bounds.right(); x += toothHeight) {
        const qreal nextX = qMin(bounds.right(), x + toothHeight);
        path.lineTo(nextX, bounds.top() + jitter(verticalRange));
    }
    for (qreal y = bounds.top(); y < bounds.bottom(); y += toothHeight) {
        const qreal nextY = qMin(bounds.bottom(), y + toothHeight);
        path.lineTo(bounds.right() - jitter(horizontalRange), nextY);
    }
    for (qreal x = bounds.right(); x > bounds.left(); x -= toothHeight) {
        const qreal nextX = qMax(bounds.left(), x - toothHeight);
        path.lineTo(nextX, bounds.bottom() - jitter(verticalRange));
    }
    for (qreal y = bounds.bottom(); y > bounds.top(); y -= toothHeight) {
        const qreal nextY = qMax(bounds.top(), y - toothHeight);
        path.lineTo(bounds.left() + jitter(horizontalRange), nextY);
    }

    path.closeSubpath();
    return path;
}
}

template<typename T>
const T& primaryOrDefault(const QSet<int>& selection, int primaryIndex, const QVector<T>& list, const T& fallback) {
    if (primaryIndex >= 0 && primaryIndex < list.size() && selection.contains(primaryIndex)) {
        return list.at(primaryIndex);
    }
    return fallback;
}

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    updateCanvasSize();
}

void CanvasWidget::setDocumentImage(const QImage& image) {
    m_documentImage = image;
    m_annotations.clear();
    m_undoStates.clear();
    m_redoStates.clear();
    m_counterStart = 1;
    m_cropMode = CropMode::Default;
    m_nextStepSequence = 1;
    clearSelection();
    m_isMovingSelection = false;
    m_previewAnnotation = Annotation{};
    m_isDrawingAnnotation = false;
    updateCanvasSize();
    updateUndoRedoState();
    m_modified = false;
    Q_EMIT documentSizeChanged(documentSize());
    Q_EMIT cropModeChanged(m_cropMode);
    Q_EMIT counterStartChanged(m_counterStart);
    Q_EMIT editingContextChanged(editingContext());
    emitSelectionAvailability();
    emitClipboardAvailability();
    Q_EMIT modifiedChanged(false);
    update();
}

void CanvasWidget::clearDocument() {
    m_documentImage = QImage();
    m_annotations.clear();
    m_undoStates.clear();
    m_redoStates.clear();
    m_counterStart = 1;
    m_cropMode = CropMode::Default;
    m_nextStepSequence = 1;
    clearSelection();
    m_isMovingSelection = false;
    m_previewAnnotation = Annotation{};
    m_isDrawingAnnotation = false;
    updateCanvasSize();
    updateUndoRedoState();
    m_modified = false;
    Q_EMIT documentSizeChanged(documentSize());
    Q_EMIT cropModeChanged(m_cropMode);
    Q_EMIT counterStartChanged(m_counterStart);
    Q_EMIT editingContextChanged(editingContext());
    emitSelectionAvailability();
    emitClipboardAvailability();
    Q_EMIT modifiedChanged(false);
    update();
}

bool CanvasWidget::isModified() const {
    return m_modified;
}

void CanvasWidget::markSaved() {
    if (!m_modified) {
        return;
    }
    m_modified = false;
    Q_EMIT modifiedChanged(false);
}

QColor CanvasWidget::currentStrokeColor() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_strokeColor;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex)) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).strokeColor;
    }
    return m_strokeColor;
}

QColor CanvasWidget::currentFillColor() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_fillColor;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex)) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).fillColor;
    }
    return m_fillColor;
}

int CanvasWidget::currentStrokeWidth() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_strokeWidth;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex)) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).strokeWidth;
    }
    return m_strokeWidth;
}

QFont CanvasWidget::currentTextFont() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_textFont;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && isTextLike(m_annotations.at(m_primarySelectedAnnotationIndex))) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).font;
    }
    return m_textFont;
}

Qt::Alignment CanvasWidget::currentTextHorizontalAlignment() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_textHorizontalAlignment;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && isTextLike(m_annotations.at(m_primarySelectedAnnotationIndex))) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).textHorizontalAlignment;
    }
    return m_textHorizontalAlignment;
}

Qt::Alignment CanvasWidget::currentTextVerticalAlignment() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_textVerticalAlignment;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && isTextLike(m_annotations.at(m_primarySelectedAnnotationIndex))) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).textVerticalAlignment;
    }
    return m_textVerticalAlignment;
}

CanvasWidget::ArrowHeadMode CanvasWidget::currentArrowHeadMode() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_arrowHeadMode;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && m_annotations.at(m_primarySelectedAnnotationIndex).type == AnnotationType::Arrow) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).arrowHeadMode;
    }
    return m_arrowHeadMode;
}

int CanvasWidget::currentPixelSize() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_pixelSize;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && m_annotations.at(m_primarySelectedAnnotationIndex).type == AnnotationType::Pixelate) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).pixelSize;
    }
    return m_pixelSize;
}

int CanvasWidget::currentBlurRadius() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_blurRadius;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && m_annotations.at(m_primarySelectedAnnotationIndex).type == AnnotationType::Blur) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).blurRadius;
    }
    return m_blurRadius;
}

int CanvasWidget::currentMagnificationFactor() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_magnificationFactor;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && m_annotations.at(m_primarySelectedAnnotationIndex).type == AnnotationType::Magnify) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).magnificationFactor;
    }
    return m_magnificationFactor;
}

bool CanvasWidget::currentShadowEnabled() const {
    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_shadowEnabled;
    }
    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size() && m_selectedAnnotationIndices.contains(m_primarySelectedAnnotationIndex) && isTextLike(m_annotations.at(m_primarySelectedAnnotationIndex))) {
        return m_annotations.at(m_primarySelectedAnnotationIndex).shadowEnabled;
    }
    return m_shadowEnabled;
}

bool CanvasWidget::hasDocument() const {
    return !m_documentImage.isNull();
}

QSize CanvasWidget::documentSize() const {
    return hasDocument() ? m_documentImage.size() : QSize(kPlaceholderWidth, kPlaceholderHeight);
}

QImage CanvasWidget::renderDocumentImage() const {
    if (!hasDocument()) {
        return QImage();
    }

    QImage rendered = m_documentImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&rendered);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const Annotation& annotation : m_annotations) {
        drawAnnotation(painter, annotation, 1.0);
    }

    return rendered;
}

void CanvasWidget::setZoomFactor(double zoomFactor) {
    m_zoomFactor = qMax(0.1, zoomFactor);
    updateCanvasSize();
    update();
}

double CanvasWidget::zoomFactor() const {
    return m_zoomFactor;
}

void CanvasWidget::setActiveTool(const QString& toolName) {
    storeCurrentToolStyle();
    m_activeTool = toolName;
    m_isDrawingAnnotation = false;
    m_previewAnnotation = Annotation{};
    applyToolDefaults(toolName);
    Q_EMIT editingContextChanged(editingContext());
    update();
}

QString CanvasWidget::activeTool() const {
    return m_activeTool;
}

void CanvasWidget::setStrokeColor(const QColor& color) {
    m_strokeColor = color;

    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            m_annotations[index].strokeColor = color;
        }
        updateUndoRedoState();
    }

    update();
}

void CanvasWidget::setFillColor(const QColor& color) {
    m_fillColor = color;

    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            m_annotations[index].fillColor = color;
        }
        updateUndoRedoState();
    }

    update();
}

void CanvasWidget::setStrokeWidth(int width) {
    m_strokeWidth = qMax(1, width);

    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            m_annotations[index].strokeWidth = m_strokeWidth;
        }
        updateUndoRedoState();
    }

    update();
}

void CanvasWidget::setTextFont(const QFont& font) {
    m_textFont = font;

    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (isTextLike(m_annotations[index])) {
                m_annotations[index].font = font;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setTextHorizontalAlignment(Qt::Alignment alignment) {
    m_textHorizontalAlignment = alignment;
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (isTextLike(m_annotations[index])) {
                m_annotations[index].textHorizontalAlignment = alignment;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setTextVerticalAlignment(Qt::Alignment alignment) {
    m_textVerticalAlignment = alignment;
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (isTextLike(m_annotations[index])) {
                m_annotations[index].textVerticalAlignment = alignment;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setArrowHeadMode(ArrowHeadMode mode) {
    m_arrowHeadMode = mode;
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (m_annotations[index].type == AnnotationType::Arrow) {
                m_annotations[index].arrowHeadMode = mode;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setPixelSize(int size) {
    m_pixelSize = qMax(2, size);
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (m_annotations[index].type == AnnotationType::Pixelate) {
                m_annotations[index].pixelSize = m_pixelSize;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setBlurRadius(int radius) {
    m_blurRadius = qMax(2, radius);
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (m_annotations[index].type == AnnotationType::Blur) {
                m_annotations[index].blurRadius = m_blurRadius;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setMagnificationFactor(int factor) {
    m_magnificationFactor = qMax(2, factor);
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (m_annotations[index].type == AnnotationType::Magnify) {
                m_annotations[index].magnificationFactor = m_magnificationFactor;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setShadowEnabled(bool enabled) {
    m_shadowEnabled = enabled;
    if (hasSelection()) {
        pushUndoState();
        for (int index : m_selectedAnnotationIndices) {
            if (isTextLike(m_annotations[index])) {
                m_annotations[index].shadowEnabled = enabled;
            }
        }
        updateUndoRedoState();
        update();
    }
}

void CanvasWidget::setCounterStart(int value) {
    const int normalized = qBound(0, value, 999);
    if (m_counterStart == normalized) {
        return;
    }
    m_counterStart = normalized;
    Q_EMIT counterStartChanged(m_counterStart);
    update();
}

int CanvasWidget::counterStart() const {
    return m_counterStart;
}

void CanvasWidget::setCropMode(CropMode mode) {
    CropMode effectiveMode = mode;
    if (mode == CropMode::AutoCrop) {
        const QRectF autoCropRect = computeAutoCropRect();
        if (!autoCropRect.isValid() || autoCropRect.width() < kMinimumRectangleSide || autoCropRect.height() < kMinimumRectangleSide) {
            effectiveMode = CropMode::Default;
            m_hasPendingCrop = false;
            m_pendingCropRect = QRectF();
            Q_EMIT statusMessageChanged(QStringLiteral("Auto crop could not find a trim area."));
        } else {
            m_hasPendingCrop = true;
            m_pendingCropRect = autoCropRect;
            Q_EMIT statusMessageChanged(QStringLiteral("Auto crop ready. Confirm or cancel."));
        }
    }

    m_cropMode = effectiveMode;
    Q_EMIT cropModeChanged(m_cropMode);
    Q_EMIT editingContextChanged(editingContext());
    update();
}

CanvasWidget::CropMode CanvasWidget::cropMode() const {
    return m_cropMode;
}

void CanvasWidget::resizeDocumentImage(const QSize& newSize) {
    if (!hasDocument() || !newSize.isValid()) {
        return;
    }

    pushUndoState();

    const qreal scaleX = static_cast<qreal>(newSize.width()) / static_cast<qreal>(m_documentImage.width());
    const qreal scaleY = static_cast<qreal>(newSize.height()) / static_cast<qreal>(m_documentImage.height());
    m_documentImage = m_documentImage.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QTransform transform;
    transform.scale(scaleX, scaleY);
    const qreal fontScale = qMin(scaleX, scaleY);

    for (Annotation& annotation : m_annotations) {
        transformAnnotation(annotation, transform, true, fontScale);
    }

    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Image resized."));
}

bool CanvasWidget::canUndo() const {
    return !m_undoStates.isEmpty();
}

bool CanvasWidget::canRedo() const {
    return !m_redoStates.isEmpty();
}

bool CanvasWidget::canCopySelection() const {
    return hasSelection();
}

bool CanvasWidget::canPasteSelection() const {
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    return mimeData != nullptr && mimeData->hasFormat(kClipboardMimeType);
}

void CanvasWidget::undo() {
    if (m_undoStates.isEmpty()) {
        return;
    }

    m_redoStates.push_back(captureDocumentState());
    applyDocumentState(m_undoStates.takeLast());
    clearSelection();
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Undo."));
}

void CanvasWidget::redo() {
    if (m_redoStates.isEmpty()) {
        return;
    }

    m_undoStates.push_back(captureDocumentState());
    applyDocumentState(m_redoStates.takeLast());
    clearSelection();
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Redo."));
}

void CanvasWidget::rotateClockwise() {
    if (!hasDocument()) {
        return;
    }

    pushUndoState();
    const QSize oldSize = m_documentImage.size();
    QTransform transform;
    transform.rotate(90);
    m_documentImage = m_documentImage.transformed(transform);

    QTransform annotationTransform;
    annotationTransform.translate(oldSize.height(), 0.0);
    annotationTransform.rotate(90.0);

    for (Annotation& annotation : m_annotations) {
        transformAnnotation(annotation, annotationTransform);
    }

    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Image rotated clockwise."));
}

void CanvasWidget::rotateCounterClockwise() {
    if (!hasDocument()) {
        return;
    }

    pushUndoState();
    const QSize oldSize = m_documentImage.size();
    QTransform transform;
    transform.rotate(-90);
    m_documentImage = m_documentImage.transformed(transform);

    QTransform annotationTransform;
    annotationTransform.translate(0.0, oldSize.width());
    annotationTransform.rotate(-90.0);

    for (Annotation& annotation : m_annotations) {
        transformAnnotation(annotation, annotationTransform);
    }

    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Image rotated counter clockwise."));
}

void CanvasWidget::confirmPendingAction() {
    if (!m_hasPendingCrop) {
        return;
    }

    const QRectF crop = effectiveCropRect();
    m_hasPendingCrop = false;
    m_pendingCropRect = QRectF();
    applyCropMode(crop);
    Q_EMIT editingContextChanged(editingContext());
}

void CanvasWidget::cancelPendingAction() {
    if (!m_hasPendingCrop) {
        return;
    }

    m_hasPendingCrop = false;
    m_pendingCropRect = QRectF();
    Q_EMIT editingContextChanged(editingContext());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Pending action canceled."));
}

QJsonObject CanvasWidget::serializeAnnotation(const Annotation& annotation) {
    QJsonObject object;
    object[QStringLiteral("type")] = static_cast<int>(annotation.type);
    object[QStringLiteral("startX")] = annotation.start.x();
    object[QStringLiteral("startY")] = annotation.start.y();
    object[QStringLiteral("endX")] = annotation.end.x();
    object[QStringLiteral("endY")] = annotation.end.y();
    object[QStringLiteral("strokeColor")] = annotation.strokeColor.name(QColor::HexArgb);
    object[QStringLiteral("fillColor")] = annotation.fillColor.name(QColor::HexArgb);
    object[QStringLiteral("strokeWidth")] = annotation.strokeWidth;
    object[QStringLiteral("text")] = annotation.text;
    object[QStringLiteral("fontFamily")] = annotation.font.family();
    object[QStringLiteral("fontPointSize")] = annotation.font.pointSizeF();
    object[QStringLiteral("fontBold")] = annotation.font.bold();
    object[QStringLiteral("fontItalic")] = annotation.font.italic();
    object[QStringLiteral("textHorizontalAlignment")] = static_cast<int>(annotation.textHorizontalAlignment);
    object[QStringLiteral("textVerticalAlignment")] = static_cast<int>(annotation.textVerticalAlignment);
    object[QStringLiteral("arrowHeadMode")] = static_cast<int>(annotation.arrowHeadMode);
    object[QStringLiteral("pixelSize")] = annotation.pixelSize;
    object[QStringLiteral("blurRadius")] = annotation.blurRadius;
    object[QStringLiteral("magnificationFactor")] = annotation.magnificationFactor;
    object[QStringLiteral("shadowEnabled")] = annotation.shadowEnabled;
    object[QStringLiteral("stepSequence")] = QString::number(annotation.stepSequence);
    object[QStringLiteral("effectSeed")] = static_cast<int>(annotation.effectSeed);

    QJsonArray points;
    for (const QPointF& point : annotation.points) {
        QJsonObject pointObject;
        pointObject[QStringLiteral("x")] = point.x();
        pointObject[QStringLiteral("y")] = point.y();
        points.append(pointObject);
    }
    object[QStringLiteral("points")] = points;
    return object;
}

CanvasWidget::Annotation CanvasWidget::deserializeAnnotation(const QJsonObject& object) {
    Annotation annotation;
    annotation.type = static_cast<AnnotationType>(object.value(QStringLiteral("type")).toInt());
    annotation.start = QPointF(object.value(QStringLiteral("startX")).toDouble(), object.value(QStringLiteral("startY")).toDouble());
    annotation.end = QPointF(object.value(QStringLiteral("endX")).toDouble(), object.value(QStringLiteral("endY")).toDouble());
    annotation.strokeColor = QColor(object.value(QStringLiteral("strokeColor")).toString(QStringLiteral("#ffcc2f2f")));
    annotation.fillColor = QColor(object.value(QStringLiteral("fillColor")).toString(QStringLiteral("#fffff2cc")));
    annotation.strokeWidth = object.value(QStringLiteral("strokeWidth")).toInt(1);
    annotation.text = object.value(QStringLiteral("text")).toString();
    annotation.font = QFont(object.value(QStringLiteral("fontFamily")).toString(QStringLiteral("Noto Sans")), 14);
    annotation.font.setPointSizeF(object.value(QStringLiteral("fontPointSize")).toDouble(14.0));
    annotation.font.setBold(object.value(QStringLiteral("fontBold")).toBool(false));
    annotation.font.setItalic(object.value(QStringLiteral("fontItalic")).toBool(false));
    annotation.textHorizontalAlignment = static_cast<Qt::Alignment>(object.value(QStringLiteral("textHorizontalAlignment")).toInt(static_cast<int>(Qt::AlignLeft)));
    annotation.textVerticalAlignment = static_cast<Qt::Alignment>(object.value(QStringLiteral("textVerticalAlignment")).toInt(static_cast<int>(Qt::AlignTop)));
    annotation.arrowHeadMode = static_cast<ArrowHeadMode>(object.value(QStringLiteral("arrowHeadMode")).toInt(static_cast<int>(ArrowHeadMode::End)));
    annotation.pixelSize = object.value(QStringLiteral("pixelSize")).toInt(12);
    annotation.blurRadius = object.value(QStringLiteral("blurRadius")).toInt(12);
    annotation.magnificationFactor = object.value(QStringLiteral("magnificationFactor")).toInt(2);
    annotation.shadowEnabled = object.value(QStringLiteral("shadowEnabled")).toBool(false);
    annotation.stepSequence = object.value(QStringLiteral("stepSequence")).toVariant().toLongLong();
    annotation.effectSeed = static_cast<quint32>(object.value(QStringLiteral("effectSeed")).toInt(0));

    const QJsonArray points = object.value(QStringLiteral("points")).toArray();
    for (const QJsonValue& value : points) {
        const QJsonObject pointObject = value.toObject();
        annotation.points.push_back(QPointF(pointObject.value(QStringLiteral("x")).toDouble(), pointObject.value(QStringLiteral("y")).toDouble()));
    }

    return annotation;
}

void CanvasWidget::applyDocumentState(const DocumentState& state) {
    m_documentImage = state.image;
    m_annotations = state.annotations;
    qint64 maxStepSequence = 0;
    for (const Annotation& annotation : m_annotations) {
        maxStepSequence = qMax(maxStepSequence, annotation.stepSequence);
    }
    m_nextStepSequence = qMax<qint64>(1, maxStepSequence + 1);
    updateCanvasSize();
    Q_EMIT documentSizeChanged(documentSize());
    Q_EMIT editingContextChanged(editingContext());
}

CanvasWidget::DocumentState CanvasWidget::captureDocumentState() const {
    return DocumentState{m_documentImage, m_annotations};
}

void CanvasWidget::transformAnnotation(Annotation& annotation, const QTransform& transform, bool scaleFont, qreal fontScale) const {
    annotation.start = mappedPoint(transform, annotation.start);
    annotation.end = mappedPoint(transform, annotation.end);
    for (QPointF& point : annotation.points) {
        point = mappedPoint(transform, point);
    }
    if (scaleFont && isTextLike(annotation)) {
        annotation.font.setPointSizeF(qMax(6.0, annotation.font.pointSizeF() * fontScale));
    }
}

void CanvasWidget::translateAnnotation(Annotation& annotation, const QPointF& delta) const {
    annotation.start += delta;
    annotation.end += delta;
    for (QPointF& point : annotation.points) {
        point += delta;
    }
}

void CanvasWidget::offsetAnnotation(Annotation& annotation, const QPointF& delta, bool regenerateStepSequence) const {
    translateAnnotation(annotation, delta);
    if (regenerateStepSequence && annotation.type == AnnotationType::StepLabel) {
        const_cast<CanvasWidget*>(this)->reassignClonedAnnotationMetadata(annotation);
    }
}

void CanvasWidget::scaleAnnotationBounds(Annotation& annotation, const QRectF& sourceBounds, const QRectF& destinationBounds) const {
    if (!sourceBounds.isValid() || sourceBounds.width() <= 0.0 || sourceBounds.height() <= 0.0) {
        return;
    }

    auto scalePoint = [&](const QPointF& point) {
        const qreal xRatio = (point.x() - sourceBounds.left()) / sourceBounds.width();
        const qreal yRatio = (point.y() - sourceBounds.top()) / sourceBounds.height();
        return QPointF(destinationBounds.left() + (xRatio * destinationBounds.width()), destinationBounds.top() + (yRatio * destinationBounds.height()));
    };

    annotation.start = scalePoint(annotation.start);
    annotation.end = scalePoint(annotation.end);
    for (QPointF& point : annotation.points) {
        point = scalePoint(point);
    }
}

void CanvasWidget::reassignClonedAnnotationMetadata(Annotation& annotation) {
    if (annotation.type == AnnotationType::StepLabel) {
        annotation.stepSequence = nextStepSequence();
    }
    if (annotation.type == AnnotationType::Pixelate) {
        annotation.effectSeed = QRandomGenerator::global()->generate();
    }
}

QByteArray CanvasWidget::serializeSelectedAnnotations() const {
    QJsonArray array;
    QList<int> indices = m_selectedAnnotationIndices.values();
    std::sort(indices.begin(), indices.end());
    for (int index : indices) {
        array.append(serializeAnnotation(m_annotations.at(index)));
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

void CanvasWidget::copySelection() {
    if (!canCopySelection()) {
        return;
    }

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(kClipboardMimeType, serializeSelectedAnnotations());
    QGuiApplication::clipboard()->setMimeData(mimeData);
    emitClipboardAvailability();
    Q_EMIT statusMessageChanged(QStringLiteral("Selection copied."));
}

void CanvasWidget::cutSelection() {
    if (!canCopySelection()) {
        return;
    }

    copySelection();
    removeSelectedAnnotation();
    Q_EMIT statusMessageChanged(QStringLiteral("Selection cut."));
}

void CanvasWidget::pasteSelection() {
    if (!canPasteSelection()) {
        return;
    }

    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    const QJsonDocument document = QJsonDocument::fromJson(mimeData->data(kClipboardMimeType));
    if (!document.isArray()) {
        return;
    }

    pushUndoState();
    clearSelection();
    const QJsonArray array = document.array();
    for (const QJsonValue& value : array) {
        Annotation annotation = deserializeAnnotation(value.toObject());
        offsetAnnotation(annotation, QPointF(12.0, 12.0), true);
        m_annotations.push_back(annotation);
        m_selectedAnnotationIndices.insert(m_annotations.size() - 1);
        m_primarySelectedAnnotationIndex = m_annotations.size() - 1;
    }
    updateUndoRedoState();
    emitSelectionAvailability();
    Q_EMIT editingContextChanged(editingContext());
    emitClipboardAvailability();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Selection pasted."));
}

void CanvasWidget::selectAll() {
    if (!hasDocument() || m_annotations.isEmpty()) {
        return;
    }

    m_selectedAnnotationIndices.clear();
    for (int index = 0; index < m_annotations.size(); ++index) {
        m_selectedAnnotationIndices.insert(index);
    }
    m_primarySelectedAnnotationIndex = m_annotations.size() - 1;
    Q_EMIT editingContextChanged(editingContext());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("All objects selected."));
}

void CanvasWidget::duplicateSelection() {
    if (!hasSelection()) {
        return;
    }

    pushUndoState();
    QList<int> indices = m_selectedAnnotationIndices.values();
    std::sort(indices.begin(), indices.end());
    clearSelection();
    for (int index : indices) {
        Annotation duplicate = m_annotations.at(index);
        offsetAnnotation(duplicate, QPointF(12.0, 12.0), true);
        m_annotations.push_back(duplicate);
        m_selectedAnnotationIndices.insert(m_annotations.size() - 1);
        m_primarySelectedAnnotationIndex = m_annotations.size() - 1;
    }
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Object duplicated."));
}

void CanvasWidget::bringSelectionToFront() {
    if (!hasSelection()) {
        return;
    }

    pushUndoState();
    QList<int> indices = m_selectedAnnotationIndices.values();
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    QVector<Annotation> moved;
    for (int index : indices) {
        moved.push_front(m_annotations.takeAt(index));
    }
    clearSelection();
    for (const Annotation& annotation : moved) {
        m_annotations.push_back(annotation);
        m_selectedAnnotationIndices.insert(m_annotations.size() - 1);
        m_primarySelectedAnnotationIndex = m_annotations.size() - 1;
    }
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Object brought to front."));
}

void CanvasWidget::sendSelectionToBack() {
    if (!hasSelection()) {
        return;
    }

    pushUndoState();
    QList<int> indices = m_selectedAnnotationIndices.values();
    std::sort(indices.begin(), indices.end());
    QVector<Annotation> moved;
    for (int index : indices) {
        moved.push_back(m_annotations.takeAt(index - moved.size()));
    }
    clearSelection();
    int insertIndex = 0;
    for (const Annotation& annotation : moved) {
        m_annotations.insert(insertIndex, annotation);
        m_selectedAnnotationIndices.insert(insertIndex);
        ++insertIndex;
    }
    m_primarySelectedAnnotationIndex = moved.isEmpty() ? -1 : 0;
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Object sent to back."));
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!hasDocument()) {
        drawPlaceholder(painter);
        return;
    }

    const QRect targetRect(QPoint(0, 0), size());
    painter.drawImage(targetRect, m_documentImage);

    for (const Annotation& annotation : m_annotations) {
        drawAnnotation(painter, annotation, m_zoomFactor);
    }

    if (m_activeTool == QStringLiteral("Cursor")) {
        for (int index = 0; index < m_annotations.size(); ++index) {
            if (!isSelected(index)) {
                continue;
            }

            drawSelection(painter, m_annotations.at(index), m_zoomFactor);
        }

        if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size()) {
            drawResizeHandles(painter, m_annotations.at(m_primarySelectedAnnotationIndex), m_zoomFactor);
        }
    }

    if (m_hasPendingCrop) {
        drawPendingCrop(painter, m_zoomFactor);
    }

    if (m_isDrawingAnnotation) {
        drawAnnotation(painter, m_previewAnnotation, m_zoomFactor);
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !hasDocument()) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);
    const QPointF imagePoint = toImagePoint(event->position());
    m_lastPointerImagePoint = imagePoint;

    if (m_activeTool == QStringLiteral("Cursor")) {
        const int hitIndex = hitTestAnnotation(imagePoint);
        const bool additive = event->modifiers().testFlag(Qt::ControlModifier);
        selectAnnotation(hitIndex, additive);
        m_activeResizeHandle = hitTestResizeHandle(imagePoint);
        m_isResizingSelection = m_primarySelectedAnnotationIndex >= 0 && m_activeResizeHandle != ResizeHandle::None;
        m_isMovingSelection = hitIndex >= 0 && !m_isResizingSelection;
        if (hasSelection()) {
            Q_EMIT statusMessageChanged(m_isResizingSelection ? QStringLiteral("Resize handle selected.") : QStringLiteral("Object selected."));
        } else {
            Q_EMIT statusMessageChanged(QStringLiteral("Selection cleared."));
        }
        update();
        return;
    }

    if (m_activeTool == QStringLiteral("Text")) {
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(this,
            QStringLiteral("Add Text"),
            QStringLiteral("Text"),
            QString(),
            &accepted);

        if (accepted && !text.trimmed().isEmpty()) {
            pushUndoState();
            Annotation annotation;
            annotation.type = AnnotationType::Text;
            annotation.start = imagePoint;
            annotation.end = imagePoint + QPointF(220.0, 90.0);
            annotation.strokeColor = m_strokeColor;
            annotation.fillColor = m_fillColor;
            annotation.strokeWidth = m_strokeWidth;
            annotation.text = text;
            annotation.font = m_textFont;
            annotation.textHorizontalAlignment = m_textHorizontalAlignment;
            annotation.textVerticalAlignment = m_textVerticalAlignment;
            annotation.pixelSize = m_pixelSize;
            annotation.blurRadius = m_blurRadius;
            annotation.magnificationFactor = m_magnificationFactor;
            m_annotations.push_back(annotation);
            selectAnnotation(m_annotations.size() - 1);
            updateUndoRedoState();
            update();
            Q_EMIT statusMessageChanged(QStringLiteral("Text added."));
        }
        return;
    }

    if (m_activeTool == QStringLiteral("Emoji")) {
        bool accepted = false;
        const QString text = QInputDialog::getText(this,
            QStringLiteral("Add Emoji"),
            QStringLiteral("Emoji"),
            QLineEdit::Normal,
            QStringLiteral("😀"),
            &accepted);

        if (accepted && !text.trimmed().isEmpty()) {
            pushUndoState();
            Annotation annotation;
            annotation.type = AnnotationType::Emoji;
            annotation.start = imagePoint;
            annotation.end = imagePoint + QPointF(48.0, 48.0);
            annotation.strokeColor = m_strokeColor;
            annotation.fillColor = m_fillColor;
            annotation.strokeWidth = m_strokeWidth;
            annotation.text = text;
            annotation.font = QFont(m_textFont.family(), qMax(18, m_textFont.pointSize() + 10));
            annotation.textHorizontalAlignment = m_textHorizontalAlignment;
            annotation.textVerticalAlignment = m_textVerticalAlignment;
            annotation.pixelSize = m_pixelSize;
            annotation.blurRadius = m_blurRadius;
            annotation.magnificationFactor = m_magnificationFactor;
            m_annotations.push_back(annotation);
            selectAnnotation(m_annotations.size() - 1);
            updateUndoRedoState();
            update();
            Q_EMIT statusMessageChanged(QStringLiteral("Emoji added."));
        }
        return;
    }

    if (m_activeTool == QStringLiteral("Text Highlight")) {
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(this,
            QStringLiteral("Add Highlighted Text"),
            QStringLiteral("Text"),
            QString(),
            &accepted);

        if (accepted && !text.trimmed().isEmpty()) {
            pushUndoState();
            Annotation annotation;
            annotation.type = AnnotationType::TextHighlight;
            annotation.start = imagePoint;
            annotation.end = imagePoint + QPointF(220.0, 90.0);
            annotation.strokeColor = QColor(QStringLiteral("#1f1f1f"));
            annotation.fillColor = QColor(QStringLiteral("#ffe96b"));
            annotation.strokeWidth = m_strokeWidth;
            annotation.text = text;
            annotation.font = m_textFont;
            annotation.textHorizontalAlignment = m_textHorizontalAlignment;
            annotation.textVerticalAlignment = m_textVerticalAlignment;
            annotation.pixelSize = m_pixelSize;
            annotation.blurRadius = m_blurRadius;
            annotation.magnificationFactor = m_magnificationFactor;
            m_annotations.push_back(annotation);
            selectAnnotation(m_annotations.size() - 1);
            updateUndoRedoState();
            update();
            Q_EMIT statusMessageChanged(QStringLiteral("Text highlight added."));
        }
        return;
    }

    if (m_activeTool == QStringLiteral("Speech bubble")) {
        bool accepted = false;
        const QString text = QInputDialog::getMultiLineText(this,
            QStringLiteral("Add Speech Bubble"),
            QStringLiteral("Text"),
            QString(),
            &accepted);

        if (accepted && !text.trimmed().isEmpty()) {
            pushUndoState();
            Annotation annotation;
            annotation.type = AnnotationType::SpeechBubble;
            annotation.start = imagePoint;
            annotation.end = imagePoint + QPointF(180.0, 90.0);
            annotation.strokeColor = m_strokeColor;
            annotation.fillColor = m_fillColor;
            annotation.strokeWidth = m_strokeWidth;
            annotation.text = text;
            annotation.font = m_textFont;
            annotation.textHorizontalAlignment = m_textHorizontalAlignment;
            annotation.textVerticalAlignment = m_textVerticalAlignment;
            annotation.pixelSize = m_pixelSize;
            annotation.blurRadius = m_blurRadius;
            annotation.magnificationFactor = m_magnificationFactor;
            m_annotations.push_back(annotation);
            selectAnnotation(m_annotations.size() - 1);
            updateUndoRedoState();
            update();
            Q_EMIT statusMessageChanged(QStringLiteral("Speech bubble added."));
        }
        return;
    }

    if (m_activeTool == QStringLiteral("Step label")) {
        pushUndoState();
        Annotation annotation;
        annotation.type = AnnotationType::StepLabel;
        annotation.start = imagePoint;
        annotation.end = imagePoint + QPointF(36.0, 36.0);
        annotation.strokeColor = m_strokeColor;
        annotation.fillColor = m_fillColor;
        annotation.strokeWidth = m_strokeWidth;
        annotation.text.clear();
        annotation.font = QFont(m_textFont.family(), qMax(10, m_textFont.pointSize() - 2), QFont::Bold);
        annotation.textHorizontalAlignment = Qt::AlignCenter;
        annotation.textVerticalAlignment = Qt::AlignCenter;
        annotation.pixelSize = m_pixelSize;
        annotation.blurRadius = m_blurRadius;
        annotation.magnificationFactor = m_magnificationFactor;
        annotation.shadowEnabled = m_shadowEnabled;
        annotation.stepSequence = nextStepSequence();
        m_annotations.push_back(annotation);
        selectAnnotation(m_annotations.size() - 1);
        updateUndoRedoState();
        update();
        Q_EMIT statusMessageChanged(QStringLiteral("Step label added."));
        return;
    }

    if (isDrawableTool()) {
        m_isDrawingAnnotation = true;
            m_previewAnnotation = Annotation{
                annotationTypeForTool(),
                imagePoint,
                imagePoint,
                m_strokeColor,
                m_fillColor,
                m_strokeWidth,
                QString(),
                m_textFont,
                {},
                m_textHorizontalAlignment,
                m_textVerticalAlignment,
                m_arrowHeadMode,
                m_pixelSize,
                m_blurRadius,
                m_magnificationFactor,
            };
        if (m_previewAnnotation.type == AnnotationType::Freehand) {
            m_previewAnnotation.points.push_back(imagePoint);
        }
        selectAnnotation(-1);
        Q_EMIT statusMessageChanged(QStringLiteral("Drawing %1...").arg(m_activeTool.toLower()));
        update();
        return;
    }

    if (m_activeTool == QStringLiteral("Crop")) {
        m_isDrawingAnnotation = true;
        if (!isAutoCropMode()) {
            m_hasPendingCrop = false;
            m_pendingCropRect = QRectF();
        }
        m_previewAnnotation = Annotation{
            AnnotationType::Rectangle,
            imagePoint,
            imagePoint,
            QColor(QStringLiteral("#2c7be5")),
            QColor(44, 123, 229, 32),
            2,
            QString(),
            m_textFont,
            {},
            m_textHorizontalAlignment,
            m_textVerticalAlignment,
            m_arrowHeadMode,
            m_pixelSize,
            m_blurRadius,
            m_magnificationFactor,
        };
        selectAnnotation(-1);
        Q_EMIT statusMessageChanged(QStringLiteral("Selecting crop area..."));
        update();
        return;
    }

    Q_EMIT statusMessageChanged(QStringLiteral("%1 tool is not implemented yet.").arg(m_activeTool));
    QWidget::mousePressEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!hasDocument() || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const int hitIndex = hitTestAnnotation(toImagePoint(event->position()));
    if (hitIndex < 0 || hitIndex >= m_annotations.size()) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    Annotation& annotation = m_annotations[hitIndex];
    if (!isTextLike(annotation)) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this,
        QStringLiteral("Edit Text"),
        QStringLiteral("Text"),
        annotation.text,
        &accepted);

    if (accepted && !text.trimmed().isEmpty() && text != annotation.text) {
        pushUndoState();
        annotation.text = text;
        updateUndoRedoState();
        update();
        Q_EMIT statusMessageChanged(QStringLiteral("Text updated."));
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!hasDocument()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF imagePoint = toImagePoint(event->position());

    if (m_isResizingSelection && m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size()) {
        resizeSelectedAnnotation(m_activeResizeHandle, imagePoint);
        update();
        return;
    }

    if (m_isMovingSelection && hasSelection()) {
        const QPointF delta = imagePoint - m_lastPointerImagePoint;
        translateSelectedAnnotation(delta);
        m_lastPointerImagePoint = imagePoint;
        update();
        return;
    }

    if (!m_isDrawingAnnotation) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    m_previewAnnotation.end = imagePoint;
    if (m_previewAnnotation.type == AnnotationType::Freehand) {
        m_previewAnnotation.points.push_back(imagePoint);
    }
    update();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!hasDocument() || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_isMovingSelection) {
        m_isMovingSelection = false;
        Q_EMIT statusMessageChanged(QStringLiteral("Object moved."));
        return;
    }

    if (m_isResizingSelection) {
        m_isResizingSelection = false;
        m_activeResizeHandle = ResizeHandle::None;
        Q_EMIT statusMessageChanged(QStringLiteral("Object resized."));
        return;
    }

    if (!m_isDrawingAnnotation) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_isDrawingAnnotation = false;
    m_previewAnnotation.end = toImagePoint(event->position());

    if (m_activeTool == QStringLiteral("Crop")) {
        const QRectF rect = normalizedClampedRect(m_previewAnnotation.start, m_previewAnnotation.end);
        if (rect.width() >= kMinimumRectangleSide && rect.height() >= kMinimumRectangleSide) {
            m_hasPendingCrop = true;
            m_pendingCropRect = rect;
            Q_EMIT editingContextChanged(editingContext());
            Q_EMIT statusMessageChanged(QStringLiteral("Crop ready. Confirm or cancel."));
        } else {
            Q_EMIT statusMessageChanged(QStringLiteral("Crop canceled."));
        }
        m_previewAnnotation = Annotation{};
        update();
        return;
    }

    if (annotationHasMinimumSize(m_previewAnnotation)) {
        commitAnnotation(m_previewAnnotation);
        selectAnnotation(m_annotations.size() - 1);
        Q_EMIT statusMessageChanged(QStringLiteral("%1 added.").arg(m_activeTool));
    } else {
        Q_EMIT statusMessageChanged(QStringLiteral("%1 canceled.").arg(m_activeTool));
    }

    m_previewAnnotation = Annotation{};
    update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (!hasDocument()) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }

    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }

    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        removeSelectedAnnotation();
        return;
    }

    QPointF delta;
    switch (event->key()) {
    case Qt::Key_Left:
        delta = QPointF(-1.0, 0.0);
        break;
    case Qt::Key_Right:
        delta = QPointF(1.0, 0.0);
        break;
    case Qt::Key_Up:
        delta = QPointF(0.0, -1.0);
        break;
    case Qt::Key_Down:
        delta = QPointF(0.0, 1.0);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    translateSelectedAnnotation(delta);
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Object nudged."));
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const QPoint angleDelta = event->angleDelta();
        if (angleDelta.y() > 0) {
            Q_EMIT zoomStepRequested(1);
        } else if (angleDelta.y() < 0) {
            Q_EMIT zoomStepRequested(-1);
        }
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}

void CanvasWidget::updateCanvasSize() {
    const QSize baseSize = documentSize();
    const QSize scaledSize(qMax(1, qRound(baseSize.width() * m_zoomFactor)), qMax(1, qRound(baseSize.height() * m_zoomFactor)));
    setFixedSize(scaledSize);
    updateGeometry();
}

QPointF CanvasWidget::toImagePoint(const QPointF& widgetPoint) const {
    if (!hasDocument()) {
        return widgetPoint;
    }

    const qreal x = qBound(0.0, widgetPoint.x() / m_zoomFactor, static_cast<qreal>(m_documentImage.width()));
    const qreal y = qBound(0.0, widgetPoint.y() / m_zoomFactor, static_cast<qreal>(m_documentImage.height()));
    return QPointF(x, y);
}

QRectF CanvasWidget::normalizedClampedRect(const QPointF& start, const QPointF& end) const {
    QRectF rect(start, end);
    rect = rect.normalized();

    const QRectF bounds(0.0, 0.0, m_documentImage.width(), m_documentImage.height());
    return rect.intersected(bounds);
}

QRectF CanvasWidget::annotationBounds(const Annotation& annotation) const {
    if (annotation.type == AnnotationType::Text) {
        QFontMetricsF metrics(annotation.font);
        const QRectF textRect = metrics.boundingRect(QRectF(annotation.start, QSizeF(10000.0, 10000.0)), Qt::TextWordWrap, annotation.text);
        return textRect.translated(annotation.start - textRect.topLeft()).adjusted(-4.0, -4.0, 4.0, 4.0);
    }

    if (annotation.type == AnnotationType::Freehand && !annotation.points.isEmpty()) {
        QRectF bounds(annotation.points.first(), QSizeF(1.0, 1.0));
        for (const QPointF& point : annotation.points) {
            bounds = bounds.united(QRectF(point, QSizeF(1.0, 1.0)));
        }
        const qreal pad = qMax<qreal>(kHitTolerance, annotation.strokeWidth + 4.0);
        return bounds.adjusted(-pad, -pad, pad, pad);
    }

    if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
        QRectF rect(annotation.start, annotation.end);
        rect = rect.normalized();
        const qreal pad = qMax<qreal>(kHitTolerance, annotation.strokeWidth + 4.0);
        return rect.adjusted(-pad, -pad, pad, pad);
    }

    return normalizedClampedRect(annotation.start, annotation.end);
}

CanvasWidget::ResizeHandle CanvasWidget::hitTestResizeHandle(const QPointF& imagePoint) const {
    if (m_primarySelectedAnnotationIndex < 0 || m_primarySelectedAnnotationIndex >= m_annotations.size()) {
        return ResizeHandle::None;
    }

    const Annotation& annotation = m_annotations.at(m_primarySelectedAnnotationIndex);
    if (annotation.type == AnnotationType::Text) {
        return ResizeHandle::None;
    }

    auto nearPoint = [&](const QPointF& point) {
        return QLineF(point, imagePoint).length() <= kHandleSize;
    };

    if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
        if (nearPoint(annotation.start)) {
            return ResizeHandle::LineStart;
        }
        if (nearPoint(annotation.end)) {
            return ResizeHandle::LineEnd;
        }
        return ResizeHandle::None;
    }

    const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
    if (nearPoint(rect.topLeft())) {
        return ResizeHandle::TopLeft;
    }
    if (nearPoint(rect.topRight())) {
        return ResizeHandle::TopRight;
    }
    if (nearPoint(rect.bottomLeft())) {
        return ResizeHandle::BottomLeft;
    }
    if (nearPoint(rect.bottomRight())) {
        return ResizeHandle::BottomRight;
    }

    return ResizeHandle::None;
}

QRectF CanvasWidget::cropRect() const {
    return normalizedClampedRect(m_previewAnnotation.start, m_previewAnnotation.end);
}

QRectF CanvasWidget::effectiveCropRect() const {
    if (m_hasPendingCrop && m_pendingCropRect.isValid()) {
        return m_pendingCropRect;
    }
    return cropRect();
}

CanvasWidget::AnnotationType CanvasWidget::annotationTypeForTool() const {
    if (m_activeTool == QStringLiteral("Rectangle")) {
        return AnnotationType::Rectangle;
    }
    if (m_activeTool == QStringLiteral("Ellipse")) {
        return AnnotationType::Ellipse;
    }
    if (m_activeTool == QStringLiteral("Arrow")) {
        return AnnotationType::Arrow;
    }
    if (m_activeTool == QStringLiteral("Freehand")) {
        return AnnotationType::Freehand;
    }
    if (m_activeTool == QStringLiteral("Text")) {
        return AnnotationType::Text;
    }
    if (m_activeTool == QStringLiteral("Text Highlight")) {
        return AnnotationType::TextHighlight;
    }
    if (m_activeTool == QStringLiteral("Highlight")) {
        return AnnotationType::Highlight;
    }
    if (m_activeTool == QStringLiteral("Obfuscate")) {
        return AnnotationType::Pixelate;
    }
    if (m_activeTool == QStringLiteral("Blur")) {
        return AnnotationType::Blur;
    }
    if (m_activeTool == QStringLiteral("Grayscale")) {
        return AnnotationType::Grayscale;
    }
    if (m_activeTool == QStringLiteral("Magnify")) {
        return AnnotationType::Magnify;
    }
    return AnnotationType::Line;
}

bool CanvasWidget::isDrawableTool() const {
    return m_activeTool == QStringLiteral("Rectangle") || m_activeTool == QStringLiteral("Ellipse") || m_activeTool == QStringLiteral("Line") || m_activeTool == QStringLiteral("Arrow") || m_activeTool == QStringLiteral("Freehand") || m_activeTool == QStringLiteral("Highlight") || m_activeTool == QStringLiteral("Obfuscate") || m_activeTool == QStringLiteral("Blur") || m_activeTool == QStringLiteral("Grayscale") || m_activeTool == QStringLiteral("Magnify");
}

bool CanvasWidget::isAutoCropMode() const {
    return m_cropMode == CropMode::AutoCrop;
}

bool CanvasWidget::annotationHasMinimumSize(const Annotation& annotation) const {
    if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
        const qreal deltaX = annotation.end.x() - annotation.start.x();
        const qreal deltaY = annotation.end.y() - annotation.start.y();
        return qSqrt((deltaX * deltaX) + (deltaY * deltaY)) >= kMinimumLineLength;
    }

    if (annotation.type == AnnotationType::Text || annotation.type == AnnotationType::Emoji || annotation.type == AnnotationType::TextHighlight) {
        return !annotation.text.trimmed().isEmpty();
    }

    if (annotation.type == AnnotationType::Freehand) {
        return annotation.points.size() > 1;
    }

    if (annotation.type == AnnotationType::SpeechBubble || annotation.type == AnnotationType::StepLabel) {
        return true;
    }

    const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
    return rect.width() >= kMinimumRectangleSide && rect.height() >= kMinimumRectangleSide;
}

int CanvasWidget::hitTestAnnotation(const QPointF& imagePoint) const {
    for (int index = m_annotations.size() - 1; index >= 0; --index) {
        const Annotation& annotation = m_annotations.at(index);
        if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
            const QLineF line(annotation.start, annotation.end);
            if (line.length() <= 0.0) {
                continue;
            }

            const QLineF normal = line.normalVector();
            const QLineF unit = normal.unitVector();
            const QPointF offset = (unit.p2() - unit.p1()) * qMax<qreal>(kHitTolerance, annotation.strokeWidth + 2.0);
            QPainterPath path;
            path.moveTo(line.p1() + offset);
            path.lineTo(line.p2() + offset);
            path.lineTo(line.p2() - offset);
            path.lineTo(line.p1() - offset);
            path.closeSubpath();
            if (path.contains(imagePoint)) {
                return index;
            }
            continue;
        }

        if (annotationBounds(annotation).contains(imagePoint)) {
            return index;
        }
    }

    return -1;
}

bool CanvasWidget::hasSelection() const {
    return !m_selectedAnnotationIndices.isEmpty();
}

bool CanvasWidget::isSelected(int index) const {
    return m_selectedAnnotationIndices.contains(index);
}

bool CanvasWidget::isTextLike(const Annotation& annotation) const {
    return annotation.type == AnnotationType::Text || annotation.type == AnnotationType::Emoji || annotation.type == AnnotationType::TextHighlight || annotation.type == AnnotationType::SpeechBubble || annotation.type == AnnotationType::StepLabel;
}

QString CanvasWidget::editingContext() const {
    if (m_hasPendingCrop) {
        return QStringLiteral("CropPending");
    }

    if (m_activeTool != QStringLiteral("Cursor")) {
        return m_activeTool;
    }

    if (m_primarySelectedAnnotationIndex >= 0 && m_primarySelectedAnnotationIndex < m_annotations.size()) {
        const Annotation& annotation = m_annotations.at(m_primarySelectedAnnotationIndex);
        switch (annotation.type) {
        case AnnotationType::Rectangle:
            return QStringLiteral("Rectangle");
        case AnnotationType::Ellipse:
            return QStringLiteral("Ellipse");
        case AnnotationType::Line:
            return QStringLiteral("Line");
        case AnnotationType::Arrow:
            return QStringLiteral("Arrow");
        case AnnotationType::Freehand:
            return QStringLiteral("Freehand");
        case AnnotationType::Text:
            return QStringLiteral("Text");
        case AnnotationType::Emoji:
            return QStringLiteral("Emoji");
        case AnnotationType::TextHighlight:
            return QStringLiteral("Text Highlight");
        case AnnotationType::Highlight:
            return QStringLiteral("Highlight");
        case AnnotationType::Pixelate:
            return QStringLiteral("Obfuscate");
        case AnnotationType::Blur:
            return QStringLiteral("Blur");
        case AnnotationType::Grayscale:
            return QStringLiteral("Grayscale");
        case AnnotationType::Magnify:
            return QStringLiteral("Magnify");
        case AnnotationType::SpeechBubble:
            return QStringLiteral("Speech bubble");
        case AnnotationType::StepLabel:
            return QStringLiteral("Step label");
        }
    }

    return m_activeTool;
}

QString CanvasWidget::stepLabelText(const Annotation& annotation) const {
    if (annotation.type != AnnotationType::StepLabel) {
        return annotation.text;
    }

    int number = m_counterStart;
    QList<qint64> orderedSequences;
    orderedSequences.reserve(m_annotations.size());
    for (const Annotation& current : m_annotations) {
        if (current.type == AnnotationType::StepLabel && current.stepSequence >= 0) {
            orderedSequences.push_back(current.stepSequence);
        }
    }
    std::sort(orderedSequences.begin(), orderedSequences.end());
    for (qint64 sequence : orderedSequences) {
        if (sequence == annotation.stepSequence) {
            break;
        }
        ++number;
    }
    return QString::number(number);
}

qint64 CanvasWidget::nextStepSequence() {
    return m_nextStepSequence++;
}

QRectF CanvasWidget::computeAutoCropRect() const {
    if (!hasDocument() || m_documentImage.isNull()) {
        return QRectF();
    }

    const QColor borderColor = QColor::fromRgba(m_documentImage.pixel(0, 0));
    auto farFromBorder = [&](const QColor& color) {
        return qAbs(color.red() - borderColor.red()) > 12
            || qAbs(color.green() - borderColor.green()) > 12
            || qAbs(color.blue() - borderColor.blue()) > 12
            || qAbs(color.alpha() - borderColor.alpha()) > 12;
    };

    int top = 0;
    while (top < m_documentImage.height()) {
        bool found = false;
        for (int x = 0; x < m_documentImage.width(); ++x) {
            if (farFromBorder(QColor::fromRgba(m_documentImage.pixel(x, top)))) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        ++top;
    }

    int bottom = m_documentImage.height() - 1;
    while (bottom >= top) {
        bool found = false;
        for (int x = 0; x < m_documentImage.width(); ++x) {
            if (farFromBorder(QColor::fromRgba(m_documentImage.pixel(x, bottom)))) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        --bottom;
    }

    int left = 0;
    while (left < m_documentImage.width()) {
        bool found = false;
        for (int y = top; y <= bottom; ++y) {
            if (farFromBorder(QColor::fromRgba(m_documentImage.pixel(left, y)))) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        ++left;
    }

    int right = m_documentImage.width() - 1;
    while (right >= left) {
        bool found = false;
        for (int y = top; y <= bottom; ++y) {
            if (farFromBorder(QColor::fromRgba(m_documentImage.pixel(right, y)))) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        --right;
    }

    if (left >= right || top >= bottom) {
        return QRectF();
    }
    return QRectF(left, top, (right - left) + 1, (bottom - top) + 1);
}

void CanvasWidget::applyToolDefaults(const QString& toolName) {
    if (toolName == QStringLiteral("Cursor") || toolName == QStringLiteral("Resize") || toolName == QStringLiteral("Rotate clockwise") || toolName == QStringLiteral("Rotate counter clockwise") || toolName == QStringLiteral("Crop")) {
        return;
    }

    if (m_toolStyles.contains(toolName)) {
        loadToolStyle(toolName);
        return;
    }

    if (toolName == QStringLiteral("Rectangle") || toolName == QStringLiteral("Ellipse")) {
        m_fillColor = Qt::transparent;
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 2;
        m_shadowEnabled = true;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Line")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 2;
        m_shadowEnabled = true;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Arrow")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 2;
        m_arrowHeadMode = ArrowHeadMode::End;
        m_shadowEnabled = true;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Freehand")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 3;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Text")) {
        m_fillColor = Qt::transparent;
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 2;
        m_textHorizontalAlignment = Qt::AlignHCenter;
        m_textVerticalAlignment = Qt::AlignVCenter;
        m_shadowEnabled = true;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Speech bubble")) {
        m_fillColor = QColor(Qt::white);
        m_strokeColor = QColor(Qt::blue);
        m_strokeWidth = 2;
        m_textHorizontalAlignment = Qt::AlignHCenter;
        m_textVerticalAlignment = Qt::AlignVCenter;
        m_shadowEnabled = false;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Step label")) {
        m_fillColor = QColor(Qt::darkRed);
        m_strokeColor = QColor(Qt::white);
        m_strokeWidth = 0;
        m_textHorizontalAlignment = Qt::AlignHCenter;
        m_textVerticalAlignment = Qt::AlignVCenter;
        m_shadowEnabled = false;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Emoji")) {
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Text Highlight")) {
        m_fillColor = QColor(QStringLiteral("#ffe96b"));
        m_strokeColor = QColor(QStringLiteral("#1f1f1f"));
        m_textHorizontalAlignment = Qt::AlignLeft;
        m_textVerticalAlignment = Qt::AlignTop;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Highlight")) {
        m_fillColor = QColor(Qt::yellow);
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 0;
        m_shadowEnabled = false;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Obfuscate")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 0;
        m_pixelSize = 5;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Blur")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 0;
        m_blurRadius = 3;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Magnify")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 0;
        m_magnificationFactor = 2;
        storeCurrentToolStyle();
        return;
    }

    if (toolName == QStringLiteral("Grayscale")) {
        m_strokeColor = QColor(Qt::red);
        m_strokeWidth = 0;
        storeCurrentToolStyle();
        return;
    }
}

void CanvasWidget::storeCurrentToolStyle() {
    if (m_activeTool.isEmpty() || m_activeTool == QStringLiteral("Cursor") || m_activeTool == QStringLiteral("Crop") || m_activeTool == QStringLiteral("Resize") || m_activeTool == QStringLiteral("Rotate clockwise") || m_activeTool == QStringLiteral("Rotate counter clockwise")) {
        return;
    }

    ToolStyleState state;
    state.strokeColor = m_strokeColor;
    state.fillColor = m_fillColor;
    state.strokeWidth = m_strokeWidth;
    state.font = m_textFont;
    state.textHorizontalAlignment = m_textHorizontalAlignment;
    state.textVerticalAlignment = m_textVerticalAlignment;
    state.arrowHeadMode = m_arrowHeadMode;
    state.pixelSize = m_pixelSize;
    state.blurRadius = m_blurRadius;
    state.magnificationFactor = m_magnificationFactor;
    state.shadowEnabled = m_shadowEnabled;
    m_toolStyles.insert(m_activeTool, state);
}

void CanvasWidget::loadToolStyle(const QString& toolName) {
    const ToolStyleState state = m_toolStyles.value(toolName);
    m_strokeColor = state.strokeColor;
    m_fillColor = state.fillColor;
    m_strokeWidth = state.strokeWidth;
    m_textFont = state.font;
    m_textHorizontalAlignment = state.textHorizontalAlignment;
    m_textVerticalAlignment = state.textVerticalAlignment;
    m_arrowHeadMode = state.arrowHeadMode;
    m_pixelSize = state.pixelSize;
    m_blurRadius = state.blurRadius;
    m_magnificationFactor = state.magnificationFactor;
    m_shadowEnabled = state.shadowEnabled;
}

void CanvasWidget::emitSelectionAvailability() {
    Q_EMIT selectionAvailabilityChanged(hasSelection());
}

void CanvasWidget::emitClipboardAvailability() {
    Q_EMIT clipboardAvailabilityChanged(canPasteSelection());
}

void CanvasWidget::selectAnnotation(int index, bool additive) {
    if (!additive) {
        m_selectedAnnotationIndices.clear();
    }

    if (index < 0 || index >= m_annotations.size()) {
        if (!additive) {
            m_primarySelectedAnnotationIndex = -1;
        }
        return;
    }

    if (additive && m_selectedAnnotationIndices.contains(index)) {
        m_selectedAnnotationIndices.remove(index);
        if (m_primarySelectedAnnotationIndex == index) {
            m_primarySelectedAnnotationIndex = m_selectedAnnotationIndices.isEmpty() ? -1 : *m_selectedAnnotationIndices.constBegin();
        }
        return;
    }

    m_selectedAnnotationIndices.insert(index);
    m_primarySelectedAnnotationIndex = index;
    Q_EMIT editingContextChanged(editingContext());
    emitSelectionAvailability();
}

void CanvasWidget::clearSelection() {
    m_selectedAnnotationIndices.clear();
    m_primarySelectedAnnotationIndex = -1;
    Q_EMIT editingContextChanged(editingContext());
    emitSelectionAvailability();
}

void CanvasWidget::translateSelectedAnnotation(const QPointF& delta) {
    if (!hasSelection()) {
        return;
    }

    QPointF boundedDelta = delta;
    for (int index : m_selectedAnnotationIndices) {
        const Annotation& annotation = m_annotations.at(index);
        boundedDelta.setX(qBound(-annotationBounds(annotation).left(), boundedDelta.x(), static_cast<qreal>(m_documentImage.width()) - annotationBounds(annotation).right()));
        boundedDelta.setY(qBound(-annotationBounds(annotation).top(), boundedDelta.y(), static_cast<qreal>(m_documentImage.height()) - annotationBounds(annotation).bottom()));
    }

    if (qFuzzyIsNull(boundedDelta.x()) && qFuzzyIsNull(boundedDelta.y())) {
        return;
    }

    if (!m_isMovingSelection) {
        pushUndoState();
        m_isMovingSelection = true;
    }

    for (int index : m_selectedAnnotationIndices) {
        translateAnnotation(m_annotations[index], boundedDelta);
    }
}

void CanvasWidget::resizeSelectedAnnotation(ResizeHandle handle, const QPointF& imagePoint) {
    if (m_primarySelectedAnnotationIndex < 0 || m_primarySelectedAnnotationIndex >= m_annotations.size()) {
        return;
    }

    Annotation& annotation = m_annotations[m_primarySelectedAnnotationIndex];
    if (!m_isResizingSelection) {
        pushUndoState();
        m_isResizingSelection = true;
    }

    const QPointF point(qBound(0.0, imagePoint.x(), static_cast<qreal>(m_documentImage.width())), qBound(0.0, imagePoint.y(), static_cast<qreal>(m_documentImage.height())));
    switch (handle) {
    case ResizeHandle::LineStart:
        annotation.start = point;
        break;
    case ResizeHandle::LineEnd:
        annotation.end = point;
        break;
    case ResizeHandle::TopLeft:
        annotation.start = point;
        break;
    case ResizeHandle::TopRight:
        annotation.start.setY(point.y());
        annotation.end.setX(point.x());
        break;
    case ResizeHandle::BottomLeft:
        annotation.start.setX(point.x());
        annotation.end.setY(point.y());
        break;
    case ResizeHandle::BottomRight:
        annotation.end = point;
        break;
    case ResizeHandle::None:
        break;
    }

    if (annotation.type == AnnotationType::Freehand) {
        const QRectF currentBounds = annotationBounds(annotation);
        QRectF targetBounds = currentBounds;
        switch (handle) {
        case ResizeHandle::TopLeft:
            targetBounds.setTopLeft(point);
            break;
        case ResizeHandle::TopRight:
            targetBounds.setTop(point.y());
            targetBounds.setRight(point.x());
            break;
        case ResizeHandle::BottomLeft:
            targetBounds.setLeft(point.x());
            targetBounds.setBottom(point.y());
            break;
        case ResizeHandle::BottomRight:
            targetBounds.setBottomRight(point);
            break;
        default:
            break;
        }
        targetBounds = targetBounds.normalized();
        if (targetBounds.width() >= 1.0 && targetBounds.height() >= 1.0) {
            scaleAnnotationBounds(annotation, currentBounds, targetBounds);
        }
    }
}

void CanvasWidget::removeSelectedAnnotation() {
    if (!hasSelection()) {
        return;
    }

    pushUndoState();
    QList<int> indices = m_selectedAnnotationIndices.values();
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (int index : indices) {
        m_annotations.removeAt(index);
    }
    clearSelection();
    updateUndoRedoState();
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Object deleted."));
}

void CanvasWidget::applyCrop(const QRectF& cropRect) {
    if (!hasDocument()) {
        return;
    }

    pushUndoState();
    const QRect sourceRect = cropRect.toAlignedRect().intersected(QRect(0, 0, m_documentImage.width(), m_documentImage.height()));
    m_documentImage = m_documentImage.copy(sourceRect);

    QVector<Annotation> croppedAnnotations;
    croppedAnnotations.reserve(m_annotations.size());
    for (Annotation annotation : m_annotations) {
        const QRectF bounds = annotationBounds(annotation);
        if (!bounds.intersects(cropRect)) {
            continue;
        }

        translateAnnotation(annotation, -cropRect.topLeft());
        if (annotation.type == AnnotationType::Rectangle || annotation.type == AnnotationType::Ellipse) {
            const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
            annotation.start = rect.topLeft();
            annotation.end = rect.bottomRight();
        }
        croppedAnnotations.push_back(annotation);
    }

    m_annotations = croppedAnnotations;
    clearSelection();
    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Image cropped."));
}

void CanvasWidget::applyHorizontalCrop(const QRectF& cropRect) {
    if (!hasDocument()) {
        return;
    }

    pushUndoState();
    const QRect sourceRect = cropRect.toAlignedRect().intersected(m_documentImage.rect());
    if (sourceRect.isEmpty() || sourceRect.height() >= m_documentImage.height()) {
        return;
    }

    QImage newImage(m_documentImage.width(), m_documentImage.height() - sourceRect.height(), QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(QRect(0, 0, m_documentImage.width(), sourceRect.top()), m_documentImage, QRect(0, 0, m_documentImage.width(), sourceRect.top()));
    painter.drawImage(QRect(0, sourceRect.top(), m_documentImage.width(), m_documentImage.height() - sourceRect.bottom() - 1),
        m_documentImage,
        QRect(0, sourceRect.bottom() + 1, m_documentImage.width(), m_documentImage.height() - sourceRect.bottom() - 1));

    m_documentImage = newImage;
    QVector<Annotation> transformed;
    transformed.reserve(m_annotations.size());
    for (Annotation annotation : m_annotations) {
        const QRectF bounds = annotationBounds(annotation);
        if (bounds.intersects(cropRect)) {
            continue;
        }
        if (bounds.top() >= cropRect.bottom()) {
            translateAnnotation(annotation, QPointF(0.0, -sourceRect.height()));
        }
        transformed.push_back(annotation);
    }
    m_annotations = transformed;
    clearSelection();
    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Horizontal crop applied."));
}

void CanvasWidget::applyVerticalCrop(const QRectF& cropRect) {
    if (!hasDocument()) {
        return;
    }

    pushUndoState();
    const QRect sourceRect = cropRect.toAlignedRect().intersected(m_documentImage.rect());
    if (sourceRect.isEmpty() || sourceRect.width() >= m_documentImage.width()) {
        return;
    }

    QImage newImage(m_documentImage.width() - sourceRect.width(), m_documentImage.height(), QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(QRect(0, 0, sourceRect.left(), m_documentImage.height()), m_documentImage, QRect(0, 0, sourceRect.left(), m_documentImage.height()));
    painter.drawImage(QRect(sourceRect.left(), 0, m_documentImage.width() - sourceRect.right() - 1, m_documentImage.height()),
        m_documentImage,
        QRect(sourceRect.right() + 1, 0, m_documentImage.width() - sourceRect.right() - 1, m_documentImage.height()));

    m_documentImage = newImage;
    QVector<Annotation> transformed;
    transformed.reserve(m_annotations.size());
    for (Annotation annotation : m_annotations) {
        const QRectF bounds = annotationBounds(annotation);
        if (bounds.intersects(cropRect)) {
            continue;
        }
        if (bounds.left() >= cropRect.right()) {
            translateAnnotation(annotation, QPointF(-sourceRect.width(), 0.0));
        }
        transformed.push_back(annotation);
    }
    m_annotations = transformed;
    clearSelection();
    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(QStringLiteral("Vertical crop applied."));
}

void CanvasWidget::applyCropMode(const QRectF& cropRect) {
    switch (m_cropMode) {
    case CropMode::Horizontal:
        applyHorizontalCrop(cropRect);
        break;
    case CropMode::Vertical:
        applyVerticalCrop(cropRect);
        break;
    case CropMode::AutoCrop:
    case CropMode::Default:
        applyCrop(cropRect);
        break;
    }
}

void CanvasWidget::applyWholeImageEffect(const QImage& newImage, const QPointF& translation, const QString& statusMessage) {
    if (!hasDocument() || newImage.isNull()) {
        return;
    }

    pushUndoState();
    m_documentImage = newImage;
    if (!qFuzzyIsNull(translation.x()) || !qFuzzyIsNull(translation.y())) {
        for (Annotation& annotation : m_annotations) {
            translateAnnotation(annotation, translation);
        }
    }
    clearSelection();
    updateCanvasSize();
    updateUndoRedoState();
    Q_EMIT documentSizeChanged(documentSize());
    update();
    Q_EMIT statusMessageChanged(statusMessage);
}

void CanvasWidget::pushUndoState() {
    m_undoStates.push_back(captureDocumentState());
    m_redoStates.clear();
    if (!m_modified) {
        m_modified = true;
        Q_EMIT modifiedChanged(true);
    }
    updateUndoRedoState();
}

void CanvasWidget::commitAnnotation(const Annotation& annotation) {
    pushUndoState();
    Annotation committed = annotation;
    if (committed.type == AnnotationType::StepLabel && committed.stepSequence < 0) {
        committed.stepSequence = nextStepSequence();
    }
    if (committed.type == AnnotationType::Pixelate && committed.effectSeed == 0) {
        committed.effectSeed = QRandomGenerator::global()->generate();
    }
    m_annotations.push_back(committed);
    updateUndoRedoState();
}

QImage CanvasWidget::applyWholeImageGrayscale(const QImage& image) {
    return image.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage CanvasWidget::applyWholeImageInvert(const QImage& image) {
    QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    result.invertPixels(QImage::InvertRgb);
    return result;
}

QImage CanvasWidget::applyWholeImageBorder(const QImage& image, int width, const QColor& color) {
    const int borderWidth = qMax(1, width);
    QImage result(image.size() + QSize(borderWidth * 2, borderWidth * 2), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.fillRect(result.rect(), color);
    painter.drawImage(borderWidth, borderWidth, image);
    return result;
}

QImage CanvasWidget::applyWholeImageRemoveTransparency(const QImage& image, const QColor& color) {
    QImage result(image.size(), QImage::Format_RGB32);
    result.fill(color);
    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    return result;
}

QImage CanvasWidget::applyWholeImageDropShadow(const QImage& image, int shadowSize, const QPoint& offset, qreal darkness) {
    const int padding = qMax(6, shadowSize * 2);
    const int leftPadding = padding + qMax(0, -offset.x());
    const int topPadding = padding + qMax(0, -offset.y());
    const int rightPadding = padding + qMax(0, offset.x());
    const int bottomPadding = padding + qMax(0, offset.y());

    QImage result(image.size() + QSize(leftPadding + rightPadding, topPadding + bottomPadding), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QImage shadow(image.size(), QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    {
        QPainter shadowPainter(&shadow);
        shadowPainter.drawImage(0, 0, image);
        shadowPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        shadowPainter.fillRect(shadow.rect(), QColor(0, 0, 0, qBound(0, qRound(darkness * 255.0), 255)));
    }

    QPainter painter(&result);
    for (int blurY = -shadowSize; blurY <= shadowSize; ++blurY) {
        for (int blurX = -shadowSize; blurX <= shadowSize; ++blurX) {
            const qreal distance = qSqrt((blurX * blurX) + (blurY * blurY));
            if (distance > shadowSize) {
                continue;
            }
            painter.setOpacity(qMax(0.02, 0.16 * (1.0 - (distance / qMax(1, shadowSize)))));
            painter.drawImage(leftPadding + offset.x() + blurX, topPadding + offset.y() + blurY, shadow);
        }
    }
    painter.setOpacity(1.0);
    painter.drawImage(leftPadding, topPadding, image);
    return result;
}

QImage CanvasWidget::applyWholeImageTornEdges(const QImage& image, int toothHeight, int horizontalRange, int verticalRange, bool withShadow) {
    const int padding = withShadow ? qMax(12, toothHeight) : toothHeight;
    QImage canvas(image.size() + QSize(padding * 2, padding * 2), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainterPath tornPath = createTornEdgePath(QRectF(padding, padding, image.width(), image.height()), toothHeight, horizontalRange, verticalRange, *QRandomGenerator::global());
    {
        QPainter painter(&canvas);
        if (withShadow) {
            QPainterPath shadowPath = tornPath.translated(5.0, 5.0);
            painter.fillPath(shadowPath, QColor(0, 0, 0, 90));
        }
        painter.setClipPath(tornPath);
        painter.drawImage(padding, padding, image);
    }
    return canvas;
}

void CanvasWidget::updateUndoRedoState() {
    Q_EMIT undoAvailableChanged(canUndo());
    Q_EMIT redoAvailableChanged(canRedo());
}

void CanvasWidget::drawAnnotation(QPainter& painter, const Annotation& annotation, double scale) const {
    QPen pen(annotation.strokeColor);
    pen.setWidth(qMax(1, qRound(annotation.strokeWidth * scale)));
    painter.setPen(pen);

    const QPointF scaledStart(annotation.start.x() * scale, annotation.start.y() * scale);
    const QPointF scaledEnd(annotation.end.x() * scale, annotation.end.y() * scale);

    if (annotation.type == AnnotationType::Text || annotation.type == AnnotationType::Emoji) {
        painter.setFont(annotation.font);
        const QRectF textBox(annotation.start.x() * scale,
            annotation.start.y() * scale,
            qMax<qreal>(1.0, (annotation.end.x() - annotation.start.x()) * scale),
            qMax<qreal>(1.0, (annotation.end.y() - annotation.start.y()) * scale));
        if (annotation.shadowEnabled) {
            painter.setPen(QColor(0, 0, 0, 120));
            painter.drawText(textBox.translated(2.0, 2.0), annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        }
        painter.setPen(annotation.strokeColor);
        painter.drawText(textBox, annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        return;
    }

    if (annotation.type == AnnotationType::TextHighlight) {
        painter.setFont(annotation.font);
        QFontMetricsF metrics(annotation.font);
        QRectF textRect = metrics.boundingRect(QRectF(annotation.start, QSizeF(10000.0, 10000.0)), Qt::TextWordWrap, annotation.text);
        textRect = textRect.translated(annotation.start - textRect.topLeft());
        const QRectF scaledTextRect(textRect.left() * scale, textRect.top() * scale, textRect.width() * scale, textRect.height() * scale);
        QColor highlight = annotation.fillColor;
        highlight.setAlpha(160);
        painter.fillRect(scaledTextRect.adjusted(-2, -1, 2, 1), highlight);
        if (annotation.shadowEnabled) {
            painter.setPen(QColor(0, 0, 0, 120));
            painter.drawText(scaledTextRect.translated(2.0, 2.0), annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        }
        painter.setPen(annotation.strokeColor);
        painter.drawText(scaledTextRect, annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        return;
    }

    if (annotation.type == AnnotationType::SpeechBubble) {
        const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
        const QRectF scaledRect(rect.left() * scale, rect.top() * scale, rect.width() * scale, rect.height() * scale);
        painter.setBrush(annotation.fillColor);
        painter.setPen(QPen(annotation.strokeColor, qMax(1, qRound(annotation.strokeWidth * scale))));
        painter.drawRoundedRect(scaledRect, 12.0, 12.0);
        const QPointF tailBase((rect.left() + 30.0) * scale, rect.bottom() * scale);
        QPolygonF tail;
        tail << tailBase << QPointF(tailBase.x() + 20.0, tailBase.y()) << QPointF((rect.left() + 8.0) * scale, (rect.bottom() + 18.0) * scale);
        painter.drawPolygon(tail);
        if (annotation.shadowEnabled) {
            painter.setPen(QColor(0, 0, 0, 120));
            painter.drawText(scaledRect.adjusted(12.0, 12.0, -8.0, -8.0), annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        }
        painter.setPen(QPen(annotation.strokeColor));
        painter.setFont(annotation.font);
        painter.drawText(scaledRect.adjusted(10.0, 10.0, -10.0, -10.0), annotation.textHorizontalAlignment | annotation.textVerticalAlignment | Qt::TextWordWrap, annotation.text);
        return;
    }

    if (annotation.type == AnnotationType::StepLabel) {
        const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
        const QRectF scaledRect = ::scaledRect(rect, scale);
        const QString text = stepLabelText(annotation);
        painter.setBrush(annotation.fillColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(scaledRect);
        if (annotation.shadowEnabled) {
            painter.setPen(QColor(0, 0, 0, 120));
            painter.setFont(annotation.font);
            painter.drawText(scaledRect.translated(2.0, 2.0), Qt::AlignCenter, text);
        }
        painter.setPen(QPen(annotation.strokeColor));
        painter.setFont(annotation.font);
        painter.drawText(scaledRect, Qt::AlignCenter, text);
        return;
    }

    if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
        painter.drawLine(scaledStart, scaledEnd);

        if (annotation.type == AnnotationType::Arrow) {
            const QPointF direction = scaledEnd - scaledStart;
            const qreal length = qSqrt((direction.x() * direction.x()) + (direction.y() * direction.y()));
            if (length > 0.0) {
                const QPointF unit(direction.x() / length, direction.y() / length);
                const QPointF normal(-unit.y(), unit.x());
                const qreal headLength = qMax<qreal>(10.0, annotation.strokeWidth * scale * 3.5);
                const qreal headWidth = qMax<qreal>(6.0, annotation.strokeWidth * scale * 2.0);

                auto drawArrowHead = [&](const QPointF& tip, const QPointF& directionUnit) {
                    const QPointF headBase = tip - (directionUnit * headLength);
                    const QPointF left = headBase + (QPointF(-directionUnit.y(), directionUnit.x()) * headWidth);
                    const QPointF right = headBase - (QPointF(-directionUnit.y(), directionUnit.x()) * headWidth);
                    QPolygonF arrowHead;
                    arrowHead << tip << left << right;
                    painter.setBrush(annotation.strokeColor);
                    painter.drawPolygon(arrowHead);
                };

                if (annotation.arrowHeadMode == ArrowHeadMode::End || annotation.arrowHeadMode == ArrowHeadMode::Both) {
                    drawArrowHead(scaledEnd, unit);
                }
                if (annotation.arrowHeadMode == ArrowHeadMode::Start || annotation.arrowHeadMode == ArrowHeadMode::Both) {
                    drawArrowHead(scaledStart, -unit);
                }
            }
        }

        return;
    }

    if (annotation.type == AnnotationType::Freehand) {
        if (annotation.points.size() < 2) {
            return;
        }

        QPainterPath path;
        path.moveTo(annotation.points.first().x() * scale, annotation.points.first().y() * scale);
        for (int index = 1; index < annotation.points.size(); ++index) {
            const QPointF& point = annotation.points.at(index);
            path.lineTo(point.x() * scale, point.y() * scale);
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        return;
    }

    const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
    const QRectF scaledRect(rect.left() * scale,
        rect.top() * scale,
        rect.width() * scale,
        rect.height() * scale);

    if (annotation.type == AnnotationType::Pixelate || annotation.type == AnnotationType::Blur || annotation.type == AnnotationType::Grayscale || annotation.type == AnnotationType::Magnify) {
        const QRect sourceRect = rect.toAlignedRect().intersected(QRect(0, 0, m_documentImage.width(), m_documentImage.height()));
        if (!sourceRect.isEmpty()) {
            QImage cropped;
            if (annotation.type == AnnotationType::Pixelate) {
                cropped = createRedactionFill(m_documentImage, sourceRect, annotation.effectSeed);
            } else if (annotation.type == AnnotationType::Blur) {
                cropped = m_documentImage.copy(sourceRect);
                const int blurFactor = qMax(2, annotation.blurRadius);
                const QSize smallSize(qMax(1, cropped.width() / blurFactor), qMax(1, cropped.height() / blurFactor));
                cropped = cropped.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                cropped = cropped.scaled(sourceRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            } else if (annotation.type == AnnotationType::Grayscale) {
                cropped = m_documentImage.copy(sourceRect);
                cropped = cropped.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
            } else {
                cropped = m_documentImage.copy(sourceRect);
                const QSize zoomedSize(qMax(1, sourceRect.width() * annotation.magnificationFactor), qMax(1, sourceRect.height() * annotation.magnificationFactor));
                cropped = cropped.scaled(zoomedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                const QRect centerCrop((cropped.width() - sourceRect.width()) / 2, (cropped.height() - sourceRect.height()) / 2, sourceRect.width(), sourceRect.height());
                cropped = cropped.copy(centerCrop.intersected(cropped.rect()));
            }
            painter.drawImage(scaledRect, cropped);
            if (annotation.strokeWidth > 0) {
                painter.setPen(QPen(annotation.strokeColor, qMax(1, qRound(annotation.strokeWidth * scale))));
                painter.drawRect(scaledRect);
            }
        }
        return;
    }

    if (annotation.type == AnnotationType::Highlight) {
        QColor highlight = QColor(QStringLiteral("#ffe96b"));
        highlight.setAlpha(120);
        painter.fillRect(scaledRect, highlight);
        if (annotation.strokeWidth > 0) {
            painter.setPen(QPen(QColor(QStringLiteral("#d4b100")), qMax(1, qRound(annotation.strokeWidth * scale))));
            painter.drawRect(scaledRect);
        }
        return;
    }

    QColor fill = annotation.fillColor;
    fill.setAlpha(110);
    painter.setBrush(fill);

    if (annotation.type == AnnotationType::Rectangle) {
        painter.drawRect(scaledRect);
    } else {
        painter.drawEllipse(scaledRect);
    }

    painter.setBrush(Qt::NoBrush);
}

void CanvasWidget::drawSelection(QPainter& painter, const Annotation& annotation, double scale) const {
    QPen selectionPen(QColor(QStringLiteral("#2c7be5")));
    selectionPen.setStyle(Qt::DashLine);
    selectionPen.setWidth(1);
    painter.setPen(selectionPen);
    painter.setBrush(Qt::NoBrush);

    const QRectF bounds = annotationBounds(annotation);
    const QRectF scaledBounds(bounds.left() * scale,
        bounds.top() * scale,
        bounds.width() * scale,
        bounds.height() * scale);
    painter.drawRect(scaledBounds);
}

void CanvasWidget::drawResizeHandles(QPainter& painter, const Annotation& annotation, double scale) const {
    if (annotation.type == AnnotationType::Text) {
        return;
    }

    painter.setPen(QPen(QColor(QStringLiteral("#2c7be5"))));
    painter.setBrush(QColor(QStringLiteral("#ffffff")));

    auto drawHandle = [&](const QPointF& point) {
        const QRectF handleRect((point.x() * scale) - (kHandleSize / 2.0), (point.y() * scale) - (kHandleSize / 2.0), kHandleSize, kHandleSize);
        painter.drawRect(handleRect);
    };

    if (annotation.type == AnnotationType::Line || annotation.type == AnnotationType::Arrow) {
        drawHandle(annotation.start);
        drawHandle(annotation.end);
        return;
    }

    const QRectF rect = normalizedClampedRect(annotation.start, annotation.end);
    drawHandle(rect.topLeft());
    drawHandle(rect.topRight());
    drawHandle(rect.bottomLeft());
    drawHandle(rect.bottomRight());
}

void CanvasWidget::drawPendingCrop(QPainter& painter, double scale) const {
    if (!m_hasPendingCrop) {
        return;
    }

    const QRectF scaledRect(m_pendingCropRect.left() * scale,
        m_pendingCropRect.top() * scale,
        m_pendingCropRect.width() * scale,
        m_pendingCropRect.height() * scale);
    QPen pen(QColor(QStringLiteral("#2c7be5")));
    pen.setStyle(Qt::DashLine);
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(scaledRect);
}

void CanvasWidget::drawPlaceholder(QPainter& painter) const {
    painter.fillRect(rect(), Qt::white);

    QFont titleFont = painter.font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);

    painter.setPen(QColor(QStringLiteral("#222222")));
    painter.setFont(titleFont);
    painter.drawText(rect().adjusted(0, -22, 0, 0), Qt::AlignCenter, QStringLiteral("Blueshot Editor for Linux"));

    QFont bodyFont = painter.font();
    bodyFont.setPointSize(10);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor(QStringLiteral("#555555")));
    painter.drawText(rect().adjusted(0, 24, 0, 0), Qt::AlignCenter, QStringLiteral("Open an image to start editing."));
}
