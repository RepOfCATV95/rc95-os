#include "precomp.h"

#include <math.h>

#define TCFM_SHOW (WM_USER + 0x200)
#define TCFM_HIDE (WM_USER + 0x201)
#define TCFM_TIMER 1

static const WCHAR szTrayClockFlyoutClass[] = L"TrayClockFlyout";
static const INT CLOCK_FLYOUT_WIDTH = 470;
static const INT CLOCK_FLYOUT_HEIGHT = 300;

class CTrayClockFlyout :
    public CComCoClass<CTrayClockFlyout>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public CWindowImpl<CTrayClockFlyout, CWindow, CControlWinTraits>,
    public IOleWindow
{
    HWND m_hwndOwner;
    RECT m_rcControlPanelLink;

    VOID DrawAnalogClock(HDC hDC, const SYSTEMTIME& st, INT centerX, INT centerY, INT radius)
    {
        HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
        HPEN hOldPen = (HPEN)SelectObject(hDC, hPen);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hDC, GetSysColorBrush(COLOR_WINDOW));

        Ellipse(hDC, centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);

        for (INT i = 0; i < 12; ++i)
        {
            double angle = (i * 3.14159265358979323846) / 6.0;
            INT outerX = centerX + (INT)(sin(angle) * (radius - 4));
            INT outerY = centerY - (INT)(cos(angle) * (radius - 4));
            INT innerRadius = (i % 3 == 0) ? radius - 12 : radius - 8;
            INT innerX = centerX + (INT)(sin(angle) * innerRadius);
            INT innerY = centerY - (INT)(cos(angle) * innerRadius);
            MoveToEx(hDC, innerX, innerY, NULL);
            LineTo(hDC, outerX, outerY);
        }

        const double minuteAngle = ((st.wMinute + st.wSecond / 60.0) * 3.14159265358979323846) / 30.0;
        const double hourAngle = (((st.wHour % 12) + st.wMinute / 60.0) * 3.14159265358979323846) / 6.0;
        MoveToEx(hDC, centerX, centerY, NULL);
        LineTo(hDC,
               centerX + (INT)(sin(hourAngle) * (radius * 0.48)),
               centerY - (INT)(cos(hourAngle) * (radius * 0.48)));
        MoveToEx(hDC, centerX, centerY, NULL);
        LineTo(hDC,
               centerX + (INT)(sin(minuteAngle) * (radius * 0.72)),
               centerY - (INT)(cos(minuteAngle) * (radius * 0.72)));

        HPEN hSecondPen = CreatePen(PS_SOLID, 1, RGB(180, 0, 0));
        SelectObject(hDC, hSecondPen);
        const double secondAngle = (st.wSecond * 3.14159265358979323846) / 30.0;
        MoveToEx(hDC, centerX, centerY, NULL);
        LineTo(hDC,
               centerX + (INT)(sin(secondAngle) * (radius * 0.82)),
               centerY - (INT)(cos(secondAngle) * (radius * 0.82)));

        SelectObject(hDC, hOldPen);
        SelectObject(hDC, hOldBrush);
        DeleteObject(hSecondPen);
        DeleteObject(hPen);
    }

    VOID DrawCalendar(HDC hDC, const SYSTEMTIME& st, const RECT& rcCalendar)
    {
        SYSTEMTIME firstDay = st;
        firstDay.wDay = 1;
        FILETIME firstDayFileTime;
        if (!SystemTimeToFileTime(&firstDay, &firstDayFileTime) ||
            !FileTimeToSystemTime(&firstDayFileTime, &firstDay))
        {
            return;
        }

        WCHAR szMonth[64];
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"MMMM yyyy",
                       szMonth, _ARRAYSIZE(szMonth));

        RECT rcTitle = rcCalendar;
        rcTitle.bottom = rcTitle.top + 26;
        DrawTextW(hDC, szMonth, -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const INT cellWidth = (rcCalendar.right - rcCalendar.left) / 7;
        const INT rowHeight = 24;
        SYSTEMTIME weekday = { 2024, 1, 0, 7, 0, 0, 0, 0 };
        for (INT column = 0; column < 7; ++column)
        {
            WCHAR szWeekday[16];
            GetDateFormatW(LOCALE_USER_DEFAULT, 0, &weekday, L"ddd",
                           szWeekday, _ARRAYSIZE(szWeekday));

            RECT rcCell = {
                rcCalendar.left + column * cellWidth,
                rcTitle.bottom,
                rcCalendar.left + (column + 1) * cellWidth,
                rcTitle.bottom + rowHeight
            };
            DrawTextW(hDC, szWeekday, -1, &rcCell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            FILETIME weekdayFileTime;
            SystemTimeToFileTime(&weekday, &weekdayFileTime);
            ULONGLONG ticks = ((ULONGLONG)weekdayFileTime.dwHighDateTime << 32) |
                              weekdayFileTime.dwLowDateTime;
            ticks += 24ULL * 60 * 60 * 10000000;
            weekdayFileTime.dwHighDateTime = (DWORD)(ticks >> 32);
            weekdayFileTime.dwLowDateTime = (DWORD)ticks;
            FileTimeToSystemTime(&weekdayFileTime, &weekday);
        }

        for (INT day = 1; day <= 31; ++day)
        {
            SYSTEMTIME date = st;
            date.wDay = (WORD)day;
            FILETIME dateFileTime;
            if (!SystemTimeToFileTime(&date, &dateFileTime))
                break;

            SYSTEMTIME normalizedDate;
            if (!FileTimeToSystemTime(&dateFileTime, &normalizedDate) ||
                normalizedDate.wMonth != st.wMonth)
            {
                break;
            }

            INT cellIndex = firstDay.wDayOfWeek + day - 1;
            INT row = cellIndex / 7;
            INT column = cellIndex % 7;
            RECT rcCell = {
                rcCalendar.left + column * cellWidth,
                rcTitle.bottom + rowHeight + row * rowHeight,
                rcCalendar.left + (column + 1) * cellWidth,
                rcTitle.bottom + rowHeight + (row + 1) * rowHeight
            };

            if (day == st.wDay)
            {
                InflateRect(&rcCell, -3, -2);
                FillRect(hDC, &rcCell, GetSysColorBrush(COLOR_HIGHLIGHT));
                SetTextColor(hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
            }
            else
            {
                SetTextColor(hDC, GetSysColor(COLOR_WINDOWTEXT));
            }

            WCHAR szDay[4];
            wsprintfW(szDay, L"%d", day);
            DrawTextW(hDC, szDay, -1, &rcCell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    VOID Show()
    {
        RECT rcOwner;
        RECT rcWork;
        MONITORINFO mi = { sizeof(mi) };

        if (!m_hwndOwner || !::GetWindowRect(m_hwndOwner, &rcOwner))
            return;

        HMONITOR hMonitor = MonitorFromRect(&rcOwner, MONITOR_DEFAULTTONEAREST);
        if (hMonitor && GetMonitorInfoW(hMonitor, &mi))
            rcWork = mi.rcWork;
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);

        INT x = rcOwner.right - CLOCK_FLYOUT_WIDTH;
        INT y = rcOwner.top - CLOCK_FLYOUT_HEIGHT - 4;
        if (y < rcWork.top)
            y = rcOwner.bottom + 4;

        if (x < rcWork.left)
            x = rcWork.left;
        else if (x + CLOCK_FLYOUT_WIDTH > rcWork.right)
            x = rcWork.right - CLOCK_FLYOUT_WIDTH;
        if (y < rcWork.top)
            y = rcWork.top;
        else if (y + CLOCK_FLYOUT_HEIGHT > rcWork.bottom)
            y = rcWork.bottom - CLOCK_FLYOUT_HEIGHT;

        SetWindowPos(HWND_TOPMOST, x, y, CLOCK_FLYOUT_WIDTH, CLOCK_FLYOUT_HEIGHT,
                     SWP_NOACTIVATE);
        SetTimer(TCFM_TIMER, 1000, NULL);
        Invalidate();
        ShowWindow(SW_SHOWNOACTIVATE);
    }

    VOID Hide()
    {
        if (m_hWnd)
        {
            KillTimer(TCFM_TIMER);
            ShowWindow(SW_HIDE);
        }
    }

public:
    CTrayClockFlyout() :
        m_hwndOwner(NULL)
    {
        ZeroMemory(&m_rcControlPanelLink, sizeof(m_rcControlPanelLink));
    }

    HRESULT Initialize(IN HWND hwndOwner)
    {
        m_hwndOwner = hwndOwner;

        DWORD dwStyle = WS_POPUP | WS_BORDER | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        DWORD dwExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        RECT rcWindow = { 0, 0, CLOCK_FLYOUT_WIDTH, CLOCK_FLYOUT_HEIGHT };

        Create(hwndOwner, rcWindow, L"", dwStyle, dwExStyle);
        return m_hWnd ? S_OK : E_FAIL;
    }

    LRESULT OnEraseBackground(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        return TRUE;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(&ps);
        RECT rc;
        GetClientRect(&rc);
        FillRect(hDC, &rc, GetSysColorBrush(COLOR_3DFACE));

        SetBkMode(hDC, TRANSPARENT);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);

        SYSTEMTIME st;
        WCHAR szTime[128];
        WCHAR szWeekday[128];
        WCHAR szDate[128];
        GetLocalTime(&st);
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, szTime, _ARRAYSIZE(szTime));
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"dddd",
                       szWeekday, _ARRAYSIZE(szWeekday));
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL,
                       szDate, _ARRAYSIZE(szDate));

        DrawAnalogClock(hDC, st, 95, 78, 56);

        RECT rcText = { 22, 145, 168, 171 };
        DrawTextW(hDC, szTime, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rcText.top = 174;
        rcText.bottom = 198;
        DrawTextW(hDC, szWeekday, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT rcCalendar = { 196, 24, 450, 216 };
        DrawCalendar(hDC, st, rcCalendar);
        RECT rcDigitalDate = { 196, 221, 450, 245 };
        DrawTextW(hDC, szDate, -1, &rcDigitalDate, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hDC, RGB(0, 0, 180));
        const WCHAR szControlPanelLink[] = L"Date and time settings";
        RECT rcLink = { 16, 260, 454, 286 };
        m_rcControlPanelLink = rcLink;
        DrawTextW(hDC, szControlPanelLink, -1, &rcLink,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        MoveToEx(hDC, rcLink.left + 132, rcLink.bottom - 5, NULL);
        LineTo(hDC, rcLink.right - 132, rcLink.bottom - 5);

        SelectObject(hDC, hOldFont);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnShowFlyout(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        Show();
        return TRUE;
    }

    LRESULT OnHideFlyout(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        Hide();
        return TRUE;
    }

    LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        Hide();
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnActivateApp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (!wParam)
            Hide();
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if (wParam == VK_ESCAPE)
        {
            Hide();
            if (m_hwndOwner)
                SetForegroundWindow(m_hwndOwner);
            return 0;
        }
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        Invalidate();
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
        if (PtInRect(&m_rcControlPanelLink, pt))
        {
            HINSTANCE hResult = ShellExecuteW(m_hWnd, L"open", L"control.exe",
                                              L"timedate.cpl", NULL, SW_SHOWNORMAL);
            if ((INT_PTR)hResult <= 32)
                WARN("Failed to open date and time settings (ShellExecute=%p)\n", hResult);
        }
        return 0;
    }

    DECLARE_NOT_AGGREGATABLE(CTrayClockFlyout)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CTrayClockFlyout)
        COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()

    DECLARE_WND_CLASS_EX(szTrayClockFlyoutClass, CS_DBLCLKS, COLOR_3DFACE)

    BEGIN_MSG_MAP(CTrayClockFlyout)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
        MESSAGE_HANDLER(WM_ACTIVATEAPP, OnActivateApp)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(TCFM_SHOW, OnShowFlyout)
        MESSAGE_HANDLER(TCFM_HIDE, OnHideFlyout)
    END_MSG_MAP()

    STDMETHODIMP GetWindow(HWND* phwnd) override
    {
        if (!phwnd)
            return E_INVALIDARG;
        *phwnd = m_hWnd;
        return S_OK;
    }

    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override
    {
        return E_NOTIMPL;
    }
};

VOID CTrayClockFlyout_Show(IN HWND hwndFlyout)
{
    if (hwndFlyout)
        SendMessageW(hwndFlyout, TCFM_SHOW, 0, 0);
}

VOID CTrayClockFlyout_Hide(IN HWND hwndFlyout)
{
    if (hwndFlyout)
        SendMessageW(hwndFlyout, TCFM_HIDE, 0, 0);
}

HRESULT CTrayClockFlyout_CreateInstance(HWND hwndOwner, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CTrayClockFlyout>(hwndOwner, riid, ppv);
}
