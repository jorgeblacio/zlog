#!/bin/bash

# adapted from https://github.com/ceph/ceph/blob/master/install-deps.sh

set -e

if [[ "$OSTYPE" == "darwin"* ]]; then
  brew update || true
  brew install boost protobuf cmake lmdb || true
  exit 0
fi

ZLOG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if test $(id -u) != 0 ; then
  SUDO=sudo
fi

function debs() {
  local tmp=$(mktemp -d)
  trap "rm -rf $tmp" EXIT

  $SUDO apt-get update

  $SUDO env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y devscripts equivs \
    git # for packaging

  # run mk-build-deps in tmp dir to avoid creation of artifact files that
  # cause errors for read-only docker mounts
  pushd $tmp
  $SUDO env DEBIAN_FRONTEND=noninteractive \
    mk-build-deps --install --remove \
    --tool="apt-get -y --no-install-recommends" \
    ${ZLOG_DIR}/debian/control || exit 1
  popd
  rm -rf $tmp

  $SUDO env DEBIAN_FRONTEND=noninteractive \
    apt-get -y remove zlog-build-deps

  # for doc/build.sh
  $SUDO env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y python-virtualenv
}

function rpms() {
  local tmp=$(mktemp -d)
  trap "rm -fr $tmp" EXIT

  yumdnf="yum"
  builddepcmd="yum-builddep -y"
  if command -v dnf > /dev/null; then
    yumdnf="dnf"
    $SUDO dnf install -y 'dnf-command(builddep)'
    builddepcmd="dnf -y builddep --allowerasing"
  fi

  $SUDO $yumdnf install -y redhat-lsb-core \
    git # for packaging

  spec_in="zlog.spec.in"
  gpp_sym=" "
  gcc_sym=" "

  case $(lsb_release -si) in
    Fedora)
      if test $yumdnf = yum; then
        $SUDO $yumdnf install -y yum-utils
      fi
      ;;
    CentOS)
      $SUDO yum install -y yum-utils
      MAJOR_VERSION=$(lsb_release -rs | cut -f1 -d.)
      $SUDO yum-config-manager --add-repo https://dl.fedoraproject.org/pub/epel/$MAJOR_VERSION/x86_64/
      $SUDO yum install --nogpgcheck -y epel-release

      $SUDO rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-EPEL-$MAJOR_VERSION
      $SUDO rm -f /etc/yum.repos.d/dl.fedoraproject.org*

      if test $(lsb_release -si) = CentOS -a $MAJOR_VERSION = 7 ; then
        $SUDO yum-config-manager --enable cr
        case $(uname -m) in
          x86_64)
            $SUDO yum -y install centos-release-scl
            # $SUDO yum -y install devtoolset-7-gcc-c++ devtoolset-7-libatomic-devel
            sed -e 's/gcc-c++/devtoolset-7-gcc-c++/g' zlog.spec.in > ${tmp}/zlog.spec.in 
            sed -i 's/libatomic/devtoolset-7-libatomic-devel/g' ${tmp}/zlog.spec.in
            spec_in="${tmp}/zlog.spec.in"
            gpp_sym="ln -sf /opt/rh/devtoolset-7/root/usr/bin/g++ /usr/bin/g++"
            gcc_sym="ln -sf /opt/rh/devtoolset-7/root/usr/bin/gcc /usr/bin/gcc"
            ;;
        esac
      fi
      ;;
    *)
      echo "unknown release"
      exit 1
      ;;
  esac

  sed -e 's/@//g' < ${spec_in} > ${tmp}/zlog.spec
  $SUDO $builddepcmd ${tmp}/zlog.spec 2>&1 | tee ${tmp}/yum-builddep.out
  ! grep -q -i error: ${tmp}/yum-builddep.out || exit 1

  eval ${gpp_sym}
  eval ${gcc_sym}

  # for doc/build.sh
  $SUDO $yumdnf install -y python-virtualenv
}

function pacman() {
  local tmp=$(mktemp -d)
  trap "rm -rf $tmp" EXIT

  $SUDO pacman -Su

  $SUDO pacman -S boost protobuf cmake lmdb

  exit 0
}

source /etc/os-release
case $ID in
  debian|ubuntu)
    debs
    ;;

  centos|fedora)
    rpms
    ;;

  arch)
    pacman
    ;;

  *)
    echo "$ID not supported. Install dependencies manually."
    exit 1
    ;;
esac
