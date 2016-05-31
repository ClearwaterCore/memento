# included mk file for memento

MEMENTO_DIR := ${ROOT}/src
MEMENTO_TEST_DIR := ${ROOT}/tests

memento:
	make -C ${MEMENTO_DIR}

memento_test:
	make -C ${MEMENTO_DIR} test

memento_full_test:
	make -C ${MEMENTO_DIR} full_test

memento_clean:
	make -C ${MEMENTO_DIR} clean

memento_distclean: memento_clean

.PHONY: memento memento_test memento_clean memento_distclean
