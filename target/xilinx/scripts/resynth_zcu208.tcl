# Re-synthesize cheshire ZCU208 after dmi_bscane_tap.sv update.
# Run from the project build directory:
#   vivado -mode batch -source ../scripts/resynth_zcu208.tcl

set project_root [file dirname [file normalize [info script]]]
set project_root [file normalize "$project_root/../build/zcu208.cheshire"]

open_project $project_root/cheshire.xpr

# Reset and re-run synthesis
reset_run synth_1
launch_runs -jobs 20 synth_1
wait_on_run synth_1
open_run synth_1

# Reset and re-run implementation through write_bitstream
reset_run impl_1
launch_runs -jobs 20 impl_1 -to_step write_bitstream
wait_on_run impl_1
open_run impl_1

# Copy outputs
set out_dir [file normalize "$project_root/../../out"]
file mkdir $out_dir
file copy -force $project_root/cheshire.runs/impl_1/cheshire_top_xilinx.bit $out_dir/cheshire.zcu208.bit
file copy -force $project_root/cheshire.runs/impl_1/cheshire_top_xilinx.ltx $out_dir/cheshire.zcu208.ltx

puts "Done. Bitstream: $out_dir/cheshire.zcu208.bit"
