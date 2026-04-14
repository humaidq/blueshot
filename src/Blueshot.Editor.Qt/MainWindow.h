#pragma once

#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QString>

class QAction;
class QActionGroup;
class CanvasWidget;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QObject;
class QScrollArea;
class QSpinBox;
class QToolBar;
class QToolButton;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& initialFilePath = QString(), QWidget* parent = nullptr);

private Q_SLOTS:
    void openImage();
    void saveImage();
    void saveImageAs();
    void closeImage();
    void resizeImage();
    void rotateImageClockwise();
    void rotateImageCounterClockwise();
    void copyToClipboard();
    void duplicateSelection();
    void bringSelectionToFront();
    void sendSelectionToBack();
    void selectAllObjects();
    void selectTool();
    void zoomByStep(int direction);
    void setZoomPreset(int index);
    void setActualSize();
    void setBestFit();
    void setStrokeWidthPreset(int index);
    void setFontFamilyPreset(int index);
    void setFontSizePreset(int index);
    void setBoldText(bool checked);
    void setItalicText(bool checked);
    void selectHorizontalAlignmentAction();
    void selectVerticalAlignmentAction();
    void setArrowHeadPreset(int index);
    void setPixelSizePreset(int index);
    void setBlurRadiusPreset(int index);
    void setMagnificationFactorPreset(int index);
    void setShadowEnabled(bool checked);
    void setCounterStartValue(int value);
    void setCropModePreset(int index);
    void triggerUndo();
    void triggerRedo();
    void cutSelection();
    void copySelection();
    void pasteSelection();
    void confirmPendingAction();
    void cancelPendingAction();
    void activateToolFromAction();
    void showAboutDialog();
    void updateUndoAvailability(bool available);
    void updateRedoAvailability(bool available);
    void updateSelectionAvailability(bool available);
    void updateClipboardAvailability(bool available);
    void updateDocumentSize(const QSize& size);
    void updateModifiedState(bool modified);
    void applyBorderEffect();
    void applyGrayscaleEffect();
    void applyInvertEffect();
    void applyRemoveTransparencyEffect();
    void applyDropShadowEffect();
    void applyTornEdgesEffect();
    void chooseFillColor();
    void chooseLineColor();
    void selectEmojiFromToolbar();
    void emojiTextEditingFinished();

private:
    void syncCanvasWorkspaceSize();
    void closeEvent(QCloseEvent* event) override;
    void createMenus();
    void createToolBars();
    void createCentralLayout();
    void createStatusBar();
    void resetCanvas();
    bool loadImageFromPath(const QString& filePath);
    bool saveImageToPath(const QString& filePath);
    void setStatusMessage(const QString& message);
    void setZoomFactor(double zoomFactor);
    void updateColorButton(QToolButton* button, const QColor& color, const QString& iconPath);
    void updateEmojiButton();
    void updateAlignmentButtons();
    void refreshPropertyVisibility(const QString& context);
    void syncPropertyControlsFromCanvas();
    bool confirmCloseWithUnsavedChanges();
    void updateWindowTitle();
    QString chooseEmoji(const QString& initialEmoji);
    bool tryOpenNativeEmojiPicker(QWidget* targetWidget);
    QAction* addToolAction(QActionGroup* group, QToolBar* toolbar, const QString& iconPath, const QString& toolName, bool checked = false);
    void activateToolByName(const QString& toolName);
    QAction* addToolbarButton(QToolBar* toolbar, const QString& iconPath, const QString& text, const QObject* receiver, const char* member, bool enabled = true);

    CanvasWidget* m_canvasWidget = nullptr;
    QLabel* m_dimensionsLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_fontLabel = nullptr;
    QLabel* m_arrowHeadLabel = nullptr;
    QLabel* m_pixelSizeLabel = nullptr;
    QLabel* m_blurRadiusLabel = nullptr;
    QLabel* m_magnificationFactorLabel = nullptr;
    QLabel* m_counterLabel = nullptr;
    QLabel* m_cropModeLabel = nullptr;
    QComboBox* m_zoomComboBox = nullptr;
    QComboBox* m_lineWidthComboBox = nullptr;
    QComboBox* m_fontFamilyComboBox = nullptr;
    QComboBox* m_fontSizeComboBox = nullptr;
    QLineEdit* m_emojiLineEdit = nullptr;
    QComboBox* m_arrowHeadComboBox = nullptr;
    QComboBox* m_pixelSizeComboBox = nullptr;
    QComboBox* m_blurRadiusComboBox = nullptr;
    QComboBox* m_magnificationFactorComboBox = nullptr;
    QComboBox* m_cropModeComboBox = nullptr;
    QSpinBox* m_counterSpinBox = nullptr;
    QToolButton* m_fillColorButton = nullptr;
    QToolButton* m_lineColorButton = nullptr;
    QToolButton* m_horizontalAlignmentButton = nullptr;
    QToolButton* m_verticalAlignmentButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_canvasWorkspace = nullptr;
    QToolBar* m_propertiesToolbar = nullptr;
    QAction* m_fillColorAction = nullptr;
    QAction* m_lineColorAction = nullptr;
    QAction* m_lineWidthAction = nullptr;
    QAction* m_fontLabelAction = nullptr;
    QAction* m_fontFamilyAction = nullptr;
    QAction* m_fontSizeAction = nullptr;
    QAction* m_emojiAction = nullptr;
    QAction* m_emojiTextAction = nullptr;
    QAction* m_horizontalAlignmentAction = nullptr;
    QAction* m_verticalAlignmentAction = nullptr;
    QAction* m_arrowHeadLabelAction = nullptr;
    QAction* m_arrowHeadAction = nullptr;
    QAction* m_pixelSizeLabelAction = nullptr;
    QAction* m_pixelSizeAction = nullptr;
    QAction* m_blurRadiusLabelAction = nullptr;
    QAction* m_blurRadiusAction = nullptr;
    QAction* m_magnificationFactorLabelAction = nullptr;
    QAction* m_magnificationFactorAction = nullptr;
    QAction* m_counterLabelAction = nullptr;
    QAction* m_counterAction = nullptr;
    QAction* m_cropModeLabelAction = nullptr;
    QAction* m_cropModeAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_undoToolbarAction = nullptr;
    QAction* m_redoToolbarAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_saveAsToolbarAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_cutToolbarAction = nullptr;
    QAction* m_copyToolbarAction = nullptr;
    QAction* m_pasteToolbarAction = nullptr;
    QAction* m_boldAction = nullptr;
    QAction* m_italicAction = nullptr;
    QAction* m_shadowAction = nullptr;
    QAction* m_confirmAction = nullptr;
    QAction* m_cancelAction = nullptr;
    QToolButton* m_emojiButton = nullptr;
    QHash<QString, QAction*> m_toolActions;
    QString m_currentFilePath;
    double m_zoomFactor = 1.0;
    bool m_isModified = false;
    bool m_syncingPropertyControls = false;
};
