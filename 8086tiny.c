// 8086tiny: a tiny, highly functional, highly portable PC emulator/VM
// Copyright 2013-14, Adrian Cable (adrian.cable@gmail.com) - http://www.megalith.co.uk/8086tiny
//
// Revision 1.25
//
// This work is licensed under the MIT License. See included LICENSE.TXT.

#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>
#include <memory.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif

#ifndef NO_GRAPHICS
#include "SDL.h"
#endif

// Logging - must be defined early for use in pc_interrupt and emulator_step
#ifdef ANDROID
#include <android/log.h>
#include <jni.h>
#define LOGI2(...) do { __android_log_print(ANDROID_LOG_INFO, "8086tiny", __VA_ARGS__); } while(0)
#else
#define LOGI2(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

// Emulator system constants
#define IO_PORT_COUNT 0x10000
#define RAM_SIZE 0x10FFF0
#define REGS_BASE 0xF0000
#define VIDEO_RAM_SIZE 0x10000

// Graphics/timer/keyboard update delays (explained later)
#ifndef GRAPHICS_UPDATE_DELAY
#define GRAPHICS_UPDATE_DELAY 360000
#endif
#define KEYBOARD_TIMER_UPDATE_DELAY 20000

// 16-bit register decodes
#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7

#define REG_ES 8
#define REG_CS 9
#define REG_SS 10
#define REG_DS 11

#define REG_ZERO 12
#define REG_SCRATCH 13

// 8-bit register decodes
#define REG_AL 0
#define REG_AH 1
#define REG_CL 2
#define REG_CH 3
#define REG_DL 4
#define REG_DH 5
#define REG_BL 6
#define REG_BH 7

// FLAGS register decodes
#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_TF 45
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

// Lookup tables in the BIOS binary
#define TABLE_XLAT_OPCODE 8
#define TABLE_XLAT_SUBFUNCTION 9
#define TABLE_STD_FLAGS 10
#define TABLE_PARITY_FLAG 11
#define TABLE_BASE_INST_SIZE 12
#define TABLE_I_W_SIZE 13
#define TABLE_I_MOD_SIZE 14
#define TABLE_COND_JUMP_DECODE_A 15
#define TABLE_COND_JUMP_DECODE_B 16
#define TABLE_COND_JUMP_DECODE_C 17
#define TABLE_COND_JUMP_DECODE_D 18
#define TABLE_FLAGS_BITFIELDS 19

// Bitfields for TABLE_STD_FLAGS values
#define FLAGS_UPDATE_SZP 1
#define FLAGS_UPDATE_AO_ARITH 2
#define FLAGS_UPDATE_OC_LOGIC 4

// Helper macros

// Decode mod, r_m and reg fields in instruction
#define DECODE_RM_REG scratch2_uint = 4 * !i_mod, \
					  op_to_addr = rm_addr = i_mod < 3 ? SEGREG(seg_override_en ? seg_override : bios_table_lookup[scratch2_uint + 3][i_rm], bios_table_lookup[scratch2_uint][i_rm], regs16[bios_table_lookup[scratch2_uint + 1][i_rm]] + bios_table_lookup[scratch2_uint + 2][i_rm] * i_data1+) : GET_REG_ADDR(i_rm), \
					  op_from_addr = GET_REG_ADDR(i_reg), \
					  i_d && (scratch_uint = op_from_addr, op_from_addr = rm_addr, op_to_addr = scratch_uint)

// Return memory-mapped register location (offset into mem array) for register #reg_id
#define GET_REG_ADDR(reg_id) (REGS_BASE + (i_w ? 2 * reg_id : 2 * reg_id + reg_id / 4 & 7))

// Returns number of top bit in operand (i.e. 8 for 8-bit operands, 16 for 16-bit operands)
#define TOP_BIT 8*(i_w + 1)

// Opcode execution unit helpers
#define OPCODE ;break; case
#define OPCODE_CHAIN ; case

// [I]MUL/[I]DIV/DAA/DAS/ADC/SBB helpers
#define MUL_MACRO(op_data_type,out_regs) (set_opcode(0x10), \
										  out_regs[i_w + 1] = (op_result = CAST(op_data_type)mem[rm_addr] * (op_data_type)*out_regs) >> 16, \
										  regs16[REG_AX] = op_result, \
										  set_OF(set_CF(op_result - (op_data_type)op_result)))
#define DIV_MACRO(out_data_type,in_data_type,out_regs) (scratch_int = CAST(out_data_type)mem[rm_addr]) && !(scratch2_uint = (in_data_type)(scratch_uint = (out_regs[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (out_data_type)scratch2_uint) ? out_regs[i_w+1] = scratch_uint - scratch_int * (*out_regs = scratch2_uint) : pc_interrupt(0)
#define DAA_DAS(op1,op2,mask,min) set_AF((((scratch2_uint = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF]) && (op_result = regs8[REG_AL] op1 6, set_CF(regs8[FLAG_CF] || (regs8[REG_AL] op2 scratch2_uint))), \
								  set_CF((((mask & 1 ? scratch2_uint : regs8[REG_AL]) & mask) > min) || regs8[FLAG_CF]) && (op_result = regs8[REG_AL] op1 0x60)
#define ADC_SBB_MACRO(a) OP(a##= regs8[FLAG_CF] +), \
						 set_CF(regs8[FLAG_CF] && (op_result == op_dest) || (a op_result < a(int)op_dest)), \
						 set_AF_OF_arith()

// Execute arithmetic/logic operations in emulator memory/registers
#define R_M_OP(dest,op,src) (i_w ? op_dest = CAST(unsigned short)dest, op_result = CAST(unsigned short)dest op (op_source = CAST(unsigned short)src) \
								 : (op_dest = dest, op_result = dest op (op_source = CAST(unsigned char)src)))
#define MEM_OP(dest,op,src) R_M_OP(mem[dest],op,mem[src])
#define OP(op) MEM_OP(op_to_addr,op,op_from_addr)

// Increment or decrement a register #reg_id (usually SI or DI), depending on direction flag and operand size (given by i_w)
#define INDEX_INC(reg_id) (regs16[reg_id] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1))

// Helpers for stack operations
#define R_M_PUSH(a) (i_w = 1, R_M_OP(mem[SEGREG(REG_SS, REG_SP, --)], =, a))
#define R_M_POP(a) (i_w = 1, regs16[REG_SP] += 2, R_M_OP(a, =, mem[SEGREG(REG_SS, REG_SP, -2+)]))

// Convert segment:offset to linear address in emulator memory space
#define SEGREG(reg_seg,reg_ofs,op) 16 * regs16[reg_seg] + (unsigned short)(op regs16[reg_ofs])

// Returns sign bit of an 8-bit or 16-bit operand
#define SIGN_OF(a) (1 & (i_w ? CAST(short)a : a) >> (TOP_BIT - 1))

// Reinterpretation cast
#define CAST(a) *(a*)&

// Keyboard driver for console. This may need changing for UNIX/non-UNIX platforms
#ifdef _WIN32
#define KEYBOARD_DRIVER kbhit() && (mem[0x4A6] = getch(), pc_interrupt(7))
#else
#define KEYBOARD_DRIVER read(0, mem + 0x4A6, 1) && (int8_asap = (mem[0x4A6] == 0x1B), pc_interrupt(7))
#endif

// Keyboard driver for SDL
#ifdef NO_GRAPHICS
#define SDL_KEYBOARD_DRIVER KEYBOARD_DRIVER
#else
#define SDL_KEYBOARD_DRIVER sdl_screen ? SDL_PollEvent(&sdl_event) && (sdl_event.type == SDL_KEYDOWN || sdl_event.type == SDL_KEYUP) && (scratch_uint = sdl_event.key.keysym.unicode, scratch2_uint = sdl_event.key.keysym.mod, CAST(short)mem[0x4A6] = 0x400 + 0x800*!!(scratch2_uint & KMOD_ALT) + 0x1000*!!(scratch2_uint & KMOD_SHIFT) + 0x2000*!!(scratch2_uint & KMOD_CTRL) + 0x4000*(sdl_event.type == SDL_KEYUP) + ((!scratch_uint || scratch_uint > 0x7F) ? sdl_event.key.keysym.sym : scratch_uint), pc_interrupt(7)) : (KEYBOARD_DRIVER)
#endif

// Global variable definitions
unsigned char mem[RAM_SIZE], io_ports[IO_PORT_COUNT], *opcode_stream, *regs8, i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit, raw_opcode_id, xlat_opcode_id, extra, rep_mode, seg_override_en, rep_override_en, trap_flag, int8_asap, scratch_uchar, io_hi_lo, *vid_mem_base, spkr_en, bios_table_lookup[20][256];
unsigned short *regs16, reg_ip, seg_override, file_index, wave_counter;
unsigned int op_source, op_dest, rm_addr, op_to_addr, op_from_addr, i_data0, i_data1, i_data2, scratch_uint, scratch2_uint, inst_counter, set_flags_type, GRAPHICS_X, GRAPHICS_Y, pixel_colors[16], vmem_ctr;
int op_result, disk[3], scratch_int;
time_t clock_buf;
struct timeb ms_clock;

#ifndef NO_GRAPHICS
SDL_AudioSpec sdl_audio = {44100, AUDIO_U8, 1, 0, 128};
SDL_Surface *sdl_screen;
SDL_Event sdl_event;
unsigned short vid_addr_lookup[VIDEO_RAM_SIZE], cga_colors[4] = {0 /* Black */, 0x1F1F /* Cyan */, 0xE3E3 /* Magenta */, 0xFFFF /* White */};
#endif

// Helper functions

// Set carry flag
char set_CF(int new_CF)
{
	return regs8[FLAG_CF] = !!new_CF;
}

// Set auxiliary flag
char set_AF(int new_AF)
{
	return regs8[FLAG_AF] = !!new_AF;
}

// Set overflow flag
char set_OF(int new_OF)
{
	return regs8[FLAG_OF] = !!new_OF;
}

// Set auxiliary and overflow flag after arithmetic operations
char set_AF_OF_arith()
{
	set_AF((op_source ^= op_dest ^ op_result) & 0x10);
	if (op_result == op_dest)
		return set_OF(0);
	else
		return set_OF(1 & (regs8[FLAG_CF] ^ op_source >> (TOP_BIT - 1)));
}

// Assemble and return emulated CPU FLAGS register in scratch_uint
void make_flags()
{
	scratch_uint = 0xF002; // 8086 has reserved and unused flags set to 1
	for (int i = 9; i--;)
		scratch_uint += regs8[FLAG_CF + i] << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i];
}

// Set emulated CPU FLAGS register from regs8[FLAG_xx] values
void set_flags(int new_flags)
{
	for (int i = 9; i--;)
		regs8[FLAG_CF + i] = !!(1 << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i] & new_flags);
}

// Convert raw opcode to translated opcode index. This condenses a large number of different encodings of similar
// instructions into a much smaller number of distinct functions, which we then execute
void set_opcode(unsigned char opcode)
{
	xlat_opcode_id = bios_table_lookup[TABLE_XLAT_OPCODE][raw_opcode_id = opcode];
	extra = bios_table_lookup[TABLE_XLAT_SUBFUNCTION][opcode];
	i_mod_size = bios_table_lookup[TABLE_I_MOD_SIZE][opcode];
	set_flags_type = bios_table_lookup[TABLE_STD_FLAGS][opcode];
}

// Execute INT #interrupt_num on the emulated machine
char pc_interrupt(unsigned char interrupt_num)
{
    unsigned short ivt_ip = (unsigned short)mem[4 * interrupt_num];
    unsigned short ivt_cs = (unsigned short)mem[4 * interrupt_num + 2];
    set_opcode(0xCD); // Decode like INT

    make_flags();
    R_M_PUSH(scratch_uint);
    R_M_PUSH(regs16[REG_CS]);
    R_M_PUSH(reg_ip);
    MEM_OP(REGS_BASE + 2 * REG_CS, =, 4 * interrupt_num + 2);
    R_M_OP(reg_ip, =, mem[4 * interrupt_num]);
    LOGI2("INT %02X: IVT[%02X] IP=%04X CS=%04X -> CS:IP=%04X:%04X", interrupt_num, interrupt_num, ivt_ip, ivt_cs, regs16[REG_CS], reg_ip);
    if (regs16[REG_CS] == 0)
        LOGI2("WARNING: INT %02X set CS=0! IVT raw bytes: [0x%02X,0x%02X,0x%02X,0x%02X]", interrupt_num, mem[4*interrupt_num], mem[4*interrupt_num+1], mem[4*interrupt_num+2], mem[4*interrupt_num+3]);

    return regs8[FLAG_TF] = regs8[FLAG_IF] = 0;
}

// AAA and AAS instructions - which_operation is +1 for AAA, and -1 for AAS
int AAA_AAS(char which_operation)
{
	return (regs16[REG_AX] += 262 * which_operation*set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF])), regs8[REG_AL] &= 0x0F);
}

#ifndef NO_GRAPHICS
void audio_callback(void *data, unsigned char *stream, int len)
{
	for (int i = 0; i < len; i++)
		stream[i] = (spkr_en == 3) && CAST(unsigned short)mem[0x4AA] ? -((54 * wave_counter++ / CAST(unsigned short)mem[0x4AA]) & 1) : sdl_audio.silence;

	spkr_en = io_ports[0x61] & 3;
}
#endif

// Text mode framebuffer: 80 cols x 25 rows x 8x8 font = 640x200
// Each pixel is 32-bit ARGB
unsigned int text_framebuffer[640 * 200];
int text_fb_width = 640;
int text_fb_height = 200;
int text_mode_active = 0;

#ifdef ANDROID
jobject g_emulator_view = NULL;
JNIEnv *g_jni_env = NULL;
int reset_requested = 0;
#else
int reset_requested = 0;
#endif

// Corrected 8x8 font for text mode (ASCII 32-127, 96 chars)
// Fixed: Z (8 bytes), p (8 bytes), added glyphs for [ ] ` { | } ~ DEL
static const unsigned char font8x8_basic[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 0x21 !
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x22 "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 0x23 #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 0x24 $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 0x25 %
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 0x26 &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 0x27 '
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 0x28 (
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 0x29 )
    {0x00,0x63,0x3E,0xFF,0x3E,0x63,0x00,0x00}, // 0x2A *
    {0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00}, // 0x2B +
    {0x00,0x00,0x00,0x00,0x0C,0x0C,0x06,0x00}, // 0x2C ,
    {0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00}, // 0x2D -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 0x2E .
    {0x18,0x0C,0x0C,0x06,0x03,0x03,0x01,0x00}, // 0x2F /
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 0x30 0
    {0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x3E,0x00}, // 0x31 1
    {0x3E,0x63,0x30,0x1C,0x06,0x3F,0x7F,0x00}, // 0x32 2
    {0x3E,0x63,0x30,0x1C,0x30,0x63,0x3E,0x00}, // 0x33 3
    {0x33,0x33,0x3F,0x30,0x30,0x30,0x78,0x00}, // 0x34 4
    {0x7F,0x30,0x3E,0x30,0x30,0x63,0x3E,0x00}, // 0x35 5
    {0x1C,0x06,0x03,0x3E,0x63,0x63,0x3E,0x00}, // 0x36 6
    {0x7F,0x63,0x31,0x18,0x0C,0x06,0x03,0x00}, // 0x37 7
    {0x3E,0x63,0x63,0x3E,0x63,0x63,0x3E,0x00}, // 0x38 8
    {0x3E,0x63,0x63,0x7E,0x30,0x18,0x0C,0x00}, // 0x39 9
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x06,0x00}, // 0x3A :
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 0x3B ;
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 0x3C <
    {0x00,0x00,0x3E,0x00,0x3E,0x00,0x00,0x00}, // 0x3D =
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 0x3E >
    {0x3E,0x63,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 0x3F ?
    {0x3E,0x63,0x7B,0x7B,0x7B,0x3F,0x1C,0x00}, // 0x40 @
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // 0x41 A
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // 0x42 B
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 0x43 C
    {0x78,0x6C,0x66,0x63,0x63,0x66,0x78,0x00}, // 0x44 D
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 0x45 E
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 0x46 F
    {0x3C,0x66,0x03,0x03,0x33,0x66,0x3C,0x00}, // 0x47 G
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // 0x48 H
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 0x49 I
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // 0x4A J
    {0x63,0x33,0x1B,0x0F,0x0D,0x33,0x63,0x00}, // 0x4B K
    {0x0F,0x06,0x06,0x06,0x06,0x46,0x7F,0x00}, // 0x4C L
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 0x4D M
    {0x63,0x73,0x7B,0x7F,0x77,0x67,0x63,0x00}, // 0x4E N
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 0x4F O
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // 0x50 P
    {0x3C,0x66,0x66,0x66,0x76,0x6C,0x36,0x00}, // 0x51 Q
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00}, // 0x52 R
    {0x3C,0x66,0x0C,0x18,0x30,0x66,0x3C,0x00}, // 0x53 S
    {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00}, // 0x54 T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3E,0x00}, // 0x55 U
    {0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // 0x56 V
    {0x63,0x63,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 0x57 W
    {0x63,0x66,0x3C,0x18,0x3C,0x66,0x63,0x00}, // 0x58 X
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 0x59 Y
    {0x7F,0x63,0x31,0x18,0x46,0x63,0x00,0x00}, // 0x5A Z (fixed: was 7 bytes)
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 0x5B [
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 0x5C \
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 0x5D ]
    {0x08,0x1C,0x3E,0x7F,0x3E,0x1C,0x08,0x00}, // 0x5E ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 0x5F _
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x60 `
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x3E,0x00}, // 0x61 a
    {0x07,0x06,0x06,0x7E,0x63,0x63,0x7E,0x00}, // 0x62 b
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 0x63 c
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x3E,0x00}, // 0x64 d
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 0x65 e
    {0x1C,0x06,0x06,0x0F,0x06,0x06,0x06,0x00}, // 0x66 f
    {0x00,0x00,0x3E,0x63,0x7E,0x30,0x1F,0x00}, // 0x67 g
    {0x07,0x06,0x06,0x7E,0x63,0x63,0x63,0x00}, // 0x68 h
    {0x0C,0x0C,0x00,0x0E,0x0C,0x0C,0x1E,0x00}, // 0x69 i
    {0x18,0x00,0x18,0x18,0x18,0x18,0x68,0x00}, // 0x6A j
    {0x07,0x06,0x33,0x1B,0x0F,0x1B,0x3B,0x00}, // 0x6B k
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0E,0x00}, // 0x6C l
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 0x6D m
    {0x00,0x00,0x33,0x7F,0x7F,0x63,0x63,0x00}, // 0x6E n
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 0x6F o
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x00}, // 0x70 p (fixed: was 9 bytes)
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x00}, // 0x71 q
    {0x00,0x00,0x7C,0x66,0x66,0x60,0x00,0x00}, // 0x72 r
    {0x00,0x00,0x3E,0x63,0x30,0x1E,0x00,0x00}, // 0x73 s
    {0x06,0x06,0x06,0x0F,0x06,0x06,0x46,0x00}, // 0x74 t
    {0x00,0x00,0x63,0x63,0x63,0x33,0x1E,0x00}, // 0x75 u
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // 0x76 v
    {0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00}, // 0x77 w
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x00,0x00}, // 0x78 x
    {0x00,0x00,0x63,0x63,0x7E,0x30,0x1F,0x00}, // 0x79 y
    {0x00,0x00,0x7F,0x18,0x18,0x7F,0x00,0x00}, // 0x7A z
    {0x0C,0x18,0x18,0x30,0x18,0x18,0x0C,0x00}, // 0x7B {
    {0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x00}, // 0x7C |
    {0x30,0x18,0x18,0x0C,0x18,0x18,0x30,0x00}, // 0x7D }
    {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00}, // 0x7E ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7F DEL
};

// Render text mode video RAM (B800:0) to text_framebuffer
void render_text_mode() {
    unsigned char *text_vram = mem + 0xB8000;
    int char_width = 8;
    int char_height = 8;
    int cols = 80;
    int rows = 25;
    
    // Clear framebuffer to black
    for (int i = 0; i < text_fb_width * text_fb_height; i++) {
        text_framebuffer[i] = 0xFF000000;
    }
    
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int vram_offset = (row * cols + col) * 2;
            unsigned char ch = text_vram[vram_offset];
            unsigned char attr = text_vram[vram_offset + 1];
            
            int fg_color = attr & 0x0F;
            int bg_color = (attr >> 4) & 0x07;
            int blink = (attr >> 7) & 1;
            
            // CGA color palette to 32-bit ARGB
            static const unsigned int cga_palette[16] = {
                0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, // 0-3
                0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA, // 4-7
                0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, // 8-11
                0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF  // 12-15
            };
            
            unsigned int fg = cga_palette[fg_color];
            unsigned int bg = cga_palette[bg_color];
            
            if (ch >= 32 && ch < 128) {
                const unsigned char *glyph = font8x8_basic[ch - 32];
                for (int y = 0; y < char_height; y++) {
                    unsigned char bits = glyph[y];
                    for (int x = 0; x < char_width; x++) {
                        int fb_x = col * char_width + x;
                        int fb_y = row * char_height + y;
                        if (fb_x < text_fb_width && fb_y < text_fb_height) {
                            text_framebuffer[fb_y * text_fb_width + fb_x] = (bits & (0x80 >> x)) ? fg : bg;
                        }
                    }
                }
            }
        }
    }
    text_mode_active = 1;
}

// Emulator initialization function (formerly main() initialization logic)
void emulator_init(int argc, char **argv) {
    LOGI2("emulator_init: entered, argc=%d", argc);
    for (int i = 0; i < argc; i++) LOGI2("emulator_init: argv[%d]=%s", i, argv[i]);
    
#ifndef NO_GRAPHICS
    LOGI2("emulator_init: SDL_Init(SDL_INIT_AUDIO) - SKIPPED");
    sdl_audio.callback = audio_callback;
#ifdef _WIN32
    sdl_audio.samples = 512;
#endif
    LOGI2("emulator_init: SDL_Init done (skipped)");
#endif
    
    // regs16 and reg8 point to F000:0, the start of memory-mapped registers. CS is initialised to F000
    regs16 = (unsigned short *)(regs8 = mem + REGS_BASE);
    regs16[REG_CS] = 0xF000;
    
    // Trap flag off
    regs8[FLAG_TF] = 0;
    
    // Set DL equal to the boot device: 0 for the FD, or 0x80 for the HD. Normally, boot from the FD.
    // But, if the HD image file is prefixed with @, then boot from the HD
    regs8[REG_DL] = ((argc > 3) && (*argv[3] == '@')) ? argv[3]++, 0x80 : 0;
    
    // Open BIOS (file id disk[2]), floppy disk image (disk[1]), and hard disk image (disk[0]) if specified
    LOGI2("emulator_init: opening files");
    for (file_index = 3; file_index;)
        disk[--file_index] = *++argv ? open(*argv, 32898) : 0;
    LOGI2("emulator_init: files opened, disk[0]=%d disk[1]=%d disk[2]=%d", disk[0], disk[1], disk[2]);
    
    // Set CX:AX equal to the hard disk image size, if present
    CAST(unsigned)regs16[REG_AX] = *disk ? lseek(*disk, 0, 2) >> 9 : 0;
    
    // Load BIOS image into F000:0100, and set IP to 0100
    LOGI2("emulator_init: reading BIOS into memory");
    read(disk[2], regs8 + (reg_ip = 0x100), 0xFF00);
    LOGI2("emulator_init: BIOS loaded");
    
    // Load instruction decoding helper table
    LOGI2("emulator_init: loading bios_table_lookup");
    for (int i = 0; i < 20; i++)
        for (int j = 0; j < 256; j++)
            bios_table_lookup[i][j] = regs8[regs16[0x81 + i] + j];
    LOGI2("emulator_init: bios_table_lookup loaded");
    
    LOGI2("emulator_init: init complete");
}

// Emulator step function (formerly main() loop)
void emulator_step(int max_instructions) {
    LOGI2("emulator_step: started, max_instructions=%d", max_instructions);
    int instructions_executed = 0;
    
    for (; opcode_stream = mem + 16 * regs16[REG_CS] + reg_ip, opcode_stream != mem && instructions_executed < max_instructions;)
    {
        // Handle reset request
        if (reset_requested) {
            reset_requested = 0;
            regs16 = (unsigned short *)(regs8 = mem + REGS_BASE);
            regs16[REG_CS] = 0xF000;
            regs8[FLAG_TF] = 0;
            reg_ip = 0x100;
            lseek(disk[2], 0, SEEK_SET);
            read(disk[2], regs8 + reg_ip, 0xFF00);
            for (int i = 0; i < 20; i++)
                for (int j = 0; j < 256; j++)
                    bios_table_lookup[i][j] = regs8[regs16[0x81 + i] + j];
            mem[0x4A6] = 0;
            io_ports[0x64] = 0;
            inst_counter = 0;
            int8_asap = 0;
            seg_override_en = 0;
            rep_override_en = 0;
            trap_flag = 0;
            LOGI2("emulator_step: reset complete");
            continue;
        }
        
        set_opcode(*opcode_stream);
        
        if (inst_counter < 200) {
            LOGI2("INST #%d: CS:IP=%04X:%04X, raw=0x%02X, xlat=%d, i_w=%d, i_d=%d", inst_counter, regs16[REG_CS], reg_ip, raw_opcode_id, xlat_opcode_id, i_w, i_d);
        }
        if (regs16[REG_CS] == 0 || reg_ip == 0) {
            LOGI2("WARNING: CS=%04X IP=%04X at inst #%d (raw=0x%02X, xlat=%d)", regs16[REG_CS], reg_ip, inst_counter, raw_opcode_id, xlat_opcode_id);
        }
        
        i_w = (i_reg4bit = raw_opcode_id & 7) & 1;
        i_d = i_reg4bit / 2 & 1;
        
        i_data0 = CAST(short)opcode_stream[1];
        i_data1 = CAST(short)opcode_stream[2];
        i_data2 = CAST(short)opcode_stream[3];
        
        if (seg_override_en) seg_override_en--;
        if (rep_override_en) rep_override_en--;
        
        if (i_mod_size)
        {
            i_mod = (i_data0 & 0xFF) >> 6;
            i_rm = i_data0 & 7;
            i_reg = i_data0 / 8 & 7;
            
            if ((!i_mod && i_rm == 6) || (i_mod == 2))
                i_data2 = CAST(short)opcode_stream[4];
            else if (i_mod != 1)
                i_data2 = i_data1;
            else
                i_data1 = (char)i_data1;
            
            DECODE_RM_REG;
        }
        
        switch (xlat_opcode_id)
        {
            OPCODE_CHAIN 0: // Conditional jump (JAE, JNAE, etc.)
                scratch_uchar = raw_opcode_id / 2 & 7;
                reg_ip += (char)i_data0 * (i_w ^ (regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_A][scratch_uchar]] || regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_B][scratch_uchar]] || regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_C][scratch_uchar]] ^ regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_D][scratch_uchar]]));
            OPCODE 1: // MOV reg, imm
                i_w = !!(raw_opcode_id & 8);
                R_M_OP(mem[GET_REG_ADDR(i_reg4bit)], =, i_data0);
            OPCODE 3: // PUSH regs16
                R_M_PUSH(regs16[i_reg4bit]);
            OPCODE 4: // POP regs16
                R_M_POP(regs16[i_reg4bit]);
            OPCODE 2: // INC|DEC regs16
                i_w = 1;
                i_d = 0;
                i_reg = i_reg4bit;
                DECODE_RM_REG;
                i_reg = extra;
            OPCODE_CHAIN 5: // INC|DEC|JMP|CALL|PUSH
                if (i_reg < 2) // INC|DEC
                    MEM_OP(op_from_addr, += 1 - 2 * i_reg +, REGS_BASE + 2 * REG_ZERO), op_source = 1, set_AF_OF_arith(), set_OF(op_dest + 1 - i_reg == 1 << (TOP_BIT - 1)), (xlat_opcode_id == 5) && (set_opcode(0x10), 0);
                else if (i_reg != 6) // JMP|CALL
                    i_reg - 3 || R_M_PUSH(regs16[REG_CS]), i_reg & 2 && R_M_PUSH(reg_ip + 2 + i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6)), i_reg & 1 && (regs16[REG_CS] = CAST(short)mem[op_from_addr + 2]), R_M_OP(reg_ip, =, mem[op_from_addr]), set_opcode(0x9A);
                else // PUSH
                    R_M_PUSH(mem[rm_addr]);
            OPCODE 6: // TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg
                op_to_addr = op_from_addr;
                switch (i_reg)
                {
                    OPCODE_CHAIN 0: // TEST
                        set_opcode(0x20), reg_ip += i_w + 1, R_M_OP(mem[op_to_addr], &, i_data2);
                    OPCODE 2: // NOT
                        OP(=~);
                    OPCODE 3: // NEG
                        OP(=-); op_dest = 0, set_opcode(0x28), set_CF(op_result > op_dest);
                    OPCODE 4: // MUL
                        i_w ? MUL_MACRO(unsigned short, regs16) : MUL_MACRO(unsigned char, regs8);
                    OPCODE 5: // IMUL
                        i_w ? MUL_MACRO(short, regs16) : MUL_MACRO(char, regs8);
                    OPCODE 6: // DIV
                        LOGI2("DIV: i_w=%d rm_addr=0x%X mem[rm_addr]=0x%02X DX=%04X AX=%04X", i_w, rm_addr, mem[rm_addr], regs16[REG_DX], regs16[REG_AX]);
                        i_w ? DIV_MACRO(unsigned short, unsigned, regs16) : DIV_MACRO(unsigned char, unsigned short, regs8);
                    OPCODE 7: // IDIV
                        LOGI2("IDIV: i_w=%d rm_addr=0x%X mem[rm_addr]=0x%02X DX=%04X AX=%04X", i_w, rm_addr, mem[rm_addr], regs16[REG_DX], regs16[REG_AX]);
                        i_w ? DIV_MACRO(short, int, regs16) : DIV_MACRO(char, short, regs8);
                }
            OPCODE 7: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed
                rm_addr = REGS_BASE;
                i_data2 = i_data0;
                i_mod = 3;
                i_reg = extra;
                reg_ip--;
            OPCODE_CHAIN 8: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP reg, immed
                op_to_addr = rm_addr;
                regs16[REG_SCRATCH] = (i_d |= !i_w) ? (char)i_data2 : i_data2;
                op_from_addr = REGS_BASE + 2 * REG_SCRATCH;
                reg_ip += !i_d + 1;
                set_opcode(0x08 * (extra = i_reg));
            OPCODE_CHAIN 9: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m
                switch (extra)
                {
                    OPCODE_CHAIN 0: // ADD
                        OP(+=), set_CF(op_result < op_dest);
                    OPCODE 1: // OR
                        OP(|=);
                    OPCODE 2: // ADC
                        ADC_SBB_MACRO(+);
                    OPCODE 3: // SBB
                        ADC_SBB_MACRO(-);
                    OPCODE 4: // AND
                        OP(&=);
                    OPCODE 5: // SUB
                        OP(-=), set_CF(op_result > op_dest);
                    OPCODE 6: // XOR
                        OP(^=);
                    OPCODE 7: // CMP
                        OP(-), set_CF(op_result > op_dest);
                    OPCODE 8: // MOV
                        OP(=);
                }
            OPCODE 10: // MOV sreg, r/m | POP r/m | LEA reg, r/m
                if (!i_w) // MOV
                    i_w = 1, i_reg += 8, DECODE_RM_REG, OP(=);
                else if (!i_d) // LEA
                    seg_override_en = 1, seg_override = REG_ZERO, DECODE_RM_REG, R_M_OP(mem[op_from_addr], =, rm_addr);
                else // POP
                    R_M_POP(mem[rm_addr]);
            OPCODE 11: // MOV AL/AX, [loc]
                i_mod = i_reg = 0;
                i_rm = 6;
                i_data1 = i_data0;
                DECODE_RM_REG;
                MEM_OP(op_from_addr, =, op_to_addr);
            OPCODE 12: // ROL|ROR|RCL|RCR|SHL|SHR|???|SAR reg/mem, 1/CL/imm (80186)
                scratch2_uint = SIGN_OF(mem[rm_addr]), scratch_uint = extra ? ++reg_ip, (char)i_data1 : i_d ? 31 & regs8[REG_CL] : 1;
                if (scratch_uint)
                {
                    if (i_reg < 4) scratch_uint %= i_reg / 2 + TOP_BIT, R_M_OP(scratch2_uint, =, mem[rm_addr]);
                    if (i_reg & 1) R_M_OP(mem[rm_addr], >>=, scratch_uint);
                    else R_M_OP(mem[rm_addr], <<=, scratch_uint);
                    if (i_reg > 3) set_opcode(0x10);
                    if (i_reg > 4) set_CF(op_dest >> (scratch_uint - 1) & 1);
                }
                switch (i_reg)
                {
                    OPCODE_CHAIN 0: // ROL
                        R_M_OP(mem[rm_addr], += , scratch2_uint >> (TOP_BIT - scratch_uint));
                        set_OF(SIGN_OF(op_result) ^ set_CF(op_result & 1));
                    OPCODE 1: // ROR
                        scratch2_uint &= (1 << scratch_uint) - 1, R_M_OP(mem[rm_addr], += , scratch2_uint << (TOP_BIT - scratch_uint));
                        set_OF(SIGN_OF(op_result * 2) ^ set_CF(SIGN_OF(op_result)));
                    OPCODE 2: // RCL
                        R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (scratch_uint - 1)) + , scratch2_uint >> (1 + TOP_BIT - scratch_uint));
                        set_OF(SIGN_OF(op_result) ^ set_CF(scratch2_uint & 1 << (TOP_BIT - scratch_uint)));
                    OPCODE 3: // RCR
                        R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (TOP_BIT - scratch_uint)) + , scratch2_uint << (1 + TOP_BIT - scratch_uint));
                        set_CF(scratch2_uint & 1 << (scratch_uint - 1));
                        set_OF(SIGN_OF(op_result) ^ SIGN_OF(op_result * 2));
                    OPCODE 4: // SHL
                        set_OF(SIGN_OF(op_result) ^ set_CF(SIGN_OF(op_dest << (scratch_uint - 1))));
                    OPCODE 5: // SHR
                        set_OF(SIGN_OF(op_dest));
                    OPCODE 7: // SAR
                        scratch_uint < TOP_BIT || set_CF(scratch2_uint);
                        set_OF(0), R_M_OP(mem[rm_addr], +=, scratch2_uint *= ~(((1 << TOP_BIT) - 1) >> scratch_uint));
                }
            OPCODE 13: // LOOPxx|JCZX
                scratch_uint = !!--regs16[REG_CX];
                switch(i_reg4bit)
                {
                    OPCODE_CHAIN 0: // LOOPNZ
                        scratch_uint &= !regs8[FLAG_ZF];
                    OPCODE 1: // LOOPZ
                        scratch_uint &= regs8[FLAG_ZF];
                    OPCODE 3: // JCXXZ
                        scratch_uint = !++regs16[REG_CX];
                }
                reg_ip += scratch_uint*(char)i_data0;
            OPCODE 14: // JMP | CALL short/near
                reg_ip += 3 - i_d;
                if (!i_w) {
                    if (i_d) { // JMP far
                        LOGI2("JMP FAR: i_data0=%04X i_data2=%04X -> CS=%04X IP=0000", i_data0, i_data2, i_data2);
                        reg_ip = 0, regs16[REG_CS] = i_data2;
                    }
                    else // CALL
                        R_M_PUSH(reg_ip);
                }
                reg_ip += i_d && i_w ? (char)i_data0 : i_data0;
            OPCODE 15: // TEST reg, r/m
                MEM_OP(op_from_addr, &, op_to_addr);
            OPCODE 16: // XCHG AX, regs16
                i_w = 1;
                op_to_addr = REGS_BASE;
                op_from_addr = GET_REG_ADDR(i_reg4bit);
            OPCODE_CHAIN 24: // NOP|XCHG reg, r/m
                if (op_to_addr != op_from_addr)
                    OP(^=), MEM_OP(op_from_addr, ^=, op_to_addr), OP(^=);
            OPCODE 17: // MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2)
                scratch2_uint = seg_override_en ? seg_override : REG_DS;
                for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1; scratch_uint; scratch_uint--)
                {
                    MEM_OP(extra < 2 ? SEGREG(REG_ES, REG_DI,) : REGS_BASE, =, extra & 1 ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,));
                    extra & 1 || INDEX_INC(REG_SI);
                    extra & 2 || INDEX_INC(REG_DI);
                }
                if (rep_override_en) regs16[REG_CX] = 0;
            OPCODE 18: // CMPSx (extra=0)|SCASx (extra=1)
                scratch2_uint = seg_override_en ? seg_override : REG_DS;
                if ((scratch_uint = rep_override_en ? regs16[REG_CX] : 1))
                {
                    for (; scratch_uint; rep_override_en || scratch_uint--)
                    {
                        MEM_OP(extra ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,), -, SEGREG(REG_ES, REG_DI,));
                        extra || INDEX_INC(REG_SI);
                        INDEX_INC(REG_DI), rep_override_en && !(--regs16[REG_CX] && (!op_result == rep_mode)) && (scratch_uint = 0);
                    }
                    set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH, set_CF(op_result > op_dest);
                }
            OPCODE 19: // RET|RETF|IRET
                i_d = i_w;
                R_M_POP(reg_ip);
                if (extra) // IRET|RETF|RETF imm16
                    R_M_POP(regs16[REG_CS]);
                if (extra & 2) // IRET
                    set_flags(R_M_POP(scratch_uint));
                else if (!i_d) // RET|RETF imm16
                    regs16[REG_SP] += i_data0;
            OPCODE 20: // MOV r/m, immed
                R_M_OP(mem[op_from_addr], =, i_data2);
            OPCODE 21: // IN AL/AX, DX/imm8
                io_ports[0x20] = 0; // PIC EOI
                io_ports[0x42] = --io_ports[0x40]; // PIT channel 0/2 read placeholder
                io_ports[0x3DA] ^= 9; // CGA refresh
                scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
                scratch_uint == 0x60 && (io_ports[0x64] = 0); // Scancode read flag
                scratch_uint == 0x3D5 && (io_ports[0x3D4] >> 1 == 7) && (io_ports[0x3D5] = ((mem[0x49E]*80 + mem[0x49D] + CAST(short)mem[0x4AD]) & (io_ports[0x3D4] & 1 ? 0xFF : 0xFF00)) >> (io_ports[0x3D4] & 1 ? 0 : 8));
                R_M_OP(regs8[REG_AL], =, io_ports[scratch_uint]);
            OPCODE 22: // OUT DX/imm8, AL/AX
                scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
                R_M_OP(io_ports[scratch_uint], =, regs8[REG_AL]);
                scratch_uint == 0x61 && (io_hi_lo = 0, spkr_en |= regs8[REG_AL] & 3);
                (scratch_uint == 0x40 || scratch_uint == 0x42) && (io_ports[0x43] & 6) && (mem[0x469 + scratch_uint - (io_hi_lo ^= 1)] = regs8[REG_AL]);
#ifndef NO_GRAPHICS
                scratch_uint == 0x43 && (io_hi_lo = 0, regs8[REG_AL] >> 6 == 2) && (SDL_PauseAudio((regs8[REG_AL] & 0xF7) != 0xB6), 0);
#endif
                scratch_uint == 0x3D5 && (io_ports[0x3D4] >> 1 == 6) && (mem[0x4AD + !(io_ports[0x3D4] & 1)] = regs8[REG_AL]);
                scratch_uint == 0x3D5 && (io_ports[0x3D4] >> 1 == 7) && (scratch2_uint = ((mem[0x49E]*80 + mem[0x49D] + CAST(short)mem[0x4AD]) & (io_ports[0x3D4] & 1 ? 0xFF00 : 0xFF)) + (regs8[REG_AL] << (io_ports[0x3D4] & 1 ? 0 : 8)) - CAST(short)mem[0x4AD], mem[0x49D] = scratch2_uint % 80, mem[0x49E] = scratch2_uint / 80);
                scratch_uint == 0x3B5 && io_ports[0x3B4] == 1 && (GRAPHICS_X = regs8[REG_AL] * 16);
                scratch_uint == 0x3B5 && io_ports[0x3B4] == 6 && (GRAPHICS_Y = regs8[REG_AL] * 4);
            OPCODE 23: // REPxx
                rep_override_en = 2;
                rep_mode = i_w;
                seg_override_en && seg_override_en++;
            OPCODE 25: // PUSH reg
                R_M_PUSH(regs16[extra]);
            OPCODE 26: // POP reg
                R_M_POP(regs16[extra]);
            OPCODE 27: // xS: segment overrides
                seg_override_en = 2;
                seg_override = extra;
                rep_override_en && rep_override_en++;
            OPCODE 28: // DAA/DAS
                i_w = 0;
                extra ? DAA_DAS(-=, >=, 0xFF, 0x99) : DAA_DAS(+=, <, 0xF0, 0x90);
            OPCODE 29: // AAA/AAS
                op_result = AAA_AAS(extra - 1);
            OPCODE 30: // CBW
                regs8[REG_AH] = -SIGN_OF(regs8[REG_AL]);
            OPCODE 31: // CWD
                regs16[REG_DX] = -SIGN_OF(regs16[REG_AX]);
            OPCODE 32: // CALL FAR imm16:imm16
                R_M_PUSH(regs16[REG_CS]);
                R_M_PUSH(reg_ip + 5);
                regs16[REG_CS] = i_data2;
                reg_ip = i_data0;
            OPCODE 33: // PUSHF
                make_flags();
                R_M_PUSH(scratch_uint);
            OPCODE 34: // POPF
                set_flags(R_M_POP(scratch_uint));
            OPCODE 35: // SAHF
                make_flags();
                set_flags((scratch_uint & 0xFF00) + regs8[REG_AH]);
            OPCODE 36: // LAHF
                make_flags(),
                regs8[REG_AH] = scratch_uint;
            OPCODE 37: // LES|LDS reg, r/m
                i_w = i_d = 1;
                DECODE_RM_REG;
                OP(=);
                MEM_OP(REGS_BASE + extra, =, rm_addr + 2);
            OPCODE 38: // INT 3
                ++reg_ip;
                pc_interrupt(3);
            OPCODE 39: // INT imm8
                reg_ip += 2;
                pc_interrupt(i_data0);
            OPCODE 40: // INTO
                ++reg_ip;
                regs8[FLAG_OF] && pc_interrupt(4);
            OPCODE 41: // AAM
                if (i_data0 &= 0xFF)
                    regs8[REG_AH] = regs8[REG_AL] / i_data0, op_result = regs8[REG_AL] %= i_data0;
                else // Divide by zero
                    pc_interrupt(0);
            OPCODE 42: // AAD
                i_w = 0;
                regs16[REG_AX] = op_result = 0xFF & regs8[REG_AL] + i_data0 * regs8[REG_AH];
            OPCODE 43: // SALC
                regs8[REG_AL] = -regs8[FLAG_CF];
            OPCODE 44: // XLAT
                regs8[REG_AL] = mem[SEGREG(seg_override_en ? seg_override : REG_DS, REG_BX, regs8[REG_AL] +)];
            OPCODE 45: // CMC
                regs8[FLAG_CF] ^= 1;
            OPCODE 46: // CLC|STC|CLI|STI|CLD|STD
                regs8[extra / 2] = extra & 1;
            OPCODE 47: // TEST AL/AX, immed
                R_M_OP(regs8[REG_AL], &, i_data0);
            OPCODE 48: // Emulator-specific 0F xx opcodes
                switch ((char)i_data0)
                {
                    OPCODE_CHAIN 0: // PUTCHAR_AL
                        write(1, regs8, 1);
                    OPCODE 1: // GET_RTC
                        time(&clock_buf);
                        ftime(&ms_clock);
                        memcpy(mem + SEGREG(REG_ES, REG_BX,), localtime(&clock_buf), sizeof(struct tm));
                        CAST(short)mem[SEGREG(REG_ES, REG_BX, 36+)] = ms_clock.millitm;
                    OPCODE 2: // DISK_READ
                    OPCODE_CHAIN 3: // DISK_WRITE
                        regs8[REG_AL] = ~lseek(disk[regs8[REG_DL]], CAST(unsigned)regs16[REG_BP] << 9, 0)
                            ? ((char)i_data0 == 3 ? (int(*)())write : (int(*)())read)(disk[regs8[REG_DL]], mem + SEGREG(REG_ES, REG_BX,), regs16[REG_AX])
                            : 0;
                }
        }

        // Increment instruction pointer by computed instruction length. Tables in the BIOS binary
        // help us here.
        reg_ip += (i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6))*i_mod_size + bios_table_lookup[TABLE_BASE_INST_SIZE][raw_opcode_id] + bios_table_lookup[TABLE_I_W_SIZE][raw_opcode_id]*(i_w + 1);

        // If instruction needs to update SF, ZF and PF, set them as appropriate
        if (set_flags_type & FLAGS_UPDATE_SZP)
        {
            regs8[FLAG_SF] = SIGN_OF(op_result);
            regs8[FLAG_ZF] = !op_result;
            regs8[FLAG_PF] = bios_table_lookup[TABLE_PARITY_FLAG][(unsigned char)op_result];

            // If instruction is an arithmetic or logic operation, also set AF/OF/CF as appropriate.
            if (set_flags_type & FLAGS_UPDATE_AO_ARITH)
                set_AF_OF_arith();
            if (set_flags_type & FLAGS_UPDATE_OC_LOGIC)
                set_CF(0), set_OF(0);
        }

        // Poll timer/keyboard every KEYBOARD_TIMER_UPDATE_DELAY instructions
        if (!(++inst_counter % KEYBOARD_TIMER_UPDATE_DELAY))
            int8_asap = 1;

#ifndef NO_GRAPHICS
        // Update the video graphics display every GRAPHICS_UPDATE_DELAY instructions
        if (!(inst_counter % GRAPHICS_UPDATE_DELAY))
        {
            // Video card in graphics mode?
            if (io_ports[0x3B8] & 2)
            {
                // If we don't already have an SDL window open, set it up and compute color and video memory translation tables
                if (!sdl_screen)
                {
                    for (int i = 0; i < 16; i++)
                        pixel_colors[i] = mem[0x4AC] ? // CGA?
                            cga_colors[(i & 12) >> 2] + (cga_colors[i & 3] << 16) // CGA -> RGB332
                            : 0xFF*(((i & 1) << 24) + ((i & 2) << 15) + ((i & 4) << 6) + ((i & 8) >> 3)); // Hercules -> RGB332

                    for (int i = 0; i < GRAPHICS_X * GRAPHICS_Y / 4; i++)
                        vid_addr_lookup[i] = i / GRAPHICS_X * (GRAPHICS_X / 8) + (i / 2) % (GRAPHICS_X / 8) + 0x2000*(mem[0x4AC] ? (2 * i / GRAPHICS_X) % 2 : (4 * i / GRAPHICS_X) % 4);

                    SDL_Init(SDL_INIT_VIDEO);
                    sdl_screen = SDL_SetVideoMode(GRAPHICS_X, GRAPHICS_Y, 8, 0);
                    SDL_EnableUNICODE(1);
                    SDL_EnableKeyRepeat(500, 30);
                }

                // Refresh SDL display from emulated graphics card video RAM
                vid_mem_base = mem + 0xB0000 + 0x8000*(mem[0x4AC] ? 1 : io_ports[0x3B8] >> 7);
                for (int i = 0; i < GRAPHICS_X * GRAPHICS_Y / 4; i++)
                    ((unsigned *)sdl_screen->pixels)[i] = pixel_colors[15 & (vid_mem_base[vid_addr_lookup[i]] >> 4*!(i & 1))];

                SDL_Flip(sdl_screen);
            }
            else if (sdl_screen) // Application has gone back to text mode, so close the SDL window
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                sdl_screen = 0;
            }
            SDL_PumpEvents();
        }
#endif

        // Application has set trap flag, so fire INT 1
        if (trap_flag)
            pc_interrupt(1);

        trap_flag = regs8[FLAG_TF];

        // If a timer tick is pending, interrupts are enabled, and no overrides/REP are active,
        // then process the tick and check for new keystrokes
        if (int8_asap && !seg_override_en && !rep_override_en && regs8[FLAG_IF] && !regs8[FLAG_TF])
            pc_interrupt(0xA), int8_asap = 0, SDL_KEYBOARD_DRIVER;
        
        instructions_executed++;
    }
    
    LOGI2("emulator_step: executed %d instructions (completed %d)", instructions_executed, max_instructions);
}

// JNI Bridge Functions
#ifdef ANDROID

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeInit8086(JNIEnv* env, jobject thiz, jstring jcurdir, jstring jcmdline) {
    LOGI2("nativeInit8086 called");
    g_jni_env = env;
    g_emulator_view = (*env)->NewGlobalRef(env, thiz);
    
    const char *curdir = (*env)->GetStringUTFChars(env, jcurdir, NULL);
    const char *cmdline = (*env)->GetStringUTFChars(env, jcmdline, NULL);
    
    if (curdir) {
        chdir(curdir);
        LOGI2("Changed directory to: %s", curdir);
    }
    
    // Parse cmdline into argc/argv
    int argc = 1;
    char *argv[10];
    argv[0] = "8086tiny";
    
    if (cmdline && strlen(cmdline) > 0) {
        char *cmd_copy = strdup(cmdline);
        char *token = strtok(cmd_copy, " ");
        while (token && argc < 10) {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }
    }
    
    LOGI2("Calling emulator_init with argc=%d", argc);
    for (int i = 0; i < argc; i++) {
        LOGI2("argv[%d] = '%s'", i, argv[i]);
    }
    
    emulator_init(argc, argv);
    
    if (curdir) (*env)->ReleaseStringUTFChars(env, jcurdir, curdir);
    if (cmdline) (*env)->ReleaseStringUTFChars(env, jcmdline, cmdline);
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeStepFrame8086(JNIEnv* env, jobject thiz) {
    emulator_step(10000);
    render_text_mode();
}

JNIEXPORT jintArray JNICALL
Java_com_eight086tiny_MainActivity_nativeGetFramebuffer8086(JNIEnv* env, jobject thiz) {
    jintArray result = (*env)->NewIntArray(env, text_fb_width * text_fb_height);
    if (result) {
        (*env)->SetIntArrayRegion(env, result, 0, text_fb_width * text_fb_height, (jint*)text_framebuffer);
    }
    return result;
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeGetScreenSize8086(JNIEnv* env, jobject thiz, jintArray size) {
    jint sz[2] = {text_fb_width, text_fb_height};
    (*env)->SetIntArrayRegion(env, size, 0, 2, sz);
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeSetEmulatorView(JNIEnv* env, jobject thiz, jobject view) {
    if (g_emulator_view) {
        (*env)->DeleteGlobalRef(env, g_emulator_view);
    }
    g_emulator_view = (*env)->NewGlobalRef(env, view);
    LOGI2("nativeSetEmulatorView called");
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeInjectKey(JNIEnv* env, jobject thiz, jint keyCode, jint down, jint unicode) {
    if (down) {
        mem[0x4A6] = (unsigned char)(unicode & 0xFF);
        pc_interrupt(7);
    }
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeKey(JNIEnv* env, jobject thiz, jint keyCode, jint down, jint unicode, jint gamepadId) {
    if (down) {
        mem[0x4A6] = (unsigned char)(unicode & 0xFF);
        pc_interrupt(7);
    }
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeReset8086(JNIEnv* env, jobject thiz) {
    LOGI2("nativeReset8086 called - triggering keyboard controller reset");
    io_ports[0x64] = 0xFE;
    reset_requested = 1;
    mem[0x4A6] = 0; // Clear keyboard buffer
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeTestFont8086(JNIEnv* env, jobject thiz) {
    LOGI2("nativeTestFont8086: testing font rendering");
    render_text_mode();
}

#endif

// Emulator entry point (desktop builds only)
#ifndef NO_MAIN
int main(int argc, char **argv)
{
    LOGI2("main: using emulator_init + emulator_step loop");
    
    // Use the new frame-based initialization and execution functions
    emulator_init(argc, argv);
    
    // Desktop builds: run in a continuous loop
    while (1) {
        emulator_step(10000);
    }
    
    return 0;
}
#endif
