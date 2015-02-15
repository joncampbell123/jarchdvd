#!/usr/bin/perl
#
# Take older fragmented JarchDVD rips and
#    1) convert to an ISO
#    2) decrypt CSS, if possible
#    3) verify the file decodes without errors
#    4) if it decodes with errors, then assume CSS keys failed and the conversion must be re-done with the CSS keys dumped to a text file for reference
#    5) on completion, move the old files into a __DISCARD__ subfolder
#
# This is necessary to help clean up the DVD collection, to identify cases where th CSS keystore failed, and to delete bad rips.
# When all has been verified, the maintainer is expected to rm -Rfv */__DISCARD from the root of the DVD rips tree.
#
# This is especially useful for pre-2010 rips that used the fragmented storage method once needed for archiving on DVD-R.

# command to tell FFMPEG to play from STDIN and output to /dev/null, and emit errors to errors.txt
# 
#     ffmpeg -f mpeg -i - -an -pix_fmt yuv420p -f rawvideo - >/dev/null 2>errors.txt
#

my $out_iso = `which jarchdvd-out-iso`; chomp $out_iso;
die "Cannot locate jarchdvd output ISO program" unless -x $out_iso;

my $out_vob = `which jarchdvd-out-vob`; chomp $out_vob;
die "Cannot locate jarchdvd output VOB program" unless -x $out_vob;

unlink("__BAD__DECRYPTION__");

# usually a JarchDVD rip is contained in a folder with the name "dvd rip - <blahblah>" automatically name the ISO that after rip
my $name = `basename "\`pwd\`"`; chomp $name;
$name =~ s/^dvd +rip +- +//g;
$name =~ s/ +$//;
die "Bad name for this folder" if $name eq "";

print "RIP name: $name\n";

die "$name.iso already exists." if -f "$name.iso";

die unless ( -f "dvdrom-image.img-dir" || -f "dvdrom-image.img-0000" );

# most of my archives have the ripmaps compressed!
system("for i in *.xz; do if [ -f \"\$i\" ]; then xz -d \"\$i\" || exit 1; fi; done") == 0 || die;
system("for i in *.lzma; do if [ -f \"\$i\" ]; then lzma -d \"\$i\" || exit 1; fi; done") == 0 || die;

die "Need rip-map!" unless -f "dvdrom-image.ripmap";

# 1 & 2: convert to ISO. Decrypt if possible
if ( !( -f "jarchdvd-output.iso" ) ) {
	if ( -s "JarchDVD-Title.keystore" <= 16 ) {
		print "WARNING: CSS keystore looks empty. Hope the DVD isn't encrypted!\n";
	}

	print "Step #1: Convert to ISO\n";
	$x = system($out_iso);
	if ($x != 0) {
		print "Conversion failed\n";
		unlink("jarchdvd-output.iso");
		exit 1;
	}
}

# 3: verify the program decodes properly. Look for massive complaints from FFMPEG about "concealing AC errors" or "MV errors"
if ( !( -f "errors.txt" ) ) {
	print "Step #3: Verify the MPEG stream is ok\n";
	$x = system("$out_vob <jarchdvd-output.iso | ffmpeg -f mpeg -i - -an -pix_fmt yuv420p -f rawvideo - >/dev/null 2>errors.txt");
	if ($x & 0xFF) { # if it died because of a signal (such as CTRL+C) then remove errors.txt
		print "Step 3 failed, FFMPEG aborted\n";
		unlink("errors.txt");
		exit 1;
	}
}

# read the errors.txt. look for messages like:
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 5 11
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 14 15
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 23 17
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 21 23
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 22 25
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 24 26
# [mpeg2video @ 0x91db6e0] Warning MVs not available
# [mpeg2video @ 0x91db6e0] concealing 1035 DC, 1035 AC, 1035 MV errors
# [mpeg2video @ 0x91db6e0] ac-tex damaged at 41 3
# [mpeg2video @ 0x91db6e0] 00 motion_type at 24 17
open(X,"<errors.txt") || die;
my $errcnt = 0;
while (my $line = <X>) {
	chomp $line;
	$errcnt++ if $line =~ m/\[mpeg2video +\@ +.*\] +.*(warning|error|damaged|motion_type)/i;
}
close(X);
print "$errcnt errors reported by FFMPEG.\n";
if ($errcnt >= 4096) {  # some rips are quite OK, but have junk somewhere
	print "That's just not acceptable. Leaving it alone.\n";
	open(X,">__BAD__DECRYPTION__");
	close(X);
	exit 0;
}

# rename ISO
die "$name.iso already exists" if -f "$name.iso" && -f "jarchdvd-output.iso";
rename("jarchdvd-output.iso","$name.iso");

# move stuff into __DISCARD__ folder
mkdir("__DISCARD__");
die unless -d "__DISCARD__";
system("for i in errors.txt *.keystore *.list *.ripmap *.bin dvdrom-image.img-dir dvd-rip-image.iso dvdrom-image.img-????; do if [ -f \"\$i\" ]; then mv -vn \"\$i\" __DISCARD__/ || exit 1; fi; done") == 0 || die;

