/* Custom-drawn controls: Gantt chart, memory map, and the container "page" used by the tabs. */
#include "gui.h"
#include <stdio.h>
#include <string.h>

static const COLORREF PAL[12] = {
    RGB(59,125,216), RGB(224,123,57), RGB(63,166,107), RGB(180,79,176), RGB(190,140,20), RGB(42,161,176),
    RGB(196,71,58),  RGB(107,142,35), RGB(122,92,199),  RGB(204,102,153), RGB(95,127,143), RGB(140,109,49)
};
COLORREF pid_color(int pid) { return PAL[((pid % 12) + 12) % 12]; }

static const char *name_of(const SchedResult *s, int pid)
{
    for (int i = 0; i < s->n; i++) if (s->r[i].p.pid == pid) return s->r[i].p.name;
    return "?";
}

static int text_w(HDC dc, const char *t)
{
    SIZE sz; GetTextExtentPoint32A(dc, t, (int)strlen(t), &sz); return sz.cx;
}

static void draw_note(HDC dc, int x, int y, const char *t, COLORREF c)
{
    SetTextColor(dc, c); TextOutA(dc, x, y, t, (int)strlen(t));
}

/* ---------------------------------------------------------------- Gantt chart */
static void paint_gantt(HDC dc, const RECT *rc)
{
    FillRect(dc, rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    if (!g.cur || g.cur->sched.ng == 0 || g.cur->sched.total_time <= 0) {
        draw_note(dc, 10, 10, "Gantt chart: run a simulation to see which process uses the CPU and when.", RGB(110,110,110));
        return;
    }
    const SchedResult *s = &g.cur->sched;
    char buf[64];
    snprintf(buf, sizeof buf, "Gantt chart  (time 0 to %d)", s->total_time);
    draw_note(dc, 10, 4, buf, RGB(30,40,50));

    int margin = 10, barTop = 26, barH = 36;
    double scale = (double)(rc->right - 2 * margin) / s->total_time;
    int lastRight = -100, lastEnd = 0;
    HBRUSH hatch = CreateHatchBrush(HS_BDIAGONAL, RGB(150,158,166));
    HBRUSH frame = CreateSolidBrush(RGB(255,255,255));

    for (int i = 0; i < s->ng; i++) {
        const GanttSeg *sg = &s->g[i];
        int x1 = margin + (int)(sg->start * scale + 0.5), x2 = margin + (int)(sg->end * scale + 0.5);
        if (x2 <= x1) x2 = x1 + 1;
        RECT r = { x1, barTop, x2, barTop + barH };
        const char *label;
        if (sg->pid < 0) {
            SetBkMode(dc, OPAQUE); SetBkColor(dc, RGB(236,238,240));
            FillRect(dc, &r, hatch); SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(70,78,86)); label = "Idle";
        } else {
            HBRUSH b = CreateSolidBrush(pid_color(sg->pid)); FillRect(dc, &r, b); DeleteObject(b);
            SetTextColor(dc, RGB(255,255,255)); label = name_of(s, sg->pid);
        }
        FrameRect(dc, &r, frame);
        if (text_w(dc, label) + 4 <= x2 - x1) DrawTextA(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        snprintf(buf, sizeof buf, "%d", sg->start);
        int tw = text_w(dc, buf), left = x1 - tw / 2;
        if (left >= lastRight + 4) { draw_note(dc, left, barTop + barH + 4, buf, RGB(60,60,60)); lastRight = left + tw; }
        lastEnd = x2;
    }
    snprintf(buf, sizeof buf, "%d", s->total_time);
    int tw = text_w(dc, buf), left = lastEnd - tw;
    if (left >= lastRight + 4) draw_note(dc, left, barTop + barH + 4, buf, RGB(60,60,60));
    DeleteObject(hatch); DeleteObject(frame);
}

/* ---------------------------------------------------------------- memory map */
static void paint_memmap(HDC dc, const RECT *rc)
{
    FillRect(dc, rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    if (!g.cur || g.cur->mem.nblocks == 0) {
        draw_note(dc, 10, 10, "Memory map: run a simulation to see how the processes are placed in memory.", RGB(110,110,110));
        return;
    }
    const MemResult *m = &g.cur->mem;
    char buf[96];
    snprintf(buf, sizeof buf, "Memory map - %s   (colour = process data, hatched = wasted space inside a block)", m->strategy_name);
    draw_note(dc, 10, 4, buf, RGB(30,40,50));

    int margin = 10, gap = 4, top = 26, h = 44;
    int usable = rc->right - 2 * margin - (m->nblocks - 1) * gap;
    if (usable < m->nblocks * 20) usable = m->nblocks * 20;
    int w[MAX_BLOCKS], sum = 0;
    for (int i = 0; i < m->nblocks; i++) {
        w[i] = (int)((double)usable * m->blocks[i] / m->total_mem);
        if (w[i] < 34) w[i] = 34;
        sum += w[i];
    }
    if (sum > usable) for (int i = 0; i < m->nblocks; i++) w[i] = w[i] * usable / sum;

    HBRUSH hatch = CreateHatchBrush(HS_FDIAGONAL, RGB(196,160,100));
    HBRUSH edge = CreateSolidBrush(RGB(70,80,90));
    int x = margin;
    for (int i = 0; i < m->nblocks; i++) {
        RECT blk = { x, top, x + w[i], top + h };
        const MemAlloc *a = NULL;
        for (int k = 0; k < m->n; k++) if (m->a[k].allocated && m->a[k].block == i + 1) { a = &m->a[k]; break; }
        if (a) {
            SetBkMode(dc, OPAQUE); SetBkColor(dc, RGB(250,240,220));
            FillRect(dc, &blk, hatch); SetBkMode(dc, TRANSPARENT);
            int uw = (int)((double)w[i] * a->size / a->block_size);
            RECT used = { x, top, x + uw, top + h };
            HBRUSH b = CreateSolidBrush(pid_color(a->pid)); FillRect(dc, &used, b); DeleteObject(b);
            SetTextColor(dc, RGB(255,255,255));
            if (text_w(dc, a->name) + 4 <= uw) DrawTextA(dc, a->name, -1, &used, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(dc, RGB(120,120,120));
            DrawTextA(dc, "free", -1, &blk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        FrameRect(dc, &blk, edge);
        snprintf(buf, sizeof buf, "B%d  %d KB", i + 1, m->blocks[i]);
        SetTextColor(dc, RGB(60,60,60));
        RECT lab = { x, top + h + 3, x + w[i], top + h + 20 };
        DrawTextA(dc, buf, -1, &lab, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
        x += w[i] + gap;
    }
    DeleteObject(hatch); DeleteObject(edge);
}

typedef void (*PaintFn)(HDC, const RECT *);

static LRESULT paint_buffered(HWND h, PaintFn fn)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
    HGDIOBJ old = SelectObject(mem, bm);
    fn(mem, &rc);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bm); DeleteDC(mem);
    EndPaint(h, &ps);
    return 0;
}

static LRESULT CALLBACK GanttProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:  InvalidateRect(h, NULL, FALSE); return 0;
    case WM_PAINT: return paint_buffered(h, paint_gantt);
    }
    return DefWindowProcA(h, m, w, l);
}

static LRESULT CALLBACK MemMapProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:  InvalidateRect(h, NULL, FALSE); return 0;
    case WM_PAINT: return paint_buffered(h, paint_memmap);
    }
    return DefWindowProcA(h, m, w, l);
}

/* A tab page: just forwards control messages to the main window. */
static LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageA(g.hwnd, m, w, l);       /* the main window handles every button and list */
    }
    return DefWindowProcA(h, m, w, l);
}

static void reg(HINSTANCE hi, const char *name, WNDPROC proc, HBRUSH bg)
{
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.lpszClassName = name;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = bg;
    RegisterClassA(&wc);
}

void widgets_register(HINSTANCE hi)
{
    reg(hi, "VOSGantt",  GanttProc,  NULL);
    reg(hi, "VOSMemMap", MemMapProc, NULL);
    reg(hi, "VOSPage",   PageProc,   (HBRUSH)(COLOR_BTNFACE + 1));
}
