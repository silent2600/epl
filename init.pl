

BEGIN {
    if ( scalar @INC < 2 ) {
	EPL::log("Note: You need to set your \@INC in init.pl");
    }
}

