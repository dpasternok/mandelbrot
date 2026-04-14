#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void color_escape(int iter, int max_iter) {
    if (iter == max_iter) {
        printf(" ");
        return;
    }

    double t = (double)iter / max_iter;

    double r = 9*(1-t)*t*t*t * 255;
    double g = 15*(1-t)*(1-t)*t*t * 255;
    double b = 8.5*(1-t)*(1-t)*(1-t)*t * 255;

    printf("\x1b[38;2;%d;%d;%dm█", (int)r, (int)g, (int)b);
}

int main(int argc, char **argv) {
    double cx = atof(argv[1]);
    double cy = atof(argv[2]);
    double scale = atof(argv[3]);
    int rows = atoi(argv[4]);
    int cols = atoi(argv[5]);
    int max_iter = atoi(argv[6]);
    double vert_scale = atof(argv[7]);   // NEW PARAMETER

    double x_min = cx - scale/2;
    double height = scale * rows / cols * vert_scale;   // CHANGED
    double y_min = cy - height/2;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            double x0 = x_min + scale * j / cols;
            double y0 = y_min + height * i / rows;

            double x = 0, y = 0;
            int iter = 0;

            while (x*x + y*y <= 4 && iter < max_iter) {
                double xt = x*x - y*y + x0;
                y = 2*x*y + y0;
                x = xt;
                iter++;
            }

            color_escape(iter, max_iter);
        }
        printf("\x1b[0m\n");
    }
}