#!/usr/bin/perl

use strict;
use warnings;
use 5.010;

my $generate_word_patterns = 0;

# cat names.txt | sed -E 's/^/\//' | sed -E 's/$/\//' | sed -E 's/ /[ ]+/' | sed -E 's/^/NAME_3 ^1 :/' | sed -E 's/$/ \~3\;/'
sub getRules
{
	my ($line) = @_;
	$line =~ s/^\s+//;	# ltrim
	$line =~ s/\s+$//;	# rtrim
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
	if ($generate_word_patterns)
	{
		print( "_$name"  . "__0 ^4 : /$line/;\n");
		print( "$name" . "__0 = _$name" . "__0;\n");
		print( "_$name" . "__1 ^3 : /$line/ ~1;\n");
		print( "$name" . "__1 = _$name" . "__1;\n");
		print( "_$name" . "__2 ^2 : /$line/ ~2;\n");
		print( "$name" . "__2 = _$name" . "__2;\n");
		print( "_$name" . "__3 ^1 : /$line/ ~3;\n");
		print( "$name" . "__3 = _$name" . "__3;\n");
	}
	else
	{
		print( "$name" . "__0 ^4 : /$line/;\n");
		print( "$name" . "__1 ^3 : /$line/ ~1;\n");
		print( "$name" . "__2 ^2 : /$line/ ~2;\n");
		print( "$name" . "__3 ^1 : /$line/ ~3;\n");
	}
}

if ($generate_word_patterns)
{
	print( "%MATCHER exclusive\n");
}
while (<>)
{
	chomp;
	getRules( $_);
}

