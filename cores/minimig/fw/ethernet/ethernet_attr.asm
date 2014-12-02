CISTPL_DEVICE			EQU	$01
CISTPL_VERS_1			EQU	$15
CISTPL_CONFIG			EQU	$1A
CISTPL_CFTABLE_ENTRY	EQU	$1B
CISTPL_FUNC_ID			EQU	$21
CISTPL_END				EQU	$FF

CISTPL_FUNCID_NETWORK	EQU	$06

	dc.b	CISTPL_DEVICE
	dc.b	1
	dc.b	0	; DTYPE_NULL, DSPEED_NULL
	
	dc.b	CISTPL_VERS_1
	dc.b	.vers_1_end-.vers_1_data 
.vers_1_data
	dc.b	4, 1
	dc.b	"MiST", 0
	dc.b	"Ethernet", 0
	dc.b	0
	dc.b	0
	dc.b	$FF
.vers_1_end

	dc.b	CISTPL_CONFIG
	dc.b	5
	dc.b	(1<<2)!(2-1)	; 1 byte of register present map, 2 bytes of register address
	dc.b	0				; TPCC_LAST
	dc.b	$00,$10			; config register base $1000
	dc.b	1				; register 0 present

	dc.b	CISTPL_CFTABLE_ENTRY
	dc.b	.cftable_entry_end-.cftable_entry_data
.cftable_entry_data
	dc.b	$81
	dc.b	$04			;TPCE_IF
	dc.b	$08			;TPCE_FS = IO Space
	dc.b	(1<<5)!5	;TPCE_IO - 8 bit IO, 5 address lines
.cftable_entry_end
	
	dc.b	CISTPL_FUNC_ID
	dc.b	2
	dc.b	CISTPL_FUNCID_NETWORK, 0
	
	dc.b	CISTPL_END

