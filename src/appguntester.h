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

#ifndef APPGUNTESTER_H
#define APPGUNTESTER_H

#include <QWidget>
#include <QTimer>
#include <QColor>
#include <QElapsedTimer>
#include <QMap>
#include <QSet>
#include <QStringList>

/// @brief      Full-screen gun test screen
/// @details    On Windows each gun is read separately through Raw Input, the same way MAME does,
///             so several guns can be tested at once. Elsewhere all guns share the system pointer.
///             Recoil and rumble come from the guns' own firmware, as in a game.
class AppGunTester : public QWidget
{
    Q_OBJECT

public:
    /// @param      startNote
    ///             Line added to the event log on opening (e.g. which device was undocked), or empty
    explicit AppGunTester(const QString &startNote = QString(), QWidget *parent = nullptr);
    ~AppGunTester();

signals:
    void Closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    // Used when Raw Input isn't available: everything comes from the one system pointer.
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

#ifdef Q_OS_WIN
#if QT_VERSION_MAJOR > 5
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif
#endif // Q_OS_WIN

private:
    struct Gun {
        QString key;                // VID&PID (shared by all interfaces of one gun)
        QString label;              // product string, or VID/PID
        QColor colour;
        int order = 0;              // order found
        int player = 0;             // 1-4 from the product name ("... P2") or PID 0001-0004, 0 = unknown
        QString pid;
        bool connected = true;
        bool seenAbsolute = false;
        bool isOpenFire = false;

        double x = 0.5, y = 0.5;    // 0..1 across the screen
        bool hasPosition = false;
        bool left = false, right = false, middle = false, back = false, forward = false;
        QStringList keysDown;

        int shots = 0;              // trigger (left button) presses
        int offscreenShots = 0;     // trigger presses while at an edge
        int rightClicks = 0;
        qint64 lastInput = -1;      // ms on the tester's clock, -1 = none yet
        qint64 lastFlash = -1;      // trigger flash
        QSet<quintptr> handles;

        bool AtEdge() const { return hasPosition && (x <= 0.002 || x >= 0.998 || y <= 0.002 || y >= 0.998); }
    };

    void AddLog(const QString &msg);
    void ClearCounters();
    QColor PickColour(const Gun &gun, const QString &pid) const;
    static QString ShortTag(const Gun &gun);
    static int PlayerNumber(const Gun &gun);

    void DrawGrid(QPainter &p, int w, int h, qreal unit);
    void DrawCrosshair(QPainter &p, const Gun &gun, int w, int h, qreal unit);
    void DrawPanel(QPainter &p, const Gun &gun, bool right, int row, int w, qreal unit);
    void DrawLog(QPainter &p, int w, int h, qreal unit);
    qreal Lamp(QPainter &p, const QString &label, bool on, qreal x, qreal y, qreal unit, const QColor &c);

#ifdef Q_OS_WIN
    void RegisterRawInput();
    void UnregisterRawInput();
    void ScanExisting();
    Gun *Identify(quintptr handle, bool absoluteSeen, bool quiet);
    void HandleInput(void *hRawInput);
    void HandleDeviceChange(int change, quintptr handle);

    QMap<quintptr, QString> handleToKey;
    QSet<quintptr> ignoredHandles;              // devices that aren't guns
    bool rawInputActive = false;

    // Set from Raw Input just before the matching key press arrives, so keys sent by a gun
    // (e.g. Start / Coin mapped to keys) never trigger the tester's shortcuts.
    bool lastKeyFromGun = false;
#endif // Q_OS_WIN

    Gun *PointerGun();                          // the one shared pointer, without Raw Input

    QMap<QString, Gun> guns;
    QStringList log;
    QTimer frameTimer;
    QElapsedTimer clock;
    bool showGrid = true;
    bool showHelp = true;
    int nextOrder = 0;
};

#endif // APPGUNTESTER_H
