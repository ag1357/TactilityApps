/* Two 16-byte-aligned destination rows and one aligned source. chunks counts
 * groups of eight input pixels. No tail: the frontend uses width 240.
 * P4 build-qualified only; lane/loop/task-switch tests on hardware are pending. */
    .text
    .align 2
    .global ct_pie_expand2x_rows
    .type ct_pie_expand2x_rows,@function
ct_pie_expand2x_rows:
    beqz a3,2f
    esp.lp.setup 0,a3,1f
    esp.vld.128.ip q0,a2,0
    esp.vld.128.ip q1,a2,16
    esp.vzip.16 q0,q1
    esp.vst.128.ip q0,a0,16
    esp.vst.128.ip q1,a0,16
    esp.vst.128.ip q0,a1,16
    esp.vst.128.ip q1,a1,16
1:
2:  ret
    .size ct_pie_expand2x_rows,.-ct_pie_expand2x_rows
