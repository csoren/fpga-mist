module pccard_ne2000(
	input	clk,
	input	reset,
	
	input  [25:0]	addr,
	input  [15:0]	data_in,
	output [15:0]	data_out,
	
	input	cc_reg,
	input	cc_iord,
	input	cc_iowr,
	input	cc_oe,
	input	cc_we,
	input	cc_ena,
	output	cc_ireq
);


endmodule
