#!/bin/bash
top=`pwd`
for dir in *; do
	cd "$top" || exit 1
	if [ -d "$dir" ]; then
		cd "$dir" || exit 1
		if [[ -f dvdrom-image.ripmap || -f dvdrom-image.ripmap.lzma ]]; then
			# DOIT
			../jarchdvd-to-iso.pl || exit 1
		fi
	fi
done
