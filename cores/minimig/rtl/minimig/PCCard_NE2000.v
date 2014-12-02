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
	input	cc_ce1,	// enable d[7:0]
	input	cc_ce2,	// enable d[15:8]
	
	output  cc_ireq
);

wire [7:0] attribute_data;

ethernet_attr AttributeMemory(
	.clock(clk),
	.address(addr[8:1]),
	.q(attribute_data)
);

wire attr_read  = cc_reg && cc_oe;
wire attr_write = cc_reg && cc_we;

wire io_read  = cc_reg && cc_iord;
wire io_write = cc_reg && cc_iowr;

assign cc_ireq = 1'b0;

wire word_access = cc_ce1 && cc_ce2;

wire byte_access_odd = addr[0] && cc_ce1 && !cc_ce2;

wire byte_access_even = (!addr[0] && cc_ce1 && !cc_ce2) || word_access;

// Configuration register @ $1000 in attribute memory
wire sel_enabled = addr[12];

reg enabled = 0;

always @(posedge clk)
	if (attr_write && sel_enabled && byte_access_even)
		enabled <= data_in[0];

wire [4:0] io_register = { addr[4:1], byte_access_odd };

assign data_out =
	(reset)                        ? 16'd0 :
	(attr_read && sel_enabled)     ? enabled :
	(attr_read && addr[16:8] == 0) ? attribute_data :
	(io_read && enabled)           ? io_register :
	16'd0;
		
endmodule
