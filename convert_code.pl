#!/usr/bin/perl

# convert code.pl to code.h

use FindBin qw($Bin);
my $src = "$Bin/code.pl";
my $file = "$Bin/code.h";

open(S, "<", $src) || die "$!";
open(F, ">", $file) || die "$!";

print F (<<TOP);
#if !defined PERLCODEH
#define PERLCODEH
#define PerlLoaderCode { \\
TOP
while ( my $line = <S> ) {
    $line =~s/^/\    \"/;
    $line =~s/$/\"\t\\/;
    print F $line;
}
print F "}\n#endif\n";
close(S);
close(F);
exit(0);
