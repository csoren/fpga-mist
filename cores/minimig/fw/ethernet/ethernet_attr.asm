CISTPL_VERS_1	EQU	$15
CISTPL_FUNC_ID	EQU	$21
CISTPL_END		EQU	$FF

CISTPL_FUNCID_NETWORK	EQU	$06

	dc.b	CISTPL_VERS_1
	dc.b	.vers_1_end-.vers_1_data 
.vers_1_data
	dc.b	1, 0
	dc.b	"MiST", 0
	dc.b	"Ethernet", 0
	dc.b	0
	dc.b	0
.vers_1_end
	
	dc.b	CISTPL_FUNC_ID
	dc.b	2
	dc.b	CISTPL_FUNCID_NETWORK, 0
	
	dc.b	CISTPL_END

