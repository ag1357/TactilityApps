/* Synthetic P4 app surface for --gc-sections: references the pipeline entry
 * points an integration would actually call, so the reported .text is the
 * realistic binary cost of a stripped Jet under each config. */
#include "Scene.hpp"
#include "Camera.hpp"
#include "Object.hpp"
#include "Material.hpp"
#include <stddef.h>
using namespace Renderer;

/* size-measurement stub: bare ld has no libc; the app build links newlib's
 * optimized memcpy whose .text cost is a few hundred bytes at most */
extern "C" void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    while (n--) *dd++ = *ss++;
    return d;
}

extern "C" void app_surface(uint16_t *fb, uint16_t *zb) {
    Scene *scene = new Scene(fb, zb, 240, 160);
    Camera *cam = new Camera();
    Object *obj = new Object();
    Material *mat = new Material(0x1234);
    obj->cullingMode = CullingMode::NO_CULLING;
    obj->calculateBoundingBox();
    scene->addObject(obj);
    scene->setCamera(cam);
    scene->setBackcolor(0x6393);
    scene->render();
    scene->prepareFrame();
    scene->rasterizeBand(0, 160);
    scene->advanceFrameCounter();
    (void)mat;
}
