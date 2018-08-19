#!/bin/sh -
#################################################################
# Copyright (c) 2014-2017 Fidelity National Information         #
# Services, Inc. and/or its subsidiaries. All rights reserved.  #
#								#
# Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.	#
# All rights reserved.						#
#								#
# Copyright (c) 2018 Stephen L Johnson.				#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################

# This script automates the installation of YottaDB as much as possible,
# to the extent of attempting to download the distribution file.
# Current limitation is GNU/Linux on x86 (32- & 64-bit) architectures
# and root installation, but it is intended to relax this in the future.

# NOTE: This script requires the GNU wget program to download
# distribution files that are not on the local file system.

# Revision history
#
# 2011-02-15  0.01 K.S. Bhaskar - Initial version for internal use
# 2011-02-20  0.02 K.S. Bhaskar - Mostly usable with enough features for first Beta test
# 2011-02-21  0.03 K.S. Bhaskar - Deal with case of no group bin, bug fixes, download from FTP site, other platforms
# 2011-02-28  0.04 K.S. Bhaskar - Use which to get locations of id and grep, more bug fixes
# 2011-03-05  0.05 K.S. Bhaskar - Through V5.4-001 group only needed if execution restricted to a group
# 2011-03-08  0.06 K.S. Bhaskar - Make it work when bundled with GT.M V5.4-002
# 2011-03-10  0.10 K.S. Bhaskar - Incorporate review comments to bundle with V5.4-002 distribution
# 2011-05-03  0.11 K.S. Bhaskar - Allow for letter suffix releases
# 2011-10-25  0.12 K.S. Bhaskar - Support option to delete .o files on shared library platforms
# 2014-08-13  0.13 K.S. Bhaskar - Add verbosity around getting latest version and tarball, if requested
# 2015-10-13  0.14 GT.M Staff   - Fix a few minor bugs
# 2017-07-16  0.15 Sam Habiel   - --yottadb or --distrib https://github.com/YottaDB/YottaDB to install YottaDB
# 2017-08-12  0.16 Christopher Edwards - Default to YottaDB
# 2017-10-xx  0.17 Narayanan Iyer - See git commit message for description of changes.
#	Going forward, this script is maintained at https://github.com/YottaDB/YottaDB/blob/master/sr_unix/ydbinstall.sh
#	and there is no revision history in this file.

# Turn on debugging if set
if [ "Y" = "$ydb_debug" ] ; then set -x ; fi

# Initialization
timestamp=`date +%Y%m%d%H%M%S`
ydb_id=`which id`
ydb_grep=`which grep`
if [ -z "$USER" ] ; then USER=`$ydb_id -un` ; fi

# Functions
dump_info()
{
    set +x
    if [ -n "$gtm_arch" ] ; then echo gtm_arch " : " $gtm_arch ; fi
    if [ -n "$gtm_buildtype" ] ; then echo gtm_buildtype " : " $gtm_buildtype ; fi
    if [ -n "$gtm_configure_in" ] ; then echo gtm_configure_in " : " $gtm_configure_in ; fi
    if [ -n "$gtm_copyenv" ] ; then echo gtm_copyenv " : " $gtm_copyenv ; fi
    if [ -n "$gtm_copyexec" ] ; then echo gtm_copyexec " : " $gtm_copyexec ; fi
    if [ -n "$ydb_debug" ] ; then echo ydb_debug " : " $ydb_debug ; fi
    if [ -n "$ydb_dist" ] ; then echo ydb_dist " : " $ydb_dist ; fi
    if [ -n "$ydb_distrib" ] ; then echo ydb_distrib " : " $ydb_distrib ; fi
    if [ -n "$gtm_dryrun" ] ; then echo gtm_dryrun " : " $gtm_dryrun ; fi
    if [ -n "$ydb_filename" ] ; then echo ydb_filename " : " $ydb_filename ; fi
    if [ -n "$ydb_flavor" ] ; then echo ydb_flavor " : " $ydb_flavor ; fi
    if [ -n "$gtm_ftp_dirname" ] ; then echo gtm_ftp_dirname " : " $gtm_ftp_dirname ; fi
    if [ -n "$gtm_group" ] ; then echo gtm_group " : " $gtm_group ; fi
    if [ -n "$gtm_group_already" ] ; then echo gtm_group_already " : " $gtm_group_already ; fi
    if [ -n "$gtm_group_restriction" ] ; then echo gtm_group_restriction " : " $gtm_group_restriction ; fi
    if [ -n "$gtm_hostos" ] ; then echo gtm_hostos " : " $gtm_hostos ; fi
    if [ -n "$ydb_icu_version" ] ; then echo ydb_icu_version " : " $ydb_icu_version ; fi
    if [ -n "$gtm_install_flavor" ] ; then echo gtm_install_flavor " : " $gtm_install_flavor ; fi
    if [ -n "$ydb_installdir" ] ; then echo ydb_installdir " : " $ydb_installdir ; fi
    if [ -n "$gtm_keep_obj" ] ; then echo gtm_keep_obj " : " $gtm_keep_obj ; fi
    if [ -n "$gtm_lcase_utils" ] ; then echo gtm_lcase_utils " : " $gtm_lcase_utils ; fi
    if [ -n "$gtm_linkenv" ] ; then echo gtm_linkenv " : " $gtm_linkenv ; fi
    if [ -n "$gtm_linkexec" ] ; then echo gtm_linkexec " : " $gtm_linkexec ; fi
    if [ -n "$gtm_overwrite_existing" ] ; then echo gtm_overwrite_existing " : " $gtm_overwrite_existing ; fi
    if [ -n "$gtm_prompt_for_group" ] ; then echo gtm_prompt_for_group " : " $gtm_prompt_for_group ; fi
    if [ -n "$gtm_sf_dirname" ] ; then echo gtm_sf_dirname " : " $gtm_sf_dirname ; fi
    if [ -n "$gtm_tmpdir" ] ; then echo gtm_tmpdir " : " $gtm_tmpdir ; fi
    if [ -n "$gtm_user" ] ; then echo gtm_user " : " $gtm_user ; fi
    if [ -n "$gtm_verbose" ] ; then echo gtm_verbose " : " $gtm_verbose ; fi
    if [ -n "$ydb_version" ] ; then echo ydb_version " : " $ydb_version ; fi
    if [ -n "$gtm_gtm" ] ; then echo gtm_gtm " : " $gtm_gtm ; fi
    if [ -n "$ydb_routines" ] ; then echo ydb_routines " : " $ydb_routines ; fi
    if [ -n "$timestamp" ] ; then echo timestamp " : " $timestamp ; fi
    if [ -n "$ydb_change_removeipc" ] ; then echo ydb_change_removeipc " : " $ydb_change_removeipc ; fi
    if [ "Y" = "$ydb_debug" ] ; then set -x ; fi
}

err_exit()
{
	echo "YottaDB installation aborted due to above error. Run ydbinstall --help for detailed option list"
	exit 1
}

help_exit()
{
    set +x
    echo "ydbinstall [option] ... [version]"
    echo "Options are:"
    echo "--build-type buildtype - * type of YottaDB build, default is pro"
    echo "--copyenv dirname - copy gtmprofile and gtmcshrc files to dirname; incompatible with linkenv"
    echo "--copyexec dirname - copy gtm script to dirname; incompatible with linkexec"
    echo "--debug - * turn on debugging with set -x"
    echo "--distrib dirname or URL - source directory for YottaDB/GT.M distribution tarball, local or remote"
    echo "--preserveRemoveIPC - do not allow changes to RemoveIPC in /etc/systemd/login.conf if needed; defaults to allow changes"
    echo "--dry-run - do everything short of installing YottaDB, including downloading the distribution"
    echo "--group group - group that should own the YottaDB installation"
    echo "--group-restriction - limit execution to a group; defaults to unlimited if not specified"
    echo "--gtm - Install GT.M instead of YottaDB"
    echo "--help - print this usage information"
    echo "--installdir dirname - directory where YottaDB is to be installed; defaults to /usr/local/lib/yottadb/version"
    m1="--keep-obj - keep .o files"
    m1="$m1"" of M routines (normally deleted on platforms with YottaDB support for routines in shared libraries)"
    echo "$m1"
    echo "--linkenv dirname - create link in dirname to gtmprofile and gtmcshrc files; incompatible with copyenv"
    echo "--linkexec dirname - create link in dirname to gtm script; incompatible with copyexec"
    echo "--overwrite-existing - install into an existing directory, overwriting contents; defaults to requiring new directory"
    m1="--prompt-for-group - * YottaDB installation "
    m1="$m1""script will prompt for group; default is yes for production releases V5.4-002 or later, no for all others"
    echo "$m1"
    echo "--ucaseonly-utils -- install only upper case utility program names; defaults to both if not specified"
    echo "--user username - user who should own YottaDB installation; default is root"
    m1="--utf8 ICU_version - install "
    m1="$m1""UTF-8 support using specified  major.minor ICU version; specify default to use default version"
    echo "$m1"
    echo "--verbose - * output diagnostic information as the script executes; default is to run quietly"
    echo "options that take a value (e.g, --group) can be specified as either --option=value or --option value"
    echo "options marked with * are likely to be of interest primarily to YottaDB developers"
    echo "version is defaulted from mumps file if one exists in the same directory as the installer"
    echo "This version must run as root."
    echo ""
    echo "Example usages are (assumes latest YottaDB release is r1.22 and latest GT.M version is V6.3-005)"
    echo "  ydbinstall.sh                          # installs latest YottaDB release (r1.22) at /usr/local/lib/yottadb/r122"
    echo "  ydbinstall.sh --utf8 default           # installs YottaDB release r1.22 with added support for UTF-8"
    echo "  ydbinstall.sh --installdir /r122 r1.22 # installs YottaDB r1.22 at /r122"
    echo "  ydbinstall.sh --gtm                    # installs latest GT.M version (V6.3-005) at /usr/local/lib/fis-gtm/V6.3-005_x86_64"
    echo ""
    exit 1
}

mktmpdir()
{
    case `uname -s` in
        AIX | SunOS) tmpdirname="/tmp/${USER}_$$_${timestamp}"
            ( umask 077 ; mkdir $tmpdirname ) ;;
        HP-UX) tmpdirname=`mktemp`
            ( umask 077 ; mkdir $tmpdirname ) ;;
        *) tmpdirname=`mktemp -d` ;;
    esac
    echo $tmpdirname
}

# This function is invoked whenever we detect an option that requires a value (e.g. --utf8) that is not
# immediately followed by a =. In that case, the next parameter in the command line ($2) is the value.
# We check if this parameter starts with a "--" as well and if so it denotes a different option and not a value.
#
# Input
# -----
# $1 for this function is the # of parameters remaining to be processed in command line
# $2 for this function is the next parameter in the command line immediately after the current option (which has a -- prefix).
#
# Output
# ------
# returns 0 in case $2 is non-null and does not start with a "--"
# returns 1 otherwise.
#
isvaluevalid()
{
	if [ 1 -lt "$1" ] ; then
		# bash might have a better way for checking whether $2 starts with "--" than the grep done below
		# but we want this script to run with sh so go with the approach that will work on all shells.
		retval=`echo "$2" | grep -c '^--'`
	else
		# option (e.g. --utf8) is followed by no other parameters in the command line
		retval=1
	fi
	echo $retval
}

# Defaults that can be over-ridden by command line options to follow
# YottaDB prefixed versions:
if [ -n "$ydb_buildtype" ] ; then gtm_buildtype="$ydb_buildtype" ; fi
if [ -n "$ydb_keep_obj" ] ; then gtm_keep_obj="$ydb_keep_obj" ; fi
if [ -n "$ydb_dryrun" ] ; then gtm_dryrun="$ydb_dryrun" ; fi
if [ -n "$ydb_group_restriction" ] ; then gtm_group_restriction="$ydb_group_restriction" ; fi
if [ -n "$ydb_gtm" ] ; then gtm_gtm="$ydb_gtm" ; fi
if [ -n "$ydb_lcase_utils" ] ; then gtm_lcase_utils="$ydb_lcase_utils" ; fi
if [ -n "$ydb_overwrite_existing" ] ; then gtm_overwrite_existing="$ydb_overwrite_existing" ; fi
if [ -n "$ydb_prompt_for_group" ] ; then gtm_prompt_for_group="$ydb_prompt_for_group" ; fi
if [ -n "$ydb_verbose" ] ; then gtm_verbose="$ydb_verbose" ; fi
if [ -z "$ydb_change_removeipc" ] ; then ydb_change_removeipc="yes" ; fi
# GTM prefixed versions (for backwards compatibility)
if [ -z "$gtm_buildtype" ] ; then gtm_buildtype="pro" ; fi
if [ -z "$gtm_keep_obj" ] ; then gtm_keep_obj="N" ; fi
if [ -z "$gtm_dryrun" ] ; then gtm_dryrun="N" ; fi
if [ -z "$gtm_group_restriction" ] ; then gtm_group_restriction="N" ; fi
if [ -z "$gtm_gtm" ] ; then gtm_gtm="N" ; fi
if [ -z "$gtm_lcase_utils" ] ; then gtm_lcase_utils="Y" ; fi
if [ -z "$gtm_overwrite_existing" ] ; then gtm_overwrite_existing="N" ; fi
if [ -z "$gtm_prompt_for_group" ] ; then gtm_prompt_for_group="N" ; fi
if [ -z "$gtm_verbose" ] ; then gtm_verbose="N" ; fi


# Initializing internal flags
gtm_group_already="N"

# Process command line
while [ $# -gt 0 ] ; do
    case "$1" in
        --build-type*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_buildtype=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_buildtype=$2 ; shift
                else echo "--build-type needs a value" ; err_exit
                fi
            fi ;;
        --copyenv*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_copyenv=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_copyenv=$2 ; shift
                else echo "--copyenv needs a value" ; err_exit
                fi
            fi
            unset gtm_linkenv
            shift ;;
        --copyexec*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_copyexec=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_copyexec=$2 ; shift
                else echo "--copyexec needs a value" ; err_exit
                fi
            fi
            unset gtm_linkexec
            shift ;;
        --debug) ydb_debug="Y" ; set -x ; shift ;;
        --distrib*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then ydb_distrib=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then ydb_distrib=$2 ; shift
                else echo "--distrib needs a value" ; err_exit
                fi
            fi
            shift ;;
        --dry-run) gtm_dryrun="Y" ; shift ;;
        --gtm)
            gtm_gtm="Y"
            shift ;;
        --group-restriction) gtm_group_restriction="Y" ; shift ;; # must come before group*
        --group*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_group=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_group=$2 ; shift
                else echo "--group needs a value" ; err_exit
                fi
            fi
            shift ;;
        --help) help_exit ;;
        --installdir*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then ydb_installdir=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then ydb_installdir=$2 ; shift
                else echo "--installdir needs a value" ; err_exit
                fi
            fi
            shift ;;
        --keep-obj) gtm_keep_obj="Y" ; shift ;;
        --linkenv*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_linkenv=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_linkenv=$2 ; shift
                else echo "--linkenv needs a value" ; err_exit
                fi
            fi
            unset gtm_copyenv
            shift ;;
        --linkexec*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_linkexec=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_linkexec=$2 ; shift
                else echo "--linkexec needs a value" ; err_exit
                fi
            fi
            unset gtm_copyexec
            shift ;;
        --overwrite-existing) gtm_overwrite_existing="Y" ; shift ;;
        --preserveRemoveIPC) ydb_change_removeipc="no" ; shift ;; # must come before group*
        --prompt-for-group) gtm_prompt_for_group="Y" ; shift ;;
        --ucaseonly-utils) gtm_lcase_utils="N" ; shift ;;
        --user*) tmp=`echo $1 | cut -s -d = -f 2-`
            if [ -n "$tmp" ] ; then gtm_user=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then gtm_user=$2 ; shift
                else echo "--user needs a value" ; err_exit
                fi
            fi
            shift ;;
        --utf8*) tmp=`echo $1 | cut -s -d = -f 2- | tr DEFAULT default`
            if [ -n "$tmp" ] ; then ydb_icu_version=$tmp
            else retval=`isvaluevalid $# $2` ; if [ "$retval" -eq 0 ] ; then ydb_icu_version=`echo $2 | tr DEFAULT default`; shift
                else echo "--utf8 needs a value" ; err_exit
                fi
            fi
            shift ;;
        --verbose) gtm_verbose="Y" ; shift ;;
        -*) echo Unrecognized option "$1" ; err_exit ;;
        *) if [ -n "$ydb_version" ] ; then echo Nothing must follow the YottaDB/GT.M version ; err_exit
            else ydb_version=$1 ; shift ; fi
    esac
done
if [ "Y" = "$gtm_verbose" ] ; then echo Processed command line ; dump_info ; fi

# Set environment variables according to machine architecture
gtm_arch=`uname -m | tr -d _`
case $gtm_arch in
    sun*) gtm_arch="sparc" ;;
esac
gtm_hostos=`uname -s | tr A-Z a-z`
case $gtm_hostos in
    gnu/linux) gtm_hostos="linux" ;;
    hp-ux) gtm_hostos="hpux" ;;
    sun*) gtm_hostos="solaris" ;;
    Darwin) gtm_hotos="darwin" ;;
esac
gtm_shlib_support="Y"
case ${gtm_hostos}_${gtm_arch} in
    linux_x8664)
        gtm_sf_dirname="GT.M-amd64-Linux"
        gtm_ftp_dirname="linux_x8664"
        ydb_flavor="x8664"
        gtm_install_flavor="x86_64" ;;
    linux_armv6l)
        ydb_flavor="armv6l" ;;
    linux_armv7l)
        ydb_flavor="armv7l" ;;
    darwin_x8664)
        ydb_flavor="x8664" ;;
    *) echo Architecture `uname -s` on `uname -m` not supported by this script ; err_exit ;;
esac

# YottaDB version is required - first see if ydbinstall and mumps are bundled
if [ -z "$ydb_version" ] ; then
    tmp=`dirname $0`
    if [ -e "$tmp/mumps" -a -e "$tmp/_XCMD.m" ] ; then
        ydb_distrib=$tmp
        ydb_dist=$tmp ; export ydb_dist
        chmod +x $ydb_dist/mumps
        tmp=`mktmpdir`
        ydb_routines="$tmp($ydb_dist)" ; export ydb_routines
        ydb_version=`$ydb_dist/mumps -run %XCMD 'write $piece($zyrelease," ",2)'`
        rm -rf $tmp
    fi
fi
if [ "Y" = "$gtm_verbose" ] ; then
    echo Determined architecture, OS and YottaDB/GT.M version ; dump_info
    wget_flags="-P"
else wget_flags="-qP"
fi

# See if YottaDB version can be determined from meta data
if [ -z "$ydb_distrib" ] ; then
    ydb_distrib="https://api.github.com/repos/YottaDB/YottaDB/"
fi
if [ "Y" = "$gtm_gtm" ] ; then
    ydb_distrib="http://sourceforge.net/projects/fis-gtm"
fi

tmpdir=`mktmpdir`
gtm_tmpdir=$tmpdir
mkdir $gtm_tmpdir/tmp
latest=`echo "$ydb_version" | tr LATES lates`
if [ -z "$ydb_version" -o "latest" = "$latest" ] ; then
    case $ydb_distrib in
        http://sourceforge.net/projects/fis-gtm | https://sourceforge.net/projects/fis-gtm)
            gtm_gtm="Y"
            if [ "Y" = "$gtm_verbose" ] ; then
               echo wget ${ydb_distrib}/files/${gtm_sf_dirname}/latest to determine latest version
               echo Check proxy settings if wget hangs
            fi
            if { wget $wget_flags $gtm_tmpdir ${ydb_distrib}/files/${gtm_sf_dirname}/latest 2>&1 1>${gtm_tmpdir}/wget_latest.log ; } ; then
                ydb_version=`cat ${gtm_tmpdir}/latest`
            else echo Unable to determine YottaDB/GT.M version ; err_exit
            fi ;;
        ftp://*)
            if [ "Y" = "$gtm_verbose" ] ; then
               echo wget $gtm_tmpdir ${ydb_distrib}/${gtm_ftp_dirname}/latest to determine latest version
               echo Check proxy settings if wget hangs
            fi
            if { wget $wget_flags $gtm_tmpdir ${ydb_distrib}/${gtm_ftp_dirname}/latest 2>&1 1>${gtm_tmpdir}/wget_latest.log ; } ; then
                ydb_version=`cat ${gtm_tmpdir}/latest`
            else echo Unable to determine YottaDB/GT.M version ; err_exit
            fi ;;
        https://api.github.com/repos/YottaDB/YottaDB* | https://github.com/YottaDB/YottaDB*)
            if [ "Y" = "$gtm_verbose" ] ; then
                echo wget https://api.github.com/repos/YottaDB/YottaDB/releases/latest to determine latest version
                echo Check proxy settings if wget hangs
            fi
            if { wget $wget_flags $gtm_tmpdir https://api.github.com/repos/YottaDB/YottaDB/releases/latest 2>&1 1>${gtm_tmpdir}/wget_latest.log ; } ; then
                ydb_version=`grep "tag_name" ${gtm_tmpdir}/latest | cut -d'"' -f4`
            fi ;;
        *)
            if [ -f ${ydb_distrib}/latest ] ; then
                ydb_version=`cat ${ydb_distrib}/latest`
                if [ "Y" = "$gtm_verbose" ] ; then echo Version is $ydb_version ; fi
            else echo Unable to determine YottaDB/GT.M version ; err_exit
            fi ;;
    esac
fi
if [ -z "$ydb_version" ] ; then
echo YottaDB/GT.M version to install is required ; err_exit
fi

# Now that "ydb_version" is determined, get YottaDB/GT.M distribution if ydbinstall is not bundled with distribution
if [ -f "${ydb_distrib}/mumps" ] ; then gtm_tmpdir=$ydb_distrib
else
    tmp=`echo $ydb_version | tr -d .-`
    ydb_filename=""
    if [ "Y" = "$gtm_gtm" ] ; then ydb_filename=gtm_${tmp}_${gtm_hostos}_${ydb_flavor}_${gtm_buildtype}.tar.gz ; fi
    case $ydb_distrib in
        http://sourceforge.net/projects/fis-gtm | https://sourceforge.net/projects/fis-gtm)
            if [ "Y" = "$gtm_verbose" ] ; then
                echo wget ${ydb_distrib}/files/${gtm_sf_dirname}/${ydb_version}/${ydb_filename} to download tarball
                echo Check proxy settings if wget hangs
            fi
            if { ! wget $wget_flags $gtm_tmpdir ${ydb_distrib}/files/${gtm_sf_dirname}/${ydb_version}/${ydb_filename} \
                	2>&1 1>${gtm_tmpdir}/wget_dist.log ; } ; then
                echo Unable to download GT.M distribution $ydb_filename ; err_exit
            fi ;;
        https://api.github.com/repos/YottaDB/YottaDB* | https://github.com/YottaDB/YottaDB*)
            if [ "Y" = "$gtm_verbose" ] ; then
                echo wget https://api.github.com/repos/YottaDB/YottaDB/releases/tags/${ydb_version} and parse to download tarball
                echo Check proxy settings if wget hangs
            fi
            if { wget $wget_flags $gtm_tmpdir https://api.github.com/repos/YottaDB/YottaDB/releases/tags/${ydb_version} 2>&1 1>${gtm_tmpdir}/wget_dist.log ;} ; then
		# There might be multiple binary tarballs of YottaDB (for various architectures & platforms).
		# If so, choose the one that corresponds to the current host.
		yottadb_download_urls=`grep "browser_download_url" ${gtm_tmpdir}/${ydb_version} | cut -d'"' -f4`
		# Determine current host's architecture
		arch=`uname -m | tr -d '_'`
		# Determine current host's OS. We expect the OS name in the tarball.
		platform=`uname -s | tr '[A-Z]' '[a-z]'`
		yottadb_download_url=""
		for fullfilename in $yottadb_download_urls
		do
			ydb_filename=$(basename "$fullfilename")	# Get filename without the leading path
			extension="${ydb_filename##*.}"			# Get file extension
			if [ $extension != "tgz" ] ; then
				# Binary tarballs have a ".tgz" extension. Skip anything else.
				continue
			fi
			case $ydb_filename in
				*"$arch"*) ;;			# If tarball has current architecture in its name, consider it
				*)         continue ;;		# else move on to next tarball
			esac
			if [ $platform = "linux" ] ; then
				# If the current host is a RHEL box then set the OS to RHEL (not Linux) as there is a RHEL-specific tarball.
				# We check that it is a RHEL box by the existence of the file /etc/redhat-release.
				# But note that RHEL-specific releases of YottaDB happened only starting r1.10 so do this only
				# if the requested version is not r1.00 (the only YottaDB release prior to r1.10)
				if [ -e /etc/redhat-release -a "r1.00" != ${ydb_version} ] ; then
					platform="rhel"
				fi
			fi
			case $ydb_filename in
				*"$platform"*) ;;			# If tarball has current architecture in its name, consider it
				*)             continue ;;		# else move on to next tarball
			esac
			yottadb_download_url=$fullfilename
			break					# Now that we found one tarball, stop looking at other choices
		done
		if [ $yottadb_download_url = "" ]; then echo Unable to find YottaDB tarball for ${ydb_version} $platform $arch ; err_exit; fi
                wget $wget_flags $gtm_tmpdir $yottadb_download_url
                if [ ! -f ${gtm_tmpdir}/${ydb_filename} ]; then echo Unable to download YottaDB distribution $ydb_filename ; err_exit; fi
            else echo Error during wget of YottaDB distribution file ${ydb_distrib}/${ydb_filename} ; err_exit
            fi ;;
        ftp://*)
            if [ "Y" = "$gtm_verbose" ] ; then
                echo wget ${ydb_distrib}/${gtm_ftp_dirname}/${tmp}/${ydb_filename} to download tarball
                echo Check proxy settings if wget hangs
            fi
            if { ! wget $wget_flags $gtm_tmpdir ${ydb_distrib}/${gtm_ftp_dirname}/${tmp}/${ydb_filename} \
                	2>&1 1>${gtm_tmpdir}/wget_dist.log ; } ; then
                echo Unable to download GT.M distribution $ydb_filename ; err_exit
            fi ;;
        *)
            if [ -f ${ydb_distrib}/${ydb_filename} ] ; then
                if [ "Y" = "$gtm_verbose" ] ; then echo tarball is ${ydb_distrib}/${ydb_filename} ; fi
                ln -s ${ydb_distrib}/${ydb_filename} $gtm_tmpdir
            else echo Unable to locate YottaDB/GT.M distribution file ${ydb_distrib}/${ydb_filename} ; err_exit
            fi ;;
    esac
    ( cd $gtm_tmpdir/tmp ; gzip -d < ${gtm_tmpdir}/${ydb_filename} | tar xf - 2>&1 1>${gtm_tmpdir}/tar.log )
fi
if [ "Y" = "$gtm_verbose" ] ; then echo Downloaded and unpacked YottaDB/GT.M distribution ; dump_info ; fi

# Check installation settings & provide defaults as needed
tmp=`$ydb_id -un`
if [ -z "$gtm_user" ] ; then gtm_user=$tmp
else if [ "$gtm_user" != "`$ydb_id -un $gtm_user`" ] ; then
    echo $gtm_user is a non-existent user ; err_exit
    fi
fi
if [ "root" = $tmp ] ; then
    if [ -z "$gtm_group" ] ; then gtm_group=`$ydb_id -gn`
    else if [ "root" != "$gtm_user" -a "$gtm_group" != "`$ydb_id -Gn $gtm_user | xargs -n 1 | $ydb_grep $gtm_group`" ] ; then
        echo $gtm_user is not a member of $gtm_group ; err_exit
        fi
    fi
 else
    echo Non-root installations not currently supported
    if [ "N" = "$gtm_dryrun" ] ; then err_exit
    else echo "Continuing because --dry-run selected"
    fi
fi
if [ -z "$ydb_installdir" ] ; then
    if [ "N" = "$gtm_gtm" ] ; then
         ydbver=`echo $ydb_version | tr '[A-Z]' '[a-z]' | tr -d '.-'`
         ydb_installdir=/usr/local/lib/yottadb/${ydbver}
    else ydb_installdir=/usr/local/lib/fis-gtm/${ydb_version}_${gtm_install_flavor}
    fi
fi
# if install directory is relative then need to make it absolute before passing it to configure
# (or else configure will create a subdirectory under $tmpdir (/tmp/.*) and install YottaDB there which is not what we want)
if [ `echo $ydb_installdir | grep -c '^/'` -eq 0 ] ; then
    ydb_installdir=`pwd`/$ydb_installdir
fi
if [ -d "$ydb_installdir" -a "Y" != "$gtm_overwrite_existing" ] ; then
    echo $ydb_installdir exists and --overwrite-existing not specified ; err_exit
fi

if [ "Y" = "$gtm_verbose" ] ; then echo Finished checking options and assigning defaults ; dump_info ; fi

# Prepare input to YottaDB configure script. The corresponding questions in configure.gtc are listed below in comments
gtm_configure_in=${gtm_tmpdir}/configure_${timestamp}.in
export ydb_change_removeipc			# Signal configure.gtc to set RemoveIPC=no or not, if needed
echo $gtm_user >>$gtm_configure_in		# Response to : "What user account should own the files?"
echo $gtm_group >>$gtm_configure_in		# Response to : "What group should own the files?"
echo $gtm_group_restriction >>$gtm_configure_in	# Response to : "Should execution of YottaDB be restricted to this group?"
echo $ydb_installdir >>$gtm_configure_in	# Response to : "In what directory should YottaDB be installed?"
echo y >>$gtm_configure_in			# Response to one of two possible questions
						#	"Directory $ydb_dist exists. If you proceed with this installation then some files will be over-written. Is it ok to proceed?"
						#	"Directory $ydb_dist does not exist. Do you wish to create it as part of this installation? (y or n)"
if [ -z "$ydb_icu_version" ] ; then echo n  >>$gtm_configure_in	# Response to : "Should UTF-8 support be installed?"
else echo y  >>$gtm_configure_in		# Response to : "Should UTF-8 support be installed?"
    if [ "default" = $ydb_icu_version ] ; then echo n  >>$gtm_configure_in	# Response to : "Should an ICU version other than the default be used?"
    else echo y >>$gtm_configure_in		# Response to : "Should an ICU version other than the default be used?"
        echo $ydb_icu_version >>$gtm_configure_in	# Response to : "Enter ICU version"
    fi
fi
echo $gtm_lcase_utils >>$gtm_configure_in	# Response to : "Do you want uppercase and lowercase versions of the MUMPS routines?"
if [ "Y" = $gtm_shlib_support ] ; then echo $gtm_keep_obj >>$gtm_configure_in ; fi	# Response to : "Object files of M routines placed in shared library $ydb_dist/libyottadbutil$ext. Keep original .o object files (y or n)?"
echo n >>$gtm_configure_in			# Response to : "Installation completed. Would you like all the temporary files removed from this directory?"
if [ "Y" = "$gtm_verbose" ] ; then echo Prepared configuration file ; cat $gtm_configure_in ; dump_info ; fi


# Run the YottaDB configure script
if [ "$ydb_distrib" != "$gtm_tmpdir" ] ; then
    chmod +w $gtm_tmpdir/tmp
    cd $gtm_tmpdir/tmp
    # Starting YottaDB r1.10, unpacking the binary tarball creates an additional directory (e.g. yottadb_r122)
    # before the untar so cd into that subdirectory to get at the "configure" script from the distribution.
    if [ "N" = "$gtm_gtm" -a "r1.00" != ${ydb_version} ] ; then
        cd yottadb_r*
    fi
fi

if [ -e configure.sh ] ; then rm -f configure.sh ; fi

tmp=`head -1 configure | cut -f 1`
if [ "#!/bin/sh" != "$tmp" ] ; then
    echo "#!/bin/sh" >configure.sh
fi
cat configure >>configure.sh
chmod +x configure.sh

# Stop here if this is a dry run
if [ "Y" = "$gtm_dryrun" ] ; then echo Installation prepared in $gtm_tmpdir ; exit ; fi

sh -x ./configure.sh <$gtm_configure_in 1> $gtm_tmpdir/configure_${timestamp}.out 2>$gtm_tmpdir/configure_${timestamp}.err
if [ $? -gt 0 ] ; then echo "configure.sh failed. Output follows"; cat $gtm_tmpdir/configure_${timestamp}.out $gtm_tmpdir/configure_${timestamp}.err ; exit 1; fi
rm -rf $tmpdir	# Now that install is successful, remove temporary directory
if [ "Y" = "$gtm_gtm" ] ; then
	product_name="GT.M"
else
	product_name="YottaDB"
fi
echo $product_name version $ydb_version installed successfully at $ydb_installdir

# Create copies of environment scripts and gtm executable
if [ -d "$gtm_linkenv" ] ; then
    ( cd $gtm_linkenv ; ln -s $ydb_installdir/ydb_env_set $ydb_installdir/ydb_env_unset $ydb_installdir/gtmprofile ./ )
    if [ "Y" = "$gtm_verbose" ] ; then echo Linked env ; ls -l $gtm_linkenv ; fi
else if [ -d "$gtm_copyenv" ] ; then
        ( cd $gtm_copyenv ; cp -P $ydb_installdir/ydb_env_set $ydb_installdir/ydb_env_unset $ydb_installdir/gtmprofile ./ )
        if [ "Y" = "$gtm_verbose" ] ; then echo Copied env ; ls -l $gtm_copyenv ; fi
     fi
fi
if [ -d "$gtm_linkexec" ] ; then
    ( cd $gtm_linkexec ; ln -s $ydb_installdir/ydb $ydb_installdir/gtm ./ )
    if [ "Y" = "$gtm_verbose" ] ; then echo Linked exec ; ls -l $gtm_linkexec ; fi
else if [ -d "$gtm_copyexec" ] ; then
        ( cd $gtm_copyexec ; cp -P $ydb_installdir/ydb $ydb_installdir/gtm ./ )
        if [ "Y" = "$gtm_verbose" ] ; then echo Copied exec ; ls -l $gtm_copyexec ; fi
     fi
fi
