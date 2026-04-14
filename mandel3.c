#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include <math.h>

#if defined(__AVX512F__) && defined(__AVX512DQ__)
#define USE_AVX512
#include <immintrin.h>
#elif defined(__AVX2__)
#define USE_AVX2
#include <immintrin.h>
#endif

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define INITIAL_MAX_ITER 100

// Thread data structure
typedef struct {
    int start_row;
    int end_row;
    int width;
    int height;
    double cx;
    double cy;
    double scale;
    int max_iter;
    Uint32 *pixels;
} thread_data_t;

// Convert iteration to RGB color
static inline Uint32 iter_to_color(int iter, int max_iter) {
    if (iter == max_iter) {
        return 0xFF000000;  // Black
    }

    double t = (double)iter / max_iter;

    int r = (int)(9 * (1-t) * t*t*t * 255);
    int g = (int)(15 * (1-t)*(1-t) * t*t * 255);
    int b = (int)(8.5 * (1-t)*(1-t)*(1-t) * t * 255);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Calculate optimal iterations based on zoom level
static int calculate_iterations(double scale, double initial_scale) {
    // Logarithmic scaling: more zoom = more iterations needed
    const int base_iter = 100;
    const double k = 50.0;  // Scaling factor

    if (scale >= initial_scale) {
        // Zoomed out or at initial scale
        return base_iter;
    }

    // Zoomed in: increase iterations logarithmically
    double zoom_factor = initial_scale / scale;
    int calculated_iter = (int)(base_iter + k * log(zoom_factor));

    // Clamp between reasonable values
    if (calculated_iter < 50) calculated_iter = 50;
    if (calculated_iter > 2000) calculated_iter = 2000;

    return calculated_iter;
}

// Quick check for main cardioid and period-2 bulb
static inline int in_main_set(double x0, double y0) {
    // Check main cardioid
    double q = (x0 - 0.25) * (x0 - 0.25) + y0 * y0;
    if (q * (q + (x0 - 0.25)) < 0.25 * y0 * y0) return 1;

    // Check period-2 bulb
    if ((x0 + 1.0) * (x0 + 1.0) + y0 * y0 < 0.0625) return 1;

    return 0;
}

// AVX-512 optimized Mandelbrot (processes 8 pixels at once)
#ifdef USE_AVX512
static inline void compute_8_pixels_avx512(double x0, double dx, double y0,
                                           int max_iter, Uint32 *output) {
    // Setup 8 x0 values
    __m512d x0_vec = _mm512_set_pd(x0 + 7*dx, x0 + 6*dx, x0 + 5*dx, x0 + 4*dx,
                                    x0 + 3*dx, x0 + 2*dx, x0 + dx, x0);
    __m512d y0_vec = _mm512_set1_pd(y0);

    __m512d zx = _mm512_setzero_pd();
    __m512d zy = _mm512_setzero_pd();

    __m512d four = _mm512_set1_pd(4.0);
    __m512d two = _mm512_set1_pd(2.0);

    int iters[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    __mmask8 active = 0xFF; // All 8 pixels active initially

    for (int i = 0; i < max_iter && active; i++) {
        // zx2 = zx * zx, zy2 = zy * zy
        __m512d zx2 = _mm512_mul_pd(zx, zx);
        __m512d zy2 = _mm512_mul_pd(zy, zy);

        // Check if |z|^2 > 4 for each pixel
        __m512d sum = _mm512_add_pd(zx2, zy2);
        __mmask8 escaped = _mm512_cmp_pd_mask(sum, four, _CMP_GT_OQ);

        // Update iteration counts for still-active pixels
        for (int j = 0; j < 8; j++) {
            if ((active & (1 << j)) && !(escaped & (1 << j))) {
                iters[j] = i + 1;
            }
        }

        // Remove escaped pixels from active set
        active &= ~escaped;

        if (!active) break;

        // zy_new = 2 * zx * zy + y0
        __m512d zx_zy = _mm512_mul_pd(zx, zy);
        zy = _mm512_fmadd_pd(two, zx_zy, y0_vec);

        // zx_new = zx^2 - zy^2 + x0
        __m512d diff = _mm512_sub_pd(zx2, zy2);
        zx = _mm512_add_pd(diff, x0_vec);
    }

    // Convert iterations to colors
    for (int j = 0; j < 8; j++) {
        output[j] = iter_to_color(iters[j], max_iter);
    }
}
#endif

// AVX2 optimized Mandelbrot (processes 4 pixels at once)
#ifdef USE_AVX2
static inline void compute_4_pixels_avx2(double x0, double dx, double y0,
                                         int max_iter, Uint32 *output) {
    // Setup 4 x0 values: [x0, x0+dx, x0+2*dx, x0+3*dx]
    __m256d x0_vec = _mm256_set_pd(x0 + 3*dx, x0 + 2*dx, x0 + dx, x0);
    __m256d y0_vec = _mm256_set1_pd(y0);

    __m256d zx = _mm256_setzero_pd();
    __m256d zy = _mm256_setzero_pd();

    __m256d four = _mm256_set1_pd(4.0);
    __m256d two = _mm256_set1_pd(2.0);

    int iters[4] = {0, 0, 0, 0};
    int active = 0xF; // All 4 pixels active initially

    for (int i = 0; i < max_iter && active; i++) {
        // zx2 = zx * zx, zy2 = zy * zy
        __m256d zx2 = _mm256_mul_pd(zx, zx);
        __m256d zy2 = _mm256_mul_pd(zy, zy);

        // Check if |z|^2 > 4 for each pixel
        __m256d sum = _mm256_add_pd(zx2, zy2);
        __m256d cmp = _mm256_cmp_pd(sum, four, _CMP_GT_OQ);
        int mask = _mm256_movemask_pd(cmp);

        // Update iteration counts for still-active pixels
        for (int j = 0; j < 4; j++) {
            if ((active & (1 << j)) && !(mask & (1 << j))) {
                iters[j] = i + 1;
            }
        }

        // Remove escaped pixels from active set
        active &= ~mask;

        if (!active) break;

        // zy_new = 2 * zx * zy + y0
        __m256d zx_zy = _mm256_mul_pd(zx, zy);
        zy = _mm256_fmadd_pd(two, zx_zy, y0_vec);

        // zx_new = zx^2 - zy^2 + x0
        __m256d diff = _mm256_sub_pd(zx2, zy2);
        zx = _mm256_add_pd(diff, x0_vec);
    }

    // Convert iterations to colors
    for (int j = 0; j < 4; j++) {
        output[j] = iter_to_color(iters[j], max_iter);
    }
}
#endif

// Thread function rendering a fragment
static void *render_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;

    double aspect = (double)data->height / data->width;
    double x_min = data->cx - data->scale / 2;
    double x_max = data->cx + data->scale / 2;
    double y_min = data->cy - (data->scale * aspect) / 2;
    double y_max = data->cy + (data->scale * aspect) / 2;

    double dx = (x_max - x_min) / data->width;
    double dy = (y_max - y_min) / data->height;

    for (int y = data->start_row; y < data->end_row; y++) {
        double y0 = y_min + dy * y;

        int x = 0;

#ifdef USE_AVX512
        // Process 8 pixels at a time with AVX-512
        for (; x <= data->width - 8; x += 8) {
            double x0 = x_min + dx * x;
            compute_8_pixels_avx512(x0, dx, y0, data->max_iter,
                                   &data->pixels[y * data->width + x]);
        }
#endif

#ifdef USE_AVX2
        // Process 4 pixels at a time with AVX2 (for remaining pixels)
        for (; x <= data->width - 4; x += 4) {
            double x0 = x_min + dx * x;
            compute_4_pixels_avx2(x0, dx, y0, data->max_iter,
                                 &data->pixels[y * data->width + x]);
        }
#endif

        // Process remaining pixels (scalar)
        for (; x < data->width; x++) {
            double x0 = x_min + dx * x;

            int iter;

            // Quick check for main cardioid and bulb
            if (in_main_set(x0, y0)) {
                iter = data->max_iter;
            } else {
                double zx = 0, zy = 0;
                double zx2 = 0, zy2 = 0;
                iter = 0;

                // Period checking: detect cycles to exit early for points in set
                double check_zx = 0, check_zy = 0;
                int check_iter = 0;
                int period = 0;

                while (zx2 + zy2 <= 4 && iter < data->max_iter) {
                    zy = 2 * zx * zy + y0;
                    zx = zx2 - zy2 + x0;
                    zx2 = zx * zx;
                    zy2 = zy * zy;
                    iter++;

                    // Check if we've cycled back (point is in the set)
                    if (zx == check_zx && zy == check_zy) {
                        iter = data->max_iter;
                        break;
                    }

                    // Update checkpoint at power-of-2 intervals
                    period++;
                    if (period >= check_iter) {
                        check_zx = zx;
                        check_zy = zy;
                        check_iter = period;
                        period = 0;
                    }
                }
            }

            data->pixels[y * data->width + x] = iter_to_color(iter, data->max_iter);
        }
    }

    return NULL;
}

// Render Mandelbrot set with simple multithreading
static void render_mandelbrot(Uint32 *pixels, int width, int height,
                              double cx, double cy, double scale, int max_iter) {
    int num_threads = SDL_GetCPUCount();
    if (num_threads < 1) num_threads = 4;
    if (num_threads > height) num_threads = height;

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_data_t *thread_data = malloc(num_threads * sizeof(thread_data_t));

    int rows_per_thread = height / num_threads;
    int extra_rows = height % num_threads;
    int current_row = 0;

    // Start threads
    for (int t = 0; t < num_threads; t++) {
        int thread_rows = rows_per_thread + (t < extra_rows ? 1 : 0);

        thread_data[t].start_row = current_row;
        thread_data[t].end_row = current_row + thread_rows;
        thread_data[t].width = width;
        thread_data[t].height = height;
        thread_data[t].cx = cx;
        thread_data[t].cy = cy;
        thread_data[t].scale = scale;
        thread_data[t].max_iter = max_iter;
        thread_data[t].pixels = pixels;

        pthread_create(&threads[t], NULL, render_thread, &thread_data[t]);

        current_row += thread_rows;
    }

    // Wait for completion
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    free(threads);
    free(thread_data);
}

// Draw UI overlay
static void draw_ui(SDL_Window *window, double cx, double cy, double scale, int max_iter) {
    char title[256];
#ifdef USE_AVX512
    snprintf(title, sizeof(title),
             "Mandelbrot Explorer [AVX-512] - center=(%.6f, %.6f) scale=%.6e iter=%d",
             cx, cy, scale, max_iter);
#elif defined(USE_AVX2)
    snprintf(title, sizeof(title),
             "Mandelbrot Explorer [AVX2] - center=(%.6f, %.6f) scale=%.6e iter=%d",
             cx, cy, scale, max_iter);
#else
    snprintf(title, sizeof(title),
             "Mandelbrot Explorer - center=(%.6f, %.6f) scale=%.6e iter=%d",
             cx, cy, scale, max_iter);
#endif
    SDL_SetWindowTitle(window, title);
}

int main(void) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window *window = SDL_CreateWindow(
        "Mandelbrot Explorer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create texture for pixel buffer
    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        WINDOW_WIDTH,
        WINDOW_HEIGHT
    );

    // Allocate pixel buffer
    Uint32 *pixels = malloc(WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(Uint32));

    // Initial parameters
    double cx = -0.5;
    double cy = 0.0;
    double scale = 3.0;
    double initial_scale = 3.0;  // Remember initial scale for iteration calculation
    int max_iter = INITIAL_MAX_ITER;

    int width = WINDOW_WIDTH;
    int height = WINDOW_HEIGHT;

    // Smooth zoom animation variables
    double target_scale = scale;
    double target_cx = cx;
    double target_cy = cy;
    int target_max_iter = max_iter;
    int animating = 0;
    const double zoom_speed = 0.25;

    // Frame skipping
    int frame_counter = 0;
    const int render_every_n_frames = 2;

    // Drag variables
    int dragging = 0;
    int drag_start_x = 0;
    int drag_start_y = 0;
    double drag_start_cx = 0;
    double drag_start_cy = 0;

    int needs_redraw = 1;
    int running = 1;

    printf("Controls:\n");
    printf("  Mouse wheel - smooth zoom at cursor\n");
    printf("  Right click drag - pan/move view\n");
    printf("  Arrow keys - move\n");
    printf("  +/- - zoom in/out\n");
    printf("  [ / ] - manually adjust detail (overrides auto-calculation)\n");
    printf("  R - reset view\n");
    printf("  ESC/Q - quit\n");
#ifdef USE_AVX512
    printf("\nSIMD acceleration: AVX-512 ENABLED (8 pixels at once)\n");
#elif defined(USE_AVX2)
    printf("\nSIMD acceleration: AVX2 ENABLED (4 pixels at once)\n");
#else
    printf("\nSIMD acceleration: DISABLED\n");
#endif
    printf("Optimizations: Cardioid/Bulb checking + Period detection\n");
    printf("Iterations: Auto-calculated based on zoom level (log scaling)\n");

    // Main loop
    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;

                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                        case SDLK_q:
                            running = 0;
                            break;

                        case SDLK_EQUALS:
                        case SDLK_PLUS:
                            target_scale = scale * 0.7;
                            target_cx = cx;
                            target_cy = cy;
                            max_iter = calculate_iterations(target_scale, initial_scale);
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_MINUS:
                            target_scale = scale / 0.7;
                            target_cx = cx;
                            target_cy = cy;
                            max_iter = calculate_iterations(target_scale, initial_scale);
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_UP:
                            target_cx = cx;
                            target_cy = cy - scale * 0.1;
                            target_scale = scale;
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_DOWN:
                            target_cx = cx;
                            target_cy = cy + scale * 0.1;
                            target_scale = scale;
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_LEFT:
                            target_cx = cx - scale * 0.1;
                            target_cy = cy;
                            target_scale = scale;
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_RIGHT:
                            target_cx = cx + scale * 0.1;
                            target_cy = cy;
                            target_scale = scale;
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_r:
                            target_cx = -0.5;
                            target_cy = 0.0;
                            target_scale = initial_scale;
                            max_iter = INITIAL_MAX_ITER;
                            target_max_iter = max_iter;
                            animating = 1;
                            break;

                        case SDLK_LEFTBRACKET:
                            // Decrease iterations
                            if (max_iter > 20) {
                                max_iter -= 20;
                                target_max_iter = max_iter;
                                needs_redraw = 1;
                            }
                            break;

                        case SDLK_RIGHTBRACKET:
                            // Increase iterations
                            if (max_iter < 2000) {
                                max_iter += 20;
                                target_max_iter = max_iter;
                                needs_redraw = 1;
                            }
                            break;
                    }
                    break;

                case SDL_MOUSEWHEEL: {
                    int mouse_x, mouse_y;
                    SDL_GetMouseState(&mouse_x, &mouse_y);

                    double aspect = (double)height / width;
                    double x_min = cx - scale / 2;
                    double y_min = cy - (scale * aspect) / 2;

                    double mouse_cx = x_min + scale * mouse_x / width;
                    double mouse_cy = y_min + (scale * aspect) * mouse_y / height;

                    double zoom_factor = (event.wheel.y > 0) ? 0.85 : 1.15;

                    target_scale = scale * zoom_factor;

                    double offset_x = mouse_cx - cx;
                    double offset_y = mouse_cy - cy;

                    target_cx = mouse_cx - offset_x * zoom_factor;
                    target_cy = mouse_cy - offset_y * zoom_factor;

                    // Calculate iterations based on new scale
                    max_iter = calculate_iterations(target_scale, initial_scale);
                    target_max_iter = max_iter;

                    animating = 1;
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        dragging = 1;
                        drag_start_x = event.button.x;
                        drag_start_y = event.button.y;
                        drag_start_cx = cx;
                        drag_start_cy = cy;
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        dragging = 0;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (dragging) {
                        int dx = event.motion.x - drag_start_x;
                        int dy = event.motion.y - drag_start_y;

                        double aspect = (double)height / width;
                        double offset_x = -dx * scale / width;
                        double offset_y = -dy * (scale * aspect) / height;

                        cx = drag_start_cx + offset_x;
                        cy = drag_start_cy + offset_y;
                        target_cx = cx;
                        target_cy = cy;

                        needs_redraw = 1;
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        width = event.window.data1;
                        height = event.window.data2;

                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(
                            renderer,
                            SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING,
                            width,
                            height
                        );

                        free(pixels);
                        pixels = malloc(width * height * sizeof(Uint32));

                        needs_redraw = 1;
                    }
                    break;
            }
        }

        // Handle smooth animation
        if (animating) {
            frame_counter++;

            double scale_diff = target_scale - scale;
            double cx_diff = target_cx - cx;
            double cy_diff = target_cy - cy;

            scale += scale_diff * zoom_speed;
            cx += cx_diff * zoom_speed;
            cy += cy_diff * zoom_speed;

            if (fabs(scale_diff) < target_scale * 0.001 &&
                fabs(cx_diff) < target_scale * 0.001 &&
                fabs(cy_diff) < target_scale * 0.001) {
                scale = target_scale;
                cx = target_cx;
                cy = target_cy;
                max_iter = target_max_iter;
                animating = 0;
                frame_counter = 0;
                needs_redraw = 1;
            } else {
                if (frame_counter >= render_every_n_frames) {
                    needs_redraw = 1;
                    frame_counter = 0;
                }
            }
        }

        // Render if needed
        if (needs_redraw) {
            render_mandelbrot(pixels, width, height, cx, cy, scale, max_iter);

            SDL_UpdateTexture(texture, NULL, pixels, width * sizeof(Uint32));

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            draw_ui(window, cx, cy, scale, max_iter);

            needs_redraw = 0;
        }

        if (!animating) {
            SDL_Delay(10);
        } else {
            SDL_Delay(1);
        }
    }

    // Cleanup
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
