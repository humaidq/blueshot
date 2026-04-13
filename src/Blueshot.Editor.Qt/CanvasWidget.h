#pragma once

#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QSet>
#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtGui/QFont>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class CropMode {
        Default,
        AutoCrop,
        Vertical,
        Horizontal,
    };

    enum class ArrowHeadMode {
        End,
        Start,
        Both,
        None,
    };

    enum class AnnotationType {
        Rectangle,
        Ellipse,
        Line,
        Arrow,
        Freehand,
        Text,
        Emoji,
        TextHighlight,
        Highlight,
        Pixelate,
        Blur,
        Grayscale,
        Magnify,
        SpeechBubble,
        StepLabel,
    };

    explicit CanvasWidget(QWidget* parent = nullptr);

    void setDocumentImage(const QImage& image);
    void clearDocument();
    [[nodiscard]] bool hasDocument() const;
    [[nodiscard]] QSize documentSize() const;
    [[nodiscard]] QImage renderDocumentImage() const;

    void setZoomFactor(double zoomFactor);
    [[nodiscard]] double zoomFactor() const;

    void setActiveTool(const QString& toolName);
    [[nodiscard]] QString activeTool() const;

    void setStrokeColor(const QColor& color);
    void setFillColor(const QColor& color);
    void setStrokeWidth(int width);
    void setTextFont(const QFont& font);
    void setTextHorizontalAlignment(Qt::Alignment alignment);
    void setTextVerticalAlignment(Qt::Alignment alignment);
    void setArrowHeadMode(ArrowHeadMode mode);
    void setPixelSize(int size);
    void setBlurRadius(int radius);
    void setMagnificationFactor(int factor);
    void setShadowEnabled(bool enabled);
    void setCounterStart(int value);
    [[nodiscard]] int counterStart() const;
    void setCropMode(CropMode mode);
    [[nodiscard]] CropMode cropMode() const;
    void resizeDocumentImage(const QSize& newSize);
    void applyWholeImageEffect(const QImage& newImage, const QPointF& translation, const QString& statusMessage);
    static QImage applyWholeImageGrayscale(const QImage& image);
    static QImage applyWholeImageInvert(const QImage& image);
    static QImage applyWholeImageBorder(const QImage& image, int width, const QColor& color);
    static QImage applyWholeImageRemoveTransparency(const QImage& image, const QColor& color);
    static QImage applyWholeImageDropShadow(const QImage& image, int shadowSize, const QPoint& offset, qreal darkness);
    static QImage applyWholeImageTornEdges(const QImage& image, int toothHeight, int horizontalRange, int verticalRange, bool withShadow);
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] bool canCopySelection() const;
    [[nodiscard]] bool canPasteSelection() const;
    [[nodiscard]] bool isModified() const;
    void markSaved();
    [[nodiscard]] QColor currentStrokeColor() const;
    [[nodiscard]] QColor currentFillColor() const;
    [[nodiscard]] int currentStrokeWidth() const;
    [[nodiscard]] QFont currentTextFont() const;
    [[nodiscard]] Qt::Alignment currentTextHorizontalAlignment() const;
    [[nodiscard]] Qt::Alignment currentTextVerticalAlignment() const;
    [[nodiscard]] ArrowHeadMode currentArrowHeadMode() const;
    [[nodiscard]] int currentPixelSize() const;
    [[nodiscard]] int currentBlurRadius() const;
    [[nodiscard]] int currentMagnificationFactor() const;
    [[nodiscard]] bool currentShadowEnabled() const;

public Q_SLOTS:
    void undo();
    void redo();
    void rotateClockwise();
    void rotateCounterClockwise();
    void confirmPendingAction();
    void cancelPendingAction();
    void copySelection();
    void cutSelection();
    void pasteSelection();
    void selectAll();
    void duplicateSelection();
    void bringSelectionToFront();
    void sendSelectionToBack();

Q_SIGNALS:
    void statusMessageChanged(const QString& message);
    void undoAvailableChanged(bool available);
    void redoAvailableChanged(bool available);
    void documentSizeChanged(const QSize& size);
    void editingContextChanged(const QString& context);
    void selectionAvailabilityChanged(bool available);
    void clipboardAvailabilityChanged(bool available);
    void cropModeChanged(CanvasWidget::CropMode mode);
    void counterStartChanged(int value);
    void zoomStepRequested(int direction);
    void modifiedChanged(bool modified);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Annotation {
        AnnotationType type = AnnotationType::Rectangle;
        QPointF start;
        QPointF end;
        QColor strokeColor;
        QColor fillColor;
        int strokeWidth = 1;
        QString text;
        QFont font;
        QVector<QPointF> points;
        Qt::Alignment textHorizontalAlignment = Qt::AlignLeft;
        Qt::Alignment textVerticalAlignment = Qt::AlignTop;
        ArrowHeadMode arrowHeadMode = ArrowHeadMode::End;
        int pixelSize = 12;
        int blurRadius = 12;
        int magnificationFactor = 2;
        bool shadowEnabled = false;
        qint64 stepSequence = -1;
        quint32 effectSeed = 0;

        bool operator==(const Annotation& other) const = default;
    };

    struct DocumentState {
        QImage image;
        QVector<Annotation> annotations;

        bool operator==(const DocumentState& other) const = default;
    };

    struct ToolStyleState {
        QColor strokeColor = QColor(QStringLiteral("#cc2f2f"));
        QColor fillColor = QColor(QStringLiteral("#fff2cc"));
        int strokeWidth = 3;
        QFont font = QFont(QStringLiteral("Noto Sans"), 14);
        Qt::Alignment textHorizontalAlignment = Qt::AlignLeft;
        Qt::Alignment textVerticalAlignment = Qt::AlignTop;
        ArrowHeadMode arrowHeadMode = ArrowHeadMode::End;
        int pixelSize = 12;
        int blurRadius = 12;
        int magnificationFactor = 2;
        bool shadowEnabled = false;
    };

    enum class ResizeHandle {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        LineStart,
        LineEnd,
    };

    void updateCanvasSize();
    [[nodiscard]] QPointF toImagePoint(const QPointF& widgetPoint) const;
    [[nodiscard]] QRectF normalizedClampedRect(const QPointF& start, const QPointF& end) const;
    [[nodiscard]] QRectF textLayoutRect(const Annotation& annotation) const;
    [[nodiscard]] QRectF annotationBounds(const Annotation& annotation) const;
    [[nodiscard]] ResizeHandle hitTestResizeHandle(const QPointF& imagePoint) const;
    [[nodiscard]] QRectF cropRect() const;
    [[nodiscard]] QRectF effectiveCropRect() const;
    [[nodiscard]] AnnotationType annotationTypeForTool() const;
    [[nodiscard]] bool isDrawableTool() const;
    [[nodiscard]] bool annotationHasMinimumSize(const Annotation& annotation) const;
    [[nodiscard]] int hitTestAnnotation(const QPointF& imagePoint) const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] bool isSelected(int index) const;
    [[nodiscard]] bool isTextLike(const Annotation& annotation) const;
    [[nodiscard]] bool isAutoCropMode() const;
    [[nodiscard]] QString editingContext() const;
    [[nodiscard]] QString stepLabelText(const Annotation& annotation) const;
    [[nodiscard]] qint64 nextStepSequence();
    [[nodiscard]] QRectF computeAutoCropRect() const;
    void applyToolDefaults(const QString& toolName);
    void storeCurrentToolStyle();
    void loadToolStyle(const QString& toolName);
    [[nodiscard]] QByteArray serializeSelectedAnnotations() const;
    static Annotation deserializeAnnotation(const QJsonObject& object);
    static QJsonObject serializeAnnotation(const Annotation& annotation);
    void emitSelectionAvailability();
    void emitClipboardAvailability();
    void selectAnnotation(int index, bool additive = false);
    void clearSelection();
    void applyDocumentState(const DocumentState& state);
    [[nodiscard]] DocumentState captureDocumentState() const;
    void syncModifiedState();
    void transformAnnotation(Annotation& annotation, const QTransform& transform, bool scaleFont = false, qreal fontScale = 1.0) const;
    void translateAnnotation(Annotation& annotation, const QPointF& delta) const;
    void offsetAnnotation(Annotation& annotation, const QPointF& delta, bool regenerateStepSequence) const;
    void scaleAnnotationBounds(Annotation& annotation, const QRectF& sourceBounds, const QRectF& destinationBounds) const;
    void reassignClonedAnnotationMetadata(Annotation& annotation);
    bool translateSelectedAnnotation(const QPointF& delta, bool groupUndoWithInteraction = false);
    bool resizeSelectedAnnotation(ResizeHandle handle, const QPointF& imagePoint, bool groupUndoWithInteraction = false);
    void removeSelectedAnnotation();
    void applyCrop(const QRectF& cropRect);
    void applyCropMode(const QRectF& cropRect);
    void applyHorizontalCrop(const QRectF& cropRect);
    void applyVerticalCrop(const QRectF& cropRect);
    void pushUndoState();
    void commitAnnotation(const Annotation& annotation);
    void updateUndoRedoState();
    void drawAnnotation(QPainter& painter, const Annotation& annotation, double scale, const QImage* composedImage = nullptr) const;
    void drawSelection(QPainter& painter, const Annotation& annotation, double scale) const;
    void drawResizeHandles(QPainter& painter, const Annotation& annotation, double scale) const;
    void drawPendingCrop(QPainter& painter, double scale) const;
    void drawPlaceholder(QPainter& painter) const;

    QImage m_documentImage;
    QVector<Annotation> m_annotations;
    QVector<DocumentState> m_undoStates;
    QVector<DocumentState> m_redoStates;
    DocumentState m_savedState;
    QString m_activeTool = QStringLiteral("Cursor");
    QColor m_strokeColor = QColor(QStringLiteral("#cc2f2f"));
    QColor m_fillColor = QColor(QStringLiteral("#fff2cc"));
    int m_strokeWidth = 3;
    QFont m_textFont = QFont(QStringLiteral("Noto Sans"), 14);
    Qt::Alignment m_textHorizontalAlignment = Qt::AlignLeft;
    Qt::Alignment m_textVerticalAlignment = Qt::AlignTop;
    ArrowHeadMode m_arrowHeadMode = ArrowHeadMode::End;
    int m_pixelSize = 12;
    int m_blurRadius = 12;
    int m_magnificationFactor = 2;
    bool m_shadowEnabled = false;
    int m_counterStart = 1;
    CropMode m_cropMode = CropMode::Default;
    qint64 m_nextStepSequence = 1;
    bool m_modified = false;
    double m_zoomFactor = 1.0;
    QHash<QString, ToolStyleState> m_toolStyles;
    bool m_isDrawingAnnotation = false;
    bool m_isMovingSelection = false;
    bool m_isResizingSelection = false;
    bool m_hasPendingCrop = false;
    bool m_hasSavedState = false;
    bool m_moveUndoRecorded = false;
    bool m_resizeUndoRecorded = false;
    bool m_selectionMoved = false;
    bool m_selectionResized = false;
    int m_primarySelectedAnnotationIndex = -1;
    QSet<int> m_selectedAnnotationIndices;
    ResizeHandle m_activeResizeHandle = ResizeHandle::None;
    QPointF m_lastPointerImagePoint;
    Annotation m_previewAnnotation;
    QRectF m_pendingCropRect;
};
