#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <pthread.h>

// Global terminal settings
static struct termios orig_termios;

// Thread data structure
typedef struct {
    int start_row;
    int end_row;
    int rows;
    int cols;
    double cx;
    double cy;
    double scale;
    double vert_scale;
    int max_iter;
    char *buffer;  // Buffer for colors
} thread_data_t;

// Function returning color string for given iteration
static int color_to_string(char *buf, int iter, int max_iter) {
    if (iter == max_iter) {
        buf[0] = ' ';
        return 1;
    }

    double t = (double)iter / max_iter;
    int r = (int)(9*(1-t)*t*t*t * 255);
    int g = (int)(15*(1-t)*(1-t)*t*t * 255);
    int b = (int)(8.5*(1-t)*(1-t)*(1-t)*t * 255);

    return sprintf(buf, "\x1b[38;2;%d;%d;%dm█", r, g, b);
}

// Quick check if point is in main Mandelbrot areas
static inline int quick_check(double x0, double y0) {
    // Check main cardioid
    double q = (x0 - 0.25)*(x0 - 0.25) + y0*y0;
    if (q * (q + (x0 - 0.25)) < 0.25 * y0*y0) return 1;

    // Check period-2 bulb
    if ((x0 + 1)*(x0 + 1) + y0*y0 < 0.0625) return 1;

    return 0;
}

// Thread function rendering a fragment
static void *render_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;

    double x_min = data->cx - data->scale/2;
    double height = data->scale * data->rows / data->cols * data->vert_scale;
    double y_min = data->cy - height/2;

    int buf_pos = 0;
    char color_buf[32];

    for (int i = data->start_row; i < data->end_row; i++) {
        for (int j = 0; j < data->cols; j++) {
            double x0 = x_min + data->scale * j / data->cols;
            double y0 = y_min + height * i / data->rows;

            int iter;

            // Quick check for main areas
            if (quick_check(x0, y0)) {
                iter = data->max_iter;
            } else {
                double x = 0, y = 0;
                double x2 = 0, y2 = 0;
                iter = 0;

                // Optimization: avoid double multiplication
                while (x2 + y2 <= 4 && iter < data->max_iter) {
                    y = 2*x*y + y0;
                    x = x2 - y2 + x0;
                    x2 = x*x;
                    y2 = y*y;
                    iter++;
                }
            }

            int len = color_to_string(color_buf, iter, data->max_iter);
            memcpy(data->buffer + buf_pos, color_buf, len);
            buf_pos += len;
        }

        // Reset color and newline
        memcpy(data->buffer + buf_pos, "\x1b[0m\n", 5);
        buf_pos += 5;
    }

    return NULL;
}

// Enable raw mode for terminal
static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Disable raw mode
static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Get terminal size
static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        *rows = 24;
        *cols = 80;
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

// Render Mandelbrot set with multithreading
static void render_mandelbrot(double cx, double cy, double scale,
                              int rows, int cols, int max_iter,
                              double vert_scale) {
    // Number of threads = number of CPU cores (typically 4-8)
    int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_threads < 1) num_threads = 4;
    if (num_threads > rows) num_threads = rows;

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_data_t *thread_data = malloc(num_threads * sizeof(thread_data_t));

    // Estimate buffer size (maximum ~25 bytes per pixel)
    int buffer_size = (rows / num_threads + 1) * cols * 30;

    int rows_per_thread = rows / num_threads;
    int extra_rows = rows % num_threads;

    int current_row = 0;

    // Start threads
    for (int t = 0; t < num_threads; t++) {
        int thread_rows = rows_per_thread + (t < extra_rows ? 1 : 0);

        thread_data[t].start_row = current_row;
        thread_data[t].end_row = current_row + thread_rows;
        thread_data[t].rows = rows;
        thread_data[t].cols = cols;
        thread_data[t].cx = cx;
        thread_data[t].cy = cy;
        thread_data[t].scale = scale;
        thread_data[t].vert_scale = vert_scale;
        thread_data[t].max_iter = max_iter;
        thread_data[t].buffer = malloc(buffer_size);

        pthread_create(&threads[t], NULL, render_thread, &thread_data[t]);

        current_row += thread_rows;
    }

    // Wait for completion and print results in order
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
        printf("%s", thread_data[t].buffer);
        free(thread_data[t].buffer);
    }

    free(threads);
    free(thread_data);
}

// Clear terminal
static void clear_screen(void) {
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
}

// Hide cursor
static void hide_cursor(void) {
    printf("\x1b[?25l");
    fflush(stdout);
}

// Show cursor
static void show_cursor(void) {
    printf("\x1b[?25h");
    fflush(stdout);
}

// Draw full screen with interface
static void draw(double cx, double cy, double scale, int max_iter,
                 double vert_scale) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    // Leave 2 lines for info at bottom
    rows -= 2;

    clear_screen();
    render_mandelbrot(cx, cy, scale, rows, cols, max_iter, vert_scale);

    printf("\ncenter=(%.6f, %.6f) scale=%.6f iter=%d\n", cx, cy, scale, max_iter);
    printf("[arrows move, +/- zoom, q exit]\n");
    fflush(stdout);
}

// Main function
int main(void) {
    // Initial parameters
    double cx = -0.5;
    double cy = 0.0;
    double scale = 3.0;
    int max_iter = 100;
    double vert_scale = 2.0;

    // Terminal configuration
    enable_raw_mode();
    hide_cursor();

    // Draw initial image
    draw(cx, cy, scale, max_iter, vert_scale);

    // Main event loop
    char buf[3];
    while (1) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

        if (n <= 0) continue;

        // Single character
        if (n == 1) {
            switch (buf[0]) {
                case 'q':
                case 'Q':
                case 27:  // ESC (if pressed alone)
                    goto cleanup;

                case '=':
                case '+':
                    scale *= 0.7;
                    max_iter += 5;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;

                case '-':
                case '_':
                    scale /= 0.7;
                    if (max_iter > 20) max_iter -= 5;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;
            }
        }
        // Escape sequences (arrow keys)
        else if (n >= 3 && buf[0] == 27 && buf[1] == '[') {
            switch (buf[2]) {
                case 'A':  // Arrow up
                    cy -= scale * 0.1;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;

                case 'B':  // Arrow down
                    cy += scale * 0.1;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;

                case 'C':  // Arrow right
                    cx += scale * 0.1;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;

                case 'D':  // Arrow left
                    cx -= scale * 0.1;
                    draw(cx, cy, scale, max_iter, vert_scale);
                    break;
            }
        }
    }

cleanup:
    // Restore terminal
    show_cursor();
    clear_screen();
    disable_raw_mode();

    return 0;
}
