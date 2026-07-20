#include "verve/vv_theme.h"
#include "vv_test.h"
#include <math.h>
#include <string.h>

// The theme library: introspection, the named palettes, and a text round-trip.

static bool color_near(vv_Color a, vv_Color b, float eps) {
    return fabsf(a.r - b.r) < eps && fabsf(a.g - b.g) < eps &&
           fabsf(a.b - b.b) < eps;
}

int main(void) {
    // Introspection: 12 colour fields, each reachable by name + offset.
    CHECK(vv_theme_field_count == 12);
    {
        vv_Theme t = vv_theme_dark();
        vv_theme_field_set(&t, 2, vv_rgb(0.1f, 0.2f, 0.3f));   // "accent"
        CHECK(strcmp(vv_theme_fields[2].name, "accent") == 0);
        CHECK(color_near(vv_theme_field_get(&t, 2), vv_rgb(0.1f, 0.2f, 0.3f), 1e-4f));
        CHECK(color_near(t.accent, vv_rgb(0.1f, 0.2f, 0.3f), 1e-4f));
    }

    // The library has a non-empty preset table and every maker returns a theme.
    CHECK(vv_theme_preset_count >= 8);
    for (int i = 0; i < vv_theme_preset_count; i++) {
        CHECK(vv_theme_presets[i].name != NULL);
        CHECK(vv_theme_presets[i].make != NULL);
        vv_Theme t = vv_theme_presets[i].make();
        CHECK(t.font_size > 0.0f);   // a real palette, not zeroed
    }

    // write -> parse round-trips every colour + radius + font_size.
    {
        vv_Theme src = vv_theme_adwaita();
        char buf[1024];
        int n = vv_theme_write(&src, buf, (int)sizeof buf);
        CHECK(n > 0 && n < (int)sizeof buf);

        vv_Theme dst = vv_theme_dark();          // start from a different base
        int got = vv_theme_parse(&dst, buf);
        CHECK(got == vv_theme_field_count);       // all colours recognized
        for (int i = 0; i < vv_theme_field_count; i++)
            CHECK(color_near(vv_theme_field_get(&src, i), vv_theme_field_get(&dst, i), 1e-3f));
        // Every scalar metric (radius/border/padding/gap/font) round-trips too.
        CHECK(vv_theme_metric_count == 6);
        for (int i = 0; i < vv_theme_metric_count; i++)
            CHECK(fabsf(vv_theme_metric_get(&src, i) - vv_theme_metric_get(&dst, i)) < 1e-2f);
    }

    // A partial file leaves unmentioned fields untouched.
    {
        vv_Theme t = vv_theme_dark();
        vv_Color keep = t.surface;
        int got = vv_theme_parse(&t, "accent 0.5 0.5 0.5\nradius 3\n");
        CHECK(got == 1);
        CHECK(color_near(t.accent, vv_rgb(0.5f, 0.5f, 0.5f), 1e-4f));
        CHECK(color_near(t.surface, keep, 1e-6f));   // untouched
        CHECK(fabsf(t.radius - 3.0f) < 1e-4f);
    }

    // File round-trip through /tmp.
    {
        vv_Theme src = vv_theme_nord();
        const char *path = "/tmp/vv_test_theme.vvtheme";
        CHECK(vv_theme_save(&src, path));
        vv_Theme dst = vv_theme_light();
        CHECK(vv_theme_load(&dst, path));
        for (int i = 0; i < vv_theme_field_count; i++)
            CHECK(color_near(vv_theme_field_get(&src, i), vv_theme_field_get(&dst, i), 1e-3f));
    }

    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
