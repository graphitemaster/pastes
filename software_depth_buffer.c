#include <float.h>
#include <stdio.h>

typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    float a[4], b[4], c[4], d[4];
} mat4_t;

mat4_t mat4_look_at_lh(const vec3_t *const, const vec3_t *const, const mat4_t *const);
mat4_t mat4_perspective_fov_rh(float, float, float, float);
mat4_t mat4_rotation_yaw_pitch_roll(const vec3_t *const, const vec3_t *const);
mat4_t mat4_translation(const vec3_t *const);
mat4_t mat4_scale(const vec3_t *const);
mat4_t mat4_mul(const mat4_t *const, const mat4_t *const);

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

// adjust for depth buffer size
#define WIDTH 320
#define HEIGHT 240

static float depth[WIDTH*HEIGHT];

static vec3_t transform_coordinate(const vec3_t *const c, const mat4_t *const t) {
    const float x = (((c->x * t->a[0]) + (c->y * t->b[0])) + (c->z * t->c[0])) + t->d[0];
    const float y = (((c->x * t->a[1]) + (c->y * t->b[1])) + (c->z * t->c[1])) + t->d[1];
    const float z = (((c->x * t->a[2]) + (c->y * t->b[2])) + (c->z * t->c[2])) + t->d[2];
    const float w = 1.0f / ((((c->x * t->a[3]) + (c->y * t->b[3])) + (c->z * t->c[3])) + t->d[3]);
    return (vec3_t) { x * w, y * w, z * w };
}

static vec3_t project(const vec3_t *const coordinate, const mat4_t *const transform) {
    const vec3_t point = transform_coordinate(coordinate, transform);
    const float x = point.x * WIDTH + WIDTH / 2.0f;
    const float y = -point.y * HEIGHT + HEIGHT / 2.0f;
    return (vec3_t){ x, y, point.z };
}

static float clamp(float value, float min, float max) {
    return MAX(min, MIN(value, max));
}

static float interpolate(float min, float max, float grad) {
    return min + (max - min) * clamp(grad, 0.0f, 1.0f);
}

static void put(int x, int y, float z) {
    const int index = x + y * WIDTH;
    if (depth[index] < z)
        return;
    depth[index] = z;
}

static void draw(const vec3_t *const p) {
    const int x = p->x;
    const int y = p->y;
    // clip
    if (x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT)
        put(x, y, p->z);
}

static void process(int y,
                    const vec3_t *const a,
                    const vec3_t *const b,
                    const vec3_t *const c,
                    const vec3_t *const d)
{
    const float g1 = a->y != b->y ? (y - a->y) / (b->y - a->y) : 1.0f;
    const float g2 = c->y != d->y ? (y - c->y) / (d->y - c->y) : 1.0f;
    const int sx = (int)interpolate(a->x, b->x, g1);
    const int ex = (int)interpolate(c->x, d->x, g2);
    const float z1 = interpolate(a->z, b->z, g1);
    const float z2 = interpolate(c->z, d->z, g2);
    for (int x = sx; x < ex; x++) {
        float grad = (x - sx) / (float)(ex - sx);
        float z = interpolate(z1, z2, grad);
        draw(&((vec3_t){x, y, z}));
    }
}

static void swap(vec3_t *a, vec3_t *b) {
    vec3_t t = *a;
    *a = *b;
    *b = t;
}

static void triangle(vec3_t *const p1,
                     vec3_t *const p2,
                     vec3_t *const p3)
{
    if (p1->y > p2->y) swap(p2, p1);
    if (p2->y > p3->y) swap(p2, p3);
    if (p1->y > p2->y) swap(p2, p1);
    // inverse slopes
    float p1p2 = 0.0f;
    float p1p3 = 0.0f;
    if (p2->y - p1->y > 0.0f) p1p2 = (p2->x - p1->x) / (p2->y - p1->y);
    if (p3->y - p1->y > 0.0f) p1p3 = (p3->x - p1->x) / (p3->y - p1->y);
    if (p1p2 > p1p3) {
        // facing right with clockwise: P1, P2, P3
        for (int y = (int)p1->y; y <= (int)p3->y; y++) {
            if (y < (int)p2->y)
                process(y, p1, p3, p1, p2);
            else
                process(y, p1, p3, p2, p3);
        }
    } else {
        // facing left with counter clockwise: P1, P2, P3
        for (int y = (int)p1->y; y <= (int)p3->y; y++) {
            if (y < (int)p2->y)
                process(y, p1, p2, p1, p3);
            else
                process(y, p2, p3, p1, p3);
        }
    }
}

// api
typedef struct {
    vec3_t position;
    vec3_t rotation;
    vec3_t scale;
} occulder_t;

void depth_clear(void) {
    for (int i = 0; i < WIDTH*HEIGHT; i++)
        depth[i] = FLT_MAX;
}

// renders vertices to depth
void depth_render_vertices(const mat4_t *const transform,
                           const vec3_t *const vertices,
                           int triangles)
{
    for (int i = 0; i < triangles; i += 3) {
        const vec3_t *const va = &vertices[i + 0];
        const vec3_t *const vb = &vertices[i + 1];
        const vec3_t *const vc = &vertices[i + 2];
        vec3_t pa = project(va, transform);
        vec3_t pb = project(vb, transform);
        vec3_t pc = project(vc, transform);
        triangle(&pa, &pb, &pc);
    }
}

// renders a bunch of occulders as cubes to depth
void depth_render_occulders(const vec3_t *const position,
                            const vec3_t *const target,
                            occulder_t *occulders,
                            int count)
{
    static const vec3_t cube[] = {
        { -1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 1.0f },
        { -1.0f, -1.0f, -1.0f },
        { -1.0f, 1.0f, -1.0f },
        { 1.0f, 1.0f, -1.0f },
        { 1.0f, -1.0f, 1.0f },
        { 1.0f, -1.0f, -1.0f }
    };

    const mat4_t view = mat4_look_at_lh(position, target, &((vec3_t){0.0f, 1.0f, 0.0f}));
    const mat4_t proj = mat4_perspective_fov_rh(0.78f, (float)WIDTH/HEIGHT, 0.01f, 1.0f);

    for (int i = 0; i < count; i++) {
        const occulder_t *const o = &occulders[i];
        const mat4_t rypr = mat4_rotation_yaw_pitch_roll(o->rotation);
        const mat4_t translation = mat4_translation(o->position);
        const mat4_t scale = mat4_scale(o->scale);
        const mat4_t rotation = mat4_mul(&rypr, &translation);
        const mat4_t world = mat4_mul(&rotation, &scale);
        const mat4_t world_view = mul(&world, &view);
        const mat4_t world_view_proj = mul(&world_view, &proj);
        depth_render_vertices(&world_view_proj, cube, 8);
    }
}
