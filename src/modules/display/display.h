#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WALL_WIDTH 1280
#define DISPLAY_WALL_HEIGHT 800

typedef struct {
    const char *winTitle;
    int x;
    int y;
    int width;
    int height;
    bool fullscreen;
}Display_t;

extern char **dispBufferMap(Display_t *dispDesc);
extern int display(Display_t *dispDesc);
extern bool displayIsRunning();
extern bool dispBufferCheckGuard();

#endif

