#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QProcess>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QMouseEvent>
#include <QSettings>
#include <QPoint>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

// X11 includes for window management
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>

class SetupDialog : public QDialog {
    Q_OBJECT
public:
    SetupDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::FramelessWindowHint);
        setFixedSize(350, 220);

        // Open X11 display
        xdisplay = XOpenDisplay(NULL);
        if (!xdisplay) {
            QMessageBox::critical(this, "Error", "Failed to open X11 display");
            return;
        }

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(8);
        mainLayout->setContentsMargins(12, 12, 12, 12);

        // Title label
        QLabel *titleLabel = new QLabel("Select Target", this);
        titleLabel->setStyleSheet("QLabel { font-weight: bold; color: white; background-color: #333333; padding: 4px; border-radius: 3px; }");
        titleLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(titleLabel);

        // Window selection with label and refresh button
        QHBoxLayout *windowLayout = new QHBoxLayout();
        QLabel *windowLabel = new QLabel("Window:", this);
        windowLabel->setStyleSheet("QLabel { color: #cccccc; }");
        windowLayout->addWidget(windowLabel);
        
        windowLayout->addStretch();
        
        QPushButton *refreshButton = new QPushButton("Refresh", this);
        refreshButton->setFixedSize(60, 28);
        refreshButton->setStyleSheet("QPushButton { background-color: #666666; color: white; border: 1px solid #888888; border-radius: 3px; font-size: 10px; } QPushButton:hover { background-color: #777777; }");
        windowLayout->addWidget(refreshButton);
        
        mainLayout->addLayout(windowLayout);
        
        windowCombo = new QComboBox(this);
        windowCombo->setFixedHeight(28);
        populateWindows();
        mainLayout->addWidget(windowCombo);

        // Keyboard selection with label
        QLabel *keyboardLabel = new QLabel("Keyboard:", this);
        keyboardLabel->setStyleSheet("QLabel { color: #cccccc; }");
        mainLayout->addWidget(keyboardLabel);
        
        keyboardCombo = new QComboBox(this);
        keyboardCombo->setFixedHeight(28);
        populateKeyboards();
        mainLayout->addWidget(keyboardCombo);

        // Status label
        statusLabel = new QLabel("", this);
        statusLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 10px; }");
        statusLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(statusLabel);

        // OK button
        QPushButton *okButton = new QPushButton("Start Control", this);
        okButton->setFixedHeight(32);
        okButton->setStyleSheet("QPushButton { background-color: #4444ff; color: white; border: 1px solid #6666ff; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #6666ff; }");
        mainLayout->addWidget(okButton);

        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(refreshButton, &QPushButton::clicked, this, &SetupDialog::refreshWindows);
        
        updateStatus();
    }

    ~SetupDialog() {
        if (xdisplay) {
            XCloseDisplay(xdisplay);
        }
    }

    QString selectedWindow() const {
        return windowCombo->currentText();
    }

    QString selectedKeyboard() const {
        return keyboardCombo->currentText();
    }

    QString selectedWindowId() const {
        return windowCombo->currentData().toString();
    }

private slots:
    void refreshWindows() {
        windowCombo->clear();
        populateWindows();
        updateStatus();
    }

private:
    void updateStatus() {
        QString status = QString("Found %1 windows, %2 keyboards").arg(windowCombo->count()).arg(keyboardCombo->count());
        statusLabel->setText(status);
    }

    QString getWindowTitle(Window win) {
        QString title;
        
        // Method 1: Try _NET_WM_NAME (UTF-8)
        Atom netWmName = XInternAtom(xdisplay, "_NET_WM_NAME", False);
        Atom utf8String = XInternAtom(xdisplay, "UTF8_STRING", False);
        
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = NULL;
        
        if (XGetWindowProperty(xdisplay, win, netWmName, 0, 1024, False, utf8String,
                              &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
            if (actualType == utf8String && actualFormat == 8 && nitems > 0) {
                title = QString::fromUtf8((char *)prop, nitems);
            }
            if (prop) XFree(prop);
        }
        
        // Method 2: Try WM_NAME (legacy)
        if (title.isEmpty()) {
            char *name = NULL;
            if (XFetchName(xdisplay, win, &name) != 0 && name != NULL) {
                title = QString::fromLocal8Bit(name);
                XFree(name);
            }
        }
        
        // Method 3: Try _NET_WM_VISIBLE_NAME
        if (title.isEmpty()) {
            Atom netWmVisibleName = XInternAtom(xdisplay, "_NET_WM_VISIBLE_NAME", False);
            if (XGetWindowProperty(xdisplay, win, netWmVisibleName, 0, 1024, False, utf8String,
                                  &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
                if (actualType == utf8String && actualFormat == 8 && nitems > 0) {
                    title = QString::fromUtf8((char *)prop, nitems);
                }
                if (prop) XFree(prop);
            }
        }
        
        return title.trimmed();
    }

    void populateWindows() {
        if (!xdisplay) return;

        QMap<QString, QString> windows; // title -> windowId
        
        Window root = DefaultRootWindow(xdisplay);
        
        // Method 1: _NET_CLIENT_LIST (most window managers)
        Atom netClientList = XInternAtom(xdisplay, "_NET_CLIENT_LIST", False);
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *clientListProp = NULL;

        if (XGetWindowProperty(xdisplay, root, netClientList, 0, 1024, False, XA_WINDOW,
                              &actualType, &actualFormat, &nitems, &bytesAfter, &clientListProp) == Success) {
            
            if (actualType == XA_WINDOW && actualFormat == 32 && nitems > 0) {
                Window *windowList = (Window *)clientListProp;
                
                for (unsigned long i = 0; i < nitems; i++) {
                    Window win = windowList[i];
                    if (win == None) continue;
                    
                    QString title = getWindowTitle(win);
                    if (!title.isEmpty() && !isSystemWindow(title, win)) {
                        QString windowIdStr = QString::number(win, 16);
                        windows[title] = windowIdStr;
                    }
                }
            }
            if (clientListProp) XFree(clientListProp);
        }
        
        // Method 2: XQueryTree (fallback - finds all child windows)
        if (windows.isEmpty()) {
            Window root_return, parent_return;
            Window *children = NULL;
            unsigned int nchildren;
            
            if (XQueryTree(xdisplay, root, &root_return, &parent_return, &children, &nchildren)) {
                for (unsigned int i = 0; i < nchildren; i++) {
                    Window win = children[i];
                    if (win == None) continue;
                    
                    // Basic window attribute check
                    XWindowAttributes attrs;
                    if (!XGetWindowAttributes(xdisplay, win, &attrs)) continue;
                    
                    // Skip invisible and override_redirect windows (usually menus/tooltips)
                    if (attrs.map_state != IsViewable || attrs.override_redirect) continue;
                    
                    QString title = getWindowTitle(win);
                    if (!title.isEmpty() && !isSystemWindow(title, win)) {
                        QString windowIdStr = QString::number(win, 16);
                        windows[title] = windowIdStr;
                    }
                }
                if (children) XFree(children);
            }
        }
        
        // Method 3: Try _NET_CLIENT_LIST_STACKING (alternative property)
        Atom netClientListStacking = XInternAtom(xdisplay, "_NET_CLIENT_LIST_STACKING", False);
        if (XGetWindowProperty(xdisplay, root, netClientListStacking, 0, 1024, False, XA_WINDOW,
                              &actualType, &actualFormat, &nitems, &bytesAfter, &clientListProp) == Success) {
            
            if (actualType == XA_WINDOW && actualFormat == 32 && nitems > 0) {
                Window *windowList = (Window *)clientListProp;
                
                for (unsigned long i = 0; i < nitems; i++) {
                    Window win = windowList[i];
                    if (win == None) continue;
                    
                    QString title = getWindowTitle(win);
                    if (!title.isEmpty() && !isSystemWindow(title, win)) {
                        QString windowIdStr = QString::number(win, 16);
                        windows[title] = windowIdStr;
                    }
                }
            }
            if (clientListProp) XFree(clientListProp);
        }
        
        // Add windows to combo box
        for (auto it = windows.begin(); it != windows.end(); ++it) {
            windowCombo->addItem(it.key(), it.value());
        }
        
        if (windowCombo->count() == 0) {
            windowCombo->addItem("No windows found - Click Refresh");
        }
    }

    bool isSystemWindow(const QString& title, Window win) {
        if (title.isEmpty()) return true;
        
        // Common system window titles/classes to skip
        QStringList systemTitles = {
            "Desktop", "Xfdesktop", "xfdesktop", "gnome-shell", "plasmashell",
            "kicker", "panel", "tray", "dock", "launcher", "notify-osd",
            "xfce4-panel", "plasma-desktop", "cairo-dock"
        };
        
        for (const QString& systemTitle : systemTitles) {
            if (title.contains(systemTitle, Qt::CaseInsensitive)) {
                return true;
            }
        }
        
        // Check window class
        XClassHint classHint;
        if (XGetClassHint(xdisplay, win, &classHint)) {
            QString resName = classHint.res_name ? QString::fromLocal8Bit(classHint.res_name) : "";
            QString resClass = classHint.res_class ? QString::fromLocal8Bit(classHint.res_class) : "";
            
            QStringList systemClasses = {
                "xfdesktop", "gnome-shell", "plasmashell", "kicker", "xfce4-panel",
                "plasma-desktop", "cairo-dock", "docky", "avant-window-navigator"
            };
            
            for (const QString& systemClass : systemClasses) {
                if (resName.contains(systemClass, Qt::CaseInsensitive) || 
                    resClass.contains(systemClass, Qt::CaseInsensitive)) {
                    XFree(classHint.res_name);
                    XFree(classHint.res_class);
                    return true;
                }
            }
            
            XFree(classHint.res_name);
            XFree(classHint.res_class);
        }
        
        return false;
    }

    void populateKeyboards() {
        keyboardCombo->clear();
        QDir devDir("/dev/input/by-id");
        QFileInfoList devices = devDir.entryInfoList(QDir::Files);
        
        foreach (const QFileInfo &device, devices) {
            if (device.fileName().contains("kbd") || device.fileName().contains("keyboard")) {
                keyboardCombo->addItem(device.fileName());
            }
        }
        
        if (keyboardCombo->count() == 0) {
            keyboardCombo->addItem("No keyboard devices found");
        }
    }

    QComboBox *windowCombo;
    QComboBox *keyboardCombo;
    QLabel *statusLabel;
    Display* xdisplay = nullptr;
};
class ControlPanel : public QMainWindow {
    Q_OBJECT

public:
    ControlPanel(QWidget *parent = nullptr) : QMainWindow(parent) {
        SetupDialog setupDialog;
        if (setupDialog.exec() != QDialog::Accepted) {
            QApplication::quit();
            return;
        }

        selectedWindow = setupDialog.selectedWindow();
        selectedKeyboard = setupDialog.selectedKeyboard();
        selectedWindowId = setupDialog.selectedWindowId();

        // Open X11 display for window management
        xdisplay = XOpenDisplay(NULL);
        if (!xdisplay) {
            QMessageBox::critical(this, "Error", "Failed to open X11 display");
            QApplication::quit();
            return;
        }

        // Open keyboard device
        keyboardFd = openKeyboardDevice(selectedKeyboard);
        if (keyboardFd == -1) {
            QMessageBox::critical(this, "Error", 
                "Failed to open keyboard device: " + selectedKeyboard + 
                "\n\nYou may need to run this program with sudo or add yourself to the input group:\n"
                "sudo usermod -a -G input $USER");
            QApplication::quit();
            return;
        }

        setupUI();
        loadSettings();
        
        // Set window to always be on top of all other windows
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setFixedSize(220, 180);
        
        // Ensure the window is visible and raised to top
        raise();
        activateWindow();
    }

    ~ControlPanel() {
        saveSettings();
        if (keyboardFd != -1) {
            ::close(keyboardFd);
        }
        if (xdisplay) {
            XCloseDisplay(xdisplay);
        }
    }

    void showEvent(QShowEvent *event) override {
        QMainWindow::showEvent(event);
        // Ensure window stays on top when shown
        raise();
        activateWindow();
    }

private slots:
    void onToggle1Clicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        toggle1State = !toggle1State;
        updateButtonAppearance(btnToggle1, toggle1State);
        sendKeyEvent(KEY_LEFTSHIFT, toggle1State ? 1 : 0);
        // Don't return mouse for toggle buttons
    }

    void onToggle2Clicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        toggle2State = !toggle2State;
        updateButtonAppearance(btnToggle2, toggle2State);
        sendKeyEvent(KEY_LEFTCTRL, toggle2State ? 1 : 0);
        // Don't return mouse for toggle buttons
    }

    void onToggle3Clicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        toggle3State = !toggle3State;
        updateButtonAppearance(btnToggle3, toggle3State);
        sendKeyEvent(KEY_LEFTALT, toggle3State ? 1 : 0);
        // Don't return mouse for toggle buttons
    }

    void onAction4Clicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        
        QTimer::singleShot(150, this, [this, originalMousePos]() {
            sendKeyEvent(KEY_ESC, 1);
            QTimer::singleShot(50, this, [this, originalMousePos]() { 
                sendKeyEvent(KEY_ESC, 0);
                
                QTimer::singleShot(100, this, [this, originalMousePos]() {
                    sendKeyEvent(KEY_KPPLUS, 1);
                    QTimer::singleShot(50, this, [this, originalMousePos]() { 
                        sendKeyEvent(KEY_KPPLUS, 0);
                        // Only return mouse for kp_plus
                        returnMouseToPosition(originalMousePos);
                    });
                });
            });
        });
    }

    void onTabClicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        
        QTimer::singleShot(100, this, [this]() {
            // Send Tab key first
            sendKeyEvent(KEY_TAB, 1);
            QTimer::singleShot(50, this, [this]() { 
                sendKeyEvent(KEY_TAB, 0);
                
                // Then turn off all modifiers after sending Tab
                QTimer::singleShot(50, this, [this]() {
                    if (toggle1State) {
                        sendKeyEvent(KEY_LEFTSHIFT, 0);
                        toggle1State = false;
                        updateButtonAppearance(btnToggle1, false);
                    }
                    if (toggle2State) {
                        sendKeyEvent(KEY_LEFTCTRL, 0);
                        toggle2State = false;
                        updateButtonAppearance(btnToggle2, false);
                    }
                    if (toggle3State) {
                        sendKeyEvent(KEY_LEFTALT, 0);
                        toggle3State = false;
                        updateButtonAppearance(btnToggle3, false);
                    }
                });
            });
        });
    }

    void onDelClicked() {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        
        QTimer::singleShot(100, this, [this]() {
            // Send Del key first
            sendKeyEvent(KEY_DELETE, 1);
            QTimer::singleShot(50, this, [this]() { 
                sendKeyEvent(KEY_DELETE, 0);
                
                // Then turn off all modifiers after sending Del
                QTimer::singleShot(50, this, [this]() {
                    if (toggle1State) {
                        sendKeyEvent(KEY_LEFTSHIFT, 0);
                        toggle1State = false;
                        updateButtonAppearance(btnToggle1, false);
                    }
                    if (toggle2State) {
                        sendKeyEvent(KEY_LEFTCTRL, 0);
                        toggle2State = false;
                        updateButtonAppearance(btnToggle2, false);
                    }
                    if (toggle3State) {
                        sendKeyEvent(KEY_LEFTALT, 0);
                        toggle3State = false;
                        updateButtonAppearance(btnToggle3, false);
                    }
                });
            });
        });
    }

    void onKeyClicked(int keyCode, const QString& keyName) {
        QPoint originalMousePos = QCursor::pos();
        // Move mouse away before focusing window
        moveMouseAway();
        focusSelectedWindow();
        
        QTimer::singleShot(100, this, [this, keyCode]() {
            // Send key first
            sendKeyEvent(keyCode, 1);
            QTimer::singleShot(50, this, [this, keyCode]() { 
                sendKeyEvent(keyCode, 0);
                
                // Then turn off all modifiers after sending key
                QTimer::singleShot(50, this, [this]() {
                    if (toggle1State) {
                        sendKeyEvent(KEY_LEFTSHIFT, 0);
                        toggle1State = false;
                        updateButtonAppearance(btnToggle1, false);
                    }
                    if (toggle2State) {
                        sendKeyEvent(KEY_LEFTCTRL, 0);
                        toggle2State = false;
                        updateButtonAppearance(btnToggle2, false);
                    }
                    if (toggle3State) {
                        sendKeyEvent(KEY_LEFTALT, 0);
                        toggle3State = false;
                        updateButtonAppearance(btnToggle3, false);
                    }
                });
            });
        });
    }

    void onGClicked() { onKeyClicked(KEY_G, "G"); }
    void onEClicked() { onKeyClicked(KEY_E, "E"); }
    void onSClicked() { onKeyClicked(KEY_S, "S"); }
    void onXClicked() { onKeyClicked(KEY_X, "X"); }
    void onYClicked() { onKeyClicked(KEY_Y, "Y"); }
    void onZClicked() { onKeyClicked(KEY_Z, "Z"); }
    void onFClicked() { onKeyClicked(KEY_F, "F"); }
    void onBClicked() { onKeyClicked(KEY_B, "B"); }

    void onCloseClicked() {
        close();
    }

private:
    void setupUI() {
        QWidget *centralWidget = new QWidget(this);
        QGridLayout *gridLayout = new QGridLayout(centralWidget);
        gridLayout->setSpacing(2);
        gridLayout->setContentsMargins(4, 4, 4, 4);

        // First row: Shift, Ctrl, Alt, Close
        btnToggle1 = createLargeButton("Shift", 36);
        btnToggle2 = createLargeButton("Ctrl", 36);
        btnToggle3 = createLargeButton("Alt", 36);
        QPushButton *closeButton = createLargeButton("×", 36);
        closeButton->setStyleSheet("QPushButton { background-color: #ff4444; color: white; border: 1px solid #ff6666; font-size: 16px; } QPushButton:hover { background-color: #ff6666; }");

        gridLayout->addWidget(btnToggle1, 0, 0);
        gridLayout->addWidget(btnToggle2, 0, 1);
        gridLayout->addWidget(btnToggle3, 0, 2);
        gridLayout->addWidget(closeButton, 0, 3);

        // Second row: X, Y, Z, Drag handle
        QPushButton *btnX = createLargeButton("X", 36);
        QPushButton *btnY = createLargeButton("Y", 36);
        QPushButton *btnZ = createLargeButton("Z", 36);
        dragButton = createLargeButton("≡", 36);
        dragButton->setStyleSheet("QPushButton { background-color: #333333; color: white; border: 1px solid #555555; font-size: 20px; } QPushButton:hover { background-color: #444444; }");
        dragButton->setCursor(Qt::SizeAllCursor);

        gridLayout->addWidget(btnX, 1, 0);
        gridLayout->addWidget(btnY, 1, 1);
        gridLayout->addWidget(btnZ, 1, 2);
        gridLayout->addWidget(dragButton, 1, 3);

        // Third row: G, S, E, F
        QPushButton *btnG = createLargeButton("G", 36);
        QPushButton *btnS = createLargeButton("S", 36);
        QPushButton *btnE = createLargeButton("E", 36);
        QPushButton *btnF = createLargeButton("F", 36);

        gridLayout->addWidget(btnG, 2, 0);
        gridLayout->addWidget(btnS, 2, 1);
        gridLayout->addWidget(btnE, 2, 2);
        gridLayout->addWidget(btnF, 2, 3);

        // Fourth row: B, Tab, Del, Plus
        QPushButton *btnB = createLargeButton("B", 36);
        QPushButton *btnTab = createLargeButton("Tab", 36);
        QPushButton *btnDel = createLargeButton("Del", 36);
        QPushButton *btnAction4 = createLargeButton("+", 36);

        gridLayout->addWidget(btnB, 3, 0);
        gridLayout->addWidget(btnTab, 3, 1);
        gridLayout->addWidget(btnDel, 3, 2);
        gridLayout->addWidget(btnAction4, 3, 3);

        setCentralWidget(centralWidget);

        // Connect signals
        connect(btnToggle1, &QPushButton::clicked, this, &ControlPanel::onToggle1Clicked);
        connect(btnToggle2, &QPushButton::clicked, this, &ControlPanel::onToggle2Clicked);
        connect(btnToggle3, &QPushButton::clicked, this, &ControlPanel::onToggle3Clicked);
        connect(btnAction4, &QPushButton::clicked, this, &ControlPanel::onAction4Clicked);
        connect(btnTab, &QPushButton::clicked, this, &ControlPanel::onTabClicked);
        connect(btnDel, &QPushButton::clicked, this, &ControlPanel::onDelClicked);
        connect(btnG, &QPushButton::clicked, this, &ControlPanel::onGClicked);
        connect(btnE, &QPushButton::clicked, this, &ControlPanel::onEClicked);
        connect(btnS, &QPushButton::clicked, this, &ControlPanel::onSClicked);
        connect(btnX, &QPushButton::clicked, this, &ControlPanel::onXClicked);
        connect(btnY, &QPushButton::clicked, this, &ControlPanel::onYClicked);
        connect(btnZ, &QPushButton::clicked, this, &ControlPanel::onZClicked);
        connect(btnF, &QPushButton::clicked, this, &ControlPanel::onFClicked);
        connect(btnB, &QPushButton::clicked, this, &ControlPanel::onBClicked);
        connect(closeButton, &QPushButton::clicked, this, &ControlPanel::onCloseClicked);

        // Initial button states
        updateButtonAppearance(btnToggle1, toggle1State);
        updateButtonAppearance(btnToggle2, toggle2State);
        updateButtonAppearance(btnToggle3, toggle3State);

        // Make only the drag button draggable
        dragButton->installEventFilter(this);
    }

    QPushButton* createLargeButton(const QString &text, int size) {
        QPushButton *button = new QPushButton(text, this);
        button->setFixedSize(size, size);
        button->setStyleSheet(R"(
            QPushButton {
                font-size: 10px;
                font-weight: bold;
                border: 1px solid #666666;
                border-radius: 4px;
                background-color: #444444;
                color: white;
            }
            QPushButton:pressed {
                background-color: #666666;
            }
            QPushButton:hover {
                border: 1px solid #888888;
            }
        )");
        return button;
    }

    void updateButtonAppearance(QPushButton *button, bool state) {
        QString style;
        if (state) {
            style = QString(R"(
                QPushButton {
                    font-size: 10px;
                    font-weight: bold;
                    border: 1px solid #00AA00;
                    border-radius: 4px;
                    background-color: #00FF00;
                    color: black;
                }
                QPushButton:pressed {
                    background-color: #00CC00;
                }
                QPushButton:hover {
                    border: 1px solid #00FF00;
                }
            )");
        } else {
            style = QString(R"(
                QPushButton {
                    font-size: 10px;
                    font-weight: bold;
                    border: 1px solid #666666;
                    border-radius: 4px;
                    background-color: #444444;
                    color: white;
                }
                QPushButton:pressed {
                    background-color: #666666;
                }
                QPushButton:hover {
                    border: 1px solid #888888;
                }
            )");
        }
        button->setStyleSheet(style);
    }

    void moveMouseAway() {
        // Move mouse to center of screen
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        QRect screenGeometry = screen->geometry();
        QPoint center = screenGeometry.center();
        QCursor::setPos(center);
        qDebug() << "Mouse moved away to safe position";
    }
    
    void returnMouseToPosition(const QPoint &position) {
        QCursor::setPos(position);
        qDebug() << "Mouse returned to position:" << position;
    }

    void focusSelectedWindow() {
        if (!selectedWindowId.isEmpty() && xdisplay) {
            bool ok;
            Window windowId = selectedWindowId.toULong(&ok, 16);
            if (ok) {
                qDebug() << "Focusing Blender window:" << selectedWindowId << "(" << windowId << ")";
                
                XRaiseWindow(xdisplay, windowId);
                XSetInputFocus(xdisplay, windowId, RevertToParent, CurrentTime);
                
                Atom netActiveWindow = XInternAtom(xdisplay, "_NET_ACTIVE_WINDOW", False);
                if (netActiveWindow != None) {
                    XEvent event;
                    memset(&event, 0, sizeof(event));
                    event.xclient.type = ClientMessage;
                    event.xclient.serial = 0;
                    event.xclient.send_event = True;
                    event.xclient.display = xdisplay;
                    event.xclient.window = windowId;
                    event.xclient.message_type = netActiveWindow;
                    event.xclient.format = 32;
                    event.xclient.data.l[0] = 1;
                    event.xclient.data.l[1] = CurrentTime;
                    event.xclient.data.l[2] = 0;
                    event.xclient.data.l[3] = 0;
                    event.xclient.data.l[4] = 0;
                    
                    XSendEvent(xdisplay, DefaultRootWindow(xdisplay), False,
                              SubstructureRedirectMask | SubstructureNotifyMask, &event);
                }
                
                XFlush(xdisplay);
                qDebug() << "Blender window focus completed";
            }
        }
    }

    int openKeyboardDevice(const QString &deviceName) {
        QString symlinkPath = "/dev/input/by-id/" + deviceName;
        
        char resolvedPath[PATH_MAX];
        if (realpath(symlinkPath.toUtf8().constData(), resolvedPath) == NULL) {
            qWarning() << "Failed to resolve symlink:" << symlinkPath << "Error:" << strerror(errno);
            return -1;
        }
        
        QString actualDevicePath = QString(resolvedPath);
        qDebug() << "Symlink:" << symlinkPath << "points to:" << actualDevicePath;
        
        int fd = ::open(actualDevicePath.toUtf8().constData(), O_RDWR);
        if (fd == -1) {
            qWarning() << "Failed to open keyboard device:" << actualDevicePath << "Error:" << strerror(errno);
        } else {
            qDebug() << "Successfully opened keyboard device:" << actualDevicePath;
        }
        return fd;
    }

    void sendKeyEvent(int keyCode, int value) {
        if (keyboardFd == -1) {
            qWarning() << "Keyboard device not open";
            return;
        }

        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ev.time = tv;
        
        ev.type = EV_KEY;
        ev.code = keyCode;
        ev.value = value;
        
        if (::write(keyboardFd, &ev, sizeof(ev)) == -1) {
            qWarning() << "Failed to write key event:" << strerror(errno);
            return;
        }
        
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        if (::write(keyboardFd, &ev, sizeof(ev)) == -1) {
            qWarning() << "Failed to write sync event:" << strerror(errno);
        }
        
        qDebug() << "Sent key event: code=" << keyCode << "value=" << value;
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
        if (obj == dragButton) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    dragPosition = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->buttons() & Qt::LeftButton) {
                    move(mouseEvent->globalPosition().toPoint() - dragPosition);
                    event->accept();
                    return true;
                }
            }
        }
        return QMainWindow::eventFilter(obj, event);
    }

    void loadSettings() {
        QSettings settings("ControlPanel", "App");
        QPoint pos = settings.value("position", QPoint(100, 100)).toPoint();
        move(pos);
    }

    void saveSettings() {
        QSettings settings("ControlPanel", "App");
        settings.setValue("position", pos());
    }

    QPushButton *btnToggle1;
    QPushButton *btnToggle2;
    QPushButton *btnToggle3;
    QPushButton *dragButton;
    
    QString selectedWindow;
    QString selectedKeyboard;
    QString selectedWindowId;
    int keyboardFd = -1;
    Display* xdisplay = nullptr;
    
    bool toggle1State = false;
    bool toggle2State = false;
    bool toggle3State = false;
    QPoint dragPosition;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    ControlPanel window;
    window.show();
    
    return app.exec();
}

#include "main.moc"