/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* ARP: arpeggiator mode, tempo, range and gate. */

static const char *const ARP_MODE_NAMES[] = {
    "Off", "Up", "Down", "Up/Down", "Random", "Order"
};
#define ARP_MODE_COUNT 6

static struct {
    lv_obj_t *mode;
} lvt_arp;

static void lvt_arp_show(void)
{
    arp_data_t d;
    char b[8];
    uint32_t mi;
    arp_read(&d);

    lv_snprintf(b, sizeof b, "%d", (int)d.mode);
    lvt_card_set(0, "MODE", b, "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)d.tempo);
    lvt_card_set(1, "TEMPO", b, "", lvt_pal.primary, d.tempo * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.range);
    lvt_card_set(2, "RANGE", b, "oct", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)d.gate);
    lvt_card_set(3, "GATE", b, "%", lvt_pal.primary, d.gate * 10);

    mi = (uint32_t)d.mode < ARP_MODE_COUNT ? (uint32_t)d.mode : 0;
    lvt_text(lvt_arp.mode, ARP_MODE_NAMES[mi]);
}

static void lvt_arp_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;
    lvt_cards(body);
    panel = lvt_panel(body);

    lvt_arp.mode = lv_label_create(panel);
    lv_obj_add_style(lvt_arp.mode, &lvt_st_theme, 0);
    lv_obj_set_style_text_font(lvt_arp.mode, &lv_font_montserrat_20, 0);
    lv_obj_set_style_margin_top(lvt_arp.mode, 20, 0);
    lv_label_set_text_static(lvt_arp.mode, "");

    lvt_footer("", "ARP", "");
    lvt_arp_show();
}

static const ctl_slot_t ARP_CONTROLS[] = {
    { ENC_K1, P_ARP,   0, 5, 1, CTLF_WRAP, "MODE" },
    { ENC_K2, P_ARTM,  0, 100, 1, 0, "TEMPO" },
    { ENC_K3, P_ARNG,  0, 4, 1, 0, "RANGE" },
    { ENC_K4, P_AGATE, 0, 100, 1, 0, "GATE" },
};

static const modd_page_t PAGE_ARP = {
    .name      = "ARP",
    .title     = "Arpeggiator",
    .view      = VIEW_ARP,
    .flags     = 0,
    .enter     = lvt_arp_enter,
    .status    = lvt_arp_show,
    .controls  = ARP_CONTROLS,
    .ncontrols = 4,
    .help_text = "K1  Mode\nK2  Tempo\nK3  Range\nK4  Gate",
};
