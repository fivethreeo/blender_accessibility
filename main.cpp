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
        setFixedSize(300, 180);

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

        // Window selection with label
        QLabel *windowLabel = new QLabel("Window:", this);
        windowLabel->setStyleSheet("QLabel { color: #cccccc; }");
        mainLayout->addWidget(windowLabel);
        
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

        // OK button
        QPushButton *okButton = new QPushButton("Start Control", this);
        okButton->setFixedHeight(32);
        okButton->setStyleSheet("QPushButton { background-color: #4444ff; color: white; border: 1px solid #6666ff; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #6666ff; }");
        mainLayout->addWidget(okButton);

        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
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

private:
    void populateWindows() {
        if (!xdisplay) return;

        Window root = DefaultRootWindow(xdisplay);
        
        // Get the list of window IDs
        Atom netClientList = XInternAtom(xdisplay, "_NET_CLIENT_LIST", False);
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *clientListProp = NULL;

        if (XGetWindowProperty(xdisplay, root, netClientList, 0, 1024, False, XA_WINDOW,
                              &actualType, &actualFormat, &nitems, &bytesAfter, &clientListProp) == Success) {
            
            if (actualType == XA_WINDOW && actualFormat == 32 && nitems > 0) {
                Window *windows = (Window *)clientListProp;
                
                for (unsigned long i = 0; i < nitems; i++) {
                    Window win = windows[i];
                    
                    // Get window name
                    char *name = NULL;
                    if (XFetchName(xdisplay, win, &name) != 0 && name != NULL) {
                        QString title = QString::fromUtf8(name);
                        if (!title.isEmpty()) {
                            QString windowIdStr = QString::number(win, 16);
                            windowCombo->addItem(title, windowIdStr);
                        }
                        XFree(name);
                        name = NULL;
                    }
                    
                    // Also try _NET_WM_NAME for UTF-8 support
                    Atom netWmName = XInternAtom(xdisplay, "_NET_WM_NAME", False);
                    Atom utf8String = XInternAtom(xdisplay, "UTF8_STRING", False);
                    
                    unsigned char *wmNameProp = NULL;
                    if (XGetWindowProperty(xdisplay, win, netWmName, 0, 1024, False, utf8String,
                                          &actualType, &actualFormat, &nitems, &bytesAfter, &wmNameProp) == Success) {
                        
                        if (actualType == utf8String && actualFormat == 8 && nitems > 0) {
                            QString title = QString::fromUtf8((char *)wmNameProp, nitems);
                            if (!title.isEmpty()) {
                                QString windowIdStr = QString::number(win, 16);
                                // Check if we already added this window
                                bool found = false;
                                for (int j = 0; j < windowCombo->count(); j++) {
                                    if (windowCombo->itemData(j).toString() == windowIdStr) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    windowCombo->addItem(title, windowIdStr);
                                }
                            }
                        }
                        if (wmNameProp) XFree(wmNameProp);
                    }
                }
            }
            if (clientListProp) XFree(clientListProp);
        }
        
        if (windowCombo->count() == 0) {
            windowCombo->addItem("No windows found");
        }
    }

    void populateKeyboards() {
        QDir devDir("/dev/input/by-id");
        QFileInfoList devices = devDir.entryInfoList(QDir::Files);
        
        foreach (const QFileInfo &device, devices) {
            if (device.fileName().contains("kbd")) {
                keyboardCombo->addItem(device.fileName());
            }
        }
        
        if (keyboardCombo->count() == 0) {
            keyboardCombo->addItem("No keyboard devices found");
        }
    }

    QComboBox *windowCombo;
    QComboBox *keyboardCombo;
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
        setFixedSize(160, 120);
        
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
        focusSelectedWindow();
        toggle1State = !toggle1State;
        updateButtonAppearance(btnToggle1, toggle1State);
        sendKeyEvent(KEY_LEFTSHIFT, toggle1State ? 1 : 0);
        QTimer::singleShot(100, this, [this, originalMousePos]() {
            returnMouseToPosition(originalMousePos);
        });
    }

    void onToggle2Clicked() {
        QPoint originalMousePos = QCursor::pos();
        focusSelectedWindow();
        toggle2State = !toggle2State;
        updateButtonAppearance(btnToggle2, toggle2State);
        sendKeyEvent(KEY_LEFTCTRL, toggle2State ? 1 : 0);
        QTimer::singleShot(100, this, [this, originalMousePos]() {
            returnMouseToPosition(originalMousePos);
        });
    }

    void onToggle3Clicked() {
        QPoint originalMousePos = QCursor::pos();
        focusSelectedWindow();
        toggle3State = !toggle3State;
        updateButtonAppearance(btnToggle3, toggle3State);
        sendKeyEvent(KEY_LEFTALT, toggle3State ? 1 : 0);
        QTimer::singleShot(100, this, [this, originalMousePos]() {
            returnMouseToPosition(originalMousePos);
        });
    }

    void onAction4Clicked() {
        QPoint originalMousePos = QCursor::pos();
        // For Blender, we need to ensure the 3D view is active
        focusSelectedWindow();
        
        // Additional steps for Blender: send a harmless key to activate the 3D view
        QTimer::singleShot(150, this, [this, originalMousePos]() {
            // Send a harmless key like ESC first to ensure Blender's 3D view gets focus
            sendKeyEvent(KEY_ESC, 1);
            QTimer::singleShot(50, this, [this, originalMousePos]() { 
                sendKeyEvent(KEY_ESC, 0);
                
                // Then send the actual numpad plus after another delay
                QTimer::singleShot(100, this, [this, originalMousePos]() {
                    sendKeyEvent(KEY_KPPLUS, 1);
                    QTimer::singleShot(50, this, [this, originalMousePos]() { 
                        sendKeyEvent(KEY_KPPLUS, 0);
                        // Return mouse to original position after all operations
                        returnMouseToPosition(originalMousePos);
                    });
                });
            });
        });
    }

    void onCloseClicked() {
        close();
    }

private:
    void setupUI() {
        QWidget *centralWidget = new QWidget(this);
        QGridLayout *gridLayout = new QGridLayout(centralWidget);
        gridLayout->setSpacing(2);
        gridLayout->setContentsMargins(4, 4, 4, 4);

        // First row: Toggle buttons + Close
        btnToggle1 = createLargeButton("Shift", 36);
        btnToggle2 = createLargeButton("Ctrl", 36);
        QPushButton *closeButton = createLargeButton("×", 36);
        closeButton->setStyleSheet("QPushButton { background-color: #ff4444; color: white; border: 1px solid #ff6666; font-size: 16px; } QPushButton:hover { background-color: #ff6666; }");

        gridLayout->addWidget(btnToggle1, 0, 0);
        gridLayout->addWidget(btnToggle2, 0, 1);
        gridLayout->addWidget(closeButton, 0, 2);

        // Second row: Toggle button + Action button + Drag handle
        btnToggle3 = createLargeButton("Alt", 36);
        QPushButton *btnAction4 = createLargeButton("+", 36);
        dragButton = createLargeButton("≡", 36);
        dragButton->setStyleSheet("QPushButton { background-color: #333333; color: white; border: 1px solid #555555; font-size: 20px; } QPushButton:hover { background-color: #444444; }");
        dragButton->setCursor(Qt::SizeAllCursor);

        gridLayout->addWidget(btnToggle3, 1, 0);
        gridLayout->addWidget(btnAction4, 1, 1);
        gridLayout->addWidget(dragButton, 1, 2);

        setCentralWidget(centralWidget);

        // Connect signals
        connect(btnToggle1, &QPushButton::clicked, this, &ControlPanel::onToggle1Clicked);
        connect(btnToggle2, &QPushButton::clicked, this, &ControlPanel::onToggle2Clicked);
        connect(btnToggle3, &QPushButton::clicked, this, &ControlPanel::onToggle3Clicked);
        connect(btnAction4, &QPushButton::clicked, this, &ControlPanel::onAction4Clicked);
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

    void returnMouseToPosition(const QPoint &position) {
        QCursor::setPos(position);
        qDebug() << "Mouse returned to position:" << position;
    }

    void focusSelectedWindow() {
        if (!selectedWindowId.isEmpty() && xdisplay) {
            // Convert window ID from hex string to Window
            bool ok;
            Window windowId = selectedWindowId.toULong(&ok, 16);
            if (ok) {
                qDebug() << "Focusing Blender window:" << selectedWindowId << "(" << windowId << ")";
                
                // More aggressive focus method for Blender
                
                // 1. Raise the window first
                XRaiseWindow(xdisplay, windowId);
                
                // 2. Set input focus
                XSetInputFocus(xdisplay, windowId, RevertToParent, CurrentTime);
                
                // 3. Use NETWM protocol to activate the window
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
                    event.xclient.data.l[0] = 1; // Source indication (1 = application)
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
        
        // Use realpath to resolve the symlink to absolute path
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
        
        // Get current time
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ev.time = tv;
        
        // Set up key event
        ev.type = EV_KEY;
        ev.code = keyCode;
        ev.value = value;
        
        // Write key event
        if (::write(keyboardFd, &ev, sizeof(ev)) == -1) {
            qWarning() << "Failed to write key event:" << strerror(errno);
            return;
        }
        
        // Sync event
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        if (::write(keyboardFd, &ev, sizeof(ev)) == -1) {
            qWarning() << "Failed to write sync event:" << strerror(errno);
        }
        
        qDebug() << "Sent key event: code=" << keyCode << "value=" << value;
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
        // Only allow dragging from the drag button, not from modifier key buttons
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
