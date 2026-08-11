/* =====================================================================
   cc-reminder case  -  Wemos D1 Mini + 1x WS2812        v3
   ---------------------------------------------------------------------
   THAY DOI SO VOI V1:
   1. Hoc board la HOC KIN (4 thanh bao quanh) thay vi 4 go o goc.
      Khe ho giam tu 0.4 -> 0.25mm/ben, them 3 go bu sai so (crush rib)
      de bu dung sai giua cac lo board.
      Board duoc day sat vao thanh USB -> lo USB khong con lech.
   2. Lo USB tinh tu mat tren PCB, co bien tinh chinh usb_off_y/usb_off_z,
      va co hoc lom mat ngoai de phich cam vao sau hon.
   3. THANH GAI thiet ke lai hoan toan: bo 4 chan mong, doi thanh tam
      hinh chu U truot ngang vao 2 thanh ray. Khong con chi tiet nao
      chiu uon theo huong yeu cua vat in.
   4. Them part "fit_test": mieng thu khop, in ~5 phut de kiem tra hoc
      board + lo USB truoc khi in lai ca de.

   THAY DOI V3:
   5. Mat ngoai cua DE noi ra bang dau vanh rang luoc cua nap
      (44 -> 46.6mm) de hai mat ngoai TRUNG NHAU. Long trong van 40mm,
      nen hoc board va lo USB KHONG doi - da canh roi thi giu nguyen.
      Thanh de day len 2.0 -> 3.3mm; hoc lom USB tang len 2.4mm de bu lai.
   6. Them 8 nub ma sat tren go cam cua nap -> nap khit, khong rung lac.

   openscad -D 'part="base"' -o base.stl cc_reminder_case.scad
   ===================================================================== */

part        = "all";   // [all, base, diffuser, led_bar, led_clip, fit_test]
print_ready = true;    // STL xuat ra da lat san dung huong in

$fn = 72;

/* ---------------- VO ---------------- */
case_w      = 44;
corner_r    = 8;
wall        = 2.0;
floor_t     = 1.6;
base_h      = 15;

/* ---------------- BOARD ---------------- */
pcb_x        = 34.2;   // <-- DO LAI board cua ban
pcb_y        = 25.6;   // <-- DO LAI board cua ban
pcb_t        = 1.6;
pcb_fit      = 0.25;   // khe ho MOI BEN. Chat qua -> tang 0.1
crush_h      = 0.35;   // go bu sai so tren thanh +Y
usb_edge_gap = 0.6;    // khe tu canh PCB den thanh vo phia USB
standoff_h   = 2.0;

/* ---------------- LO USB ----------------
   Tam lo tinh tu MAT TREN PCB cong usb_ctr_dz.
   Micro-USB tren D1 Mini cao ~2.6mm nen tam o khoang 1.3mm.   */
usb_w        = 13.0;
usb_h        = 7.0;
usb_ctr_dz   = 1.3;
usb_off_y    = 0;      // + la dich sang +Y
usb_off_z    = 0;      // + la dich len cao
usb_cb_w     = 16;     // hoc lom mat ngoai cho phich cam sau hon
usb_cb_h     = 9;
usb_cb_d     = 2.4;   // sau hon v2 vi thanh de day len 3.3mm

/* ---------------- LED STRIP ---------------- */
strip_x     = 10.0;
strip_y     = 16.7;    // strip 100 LED/m -> doi thanh 10
strip_gap   = 0.4;
recess_d    = 0.2;

/* ---------------- THANH DO LED ---------------- */
bar_w       = 15.0;
bar_t       = 2.0;
bar_z       = 9.0;
bar_grip    = 1.4;
bar_gap     = 0.4;
wire_sl_x   = 8;
wire_sl_y   = 3;

/* ---------------- RAY + THANH GAI ----------------
   Tam gai truot NGANG vao 2 ray -> chiu luc trong mat phang,
   dung huong khoe nhat cua vat in FDM.                        */
rail_x      = 6.4;     // tam ray
rail_t      = 2.0;
rail_len    = 26;
rail_h      = 3.4;
slot_h      = 2.3;     // chieu cao khe truot
slot_d      = 1.0;     // do sau khe an vao ray
stop_y      = 12.5;    // chan chan cuoi hanh trinh
stop_w      = 1.2;
stop_h      = 0.8;

clip_t      = 2.2;     // day tam gai
clip_len    = 28;
clip_gap    = 0.2;
notch_x     = 7.0;     // be rong khe chu U cho LED loi qua
notch_y0    = -5;

/* ---------------- NAP TAN SANG ---------------- */
dif_h       = 12;
dif_wall    = 1.2;
dif_top     = 1.0;
lip_h       = 3.0;
lip_gap     = 0.25;
lip_nub     = 0.25;    // nub ma sat tren go cam. Chat qua -> giam 0.1
fin_pitch   = 2.0;
fin_w       = 0.8;
fin_len     = 1.3;
crown       = 0;       // 1.5 = giong anh goc, nhung phai bat support

/* ================= DAN XUAT ================= */
S       = case_w/2;              // than vo = mat ngoai thanh nap
Si      = S - wall;              // long trong - GIU NGUYEN 40mm
ri      = corner_r - wall;
// Mat ngoai cua DE noi ra tan dau vanh rang luoc -> trung mat voi nap
S_out   = S + fin_len;
r_out   = corner_r + fin_len;
// Day board sat ve phia thanh USB -> lo USB luon khop
pcb_off_x = -(Si - usb_edge_gap - pcb_x/2);
pcb_top   = floor_t + standoff_h + pcb_t;
usb_z     = pcb_top + usb_ctr_dz + usb_off_z;
bar_top   = bar_t;
clip_w    = 2*(rail_x - rail_t/2 + slot_d) - clip_gap;

/* ================= HELPER ================= */
module rsq(s, r) { offset(r = r) square([2*(s-r), 2*(s-r)], center = true); }
module ring(s, r, t) { difference() { rsq(s, r); rsq(s - t, r - t); } }

/* ================= BASE ================= */

// Hoc kin bao quanh board
module pcb_pocket() {
    px = pcb_x/2 + pcb_fit;
    py = pcb_y/2 + pcb_fit;
    h  = standoff_h + pcb_t + 1.4;
    t  = 1.8;

    // 2 thanh doc canh dai
    for (sy = [-1, 1])
        translate([pcb_off_x, sy*(py + t/2), floor_t + h/2])
            cube([pcb_x - 6, t, h], center = true);
    // 2 thanh 2 dau ngan
    for (sx = [-1, 1])
        translate([pcb_off_x + sx*(px + t/2), 0, floor_t + h/2])
            cube([t, pcb_y - 6, h], center = true);
    // be do PCB
    for (sx = [-1, 1], sy = [-1, 1])
        translate([pcb_off_x + sx*(pcb_x/2 - 3.5), sy*(pcb_y/2 - 3.5),
                   floor_t + standoff_h/2])
            cube([6, 6, standoff_h], center = true);
    // go bu sai so: nen board ve phia -Y cho khit
    for (dx = [-10, 0, 10])
        translate([pcb_off_x + dx, py - crush_h/2 + 0.02,
                   floor_t + standoff_h + pcb_t/2])
            cube([1.2, crush_h, pcb_t + 0.8], center = true);
}

module base_cuts() {
    // lo USB xuyen qua ca thanh vo va thanh hoc board
    translate([-S_out - 3, usb_off_y - usb_w/2, usb_z - usb_h/2])
        cube([(S_out - Si) + 6, usb_w, usb_h]);
    // hoc lom mat ngoai cho phich cam sau hon
    translate([-S_out - 0.1, usb_off_y - usb_cb_w/2, usb_z - usb_cb_h/2])
        cube([usb_cb_d + 0.1, usb_cb_w, usb_cb_h]);
    // 2 ranh cho dau thanh do LED
    for (sy = [-1, 1])
        translate([0, sy*(Si + bar_grip/2), (bar_z + base_h + 1)/2])
            cube([bar_w + bar_gap, bar_grip, base_h + 1 - bar_z], center = true);
    // khe thoat nhiet
    for (i = [-1, 0, 1])
        translate([pcb_off_x + i*7, 0, -1])
            cube([2, 14, floor_t + 2], center = true);
}

module base() {
    difference() {
        union() {
            // vo da khoet rong truoc, roi moi them hoc board,
            // sau do moi khoet lo -> lo USB xuyen duoc ca hoc board
            difference() {
                linear_extrude(base_h) rsq(S_out, r_out);
                translate([0, 0, floor_t]) linear_extrude(base_h) rsq(Si, ri);
            }
            pcb_pocket();
        }
        base_cuts();
    }
}

// Mieng thu khop: chi phan thanh USB + 2 dau hoc board
module fit_test() {
    intersection() {
        base();
        translate([-S_out - 1, -17, -1]) cube([15 + fin_len, 34, base_h + 2]);
    }
}

/* ================= THANH DO LED ================= */
module led_bar() {
    bar_len = 2*Si + 2*bar_grip - bar_gap;
    difference() {
        union() {
            translate([0, 0, bar_t/2])
                cube([bar_w, bar_len, bar_t], center = true);
            // 2 thanh ray dan huong tam gai
            for (sx = [-1, 1])
                translate([sx*rail_x, 0, bar_t + rail_h/2])
                    cube([rail_t, rail_len, rail_h], center = true);
            // chan chan cuoi hanh trinh
            translate([0, -stop_y, bar_t + stop_h/2])
                cube([2*rail_x - rail_t, stop_w, stop_h], center = true);
        }
        // hoc dinh vi strip
        translate([0, 0, bar_t - recess_d/2 + 0.01])
            cube([strip_x + strip_gap, strip_y + strip_gap, recess_d],
                 center = true);
        // khe truot cho tam gai
        for (sx = [-1, 1])
            translate([sx*(rail_x - rail_t/2 + slot_d/2), 0, bar_t + slot_h/2])
                cube([slot_d, rail_len + 2, slot_h], center = true);
        // khe luon day xuong board
        translate([0, strip_y/2 + 4.5, bar_t/2])
            cube([wire_sl_x, wire_sl_y, bar_t + 2], center = true);
    }
}

/* ================= THANH GAI ================= */
module led_clip() {
    difference() {
        translate([0, 0, clip_t/2])
            cube([clip_w, clip_len, clip_t], center = true);
        // khe chu U mo ve dau +Y: truot qua LED khong bi vuong
        ylen = (clip_len/2 + 1) - notch_y0;
        translate([0, notch_y0 + ylen/2, clip_t/2])
            cube([notch_x, ylen, clip_t + 2], center = true);
    }
}

/* ================= NAP TAN SANG ================= */
module fin_ring(s, r, h) {
    L = 2*(s - r);
    A = PI*r/2;
    seg = L + A;
    n = round(seg/fin_pitch);
    p = seg/n;
    for (q = [0:3]) rotate([0, 0, 90*q])
        for (i = [0:n-1]) {
            t = i*p;
            if (t < L) {
                translate([-(s-r) + t, -s, 0]) rotate([0, 0, -90])
                    translate([-0.6, -fin_w/2, 0])
                        cube([fin_len + 0.6, fin_w, h]);
            } else {
                a = (t - L)*180/(PI*r);
                translate([(s-r) + r*cos(a - 90), -(s-r) + r*sin(a - 90), 0])
                    rotate([0, 0, a - 90])
                        translate([-0.6, -fin_w/2, 0])
                            cube([fin_len + 0.6, fin_w, h]);
            }
        }
}

module diffuser() {
    wall_h = dif_h - dif_top;
    linear_extrude(lip_h) ring(Si - lip_gap, ri - lip_gap, dif_wall);
    // 8 nub ma sat: giu nap khit, khong rung lac. Chua toi day go
    // de 0.6mm dau tien van vao de dang.
    for (s2 = [-1, 1], d = [-8, 8]) {
        translate([d, s2*(Si - lip_gap + lip_nub/2 - 0.01), lip_h/2 + 0.3])
            cube([1.5, lip_nub, lip_h - 0.6], center = true);
        translate([s2*(Si - lip_gap + lip_nub/2 - 0.01), d, lip_h/2 + 0.3])
            cube([lip_nub, 1.5, lip_h - 0.6], center = true);
    }
    // vanh ngang noi go cam voi thanh nap
    translate([0, 0, lip_h - 0.01])
        linear_extrude(0.9) ring(S, corner_r, S - (Si - lip_gap - dif_wall));
    translate([0, 0, lip_h]) {
        linear_extrude(wall_h) ring(S, corner_r, dif_wall);
        translate([0, 0, wall_h]) linear_extrude(dif_top) rsq(S, corner_r);
        fin_ring(S, corner_r, wall_h + dif_top + crown);
    }
}

/* ================= XUAT ================= */
if      (part == "base")     base();
else if (part == "led_bar")  led_bar();
else if (part == "led_clip") led_clip();
else if (part == "fit_test") fit_test();
else if (part == "diffuser") {
    if (print_ready) translate([0, 0, lip_h + dif_h]) rotate([180, 0, 0]) diffuser();
    else diffuser();
}
else {
    base();
    translate([case_w + 8, 0, 0]) diffuser();
    translate([0, case_w + 14, 0]) led_bar();
    translate([case_w + 8, case_w + 14, 0]) led_clip();
}
