#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
ARCHMAKE="make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE}"
SCRIPTDIR=$(pwd)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
set -x
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

	$ARCHMAKE mrproper
	$ARCHMAKE defconfig
	$ARCHMAKE -j4
	$ARCHMAKE -j4 modules
	$ARCHMAKE dtbs
fi 

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

mkdir -p rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
else
    cd busybox
fi

make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install
cd ../rootfs

DEPPATH=$(whereis ${CROSS_COMPILE}gcc | cut -d ':' -f2 | rev | cut -d '/' -f 3- | rev)/${CROSS_COMPILE::-1}/libc

echo "Adding library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | cut -d ':' -f2 | cut -d '/' -f3 | tr -d ']' \
	| xargs -I '{}' cp -f ${DEPPATH}/lib/{} lib/{}
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"  | cut -d ':' -f 2 | tr -d ' []' \
	| xargs -d '\n'-t -I '{}' cp -f ${DEPPATH}/lib64/{} lib64/{}

sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

cd "$SCRIPTDIR"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

cp finder.sh ${OUTDIR}/rootfs/home
cp writer ${OUTDIR}/rootfs/home
cp finder-test.sh ${OUTDIR}/rootfs/home
cp -Lr conf ${OUTDIR}/rootfs/home

sudo chown -R root:root ${OUTDIR}/rootfs
cd "$OUTDIR"/rootfs

find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ..
gzip -f initramfs.cpio
ln -fs linux-stable/arch/arm64/boot/Image ./Image

