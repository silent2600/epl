package MyTest;
use Data::Dumper;

sub new {
    my $class = shift;
    my $self = { hello => "world" };
    bless $self, $class;
    return $self;
}

sub oo_test { return "this is a " . ref $_[0] }

sub test {
    return "this is a test function";
}

sub noresult {}
sub mylog { EPL::log( @_ ) };
sub call_elisp_from_perl_test {
    my $str = EPL::elisp_exec("concat", "a", "b", "c", "d");
    return $str;
}

sub list_test {
    my ($mday, $mon, $year) = (localtime())[3,4,5];
    $mon++;    $year += 1900;
    return [$year, $mon, $mday];
}

sub hash_test {
    return {
	year => 2016, mon => 9,
	data => [1,2,3, "a","b","c" ]
    };
}

sub dumpvar {
    warn Dumper $_[0];
}

1;

