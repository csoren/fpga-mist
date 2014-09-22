//
// sdram.v
//
// sdram controller implementation for the MiST board adaptation
// of Luddes NES core
// http://code.google.com/p/mist-board/
// 
// Copyright (c) 2013 Till Harbaum <till@harbaum.org> 
// 
// This source file is free software: you can redistribute it and/or modify 
// it under the terms of the GNU General Public License as published 
// by the Free Software Foundation, either version 3 of the License, or 
// (at your option) any later version. 
// 
// This source file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of 
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License 
// along with this program.  If not, see <http://www.gnu.org/licenses/>. 
//

module sdram (

	// interface to the MT48LC16M16 chip
	inout [15:0]  		sd_data,    // 16 bit bidirectional data bus
	output [12:0]		sd_addr,    // 13 bit multiplexed address bus
	output [1:0] 		sd_dqm,     // two byte masks
	output [1:0] 		sd_ba,      // two banks
	output 				sd_cs,      // a single chip select
	output 				sd_we,      // write enable
	output 				sd_ras,     // row address select
	output 				sd_cas,     // columns address select

	// cpu/chipset interface
	input 		 		init,			// init signal after FPGA config to initialize RAM
	input 		 		clk,			// sdram is accessed at up to 128MHz
	input					clkref,		// reference clock to sync to
	
	input [24:0]   	addr,       // 25 bit byte address
	input 		 		we,         // cpu/chipset requests write
	input [7:0]  		din,			// data input from chipset/cpu
	input 		 		oeA,        // cpu requests data
	output reg [7:0]  doutA,	   // data output to cpu
	input 		 		oeB,        // ppu requests data
	output reg [7:0]  doutB 	   // data output to ppu
);

// no burst configured
localparam RASCAS_DELAY   = 3'd3;   // tRCD=20ns -> 3 cycles@128MHz
localparam BURST_LENGTH   = 3'b000; // 000=1, 001=2, 010=4, 011=8
localparam ACCESS_TYPE    = 1'b0;   // 0=sequential, 1=interleaved
localparam CAS_LATENCY    = 3'd3;   // 2/3 allowed
localparam OP_MODE        = 2'b00;  // only 00 (standard operation) allowed
localparam NO_WRITE_BURST = 1'b1;   // 0= write burst enabled, 1=only single access write

localparam MODE = { 3'b000, NO_WRITE_BURST, OP_MODE, CAS_LATENCY, ACCESS_TYPE, BURST_LENGTH}; 


// ---------------------------------------------------------------------
// ------------------------ cycle state machine ------------------------
// ---------------------------------------------------------------------

localparam STATE_FIRST     = 4'd0;   // first state in cycle
localparam STATE_CMD_START = 4'd1;   // state in which a new command can be started
localparam STATE_CMD_CONT  = STATE_CMD_START  + RASCAS_DELAY; // 4 command can be continued
localparam STATE_CMD_READ  = 4'd7;   // read state
localparam STATE_LAST      = 4'd15;  // last state in cycle

reg [3:0] q;
always @(posedge clk) begin
	// SDRAM (state machine) clock is 86MHz. Synchronize this to systems 21.477 Mhz clock
   // force counter to pass state LAST->FIRST exactly after the rising edge of clkref
   if(((q == STATE_LAST) && ( clkref == 1)) ||
		((q == STATE_FIRST) && ( clkref == 0)) ||
      ((q != STATE_LAST) && (q != STATE_FIRST)))
			q <= q + 3'd1;
end

// ---------------------------------------------------------------------
// --------------------------- startup/reset ---------------------------
// ---------------------------------------------------------------------

// wait 1ms (32 8Mhz cycles) after FPGA config is done before going
// into normal operation. Initialize the ram in the last 16 reset cycles (cycles 15-0)
reg [4:0] reset;
always @(posedge clk) begin
	if(init)	reset <= 5'h1f;
	else if((q == STATE_LAST) && (reset != 0))
		reset <= reset - 5'd1;
end

// ---------------------------------------------------------------------
// ------------------ generate ram control signals ---------------------
// ---------------------------------------------------------------------

// all possible commands
localparam CMD_INHIBIT         = 4'b1111;
localparam CMD_NOP             = 4'b0111;
localparam CMD_ACTIVE          = 4'b0011;
localparam CMD_READ            = 4'b0101;
localparam CMD_WRITE           = 4'b0100;
localparam CMD_BURST_TERMINATE = 4'b0110;
localparam CMD_PRECHARGE       = 4'b0010;
localparam CMD_AUTO_REFRESH    = 4'b0001;
localparam CMD_LOAD_MODE       = 4'b0000;

wire [3:0] sd_cmd;   // current command sent to sd ram

// drive control signals according to current command
assign sd_cs  = sd_cmd[3];
assign sd_ras = sd_cmd[2];
assign sd_cas = sd_cmd[1];
assign sd_we  = sd_cmd[0];

// drive ram data lines when writing, set them as inputs otherwise
// the eight bits are sent on both bytes ports. Which one's actually
// written depends on the state of dqm of which only one is active
// at a time when writing
assign sd_data = we?{din, din}:16'bZZZZZZZZZZZZZZZZ;

wire oe = oeA || oeB;

reg addr0;
always @(posedge clk)
	if((q == 1) && oe) addr0 <= addr[0];

wire [7:0] dout = addr0?sd_data[7:0]:sd_data[15:8];

always @(posedge clk) begin
	if(q == STATE_CMD_READ) begin
		if(oeA) doutA <= dout;
		if(oeB) doutB <= dout;
	end
end

wire [3:0] reset_cmd = 
	((q == STATE_CMD_START) && (reset == 13))?CMD_PRECHARGE:
	((q == STATE_CMD_START) && (reset ==  2))?CMD_LOAD_MODE:
	CMD_INHIBIT;

wire [3:0] run_cmd =
	((we || oe) && (q == STATE_CMD_START))?CMD_ACTIVE:
	(we && 			(q == STATE_CMD_CONT ))?CMD_WRITE:
	(!we &&  oe &&	(q == STATE_CMD_CONT ))?CMD_READ:
	(!we && !oe && (q == STATE_CMD_START))?CMD_AUTO_REFRESH:
	CMD_INHIBIT;
	
assign sd_cmd = (reset != 0)?reset_cmd:run_cmd;

wire [12:0] reset_addr = (reset == 13)?13'b0010000000000:MODE;
	
wire [12:0] run_addr = 
	(q == STATE_CMD_START)?addr[21:9]:{ 4'b0010, addr[24], addr[8:1]};

assign sd_addr = (reset != 0)?reset_addr:run_addr;

assign sd_ba = addr[23:22];

assign sd_dqm = we?{ addr[0], ~addr[0] }:2'b00;

endmodule
