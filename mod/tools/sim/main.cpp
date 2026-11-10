// Renders the Jungle screens to raw RGB565 frames: sim <outdir>
#include <lvgl.h>
#include <JungleUI.h>
#include <cstdio>
#include <cstring>
#include <string>
static lv_color_t fb[480 * 480];
static void flushCb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * 480 + x] = *px++;
    lv_disp_flush_ready(d);
}
static std::string outdir;
static void run(int ms) {
    for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); }
}
static void dump(const char *name) {
    std::string p = outdir + "/" + name + ".rgb565";
    FILE *f = fopen(p.c_str(), "wb");
    fwrite(fb, sizeof(lv_color_t), 480 * 480, f);
    fclose(f);
}
extern "C" { void jungle_standby_attach(lv_obj_t *parent); extern const lv_img_dsc_t img_standby; }
int main(int argc, char **argv) {
    outdir = argc > 1 ? argv[1] : ".";
    lv_init();
    static lv_disp_draw_buf_t db;
    static lv_color_t buf[480 * 480];
    lv_disp_draw_buf_init(&db, buf, nullptr, 480 * 480);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = 480; drv.ver_res = 480; drv.flush_cb = flushCb; drv.draw_buf = &db;
    lv_disp_drv_register(&drv);
    static JungleUI ui;
    ui.init();
    JungleView v;
    auto shot = [&](const char *name) { ui.show(v.mode); run(700); ui.update(v); run(600); ui.update(v); run(200); dump(name); };
    // Brew
    v = JungleView(); v.mode = JM_BREW; v.temp = 54.3f; v.fillTarget = 95; v.pressure = 0.0f; v.pressureAvailable = true; v.ready = false;
    shot("brew_heating");
    v.temp = 95.0f; v.ready = true; v.showWeight = false; shot("brew_ready");
    v.showWeight = true; v.weight = 0.0f; shot("brew_ready_scale");
    v.active = true; v.shot = true; v.shotFrac = 0.45f; v.weight = 16.2f; v.pressure = 8.7f; strcpy(v.timeText, "14 s"); shot("brew_shot_45");
    v.shotFrac = 1.0f; v.weight = 36.1f; strcpy(v.timeText, "27 s"); shot("brew_shot_100");
    v.active = false; v.shot = false; v.hold = true; shot("brew_hold");
    // Water
    v = JungleView(); v.mode = JM_WATER; v.temp = 25; v.fillTarget = 25; v.pressureAvailable = true; v.showCenter = false; shot("water_25");
    v.fillTarget = 80; v.temp = 61; shot("water_80_heating");
    v.active = true; v.temp = 80; shot("water_80_running");
    // Steam
    v = JungleView(); v.mode = JM_STEAM; v.temp = 101; v.fillTarget = 111; v.pressureAvailable = true; v.pressure = 0.3f; strcpy(v.centerText, "0.3"); shot("steam_heating");
    v.temp = 111; v.pressure = 1.4f; strcpy(v.centerText, "1.4"); shot("steam_ready");
    v.cool = true; v.coolStart = 111; v.coolTarget = 95; v.temp = 103; shot("steam_cool");
    // Grind
    v = JungleView(); v.mode = JM_GRIND; v.grindAvailable = true; strcpy(v.centerText, "18.0"); v.showCenter = true; v.temp = 95; shot("grind");
    // Brew confirm
    v = JungleView(); v.mode = JM_BREW; v.temp = 95; v.fillTarget = 95; v.pressureAvailable = true; v.confirmVisible = true; strcpy(v.confirmText, "Waage nicht verbunden. Trotzdem starten?"); shot("brew_confirm");
    {   // standby: picture + moving hands, one full cycle in 100 ms frames
        lv_obj_t *s = lv_obj_create(nullptr);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *bg = lv_img_create(s);
        lv_img_set_src(bg, &img_standby);
        lv_obj_set_style_align(bg, LV_ALIGN_CENTER, LV_PART_MAIN);
        jungle_standby_attach(s);
        lv_scr_load(s);
        run(20);
        char nm[32];
        for (int i = 0; i < 110; i++) { snprintf(nm, sizeof(nm), "standby_%03d", i); dump(nm); run(100); }
    }
    return 0;
}
