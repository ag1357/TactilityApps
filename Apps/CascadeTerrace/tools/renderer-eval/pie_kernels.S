/* P4 PIE kernels for the renderer evaluation (path 3). Assembled with the
 * exact ESP-IDF 6.1 toolchain and march used by the production P4 build
 * (rv32imafc_zicsr_zifencei_zaamo_zalrsc_zcb_zcmp_zcmt_xesploop_xespv).
 *
 * Two kernels cover the two memory-bound hot spots a port of the current
 * renderer (or a Jet-derived one) has on ESP32-P4 at 240x160 -> 480x320:
 *
 *   pie_fill_span_u16(dst, count_u16, color_ptr)
 *       RGB565 span fill: broadcast one u16 colour to 8 lanes
 *       (esp.vldbc.16.ip) then fused store+increment (esp.vst.128.ip),
 *       8 pixels per store, inside a zero-overhead PIE hardware loop.
 *       Direct replacement for fillRGB565Span's paired-u32 path.
 *
 *   pie_expand2x_row_u16(dst, src, chunks)     [chunks = src u16 / 8]
 *       Scanout expansion: load 8 pixels, esp.vzip.16 them with
 *       themselves (each pixel duplicated into adjacent lanes), store
 *       two 128-bit vectors = 16 output pixels per iteration.
 *       Direct replacement for the frontend's per-pixel upscale loop and
 *       for Jet's S3-only expandSwapRGB565 scanout.
 *
 * NOTE: PIE must be enabled on the core before use (CSR 0x7F2 /
 * mext_pie_status; ESP-IDF enables it per core and the FreeRTOS RISC-V
 * port saves/restores PIE register state in the coprocessor area, so
 * these are callable from any task after the port enables PIE).
 */
    .text
    .align  4

/* void pie_fill_span_u16(uint16_t *dst, uint32_t count_u16, const uint16_t *color_ptr) */
    .type   pie_fill_span_u16, @function
    .global pie_fill_span_u16
pie_fill_span_u16:
    /* a0 = dst, a1 = count (u16 units), a2 = color ptr */
    beqz    a1, 5f
    esp.vldbc.16.ip q0, a2, 0          /* q0 = colour broadcast to 8 u16 lanes */
    li      t0, 8
    bltu    a1, t0, 2f                 /* fewer than 8: scalar path */
    srli    t1, a1, 3                  /* t1 = full 8-pixel chunks */
    andi    a1, a1, 7                  /* remainder */
    esp.lp.setup 0, t1, 1f
    esp.vst.128.ip q0, a0, 16          /* 8 u16 pixels, dst advances 16 bytes */
1:  /* loop end */
    beqz    a1, 5f
2:  lw      t2, 0(a2)
    slli    t2, t2, 16
    srli    t3, t2, 16                 /* t3 = colour (zero-extended) */
3:  sh      t3, 0(a0)
    addi    a0, a0, 2
    addi    a1, a1, -1
    bnez    a1, 3b
5:  ret
    .size   pie_fill_span_u16, .-pie_fill_span_u16

/* void pie_expand2x_row_u16(uint16_t *dst, const uint16_t *src, uint32_t chunks)
 * chunks = number of 8-pixel source groups; writes 16*chunks u16 to dst. */
    .type   pie_expand2x_row_u16, @function
    .global pie_expand2x_row_u16
pie_expand2x_row_u16:
    /* a0 = dst, a1 = src, a2 = chunks */
    beqz    a2, 2f
    esp.lp.setup 0, a2, 1f
    esp.vld.128.ip q0, a1, 16          /* q0 = 8 source u16 pixels */
    esp.vzip.16 q0, q0                 /* q0/q1 = each pixel duplicated:
                                          q0 = [p0 p0 p1 p1 p2 p2 p3 p3]
                                          q1 = [p4 p4 p5 p5 p6 p6 p7 p7] */
    esp.vst.128.ip q0, a0, 16
    esp.vst.128.ip q1, a0, 16
1:  /* loop end */
2:  ret
    .size   pie_expand2x_row_u16, .-pie_expand2x_row_u16
