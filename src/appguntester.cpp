/*  OpenFIRE App: a configuration utility for the OpenFIRE light gun system.
    Gun Tester: full-screen test screen for guns in normal (non-docked) mode.

    Copyright (C) 2025  Team OpenFIRE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "appguntester.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTime>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#endif // Q_OS_WIN

namespace {
    const QColor backgroundColour(14, 14, 18);
    const QColor panelColour(22, 22, 28, 215);
    const QColor textColour(220, 220, 220);         // Gainsboro
    const QColor dimTextColour(128, 128, 128);
    const QColor warnColour(255, 165, 0);           // Orange
    const QColor errorColour(255, 69, 0);           // OrangeRed
    const QColor disconnectedColour(105, 105, 105); // DimGray

    const QColor p1Colour(255, 70, 70);
    const QColor p2Colour(80, 150, 255);
    const QColor spareColours[] = { QColor(50, 205, 50), QColor(255, 215, 0), QColor(238, 130, 238), QColor(255, 165, 0) };

    constexpr int logLines = 10;
    constexpr int flashMs = 120;
    const QString pointerKey = QStringLiteral("pointer");

    QFont PixelFont(qreal px, bool bold = false)
    {
        QFont f = QGuiApplication::font();
        f.setPixelSize(qMax(1, qRound(px)));
        f.setBold(bold);
        return f;
    }
}

AppGunTester::AppGunTester(const QString &startNote, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Gun Tester"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::BlankCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, backgroundColour);
    setPalette(pal);
    setAutoFillBackground(true);

    if(QScreen *screen = QGuiApplication::primaryScreen())
        setGeometry(screen->geometry());

    clock.start();
    frameTimer.setInterval(15);
    connect(&frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    if(!startNote.isEmpty())
        AddLog(startNote);
}

AppGunTester::~AppGunTester()
{
#ifdef Q_OS_WIN
    UnregisterRawInput();
#endif
}

void AppGunTester::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if(!frameTimer.isActive()) {
#ifdef Q_OS_WIN
        RegisterRawInput();
        if(rawInputActive)
            ScanExisting();
#endif
#ifdef Q_OS_WIN
        if(!rawInputActive)
#endif
            AddLog(tr("All guns share one pointer here; test one gun at a time"));
        frameTimer.start();
    }

    raise();
    activateWindow();
    setFocus();
}

void AppGunTester::closeEvent(QCloseEvent *event)
{
    frameTimer.stop();
#ifdef Q_OS_WIN
    UnregisterRawInput();
#endif
    emit Closed();
    QWidget::closeEvent(event);
}

void AppGunTester::keyPressEvent(QKeyEvent *event)
{
#ifdef Q_OS_WIN
    if(lastKeyFromGun) {
        event->accept();
        return;
    }
#endif

    switch(event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_C:
        ClearCounters();
        break;
    case Qt::Key_G:
        showGrid = !showGrid;
        break;
    case Qt::Key_H:
        showHelp = !showHelp;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void AppGunTester::AddLog(const QString &msg)
{
    log << QTime::currentTime().toString("HH:mm:ss") + "  " + msg;
    while(log.size() > logLines)
        log.removeFirst();
}

void AppGunTester::ClearCounters()
{
    for(auto &gun : guns)
        gun.shots = 0, gun.offscreenShots = 0, gun.rightClicks = 0;
    log.clear();
    AddLog(tr("Counters and log cleared"));
}

QColor AppGunTester::PickColour(const Gun &gun, const QString &pid) const
{
    static const QRegularExpression p1Rx("\\bP\\s?1\\b", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression p2Rx("\\bP\\s?2\\b", QRegularExpression::CaseInsensitiveOption);
    const bool p1 = p1Rx.match(gun.label).hasMatch() || pid == "0001";
    const bool p2 = p2Rx.match(gun.label).hasMatch() || pid == "0002";
    if(p1 && !p2) return p1Colour;
    if(p2 && !p1) return p2Colour;
    return spareColours[guns.size() % (sizeof(spareColours) / sizeof(spareColours[0]))];
}

QString AppGunTester::ShortTag(const Gun &gun)
{
    static const QRegularExpression rx("\\bP\\s?(\\d)\\b", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rx.match(gun.label);
    return m.hasMatch() ? "P" + m.captured(1) : gun.label;
}

// ---------------------------------------------------------------- shared system pointer

AppGunTester::Gun *AppGunTester::PointerGun()
{
#ifdef Q_OS_WIN
    if(rawInputActive)
        return nullptr;
#endif

    auto it = guns.find(pointerKey);
    if(it == guns.end()) {
        Gun gun;
        gun.key = pointerKey;
        gun.label = tr("Pointer (any gun)");
        gun.order = nextOrder++;
        gun.seenAbsolute = true;
        gun.colour = p1Colour;
        it = guns.insert(pointerKey, gun);
    }
    return &it.value();
}

void AppGunTester::mouseMoveEvent(QMouseEvent *event)
{
    Gun *gun = PointerGun();
    if(!gun) return;

#if QT_VERSION_MAJOR > 5
    const QPointF pos = event->position();
#else
    const QPointF pos = event->localPos();
#endif
    gun->x = std::clamp(pos.x() / qMax(1, width() - 1), 0.0, 1.0);
    gun->y = std::clamp(pos.y() / qMax(1, height() - 1), 0.0, 1.0);
    gun->hasPosition = true;
    gun->lastInput = clock.elapsed();
}

void AppGunTester::mousePressEvent(QMouseEvent *event)
{
    Gun *gun = PointerGun();
    if(!gun) return;

    mouseMoveEvent(event);
    switch(event->button()) {
    case Qt::LeftButton:
        gun->left = true;
        ++gun->shots;
        gun->lastFlash = clock.elapsed();
        if(gun->AtEdge()) ++gun->offscreenShots;
        break;
    case Qt::RightButton:   gun->right = true; ++gun->rightClicks; break;
    case Qt::MiddleButton:  gun->middle = true; break;
    case Qt::BackButton:    gun->back = true; break;
    case Qt::ForwardButton: gun->forward = true; break;
    default: break;
    }
}

void AppGunTester::mouseReleaseEvent(QMouseEvent *event)
{
    Gun *gun = PointerGun();
    if(!gun) return;

    switch(event->button()) {
    case Qt::LeftButton:    gun->left = false; break;
    case Qt::RightButton:   gun->right = false; break;
    case Qt::MiddleButton:  gun->middle = false; break;
    case Qt::BackButton:    gun->back = false; break;
    case Qt::ForwardButton: gun->forward = false; break;
    default: break;
    }
    gun->lastInput = clock.elapsed();
}

// ---------------------------------------------------------------- Windows Raw Input

#ifdef Q_OS_WIN

namespace {
    QString DeviceName(HANDLE h)
    {
        UINT size = 0;
        GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &size);
        if(size == 0) return QString();
        std::vector<wchar_t> buf(size + 1, 0);
        if(GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, buf.data(), &size) == (UINT)-1) return QString();
        return QString::fromWCharArray(buf.data());
    }

    // hid.dll is loaded at run time, so the build needs no extra libraries.
    QString ProductString(const QString &devicePath)
    {
        typedef BOOLEAN (__stdcall *HidD_GetProductString_t)(HANDLE, PVOID, ULONG);
        static HidD_GetProductString_t getProductString = nullptr;
        static bool loaded = false;
        if(!loaded) {
            loaded = true;
            if(HMODULE hid = LoadLibraryW(L"hid.dll"))
                getProductString = reinterpret_cast<HidD_GetProductString_t>(
                    reinterpret_cast<void*>(GetProcAddress(hid, "HidD_GetProductString")));
        }
        if(!getProductString || devicePath.isEmpty()) return QString();

        // Access 0 = query only; works on mouse/keyboard collections that Windows holds open.
        HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if(h == INVALID_HANDLE_VALUE) return QString();

        wchar_t buf[256] = {};
        QString product;
        if(getProductString(h, buf, sizeof(buf) - sizeof(wchar_t)))
            product = QString::fromWCharArray(buf).trimmed();
        CloseHandle(h);
        return product;
    }

    QString KeyName(USHORT vkey)
    {
        if(vkey >= '0' && vkey <= '9') return QString(QChar(vkey));
        if(vkey >= 'A' && vkey <= 'Z') return QString(QChar(vkey));
        if(vkey >= VK_F1 && vkey <= VK_F24) return QString("F%1").arg(vkey - VK_F1 + 1);
        switch(vkey) {
        case VK_RETURN:     return "Enter";
        case VK_ESCAPE:     return "Esc";
        case VK_BACK:       return "Backspace";
        case VK_TAB:        return "Tab";
        case VK_SPACE:      return "Space";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:          return "Shift";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:    return "Ctrl";
        case VK_MENU: case VK_LMENU: case VK_RMENU:             return "Alt";
        case VK_LEFT:       return "Left";
        case VK_RIGHT:      return "Right";
        case VK_UP:         return "Up";
        case VK_DOWN:       return "Down";
        default:            return QString("0x%1").arg(vkey, 2, 16, QChar('0')).toUpper();
        }
    }
}

void AppGunTester::RegisterRawInput()
{
    if(rawInputActive) return;

    const HWND hwnd = reinterpret_cast<HWND>(winId());
    RAWINPUTDEVICE devs[2];
    devs[0].usUsagePage = 0x01, devs[0].usUsage = 0x02; // mouse
    devs[1].usUsagePage = 0x01, devs[1].usUsage = 0x06; // keyboard
    for(auto &dev : devs)
        dev.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, dev.hwndTarget = hwnd;

    if(RegisterRawInputDevices(devs, 2, sizeof(RAWINPUTDEVICE)))
        rawInputActive = true;
    else AddLog(tr("ERROR: could not register for raw input (%1)").arg(GetLastError()));
}

void AppGunTester::UnregisterRawInput()
{
    if(!rawInputActive) return;

    RAWINPUTDEVICE devs[2];
    devs[0].usUsagePage = 0x01, devs[0].usUsage = 0x02;
    devs[1].usUsagePage = 0x01, devs[1].usUsage = 0x06;
    for(auto &dev : devs)
        dev.dwFlags = RIDEV_REMOVE, dev.hwndTarget = nullptr;

    RegisterRawInputDevices(devs, 2, sizeof(RAWINPUTDEVICE));
    rawInputActive = false;
}

void AppGunTester::ScanExisting()
{
    UINT count = 0;
    if(GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1 || count == 0) return;
    std::vector<RAWINPUTDEVICELIST> list(count);
    if(GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return;

    for(UINT i = 0; i < count; ++i)
        if(list[i].dwType == RIM_TYPEMOUSE || list[i].dwType == RIM_TYPEKEYBOARD)
            Identify(reinterpret_cast<quintptr>(list[i].hDevice), false, true);

    int found = 0;
    for(const auto &gun : std::as_const(guns))
        if(gun.isOpenFire) ++found;
    AddLog(found ? tr("%1 OpenFIRE gun(s) found").arg(found)
                 : tr("No OpenFIRE guns found yet - move or fire a gun"));
}

// Returns the gun for this handle, or nullptr if the device is not (yet) a gun.
AppGunTester::Gun *AppGunTester::Identify(quintptr handle, bool absoluteSeen, bool quiet)
{
    // Handle 0 = input injected by software (remote control tools, macros), not a real device.
    if(handle == 0) return nullptr;

    auto keyIt = handleToKey.constFind(handle);
    if(keyIt != handleToKey.constEnd()) {
        auto gunIt = guns.find(keyIt.value());
        if(gunIt != guns.end()) {
            if(absoluteSeen) gunIt->seenAbsolute = true;
            return &gunIt.value();
        }
    }
    if(!absoluteSeen && ignoredHandles.contains(handle)) return nullptr;

    static const QRegularExpression vidPidRx("VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
    const QString name = DeviceName(reinterpret_cast<HANDLE>(handle));
    const QRegularExpressionMatch m = vidPidRx.match(name);
    const QString vid = m.hasMatch() ? m.captured(1).toUpper() : QString();
    const QString pid = m.hasMatch() ? m.captured(2).toUpper() : QString();
    const bool isOpenFire = vid == "F143";

    // A device is treated as a gun if it is OpenFIRE (VID F143) or sends absolute mouse positions.
    if(!isOpenFire && !absoluteSeen) {
        ignoredHandles.insert(handle);
        return nullptr;
    }
    ignoredHandles.remove(handle);

    const QString key = m.hasMatch() ? vid + "&" + pid : QString("H%1").arg(handle, 0, 16).toUpper();
    handleToKey.insert(handle, key);

    auto gunIt = guns.find(key);
    if(gunIt == guns.end()) {
        Gun gun;
        gun.key = key;
        gun.isOpenFire = isOpenFire;
        const QString product = ProductString(name);
        gun.label = !product.isEmpty() ? product
                  : m.hasMatch()       ? QString("VID %1 PID %2").arg(vid, pid)
                                       : tr("Absolute mouse");
        gun.order = nextOrder++;
        gun.colour = PickColour(gun, pid);
        gunIt = guns.insert(key, gun);
        AddLog((quiet ? tr("Found: %1") : tr("Connected: %1")).arg(gun.label));
    } else if(!gunIt->connected) {
        gunIt->connected = true;
        AddLog(tr("Reconnected: %1").arg(gunIt->label));
    } else if(gunIt->label.startsWith("VID ") || gunIt->label == tr("Absolute mouse")) {
        // An earlier interface had no product string; try this one.
        const QString product = ProductString(name);
        if(!product.isEmpty()) gunIt->label = product;
    }

    gunIt->handles.insert(handle);
    if(absoluteSeen) gunIt->seenAbsolute = true;
    return &gunIt.value();
}

void AppGunTester::HandleDeviceChange(int change, quintptr handle)
{
    if(change == GIDC_ARRIVAL) {
        Identify(handle, false, false);
    } else if(change == GIDC_REMOVAL) {
        ignoredHandles.remove(handle);
        auto keyIt = handleToKey.find(handle);
        if(keyIt == handleToKey.end()) return;
        const QString key = keyIt.value();
        handleToKey.erase(keyIt);

        auto gunIt = guns.find(key);
        if(gunIt == guns.end()) return;
        gunIt->handles.remove(handle);
        if(gunIt->handles.isEmpty() && gunIt->connected) {
            gunIt->connected = false;
            gunIt->left = gunIt->right = gunIt->middle = gunIt->back = gunIt->forward = false;
            gunIt->keysDown.clear();
            AddLog(tr("DISCONNECTED: %1  (after %2 shots)").arg(gunIt->label).arg(gunIt->shots));
        }
    }
}

void AppGunTester::HandleInput(void *hRawInput)
{
    const HRAWINPUT hRaw = static_cast<HRAWINPUT>(hRawInput);
    UINT size = 0;
    GetRawInputData(hRaw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if(size == 0) return;

    // 8-byte aligned buffer for the RAWINPUT struct.
    std::vector<quint64> buf((size + sizeof(quint64) - 1) / sizeof(quint64));
    if(GetRawInputData(hRaw, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
    const RAWINPUT *raw = reinterpret_cast<const RAWINPUT*>(buf.data());
    const quintptr device = reinterpret_cast<quintptr>(raw->header.hDevice);

    if(raw->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE &mouse = raw->data.mouse;
        const bool absolute = mouse.usFlags & MOUSE_MOVE_ABSOLUTE;

        Gun *gun = Identify(device, absolute, false);
        if(!gun) return;
        gun->lastInput = clock.elapsed();
        if(!gun->connected) {
            gun->connected = true;
            AddLog(tr("Input resumed: %1").arg(gun->label));
        }

        if(absolute) {
            // Physical pixels: the area Windows maps 0..65535 onto, and this window.
            RECT area;
            if(mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) {
                area.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
                area.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
                area.right = area.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
                area.bottom = area.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            } else {
                area.left = 0, area.top = 0;
                area.right = GetSystemMetrics(SM_CXSCREEN);
                area.bottom = GetSystemMetrics(SM_CYSCREEN);
            }
            const double sx = area.left + mouse.lLastX / 65535.0 * (area.right - area.left);
            const double sy = area.top + mouse.lLastY / 65535.0 * (area.bottom - area.top);

            RECT win;
            if(GetWindowRect(reinterpret_cast<HWND>(winId()), &win)) {
                gun->x = std::clamp((sx - win.left) / qMax(1L, win.right - win.left - 1), 0.0, 1.0);
                gun->y = std::clamp((sy - win.top) / qMax(1L, win.bottom - win.top - 1), 0.0, 1.0);
                gun->hasPosition = true;
            }
        }

        const USHORT btn = mouse.usButtonFlags;
        if(btn & RI_MOUSE_LEFT_BUTTON_DOWN) {
            gun->left = true;
            ++gun->shots;
            gun->lastFlash = clock.elapsed();
            if(gun->AtEdge()) ++gun->offscreenShots;
        }
        if(btn & RI_MOUSE_LEFT_BUTTON_UP)       gun->left = false;
        if(btn & RI_MOUSE_RIGHT_BUTTON_DOWN)    gun->right = true, ++gun->rightClicks;
        if(btn & RI_MOUSE_RIGHT_BUTTON_UP)      gun->right = false;
        if(btn & RI_MOUSE_MIDDLE_BUTTON_DOWN)   gun->middle = true;
        if(btn & RI_MOUSE_MIDDLE_BUTTON_UP)     gun->middle = false;
        if(btn & RI_MOUSE_BUTTON_4_DOWN)        gun->back = true;
        if(btn & RI_MOUSE_BUTTON_4_UP)          gun->back = false;
        if(btn & RI_MOUSE_BUTTON_5_DOWN)        gun->forward = true;
        if(btn & RI_MOUSE_BUTTON_5_UP)          gun->forward = false;
    } else if(raw->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD &kb = raw->data.keyboard;
        Gun *gun = Identify(device, false, false);
        lastKeyFromGun = gun != nullptr;
        if(!gun || kb.VKey == 0 || kb.VKey == 0xFF) return;
        gun->lastInput = clock.elapsed();

        const QString name = KeyName(kb.VKey);
        if(kb.Flags & RI_KEY_BREAK)
            gun->keysDown.removeAll(name);
        else if(!gun->keysDown.contains(name))
            gun->keysDown << name;
    }
}

#if QT_VERSION_MAJOR > 5
bool AppGunTester::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
#else
bool AppGunTester::nativeEvent(const QByteArray &eventType, void *message, long *result)
#endif
{
    if(eventType == "windows_generic_MSG") {
        const MSG *msg = static_cast<const MSG*>(message);
        if(msg->message == WM_INPUT)
            HandleInput(reinterpret_cast<void*>(msg->lParam));
        else if(msg->message == WM_INPUT_DEVICE_CHANGE)
            HandleDeviceChange(static_cast<int>(msg->wParam), static_cast<quintptr>(msg->lParam));
    }
    // Always let Windows finish handling the message too.
    return QWidget::nativeEvent(eventType, message, result);
}

#endif // Q_OS_WIN

// ---------------------------------------------------------------- drawing

void AppGunTester::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const int w = width(), h = height();
    const qreal unit = qMax(8.0, h / 90.0);   // scales everything with screen size

    p.fillRect(rect(), backgroundColour);
    if(showGrid) DrawGrid(p, w, h, unit);

    QList<const Gun*> shown;
    for(const auto &gun : std::as_const(guns))
        if(gun.seenAbsolute || gun.isOpenFire)
            shown << &gun;
    std::sort(shown.begin(), shown.end(), [](const Gun *a, const Gun *b) { return a->order < b->order; });

    // Panels: two on the top row (left / right), extras stacked below.
    for(int i = 0; i < shown.size(); ++i)
        DrawPanel(p, *shown.at(i), i % 2 == 1, i / 2, w, unit);
    DrawLog(p, w, h, unit);

    const QFont f = PixelFont(unit * 1.15);
    const QFont fb = PixelFont(unit * 1.5, true);
    if(showHelp) {
        const QString help = tr("Esc close    C clear counters    G grid on/off    H hide this line");
        p.setFont(f);
        p.setPen(QColor(200, 200, 200, 120));
        const qreal tw = QFontMetricsF(f).horizontalAdvance(help);
        p.drawText(QPointF((w - tw) / 2, unit * 0.6 + QFontMetricsF(f).ascent()), help);
    }
    if(shown.isEmpty()) {
        const QString t = tr("Waiting for a gun - move or fire one");
        p.setFont(fb);
        p.setPen(textColour);
        const qreal tw = QFontMetricsF(fb).horizontalAdvance(t);
        p.drawText(QPointF((w - tw) / 2, h * 0.40 + QFontMetricsF(fb).ascent()), t);
    }

    for(const Gun *gun : std::as_const(shown))
        DrawCrosshair(p, *gun, w, h, unit);
}

void AppGunTester::DrawGrid(QPainter &p, int w, int h, qreal unit)
{
    const QPen minor(QColor(38, 38, 48), 1.0);
    const QPen major(QColor(70, 70, 86), 1.5);
    const QPen border(QColor(110, 110, 130), 2.0);
    const QPen target(QColor(150, 150, 170), 2.0);

    for(int i = 1; i < 20; ++i) {
        p.setPen(i % 2 == 0 ? major : minor);
        const qreal x = w * i / 20.0, y = h * i / 20.0;
        p.drawLine(QPointF(x, 0), QPointF(x, h));
        p.drawLine(QPointF(0, y), QPointF(w, y));
    }
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(1, 1, w - 3, h - 3));

    // Targets: centre, edge midpoints, corners (inset 5%).
    const qreal r = unit * 2.2;
    const qreal fractions[] = { 0.05, 0.5, 0.95 };
    p.setPen(target);
    for(const qreal fx : fractions)
        for(const qreal fy : fractions) {
            const QPointF c(w * fx, h * fy);
            p.drawEllipse(c, r, r);
            p.drawEllipse(c, r / 3, r / 3);
            p.drawLine(QPointF(c.x() - r * 1.5, c.y()), QPointF(c.x() + r * 1.5, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - r * 1.5), QPointF(c.x(), c.y() + r * 1.5));
        }
}

void AppGunTester::DrawCrosshair(QPainter &p, const Gun &gun, int w, int h, qreal unit)
{
    if(!gun.hasPosition || !gun.connected) return;

    const qreal cx = gun.x * (w - 1), cy = gun.y * (h - 1);
    const qreal r = unit * 2.8;
    const bool flash = gun.lastFlash >= 0 && clock.elapsed() - gun.lastFlash < flashMs;

    p.setBrush(Qt::NoBrush);
    const QPen pens[2] = { QPen(QColor(0, 0, 0, 160), 6.0), QPen(flash ? Qt::white : gun.colour, flash ? 4.0 : 3.0) };
    for(const QPen &pen : pens) {
        p.setPen(pen);
        p.drawEllipse(QPointF(cx, cy), r, r);
        p.drawLine(QPointF(cx - r * 1.8, cy), QPointF(cx - r * 0.35, cy));
        p.drawLine(QPointF(cx + r * 0.35, cy), QPointF(cx + r * 1.8, cy));
        p.drawLine(QPointF(cx, cy - r * 1.8), QPointF(cx, cy - r * 0.35));
        p.drawLine(QPointF(cx, cy + r * 0.35), QPointF(cx, cy + r * 1.8));
    }
    if(flash) {
        QColor fill = gun.colour;
        fill.setAlpha(150);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawEllipse(QPointF(cx, cy), r * 0.6, r * 0.6);
        p.setBrush(Qt::NoBrush);
    }

    // Player tag beside the crosshair, kept on screen.
    const QFont tagFont = PixelFont(unit * 1.2, true);
    const QFontMetricsF tm(tagFont);
    const QString tag = ShortTag(gun);
    qreal tx = cx + r * 1.2, ty = cy + r * 1.0;
    if(tx + tm.horizontalAdvance(tag) > w) tx = cx - r * 1.2 - tm.horizontalAdvance(tag);
    if(ty + tm.height() > h) ty = cy - r * 1.0 - tm.height();
    p.setFont(tagFont);
    p.setPen(gun.colour);
    p.drawText(QPointF(tx, ty + tm.ascent()), tag);

    if(gun.AtEdge()) {
        const QFont edgeFont = PixelFont(unit * 1.3, true);
        const QFontMetricsF em(edgeFont);
        const QString t = tr("OFF-SCREEN");
        const qreal ex = std::clamp(cx - em.horizontalAdvance(t) / 2, 4.0, qMax(4.0, w - em.horizontalAdvance(t) - 4));
        const qreal ey = std::clamp(cy - r * 2.2 - em.height(), 4.0, qMax(4.0, h - em.height() - 4));
        p.setFont(edgeFont);
        p.setPen(warnColour);
        p.drawText(QPointF(ex, ey + em.ascent()), t);
    }
}

qreal AppGunTester::Lamp(QPainter &p, const QString &label, bool on, qreal x, qreal y, qreal unit, const QColor &c)
{
    const QFontMetricsF fm(p.font());
    const qreal w = fm.horizontalAdvance(label) + unit * 1.2, h = fm.height() + unit * 0.3;
    p.fillRect(QRectF(x, y, w, h), on ? c : QColor(45, 45, 55));
    p.setPen(on ? Qt::black : dimTextColour);
    p.drawText(QPointF(x + unit * 0.6, y + unit * 0.15 + fm.ascent()), label);
    return x + w + unit * 0.6;
}

void AppGunTester::DrawPanel(QPainter &p, const Gun &gun, bool right, int row, int w, qreal unit)
{
    const QFont f = PixelFont(unit * 1.15);
    const QFont fb = PixelFont(unit * 1.5, true);
    const QFontMetricsF fm(f), fbm(fb);

    const qreal pw = unit * 30, lh = fm.height() * 1.15;
    // top padding, title, status, counters, position, two lamp rows, keys, bottom padding
    const qreal ph = unit * 0.5 + fbm.height() * 1.3 + lh * (1 + 1 + 1.25 + 1.35 + 1.35 + 1) + unit * 0.4;
    const qreal px = right ? w - pw - unit * 1.5 : unit * 1.5;
    const qreal py = unit * 3 + row * (ph + unit);

    const QColor edge = gun.connected ? gun.colour : disconnectedColour;
    p.fillRect(QRectF(px, py, pw, ph), panelColour);
    p.setPen(QPen(edge, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(px, py, pw, ph));

    const qreal x = px + unit;
    qreal y = py + unit * 0.5;
    p.setFont(fb);
    p.setPen(edge);
    p.drawText(QPointF(x, y + fbm.ascent()), gun.label);
    y += fbm.height() * 1.3;

    p.setFont(f);
    auto line = [&](const QString &text, const QColor &colour) {
        p.setPen(colour);
        p.drawText(QPointF(x, y + fm.ascent()), text);
    };

    // Status line
    if(!gun.connected) {
        line(tr("DISCONNECTED"), errorColour);
    } else if(gun.lastInput < 0) {
        line(tr("Connected - no input yet"), textColour);
    } else {
        const qint64 s = (clock.elapsed() - gun.lastInput) / 1000;
        line(s < 1 ? tr("Last input just now") : tr("Last input %1s ago").arg(s), s > 10 ? warnColour : textColour);
    }
    y += lh;

    // Counters
    line(tr("Shots %1    Off-screen shots %2    Right clicks %3").arg(gun.shots).arg(gun.offscreenShots).arg(gun.rightClicks), textColour);
    y += lh;

    // Position
    const QString pos = gun.hasPosition
        ? QString("X %1%   Y %2%").arg(gun.x * 100, 5, 'f', 1).arg(gun.y * 100, 5, 'f', 1)
        : tr("No position yet");
    line(pos + (gun.AtEdge() ? tr("   EDGE") : QString()), gun.AtEdge() ? warnColour : textColour);
    y += lh * 1.25;

    // Mouse buttons as lamps
    qreal bx = x;
    bx = Lamp(p, tr("Trigger (L)"), gun.left, bx, y, unit, gun.colour);
    bx = Lamp(p, tr("Right"), gun.right, bx, y, unit, gun.colour);
    bx = Lamp(p, tr("Middle"), gun.middle, bx, y, unit, gun.colour);
    y += lh * 1.35;
    bx = x;
    bx = Lamp(p, tr("Back (X1)"), gun.back, bx, y, unit, gun.colour);
    Lamp(p, tr("Fwd (X2)"), gun.forward, bx, y, unit, gun.colour);
    y += lh * 1.35;

    line(tr("Keys: %1").arg(gun.keysDown.isEmpty() ? "-" : gun.keysDown.join("  ")),
         gun.keysDown.isEmpty() ? dimTextColour : Qt::white);
}

void AppGunTester::DrawLog(QPainter &p, int w, int h, qreal unit)
{
    if(log.isEmpty()) return;

    const QFont f = PixelFont(unit * 1.15);
    const QFontMetricsF fm(f);
    const qreal lh = fm.height() * 1.1;
    qreal longest = 0;
    for(const QString &entry : std::as_const(log))
        longest = qMax(longest, fm.horizontalAdvance(entry));
    const qreal lw = qMin<qreal>(qMax(unit * 46, longest + unit * 1.6), w - unit * 2);
    const qreal lhTotal = lh * log.size() + unit;
    const qreal x = (w - lw) / 2, y = h - lhTotal - unit * 1.5;
    p.fillRect(QRectF(x, y, lw, lhTotal), QColor(22, 22, 28, 200));

    p.setFont(f);
    qreal ty = y + unit * 0.5;
    for(const QString &entry : std::as_const(log)) {
        p.setPen(entry.contains("DISCONNECTED") || entry.contains("ERROR") ? errorColour : QColor(192, 192, 192));
        p.drawText(QPointF(x + unit * 0.8, ty + fm.ascent()), entry);
        ty += lh;
    }
}
