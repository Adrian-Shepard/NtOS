#include "../gui/apps.h"     // Определяет window_t, app_meta_t и прототипы
#include "../tty/fb.h"       // Для fb_putc, fb_fill_rect и т.д.
#include "../sched/sched.h"  // Для PROC_MAX, process_t, process_get
#include "../mem/pmm.h"      // Для pmm_total_pages, pmm_free_pages

#define CPU_HISTORY_SIZE 60
static int g_cpu_history[CPU_HISTORY_SIZE] = {0};

static void put_str(const char *s, uint32_t fg, uint32_t bg) { 
    while (*s) fb_putc(*s++, fg, bg); 
}

void app_taskman_draw(window_t *win) {
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, 0x00141A20);
    
    int x = win->x + 16;
    int y = win->y + 40;
    int w = win->w - 32;

    fb_set_cursor(x, y);
    put_str("SYSTEM MONITOR", 0x0044DDFF, 0x00141A20);
    fb_fill_rect(x, y + 12, w, 1, 0x00808080);
    y += 24;

    // --- ГРАФИК ПАМЯТИ ---
    uint64_t total_pages = pmm_total_pages();
    uint64_t free_pages = pmm_free_pages();
    uint64_t used_pages = total_pages - free_pages;
    int mem_percent = (int)((used_pages * 100) / (total_pages ? total_pages : 1));
    
    fb_set_cursor(x, y);
    put_str("MEMORY USAGE", 0x00D0D0D0, 0x00141A20);
    
    fb_draw_box(x, y + 12, w, 12, 0x00808080);
    fb_fill_rect(x + 1, y + 13, w - 2, 10, 0x00303030);
    
    int fill_w = ((w - 2) * mem_percent) / 100;
    uint32_t mem_color = 0x0044FF88;
    if (mem_percent > 75) mem_color = 0x00FFAA00;
    if (mem_percent > 90) mem_color = 0x00FF3333;
    
    if (fill_w > 0) fb_fill_rect(x + 1, y + 13, fill_w, 10, mem_color);
    
    // Текст памяти
    char buf[64]; int p = 0;
    const char *prefix = " ";
    while(prefix[p]) { buf[p] = prefix[p]; p++; }
    if (mem_percent < 10) buf[p++] = ' ';
    if (mem_percent < 100) buf[p++] = ' ';
    buf[p++] = '0' + (mem_percent / 10) % 10;
    buf[p++] = '0' + (mem_percent % 10);
    buf[p++] = '%'; buf[p++] = ' '; buf[p++] = '(';
    
    uint64_t used_mb = (used_pages * 4) / 1024;
    char rev[16]; int r = 0;
    if (used_mb == 0) rev[r++] = '0';
    while (used_mb) { rev[r++] = '0' + (used_mb % 10); used_mb /= 10; }
    while (r--) buf[p++] = rev[r];
    const char *suffix = " MB / ";
    for(int i=0; suffix[i]; i++) buf[p++] = suffix[i];
    
    uint64_t total_mb = (total_pages * 4) / 1024;
    r = 0;
    if (total_mb == 0) rev[r++] = '0';
    while (total_mb) { rev[r++] = '0' + (total_mb % 10); total_mb /= 10; }
    while (r--) buf[p++] = rev[r];
    const char *suffix2 = " MB)";
    for(int i=0; suffix2[i]; i++) buf[p++] = suffix2[i];
    buf[p] = 0;
    
    fb_set_cursor(x, y + 26);
    put_str(buf, 0x00C0C0C0, 0x00141A20);
    y += 46;

    // --- ГРАФИК CPU ---
    fb_set_cursor(x, y);
    put_str("CPU LOAD", 0x00D0D0D0, 0x00141A20);
    
    // Считаем нагрузку (до 600%)
    int load = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        process_t *proc = process_get(i);
        if (!proc || proc->state == PROC_FREE || proc->state == PROC_ZOMBIE) continue;
        
        if (proc->state == PROC_RUNNING) load += 100;
        else if (proc->state == PROC_READY) load += 50;
        else if (proc->state == PROC_BLOCKED) load += 10;
    }
    if (load > 600) load = 600;

    // Сдвигаем историю
    for (int i = 0; i < CPU_HISTORY_SIZE - 1; i++) g_cpu_history[i] = g_cpu_history[i + 1];
    g_cpu_history[CPU_HISTORY_SIZE - 1] = load; 

    int graph_h = 50;
    int graph_y = y + 12;
    fb_fill_rect(x, graph_y, w, graph_h, 0x00000000); // Черный фон
    fb_draw_box(x, graph_y, w, graph_h, 0x00808080); // Рамка

    // Рисуем сетку (горизонтальные линии)
    fb_fill_rect(x + 1, graph_y + 1 + (graph_h / 2), w - 2, 1, 0x00202020);
    
    // Красивая отрисовка Area Chart
    for (int px = 0; px < w - 2; px++) {
        int idx = (px * CPU_HISTORY_SIZE) / (w - 2);
        int val = g_cpu_history[idx];
        
        // Высота текущего столбца (макс 600)
        int h = (val * (graph_h - 2)) / 600;
        if (h <= 0) continue;

        // Цвет градиентом
        uint32_t c = 0x0044FF88; // Зеленый
        if (val > 150) c = 0x00FFAA00; // Оранжевый
        if (val > 350) c = 0x00FF3333; // Красный

        // Рисуем вертикальную линию
        fb_fill_rect(x + 1 + px, graph_y + graph_h - 1 - h, 1, h, c);
    }
    
    y += graph_h + 24;

    // --- СПИСОК ПРОЦЕССОВ ---
    fb_fill_rect(x, y, w, 1, 0x00808080);
    y += 8;
    fb_set_cursor(x, y);
    put_str("PID  STATE     NAME", 0x00D0D0D0, 0x00141A20);
    y += 12;

    for (int i = 0; i < PROC_MAX && y < win->y + win->h - 20; i++) {
        process_t *p = process_get(i);
        if (!p || p->state == PROC_FREE) continue;
        
        fb_set_cursor(x, y);
        fb_putc('0' + (p->pid / 10) % 10, 0x00808080, 0x00141A20);
        fb_putc('0' + (p->pid % 10), 0x00808080, 0x00141A20);
        
        fb_set_cursor(x + 40, y);
        uint32_t state_color = 0x00D0D0D0;
        const char *state_str = "READY";
        if (p->state == PROC_RUNNING) { state_color = 0x0044FF88; state_str = "RUNNING"; }
        else if (p->state == PROC_BLOCKED) { state_color = 0x00FFAA00; state_str = "BLOCKED"; }
        else if (p->state == PROC_ZOMBIE) { state_color = 0x00FF3333; state_str = "ZOMBIE"; }
        put_str(state_str, state_color, 0x00141A20);
        
        fb_set_cursor(x + 100, y);
        put_str(p->name, 0x00D0D0D0, 0x00141A20);
        
        y += 12;
    }
}