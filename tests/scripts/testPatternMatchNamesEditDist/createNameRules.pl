#!/usr/bin/perl

use strict;
use warnings;
use 5.010;

# cat names.txt | sed -E 's/^/\//' | sed -E 's/$/\//' | sed -E 's/ /[ ]+/' | sed -E 's/^/NAME_3 ^1 :/' | sed -E 's/$/ \~3\;/'
sub getRules
{
	my ($line) = @_;
	my $name = '';
	my $ch;
	foreach $ch( split('', $line))
	{
		if ($ch =~ m/[a-zA-Z]/)
		{
			$name .= $ch;
		}
		elsif ($ch eq ' ')
		{
			$name .= '_';
		}
		else
		{
			$name .= printf( "_%02x", ord( $ch)); 
		}
	}
	print( "$name"  . "__0 ^4 : /$line/;\n");
	print( "$name"  . "__1 ^3 : /$line/ ~1;\n");
	print( "$name"  . "__2 ^2 : /$line/ ~2;\n");
	print( "$name"  . "__3 ^1 : /$line/ ~3;\n");
}

while (<>)
{
	chomp;
	getRules( $_);
}



