#!/bin/sh

# DEBIAN
PACKAGEID="strusutilities-0.0"

cd pkg/$PACKAGEID
dpkg-buildpackage

