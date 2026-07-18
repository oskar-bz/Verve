// vv_command.h — the core's entire output (§8).
//
// A flat array of commands in paint order, allocated in the frame arena. The
// backend iterates and translates; it knows nothing about widgets or themes.
#ifndef VV_COMMAND_H
#define VV_COMMAND_H

#include "vv_types.h"

typedef enum {
    VV_CMD_RECT,
    VV_CMD_TEXT,
    VV_CMD_IMAGE,
    VV_CMD_SCISSOR_PUSH,
    VV_CMD_SCISSOR_POP,
    VV_CMD_TRANSFORM_PUSH,
    VV_CMD_TRANSFORM_POP,
    VV_CMD_CUSTOM,
} vv_CmdKind;

// Bundles fill+border+shadow so the SDF shader can draw it in one instance (§9.1).
typedef struct {
    vv_Rect    rect;
    vv_Corners radius;
    vv_Color   fill_a, fill_b;   // equal = solid; else gradient
    float      gradient_angle;
    vv_Edges   border_width;
    vv_Color   border_color;
    vv_Shadow  shadow;
} vv_CmdRect;

typedef struct {
    const char *utf8;    // points into the frame arena
    uint32_t    len;
    vv_FontID   font;
    float       size;
    vv_Color    color;
    vv_Vec2     origin;  // baseline, left
} vv_CmdText;

typedef struct {
    vv_Rect  rect;
    vv_TexID tex;
    vv_Rect  uv;         // source region, normalized
    vv_Color tint;
} vv_CmdImage;

typedef struct {
    uint32_t id;
    void    *payload;
    vv_Rect  rect;
} vv_CmdCustom;

typedef struct {
    vv_CmdKind kind;
    union {
        vv_CmdRect   rect;
        vv_CmdText   text;
        vv_CmdImage  image;
        vv_Rect      scissor;
        vv_Mat23     xform;
        vv_CmdCustom custom;
    } as;
} vv_Command;

typedef struct {
    vv_Command *items;
    uint32_t    count;
    uint32_t    cap;
} vv_CommandBuffer;

#endif // VV_COMMAND_H
