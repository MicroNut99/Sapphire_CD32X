// hub.sv - krikzz's (mega-ed-pub 7064f45, MIT: map_sapphire.sv has the notice), the SSF map made map_sapphire


`include "../lib_base/defs.sv"


module map_hub(

	input  MapIn mai,
	output MapOut mout
);
	
	
	assign mout = 
	mai.cfg.map_idx == `MAP_SSF ? mout_ssf : 
										   mout_nom;
	
	
	MapOut mout_nom;
	map_nom nom_inst(mai, mout_nom);
	
	MapOut mout_ssf;
	map_sapphire ssf_inst(mai, mout_ssf);	// Sapphire: the SSF mapper with the Arcade Card engine
	


endmodule
