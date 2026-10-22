// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
//
// Xilinx-inferable drop-in replacements for the HPDcache behavioral SRAM models
// (`hpdcache/rtl/src/common/macros/behav/hpdcache_sram{,_wbyteenable}_1rw.sv`).
//
// The behavioral models declare a 3D array (`logic [NDATA-1:0][DATA_SIZE-1:0] mem [DEPTH]`),
// which Vivado cannot map to BRAM (Synth 8-5856) and implements in flip-flops instead. With
// HPDcache enabled this blows up the design to ~490k FFs and makes it unroutable on the ZCU208.
// Here, each of the NDATA lanes gets its own 2D array following the UG901 single-port no-change
// (byte-write) templates. Port list and read/write semantics are identical to the originals.
// The behavioral files are disabled in Vivado by `target/xilinx/scripts/impl_sys.tcl`.

module hpdcache_sram_1rw #(
  parameter int unsigned ADDR_SIZE = 0,
  parameter int unsigned DATA_SIZE = 0,
  parameter int unsigned DEPTH     = 2**ADDR_SIZE,
  parameter int unsigned NDATA     = 1
) (
  input  logic                            clk,
  input  logic                            rst_n,
  input  logic                            cs,
  input  logic                            we,
  input  logic [ADDR_SIZE-1:0]            addr,
  input  logic [NDATA-1:0][DATA_SIZE-1:0] wdata,
  output logic [NDATA-1:0][DATA_SIZE-1:0] rdata
);

  for (genvar j = 0; j < NDATA; j++) begin : gen_lane
    logic [DATA_SIZE-1:0] mem [DEPTH];
    logic [DATA_SIZE-1:0] rdata_q;

    always_ff @(posedge clk) begin
      if (cs) begin
        if (we) mem[addr] <= wdata[j];
        else    rdata_q   <= mem[addr];
      end
    end

    assign rdata[j] = rdata_q;
  end

endmodule

module hpdcache_sram_wbyteenable_1rw #(
  parameter int unsigned ADDR_SIZE = 0,
  parameter int unsigned DATA_SIZE = 0,
  parameter int unsigned DEPTH     = 2**ADDR_SIZE,
  parameter int unsigned NDATA     = 1
) (
  input  logic                              clk,
  input  logic                              rst_n,
  input  logic                              cs,
  input  logic                              we,
  input  logic [ADDR_SIZE-1:0]              addr,
  input  logic [NDATA-1:0][DATA_SIZE-1:0]   wdata,
  input  logic [NDATA-1:0][DATA_SIZE/8-1:0] wbyteenable,
  output logic [NDATA-1:0][DATA_SIZE-1:0]   rdata
);

  for (genvar j = 0; j < NDATA; j++) begin : gen_lane
    logic [DATA_SIZE-1:0] mem [DEPTH];
    logic [DATA_SIZE-1:0] rdata_q;

    always_ff @(posedge clk) begin
      if (cs) begin
        if (we) begin
          for (int i = 0; i < DATA_SIZE/8; i++) begin
            if (wbyteenable[j][i]) mem[addr][i*8 +: 8] <= wdata[j][i*8 +: 8];
          end
        end else begin
          rdata_q <= mem[addr];
        end
      end
    end

    assign rdata[j] = rdata_q;
  end

endmodule
