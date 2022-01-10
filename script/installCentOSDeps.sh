#!/bin/bash

installYumDeps(){
  ${SUDO} yum groupinstall " Development Tools" "Development Libraries " -y
  ${SUDO} yum install zlib-devel pkgconfig git log4cxx-devel gcc gcc-c++ bzip2 bzip2-devel bzip2-libs python-devel nasm yasm cmake meson ninja-build libconfig-devel -y
  ${SUDO} yum install h libtool -y
  ${SUDO} yum install glib2-devel -y
  ${SUDO} yum install centos-release-scl -y
  ${SUDO} yum install devtoolset-7-gcc* -y
}

installRepo(){
  wget -c http://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
  wget -c http://rpms.famillecollet.com/enterprise/remi-release-7.rpm
  ${SUDO} rpm -Uvh remi-release-7*.rpm epel-release-latest-7*.rpm
  ${SUDO} sed -i 's/https/http/g' /etc/yum.repos.d/epel.repo
  rm *.rpm
}

cleanup(){
  cd $CURRENT_DIR
  rm *rpm*
  cleanup_common
}

