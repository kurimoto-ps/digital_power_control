# TEMPLATE ONLY: not timing closure. CLK50 is the proposed HDL name for U5.N5.
create_clock -name clk50 -period 20.000 [get_ports {CLK50}]
# Add PLL-generated clocks after implementing PLL IP.
# ACC_12MHZ (U5.M9), ACC_96KHZ (U5.A7), SPI2/3/4 SCLK:
# Add clocks only according to the actual HDL capture architecture and confirmed frequency.
# Add min/max input/output delays from external device timing + PCB skew.
# Review CDC crossings and reset release. Do not blanket false-path external buses.
