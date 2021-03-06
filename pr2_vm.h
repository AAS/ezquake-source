/*
 *  QW262
 *  Copyright (C) 2004  [sd] angel
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  $Id: pr2_vm.h 696 2007-09-30 14:53:40Z disconn3ct $
 */

#ifndef __PR2_VM_H__
#define __PR2_VM_H__

#define SAFE_QVM
#define QVM_RUNAWAY_PROTECTION
#define QVM_DATA_PROTECTION
#define QVM_PROFILE

#ifdef _WIN32
#define EXPORT_FN __cdecl
#else
#define EXPORT_FN
#endif

#define OPSTACKSIZE		0x100
#define MAX_PROC_CALL	100
#define MAX_vmMain_Call	100
#define MAX_CYCLES		100000

// for syscall users
#define VM_LONG(x)		(*(int*)&(x))	//note: on 64bit platforms, the later bits can contain junk
#define VM_FLOAT(x)		(*(float*)&(x))	//note: on 64bit platforms, the later bits can contain junk
#define VM_POINTERQ(x)	((x)?(void*)((char *)offset+((x)%mask)):NULL)
#define VM_OOB(p,l)		((p) + (l) >= mask || VM_POINTERQ(p) < offset)

#define VM_POINTER(base,mask,x)	 ((void*)((char *)base+((x)&mask)))
#define POINTER_TO_VM(base,mask,x)	 ((x)?(int)((char *)(x) - (char*)base)&mask:0)

// <qintptr_t>
#if defined(_WIN64)
	#define qintptr_t __int64
	#define FTE_WORDSIZE 64
#elif defined(_WIN32)
	#define qintptr_t __int32
	#define FTE_WORDSIZE 32
#else
	#if __WORDSIZE == 64
		#define qintptr_t long long
		#define FTE_WORDSIZE 64
	#else
		#define qintptr_t long
		#define FTE_WORDSIZE 32
	#endif
#endif
#define quintptr_t unsigned qintptr_t
// </qintptr_t>

typedef union pr2val_s
{
	string_t	string;
	float		_float;
	int			_int;
	int			edict;
} pr2val_t;	

typedef int (EXPORT_FN *sys_call_t) (int arg, ...);
typedef int (*sys_callex_t) (byte *data, unsigned int mask, int fn,  pr2val_t* arg);

typedef enum vm_type_e
{
	VM_NONE,
	VM_NATIVE,
	VM_BYTECODE
} vm_type_t;


typedef struct vm_s {
	// common
	vm_type_t type;
	char name[MAX_QPATH];
	// shared
	void *hInst;
	sys_call_t syscall;

	// native
	int (*vmMain) (int command, int arg0, int arg1, int arg2, int arg3,
		int arg4, int arg5, int arg6, int arg7, int arg8, int arg9,
		int arg10, int arg11);
} vm_t ;

typedef enum
{
	OP_UNDEF, 

	OP_IGNORE, 

	OP_BREAK, 

	OP_ENTER,
	OP_LEAVE,
	OP_CALL,
	OP_PUSH,
	OP_POP,

	OP_CONST,
	OP_LOCAL,

	OP_JUMP,

	//-------------------

	OP_EQ,
	OP_NE,

	OP_LTI,
	OP_LEI,
	OP_GTI,
	OP_GEI,

	OP_LTU,
	OP_LEU,
	OP_GTU,
	OP_GEU,

	OP_EQF,
	OP_NEF,

	OP_LTF,
	OP_LEF,
	OP_GTF,
	OP_GEF,

	//-------------------

	OP_LOAD1,
	OP_LOAD2,
	OP_LOAD4,
	OP_STORE1,
	OP_STORE2,
	OP_STORE4,				// *(stack[top-1]) = stack[yop
	OP_ARG,
	OP_BLOCK_COPY,

	//-------------------

	OP_SEX8,
	OP_SEX16,

	OP_NEGI,
	OP_ADD,
	OP_SUB,
	OP_DIVI,
	OP_DIVU,
	OP_MODI,
	OP_MODU,
	OP_MULI,
	OP_MULU,

	OP_BAND,
	OP_BOR,
	OP_BXOR,
	OP_BCOM,

	OP_LSH,
	OP_RSHI,
	OP_RSHU,

	OP_NEGF,
	OP_ADDF,
	OP_SUBF,
	OP_DIVF,
	OP_MULF,

	OP_CVIF,
	OP_CVFI
} opcode_t;

typedef union {
	int				_int;
	unsigned int	_uint;
	float			_float;
} qvm_parm_type_t;


typedef struct {
	opcode_t		opcode;
	qvm_parm_type_t	parm;
} qvm_instruction_t;

typedef struct symbols_s
{
	int off;
	int seg;
	struct symbols_s *next;
	char name[1];
}symbols_t;

typedef struct {
	// segments
	qvm_instruction_t *cs;
	unsigned char *ds;	// DATASEG + LITSEG + BSSSEG
	unsigned char *ss;	// q3asm add stack at end of BSSSEG, defaultsize = 0x10000

	// pointer registers
	int	PC;			// program counter, points to cs, goes up
	int	SP;			// operation stack pointer, initially 0, goes up index of opStack in QVM_Exec
	int	LP;			// subroutine stack/local vars space, initially points to end of ss

	// status
	int	len_cs;		// size of cs
	int	len_ds;		// size of ds align up to power of 2
	int	ds_mask;	// bit mask of len_ds
	int	len_ss;		// size of ss

	int	cycles;		// command cicles executed
	int	reenter;
	symbols_t* sym_info;
	sys_callex_t syscall;
} qvm_t;


/*
========================================================================

QVM files

========================================================================
*/

#define	VM_MAGIC	0x12721444
typedef struct
{
	int		vmMagic;

	int		instructionCount;

	int		codeOffset;
	int		codeLength;

	int		dataOffset;
	int		dataLength;
	int		litLength;			// ( dataLength - litLength ) should be byteswapped on load
	int		bssLength;			// zero filled memory appended to datalength
} vmHeader_t;

typedef enum
{
	CODESEG,
	DATASEG,	// initialized 32 bit data, will be byte swapped
	LITSEG,		// strings
	BSSSEG,		// 0 filled
	NUM_SEGMENTS
} segmentName_t;


extern char* opcode_names[];
extern void VM_Unload(vm_t *vm);
vm_t* VM_Load(vm_t *vm, vm_type_t type, char *name,sys_call_t syscall,sys_callex_t syscallex);
extern int VM_Call(vm_t *vm, int /*command*/, int /*arg0*/, int , int , int , int , int , 
				int , int , int , int , int , int /*arg11*/);
void  QVM_StackTrace( qvm_t * qvm );
void VM_PrintInfo( vm_t * vm);

#endif /* !__PR2_VM_H__ */
