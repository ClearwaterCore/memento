# Top level Makefile for building memento

# this should come first so make does the right thing by default
all: build

ROOT ?= ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

DEB_COMPONENT := memento
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := memento-libs memento-libs-dbg memento memento-dbg memento-nginx

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

SUBMODULES := c-ares libevhtp libmemcached thrift cassandra sas-client openssl

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})
include ${MK_DIR}/memento.mk

build: ${SUBMODULES} memento

test: ${SUBMODULES} memento_test

full_test: ${SUBMODULES} memento_full_test

testall: $(patsubst %, %_test, ${SUBMODULES}) full_test

clean: $(patsubst %, %_clean, ${SUBMODULES}) memento_clean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build
	rm -rf ${ROOT}/*.a

distclean: $(patsubst %, %_distclean, ${SUBMODULES}) memento_distclean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build
	rm -rf ${ROOT}/*.a

include build-infra/cw-deb.mk

.PHONY: deb
deb: build deb-only

.PHONY: object
object: libmemento.a

libmemento.a: build
	ar cr libmemento.a build/obj/memento/*.o

.PHONY: all build test clean distclean object
