#include "MainWindow.h"

#include "CanvasWidget.h"

#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtCore/QList>
#include <QtCore/QEvent>
#include <QtCore/QtMath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QColorDialog>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QFileDialog>
#include <QtGui/QClipboard>
#include <QtCore/QFileInfo>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtGui/QKeySequence>

namespace {
bool isTintMaskPixel(const QString& iconPath, int x, int y) {
    const bool isPencil = iconPath.endsWith(QStringLiteral("pencil-color.png"));
    const bool isBucket = iconPath.endsWith(QStringLiteral("paint-can-color.png"));
    if (!isPencil && !isBucket) {
        return false;
    }

    if (y == 13 || y == 14 || y == 15) {
        return true;
    }

    if (isPencil && y == 12) {
        return (x >= 0 && x <= 1) || (x >= 4 && x <= 15);
    }

    if (isBucket) {
        if (y == 12) {
            return (x >= 0 && x <= 4) || (x >= 9 && x <= 15);
        }
        if (y == 11) {
            return (x >= 0 && x <= 2) || x == 11 || x == 15;
        }
    }

    return false;
}

QIcon createTintedToolbarIcon(const QString& iconPath, const QColor& color) {
    QImage image(iconPath);
    if (image.isNull()) {
        return QIcon(iconPath);
    }

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* scanLine = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QColor pixel = QColor::fromRgba(scanLine[x]);
            if (pixel.alpha() == 0) {
                continue;
            }

            if (!isTintMaskPixel(iconPath, x, y)) {
                continue;
            }

            QColor tinted = color;
            tinted.setAlpha(pixel.alpha());
            tinted = tinted.lighter(qBound(90, pixel.lightness() + 10, 125));
            scanLine[x] = tinted.rgba();
        }
    }

    return QIcon(QPixmap::fromImage(image));
}
}

MainWindow::MainWindow(const QString& initialFilePath, QWidget* parent) : QMainWindow(parent) {
    resize(1180, 780);
    setMinimumSize(920, 640);
    setStyleSheet(QStringLiteral(R"(
QMainWindow {
    background: #f3f3f3;
}
QMenuBar, QToolBar, QStatusBar {
    background: #f7f7f7;
}
QToolBar {
    border: none;
    spacing: 2px;
    padding: 4px 6px;
}
QToolButton {
    background: #f6f6f6;
    border: 1px solid #c9c9c9;
    padding: 4px;
    margin: 1px;
}
QToolButton:hover {
    background: #ededed;
}
QToolButton:checked {
    background: #dcebfb;
    border: 1px solid #6da6e2;
}
QStatusBar {
    border-top: 1px solid #cfcfcf;
}
    )"));

    createMenus();
    createToolBars();
    createCentralLayout();
    createStatusBar();
    resetCanvas();

    if (!initialFilePath.isEmpty()) {
        loadImageFromPath(initialFilePath);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmCloseWithUnsavedChanges()) {
        event->ignore();
        return;
    }

    event->accept();
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* openAction = fileMenu->addAction(QStringLiteral("&Open Image..."), this, &MainWindow::openImage);
    openAction->setShortcut(QKeySequence::Open);

    m_saveAction = fileMenu->addAction(QStringLiteral("&Save Image"), this, &MainWindow::saveImage);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);

    m_saveAsAction = fileMenu->addAction(QStringLiteral("Save Image &As..."), this, &MainWindow::saveImageAs);
    m_saveAsAction->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Close Image"), this, &MainWindow::closeImage);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    m_undoAction = editMenu->addAction(QStringLiteral("Undo"), this, &MainWindow::triggerUndo);
    m_redoAction = editMenu->addAction(QStringLiteral("Redo"), this, &MainWindow::triggerRedo);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    editMenu->addSeparator();
    m_cutAction = editMenu->addAction(QStringLiteral("Cut"), this, &MainWindow::cutSelection);
    m_copyAction = editMenu->addAction(QStringLiteral("Copy"), this, &MainWindow::copySelection);
    m_pasteAction = editMenu->addAction(QStringLiteral("Paste"), this, &MainWindow::pasteSelection);
    m_cutAction->setEnabled(false);
    m_copyAction->setEnabled(false);
    m_pasteAction->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Select All"), this, &MainWindow::selectAllObjects);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Duplicate"), this, &MainWindow::duplicateSelection);
    editMenu->addAction(QStringLiteral("Bring To Front"), this, &MainWindow::bringSelectionToFront);
    editMenu->addAction(QStringLiteral("Send To Back"), this, &MainWindow::sendSelectionToBack);

    QMenu* objectMenu = menuBar()->addMenu(QStringLiteral("&Object"));
    QAction* rectangleAction = objectMenu->addAction(QStringLiteral("Add Rectangle"), this, &MainWindow::activateToolFromAction);
    rectangleAction->setData(QStringLiteral("Rectangle"));
    QAction* ellipseAction = objectMenu->addAction(QStringLiteral("Add Ellipse"), this, &MainWindow::activateToolFromAction);
    ellipseAction->setData(QStringLiteral("Ellipse"));
    QAction* lineAction = objectMenu->addAction(QStringLiteral("Draw Line"), this, &MainWindow::activateToolFromAction);
    lineAction->setData(QStringLiteral("Line"));
    QAction* arrowAction = objectMenu->addAction(QStringLiteral("Draw Arrow"), this, &MainWindow::activateToolFromAction);
    arrowAction->setData(QStringLiteral("Arrow"));
    QAction* freehandAction = objectMenu->addAction(QStringLiteral("Draw Freehand"), this, &MainWindow::activateToolFromAction);
    freehandAction->setData(QStringLiteral("Freehand"));
    QAction* textAction = objectMenu->addAction(QStringLiteral("Add Text Box"), this, &MainWindow::activateToolFromAction);
    textAction->setData(QStringLiteral("Text"));
    QAction* textHighlightAction = objectMenu->addAction(QStringLiteral("Add Highlighted Text"), this, &MainWindow::activateToolFromAction);
    textHighlightAction->setData(QStringLiteral("Text Highlight"));
    QAction* emojiAction = objectMenu->addAction(QStringLiteral("Add Emoji"), this, &MainWindow::activateToolFromAction);
    emojiAction->setData(QStringLiteral("Emoji"));
    QAction* blurAction = objectMenu->addAction(QStringLiteral("Blur Area"), this, &MainWindow::activateToolFromAction);
    blurAction->setData(QStringLiteral("Blur"));
    QAction* grayscaleAction = objectMenu->addAction(QStringLiteral("Grayscale Area"), this, &MainWindow::activateToolFromAction);
    grayscaleAction->setData(QStringLiteral("Grayscale"));
    QAction* magnifyAction = objectMenu->addAction(QStringLiteral("Magnify Area"), this, &MainWindow::activateToolFromAction);
    magnifyAction->setData(QStringLiteral("Magnify"));
    objectMenu->addSeparator();
    objectMenu->addAction(QStringLiteral("Crop Image"), this, &MainWindow::activateToolFromAction)->setData(QStringLiteral("Crop"));
    objectMenu->addAction(QStringLiteral("Resize Image"), this, &MainWindow::resizeImage);
    objectMenu->addAction(QStringLiteral("Rotate Clockwise"), this, &MainWindow::rotateImageClockwise);
    objectMenu->addAction(QStringLiteral("Rotate Counter Clockwise"), this, &MainWindow::rotateImageCounterClockwise);

    QMenu* effectsMenu = menuBar()->addMenu(QStringLiteral("E&ffects"));
    effectsMenu->addAction(QStringLiteral("Add Border"), this, &MainWindow::applyBorderEffect);
    effectsMenu->addAction(QStringLiteral("Drop Shadow"), this, &MainWindow::applyDropShadowEffect);
    effectsMenu->addAction(QStringLiteral("Torn Edges"), this, &MainWindow::applyTornEdgesEffect);
    effectsMenu->addSeparator();
    effectsMenu->addAction(QStringLiteral("Grayscale"), this, &MainWindow::applyGrayscaleEffect);
    effectsMenu->addAction(QStringLiteral("Invert"), this, &MainWindow::applyInvertEffect);
    effectsMenu->addAction(QStringLiteral("Remove Transparency"), this, &MainWindow::applyRemoveTransparencyEffect);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Zoom 100%"), this, &MainWindow::setActualSize);
    viewMenu->addAction(QStringLiteral("Best Fit"), this, &MainWindow::setBestFit);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("About"), this, &MainWindow::showAboutDialog);
}

QAction* MainWindow::addToolbarButton(QToolBar* toolbar, const QString& iconPath, const QString& text, const QObject* receiver, const char* member, bool enabled) {
    QAction* action = toolbar->addAction(QIcon(iconPath), text);
    QObject::connect(action, SIGNAL(triggered()), receiver, member);
    action->setEnabled(enabled);
    return action;
}

QAction* MainWindow::addToolAction(QActionGroup* group, QToolBar* toolbar, const QString& iconPath, const QString& toolName, bool checked) {
    QAction* action = toolbar->addAction(QIcon(iconPath), toolName);
    action->setCheckable(true);
    action->setData(toolName);
    action->setChecked(checked);
    action->setToolTip(toolName);
    group->addAction(action);
    QObject::connect(action, &QAction::triggered, this, &MainWindow::selectTool);
    m_toolActions.insert(toolName, action);
    return action;
}

void MainWindow::activateToolByName(const QString& toolName) {
    QAction* action = m_toolActions.value(toolName, nullptr);
    if (action == nullptr) {
        return;
    }

    if (toolName == QStringLiteral("Rotate clockwise")) {
        rotateImageClockwise();
        m_toolActions.value(QStringLiteral("Cursor"))->setChecked(true);
        m_canvasWidget->setActiveTool(QStringLiteral("Cursor"));
        return;
    }

    if (toolName == QStringLiteral("Rotate counter clockwise")) {
        rotateImageCounterClockwise();
        m_toolActions.value(QStringLiteral("Cursor"))->setChecked(true);
        m_canvasWidget->setActiveTool(QStringLiteral("Cursor"));
        return;
    }

    if (toolName == QStringLiteral("Resize")) {
        resizeImage();
        m_toolActions.value(QStringLiteral("Cursor"))->setChecked(true);
        m_canvasWidget->setActiveTool(QStringLiteral("Cursor"));
        return;
    }

    action->setChecked(true);
    m_canvasWidget->setActiveTool(toolName);
    setStatusMessage(QStringLiteral("%1 tool selected.").arg(toolName));
}

void MainWindow::createToolBars() {
    QToolBar* actionsToolbar = new QToolBar(QStringLiteral("Actions"), this);
    actionsToolbar->setMovable(false);
    actionsToolbar->setIconSize(QSize(16, 16));
    addToolBar(Qt::TopToolBarArea, actionsToolbar);

    addToolbarButton(actionsToolbar, QStringLiteral(":/win/folder-open-image.png"), QStringLiteral("Open"), this, SLOT(openImage()));
    m_saveAsToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/picture_save.png"), QStringLiteral("Save"), this, SLOT(saveImage()), false);
    addToolbarButton(actionsToolbar, QStringLiteral(":/win/picture_to_clipboard.png"), QStringLiteral("Clipboard"), this, SLOT(copyToClipboard()));
    actionsToolbar->addSeparator();
    m_cutToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/cut.png"), QStringLiteral("Cut"), this, SLOT(cutSelection()));
    m_copyToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/page_copy.png"), QStringLiteral("Copy"), this, SLOT(copySelection()));
    m_pasteToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/shape_paste.png"), QStringLiteral("Paste"), this, SLOT(pasteSelection()));
    m_cutToolbarAction->setEnabled(false);
    m_copyToolbarAction->setEnabled(false);
    m_pasteToolbarAction->setEnabled(false);
    m_undoToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/undo.png"), QStringLiteral("Undo"), this, SLOT(triggerUndo()));
    m_redoToolbarAction = addToolbarButton(actionsToolbar, QStringLiteral(":/win/redo.png"), QStringLiteral("Redo"), this, SLOT(triggerRedo()));
    m_undoToolbarAction->setEnabled(false);
    m_redoToolbarAction->setEnabled(false);
    actionsToolbar->addSeparator();
    addToolbarButton(actionsToolbar, QStringLiteral(":/win/help.png"), QStringLiteral("Help"), this, SLOT(showAboutDialog()));

    QToolBar* propertiesToolbar = new QToolBar(QStringLiteral("Properties"), this);
    propertiesToolbar->setMovable(false);
    propertiesToolbar->setIconSize(QSize(16, 16));
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, propertiesToolbar);

    m_fillColorButton = new QToolButton(this);
    m_fillColorButton->setToolTip(QStringLiteral("Fill color"));
    m_fillColorButton->setAutoRaise(false);
    QObject::connect(m_fillColorButton, &QToolButton::clicked, this, &MainWindow::chooseFillColor);
    updateColorButton(m_fillColorButton, QColor(QStringLiteral("#fff2cc")), QStringLiteral(":/win/fugue/paint-can-color.png"));
    m_fillColorAction = propertiesToolbar->addWidget(m_fillColorButton);

    m_lineColorButton = new QToolButton(this);
    m_lineColorButton->setToolTip(QStringLiteral("Line color"));
    m_lineColorButton->setAutoRaise(false);
    QObject::connect(m_lineColorButton, &QToolButton::clicked, this, &MainWindow::chooseLineColor);
    updateColorButton(m_lineColorButton, QColor(QStringLiteral("#cc2f2f")), QStringLiteral(":/win/fugue/pencil-color.png"));
    m_lineColorAction = propertiesToolbar->addWidget(m_lineColorButton);

    m_lineWidthComboBox = new QComboBox(this);
    m_lineWidthComboBox->addItems({QStringLiteral("0 px"), QStringLiteral("1 px"), QStringLiteral("2 px"), QStringLiteral("3 px"), QStringLiteral("4 px")});
    m_lineWidthComboBox->setCurrentText(QStringLiteral("3 px"));
    m_lineWidthComboBox->setFixedWidth(72);
    m_lineWidthAction = propertiesToolbar->addWidget(m_lineWidthComboBox);
    QObject::connect(m_lineWidthComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setStrokeWidthPreset);

    m_fontLabel = new QLabel(QStringLiteral("Font"), this);
    m_fontLabel->setContentsMargins(8, 0, 2, 0);
    m_fontLabelAction = propertiesToolbar->addWidget(m_fontLabel);

    m_fontFamilyComboBox = new QComboBox(this);
    m_fontFamilyComboBox->addItems({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Liberation Sans"), QStringLiteral("Arial")});
    m_fontFamilyComboBox->setCurrentText(QStringLiteral("Noto Sans"));
    m_fontFamilyComboBox->setFixedWidth(180);
    m_fontFamilyAction = propertiesToolbar->addWidget(m_fontFamilyComboBox);
    QObject::connect(m_fontFamilyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setFontFamilyPreset);

    m_fontSizeComboBox = new QComboBox(this);
    m_fontSizeComboBox->addItems({QStringLiteral("10 pt"), QStringLiteral("12 pt"), QStringLiteral("14 pt"), QStringLiteral("18 pt")});
    m_fontSizeComboBox->setCurrentText(QStringLiteral("14 pt"));
    m_fontSizeComboBox->setFixedWidth(76);
    m_fontSizeAction = propertiesToolbar->addWidget(m_fontSizeComboBox);
    QObject::connect(m_fontSizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setFontSizePreset);

    m_horizontalAlignmentButton = new QToolButton(this);
    m_horizontalAlignmentButton->setPopupMode(QToolButton::InstantPopup);
    m_horizontalAlignmentButton->setToolTip(QStringLiteral("Horizontal alignment"));
    QMenu* horizontalAlignmentMenu = new QMenu(m_horizontalAlignmentButton);
    QAction* alignLeftAction = horizontalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-alignment.png")), QStringLiteral("Align Left"), this, &MainWindow::selectHorizontalAlignmentAction);
    alignLeftAction->setData(static_cast<int>(Qt::AlignLeft));
    QAction* alignCenterAction = horizontalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-alignment-center.png")), QStringLiteral("Align Center"), this, &MainWindow::selectHorizontalAlignmentAction);
    alignCenterAction->setData(static_cast<int>(Qt::AlignHCenter));
    QAction* alignRightAction = horizontalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-alignment-right.png")), QStringLiteral("Align Right"), this, &MainWindow::selectHorizontalAlignmentAction);
    alignRightAction->setData(static_cast<int>(Qt::AlignRight));
    m_horizontalAlignmentButton->setMenu(horizontalAlignmentMenu);
    m_horizontalAlignmentAction = propertiesToolbar->addWidget(m_horizontalAlignmentButton);

    m_verticalAlignmentButton = new QToolButton(this);
    m_verticalAlignmentButton->setPopupMode(QToolButton::InstantPopup);
    m_verticalAlignmentButton->setToolTip(QStringLiteral("Vertical alignment"));
    QMenu* verticalAlignmentMenu = new QMenu(m_verticalAlignmentButton);
    QAction* alignTopAction = verticalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-vertical-alignment-top.png")), QStringLiteral("Align Top"), this, &MainWindow::selectVerticalAlignmentAction);
    alignTopAction->setData(static_cast<int>(Qt::AlignTop));
    QAction* alignMiddleAction = verticalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-vertical-alignment-middle.png")), QStringLiteral("Align Middle"), this, &MainWindow::selectVerticalAlignmentAction);
    alignMiddleAction->setData(static_cast<int>(Qt::AlignVCenter));
    QAction* alignBottomAction = verticalAlignmentMenu->addAction(QIcon(QStringLiteral(":/win/fugue/edit-vertical-alignment.png")), QStringLiteral("Align Bottom"), this, &MainWindow::selectVerticalAlignmentAction);
    alignBottomAction->setData(static_cast<int>(Qt::AlignBottom));
    m_verticalAlignmentButton->setMenu(verticalAlignmentMenu);
    m_verticalAlignmentAction = propertiesToolbar->addWidget(m_verticalAlignmentButton);

    m_arrowHeadLabel = new QLabel(QStringLiteral("Arrow"), this);
    m_arrowHeadLabel->setContentsMargins(8, 0, 2, 0);
    m_arrowHeadLabelAction = propertiesToolbar->addWidget(m_arrowHeadLabel);
    m_arrowHeadComboBox = new QComboBox(this);
    m_arrowHeadComboBox->addItems({QStringLiteral("End"), QStringLiteral("Start"), QStringLiteral("Both"), QStringLiteral("None")});
    m_arrowHeadComboBox->setCurrentIndex(0);
    m_arrowHeadComboBox->setFixedWidth(88);
    m_arrowHeadAction = propertiesToolbar->addWidget(m_arrowHeadComboBox);
    QObject::connect(m_arrowHeadComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setArrowHeadPreset);

    m_pixelSizeLabel = new QLabel(QStringLiteral("Pixel"), this);
    m_pixelSizeLabel->setContentsMargins(8, 0, 2, 0);
    m_pixelSizeLabelAction = propertiesToolbar->addWidget(m_pixelSizeLabel);
    m_pixelSizeComboBox = new QComboBox(this);
    m_pixelSizeComboBox->addItems({QStringLiteral("4 px"), QStringLiteral("8 px"), QStringLiteral("12 px"), QStringLiteral("16 px"), QStringLiteral("24 px")});
    m_pixelSizeComboBox->setCurrentText(QStringLiteral("12 px"));
    m_pixelSizeComboBox->setFixedWidth(88);
    m_pixelSizeAction = propertiesToolbar->addWidget(m_pixelSizeComboBox);
    QObject::connect(m_pixelSizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setPixelSizePreset);

    m_blurRadiusLabel = new QLabel(QStringLiteral("Blur"), this);
    m_blurRadiusLabel->setContentsMargins(8, 0, 2, 0);
    m_blurRadiusLabelAction = propertiesToolbar->addWidget(m_blurRadiusLabel);
    m_blurRadiusComboBox = new QComboBox(this);
    m_blurRadiusComboBox->addItems({QStringLiteral("4 px"), QStringLiteral("8 px"), QStringLiteral("12 px"), QStringLiteral("16 px"), QStringLiteral("24 px")});
    m_blurRadiusComboBox->setCurrentText(QStringLiteral("12 px"));
    m_blurRadiusComboBox->setFixedWidth(88);
    m_blurRadiusAction = propertiesToolbar->addWidget(m_blurRadiusComboBox);
    QObject::connect(m_blurRadiusComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setBlurRadiusPreset);

    m_magnificationFactorLabel = new QLabel(QStringLiteral("Magnify"), this);
    m_magnificationFactorLabel->setContentsMargins(8, 0, 2, 0);
    m_magnificationFactorLabelAction = propertiesToolbar->addWidget(m_magnificationFactorLabel);
    m_magnificationFactorComboBox = new QComboBox(this);
    m_magnificationFactorComboBox->addItems({QStringLiteral("2x"), QStringLiteral("3x"), QStringLiteral("4x")});
    m_magnificationFactorComboBox->setCurrentText(QStringLiteral("2x"));
    m_magnificationFactorComboBox->setFixedWidth(72);
    m_magnificationFactorAction = propertiesToolbar->addWidget(m_magnificationFactorComboBox);
    QObject::connect(m_magnificationFactorComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setMagnificationFactorPreset);

    m_counterLabel = new QLabel(QStringLiteral("Counter"), this);
    m_counterLabel->setContentsMargins(8, 0, 2, 0);
    m_counterLabelAction = propertiesToolbar->addWidget(m_counterLabel);
    m_counterSpinBox = new QSpinBox(this);
    m_counterSpinBox->setRange(0, 999);
    m_counterSpinBox->setValue(1);
    m_counterSpinBox->setFixedWidth(68);
    m_counterAction = propertiesToolbar->addWidget(m_counterSpinBox);
    QObject::connect(m_counterSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::setCounterStartValue);

    m_cropModeLabel = new QLabel(QStringLiteral("Crop"), this);
    m_cropModeLabel->setContentsMargins(8, 0, 2, 0);
    m_cropModeLabelAction = propertiesToolbar->addWidget(m_cropModeLabel);
    m_cropModeComboBox = new QComboBox(this);
    m_cropModeComboBox->addItems({QStringLiteral("Default"), QStringLiteral("Vertical"), QStringLiteral("Horizontal"), QStringLiteral("Auto")});
    m_cropModeComboBox->setFixedWidth(100);
    m_cropModeAction = propertiesToolbar->addWidget(m_cropModeComboBox);
    QObject::connect(m_cropModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setCropModePreset);

    m_boldAction = propertiesToolbar->addAction(QIcon(QStringLiteral(":/win/text_bold.png")), QStringLiteral("Bold"));
    m_boldAction->setCheckable(true);
    m_italicAction = propertiesToolbar->addAction(QIcon(QStringLiteral(":/win/text_italic.png")), QStringLiteral("Italic"));
    m_italicAction->setCheckable(true);
    m_shadowAction = propertiesToolbar->addAction(QIcon(QStringLiteral(":/win/shadow.png")), QStringLiteral("Shadow"));
    m_shadowAction->setCheckable(true);
    QObject::connect(m_boldAction, &QAction::toggled, this, &MainWindow::setBoldText);
    QObject::connect(m_italicAction, &QAction::toggled, this, &MainWindow::setItalicText);
    QObject::connect(m_shadowAction, &QAction::toggled, this, &MainWindow::setShadowEnabled);

    m_confirmAction = propertiesToolbar->addAction(QStringLiteral("Confirm"));
    QObject::connect(m_confirmAction, &QAction::triggered, this, &MainWindow::confirmPendingAction);
    m_cancelAction = propertiesToolbar->addAction(QStringLiteral("Cancel"));
    QObject::connect(m_cancelAction, &QAction::triggered, this, &MainWindow::cancelPendingAction);

    QToolBar* toolsToolbar = new QToolBar(QStringLiteral("Tools"), this);
    toolsToolbar->setMovable(false);
    toolsToolbar->setOrientation(Qt::Vertical);
    toolsToolbar->setIconSize(QSize(16, 16));
    toolsToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::LeftToolBarArea, toolsToolbar);

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/cursor.png"), QStringLiteral("Cursor"), true);
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/shape_square_add.png"), QStringLiteral("Rectangle"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/shape_ellipse_add.png"), QStringLiteral("Ellipse"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/shape_line.png"), QStringLiteral("Line"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/shape_arrow_add.png"), QStringLiteral("Arrow"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/freehand.png"), QStringLiteral("Freehand"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/text_dropcaps.png"), QStringLiteral("Text"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/filter_highlight_text.png"), QStringLiteral("Text Highlight"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/editor/balloon.png"), QStringLiteral("Speech bubble"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/editor/notification-counter-01.png"), QStringLiteral("Step label"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/heart.png"), QStringLiteral("Emoji"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/filter_highlight_area.png"), QStringLiteral("Highlight"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/edit-pixelate.png"), QStringLiteral("Obfuscate"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/edit-blur.png"), QStringLiteral("Blur"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/filter_highlight_grayscale.png"), QStringLiteral("Grayscale"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/fugue/magnifier.png"), QStringLiteral("Magnify"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/editor/resize.png"), QStringLiteral("Resize"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/ruler-crop.png"), QStringLiteral("Crop"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/layer-rotate.png"), QStringLiteral("Rotate clockwise"));
    addToolAction(toolGroup, toolsToolbar, QStringLiteral(":/win/layer-rotate-left.png"), QStringLiteral("Rotate counter clockwise"));
}

void MainWindow::createCentralLayout() {
    QWidget* central = new QWidget(this);
    QGridLayout* layout = new QGridLayout(central);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(central);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet(QStringLiteral("background:#e4e4e4;"));

    m_canvasWidget = new CanvasWidget(m_scrollArea);
    QObject::connect(m_canvasWidget, &CanvasWidget::statusMessageChanged, this, &MainWindow::setStatusMessage);
    QObject::connect(m_canvasWidget, &CanvasWidget::undoAvailableChanged, this, &MainWindow::updateUndoAvailability);
    QObject::connect(m_canvasWidget, &CanvasWidget::redoAvailableChanged, this, &MainWindow::updateRedoAvailability);
    QObject::connect(m_canvasWidget, &CanvasWidget::documentSizeChanged, this, &MainWindow::updateDocumentSize);
    QObject::connect(m_canvasWidget, &CanvasWidget::editingContextChanged, this, &MainWindow::refreshPropertyVisibility);
    QObject::connect(m_canvasWidget, &CanvasWidget::selectionAvailabilityChanged, this, &MainWindow::updateSelectionAvailability);
    QObject::connect(m_canvasWidget, &CanvasWidget::clipboardAvailabilityChanged, this, &MainWindow::updateClipboardAvailability);
    QObject::connect(m_canvasWidget, &CanvasWidget::cropModeChanged, this, [this](CanvasWidget::CropMode) { syncPropertyControlsFromCanvas(); });
    QObject::connect(m_canvasWidget, &CanvasWidget::counterStartChanged, this, [this](int) { syncPropertyControlsFromCanvas(); });
    QObject::connect(m_canvasWidget, &CanvasWidget::zoomStepRequested, this, &MainWindow::zoomByStep);
    QObject::connect(m_canvasWidget, &CanvasWidget::modifiedChanged, this, &MainWindow::updateModifiedState);
    m_canvasWidget->setStrokeWidth(3);
    m_canvasWidget->setTextFont(QFont(QStringLiteral("Noto Sans"), 14));
    m_scrollArea->setWidget(m_canvasWidget);
    refreshPropertyVisibility(QStringLiteral("Cursor"));
    layout->addWidget(m_scrollArea, 0, 0);
    setCentralWidget(central);
}

void MainWindow::createStatusBar() {
    m_dimensionsLabel = new QLabel(QStringLiteral("No image loaded"), this);
    m_statusLabel = new QLabel(QStringLiteral("Ready."), this);
    m_zoomComboBox = new QComboBox(this);
    m_zoomComboBox->addItems({QStringLiteral("25%"), QStringLiteral("50%"), QStringLiteral("66%"), QStringLiteral("75%"), QStringLiteral("100%"), QStringLiteral("200%"), QStringLiteral("300%"), QStringLiteral("400%"), QStringLiteral("600%")});
    m_zoomComboBox->setCurrentText(QStringLiteral("100%"));
    m_zoomComboBox->setFixedWidth(88);
    QObject::connect(m_zoomComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::setZoomPreset);

    statusBar()->addPermanentWidget(m_dimensionsLabel);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("Zoom"), this));
    statusBar()->addPermanentWidget(m_zoomComboBox);
}

void MainWindow::openImage() {
    if (!confirmCloseWithUnsavedChanges()) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(this, QStringLiteral("Open image"), QString(), QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)"));
    if (filePath.isEmpty()) {
        return;
    }

    loadImageFromPath(filePath);
}

bool MainWindow::loadImageFromPath(const QString& filePath) {
    QImage image(filePath);
    if (image.isNull()) {
        setStatusMessage(QStringLiteral("Failed to open image."));
        return false;
    }

    m_currentFilePath = filePath;
    m_canvasWidget->setDocumentImage(image);
    setZoomFactor(m_zoomFactor);
    m_saveAction->setEnabled(true);
    m_saveAsAction->setEnabled(true);
    m_saveAsToolbarAction->setEnabled(true);
    const QSize size = m_canvasWidget->documentSize();
    m_dimensionsLabel->setText(QStringLiteral("%1 x %2px").arg(size.width()).arg(size.height()));
    m_isModified = false;
    updateWindowTitle();
    setStatusMessage(QStringLiteral("Loaded %1.").arg(QFileInfo(filePath).fileName()));
    return true;
}

void MainWindow::saveImage() {
    if (m_currentFilePath.isEmpty()) {
        saveImageAs();
        return;
    }

    saveImageToPath(m_currentFilePath);
}

void MainWindow::saveImageAs() {
    if (!m_canvasWidget->hasDocument()) {
        setStatusMessage(QStringLiteral("Load an image before saving."));
        return;
    }

    const QFileInfo currentFileInfo(m_currentFilePath);
    const QString suggestedName = m_currentFilePath.isEmpty() ? QStringLiteral("blueshot-editor.png") : currentFileInfo.completeBaseName() + QStringLiteral(".png");
    const QString initialDirectory = m_currentFilePath.isEmpty() ? QString() : currentFileInfo.absolutePath();

    QFileDialog saveDialog(this, QStringLiteral("Save image as"), initialDirectory, QStringLiteral("PNG image (*.png)"));
    saveDialog.setAcceptMode(QFileDialog::AcceptSave);
    saveDialog.setFileMode(QFileDialog::AnyFile);
    saveDialog.setDefaultSuffix(QStringLiteral("png"));
    saveDialog.selectFile(suggestedName);

    if (saveDialog.exec() != QDialog::Accepted) {
        return;
    }

    const QStringList selectedFiles = saveDialog.selectedFiles();
    if (selectedFiles.isEmpty()) {
        return;
    }

    const QString filePath = selectedFiles.constFirst();
    if (filePath.isEmpty()) {
        return;
    }

    saveImageToPath(filePath);
}

bool MainWindow::saveImageToPath(const QString& filePath) {
    if (!m_canvasWidget->renderDocumentImage().save(filePath)) {
        setStatusMessage(QStringLiteral("Failed to save image."));
        return false;
    }

    m_currentFilePath = filePath;
    m_canvasWidget->markSaved();
    m_isModified = false;
    updateWindowTitle();
    setStatusMessage(QStringLiteral("Saved %1.").arg(QFileInfo(filePath).fileName()));
    return true;
}

void MainWindow::closeImage() {
    if (!confirmCloseWithUnsavedChanges()) {
        return;
    }

    resetCanvas();
    setStatusMessage(QStringLiteral("Image closed."));
}

void MainWindow::resizeImage() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }

    const QSize currentSize = m_canvasWidget->documentSize();
    bool accepted = false;
    const QString sizeText = QInputDialog::getText(this,
        QStringLiteral("Resize Image"),
        QStringLiteral("Width x Height"),
        QLineEdit::Normal,
        QStringLiteral("%1x%2").arg(currentSize.width()).arg(currentSize.height()),
        &accepted);

    if (!accepted || sizeText.trimmed().isEmpty()) {
        return;
    }

    const QString normalized = sizeText.trimmed().toLower().remove(' ');
    const QStringList parts = normalized.split('x');
    if (parts.size() != 2) {
        setStatusMessage(QStringLiteral("Resize format must be WIDTHxHEIGHT."));
        return;
    }

    const int width = parts.at(0).toInt();
    const int height = parts.at(1).toInt();
    if (width <= 0 || height <= 0) {
        setStatusMessage(QStringLiteral("Resize dimensions must be positive."));
        return;
    }

    m_canvasWidget->resizeDocumentImage(QSize(width, height));
}

void MainWindow::rotateImageClockwise() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->rotateClockwise();
    }
}

void MainWindow::rotateImageCounterClockwise() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->rotateCounterClockwise();
    }
}

void MainWindow::copyToClipboard() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }

    QGuiApplication::clipboard()->setImage(m_canvasWidget->renderDocumentImage());
    setStatusMessage(QStringLiteral("Image copied to clipboard."));
}

void MainWindow::cutSelection() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->cutSelection();
    }
}

void MainWindow::copySelection() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->copySelection();
    }
}

void MainWindow::pasteSelection() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->pasteSelection();
    }
}

void MainWindow::confirmPendingAction() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->confirmPendingAction();
    }
}

void MainWindow::cancelPendingAction() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->cancelPendingAction();
    }
}

void MainWindow::duplicateSelection() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->duplicateSelection();
    }
}

void MainWindow::bringSelectionToFront() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->bringSelectionToFront();
    }
}

void MainWindow::sendSelectionToBack() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->sendSelectionToBack();
    }
}

void MainWindow::selectAllObjects() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->selectAll();
    }
}

void MainWindow::selectTool() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    activateToolByName(action->data().toString());
}

void MainWindow::zoomByStep(int direction) {
    if (m_zoomComboBox == nullptr) {
        return;
    }

    const int currentIndex = m_zoomComboBox->currentIndex();
    if (currentIndex < 0) {
        return;
    }

    const int nextIndex = qBound(0, currentIndex + direction, m_zoomComboBox->count() - 1);
    if (nextIndex != currentIndex) {
        m_zoomComboBox->setCurrentIndex(nextIndex);
    }
}

void MainWindow::setZoomPreset(int index) {
    const QList<double> zoomValues{0.25, 0.50, 0.66, 0.75, 1.0, 2.0, 3.0, 4.0, 6.0};
    if (index < 0 || index >= zoomValues.size()) {
        return;
    }

    setZoomFactor(zoomValues.at(index));
}

void MainWindow::setActualSize() {
    m_zoomComboBox->setCurrentText(QStringLiteral("100%"));
    setZoomFactor(1.0);
}

void MainWindow::setBestFit() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    const QSize documentSize = m_canvasWidget->documentSize();
    if (viewportSize.width() <= 0 || viewportSize.height() <= 0 || documentSize.width() <= 0 || documentSize.height() <= 0) {
        return;
    }

    const double widthFactor = static_cast<double>(viewportSize.width()) / static_cast<double>(documentSize.width());
    const double heightFactor = static_cast<double>(viewportSize.height()) / static_cast<double>(documentSize.height());
    setZoomFactor(qMin(widthFactor, heightFactor));
    setStatusMessage(QStringLiteral("Best fit applied."));
}

void MainWindow::setStrokeWidthPreset(int index) {
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr) {
        return;
    }

    m_canvasWidget->setStrokeWidth(index);
}

void MainWindow::setFontFamilyPreset(int index) {
    Q_UNUSED(index)
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr || m_fontFamilyComboBox == nullptr || m_fontSizeComboBox == nullptr) {
        return;
    }

    QFont font(m_fontFamilyComboBox->currentText(), m_fontSizeComboBox->currentText().split(' ').first().toInt());
    font.setBold(m_boldAction != nullptr && m_boldAction->isChecked());
    font.setItalic(m_italicAction != nullptr && m_italicAction->isChecked());
    m_canvasWidget->setTextFont(font);
}

void MainWindow::setFontSizePreset(int index) {
    Q_UNUSED(index)
    if (m_syncingPropertyControls) {
        return;
    }
    setFontFamilyPreset(0);
}

void MainWindow::setBoldText(bool checked) {
    Q_UNUSED(checked)
    if (m_syncingPropertyControls) {
        return;
    }
    setFontFamilyPreset(0);
}

void MainWindow::setItalicText(bool checked) {
    Q_UNUSED(checked)
    if (m_syncingPropertyControls) {
        return;
    }
    setFontFamilyPreset(0);
}

void MainWindow::selectHorizontalAlignmentAction() {
    if (m_syncingPropertyControls || m_canvasWidget == nullptr) {
        return;
    }
    QAction* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }
    m_canvasWidget->setTextHorizontalAlignment(static_cast<Qt::Alignment>(action->data().toInt()));
}

void MainWindow::selectVerticalAlignmentAction() {
    if (m_syncingPropertyControls || m_canvasWidget == nullptr) {
        return;
    }
    QAction* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }
    m_canvasWidget->setTextVerticalAlignment(static_cast<Qt::Alignment>(action->data().toInt()));
}

void MainWindow::setArrowHeadPreset(int index) {
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr) {
        return;
    }

    static const CanvasWidget::ArrowHeadMode options[] = {
        CanvasWidget::ArrowHeadMode::End,
        CanvasWidget::ArrowHeadMode::Start,
        CanvasWidget::ArrowHeadMode::Both,
        CanvasWidget::ArrowHeadMode::None,
    };
    m_canvasWidget->setArrowHeadMode(options[qBound(0, index, 3)]);
}

void MainWindow::setPixelSizePreset(int index) {
    Q_UNUSED(index)
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr || m_pixelSizeComboBox == nullptr) {
        return;
    }

    m_canvasWidget->setPixelSize(m_pixelSizeComboBox->currentText().split(' ').first().toInt());
}

void MainWindow::setBlurRadiusPreset(int index) {
    Q_UNUSED(index)
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr || m_blurRadiusComboBox == nullptr) {
        return;
    }

    m_canvasWidget->setBlurRadius(m_blurRadiusComboBox->currentText().split(' ').first().toInt());
}

void MainWindow::setMagnificationFactorPreset(int index) {
    Q_UNUSED(index)
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr || m_magnificationFactorComboBox == nullptr) {
        return;
    }

    m_canvasWidget->setMagnificationFactor(m_magnificationFactorComboBox->currentText().chopped(1).toInt());
}

void MainWindow::setShadowEnabled(bool checked) {
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->setShadowEnabled(checked);
    }
}

void MainWindow::setCounterStartValue(int value) {
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->setCounterStart(value);
    }
}

void MainWindow::setCropModePreset(int index) {
    if (m_syncingPropertyControls) {
        return;
    }
    if (m_canvasWidget == nullptr) {
        return;
    }

    static const CanvasWidget::CropMode options[] = {
        CanvasWidget::CropMode::Default,
        CanvasWidget::CropMode::Vertical,
        CanvasWidget::CropMode::Horizontal,
        CanvasWidget::CropMode::AutoCrop,
    };
    m_canvasWidget->setCropMode(options[qBound(0, index, 3)]);
}

void MainWindow::chooseFillColor() {
    if (m_canvasWidget == nullptr) {
        return;
    }
    const QColor color = QColorDialog::getColor(m_canvasWidget->currentFillColor(), this, QStringLiteral("Select fill color"));
    if (!color.isValid()) {
        return;
    }
    m_canvasWidget->setFillColor(color);
    updateColorButton(m_fillColorButton, color, QStringLiteral(":/win/fugue/paint-can-color.png"));
}

void MainWindow::chooseLineColor() {
    if (m_canvasWidget == nullptr) {
        return;
    }
    const QColor color = QColorDialog::getColor(m_canvasWidget->currentStrokeColor(), this, QStringLiteral("Select line color"));
    if (!color.isValid()) {
        return;
    }
    m_canvasWidget->setStrokeColor(color);
    updateColorButton(m_lineColorButton, color, QStringLiteral(":/win/fugue/pencil-color.png"));
}

void MainWindow::triggerUndo() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->undo();
    }
}

void MainWindow::triggerRedo() {
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->redo();
    }
}

void MainWindow::activateToolFromAction() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    activateToolByName(action->data().toString());
}

void MainWindow::applyBorderEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    const QImage result = CanvasWidget::applyWholeImageBorder(m_canvasWidget->renderDocumentImage(), 2, Qt::black);
    m_canvasWidget->applyWholeImageEffect(result, QPointF(2.0, 2.0), QStringLiteral("Border added."));
}

void MainWindow::applyGrayscaleEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    m_canvasWidget->applyWholeImageEffect(CanvasWidget::applyWholeImageGrayscale(m_canvasWidget->renderDocumentImage()), QPointF(), QStringLiteral("Image converted to grayscale."));
}

void MainWindow::applyInvertEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    m_canvasWidget->applyWholeImageEffect(CanvasWidget::applyWholeImageInvert(m_canvasWidget->renderDocumentImage()), QPointF(), QStringLiteral("Image inverted."));
}

void MainWindow::applyRemoveTransparencyEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    const QColor color = QColorDialog::getColor(Qt::white, this, QStringLiteral("Remove Transparency"));
    if (!color.isValid()) {
        return;
    }
    m_canvasWidget->applyWholeImageEffect(CanvasWidget::applyWholeImageRemoveTransparency(m_canvasWidget->renderDocumentImage(), color), QPointF(), QStringLiteral("Transparency removed."));
}

void MainWindow::applyDropShadowEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    const QPoint offset(-1, -1);
    const int shadowSize = 7;
    const QImage result = CanvasWidget::applyWholeImageDropShadow(m_canvasWidget->renderDocumentImage(), shadowSize, offset, 0.6);
    const int padding = qMax(6, shadowSize * 2);
    const QPointF translation(padding + qMax(0, -offset.x()), padding + qMax(0, -offset.y()));
    m_canvasWidget->applyWholeImageEffect(result, translation, QStringLiteral("Drop shadow added."));
}

void MainWindow::applyTornEdgesEffect() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument()) {
        return;
    }
    const int toothHeight = 12;
    const QImage result = CanvasWidget::applyWholeImageTornEdges(m_canvasWidget->renderDocumentImage(), toothHeight, 20, 20, true);
    const QPointF translation(qMax(12, toothHeight), qMax(12, toothHeight));
    m_canvasWidget->applyWholeImageEffect(result, translation, QStringLiteral("Torn edges added."));
}

void MainWindow::showAboutDialog() {
    QMessageBox::about(this,
        QStringLiteral("About Blueshot Editor"),
        QStringLiteral("Blueshot Editor for Linux<br><br><a href=\"https://huma.id/blueshot\">https://huma.id/blueshot</a>"));
}

void MainWindow::updateUndoAvailability(bool available) {
    if (m_undoAction != nullptr) {
        m_undoAction->setEnabled(available);
    }
    if (m_undoToolbarAction != nullptr) {
        m_undoToolbarAction->setEnabled(available);
    }
}

void MainWindow::updateRedoAvailability(bool available) {
    if (m_redoAction != nullptr) {
        m_redoAction->setEnabled(available);
    }
    if (m_redoToolbarAction != nullptr) {
        m_redoToolbarAction->setEnabled(available);
    }
}

void MainWindow::updateDocumentSize(const QSize& size) {
    if (m_dimensionsLabel != nullptr) {
        m_dimensionsLabel->setText(QStringLiteral("%1 x %2px").arg(size.width()).arg(size.height()));
    }
}

void MainWindow::updateModifiedState(bool modified) {
    m_isModified = modified;
    updateWindowTitle();
}

void MainWindow::updateSelectionAvailability(bool available) {
    if (m_cutAction != nullptr) m_cutAction->setEnabled(available);
    if (m_copyAction != nullptr) m_copyAction->setEnabled(available);
    if (m_cutToolbarAction != nullptr) m_cutToolbarAction->setEnabled(available);
    if (m_copyToolbarAction != nullptr) m_copyToolbarAction->setEnabled(available);
}

void MainWindow::updateClipboardAvailability(bool available) {
    if (m_pasteAction != nullptr) m_pasteAction->setEnabled(available);
    if (m_pasteToolbarAction != nullptr) m_pasteToolbarAction->setEnabled(available);
}

void MainWindow::resetCanvas() {
    m_currentFilePath.clear();
    m_canvasWidget->clearDocument();
    m_saveAction->setEnabled(false);
    m_saveAsAction->setEnabled(false);
    m_saveAsToolbarAction->setEnabled(false);
    m_dimensionsLabel->setText(QStringLiteral("No image loaded"));
    m_statusLabel->setText(QStringLiteral("Ready."));
    setZoomFactor(1.0);
    m_zoomComboBox->setCurrentText(QStringLiteral("100%"));
    m_isModified = false;
    updateWindowTitle();
}

void MainWindow::setStatusMessage(const QString& message) {
    m_statusLabel->setText(message);
}

void MainWindow::setZoomFactor(double zoomFactor) {
    m_zoomFactor = zoomFactor;
    if (m_canvasWidget != nullptr) {
        m_canvasWidget->setZoomFactor(zoomFactor);
    }
}

void MainWindow::refreshPropertyVisibility(const QString& context) {
    const bool textLike = context == QStringLiteral("Text") || context == QStringLiteral("Text Highlight") || context == QStringLiteral("Speech bubble") || context == QStringLiteral("Step label") || context == QStringLiteral("Emoji");
    const bool fillLike = context == QStringLiteral("Rectangle") || context == QStringLiteral("Ellipse") || context == QStringLiteral("Speech bubble") || context == QStringLiteral("Step label") || context == QStringLiteral("Highlight");
    const bool strokeLike = fillLike || context == QStringLiteral("Line") || context == QStringLiteral("Arrow") || context == QStringLiteral("Freehand") || context == QStringLiteral("Text") || context == QStringLiteral("Text Highlight");
    const bool arrowLike = context == QStringLiteral("Arrow");
    const bool stepLike = context == QStringLiteral("Step label");
    const bool blurLike = context == QStringLiteral("Blur");
    const bool magnifyLike = context == QStringLiteral("Magnify");
    const bool cropLike = context == QStringLiteral("Crop") || context == QStringLiteral("CropPending");
    const bool confirmable = context == QStringLiteral("CropPending");

    if (m_fillColorAction != nullptr) m_fillColorAction->setVisible(fillLike);
    if (m_lineColorAction != nullptr) m_lineColorAction->setVisible(strokeLike);
    if (m_lineWidthAction != nullptr) m_lineWidthAction->setVisible(strokeLike);
    if (m_fontLabelAction != nullptr) m_fontLabelAction->setVisible(textLike);
    if (m_fontFamilyAction != nullptr) m_fontFamilyAction->setVisible(textLike);
    if (m_fontSizeAction != nullptr) m_fontSizeAction->setVisible(textLike);
    if (m_horizontalAlignmentAction != nullptr) m_horizontalAlignmentAction->setVisible(textLike);
    if (m_verticalAlignmentAction != nullptr) m_verticalAlignmentAction->setVisible(textLike);
    if (m_arrowHeadLabelAction != nullptr) m_arrowHeadLabelAction->setVisible(arrowLike);
    if (m_arrowHeadAction != nullptr) m_arrowHeadAction->setVisible(arrowLike);
    if (m_pixelSizeLabelAction != nullptr) m_pixelSizeLabelAction->setVisible(false);
    if (m_pixelSizeAction != nullptr) m_pixelSizeAction->setVisible(false);
    if (m_blurRadiusLabelAction != nullptr) m_blurRadiusLabelAction->setVisible(blurLike);
    if (m_blurRadiusAction != nullptr) m_blurRadiusAction->setVisible(blurLike);
    if (m_magnificationFactorLabelAction != nullptr) m_magnificationFactorLabelAction->setVisible(magnifyLike);
    if (m_magnificationFactorAction != nullptr) m_magnificationFactorAction->setVisible(magnifyLike);
    if (m_counterLabelAction != nullptr) m_counterLabelAction->setVisible(stepLike);
    if (m_counterAction != nullptr) m_counterAction->setVisible(stepLike);
    if (m_cropModeLabelAction != nullptr) m_cropModeLabelAction->setVisible(cropLike);
    if (m_cropModeAction != nullptr) m_cropModeAction->setVisible(cropLike);
    if (m_boldAction != nullptr) m_boldAction->setVisible(textLike);
    if (m_italicAction != nullptr) m_italicAction->setVisible(textLike);
    if (m_shadowAction != nullptr) m_shadowAction->setVisible(textLike);
    if (m_confirmAction != nullptr) m_confirmAction->setVisible(confirmable);
    if (m_cancelAction != nullptr) m_cancelAction->setVisible(confirmable);

    syncPropertyControlsFromCanvas();
}

void MainWindow::updateColorButton(QToolButton* button, const QColor& color, const QString& iconPath) {
    if (button == nullptr) {
        return;
    }
    button->setIcon(createTintedToolbarIcon(iconPath, color));
    button->setIconSize(QSize(16, 16));
    button->setStyleSheet(QStringLiteral("QToolButton{background:#f6f6f6;border:1px solid #c9c9c9;padding:4px;margin:1px;}QToolButton:hover{background:#ededed;}"));
}

void MainWindow::updateAlignmentButtons() {
    if (m_canvasWidget == nullptr) {
        return;
    }

    if (m_horizontalAlignmentButton != nullptr) {
        const Qt::Alignment horizontal = m_canvasWidget->currentTextHorizontalAlignment();
        const QString iconPath = horizontal == Qt::AlignHCenter
            ? QStringLiteral(":/win/fugue/edit-alignment-center.png")
            : horizontal == Qt::AlignRight
                ? QStringLiteral(":/win/fugue/edit-alignment-right.png")
                : QStringLiteral(":/win/fugue/edit-alignment.png");
        m_horizontalAlignmentButton->setIcon(QIcon(iconPath));
        m_horizontalAlignmentButton->setIconSize(QSize(16, 16));
    }

    if (m_verticalAlignmentButton != nullptr) {
        const Qt::Alignment vertical = m_canvasWidget->currentTextVerticalAlignment();
        const QString iconPath = vertical == Qt::AlignTop
            ? QStringLiteral(":/win/fugue/edit-vertical-alignment-top.png")
            : vertical == Qt::AlignVCenter
                ? QStringLiteral(":/win/fugue/edit-vertical-alignment-middle.png")
                : QStringLiteral(":/win/fugue/edit-vertical-alignment.png");
        m_verticalAlignmentButton->setIcon(QIcon(iconPath));
        m_verticalAlignmentButton->setIconSize(QSize(16, 16));
    }
}

void MainWindow::syncPropertyControlsFromCanvas() {
    if (m_canvasWidget == nullptr) {
        return;
    }

    m_syncingPropertyControls = true;

    updateColorButton(m_fillColorButton, m_canvasWidget->currentFillColor(), QStringLiteral(":/win/fugue/paint-can-color.png"));
    updateColorButton(m_lineColorButton, m_canvasWidget->currentStrokeColor(), QStringLiteral(":/win/fugue/pencil-color.png"));
    updateAlignmentButtons();

    if (m_lineWidthComboBox != nullptr) {
        m_lineWidthComboBox->setCurrentText(QStringLiteral("%1 px").arg(m_canvasWidget->currentStrokeWidth()));
    }

    const QFont font = m_canvasWidget->currentTextFont();
    if (m_fontFamilyComboBox != nullptr) {
        const int fontIndex = m_fontFamilyComboBox->findText(font.family());
        if (fontIndex >= 0) {
            m_fontFamilyComboBox->setCurrentIndex(fontIndex);
        }
    }
    if (m_fontSizeComboBox != nullptr) {
        m_fontSizeComboBox->setCurrentText(QStringLiteral("%1 pt").arg(qRound(font.pointSizeF())));
    }
    if (m_boldAction != nullptr) {
        m_boldAction->setChecked(font.bold());
    }
    if (m_italicAction != nullptr) {
        m_italicAction->setChecked(font.italic());
    }
    if (m_shadowAction != nullptr) {
        m_shadowAction->setChecked(m_canvasWidget->currentShadowEnabled());
    }

    if (m_arrowHeadComboBox != nullptr) {
        const auto mode = m_canvasWidget->currentArrowHeadMode();
        m_arrowHeadComboBox->setCurrentIndex(mode == CanvasWidget::ArrowHeadMode::Start ? 1 : mode == CanvasWidget::ArrowHeadMode::Both ? 2 : mode == CanvasWidget::ArrowHeadMode::None ? 3 : 0);
    }
    if (m_pixelSizeComboBox != nullptr) {
        m_pixelSizeComboBox->setCurrentText(QStringLiteral("%1 px").arg(m_canvasWidget->currentPixelSize()));
    }
    if (m_blurRadiusComboBox != nullptr) {
        m_blurRadiusComboBox->setCurrentText(QStringLiteral("%1 px").arg(m_canvasWidget->currentBlurRadius()));
    }
    if (m_magnificationFactorComboBox != nullptr) {
        m_magnificationFactorComboBox->setCurrentText(QStringLiteral("%1x").arg(m_canvasWidget->currentMagnificationFactor()));
    }
    if (m_counterSpinBox != nullptr) {
        m_counterSpinBox->setValue(m_canvasWidget->counterStart());
    }
    if (m_cropModeComboBox != nullptr) {
        const auto mode = m_canvasWidget->cropMode();
        m_cropModeComboBox->setCurrentIndex(mode == CanvasWidget::CropMode::Vertical ? 1 : mode == CanvasWidget::CropMode::Horizontal ? 2 : mode == CanvasWidget::CropMode::AutoCrop ? 3 : 0);
    }

    m_syncingPropertyControls = false;
}

bool MainWindow::confirmCloseWithUnsavedChanges() {
    if (m_canvasWidget == nullptr || !m_canvasWidget->hasDocument() || !m_canvasWidget->isModified()) {
        return true;
    }

    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        QStringLiteral("Unsaved changes"),
        QStringLiteral("Save changes before closing?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Yes);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    if (result == QMessageBox::Yes) {
        const bool wasModified = m_canvasWidget->isModified();
        saveImageAs();
        return !wasModified || !m_canvasWidget->isModified();
    }

    return true;
}

void MainWindow::updateWindowTitle() {
    if (m_currentFilePath.isEmpty()) {
        setWindowTitle(m_isModified ? QStringLiteral("Blueshot Editor *") : QStringLiteral("Blueshot Editor"));
        return;
    }

    const QString fileName = QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(m_isModified
            ? QStringLiteral("%1 * - Blueshot Editor").arg(fileName)
            : QStringLiteral("%1 - Blueshot Editor").arg(fileName));
}
