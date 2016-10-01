package EplScriptLoader;

$epl_load_file_error = qq();
sub epl_load_file {
    my $file = shift;
    local $/ = undef;
    open(FILE, '<', $file) || return;
    $_ = <FILE>;
    close(FILE);
    return $_;
}

sub epl_load_eval_file {
    my $file = shift;
    my $content = epl_load_file( $file );
    return 0 if (!defined $content);
    my $eval = qq{ $content;};
    { eval $eval; }
    if ( $@ ) {
	$epl_load_file_error = $@;
	return -1;
    }
    return 1;
}

$SIG{__WARN__} = sub { EPL::log( qq(Sig warn: $_[0]\n) )};
$SIG{__DIE__}  = sub { EPL::log( qq(Sig die: $_[0]\n)  )};


1;
