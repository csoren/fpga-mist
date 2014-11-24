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
	input	cc_ce1,
	input	cc_ce2,
	
	output  cc_ireq
);

wire [7:0] attribute_data;

ethernet_attr AttributeMemory(
	.clock(clk),
	.address(addr[8:1]),
	.q(attribute_data)
);

assign data_out =
	(reset)           ? 16'd0 :
	(cc_reg && cc_oe) ? attribute_data :
	16'd0;

assign cc_ireq = 1'b0;

endmodule
