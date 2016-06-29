TARGETS := memento
TEST_TARGETS := memento_test

COMMON_SOURCES := localstore.cpp \
                  memcachedstore.cpp \
                  memcachedstoreview.cpp \
                  memcached_config.cpp \
                  signalhandler.cpp \
                  logger.cpp \
                  saslogger.cpp \
                  statistic.cpp \
                  utils.cpp \
                  load_monitor.cpp \
                  log.cpp \
                  handlers.cpp \
                  httpstack.cpp \
                  httpstack_utils.cpp \
                  accesslogger.cpp \
                  authstore.cpp \
                  httpconnection.cpp \
                  homesteadconnection.cpp \
                  httpdigestauthenticate.cpp \
                  cassandra_store.cpp \
                  call_list_store.cpp \
                  call_list_xml.cpp \
                  dnsparser.cpp \
                  baseresolver.cpp \
                  dnscachedresolver.cpp \
                  httpresolver.cpp \
                  mementosaslogger.cpp \
                  alarm.cpp \
                  base_communication_monitor.cpp \
                  communicationmonitor.cpp \
                  zmq_lvc.cpp \
                  counter.cpp \
                  accumulator.cpp \
                  memento_lvc.cpp \
                  health_checker.cpp \
                  exception_handler.cpp \
                  namespace_hop.cpp \
                  astaire_resolver.cpp

memento_SOURCES := main.cpp ${COMMON_SOURCES}

memento_test_SOURCES := ${COMMON_SOURCES} \
                        test_main.cpp \
                        test_interposer.cpp \
                        handlers_test.cpp \
                        authstore_test.cpp \
                        call_list_store_test.cpp \
                        homesteadconnection_test.cpp \
                        httpdigestauthenticate_test.cpp \
                        fakelogger.cpp \
                        fakecurl.cpp \
                        fakehomesteadconnection.cpp \
                        mock_sas.cpp \
                        curl_interposer.cpp \
                        pthread_cond_var_helper.cpp

COMMON_CPPFLAGS := -I../include \
                   -I../usr/include \
                   -I../modules/memento-common/include \
                   -I../modules/cpp-common/include \
                   -I../modules/cpp-common/test_utils \
                   -I../modules/sas-client/include \
                   -I../modules/rapidjson/include \
                   -fPIC

memento_CPPFLAGS := ${COMMON_CPPFLAGS}
memento_test_CPPFLAGS := ${COMMON_CPPFLAGS} -DGTEST_USE_OWN_TR1_TUPLE=0 -Wno-write-strings

# We need to add coverage for memento-common here as well
COVERAGE_ROOT := ..
memento_test_COVERAGE_EXCLUSIONS := ^src/ut|^usr|^modules/gmock|^modules/cpp-common|^modules/rapidjson

# Add modules/cpp-common/src as a VPATH to pull in required common modules
VPATH := ../modules/cpp-common/src ../modules/cpp-common/test_utils ut ../modules/memento-common/src

COMMON_LDFLAGS += -lzmq \
                  -L../usr/lib \
                  -lssl \
                  -ldl \
                  -lcrypto \
                  -levhtp \
                  -levent_pthreads \
                  -lmemcached \
                  -levent \
                  -lsas \
                  -lz \
                  -lcurl \
                  -lpthread \
                  -lcares \
                  -lboost_regex \
                  -lthrift \
                  -lcassandra \
                  `net-snmp-config --netsnmp-agent-libs`

memento_LDFLAGS := ${COMMON_LDFLAGS}

# Test build also uses libcurl (to verify HttpStack operation)
memento_test_LDFLAGS := ${COMMON_LDFLAGS} -lcurl

# Special extra objects for memento_test
${BUILD_DIR}/bin/memento_test : ${memento_test_OBJECT_DIR}/curl_interposer.so

${memento_test_OBJECT_DIR}/curl_interposer.so : ../modules/cpp-common/test_utils/curl_interposer.cpp ../modules/cpp-common/test_utils/curl_interposer.hpp ../modules/cpp-common/test_utils/fakecurl.cpp ../modules/cpp-common/test_utils/fakecurl.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(memento_test_CPPFLAGS) -shared -fPIC -ldl $< -o $@
CLEANS += ${memento_test_OBJECT_DIR}/curl_interposer.so

include ../build-infra/cpp.mk

ROOT := ..
MODULE_DIR := ${ROOT}/modules
include ../modules/cpp-common/makefiles/alarm-utils.mk

../usr/include/memento_alarmdefinition.h : ../build/bin/alarm_header ../memento.root/usr/share/clearwater/infrastructure/alarms/memento_alarms.json
	$< -j "../memento.root/usr/share/clearwater/infrastructure/alarms/memento_alarms.json" -n "memento"
	mv memento_alarmdefinition.h $@

${memento_OBJECT_DIR}/main.o : ../usr/include/memento_alarmdefinition.h
