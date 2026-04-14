#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define INITIAL_MAX_ITER 100

// Compute shader source code
const char *compute_shader_source = R"(
#version 430 core

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba8, binding = 0) uniform image2D img_output;

uniform vec2 center;
uniform float scale;
uniform int max_iter;
uniform int width;
uniform int height;

vec3 iter_to_color(int iter, int max_iter) {
    if (iter == max_iter) {
        return vec3(0.0, 0.0, 0.0);
    }

    float t = float(iter) / float(max_iter);

    float r = 9.0 * (1.0-t) * t*t*t;
    float g = 15.0 * (1.0-t)*(1.0-t) * t*t;
    float b = 8.5 * (1.0-t)*(1.0-t)*(1.0-t) * t;

    return vec3(r, g, b);
}

// Quick check for main cardioid and bulb
bool in_main_set(float x0, float y0) {
    // Check main cardioid
    float q = (x0 - 0.25) * (x0 - 0.25) + y0 * y0;
    if (q * (q + (x0 - 0.25)) < 0.25 * y0 * y0) return true;

    // Check period-2 bulb
    if ((x0 + 1.0) * (x0 + 1.0) + y0 * y0 < 0.0625) return true;

    return false;
}

void main() {
    ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);

    if (pixel_coords.x >= width || pixel_coords.y >= height) {
        return;
    }

    float aspect = float(height) / float(width);
    float x_min = center.x - scale / 2.0;
    float x_max = center.x + scale / 2.0;
    float y_min = center.y - (scale * aspect) / 2.0;
    float y_max = center.y + (scale * aspect) / 2.0;

    float x0 = x_min + (x_max - x_min) * float(pixel_coords.x) / float(width);
    float y0 = y_min + (y_max - y_min) * float(pixel_coords.y) / float(height);

    int iter;

    if (in_main_set(x0, y0)) {
        iter = max_iter;
    } else {
        float zx = 0.0;
        float zy = 0.0;
        float zx2 = 0.0;
        float zy2 = 0.0;
        iter = 0;

        // Period checking
        float check_zx = 0.0;
        float check_zy = 0.0;
        int check_iter = 0;
        int period = 0;

        while (zx2 + zy2 <= 4.0 && iter < max_iter) {
            zy = 2.0 * zx * zy + y0;
            zx = zx2 - zy2 + x0;
            zx2 = zx * zx;
            zy2 = zy * zy;
            iter++;

            // Check for cycle
            if (zx == check_zx && zy == check_zy) {
                iter = max_iter;
                break;
            }

            period++;
            if (period >= check_iter) {
                check_zx = zx;
                check_zy = zy;
                check_iter = period;
                period = 0;
            }
        }
    }

    vec3 color = iter_to_color(iter, max_iter);
    imageStore(img_output, pixel_coords, vec4(color, 1.0));
}
)";

// Vertex shader for displaying texture
const char *vertex_shader_source = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// Fragment shader for displaying texture
const char *fragment_shader_source = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D tex;

void main() {
    FragColor = texture(tex, TexCoord);
}
)";

// Create display shader
GLuint create_display_shader(void) {
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    GLint success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, info_log);
        fprintf(stderr, "Vertex shader compilation failed:\n%s\n", info_log);
        return 0;
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(fragment_shader, 512, NULL, info_log);
        fprintf(stderr, "Fragment shader compilation failed:\n%s\n", info_log);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program, 512, NULL, info_log);
        fprintf(stderr, "Display shader linking failed:\n%s\n", info_log);
        return 0;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

// Create and compile compute shader
GLuint create_compute_shader(void) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &compute_shader_source, NULL);
    glCompileShader(shader);

    // Check compilation status
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        fprintf(stderr, "Compute shader compilation failed:\n%s\n", info_log);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    // Check linking status
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program, 512, NULL, info_log);
        fprintf(stderr, "Shader program linking failed:\n%s\n", info_log);
        return 0;
    }

    glDeleteShader(shader);
    return program;
}

// Calculate optimal iterations based on zoom level
static int calculate_iterations(double scale, double initial_scale) {
    // Logarithmic scaling: more zoom = more iterations needed
    // Formula: base_iter + k * log(initial_scale / scale)
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

// Draw UI overlay
static void draw_ui(SDL_Window *window, double cx, double cy, double scale, int max_iter) {
    char title[256];
    snprintf(title, sizeof(title),
             "Mandelbrot Explorer [GPU OpenGL] - center=(%.6f, %.6f) scale=%.6e iter=%d",
             cx, cy, scale, max_iter);
    SDL_SetWindowTitle(window, title);
}

int main(void) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Set OpenGL version (4.3+ for compute shaders)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Create window
    SDL_Window *window = SDL_CreateWindow(
        "Mandelbrot Explorer - GPU",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create OpenGL context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "OpenGL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "GLEW initialization failed: %s\n", glewGetErrorString(err));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Check for compute shader support
    if (!GLEW_ARB_compute_shader) {
        fprintf(stderr, "Compute shaders not supported!\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    printf("GPU: %s\n", glGetString(GL_RENDERER));
    printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Check OpenGL errors
    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        printf("OpenGL error after context creation: 0x%x\n", gl_err);
    }

    // Create compute shader program
    printf("Creating compute shader...\n");
    GLuint compute_program = create_compute_shader();
    if (!compute_program) {
        fprintf(stderr, "Failed to create compute shader program!\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("Compute shader created successfully (ID: %u)\n", compute_program);

    // Create display shader
    printf("Creating display shader...\n");
    GLuint display_program = create_display_shader();
    if (!display_program) {
        fprintf(stderr, "Failed to create display shader program!\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("Display shader created successfully (ID: %u)\n", display_program);

    // Create fullscreen quad
    float quad_vertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Set initial viewport
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // Create texture for output
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    printf("Setup complete, entering main loop...\n");

    // Initial parameters
    double cx = -0.5;
    double cy = 0.0;
    double scale = 3.0;
    double initial_scale = 3.0;  // Remember initial scale for iteration calculation
    int max_iter = INITIAL_MAX_ITER;

    int width = WINDOW_WIDTH;
    int height = WINDOW_HEIGHT;

    // Animation variables
    double target_scale = scale;
    double target_cx = cx;
    double target_cy = cy;
    int target_max_iter = max_iter;
    int animating = 0;
    const double zoom_speed = 0.25;

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

    printf("\nControls:\n");
    printf("  Mouse wheel - smooth zoom at cursor\n");
    printf("  Right click drag - pan/move view\n");
    printf("  Arrow keys - move\n");
    printf("  +/- - zoom in/out\n");
    printf("  [ / ] - manually adjust detail (overrides auto-calculation)\n");
    printf("  R - reset view\n");
    printf("  ESC/Q - quit\n");
    printf("\nGPU Compute Shader: ENABLED\n");
    printf("Optimizations: Cardioid/Bulb checking + Period detection (GPU)\n");
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
                            if (max_iter > 20) {
                                max_iter -= 20;
                                target_max_iter = max_iter;
                                needs_redraw = 1;
                            }
                            break;

                        case SDLK_RIGHTBRACKET:
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

                    // Use current (possibly animating) values for calculation
                    double aspect = (double)height / width;
                    double x_min = cx - scale / 2;
                    double y_max = cy + (scale * aspect) / 2;

                    double mouse_cx = x_min + scale * mouse_x / width;
                    // Invert Y: mouse_y=0 (top) should map to y_max (top in complex plane)
                    double mouse_cy = y_max - (scale * aspect) * mouse_y / height;

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
                        // Convert pixel movement to complex plane movement
                        // X: dragging right moves image right (center moves left = -dx)
                        // Y: dragging down (dy>0, screen) maps to higher pixel_coords.y which maps to higher y in complex plane
                        //    so we want cy to increase = +dy
                        double offset_x = -dx * scale / width;
                        double offset_y = dy * (scale * aspect) / height;

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

                        printf("Window resized to %dx%d\n", width, height);

                        // Update viewport
                        glViewport(0, 0, width, height);

                        // Recreate texture with new size
                        glDeleteTextures(1, &texture);
                        glGenTextures(1, &texture);
                        glBindTexture(GL_TEXTURE_2D, texture);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

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
            // Dispatch compute shader
            glUseProgram(compute_program);
            glUniform2f(glGetUniformLocation(compute_program, "center"), (float)cx, (float)cy);
            glUniform1f(glGetUniformLocation(compute_program, "scale"), (float)scale);
            glUniform1i(glGetUniformLocation(compute_program, "max_iter"), max_iter);
            glUniform1i(glGetUniformLocation(compute_program, "width"), width);
            glUniform1i(glGetUniformLocation(compute_program, "height"), height);

            glDispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            // Render texture to screen
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(display_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform1i(glGetUniformLocation(display_program, "tex"), 0);

            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            SDL_GL_SwapWindow(window);

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
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteTextures(1, &texture);
    glDeleteProgram(compute_program);
    glDeleteProgram(display_program);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Cleanup complete, exiting.\n");

    return 0;
}
