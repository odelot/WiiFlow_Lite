# RA VBlank C0 hook + TROPHY BADGE overlay (PRODUCTION, flag-gated).
# Runs on Broadway every VBI (Ocarina codehandler C0). Must end with blr.
#
#   0xC0002FF8 : u32 frame counter   (Starlet polls it)
#   0xC0002FFC : u32 trophy flag     (Starlet sets !=0 to draw this frame;
#                                     blinks 3x on achievement unlock)
#   0xC0002FF4 : scratch, previous frame's raw VI_TFBL (double-buffer)
#   0xC0002FF0 : scratch, saved codehandler return LR
#
# Draws a 32x32 badge (white box + black rounded border + gold trophy) into
# BOTH the current VI_TFBL and the previous frame's VI_TFBL so it survives
# double buffering. draw_badge is a subroutine called twice; the codehandler
# return LR is saved first (bl clobbers LR).
#
# REGISTER CONSTRAINT: touch ONLY r3/r5/r9/r10/r11/r12 (r3 free inside
# draw_badge; r9 carries prev across the first draw). r4/r6/r7/r8/r15/r16 are
# live codehandler state — clobbering them freezes the PPC.
#
# Badge top-left = row 24 * 1280 + col 20(u32) * 4 = 0x7850.
    .globl _start
_start:
    lis    12, 0xC000
    lwz    3, 0x2FF8(12)
    addi   3, 3, 1
    stw    3, 0x2FF8(12)

    lwz    9, 0x2FFC(12)        # trophy flag (r12 still 0xC000)
    cmpwi  9, 0
    beq    done

    mflr   9
    stw    9, 0x2FF0(12)        # save codehandler return LR

    lis    12, 0xCC00
    lwz    3, 0x201C(12)        # r3 = current raw VI_TFBL
    cmpwi  3, 0
    beq    restore

    lis    12, 0xC000
    lwz    9, 0x2FF4(12)        # r9 = previous frame's raw VI_TFBL
    stw    3, 0x2FF4(12)        # save current as new previous

    # ---- current buffer ----
    rlwinm 3, 3, 0, 8, 31
    slwi   3, 3, 5
    oris   3, 3, 0xC000
    lis    12, 0x0000
    ori    12, 12, 0x7850
    add    12, 3, 12            # r12 = badge top-left base
    bl     draw_badge

    # ---- previous buffer (skip if 0) ----
    cmpwi  9, 0
    beq    restore
    rlwinm 3, 9, 0, 8, 31
    slwi   3, 3, 5
    oris   3, 3, 0xC000
    lis    12, 0x0000
    ori    12, 12, 0x7850
    add    12, 3, 12
    bl     draw_badge

restore:
    lis    12, 0xC000
    lwz    9, 0x2FF0(12)
    mtlr   9
done:
    blr

# draw_badge: r12 = badge top-left uncached base. Clobbers r3, r5, r10, r11.
# Preserves r9.
draw_badge:
    # ===================== BLACK outline =====================
    lis    11, 0x0080
    ori    11, 11, 0x0080       # COLOR_BLACK 0x00800080
    # B1: rows0-1, c1-14 (14), off 0x4
    addi   5, 12, 0x4
    li     10, 2
bb1:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5); stw 11,48(5); stw 11,52(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bb1
    # B2: rows2-29, c0-15 (16), off 0xA00
    addi   5, 12, 0xA00
    li     10, 28
bb2:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5); stw 11,48(5); stw 11,52(5); stw 11,56(5)
    stw 11,60(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bb2
    # B3: rows30-31, c1-14 (14), off 0x9604 (>0x8000 -> addis+addi)
    addis  5, 12, 1
    addi   5, 5, -0x69FC
    li     10, 2
bb3:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5); stw 11,48(5); stw 11,52(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bb3

    # ===================== WHITE interior =====================
    lis    11, 0xFF80
    ori    11, 11, 0xFF80       # COLOR_WHITE 0xFF80FF80
    # W1: rows2-3, c2-13 (12), off 0xA08
    addi   5, 12, 0xA08
    li     10, 2
bw1:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bw1
    # W2: rows4-27, c1-14 (14), off 0x1404
    addi   5, 12, 0x1404
    li     10, 24
bw2:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5); stw 11,48(5); stw 11,52(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bw2
    # W3: rows28-29, c2-13 (12), off 0x8C08 (>0x8000 -> addis+addi)
    addis  5, 12, 1
    addi   5, 5, -0x73F8
    li     10, 2
bw3:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    stw 11,40(5); stw 11,44(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bw3

    # ===================== GOLD trophy =====================
    lis    11, 0x972B
    ori    11, 11, 0x97A6       # YUYV deep gold (RGB ~204,153,0) — reads on white
    # rim: rows5-7, c3-12 (10), off 0x190C
    addi   5, 12, 0x190C
    li     10, 3
bt1:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5); stw 11,32(5); stw 11,36(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt1
    # bowl: rows8-14, c4-11 (8), off 0x2810
    addi   5, 12, 0x2810
    li     10, 7
bt2:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt2
    # taper: rows15-16, c6-9 (4), off 0x4B18
    addi   5, 12, 0x4B18
    li     10, 2
bt3:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt3
    # stem: rows17-21, c7-8 (2), off 0x551C
    addi   5, 12, 0x551C
    li     10, 5
bt4:
    stw 11,0(5); stw 11,4(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt4
    # plate: rows22-23, c6-9 (4), off 0x6E18
    addi   5, 12, 0x6E18
    li     10, 2
bt5:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt5
    # foot: rows24-26, c4-11 (8), off 0x7810
    addi   5, 12, 0x7810
    li     10, 3
bt6:
    stw 11,0(5); stw 11,4(5); stw 11,8(5); stw 11,12(5); stw 11,16(5)
    stw 11,20(5); stw 11,24(5); stw 11,28(5)
    addi 5,5,1280
    subic. 10,10,1
    bne bt6
    blr
