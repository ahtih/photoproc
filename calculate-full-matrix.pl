#!/usr/bin/perl

$matrix_program = "/home/ahti/src/photoproc/photoproc";

# /data/photo/dcraw7 -m -h -3 -c $filename |
#	photoproc -matrix - |
#   ./calculate-full-matrix.pl

use POSIX qw(floor);

@R=();
@G=();
@B=();

while (<>) {
	/^(\d+),(\d+),(\d+)$/ or next;

	push @R,$1;
	push @G,$2;
	push @B,$3;
	}

$M=int(floor(sqrt(($#R)+1)+0.5));
($#R == $M*$M-1) or die "Error: input data len ".(($#R)+1)." is not a square";

$N=($M+2) >> 1;
($M == 2*($N-1)) or die "Error: input square side len $M is not even";

$rotate_code=0;

sub calc_idx($$)
{
	my $x=shift;
	my $y=shift;

	foreach (1..$rotate_code) {
		my $new_x=$M-1 - $y;
		$y=$x;
		$x=$new_x;
		}

	return $x + $y*$M;
	}

my $best_rotate_detect_value=0;
my $best_rotate_code=0;
foreach $rotate_code (0..3) {
	my $idx=calc_idx(0,$N-2);
	my $minvalue=$R[$idx];
	$minvalue=$G[$idx] if ($minvalue > $G[$idx]);
	$minvalue=$B[$idx] if ($minvalue > $B[$idx]);

	if ($best_rotate_detect_value < $minvalue) {
		$best_rotate_detect_value = $minvalue;
		$best_rotate_code=$rotate_code;
		}
	}

$rotate_code=$best_rotate_code;

sub print_color($$$$)
{
	my $handle=shift;
	my $x=shift;
	my $y=shift;
	my $color_to_omit=shift;

	my $idx=calc_idx($x,$y);
	my $str1=($color_to_omit == 0) ? "$G[$idx]" : "$R[$idx]";
	my $str2=($color_to_omit == 2) ? "$G[$idx]" : "$B[$idx]";
	print $handle "$str1\t$str2\n";
	}

sub print_table($$$$$$$$$)
{
	open(PROG,"| $matrix_program -matrix") or
		die "Error running 2D matrix generator program ($matrix_program)";

	my $color_to_omit=shift;
	my $colorline1=shift;
	my $colorline2=shift;

	my $coord1=$_[0] * ($N-1) + (($_[1] + $_[2] < 0) ? ($N-2) : 0);
	my $coord2=$_[3] * ($N-1) + (($_[4] + $_[5] < 0) ? ($N-2) : 0);

	print_color(PROG,$N-3,0,$color_to_omit);
	for my $x (0..($N-2)) { print_color(PROG,$N-$colorline1,$x,$color_to_omit); }
	for my $y (0..($N-2)) {
		print_color(PROG,$N-$colorline2,$y,$color_to_omit);

		for my $x (0..($N-2)) {
			print_color(PROG,
					$coord1 + $x*$_[1] + $y*$_[2],
					$coord2 + $x*$_[4] + $y*$_[5],$color_to_omit);
			}
		}

	close(PROG);
	}

print "R-G table:\n"; print_table(2,6,5, 0,0,-1, 1,-1,0);
print "G-B table:\n"; print_table(0,5,2, 1,1,0, 0,0,1);
print "R-B table:\n"; print_table(1,2,6, 1,1,0, 1,0,-1);
