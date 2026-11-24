/*
 * LineCursor - one or two full-monitor lines that follow the mouse,
 * with a small settings window.  Closing the window exits.
 * MIT License, Copyright (c) 2026 zmlworks
 */
#define _WIN32_WINNT 0x0A00
#define _WIN32_IE 0x0600
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string.h>

#define SETTINGS_CLASS L"zmlworks.LineCursor.Settings"
#define LINE_CLASS     L"zmlworks.LineCursor.LineWnd"
#define MUTEX_NAME     L"Local\\zmlworks.LineCursor"

#define ARROW_BELOW_N  19
#define ARROW_RIGHT_N  17
#define ARROW_DEN      32
#define TIMER_MS       16
#define TOPMOST_TICKS  30

#define POS_MIN   (-30)
#define POS_MAX   30
#define THICK_MIN 1
#define THICK_MAX 30
#define GAP_MIN   1
#define GAP_MAX   50

#define IDC_PAUSE         160
#define IDC_POS_SLIDER    100
#define IDC_POS_LAB       1002
#define IDC_THICK_SLIDER  110
#define IDC_THICK_LAB     1112
#define IDC_COUNT_LAB     1220
#define IDC_COUNT1        120
#define IDC_COUNT2        121
#define IDC_GAP_SLIDER    130
#define IDC_GAP_LAB       1332
#define IDC_VERTICAL      140

#define UI_W  186

typedef HRESULT (WINAPI *GetDpiForMonitor_t)(HMONITOR, int, UINT *, UINT *);
typedef int     (WINAPI *GetSystemMetricsForDpi_t)(int, UINT);
typedef UINT    (WINAPI *GetDpiForWindow_t)(HWND);

typedef struct {
    HWND    wnd;
    HBITMAP bmp;
    void   *bits;
    int     w, h, x, y, dpi;
    int     visible;
} Overlay;

typedef struct {
    int pos, thick, count, gap, vertical;
} Settings;

static HINSTANCE gInst;
static HWND      gSettings;
static HFONT     gFont;
static Overlay   gOv;
static Settings  gSet = { 0, 10, 1, 20, 0 };
static UINT      gTick;
static int       gReady;
static int       gPaused;
static UINT      gUiDpi = 96;

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int scale_px(int n, UINT dpi)
{
    int s, neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    s = ((int)dpi * n + 48) / 96;
    return neg ? -s : s;
}

static UINT dpi_for_monitor(HMONITOR mon)
{
    static GetDpiForMonitor_t fn;
    static int inited;
    UINT x = 0, y = 0;
    HDC hdc;
    int dpi;

    if (!inited) {
        HMODULE h = LoadLibraryW(L"shcore.dll");
        if (h) fn = (GetDpiForMonitor_t)GetProcAddress(h, "GetDpiForMonitor");
        inited = 1;
    }
    if (fn && mon && fn(mon, 0, &x, &y) == S_OK && x > 0) return x;

    hdc = GetDC(NULL);
    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi > 0 ? (UINT)dpi : 96;
}

static UINT dpi_for_window(HWND wnd)
{
    static GetDpiForWindow_t fn;
    static int inited;
    UINT dpi;

    if (!inited) {
        fn = (GetDpiForWindow_t)GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        inited = 1;
    }
    if (fn && wnd) {
        dpi = fn(wnd);
        if (dpi > 0) return dpi;
    }
    return dpi_for_monitor(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST));
}

static int metrics_for_dpi(int idx, UINT dpi)
{
    static GetSystemMetricsForDpi_t fn;
    static int inited;

    if (!inited) {
        fn = (GetSystemMetricsForDpi_t)GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
        inited = 1;
    }
    if (fn && dpi) return fn(idx, dpi);
    return GetSystemMetrics(idx);
}

static int scan_opaque_ext(HBITMAP hbm, int *bottom, int *right)
{
    BITMAP bm;
    BITMAPINFO bi;
    HDC hdc;
    BYTE *bits;
    int x, y, stride, ok, saw_alpha, found = 0;
    int b = -1, r = -1;

    if (!hbm || !GetObjectW(hbm, sizeof bm, &bm)) return 0;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) return 0;

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    stride = bm.bmWidth * 4;
    bits = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                             (SIZE_T)stride * (SIZE_T)bm.bmHeight);
    if (!bits) return 0;

    hdc = CreateCompatibleDC(NULL);
    ok = GetDIBits(hdc, hbm, 0, bm.bmHeight, bits, &bi, DIB_RGB_COLORS);
    DeleteDC(hdc);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, bits);
        return 0;
    }

    saw_alpha = 0;
    for (y = 0; y < bm.bmHeight && !saw_alpha; y++) {
        DWORD *row = (DWORD *)(bits + (SIZE_T)y * stride);
        for (x = 0; x < bm.bmWidth; x++)
            if ((BYTE)(row[x] >> 24) != 0) { saw_alpha = 1; break; }
    }
    for (y = 0; y < bm.bmHeight; y++) {
        DWORD *row = (DWORD *)(bits + (SIZE_T)y * stride);
        for (x = 0; x < bm.bmWidth; x++) {
            DWORD p = row[x];
            BYTE a = (BYTE)(p >> 24);
            BYTE rc = (BYTE)(p >> 16), g = (BYTE)(p >> 8), bc = (BYTE)p;
            if (saw_alpha ? (a > 64) : (rc | g | bc) != 0) {
                if (y > b) b = y;
                if (x > r) r = x;
                found = 1;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, bits);
    if (!found) return 0;
    *bottom = b;
    *right = r;
    return 1;
}

/* Pixels from the arrow hotspot to just past the visible base / right edge. */
static void measure_arrow(int cell_x, int cell_y, int *below, int *right)
{
    ICONINFO ii;
    HCURSOR cur;
    int bot = -1, rgt = -1, bmp_w = 0, bmp_h = 0, ok = 0;
    BITMAP bm;

    memset(&ii, 0, sizeof ii);
    cur = LoadCursorW(NULL, IDC_ARROW);
    if (cur && GetIconInfo(cur, &ii)) {
        if (ii.hbmColor) {
            if (GetObjectW(ii.hbmColor, sizeof bm, &bm)) {
                bmp_w = bm.bmWidth;
                bmp_h = bm.bmHeight;
            }
            ok = scan_opaque_ext(ii.hbmColor, &bot, &rgt);
        }
        if (!ok && ii.hbmMask) {
            if (bmp_h <= 0 && GetObjectW(ii.hbmMask, sizeof bm, &bm)) {
                bmp_w = bm.bmWidth;
                bmp_h = bm.bmHeight > 1 ? bm.bmHeight / 2 : bm.bmHeight;
            }
            ok = scan_opaque_ext(ii.hbmMask, &bot, &rgt);
            if (ok && bot >= bmp_h && bmp_h > 0) bot = bmp_h - 1;
        }
        if (ok) {
            bot = bot - (int)ii.yHotspot + 1;
            rgt = rgt - (int)ii.xHotspot + 1;
            if (bmp_h > 0 && cell_y > 0 && bmp_h != cell_y && bot > 0)
                bot = (bot * cell_y + bmp_h / 2) / bmp_h;
            if (bmp_w > 0 && cell_x > 0 && bmp_w != cell_x && rgt > 0)
                rgt = (rgt * cell_x + bmp_w / 2) / bmp_w;
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
    if (bot < cell_y * 8 / 32 || bot > cell_y * 28 / 32)
        bot = cell_y * ARROW_BELOW_N / ARROW_DEN;
    if (rgt < cell_x * 6 / 32 || rgt > cell_x * 28 / 32)
        rgt = cell_x * ARROW_RIGHT_N / ARROW_DEN;
    if (bot < 1) bot = 1;
    if (rgt < 1) rgt = 1;
    *below = bot;
    *right = rgt;
}

/* Default 位置 is the previous "just below the arrow base", expressed
 * in the new tip-relative scale (0 = hotspot, negative = down).
 * Measured once at 96 DPI so the slider value is in logical px. */
static int default_pos_96(void)
{
    int cx, cy, below, right;
    cx = metrics_for_dpi(SM_CXCURSOR, 96);
    cy = metrics_for_dpi(SM_CYCURSOR, 96);
    if (cx < 16) cx = 32;
    if (cy < 16) cy = 32;
    measure_arrow(cx, cy, &below, &right);
    (void)right;
    return clampi(-below, POS_MIN, POS_MAX);
}

static void free_overlay_bmp(Overlay *o)
{
    if (o->bmp) {
        DeleteObject(o->bmp);
        o->bmp = NULL;
        o->bits = NULL;
    }
}

static int line_extent(UINT dpi, int *thick, int *gap)
{
    int t, g, n;

    t = scale_px(gSet.thick, dpi);
    if (t < 1) t = 1;
    g = (gSet.count >= 2) ? scale_px(gSet.gap, dpi) : 0;
    if (g < 0) g = 0;
    n = (gSet.count >= 2) ? 2 : 1;
    *thick = t;
    *gap = g;
    return n * t + (n - 1) * g;
}

static int rebuild_overlay_bmp(Overlay *o, int width, int height,
                               int thick, int gap, int count, int vertical)
{
    BITMAPINFO bi;
    HDC hdc;
    DWORD *px;
    int x, y, n, start;

    free_overlay_bmp(o);
    if (width < 1 || height < 1) return 0;

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    hdc = GetDC(NULL);
    o->bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &o->bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!o->bmp || !o->bits) { free_overlay_bmp(o); return 0; }

    px = (DWORD *)o->bits;
    memset(px, 0, (SIZE_T)width * (SIZE_T)height * 4);
    if (count < 1) count = 1;
    if (count > 2) count = 2;
    for (n = 0; n < count; n++) {
        start = n * (thick + gap);
        if (!vertical) {
            for (y = start; y < start + thick && y < height; y++)
                for (x = 0; x < width; x++)
                    px[(SIZE_T)y * width + x] = 0xFF000000u;
        } else {
            for (x = start; x < start + thick && x < width; x++)
                for (y = 0; y < height; y++)
                    px[(SIZE_T)y * width + x] = 0xFF000000u;
        }
    }
    o->w = width;
    o->h = height;
    return 1;
}

static int apply_layer(Overlay *o, int x, int y)
{
    HDC scr, mem;
    HBITMAP old;
    POINT dst, src;
    SIZE sz;
    BLENDFUNCTION bl;
    int ok;

    if (!o->wnd || !o->bmp) return 0;
    scr = GetDC(NULL);
    mem = CreateCompatibleDC(scr);
    old = (HBITMAP)SelectObject(mem, o->bmp);
    dst.x = x;
    dst.y = y;
    src.x = 0;
    src.y = 0;
    sz.cx = o->w;
    sz.cy = o->h;
    bl.BlendOp = AC_SRC_OVER;
    bl.BlendFlags = 0;
    bl.SourceConstantAlpha = 255;
    bl.AlphaFormat = AC_SRC_ALPHA;
    ok = UpdateLayeredWindow(o->wnd, scr, &dst, &sz, mem, &src, 0, &bl, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    if (ok) {
        o->x = x;
        o->y = y;
    }
    return ok;
}

static void clamp_rect(int *x, int *y, int w, int h, const RECT *m)
{
    if (*x + w > m->right) *x = m->right - w;
    if (*x < m->left) *x = m->left;
    if (*y + h > m->bottom) *y = m->bottom - h;
    if (*y < m->top) *y = m->top;
}

static void hide_overlay(Overlay *o)
{
    if (o->wnd && o->visible) {
        ShowWindow(o->wnd, SW_HIDE);
        o->visible = 0;
    }
}

static void place_overlay(Overlay *o, int x, int y, int w, int h,
                          UINT dpi, int thick, int gap, int count,
                          int vertical, int force, int reassert)
{
    int resized, moved;

    resized = force || w != o->w || h != o->h || (int)dpi != o->dpi || !o->bmp;
    moved = resized || x != o->x || y != o->y || !o->visible;
    if (!moved) {
        if (reassert)
            SetWindowPos(o->wnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    if (resized) {
        o->dpi = (int)dpi;
        if (!rebuild_overlay_bmp(o, w, h, thick, gap, count, vertical))
            return;
        if (!apply_layer(o, x, y)) return;
        SetWindowPos(o->wnd, HWND_TOPMOST, x, y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        o->visible = 1;
    } else {
        SetWindowPos(o->wnd, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | (o->visible ? 0 : SWP_SHOWWINDOW));
        o->x = x;
        o->y = y;
        o->visible = 1;
    }
}

static void follow(int force)
{
    POINT pt;
    HMONITOR mon;
    MONITORINFO mi;
    UINT dpi;
    int thick, gap, extent, pos, count, x, y, w, h, reassert;

    if (gPaused) {
        hide_overlay(&gOv);
        return;
    }
    if (!GetCursorPos(&pt)) return;
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return;

    dpi = dpi_for_monitor(mon);
    extent = line_extent(dpi, &thick, &gap);
    count = (gSet.count >= 2) ? 2 : 1;
    pos = scale_px(gSet.pos, dpi);
    reassert = ((gTick % TOPMOST_TICKS) == 0);

    /* 0 = hotspot. Negative moves the near edge down (or right if vertical). */
    if (gSet.vertical) {
        w = extent;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        x = pt.x - pos;
        y = mi.rcMonitor.top;
    } else {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = extent;
        x = mi.rcMonitor.left;
        y = pt.y - pos;
    }
    clamp_rect(&x, &y, w, h, &mi.rcMonitor);
    place_overlay(&gOv, x, y, w, h, dpi, thick, gap, count, gSet.vertical,
                  force, reassert);
}

static LRESULT CALLBACK LineProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_CLOSE) return 0;
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HWND create_overlay(void)
{
    return CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        LINE_CLASS, L"LineCursor",
        WS_POPUP, 0, 0, 0, 0, NULL, NULL, gInst, NULL);
}

static void destroy_overlays(void)
{
    if (gOv.wnd) { DestroyWindow(gOv.wnd); gOv.wnd = NULL; }
    free_overlay_bmp(&gOv);
}

/* ---------- settings UI ---------- */

static int sx(int n) { return scale_px(n, gUiDpi); }

static HWND ctrl(int id)
{
    return gSettings ? GetDlgItem(gSettings, id) : NULL;
}

typedef struct { const WCHAR *want; WCHAR *got; } FontPick;

static int CALLBACK font_enum_proc(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                   DWORD type, LPARAM lp)
{
    FontPick *p = (FontPick *)lp;
    (void)tm;
    (void)type;
    if (lstrcmpiW(lf->lfFaceName, p->want) == 0) {
        lstrcpynW(p->got, lf->lfFaceName, LF_FACESIZE);
        return 0;
    }
    return 1;
}

static HFONT make_ui_font(UINT dpi)
{
    static const WCHAR *names[] = {
        L"Yu Gothic UI", L"Meiryo UI", L"Yu Gothic", L"Meiryo",
        L"MS UI Gothic", L"Noto Sans CJK JP", L"Noto Sans CJK",
        NULL
    };
    HDC hdc = GetDC(NULL);
    int i, height = -MulDiv(9, (int)dpi, 72);
    WCHAR found[LF_FACESIZE];

    for (i = 0; names[i]; i++) {
        FontPick pick;
        LOGFONTW lf;
        memset(&lf, 0, sizeof lf);
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(lf.lfFaceName, names[i], LF_FACESIZE);
        pick.want = names[i];
        pick.got = found;
        found[0] = 0;
        EnumFontFamiliesExW(hdc, &lf, font_enum_proc, (LPARAM)&pick, 0);
        if (found[0]) {
            HFONT f = CreateFontW(height, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, found);
            ReleaseDC(NULL, hdc);
            if (f) return f;
        }
    }
    ReleaseDC(NULL, hdc);
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}

static void apply_font(HWND wnd, HFONT font)
{
    HWND c;
    SendMessageW(wnd, WM_SETFONT, (WPARAM)font, TRUE);
    for (c = GetWindow(wnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
}

static void set_named_val(int id, const WCHAR *name, int v)
{
    WCHAR buf[40];
    HWND w = ctrl(id);
    if (!w) return;
    wsprintfW(buf, L"%s  %d px", name, v);
    SetWindowTextW(w, buf);
}

static int slider_pos(int id)
{
    HWND w = ctrl(id);
    if (!w) return 0;
    return (int)SendMessageW(w, TBM_GETPOS, 0, 0);
}

static void enable_gap(int on)
{
    EnableWindow(ctrl(IDC_GAP_SLIDER), on);
    EnableWindow(ctrl(IDC_GAP_LAB), on);
}

static void read_ui(void)
{
    int old_count = gSet.count, old_vert = gSet.vertical, changed = 0;
    Settings n = gSet;

    n.pos = clampi(slider_pos(IDC_POS_SLIDER), POS_MIN, POS_MAX);
    n.thick = clampi(slider_pos(IDC_THICK_SLIDER), THICK_MIN, THICK_MAX);
    n.gap = clampi(slider_pos(IDC_GAP_SLIDER), GAP_MIN, GAP_MAX);
    if (IsDlgButtonChecked(gSettings, IDC_COUNT2) == BST_CHECKED) n.count = 2;
    else n.count = 1;
    n.vertical = (IsDlgButtonChecked(gSettings, IDC_VERTICAL) == BST_CHECKED) ? 1 : 0;

    set_named_val(IDC_POS_LAB, L"位置", n.pos);
    set_named_val(IDC_THICK_LAB, L"太さ", n.thick);
    set_named_val(IDC_GAP_LAB, L"余白", n.gap);

    if (n.pos != gSet.pos || n.thick != gSet.thick || n.count != gSet.count ||
        n.gap != gSet.gap || n.vertical != gSet.vertical)
        changed = 1;
    gSet = n;
    if (n.count != old_count) enable_gap(n.count >= 2);
    if (changed) follow(1 || (n.vertical != old_vert));
    (void)old_vert;
}

static void toggle_pause(void)
{
    gPaused = !gPaused;
    if (ctrl(IDC_PAUSE))
        SetWindowTextW(ctrl(IDC_PAUSE), gPaused ? L"再開" : L"一時停止");
    if (gPaused) hide_overlay(&gOv);
    else follow(1);
}

static void relayout(void)
{
    int m = sx(10);
    int inner_w = sx(UI_W);
    int cw = inner_w - 2 * m;
    int lab_h = sx(18);
    int sh = sx(24);
    int bh = sx(26);
    int rh = sx(22);
    int y = m;
    int radio_w = sx(36);
    RECT wr, cr;
    int ww, wh;

    MoveWindow(ctrl(IDC_PAUSE), m, y, cw, bh, TRUE);
    y += bh + sx(10);

    MoveWindow(ctrl(IDC_POS_LAB),    m, y, cw, lab_h, TRUE);
    y += lab_h;
    MoveWindow(ctrl(IDC_POS_SLIDER), m, y, cw, sh, TRUE);
    y += sh + sx(6);

    MoveWindow(ctrl(IDC_THICK_LAB),    m, y, cw, lab_h, TRUE);
    y += lab_h;
    MoveWindow(ctrl(IDC_THICK_SLIDER), m, y, cw, sh, TRUE);
    y += sh + sx(8);

    MoveWindow(ctrl(IDC_COUNT_LAB), m, y, sx(40), rh, TRUE);
    MoveWindow(ctrl(IDC_COUNT1),    m + sx(44), y, radio_w, rh, TRUE);
    MoveWindow(ctrl(IDC_COUNT2),    m + sx(44) + radio_w + sx(4), y, radio_w, rh, TRUE);
    y += rh + sx(8);

    MoveWindow(ctrl(IDC_GAP_LAB),    m, y, cw, lab_h, TRUE);
    y += lab_h;
    MoveWindow(ctrl(IDC_GAP_SLIDER), m, y, cw, sh, TRUE);
    y += sh + sx(8);

    MoveWindow(ctrl(IDC_VERTICAL), m, y, cw, rh, TRUE);
    y += rh + m;

    GetWindowRect(gSettings, &wr);
    GetClientRect(gSettings, &cr);
    ww = inner_w + (wr.right - wr.left) - (cr.right - cr.left);
    wh = y + (wr.bottom - wr.top) - (cr.bottom - cr.top);
    SetWindowPos(gSettings, NULL, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static HWND add_static(HWND p, int id, const WCHAR *text)
{
    return CreateWindowW(L"STATIC", text,
                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                         0, 0, 0, 0, p, (HMENU)(INT_PTR)id, gInst, NULL);
}

static void add_slider(HWND p, int id, int lo, int hi, int val, int page)
{
    HWND tb = CreateWindowW(TRACKBAR_CLASSW, NULL,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                            TBS_HORZ | TBS_NOTICKS | TBS_BOTH,
                            0, 0, 0, 0, p, (HMENU)(INT_PTR)id, gInst, NULL);
    SendMessageW(tb, TBM_SETRANGEMIN, FALSE, lo);
    SendMessageW(tb, TBM_SETRANGEMAX, TRUE, hi);
    SendMessageW(tb, TBM_SETLINESIZE, 0, 1);
    SendMessageW(tb, TBM_SETPAGESIZE, 0, page);
    SendMessageW(tb, TBM_SETPOS, TRUE, val);
}

static HWND add_radio(HWND p, int id, const WCHAR *text, int group)
{
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON;
    if (group) st |= WS_GROUP;
    return CreateWindowW(L"BUTTON", text, st,
                         0, 0, 0, 0, p, (HMENU)(INT_PTR)id, gInst, NULL);
}

static void create_controls(HWND p)
{
    CreateWindowW(L"BUTTON", L"一時停止",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_GROUP,
                  0, 0, 0, 0, p, (HMENU)(INT_PTR)IDC_PAUSE, gInst, NULL);

    add_static(p, IDC_POS_LAB, L"位置");
    add_slider(p, IDC_POS_SLIDER, POS_MIN, POS_MAX, gSet.pos, 5);

    add_static(p, IDC_THICK_LAB, L"太さ");
    add_slider(p, IDC_THICK_SLIDER, THICK_MIN, THICK_MAX, gSet.thick, 5);

    add_static(p, IDC_COUNT_LAB, L"本数");
    add_radio(p, IDC_COUNT1, L"1", 1);
    add_radio(p, IDC_COUNT2, L"2", 0);
    CheckDlgButton(p, gSet.count >= 2 ? IDC_COUNT2 : IDC_COUNT1, BST_CHECKED);

    add_static(p, IDC_GAP_LAB, L"余白");
    add_slider(p, IDC_GAP_SLIDER, GAP_MIN, GAP_MAX, gSet.gap, 5);

    CreateWindowW(L"BUTTON", L"縦にする",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                  0, 0, 0, 0, p, (HMENU)(INT_PTR)IDC_VERTICAL, gInst, NULL);
    CheckDlgButton(p, IDC_VERTICAL, gSet.vertical ? BST_CHECKED : BST_UNCHECKED);

    set_named_val(IDC_POS_LAB, L"位置", gSet.pos);
    set_named_val(IDC_THICK_LAB, L"太さ", gSet.thick);
    set_named_val(IDC_GAP_LAB, L"余白", gSet.gap);
    enable_gap(gSet.count >= 2);
}

static void on_ui_dpi(HWND wnd, UINT dpi)
{
    gUiDpi = dpi ? dpi : 96;
    if (gFont) { DeleteObject(gFont); gFont = NULL; }
    gFont = make_ui_font(gUiDpi);
    apply_font(wnd, gFont);
    relayout();
}

static LRESULT CALLBACK SettingsProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        gSettings = wnd;
        gUiDpi = dpi_for_window(wnd);
        create_controls(wnd);
        on_ui_dpi(wnd, gUiDpi);
        SetTimer(wnd, 1, TIMER_MS, NULL);
        gReady = 1;
        follow(1);
        return 0;
    case WM_TIMER:
        gTick++;
        if (!gPaused) follow(0);
        return 0;
    case WM_HSCROLL:
        if (gReady && lp) read_ui();
        return 0;
    case WM_COMMAND:
        if (!gReady) return 0;
        if (HIWORD(wp) == BN_CLICKED) {
            if (LOWORD(wp) == IDC_PAUSE) toggle_pause();
            else read_ui();
            return 0;
        }
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        gUiDpi = HIWORD(wp);
        on_ui_dpi(wnd, gUiDpi);
        if (r)
            SetWindowPos(wnd, NULL, r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if (!gPaused) follow(1);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        if (!gPaused) follow(1);
        return 0;
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        gReady = 0;
        KillTimer(wnd, 1);
        destroy_overlays();
        gSettings = NULL;
        if (gFont) { DeleteObject(gFont); gFont = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static int activate_existing(void)
{
    HWND w = FindWindowW(SETTINGS_CLASS, NULL);
    if (!w) return 0;
    if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    return 1;
}

static void center_on_cursor(HWND wnd)
{
    POINT pt;
    HMONITOR mon;
    MONITORINFO mi;
    RECT wr;
    int x, y, w, h;

    if (!GetCursorPos(&pt)) {
        pt.x = 0;
        pt.y = 0;
    }
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return;
    GetWindowRect(wnd, &wr);
    w = wr.right - wr.left;
    h = wr.bottom - wr.top;
    x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    SetWindowPos(wnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    HANDLE mux;
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    DWORD err;
    HWND wnd;

    (void)prev;
    (void)cmd;
    (void)show;
    gInst = inst;
    gSet.pos = default_pos_96();

    mux = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    err = GetLastError();
    if (!mux) return 1;
    if (err == ERROR_ALREADY_EXISTS) {
        activate_existing();
        CloseHandle(mux);
        return 0;
    }

    memset(&icc, 0, sizeof icc);
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SETTINGS_CLASS;
    if (!RegisterClassExW(&wc)) { CloseHandle(mux); return 1; }

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = LineProc;
    wc.hInstance = inst;
    wc.lpszClassName = LINE_CLASS;
    if (!RegisterClassExW(&wc)) { CloseHandle(mux); return 1; }

    gOv.wnd = create_overlay();
    if (!gOv.wnd) {
        destroy_overlays();
        CloseHandle(mux);
        return 1;
    }

    wnd = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW,
                          SETTINGS_CLASS, L"LineCursor",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, sx(UI_W), sx(320),
                          NULL, NULL, inst, NULL);
    if (!wnd) {
        destroy_overlays();
        CloseHandle(mux);
        return 1;
    }
    center_on_cursor(wnd);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(gSettings, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(mux);
    return 0;
}
