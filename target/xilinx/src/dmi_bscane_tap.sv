// Copyright 2026 Francesco Corvaglia, Politecnico di Torino (SMILIES group)
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Drop-in replacement for dmi_jtag_tap.sv using two BSCANE2 chains.
// Chain 3 (USER3 = 0x922): DTMCS register access.
// Chain 4 (USER4 = 0x923): DMI register access.
//
// OpenOCD config (no use_bscan_tunnel):
//   riscv set_ir dtmcs 0x922   <- USER3 selects chain 3  (xczu9eg 12-bit PL TAP IR)
//   riscv set_ir dmi   0x923   <- USER4 selects chain 4
//
// Design notes:
// - tck_o comes from chain3.TCK only (raw JTAG TCK, clean clock path — no LUT
//   OR that would confuse Vivado's clock routing and cause jitter).
// - CAPTURE/SHIFT/UPDATE are 0 when SEL=0, so OR is a safe combinational mux.
// - TDI: inactive BSCANE2 outputs 1, so AND passes the active chain's TDI.
// - TDO is driven combinatorially (no register). BSCANE2 samples TDO at the
//   CAPTURE-DR rising edge and each SHIFT-DR rising edge. dmi_jtag.sv's shift
//   registers are also posedge-clocked; at any given rising edge, the BSCANE2
//   sees the pre-shift value of dtmcs_q[0] (FF output hasn't changed yet),
//   which is the correct bit to shift out. No negedge register is needed.

module dmi_jtag_tap #(
  parameter int unsigned IrLength = 5,
  parameter logic [31:0] IdcodeValue = 32'h00000001
) (
  // Unused — maintained for pin compatibility with dmi_jtag_tap.sv.
  input  logic        tck_i,
  input  logic        tms_i,
  input  logic        trst_ni,
  input  logic        td_i,
  output logic        td_o,
  output logic        tdo_oe_o,
  input  logic        testmode_i,

  output logic        tck_o,
  output logic        dmi_clear_o,
  output logic        update_o,
  output logic        capture_o,
  output logic        shift_o,
  output logic        tdi_o,
  output logic        dtmcs_select_o,
  input  logic        dtmcs_tdo_i,
  output logic        dmi_select_o,
  input  logic        dmi_tdo_i
);

  // ----------------------------------------------------------
  // Chain 3 (USER3 = 0x922): DTMCS
  // ----------------------------------------------------------
  logic capture3, shift3, update3, tck3, tdi3;

  BSCANE2 #(
    .JTAG_CHAIN (3)
  ) i_tap_dtmcs (
    .CAPTURE (capture3),
    .DRCK    (),
    .RESET   (dmi_clear_o),
    .RUNTEST (),
    .SEL     (dtmcs_select_o),
    .SHIFT   (shift3),
    .TCK     (tck3),
    .TDI     (tdi3),
    .TMS     (),
    .TDO     (dtmcs_tdo_i),   // combinatorial — correct at every posedge TCK
    .UPDATE  (update3)
  );

  // ----------------------------------------------------------
  // Chain 4 (USER4 = 0x923): DMI
  // ----------------------------------------------------------
  logic capture4, shift4, update4, tck4, tdi4;

  BSCANE2 #(
    .JTAG_CHAIN (4)
  ) i_tap_dmi (
    .CAPTURE (capture4),
    .DRCK    (),
    .RESET   (),
    .RUNTEST (),
    .SEL     (dmi_select_o),
    .SHIFT   (shift4),
    .TCK     (tck4),
    .TDI     (tdi4),
    .TMS     (),
    .TDO     (dmi_tdo_i),     // combinatorial
    .UPDATE  (update4)
  );

  // ----------------------------------------------------------
  // Clock: use tck3 only — raw JTAG TCK, single clean source.
  // tck3 == tck4 == raw TCK; no OR needed, avoids LUT on clock.
  // ----------------------------------------------------------
  assign tck_o = tck3;

  // ----------------------------------------------------------
  // Control signals: CAPTURE/SHIFT/UPDATE are 0 when SEL=0.
  // OR mux is safe since the two chains are mutually exclusive.
  // ----------------------------------------------------------
  assign capture_o = capture3 | capture4;
  assign shift_o   = shift3   | shift4;
  assign update_o  = update3  | update4;

  // ----------------------------------------------------------
  // TDI: inactive BSCANE2 drives 1 on TDI output.
  // AND passes the active chain's outer TDI value.
  // ----------------------------------------------------------
  assign tdi_o = tdi3 & tdi4;

  // ----------------------------------------------------------
  // td_o: unused externally (BSCANE2 handles TDO internally).
  // Drive to 0 to avoid unresolved output in dmi_jtag.sv.
  // ----------------------------------------------------------
  assign td_o     = 1'b0;
  assign tdo_oe_o = dtmcs_select_o | dmi_select_o;

endmodule
