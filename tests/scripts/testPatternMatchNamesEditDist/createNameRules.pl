#!/usr/bin/perl

use strict;
use warnings;
use 5.012;
use utf8;
use Encode qw(decode encode);

# If you set this to 1, then the rule generator will generate tokens that can be used in multiword patterns
# If you set this to 0, then the rule generator will generate lexem rules only.
# In the later case you have to start strusPatternMatcher with option -K (or --tokens) to output the generated tokens.
my $generate_word_patterns = 0;

# Take a line with a name and make pattern matcher rules out of it
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
			my $chnum = ord($ch);
			my $chhex = sprintf( "%X", $chnum);
			$name .= "_$chhex";
		}
	}
	my $namelen = length( $line);
	my $edist = 0;
	if ($namelen >= 3)
	{
		$edist += 1;
	}
	if ($namelen >= 7)
	{
		$edist += 1;
	}
	if ($namelen >= 12)
	{
		$edist += 1;
	}
	if ($namelen >= 17)
	{
		$edist += 1;
	}
	if ($namelen >= 23)
	{
		$edist += 1;
	}
	my $seq = encode("utf-8", $line);
	if ($generate_word_patterns)
	{
		print( "_$name" . " : /$seq/ ~$edist;\n");
		print( "$name" . " = _$name" . ";\n");
	}
	else
	{
		print( "$name" . " : /$seq/ ~$edist;\n");
	}
}

if ($generate_word_patterns)
{
	print( "%MATCHER exclusive\n");
}
while (<>)
{
	chomp;
	getRules( decode("utf-8", $_));
}

