#pragma once

#define set_ddr_bit(p, b)			if (b) *ddr_##p |= mask_##p; else *ddr_##p &= ~mask_##p
#define set_port_bit(p, b)			if (b) *port_##p |= mask_##p; else *port_##p &= ~mask_##p
#define get_port_bit(p)				(*port_##p & mask_##p)
#define toggle_port_bit(p)			*port_##p ^= mask_##p
#define clear_port_bit(p)			*port_##p &= ~mask_##p
